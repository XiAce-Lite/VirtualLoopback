#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class VirtualLoopbackAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit VirtualLoopbackAudioProcessorEditor (VirtualLoopbackAudioProcessor&);
    ~VirtualLoopbackAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildDeviceList();
    void updateStatus();

    VirtualLoopbackAudioProcessor& processor;

    juce::ComboBox deviceBox;
    juce::TextButton refreshButton;
    juce::TextButton restartButton;
    juce::ToggleButton captureToggle;
    juce::ToggleButton muteToggle;
    juce::Slider volumeSlider;
    juce::Label volumeLabel;
    juce::Label statusLabel;
    juce::Label hintLabel;
    juce::Label titleLabel;
    juce::Label meterLabel;

    juce::Rectangle<int> meterBounds;
    float meterLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualLoopbackAudioProcessorEditor)
};
