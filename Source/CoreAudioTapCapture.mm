#include "CoreAudioTapCapture.h"

#if JUCE_MAC

#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#include <unistd.h>

#ifndef MAC_OS_VERSION_14_2
 #define MAC_OS_VERSION_14_2 140200
#endif

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_VERSION_14_2
 #import <CoreAudio/CATapDescription.h>
#endif

namespace
{
    juce::String osStatusToString (OSStatus status)
    {
        const juce::uint32 u = (juce::uint32) status;
        const char four[5] = {
            (char) ((u >> 24) & 0xff),
            (char) ((u >> 16) & 0xff),
            (char) ((u >> 8) & 0xff),
            (char) (u & 0xff),
            0
        };

        const bool printable = juce::CharacterFunctions::isPrintable ((juce::juce_wchar) four[0])
                            && juce::CharacterFunctions::isPrintable ((juce::juce_wchar) four[1])
                            && juce::CharacterFunctions::isPrintable ((juce::juce_wchar) four[2])
                            && juce::CharacterFunctions::isPrintable ((juce::juce_wchar) four[3]);

        if (printable)
            return "OSStatus '" + juce::String (four) + "' (" + juce::String ((int) status) + ")";

        return "OSStatus " + juce::String ((int) status);
    }

    juce::String cfStringToJuce (CFStringRef str)
    {
        if (str == nullptr)
            return {};

        return juce::String::fromCFString (str);
    }

    juce::String getStringProperty (AudioObjectID object, AudioObjectPropertySelector selector)
    {
        AudioObjectPropertyAddress address {
            selector,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        CFStringRef value = nullptr;
        UInt32 size = sizeof (value);

        if (AudioObjectGetPropertyData (object, &address, 0, nullptr, &size, &value) != noErr || value == nullptr)
            return {};

        auto result = cfStringToJuce (value);
        CFRelease (value);
        return result;
    }

    bool deviceHasOutputStreams (AudioObjectID device)
    {
        AudioObjectPropertyAddress address {
            kAudioDevicePropertyStreams,
            kAudioDevicePropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };

        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (device, &address, 0, nullptr, &size) != noErr)
            return false;

        return size >= sizeof (AudioStreamID);
    }

    AudioObjectID translatePidToProcessObject (pid_t pid)
    {
        AudioObjectID processObject = kAudioObjectUnknown;
        AudioObjectPropertyAddress address {
            kAudioHardwarePropertyTranslatePIDToProcessObject,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        UInt32 size = sizeof (processObject);
        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &address,
                                        sizeof (pid), &pid, &size, &processObject) != noErr)
            return kAudioObjectUnknown;

        return processObject;
    }

    bool isAlive (AudioObjectID device)
    {
        AudioObjectPropertyAddress address {
            kAudioDevicePropertyDeviceIsAlive,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        UInt32 alive = 0;
        UInt32 size = sizeof (alive);
        return AudioObjectGetPropertyData (device, &address, 0, nullptr, &size, &alive) == noErr
            && alive != 0;
    }

    NSArray<NSNumber*>* processesToExclude()
    {
        NSMutableArray<NSNumber*>* excluded = [NSMutableArray array];

        const AudioObjectID selfProc = translatePidToProcessObject (getpid());
        if (selfProc != kAudioObjectUnknown)
            [excluded addObject: @(selfProc)];

        AudioObjectPropertyAddress listAddress {
            kAudioHardwarePropertyProcessObjectList,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        UInt32 size = 0;
        if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &listAddress, 0, nullptr, &size) != noErr
            || size < sizeof (AudioObjectID))
            return excluded;

        const int count = (int) (size / sizeof (AudioObjectID));
        juce::HeapBlock<AudioObjectID> procs ((size_t) count);

        if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &listAddress, 0, nullptr, &size, procs.getData()) != noErr)
            return excluded;

        AudioObjectPropertyAddress bundleAddress {
            kAudioProcessPropertyBundleID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        for (int i = 0; i < count; ++i)
        {
            CFStringRef bundle = nullptr;
            UInt32 bundleSize = sizeof (bundle);

            if (AudioObjectGetPropertyData (procs[i], &bundleAddress, 0, nullptr, &bundleSize, &bundle) != noErr
                || bundle == nullptr)
                continue;

            const auto bid = cfStringToJuce (bundle).toLowerCase();
            CFRelease (bundle);

            if (bid.contains ("syncroom"))
            {
                NSNumber* value = @(procs[i]);
                if (! [excluded containsObject: value])
                    [excluded addObject: value];
            }
        }

        return excluded;
    }
}

