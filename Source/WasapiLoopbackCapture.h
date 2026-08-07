#pragma once

#include <JuceHeader.h>
#include <thread>
#include <atomic>

#if JUCE_WINDOWS

//==============================================================================
/** Captures the mix playing on a Windows render (playback) endpoint via WASAPI loopback. */
class WasapiLoopbackCapture
{
public:
    struct DeviceInfo
    {
        juce::String id;
        juce::String name;
        bool isDefault = false;
    };

    WasapiLoopbackCapture();
    ~WasapiLoopbackCapture();

    static juce::Array<DeviceInfo> getRenderDevices();

    /** Start capturing from the given IMMDevice ID. Empty ID = default render device. */
    bool start (const juce::String& deviceId);

    void stop();

    bool isRunning() const noexcept { return running.load(); }

    juce::String getLastError() const;

    double getCaptureSampleRate() const noexcept { return captureSampleRate.load(); }
    int getCaptureNumChannels() const noexcept { return captureNumChannels.load(); }

    /** Frames currently waiting in the capture FIFO. */
    int getNumReady() const noexcept
    {
        const juce::ScopedLock sl (fifoLock);
        return fifo.getNumReady();
    }

    int getFifoCapacity() const noexcept { return fifo.getTotalSize(); }

    /** Pull planar float samples at the capture sample rate into dest.
        Returns the number of frames actually read (may be less than numFrames). */
    int read (juce::AudioBuffer<float>& dest, int numFrames);

    float getPeakLevel() const noexcept { return peakLevel.load(); }

private:
    void captureThreadFn();
    bool openDevice (const juce::String& deviceId);
    void closeDevice();
    void pushCapturedFrames (const float* interleaved, int numFrames, int numCh);
    void discardOldestFramesUnlocked (int numFrames);

    std::atomic<bool> running { false };
    std::atomic<bool> shouldStop { false };
    std::atomic<double> captureSampleRate { 0.0 };
    std::atomic<int> captureNumChannels { 0 };
    std::atomic<float> peakLevel { 0.0f };

    mutable juce::CriticalSection errorLock;
    juce::String lastError;
    juce::String pendingDeviceId;
    juce::String activeDeviceId;

    std::thread captureThread;

    // ~2s at 48k — room for clock drift between WASAPI and ASIO
    static constexpr int fifoFrames = 96000;
    mutable juce::CriticalSection fifoLock;
    juce::AbstractFifo fifo { fifoFrames };
    juce::AudioBuffer<float> ring { 2, fifoFrames };

    struct NativeState;
    std::unique_ptr<NativeState> native;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WasapiLoopbackCapture)
};

#endif // JUCE_WINDOWS
