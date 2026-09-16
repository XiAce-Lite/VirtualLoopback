#pragma once

#include <JuceHeader.h>
#include <thread>
#include <atomic>

#if JUCE_WINDOWS

//==============================================================================
/** Captures Windows playback via WASAPI endpoint loopback or Application Loopback
    (per-process, Windows 10 build 19041+). */
class WasapiLoopbackCapture
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

    WasapiLoopbackCapture();
    ~WasapiLoopbackCapture();

    static juce::Array<DeviceInfo> getRenderDevices();

    /** Start capturing.
        - Empty / systemMixId: default render endpoint loopback (system mix)
        - pidIdPrefix + pid, or appIdPrefix + exe key: Application Loopback
        - otherwise: IMMDevice ID for endpoint loopback
    */
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
    bool openEndpointLoopback (const juce::String& deviceId);
    bool openProcessLoopback (juce::uint32 processId);
    void closeDevice();
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