struct CoreAudioTapCapture::NativeState
{
    AudioObjectID tapID = kAudioObjectUnknown;
    AudioObjectID aggregateID = kAudioObjectUnknown;
    AudioDeviceIOProcID ioProcID = nullptr;
    AudioStreamBasicDescription asbd {};
    juce::HeapBlock<float> interleave;
    int interleaveFrames = 0;
    bool ioRunning = false;

    ~NativeState() { close(); }

    void close()
    {
        if (ioRunning && aggregateID != kAudioObjectUnknown && ioProcID != nullptr)
        {
            AudioDeviceStop (aggregateID, ioProcID);
            ioRunning = false;
        }

        if (aggregateID != kAudioObjectUnknown && ioProcID != nullptr)
        {
            AudioDeviceDestroyIOProcID (aggregateID, ioProcID);
            ioProcID = nullptr;
        }

        if (aggregateID != kAudioObjectUnknown)
        {
            AudioHardwareDestroyAggregateDevice (aggregateID);
            aggregateID = kAudioObjectUnknown;
        }

        if (tapID != kAudioObjectUnknown)
        {
            AudioHardwareDestroyProcessTap (tapID);
            tapID = kAudioObjectUnknown;
        }

        interleave.free();
        interleaveFrames = 0;
        asbd = {};
    }
};

extern "C" OSStatus VirtualLoopbackTapIOProc (AudioObjectID,
                                             const AudioTimeStamp*,
                                             const AudioBufferList* inputData,
                                             const AudioTimeStamp*,
                                             AudioBufferList*,
                                             const AudioTimeStamp*,
                                             void* clientData)
{
    auto* owner = static_cast<CoreAudioTapCapture*> (clientData);
    if (owner != nullptr)
        owner->processTapInput (inputData);
    return noErr;
}

void CoreAudioTapCapture::processTapInput (const void* inputPtr)
{
    const auto* inputData = static_cast<const AudioBufferList*> (inputPtr);
    if (native == nullptr || inputData == nullptr)
        return;

    const auto& asbd = native->asbd;
    const int channels = juce::jmax (1, (int) asbd.mChannelsPerFrame);
    const bool isFloat = (asbd.mFormatFlags & kAudioFormatFlagIsFloat) != 0
                      && asbd.mBitsPerChannel == 32;

    if (! isFloat || inputData->mNumberBuffers == 0)
        return;

    if (inputData->mNumberBuffers == 1)
    {
        const AudioBuffer& buf = inputData->mBuffers[0];
        if (buf.mData == nullptr || buf.mDataByteSize == 0)
            return;

        const int bytesPerFrame = juce::jmax (1, (int) asbd.mBytesPerFrame);
        const int numFrames = (int) (buf.mDataByteSize / (UInt32) bytesPerFrame);
        pushCapturedFrames (static_cast<const float*> (buf.mData), numFrames, channels);
        return;
    }

    const AudioBuffer& first = inputData->mBuffers[0];
    if (first.mData == nullptr || first.mDataByteSize == 0)
        return;

    const int numFrames = (int) (first.mDataByteSize / sizeof (float));
    const int useCh = juce::jmin (channels, (int) inputData->mNumberBuffers);

    if (native->interleave.getData() == nullptr
        || native->interleaveFrames < numFrames)
        return;

    float* dest = native->interleave.getData();
    for (int i = 0; i < numFrames; ++i)
    {
        for (int ch = 0; ch < useCh; ++ch)
        {
            const auto* src = static_cast<const float*> (inputData->mBuffers[(UInt32) ch].mData);
            dest[i * useCh + ch] = src != nullptr ? src[i] : 0.0f;
        }
    }

    pushCapturedFrames (dest, numFrames, useCh);
}

CoreAudioTapCapture::CoreAudioTapCapture()
    : native (std::make_unique<NativeState>())
{
}

CoreAudioTapCapture::~CoreAudioTapCapture()
{
    stop();
}

