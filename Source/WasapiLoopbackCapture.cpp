#include "WasapiLoopbackCapture.h"

#if JUCE_WINDOWS

#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <ksmedia.h>
#include <cstdint>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace
{
    juce::String hresultToString (HRESULT hr)
    {
        return "HRESULT 0x" + juce::String::toHexString ((juce::int64) (juce::uint32) hr);
    }
}

template <typename T>
class WasapiComPtr
{
public:
    WasapiComPtr() = default;
    ~WasapiComPtr() { reset(); }

    WasapiComPtr (const WasapiComPtr&) = delete;
    WasapiComPtr& operator= (const WasapiComPtr&) = delete;

    WasapiComPtr (WasapiComPtr&& other) noexcept : ptr (other.ptr) { other.ptr = nullptr; }

    WasapiComPtr& operator= (WasapiComPtr&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    void reset()
    {
        if (ptr != nullptr)
        {
            ptr->Release();
            ptr = nullptr;
        }
    }

    T** resetAndGetAddressOf()
    {
        reset();
        return &ptr;
    }

    T* get() const noexcept { return ptr; }
    T* operator->() const noexcept { return ptr; }
    explicit operator bool() const noexcept { return ptr != nullptr; }

private:
    T* ptr = nullptr;
};

namespace
{
    juce::String getDevicePropertyString (IMMDevice* device, const PROPERTYKEY& key)
    {
        if (device == nullptr)
            return {};

        WasapiComPtr<IPropertyStore> props;
        if (FAILED (device->OpenPropertyStore (STGM_READ, props.resetAndGetAddressOf())))
            return {};

        PROPVARIANT var;
        PropVariantInit (&var);

        juce::String result;
        if (SUCCEEDED (props->GetValue (key, &var)) && var.vt == VT_LPWSTR && var.pwszVal != nullptr)
            result = juce::String (var.pwszVal);

        PropVariantClear (&var);
        return result;
    }
}

struct WasapiLoopbackCapture::NativeState
{
    WasapiComPtr<IMMDeviceEnumerator> enumerator;
    WasapiComPtr<IMMDevice> device;
    WasapiComPtr<IAudioClient> audioClient;
    WasapiComPtr<IAudioCaptureClient> captureClient;
    WAVEFORMATEX* mixFormat = nullptr;
    HANDLE eventHandle = nullptr;

    ~NativeState() { close(); }

    void close()
    {
        if (audioClient)
            audioClient->Stop();

        captureClient.reset();
        audioClient.reset();
        device.reset();
        enumerator.reset();

        if (mixFormat != nullptr)
        {
            CoTaskMemFree (mixFormat);
            mixFormat = nullptr;
        }

        if (eventHandle != nullptr)
        {
            CloseHandle (eventHandle);
            eventHandle = nullptr;
        }
    }
};

WasapiLoopbackCapture::WasapiLoopbackCapture()
    : native (std::make_unique<NativeState>())
{
}

WasapiLoopbackCapture::~WasapiLoopbackCapture()
{
    stop();
}

juce::String WasapiLoopbackCapture::getLastError() const
{
    const juce::ScopedLock sl (errorLock);
    return lastError;
}

juce::Array<WasapiLoopbackCapture::DeviceInfo> WasapiLoopbackCapture::getRenderDevices()
{
    juce::Array<DeviceInfo> list;

    const HRESULT coHr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    const bool shouldUninit = (coHr == S_OK);

    WasapiComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof (IMMDeviceEnumerator),
                                  (void**) enumerator.resetAndGetAddressOf())))
    {
        if (shouldUninit)
            CoUninitialize();
        return list;
    }

    juce::String defaultId;
    {
        WasapiComPtr<IMMDevice> defDev;
        if (SUCCEEDED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole,
                                                            defDev.resetAndGetAddressOf())))
        {
            LPWSTR id = nullptr;
            if (SUCCEEDED (defDev->GetId (&id)) && id != nullptr)
            {
                defaultId = juce::String (id);
                CoTaskMemFree (id);
            }
        }
    }

    WasapiComPtr<IMMDeviceCollection> collection;
    if (FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE,
                                                collection.resetAndGetAddressOf())))
    {
        if (shouldUninit)
            CoUninitialize();
        return list;
    }

    UINT count = 0;
    collection->GetCount (&count);

    for (UINT i = 0; i < count; ++i)
    {
        WasapiComPtr<IMMDevice> device;
        if (FAILED (collection->Item (i, device.resetAndGetAddressOf())) || ! device)
            continue;

        LPWSTR id = nullptr;
        if (FAILED (device->GetId (&id)) || id == nullptr)
            continue;

        DeviceInfo info;
        info.id = juce::String (id);
        CoTaskMemFree (id);

        info.name = getDevicePropertyString (device.get(), PKEY_Device_FriendlyName);
        if (info.name.isEmpty())
            info.name = info.id;

        info.isDefault = (info.id == defaultId);
        list.add (info);
    }

    if (shouldUninit)
        CoUninitialize();

    return list;
}

