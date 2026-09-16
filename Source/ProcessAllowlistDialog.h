#pragma once

#include <JuceHeader.h>
#include "AppProcessAllowlist.h"

#if JUCE_WINDOWS

//==============================================================================
class ProcessAllowlistDialog final : public juce::Component
{
public:
    ProcessAllowlistDialog()
    {
        titleLabel.setText (juce::String (L"追加プロセス一覧"), juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (titleLabel);

        hintLabel.setText (juce::String (L"オーディオセッションが無くても、起動中なら一覧に出します。\n"
                                         L"下の候補はウィンドウのあるアプリのみ。無ければ名前を手入力（拡張子なし）。"),
                           juce::dontSendNotification);
        hintLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
        hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        hintLabel.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (hintLabel);

        enabledToggle.setButtonText (juce::String (L"追加プロセス一覧を使う"));
        enabledToggle.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        addAndMakeVisible (enabledToggle);

        editor.setMultiLine (true, true);
        editor.setReturnKeyStartsNewLine (true);
        editor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2c2f38));
        editor.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.2f));
        addAndMakeVisible (editor);

        runningLabel.setText (juce::String (L"アプリから追加"), juce::dontSendNotification);
        runningLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
        addAndMakeVisible (runningLabel);

        addAndMakeVisible (runningBox);
        addButton.setButtonText (juce::String (L"追加"));
        addButton.onClick = [this] { addSelectedRunningProcess(); };
        addAndMakeVisible (addButton);

        saveButton.setButtonText (juce::String (L"保存"));
        saveButton.onClick = [this] { saveAndClose(); };
        addAndMakeVisible (saveButton);

        cancelButton.setButtonText (juce::String (L"閉じる"));
        cancelButton.onClick = [this]
        {
            if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                dw->exitModalState (0);
        };
        addAndMakeVisible (cancelButton);

        pathLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
        pathLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        addAndMakeVisible (pathLabel);

        loadFromSettings();
        refreshRunningProcesses();
        setSize (460, 420);
    }

    std::function<void()> onSaved;

private:
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1f24));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14);
        titleLabel.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        hintLabel.setBounds (r.removeFromTop (40));
        r.removeFromTop (8);
        enabledToggle.setBounds (r.removeFromTop (24));
        r.removeFromTop (8);
        editor.setBounds (r.removeFromTop (160));
        r.removeFromTop (10);

        auto addRow = r.removeFromTop (28);
        runningLabel.setBounds (addRow.removeFromLeft (110));
        addButton.setBounds (addRow.removeFromRight (64));
        addRow.removeFromRight (8);
        runningBox.setBounds (addRow);

        r.removeFromTop (12);
        pathLabel.setBounds (r.removeFromTop (36));
        r.removeFromTop (8);

        auto buttons = r.removeFromTop (28);
        cancelButton.setBounds (buttons.removeFromRight (80));
        buttons.removeFromRight (8);
        saveButton.setBounds (buttons.removeFromRight (80));
    }

    void loadFromSettings()
    {
        auto& allowlist = AppProcessAllowlist::get();
        allowlist.reload();
        enabledToggle.setToggleState (allowlist.isEnabled(), juce::dontSendNotification);

        auto text = allowlist.getProcessNamesAsText();
        if (text.isEmpty() && ! allowlist.getSettingsFile().existsAsFile())
            text = "SyncRoomChatToolV2";

        editor.setText (text, false);
        pathLabel.setText (juce::String (L"保存先: ") + allowlist.getSettingsFile().getFullPathName(),
                           juce::dontSendNotification);
    }

    void refreshRunningProcesses()
    {
        runningBox.clear (juce::dontSendNotification);
        const auto names = AppProcessAllowlist::getRunningProcessNames();
        for (int i = 0; i < names.size(); ++i)
            runningBox.addItem (names[i], i + 1);

        if (runningBox.getNumItems() > 0)
            runningBox.setSelectedItemIndex (0, juce::dontSendNotification);
    }

    void addSelectedRunningProcess()
    {
        const auto name = runningBox.getText().trim();
        if (name.isEmpty())
            return;

        auto text = editor.getText().trimEnd();
        juce::StringArray lines;
        lines.addLines (text);
        for (auto& line : lines)
            line = line.trim();

        if (! lines.contains (name, true))
        {
            if (text.isNotEmpty() && ! text.endsWithChar ('\n'))
                text << "\n";
            text << name;
            editor.setText (text, false);
        }
    }

    void saveAndClose()
    {
        auto& allowlist = AppProcessAllowlist::get();
        allowlist.setEnabled (enabledToggle.getToggleState());
        allowlist.setProcessNamesFromText (editor.getText());

        if (! allowlist.save())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                    "VirtualLoopback",
                                                    juce::String (L"設定ファイルの保存に失敗しました。"));
            return;
        }

        if (onSaved)
            onSaved();

        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (1);
    }

    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::ToggleButton enabledToggle;
    juce::TextEditor editor;
    juce::Label runningLabel;
    juce::ComboBox runningBox;
    juce::TextButton addButton;
    juce::TextButton saveButton;
    juce::TextButton cancelButton;
    juce::Label pathLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessAllowlistDialog)
};

#endif // JUCE_WINDOWS
