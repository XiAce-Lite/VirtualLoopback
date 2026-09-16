#pragma once

#include <JuceHeader.h>
#include <thread>
#include <atomic>

#if JUCE_MAC

//==============================================================================
/** Captures macOS system/app playback via Core Audio Process Tap (macOS 14.2+).
    Does not install a HAL virtual device (not BlackHole / Loopback).
*/
class CoreAudioTapCapture
{
public:
    static constexpr const char* systemMixId = "__system__";
    static constexpr const char* appIdPrefix = "__app__:";
    static constexpr const char* pidIdPrefix = "__pid__:";

    struct DeviceInfo
    {
        juce::String id;
        juce::String name;
        bool isDefault = false;
    };

    CoreAudioTapCapture();
    ~CoreAudioTapCapture();

    static juce::Array<DeviceInfo> getRenderDevices();

    /** Start capturing.
        - Empty / systemMixId: global system mix (DAW / SYNCROOM excluded)
        - appIdPrefix + bundleId, or pidIdPrefix + pid: that process only
        - otherwise: Core Audio output-device UID
    */
    bool start (const juce::String& deviceId);

    void stop();

    bool isRunning() const noexcept { return running.load(); }

    juce::String getLastError() const;

    double getCaptureSampleRate() const noexcept { return captureSampleRate.load(); }
    int getCaptureNumChannels() const noexcept { return captureNumChannels.load(); }

    int getNumReady() const noexcept
    {
        const juce::ScopedLock sl (fifoLock);
        return fifo.getNumReady();
    }

    int getFifoCapacity() const noexcept { return fifo.getTotalSize(); }

    int read (juce::AudioBuffer<float>& dest, int numFrames);

    float getPeakLevel() const noexcept { return peakLevel.load(); }

    /** HAL IOProc entry. Do not call from the plugin UI or processBlock. */
    void processTapInput (const void* inputData);

private:
    void captureThreadFn();
    bool openTap (const juce::String& deviceId);
    void closeTap();
    void pushCapturedFrames (const float* interleaved, int numFrames, int numCh);
    void discardOldestFramesUnlocked (int numFrames);
    void setError (const juce::String& text);

    std::atomic<bool> running { false };
    std::atomic<bool> shouldStop { false };
    std::atomic<double> captureSampleRate { 0.0 };
    std::atomic<int> captureNumChannels { 0 };
    std::atomic<float> peakLevel { 0.0f };

    mutable juce::CriticalSection errorLock;
    juce::String lastError;
    juce::String pendingDeviceId;

    std::thread captureThread;

    static constexpr int fifoFrames = 96000;
    mutable juce::CriticalSection fifoLock;
    juce::AbstractFifo fifo { fifoFrames };
    juce::AudioBuffer<float> ring { 2, fifoFrames };

    struct NativeState;
    std::unique_ptr<NativeState> native;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CoreAudioTapCapture)
};

#endif // JUCE_MAC