bool WasapiLoopbackCapture::start (const juce::String& deviceId)
{
    stop();

    {
        const juce::ScopedLock sl (errorLock);
        lastError.clear();
    }

    pendingDeviceId = deviceId;
    shouldStop = false;
    running = true;
    captureThread = std::thread ([this] { captureThreadFn(); });

    for (int i = 0; i < 50; ++i)
    {
        if (captureSampleRate.load() > 0.0)
            return true;

        {
            const juce::ScopedLock sl (errorLock);
            if (lastError.isNotEmpty() && ! running.load())
                break;
        }

        juce::Thread::sleep (10);
    }

    {
        const juce::ScopedLock sl (errorLock);
        if (lastError.isEmpty() && captureSampleRate.load() <= 0.0)
            lastError = "Timed out opening WASAPI loopback device";
    }

    if (captureSampleRate.load() <= 0.0)
    {
        shouldStop = true;
        if (captureThread.joinable())
            captureThread.join();
        running = false;
        return false;
    }

    return true;
}

void WasapiLoopbackCapture::stop()
{
    shouldStop = true;

    if (native != nullptr && native->eventHandle != nullptr)
        SetEvent (native->eventHandle);

    if (captureThread.joinable())
        captureThread.join();

    running = false;
    closeDevice();
    peakLevel = 0.0f;
}

bool WasapiLoopbackCapture::openDevice (const juce::String& deviceId)
{
    native->close();

    HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof (IMMDeviceEnumerator),
                                   (void**) native->enumerator.resetAndGetAddressOf());
    if (FAILED (hr))
    {
        const juce::ScopedLock sl (errorLock);
        lastError = "MMDeviceEnumerator failed: " + hresultToString (hr);
        return false;
    }

    if (deviceId.isNotEmpty())
    {
        hr = native->enumerator->GetDevice (deviceId.toWideCharPointer(),
                                            native->device.resetAndGetAddressOf());
    }
    else
    {
        hr = native->enumerator->GetDefaultAudioEndpoint (eRender, eConsole,
                                                          native->device.resetAndGetAddressOf());
    }

    if (FAILED (hr) || ! native->device)
    {
        const juce::ScopedLock sl (errorLock);
        lastError = "GetDevice failed: " + hresultToString (hr);
        return false;
    }

    hr = native->device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr,
                                   (void**) native->audioClient.resetAndGetAddressOf());
    if (FAILED (hr))
    {
        const juce::ScopedLock sl (errorLock);
        lastError = "Activate IAudioClient failed: " + hresultToString (hr);
        return false;
    }

    hr = native->audioClient->GetMixFormat (&native->mixFormat);
    if (FAILED (hr) || native->mixFormat == nullptr)
    {
        const juce::ScopedLock sl (errorLock);
        lastError = "GetMixFormat failed: " + hresultToString (hr);
        return false;
    }

    native->eventHandle = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    bool useEvent = (native->eventHandle != nullptr);

    const REFERENCE_TIME bufferDuration = 1000000; // 100 ms

    if (useEvent)
    {
        hr = native->audioClient->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                              bufferDuration,
                                              0,
                                              native->mixFormat,
                                              nullptr);
        if (SUCCEEDED (hr))
        {
            hr = native->audioClient->SetEventHandle (native->eventHandle);
            if (FAILED (hr))
                useEvent = false;
        }
        else
        {
            useEvent = false;
        }
    }

    if (! useEvent)
    {
        if (native->eventHandle != nullptr)
        {
            CloseHandle (native->eventHandle);
            native->eventHandle = nullptr;
        }

        native->audioClient.reset();
        native->captureClient.reset();

        hr = native->device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr,
                                       (void**) native->audioClient.resetAndGetAddressOf());
        if (FAILED (hr))
        {
            const juce::ScopedLock sl (errorLock);
            lastError = "Re-Activate IAudioClient failed: " + hresultToString (hr);
            return false;
        }

        if (native->mixFormat != nullptr)
        {
            CoTaskMemFree (native->mixFormat);
            native->mixFormat = nullptr;
        }

        hr = native->audioClient->GetMixFormat (&native->mixFormat);
        if (FAILED (hr) || native->mixFormat == nullptr)
        {
            const juce::ScopedLock sl (errorLock);
            lastError = "GetMixFormat (retry) failed: " + hresultToString (hr);
            return false;
        }

        hr = native->audioClient->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK,
                                              bufferDuration,
                                              0,
                                              native->mixFormat,
                                              nullptr);
        if (FAILED (hr))
        {
            const juce::ScopedLock sl (errorLock);
            lastError = "IAudioClient::Initialize (loopback) failed: " + hresultToString (hr);
            return false;
        }
    }

    hr = native->audioClient->GetService (__uuidof (IAudioCaptureClient),
                                          (void**) native->captureClient.resetAndGetAddressOf());
    if (FAILED (hr))
    {
        const juce::ScopedLock sl (errorLock);
        lastError = "GetService IAudioCaptureClient failed: " + hresultToString (hr);
        return false;
    }

    const int numCh = (int) native->mixFormat->nChannels;
    captureNumChannels = numCh;
    captureSampleRate = (double) native->mixFormat->nSamplesPerSec;

    ring.setSize (juce::jmax (2, numCh), fifoFrames, false, false, true);
    ring.clear();
    {
        const juce::ScopedLock sl (fifoLock);
        fifo.reset();
    }

    activeDeviceId = deviceId;
    return true;
}

