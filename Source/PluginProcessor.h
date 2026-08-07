#pragma once

#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include "WasapiLoopbackCapture.h"
#endif

//==============================================================================
class VirtualLoopbackAudioProcessor : public juce::AudioProcessor
{
public:
#if JUCE_WINDOWS
    using DeviceInfo = WasapiLoopbackCapture::DeviceInfo;
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

    bool isCaptureRunning() const;
    juce::String getCaptureStatusText() const;
    float getInputPeak() const;

private:
    void resetResamplerState();
    void pullIntoPending();

    double hostSampleRate = 44100.0;
    int hostBlockSize = 512;

    juce::String selectedDeviceId;
    juce::Array<DeviceInfo> devices;

#if JUCE_WINDOWS
    WasapiLoopbackCapture capture;
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
