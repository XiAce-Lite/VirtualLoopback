#pragma once

#include <JuceHeader.h>

#if JUCE_WINDOWS

//==============================================================================
class ProcessAllowlistDialog final : public juce::Component
{
public:
    ProcessAllowlistDialog();

    std::function<void()> onSaved;

private:
    void paint (juce::Graphics& g) override;
    void resized() override;

    void loadFromSettings();
    void refreshRunningProcesses();
    void addSelectedRunningProcess();
    void saveAndClose();

    juce::Label titleLabel;
    juce::Label hintLabel;
    juce::ToggleButton enabledToggle;
    juce::TextEditor editor;
    juce::Label runningLabel;
    juce::ComboBox runningBox;
    juce::StringArray runningProcessNames;
    juce::TextButton addButton;
    juce::ToggleButton broaderToggle;
    juce::TextButton saveButton;
    juce::TextButton cancelButton;
    juce::Label pathLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProcessAllowlistDialog)
};

#endif // JUCE_WINDOWS