void WasapiLoopbackCapture::closeDevice()
{
    if (native != nullptr)
        native->close();

    captureSampleRate = 0.0;
    captureNumChannels = 0;
}

void WasapiLoopbackCapture::discardOldestFramesUnlocked (int numFrames)
{
    if (numFrames <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (numFrames, start1, size1, start2, size2);
    fifo.finishedRead (size1 + size2);
}

void WasapiLoopbackCapture::pushCapturedFrames (const float* interleaved, int numFrames, int numCh)
{
    if (numFrames <= 0 || interleaved == nullptr)
        return;

    const juce::ScopedLock sl (fifoLock);

    // Keep newest audio if the reader is behind — drop oldest to make room
    const int free = fifo.getFreeSpace();
    if (free < numFrames)
        discardOldestFramesUnlocked (numFrames - free);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (numFrames, start1, size1, start2, size2);

    auto copyBlock = [&] (int start, int size, int srcOffset)
    {
        for (int i = 0; i < size; ++i)
        {
            for (int ch = 0; ch < ring.getNumChannels(); ++ch)
            {
                const float sample = (ch < numCh) ? interleaved[(srcOffset + i) * numCh + ch] : 0.0f;
                ring.setSample (ch, start + i, sample);
            }
        }
    };

    copyBlock (start1, size1, 0);
    copyBlock (start2, size2, size1);
    fifo.finishedWrite (size1 + size2);

    float localPeak = peakLevel.load() * 0.92f;
    const int written = size1 + size2;
    const int useCh = juce::jmin (numCh, 2);
    for (int i = 0; i < written; ++i)
        for (int ch = 0; ch < useCh; ++ch)
            localPeak = juce::jmax (localPeak, std::abs (interleaved[i * numCh + ch]));
    peakLevel = localPeak;
}

int WasapiLoopbackCapture::read (juce::AudioBuffer<float>& dest, int numFrames)
{
    dest.clear();
    if (numFrames <= 0)
        return 0;

    const juce::ScopedLock sl (fifoLock);

    const int outCh = dest.getNumChannels();
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (numFrames, start1, size1, start2, size2);

    auto copyBlock = [&] (int start, int size, int destOffset)
    {
        for (int ch = 0; ch < outCh; ++ch)
        {
            const int srcCh = juce::jmin (ch, ring.getNumChannels() - 1);
            if (srcCh >= 0 && size > 0)
                dest.copyFrom (ch, destOffset, ring, srcCh, start, size);
        }
    };

    copyBlock (start1, size1, 0);
    copyBlock (start2, size2, size1);
    const int got = size1 + size2;
    fifo.finishedRead (got);
    return got;
}

void WasapiLoopbackCapture::captureThreadFn()
{
    const HRESULT coHr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    const bool shouldUninit = (coHr == S_OK);

    if (! openDevice (pendingDeviceId))
    {
        running = false;
        if (shouldUninit)
            CoUninitialize();
        return;
    }

    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW (L"Pro Audio", &taskIndex);

    HRESULT hr = native->audioClient->Start();
    if (FAILED (hr))
    {
        {
            const juce::ScopedLock sl (errorLock);
            lastError = "IAudioClient::Start failed: " + hresultToString (hr);
        }
        closeDevice();
        running = false;
        if (mmcss != nullptr)
            AvRevertMmThreadCharacteristics (mmcss);
        if (shouldUninit)
            CoUninitialize();
        return;
    }

    const WAVEFORMATEX* fmt = native->mixFormat;
    const int numCh = (int) fmt->nChannels;
    const int bits = (int) fmt->wBitsPerSample;
    int validBits = bits;

    bool isFloat = false;
    bool isPcm = false;

    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
    {
        isFloat = true;
    }
    else if (fmt->wFormatTag == WAVE_FORMAT_PCM)
    {
        isPcm = true;
    }
    else if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*> (fmt);
        validBits = (int) ext->Samples.wValidBitsPerSample;
        if (validBits == 0)
            validBits = bits;

        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            isFloat = true;
        else if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
            isPcm = true;
    }

    const int bytesPerSample = bits / 8;
    juce::HeapBlock<float> convertBuf;

    while (! shouldStop.load())
    {
        if (native->eventHandle != nullptr)
            WaitForSingleObject (native->eventHandle, 50);
        else
            Sleep (5);

        if (shouldStop.load())
            break;

        UINT32 packetLength = 0;
        hr = native->captureClient->GetNextPacketSize (&packetLength);
        if (FAILED (hr))
            break;

        while (packetLength > 0 && ! shouldStop.load())
        {
            BYTE* data = nullptr;
            UINT32 numFrames = 0;
            DWORD flags = 0;

            hr = native->captureClient->GetBuffer (&data, &numFrames, &flags, nullptr, nullptr);
            if (FAILED (hr))
                break;

            if (numFrames > 0)
            {
                convertBuf.allocate ((size_t) numFrames * (size_t) numCh, false);
                float* dst = convertBuf.getData();
                const int total = (int) numFrames * numCh;

                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr)
                {
                    juce::FloatVectorOperations::clear (dst, total);
                }
                else if (isFloat && bytesPerSample == 4)
                {
                    juce::FloatVectorOperations::copy (dst, reinterpret_cast<const float*> (data), total);
                }
                else if (isPcm && bytesPerSample == 2)
                {
                    const auto* src = reinterpret_cast<const int16_t*> (data);
                    for (int i = 0; i < total; ++i)
                        dst[i] = (float) src[i] * (1.0f / 32768.0f);
                }
                else if (isPcm && bytesPerSample == 3)
                {
                    const auto* src = data;
                    for (int i = 0; i < total; ++i)
                    {
                        const int32_t s = ((int32_t) src[0]) | ((int32_t) src[1] << 8) | ((int32_t) (int8_t) src[2] << 16);
                        dst[i] = (float) s * (1.0f / 8388608.0f);
                        src += 3;
                    }
                }
                else if (isPcm && bytesPerSample == 4)
                {
                    const auto* src = reinterpret_cast<const int32_t*> (data);
                    // Scale using valid bits when container is 32-bit
                    const float scale = 1.0f / (float) (1 << juce::jmin (30, validBits - 1));
                    for (int i = 0; i < total; ++i)
                        dst[i] = (float) src[i] * scale;
                }
                else
                {
                    // Unknown mix format — keep continuity with silence rather than garbage
                    juce::FloatVectorOperations::clear (dst, total);
                }

                pushCapturedFrames (dst, (int) numFrames, numCh);
            }

            native->captureClient->ReleaseBuffer (numFrames);
            hr = native->captureClient->GetNextPacketSize (&packetLength);
            if (FAILED (hr))
                break;
        }
    }

    if (native->audioClient)
        native->audioClient->Stop();

    closeDevice();

    if (mmcss != nullptr)
        AvRevertMmThreadCharacteristics (mmcss);

    running = false;

    if (shouldUninit)
        CoUninitialize();
}

#endif // JUCE_WINDOWS
