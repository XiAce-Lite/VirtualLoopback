#include "PluginEditor.h"
#include "UiColours.h"

#if JUCE_WINDOWS
 #include "ProcessAllowlistDialog.h"
#endif

namespace
{
    class VolumeSliderLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                               const juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
            {
                LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                 0.0f, 0.0f, style, slider);
                return;
            }

            auto trackBounds = juce::Rectangle<float> ((float) x,
                                                       (float) y + (float) height * 0.35f,
                                                       (float) width,
                                                       (float) height * 0.30f);

            g.setColour (VlUi::panel());
            g.fillRoundedRectangle (trackBounds, 3.0f);

            // Fill from the left edge up to the thumb — natural volume semantics.
            const float fillRight = juce::jlimit (trackBounds.getX(),
                                                  trackBounds.getRight(),
                                                  sliderPos);
            auto filled = trackBounds.withRight (fillRight);
            g.setColour (VlUi::accent());
            g.fillRoundedRectangle (filled, 3.0f);

            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillEllipse (sliderPos - 6.0f, (float) y + (float) height * 0.5f - 6.0f, 12.0f, 12.0f);
        }
    };
}

VirtualLoopbackAudioProcessorEditor::VirtualLoopbackAudioProcessorEditor (VirtualLoopbackAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (560, 360);
    setResizeLimits (520, 320, 900, 640);
    setResizable (true, false);

    volumeLookAndFeel = std::make_unique<VolumeSliderLookAndFeel>();

    // JUCE の String(const char*) は ASCII 専用。日本語は wchar_t / UTF-8 明示が必須。
    refreshButton.setButtonText (juce::String (L"更新"));
    restartButton.setButtonText (juce::String (L"再起動"));
    allowlistButton.setButtonText (juce::String (L"追加プロセス…"));
    captureToggle.setButtonText (juce::String (L"キャプチャ"));
    muteToggle.setButtonText (juce::String (L"ミュート"));

    titleLabel.setText ("VirtualLoopback", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

#if JUCE_MAC
    hintLabel.setText (juce::String (L"既定は「システム再生音」。一覧の「アプリ: …」で Chrome など個別にも取れます。\n"
                                     L"初回は「画面収録とシステムオーディオ」で、使っている DAW を許可してください。"),
                       juce::dontSendNotification);
#else
    hintLabel.setText (juce::String (L"既定は「システム再生音」。再生中のアプリは「アプリ: …」で個別に選べます。\n"
                                     L"セッションが無いアプリは「追加プロセス…」でプロセス名を登録できます。"),
                       juce::dontSendNotification);
#endif
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

#if JUCE_WINDOWS
    allowlistButton.onClick = [this] { openAllowlistDialog(); };
    addAndMakeVisible (allowlistButton);
#else
    allowlistButton.setVisible (false);
#endif

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

    volumeSlider.setLookAndFeel (volumeLookAndFeel.get());
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
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

    meterLabel.setText (juce::String (L"出力レベル (dB)"), juce::dontSendNotification);
    meterLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    meterLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    addAndMakeVisible (meterLabel);

    rebuildDeviceList();
    updateStatus();
    startTimerHz (12);
}

VirtualLoopbackAudioProcessorEditor::~VirtualLoopbackAudioProcessorEditor()
{
    volumeSlider.setLookAndFeel (nullptr);
    stopTimer();
}

void VirtualLoopbackAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (VlUi::background());

    if (! meterBounds.isEmpty())
    {
        g.setColour (VlUi::panel());
        g.fillRoundedRectangle (meterBounds.toFloat(), 3.0f);

        auto filled = meterBounds;
        filled.setWidth (juce::jlimit (0, meterBounds.getWidth(),
                                       (int) std::round (meterLevel * (float) meterBounds.getWidth())));
        g.setColour (meterLevel > 0.95f ? juce::Colours::red.brighter (0.2f)
                                       : VlUi::accent());
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
    hintLabel.setBounds (r.removeFromTop (56));
    r.removeFromTop (10);

    auto row = r.removeFromTop (30);
    const int buttonW = 72;
    restartButton.setBounds (row.removeFromRight (buttonW));
    row.removeFromRight (8);
    refreshButton.setBounds (row.removeFromRight (buttonW));
    row.removeFromRight (8);
    deviceBox.setBounds (row);

    r.removeFromTop (10);
#if JUCE_WINDOWS
    allowlistButton.setBounds (r.removeFromTop (26).removeFromLeft (140));
    r.removeFromTop (8);
#endif

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

void VirtualLoopbackAudioProcessorEditor::openAllowlistDialog()
{
#if JUCE_WINDOWS
    auto* content = new ProcessAllowlistDialog();
    content->onSaved = [this]
    {
        processor.refreshDeviceList();
        rebuildDeviceList();
        updateStatus();
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (content);
    opts.dialogTitle = "VirtualLoopback";
    opts.dialogBackgroundColour = VlUi::background();
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
#endif
}

void VirtualLoopbackAudioProcessorEditor::rebuildDeviceList()
{
    const auto previousSelectedId = processor.getSelectedDeviceId();

    deviceBox.clear (juce::dontSendNotification);
    const auto names = processor.getRenderDeviceNames();
    for (int i = 0; i < names.size(); ++i)
        deviceBox.addItem (names[i], i + 1);

    const int idx = processor.getSelectedDeviceIndex();
    if (idx >= 0)
        deviceBox.setSelectedItemIndex (idx, juce::dontSendNotification);

    // Do not fall back to item 0 visually — that made it look like system mix
    // while the saved app selection was still active.
    if (previousSelectedId == processor.getSelectedDeviceId())
        deviceBox.repaint();
}

void VirtualLoopbackAudioProcessorEditor::updateStatus()
{
    statusLabel.setText (processor.getCaptureStatusText(), juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           processor.isCaptureRunning() ? VlUi::accent()
                                                        : VlUi::statusIdle());
}

void VirtualLoopbackAudioProcessorEditor::timerCallback()
{
    // Map peak to a DAW-like dB meter (-60 dB .. 0 dB). Linear amplitude
    // made the bar look quieter than Cubase/Logic track meters.
    const float peak = processor.getInputPeak();
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float dbNorm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    meterLevel = 0.75f * meterLevel + 0.25f * dbNorm;

    // ~2s at 12 Hz: if the chosen app starts playing after load, pick it up
    // without forcing the user to toggle device → app.
    if (++captureRetryCounter >= 24)
    {
        captureRetryCounter = 0;
        if (processor.retryCaptureIfNeeded())
            rebuildDeviceList();
    }

    updateStatus();
    captureToggle.setToggleState (processor.captureEnabledParam->get(), juce::dontSendNotification);
    muteToggle.setToggleState (processor.muteParam->get(), juce::dontSendNotification);
    repaint (meterBounds.expanded (2));
}
