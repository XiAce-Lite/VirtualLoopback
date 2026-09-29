#pragma once

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include "WasapiLoopbackCapture.h"
#elif JUCE_MAC
 #include "CoreAudioTapCapture.h"
#endif

#if JUCE_WINDOWS || JUCE_MAC
 #define VIRTUALLOOPBACK_HAS_CAPTURE 1
#else
 #define VIRTUALLOOPBACK_HAS_CAPTURE 0
#endif

//==============================================================================
class VirtualLoopbackAudioProcessor : public juce::AudioProcessor
{
public:
#if JUCE_WINDOWS
    using DeviceInfo = WasapiLoopbackCapture::DeviceInfo;
#elif JUCE_MAC
    using DeviceInfo = CoreAudioTapCapture::DeviceInfo;
#else
    struct DeviceInfo
    {
        juce::String id;
        juce::String name;
        bool isDefault = false;
    };
#endif

    VirtualLoopbackAudioProcessor();
    ~VirtualLoopbackAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioParameterFloat* volumeParam = nullptr;
    juce::AudioParameterBool* muteParam = nullptr;
    juce::AudioParameterBool* captureEnabledParam = nullptr;

    juce::StringArray getRenderDeviceNames() const;
    const juce::Array<DeviceInfo>& getDevices() const { return devices; }
    juce::String getSelectedDeviceId() const { return selectedDeviceId; }
    juce::String getSelectedDeviceName() const;
    int getSelectedDeviceIndex() const;
    void setSelectedDeviceByIndex (int index);
    void setSelectedDeviceId (const juce::String& deviceId);
    void refreshDeviceList();
    bool restartCapture();
    /** If capture is enabled but not running (e.g. app started after load), try again.
        Returns true if the device list UI should be rebuilt. */
    bool retryCaptureIfNeeded();

    bool isCaptureRunning() const;
    bool isSelectedTargetUnavailable() const;
    juce::String getCaptureStatusText() const;
    float getInputPeak() const;

private:
    void resetResamplerState();
    void pullIntoPending();
    void rememberSelectedLabelFromDevices();
    void ensureSelectedTargetVisible();
    static bool isSystemMixTargetId (const juce::String& id);
    static bool isAppCaptureTargetId (const juce::String& id);
    static juce::String stripStatusSuffix (const juce::String& name);

    double hostSampleRate = 44100.0;
    int hostBlockSize = 512;

    juce::String selectedDeviceId;
    juce::String selectedDeviceLabel; // display name without （未起動） etc.
    juce::Array<DeviceInfo> devices;

#if JUCE_WINDOWS
    WasapiLoopbackCapture capture;
#elif JUCE_MAC
    CoreAudioTapCapture capture;
#endif

    juce::AudioBuffer<float> pendingCapture; // planar, capture-rate domain
    int pendingFrames = 0;
    double readPos = 0.0; // fractional index into pendingCapture

    juce::AudioBuffer<float> pullScratch;
    std::atomic<float> uiPeak { 0.0f };

    bool outputPrimed = false;
    double adaptiveRatioScale = 1.0;
    float lastSample[2] { 0.0f, 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualLoopbackAudioProcessor)
};