juce::String CoreAudioTapCapture::getLastError() const
{
    const juce::ScopedLock sl (errorLock);
    return lastError;
}

void CoreAudioTapCapture::setError (const juce::String& text)
{
    const juce::ScopedLock sl (errorLock);
    lastError = text;
}

juce::Array<CoreAudioTapCapture::DeviceInfo> CoreAudioTapCapture::getRenderDevices()
{
    juce::Array<DeviceInfo> list;

    DeviceInfo systemMix;
    systemMix.id = systemMixId;
    systemMix.name = juce::String (L"システム再生音");
    systemMix.isDefault = true;
    list.add (systemMix);

    AudioObjectPropertyAddress devicesAddress {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize (kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size) != noErr
        || size < sizeof (AudioObjectID))
        return list;

    const int count = (int) (size / sizeof (AudioObjectID));
    juce::HeapBlock<AudioObjectID> devices ((size_t) count);

    if (AudioObjectGetPropertyData (kAudioObjectSystemObject, &devicesAddress, 0, nullptr, &size, devices.getData()) != noErr)
        return list;

    for (int i = 0; i < count; ++i)
    {
        if (! deviceHasOutputStreams (devices[i]))
            continue;

        DeviceInfo info;
        info.id = getStringProperty (devices[i], kAudioDevicePropertyDeviceUID);
        info.name = getStringProperty (devices[i], kAudioObjectPropertyName);

        if (info.id.isEmpty())
            continue;

        if (info.name.startsWith ("VirtualLoopback Tap"))
            continue;

        if (info.name.isEmpty())
            info.name = info.id;

        list.add (info);
    }

    return list;
}

