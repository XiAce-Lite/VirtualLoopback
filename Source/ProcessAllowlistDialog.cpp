#include "ProcessAllowlistDialog.h"

#if JUCE_WINDOWS

#include "AppProcessAllowlist.h"
#include "UiColours.h"

ProcessAllowlistDialog::ProcessAllowlistDialog()
{
    titleLabel.setText (juce::String (L"追加プロセス一覧"), juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    hintLabel.setText (juce::String (L"オーディオセッションが無くても、起動中なら一覧に出します。\n"
                                     L"候補はアプリ名表示（保存はプロセス名）。足りなければ下のトグルか手入力。"),
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
    editor.setColour (juce::TextEditor::backgroundColourId, VlUi::panel());
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

    broaderToggle.setButtonText (juce::String (L"インストール済みアプリも候補に含める（ヘルパー等は除く）"));
    broaderToggle.setColour (juce::ToggleButton::textColourId, juce::Colours::white.withAlpha (0.9f));
    broaderToggle.onClick = [this] { refreshRunningProcesses(); };
    addAndMakeVisible (broaderToggle);

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
    setSize (500, 460);
}

void ProcessAllowlistDialog::paint (juce::Graphics& g)
{
    g.fillAll (VlUi::background());
}

void ProcessAllowlistDialog::resized()
{
    auto r = getLocalBounds().reduced (14);
    titleLabel.setBounds (r.removeFromTop (24));
    r.removeFromTop (6);
    hintLabel.setBounds (r.removeFromTop (40));
    r.removeFromTop (8);
    enabledToggle.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);
    editor.setBounds (r.removeFromTop (150));
    r.removeFromTop (10);

    auto addRow = r.removeFromTop (28);
    runningLabel.setBounds (addRow.removeFromLeft (110));
    addButton.setBounds (addRow.removeFromRight (64));
    addRow.removeFromRight (8);
    runningBox.setBounds (addRow);

    r.removeFromTop (8);
    broaderToggle.setBounds (r.removeFromTop (24));
    r.removeFromTop (10);
    pathLabel.setBounds (r.removeFromTop (36));
    r.removeFromTop (8);

    auto buttons = r.removeFromTop (28);
    cancelButton.setBounds (buttons.removeFromRight (80));
    buttons.removeFromRight (8);
    saveButton.setBounds (buttons.removeFromRight (80));
}

void ProcessAllowlistDialog::loadFromSettings()
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

void ProcessAllowlistDialog::refreshRunningProcesses()
{
    runningBox.clear (juce::dontSendNotification);
    runningProcessNames.clear();

    const auto apps = AppProcessAllowlist::getRunningApps (broaderToggle.getToggleState());
    for (int i = 0; i < apps.size(); ++i)
    {
        const auto& app = apps.getReference (i);
        runningProcessNames.add (app.processName);

        auto label = app.displayName;
        if (! label.equalsIgnoreCase (app.processName))
            label << "  (" << app.processName << ")";

        runningBox.addItem (label, i + 1);
    }

    if (runningBox.getNumItems() > 0)
        runningBox.setSelectedItemIndex (0, juce::dontSendNotification);
}

void ProcessAllowlistDialog::addSelectedRunningProcess()
{
    const int idx = runningBox.getSelectedItemIndex();
    if (! juce::isPositiveAndBelow (idx, runningProcessNames.size()))
        return;

    const auto name = runningProcessNames[idx].trim();
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

void ProcessAllowlistDialog::saveAndClose()
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

#endif // JUCE_WINDOWS
