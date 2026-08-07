#include "PluginEditor.h"

VirtualLoopbackAudioProcessorEditor::VirtualLoopbackAudioProcessorEditor (VirtualLoopbackAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (540, 320);
    setResizeLimits (500, 300, 900, 600);
    setResizable (true, false);

    // JUCE の String(const char*) は ASCII 専用。日本語は wchar_t / UTF-8 明示が必須。
    refreshButton.setButtonText (juce::String (L"更新"));
    restartButton.setButtonText (juce::String (L"再起動"));
    captureToggle.setButtonText (juce::String (L"キャプチャ"));
    muteToggle.setButtonText (juce::String (L"ミュート"));

    titleLabel.setText ("VirtualLoopback", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    hintLabel.setText (juce::String (L"Chrome などのアプリが使用している再生デバイスを選択してください。\n"
                                     L"SyncRoom / DAW のモニター戻りが出ているデバイスは選ばないでください。"),
                       juce::dontSendNotification);
    hintLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    hintLabel.setJustificationType (juce::Justification::topLeft);
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.85f));
    addAndMakeVisible (hintLabel);

    addAndMakeVisible (deviceBox);
    deviceBox.onChange = [this]
    {
        processor.setSelectedDeviceByIndex (deviceBox.getSelectedItemIndex());
        updateStatus();
    };

    refreshButton.onClick = [this]
    {
        processor.refreshDeviceList();
        rebuildDeviceList();
        updateStatus();
    };
    addAndMakeVisible (refreshButton);

    restartButton.onClick = [this]
    {
        processor.restartCapture();
        updateStatus();
    };
    addAndMakeVisible (restartButton);

    captureToggle.setToggleState (processor.captureEnabledParam->get(), juce::dontSendNotification);
    captureToggle.onClick = [this]
    {
        *processor.captureEnabledParam = captureToggle.getToggleState();
        processor.restartCapture();
        updateStatus();
    };
    addAndMakeVisible (captureToggle);

    muteToggle.setToggleState (processor.muteParam->get(), juce::dontSendNotification);
    muteToggle.onClick = [this]
    {
        *processor.muteParam = muteToggle.getToggleState();
    };
    addAndMakeVisible (muteToggle);

    volumeLabel.setText (juce::String (L"音量"), juce::dontSendNotification);
    volumeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (volumeLabel);

    volumeSlider.setRange (0.0, 1.0, 0.01);
    volumeSlider.setValue (processor.volumeParam->get(), juce::dontSendNotification);
    volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    volumeSlider.onValueChange = [this]
    {
        *processor.volumeParam = (float) volumeSlider.getValue();
    };
    addAndMakeVisible (volumeSlider);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (statusLabel);

    meterLabel.setText (juce::String (L"出力レベル"), juce::dontSendNotification);
    meterLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    meterLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (meterLabel);

    rebuildDeviceList();
    updateStatus();
    startTimerHz (12);
}

VirtualLoopbackAudioProcessorEditor::~VirtualLoopbackAudioProcessorEditor()
{
    stopTimer();
}

void VirtualLoopbackAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1f24));

    if (! meterBounds.isEmpty())
    {
        g.setColour (juce::Colour (0xff2c2f38));
        g.fillRoundedRectangle (meterBounds.toFloat(), 3.0f);

        auto filled = meterBounds;
        filled.setWidth (juce::jlimit (0, meterBounds.getWidth(),
                                       (int) std::round (meterLevel * (float) meterBounds.getWidth())));
        g.setColour (meterLevel > 0.9f ? juce::Colours::red.brighter (0.2f)
                                       : juce::Colour (0xff5ad67c));
        g.fillRoundedRectangle (filled.toFloat(), 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawRoundedRectangle (meterBounds.toFloat(), 3.0f, 1.0f);
    }
}

void VirtualLoopbackAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto meterBlock = r.removeFromBottom (36);
    meterLabel.setBounds (meterBlock.removeFromTop (16));
    meterBlock.removeFromTop (4);
    meterBounds = meterBlock.removeFromTop (14);

    r.removeFromBottom (10);

    titleLabel.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    hintLabel.setBounds (r.removeFromTop (48));
    r.removeFromTop (10);

    auto row = r.removeFromTop (30);
    const int buttonW = 72;
    restartButton.setBounds (row.removeFromRight (buttonW));
    row.removeFromRight (8);
    refreshButton.setBounds (row.removeFromRight (buttonW));
    row.removeFromRight (8);
    deviceBox.setBounds (row);

    r.removeFromTop (12);
    auto toggles = r.removeFromTop (28);
    captureToggle.setBounds (toggles.removeFromLeft (120));
    muteToggle.setBounds (toggles.removeFromLeft (100));

    r.removeFromTop (10);
    auto vol = r.removeFromTop (28);
    volumeLabel.setBounds (vol.removeFromLeft (48));
    volumeSlider.setBounds (vol);

    r.removeFromTop (10);
    statusLabel.setBounds (r.removeFromTop (24));
}

void VirtualLoopbackAudioProcessorEditor::rebuildDeviceList()
{
    deviceBox.clear (juce::dontSendNotification);
    const auto names = processor.getRenderDeviceNames();
    for (int i = 0; i < names.size(); ++i)
        deviceBox.addItem (names[i], i + 1);

    const int idx = processor.getSelectedDeviceIndex();
    if (idx >= 0)
        deviceBox.setSelectedItemIndex (idx, juce::dontSendNotification);
}

void VirtualLoopbackAudioProcessorEditor::updateStatus()
{
    statusLabel.setText (processor.getCaptureStatusText(), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           processor.isCaptureRunning() ? juce::Colour (0xff5ad67c)
                                                        : juce::Colour (0xffffb454));
}

void VirtualLoopbackAudioProcessorEditor::timerCallback()
{
    meterLevel = 0.75f * meterLevel + 0.25f * processor.getInputPeak();
    updateStatus();
    captureToggle.setToggleState (processor.captureEnabledParam->get(), juce::dontSendNotification);
    muteToggle.setToggleState (processor.muteParam->get(), juce::dontSendNotification);
    repaint (meterBounds.expanded (2));
}