bool CoreAudioTapCapture::start (const juce::String& deviceId)
{
    stop();
    setError ({});

    pendingDeviceId = deviceId.isEmpty() ? juce::String (systemMixId) : deviceId;
    shouldStop = false;
    running = true;
    captureThread = std::thread ([this] { captureThreadFn(); });

    for (int i = 0; i < 200; ++i)
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

    if (lastError.isEmpty() && captureSampleRate.load() <= 0.0)
        setError (juce::String (L"Core Audio タップの起動がタイムアウトしました"));

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

void CoreAudioTapCapture::stop()
{
    shouldStop = true;

    if (captureThread.joinable())
        captureThread.join();

    running = false;
    closeTap();
    peakLevel = 0.0f;
}

bool CoreAudioTapCapture::openTap (const juce::String& deviceId)
{
#if __MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_VERSION_14_2
    juce::ignoreUnused (deviceId);
    setError (juce::String (L"この SDK では Process Tap をビルドできません。macOS 14.2+ SDK が必要です。"));
    return false;
#else
    if (@available (macOS 14.2, *))
    {
        @autoreleasepool
        {
            NSArray<NSNumber*>* excluded = processesToExclude();
            CATapDescription* description = nil;

            const bool useSystemMix = deviceId.isEmpty() || deviceId == systemMixId;

            if (useSystemMix)
            {
                description = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses: excluded];
            }
            else
            {
                NSString* uid = [NSString stringWithUTF8String: deviceId.toRawUTF8()];
                description = [[CATapDescription alloc] initWithProcesses: excluded
                                                             andDeviceUID: uid
                                                               withStream: 0];
                if (description != nil)
                    description.exclusive = YES;
            }

            if (description == nil)
            {
                setError (juce::String (L"CATapDescription の作成に失敗しました"));
                return false;
            }

            description.privateTap = YES;
            description.muteBehavior = CATapUnmuted;
            description.name = @"VirtualLoopback Tap";

            if (description.UUID == nil)
                description.UUID = [NSUUID UUID];

            OSStatus status = AudioHardwareCreateProcessTap (description, &native->tapID);
            if (status != noErr || native->tapID == kAudioObjectUnknown)
            {
                setError ("AudioHardwareCreateProcessTap failed: " + osStatusToString (status));
                return false;
            }

            NSString* tapUID = [description.UUID UUIDString];
            NSString* aggregateUID = [NSString stringWithFormat: @"com.XiAceLite.VirtualLoopback.tap.%d.%@",
                                      getpid(), [[NSUUID UUID] UUIDString]];

            NSDictionary* aggregateDesc = @{
                (__bridge NSString*) kAudioAggregateDeviceNameKey: @"VirtualLoopback Tap Aggregate",
                (__bridge NSString*) kAudioAggregateDeviceUIDKey: aggregateUID,
                (__bridge NSString*) kAudioAggregateDeviceIsPrivateKey: @YES,
                (__bridge NSString*) kAudioAggregateDeviceTapAutoStartKey: @YES,
                (__bridge NSString*) kAudioAggregateDeviceTapListKey: @[
                    @{
                        (__bridge NSString*) kAudioSubTapUIDKey: tapUID,
                        (__bridge NSString*) kAudioSubTapDriftCompensationKey: @YES
                    }
                ]
            };

            status = AudioHardwareCreateAggregateDevice ((__bridge CFDictionaryRef) aggregateDesc,
                                                         &native->aggregateID);
            if (status != noErr || native->aggregateID == kAudioObjectUnknown)
            {
                setError ("AudioHardwareCreateAggregateDevice failed: " + osStatusToString (status));
                return false;
            }

            for (int i = 0; i < 100 && ! isAlive (native->aggregateID); ++i)
                juce::Thread::sleep (10);

            AudioObjectPropertyAddress formatAddress {
                kAudioDevicePropertyStreamFormat,
                kAudioDevicePropertyScopeInput,
                kAudioObjectPropertyElementMain
            };

            UInt32 asbdSize = sizeof (native->asbd);
            status = AudioObjectGetPropertyData (native->aggregateID, &formatAddress, 0, nullptr,
                                                 &asbdSize, &native->asbd);
            if (status != noErr || native->asbd.mSampleRate <= 0.0 || native->asbd.mChannelsPerFrame == 0)
            {
                setError ("Failed to read tap stream format: " + osStatusToString (status));
                return false;
            }

            const int numCh = (int) native->asbd.mChannelsPerFrame;
            captureNumChannels = numCh;
            captureSampleRate = native->asbd.mSampleRate;

            ring.setSize (juce::jmax (2, numCh), fifoFrames, false, false, true);
            ring.clear();
            {
                const juce::ScopedLock sl (fifoLock);
                fifo.reset();
            }

            native->interleaveFrames = 8192;
            native->interleave.allocate ((size_t) native->interleaveFrames * (size_t) juce::jmax (2, numCh), true);

            status = AudioDeviceCreateIOProcID (native->aggregateID, VirtualLoopbackTapIOProc, this, &native->ioProcID);
            if (status != noErr)
            {
                setError ("AudioDeviceCreateIOProcID failed: " + osStatusToString (status));
                return false;
            }

            status = AudioDeviceStart (native->aggregateID, native->ioProcID);
            if (status != noErr)
            {
                setError ("AudioDeviceStart failed: " + osStatusToString (status)
                          + juce::String (L"。システム設定 → プライバシーとセキュリティ → 画面収録とシステムオーディオ で DAW を許可してください。"));
                return false;
            }

            native->ioRunning = true;
            return true;
        }
    }

    setError (juce::String (L"macOS 14.2 以降が必要です（Core Audio Process Tap）"));
    return false;
#endif
}

void CoreAudioTapCapture::closeTap()
{
    if (native != nullptr)
        native->close();

    captureSampleRate = 0.0;
    captureNumChannels = 0;
}

void CoreAudioTapCapture::discardOldestFramesUnlocked (int numFrames)
{
    if (numFrames <= 0)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (numFrames, start1, size1, start2, size2);
    fifo.finishedRead (size1 + size2);
}

void CoreAudioTapCapture::pushCapturedFrames (const float* interleaved, int numFrames, int numCh)
{
    if (numFrames <= 0 || interleaved == nullptr)
        return;

    const juce::ScopedLock sl (fifoLock);

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

int CoreAudioTapCapture::read (juce::AudioBuffer<float>& dest, int numFrames)
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

void CoreAudioTapCapture::captureThreadFn()
{
    if (! openTap (pendingDeviceId))
    {
        closeTap();
        running = false;
        return;
    }

    while (! shouldStop.load())
        juce::Thread::sleep (20);

    closeTap();
    running = false;
}

#endif // JUCE_MAC
