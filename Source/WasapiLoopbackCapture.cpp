#include "WasapiLoopbackCapture.h"

#if JUCE_WINDOWS

#ifndef NTDDI_VERSION
 #define NTDDI_VERSION 0x0A00000A
#endif
#ifndef WINVER
 #define WINVER 0x0A00
#endif
#ifndef _WIN32_WINNT
 #define _WIN32_WINNT 0x0A00
#endif

#include "AppProcessAllowlist.h"

#include <objbase.h>
#include <objidlbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <ksmedia.h>
#include <tlhelp32.h>
#include <cstdint>

#if __has_include(<audioclientactivationparams.h>)
 #include <audioclientactivationparams.h>
#else
 enum AUDIOCLIENT_ACTIVATION_TYPE
 {
     AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
     AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
 };

 enum PROCESS_LOOPBACK_MODE
 {
     PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
     PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
 };

 struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS
 {
     DWORD TargetProcessId;
     PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
 };

 struct AUDIOCLIENT_ACTIVATION_PARAMS
 {
     AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
     union
     {
         AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
     };
 };
#endif

#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
 #define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "mmdevapi.lib")

namespace
{
    juce::String hresultToString (HRESULT hr)
    {
        return "HRESULT 0x" + juce::String::toHexString ((juce::int64) (juce::uint32) hr);
    }

    bool supportsApplicationLoopback()
    {
        using RtlGetVersionFn = LONG (WINAPI*) (PRTL_OSVERSIONINFOW);
        HMODULE ntdll = GetModuleHandleW (L"ntdll.dll");
        if (ntdll == nullptr)
            return false;

        auto* fn = reinterpret_cast<RtlGetVersionFn> (GetProcAddress (ntdll, "RtlGetVersion"));
        if (fn == nullptr)
            return false;

        RTL_OSVERSIONINFOW info {};
        info.dwOSVersionInfoSize = sizeof (info);
        if (fn (&info) != 0)
            return false;

        return info.dwMajorVersion > 10
            || (info.dwMajorVersion == 10 && info.dwBuildNumber >= 19041);
    }

    juce::String getProcessImagePath (DWORD pid)
    {
        HANDLE process = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process == nullptr)
            return {};

        wchar_t path[MAX_PATH * 4] = {};
        DWORD size = (DWORD) (sizeof (path) / sizeof (path[0]));
        juce::String result;

        if (QueryFullProcessImageNameW (process, 0, path, &size))
            result = juce::String (path);

        CloseHandle (process);
        return result;
    }

    juce::String getProcessExeNameFromSnapshot (DWORD pid)
    {
        HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return {};

        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof (entry);
        juce::String result;

        if (Process32FirstW (snap, &entry))
        {
            do
            {
                if (entry.th32ProcessID == pid)
                {
                    result = juce::String (entry.szExeFile);
                    break;
                }
            }
            while (Process32NextW (snap, &entry));
        }

        CloseHandle (snap);
        return result;
    }

    juce::Array<DWORD> findPidsByProcessName (const juce::String& processNameWithoutExt)
    {
        juce::Array<DWORD> pids;
        if (processNameWithoutExt.isEmpty())
            return pids;

        const auto want = processNameWithoutExt.toLowerCase();
        HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return pids;

        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof (entry);

        if (Process32FirstW (snap, &entry))
        {
            do
            {
                auto exe = juce::String (entry.szExeFile);
                if (exe.endsWithIgnoreCase (".exe"))
                    exe = exe.dropLastCharacters (4);

                if (exe.equalsIgnoreCase (want) && entry.th32ProcessID != 0)
                    pids.add (entry.th32ProcessID);
            }
            while (Process32NextW (snap, &entry));
        }

        CloseHandle (snap);
        return pids;
    }

    juce::String makePidCaptureId (DWORD pid)
    {
        return juce::String (WasapiLoopbackCapture::pidIdPrefix) + juce::String ((juce::uint32) pid);
    }

    juce::String makeAppCaptureId (const juce::String& imagePath)
    {
        auto key = imagePath.replaceCharacter ('\\', '/').toLowerCase();
        return juce::String (WasapiLoopbackCapture::appIdPrefix) + key;
    }

    bool isWeakDisplayName (const juce::String& name)
    {
        return name.isEmpty()
            || name.startsWithIgnoreCase ("PID ")
            || name == "."
            || name.startsWithIgnoreCase ("@%")
            || name == juce::String (L"不明なアプリ");
    }

    juce::String displayNameForProcess (DWORD pid, const juce::String& sessionName, const juce::String& imagePath)
    {
        if (! isWeakDisplayName (sessionName))
            return sessionName;

        if (imagePath.isNotEmpty())
            return juce::File (imagePath).getFileNameWithoutExtension();

        const auto exeName = getProcessExeNameFromSnapshot (pid);
        if (exeName.isNotEmpty())
            return juce::File (exeName).getFileNameWithoutExtension();

        // Last resort: never show a bare PID in the device list.
        return juce::String (L"不明なアプリ");
    }

    bool looksLikeSyncRoomDestination (const juce::String& text)
    {
        // Hidden policy: Yamaha SYNCROOM / SYNCROOM2 are loopback destinations.
        // Do NOT treat SyncRoomChatTool* (or similar companions) as destinations.
        if (text.isEmpty())
            return false;

        auto lower = text.toLowerCase().replaceCharacter ('/', '\\');
        if (lower.contains ("chattool") || lower.contains ("syncroomchat"))
            return false;

        const auto base = juce::File (lower).getFileNameWithoutExtension();
        if (base == "syncroom" || base == "syncroom2")
            return true;

        if (lower.contains ("\\syncroom2\\") || lower.contains ("\\syncroom\\"))
            return true;

        return false;
    }

    bool looksLikeSyncRoom (const juce::String& sessionName, const juce::String& imagePath, DWORD pid)
    {
        if (looksLikeSyncRoomDestination (sessionName) || looksLikeSyncRoomDestination (imagePath))
            return true;

        const auto exeName = getProcessExeNameFromSnapshot (pid);
        return looksLikeSyncRoomDestination (exeName);
    }

    juce::String makeAppKeyFromPath (const juce::String& imagePath)
    {
        return juce::String (WasapiLoopbackCapture::appIdPrefix)
             + imagePath.replaceCharacter ('\\', '/').toLowerCase();
    }

    juce::String makeAppKeyFromExeName (const juce::String& exeName)
    {
        return juce::String (WasapiLoopbackCapture::appIdPrefix) + "exe:"
             + juce::File (exeName).getFileName().toLowerCase();
    }

    struct AppSessionCandidate
    {
        juce::String groupKey;
        juce::String displayName;
        DWORD pid = 0;
        bool isActive = false;
    };

    juce::String groupKeyForSession (DWORD pid, const juce::String& sessionName, const juce::String& imagePath)
    {
        if (imagePath.isNotEmpty())
            return makeAppKeyFromPath (imagePath);

        const auto exeName = getProcessExeNameFromSnapshot (pid);
        if (exeName.isNotEmpty())
            return makeAppKeyFromExeName (exeName);

        const auto pretty = displayNameForProcess (pid, sessionName, imagePath);
        if (pretty.isNotEmpty() && ! isWeakDisplayName (pretty))
            return juce::String (WasapiLoopbackCapture::appIdPrefix) + "name:" + pretty.toLowerCase();

        // Stable-enough fallback so the UI never depends on a raw PID label.
        return juce::String (WasapiLoopbackCapture::appIdPrefix) + "pid:" + juce::String ((juce::uint32) pid);
    }

    juce::String exeStemFromGroupKey (const juce::String& groupKey)
    {
        if (! groupKey.startsWith (WasapiLoopbackCapture::appIdPrefix))
            return {};

        const auto rest = groupKey.fromFirstOccurrenceOf (WasapiLoopbackCapture::appIdPrefix, false, false);
        if (rest.startsWith ("exe:"))
        {
            auto name = rest.fromFirstOccurrenceOf ("exe:", false, false);
            if (name.endsWithIgnoreCase (".exe"))
                name = name.dropLastCharacters (4);
            return name;
        }

        if (rest.startsWith ("pid:") || rest.startsWith ("name:"))
            return {};

        if (rest.containsChar ('/') || rest.containsChar ('\\'))
            return juce::File (rest.replaceCharacter ('/', '\\')).getFileNameWithoutExtension();

        return {};
    }

    void mergeAppCandidate (juce::Array<AppSessionCandidate>& apps,
                            juce::StringArray& seenKeys,
                            const AppSessionCandidate& candidate)
    {
        // pid == 0 is allowed for allowlisted-but-not-running placeholders.
        if (candidate.groupKey.isEmpty())
            return;

        const int existing = seenKeys.indexOf (candidate.groupKey);
        if (existing < 0)
        {
            seenKeys.add (candidate.groupKey);
            apps.add (candidate);
            return;
        }

        auto& prev = apps.getReference (existing);
        const auto keptName = prev.displayName;
        const auto keptPid = prev.pid;

        // Prefer an actively rendering session's PID (Discord / multi-process apps).
        if (candidate.isActive && ! prev.isActive && candidate.pid != 0)
        {
            prev = candidate;
            if (isWeakDisplayName (prev.displayName) && ! isWeakDisplayName (keptName))
                prev.displayName = keptName;
            return;
        }

        if (prev.pid == 0 && candidate.pid != 0)
            prev.pid = candidate.pid;
        else if (candidate.pid == 0 && prev.pid == 0)
            prev.pid = keptPid;

        if (isWeakDisplayName (prev.displayName) && ! isWeakDisplayName (candidate.displayName))
            prev.displayName = candidate.displayName;
        else if (candidate.isActive == prev.isActive
                 && ! isWeakDisplayName (candidate.displayName)
                 && candidate.displayName.length() > prev.displayName.length())
            prev.displayName = candidate.displayName;
    }

    void upgradeOrAddAllowlistedCandidate (juce::Array<AppSessionCandidate>& apps,
                                           juce::StringArray& seenKeys,
                                           const juce::String& processName,
                                           DWORD pid)
    {
        if (processName.isEmpty())
            return;

        // Prefer upgrading an existing session entry that is clearly the same exe,
        // so we don't leave a sibling "不明なアプリ" row behind.
        for (int i = 0; i < apps.size(); ++i)
        {
            auto& existing = apps.getReference (i);
            const auto stem = exeStemFromGroupKey (existing.groupKey);
            const bool sameExe = stem.equalsIgnoreCase (processName)
                              || existing.displayName.equalsIgnoreCase (processName)
                              || (pid != 0 && existing.pid == pid);
            if (! sameExe)
                continue;

            existing.displayName = processName;

            if (pid != 0)
                existing.pid = pid;

            return;
        }

        AppSessionCandidate candidate;
        candidate.groupKey = makeAppKeyFromExeName (processName + ".exe");
        candidate.displayName = processName;
        candidate.pid = pid;
        candidate.isActive = false;
        mergeAppCandidate (apps, seenKeys, candidate);
    }

    WAVEFORMATEX makeProcessLoopbackFormat()
    {
        // Process-loopback virtual device does not support GetMixFormat.
        // Microsoft sample uses an explicit PCM format + AUTOCONVERTPCM.
        WAVEFORMATEX format {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = (WORD) (format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        format.cbSize = 0;
        return format;
    }

    DWORD parsePidFromCaptureId (const juce::String& captureId)
    {
        if (captureId.startsWith (WasapiLoopbackCapture::pidIdPrefix))
        {
            const auto text = captureId.fromFirstOccurrenceOf (WasapiLoopbackCapture::pidIdPrefix, false, false);
            return (DWORD) text.getLargeIntValue();
        }

        const auto appPidPrefix = juce::String (WasapiLoopbackCapture::appIdPrefix) + "pid:";
        if (captureId.startsWith (appPidPrefix))
        {
            const auto text = captureId.fromFirstOccurrenceOf (appPidPrefix, false, false);
            return (DWORD) text.getLargeIntValue();
        }

        return 0;
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

    T* detach() noexcept
    {
        T* p = ptr;
        ptr = nullptr;
        return p;
    }

private:
    T* ptr = nullptr;
};

class ActivateCompletionHandler final : public IActivateAudioInterfaceCompletionHandler,
                                        public IAgileObject
{
public:
    ActivateCompletionHandler()
        : eventHandle (CreateEventW (nullptr, FALSE, FALSE, nullptr))
    {
    }

    ~ActivateCompletionHandler()
    {
        if (audioClient != nullptr)
        {
            audioClient->Release();
            audioClient = nullptr;
        }

        if (marshaller != nullptr)
        {
            marshaller->Release();
            marshaller = nullptr;
        }

        if (eventHandle != nullptr)
            CloseHandle (eventHandle);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == __uuidof (IUnknown))
        {
            *ppvObject = static_cast<IActivateAudioInterfaceCompletionHandler*> (this);
            AddRef();
            return S_OK;
        }

        if (riid == __uuidof (IActivateAudioInterfaceCompletionHandler))
        {
            *ppvObject = static_cast<IActivateAudioInterfaceCompletionHandler*> (this);
            AddRef();
            return S_OK;
        }

        if (riid == __uuidof (IAgileObject))
        {
            *ppvObject = static_cast<IAgileObject*> (this);
            AddRef();
            return S_OK;
        }

        if (riid == __uuidof (IMarshal))
        {
            if (marshaller == nullptr)
            {
                IUnknown* created = nullptr;
                const HRESULT hr = CoCreateFreeThreadedMarshaler (static_cast<IActivateAudioInterfaceCompletionHandler*> (this),
                                                                  &created);
                if (FAILED (hr))
                    return hr;

                marshaller = created;
            }

            return marshaller->QueryInterface (riid, ppvObject);
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return (ULONG) InterlockedIncrement (&refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG value = InterlockedDecrement (&refCount);
        if (value == 0)
            delete this;
        return (ULONG) value;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted (IActivateAudioInterfaceAsyncOperation* activateOperation) override
    {
        HRESULT activateResult = E_FAIL;
        IUnknown* unknown = nullptr;

        if (activateOperation != nullptr)
        {
            const HRESULT hr = activateOperation->GetActivateResult (&activateResult, &unknown);
            if (FAILED (hr))
                activateResult = hr;
        }

        resultHr = activateResult;

        if (SUCCEEDED (resultHr) && unknown != nullptr)
        {
            IAudioClient* client = nullptr;
            const HRESULT qiHr = unknown->QueryInterface (__uuidof (IAudioClient), (void**) &client);
            if (SUCCEEDED (qiHr))
                audioClient = client;
            else
                resultHr = qiHr;

            unknown->Release();
        }

        if (eventHandle != nullptr)
            SetEvent (eventHandle);

        return S_OK;
    }

    bool wait (DWORD timeoutMs)
    {
        if (eventHandle == nullptr)
            return false;

        const DWORD waitResult = WaitForSingleObject (eventHandle, timeoutMs);
        if (waitResult == WAIT_TIMEOUT)
            timedOut = true;

        return waitResult == WAIT_OBJECT_0;
    }

    HRESULT asyncCallHr = S_OK;
    HRESULT resultHr = E_FAIL;
    IAudioClient* audioClient = nullptr;
    bool timedOut = false;

private:
    volatile LONG refCount = 1;
    HANDLE eventHandle = nullptr;
    IUnknown* marshaller = nullptr;
};

struct ProcessLoopbackActivateResult
{
    WasapiComPtr<IAudioClient> client;
    HRESULT asyncCallHr = E_FAIL;
    HRESULT activateHr = E_FAIL;
    bool timedOut = false;
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

    void appendAudioSessions (IMMDevice* device,
                              juce::Array<AppSessionCandidate>& apps,
                              juce::StringArray& seenKeys)
    {
        if (device == nullptr)
            return;

        WasapiComPtr<IAudioSessionManager2> manager;
        if (FAILED (device->Activate (__uuidof (IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                      (void**) manager.resetAndGetAddressOf())) || ! manager)
            return;

        WasapiComPtr<IAudioSessionEnumerator> enumerator;
        if (FAILED (manager->GetSessionEnumerator (enumerator.resetAndGetAddressOf())) || ! enumerator)
            return;

        int count = 0;
        if (FAILED (enumerator->GetCount (&count)))
            return;

        const DWORD selfPid = GetCurrentProcessId();

        for (int i = 0; i < count; ++i)
        {
            WasapiComPtr<IAudioSessionControl> control;
            if (FAILED (enumerator->GetSession (i, control.resetAndGetAddressOf())) || ! control)
                continue;

            AudioSessionState sessionState = AudioSessionStateInactive;
            const HRESULT stateHr = control->GetState (&sessionState);
            if (SUCCEEDED (stateHr) && sessionState == AudioSessionStateExpired)
                continue;

            WasapiComPtr<IAudioSessionControl2> control2;
            if (FAILED (control->QueryInterface (__uuidof (IAudioSessionControl2),
                                                  (void**) control2.resetAndGetAddressOf())) || ! control2)
                continue;

            if (control2->IsSystemSoundsSession() == S_OK)
                continue;

            DWORD pid = 0;
            const HRESULT pidHr = control2->GetProcessId (&pid);
            // AUDCLNT_S_NO_SINGLE_PROCESS is a success code with pid==0 for cross-process sessions.
            if (FAILED (pidHr) || pid == 0 || pid == selfPid)
                continue;

            LPWSTR displayNameW = nullptr;
            juce::String sessionName;
            if (SUCCEEDED (control->GetDisplayName (&displayNameW)) && displayNameW != nullptr)
            {
                sessionName = juce::String (displayNameW);
                CoTaskMemFree (displayNameW);
            }

            const auto imagePath = getProcessImagePath (pid);
            if (looksLikeSyncRoom (sessionName, imagePath, pid))
                continue;

            AppSessionCandidate candidate;
            candidate.groupKey = groupKeyForSession (pid, sessionName, imagePath);
            candidate.displayName = displayNameForProcess (pid, sessionName, imagePath);
            candidate.pid = pid;
            candidate.isActive = (SUCCEEDED (stateHr) && sessionState == AudioSessionStateActive);
            mergeAppCandidate (apps, seenKeys, candidate);
        }
    }

    DWORD findBestPidForAppKey (IMMDeviceEnumerator* enumerator, const juce::String& appKey)
    {
        if (enumerator == nullptr || appKey.isEmpty())
            return 0;

        DWORD bestPid = 0;
        bool bestActive = false;

        WasapiComPtr<IMMDeviceCollection> collection;
        if (FAILED (enumerator->EnumAudioEndpoints (eRender,
                                                    DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED,
                                                    collection.resetAndGetAddressOf())) || ! collection)
            return 0;

        UINT count = 0;
        collection->GetCount (&count);

        for (UINT i = 0; i < count; ++i)
        {
            WasapiComPtr<IMMDevice> device;
            if (FAILED (collection->Item (i, device.resetAndGetAddressOf())) || ! device)
                continue;

            WasapiComPtr<IAudioSessionManager2> manager;
            if (FAILED (device->Activate (__uuidof (IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                          (void**) manager.resetAndGetAddressOf())) || ! manager)
                continue;

            WasapiComPtr<IAudioSessionEnumerator> sessions;
            if (FAILED (manager->GetSessionEnumerator (sessions.resetAndGetAddressOf())) || ! sessions)
                continue;

            int sessionCount = 0;
            if (FAILED (sessions->GetCount (&sessionCount)))
                continue;

            for (int s = 0; s < sessionCount; ++s)
            {
                WasapiComPtr<IAudioSessionControl> control;
                if (FAILED (sessions->GetSession (s, control.resetAndGetAddressOf())) || ! control)
                    continue;

                AudioSessionState sessionState = AudioSessionStateInactive;
                const HRESULT stateHr = control->GetState (&sessionState);
                if (SUCCEEDED (stateHr) && sessionState == AudioSessionStateExpired)
                    continue;

                WasapiComPtr<IAudioSessionControl2> control2;
                if (FAILED (control->QueryInterface (__uuidof (IAudioSessionControl2),
                                                      (void**) control2.resetAndGetAddressOf())) || ! control2)
                    continue;

                DWORD pid = 0;
                if (FAILED (control2->GetProcessId (&pid)) || pid == 0)
                    continue;

                LPWSTR displayNameW = nullptr;
                juce::String sessionName;
                if (SUCCEEDED (control->GetDisplayName (&displayNameW)) && displayNameW != nullptr)
                {
                    sessionName = juce::String (displayNameW);
                    CoTaskMemFree (displayNameW);
                }

                const auto imagePath = getProcessImagePath (pid);
                const auto key = groupKeyForSession (pid, sessionName, imagePath);
                if (! key.equalsIgnoreCase (appKey))
                    continue;

                const bool active = (SUCCEEDED (stateHr) && sessionState == AudioSessionStateActive);
                if (bestPid == 0 || (active && ! bestActive))
                {
                    bestPid = pid;
                    bestActive = active;
                }
            }
        }

        if (bestPid != 0)
            return bestPid;

        // Allowlisted apps may have no audio session yet — resolve by exe name.
        if (appKey.startsWith (WasapiLoopbackCapture::appIdPrefix))
        {
            const auto rest = appKey.fromFirstOccurrenceOf (WasapiLoopbackCapture::appIdPrefix, false, false);
            juce::String processName;

            if (rest.startsWith ("exe:"))
            {
                processName = rest.fromFirstOccurrenceOf ("exe:", false, false);
                if (processName.endsWithIgnoreCase (".exe"))
                    processName = processName.dropLastCharacters (4);
            }
            else if (rest.startsWith ("pid:"))
            {
                return (DWORD) rest.fromFirstOccurrenceOf ("pid:", false, false).getLargeIntValue();
            }
            else if (rest.containsChar ('/'))
            {
                processName = juce::File (rest.replaceCharacter ('/', '\\')).getFileNameWithoutExtension();
            }

            const auto pids = findPidsByProcessName (processName);
            if (! pids.isEmpty())
                return pids.getFirst();
        }

        return 0;
    }

    void appendAllowlistedRunningProcesses (juce::Array<AppSessionCandidate>& apps, juce::StringArray& seenKeys)
    {
        auto& allowlist = AppProcessAllowlist::get();
        allowlist.reload();
        if (! allowlist.isEnabled())
            return;

        const DWORD selfPid = GetCurrentProcessId();

        for (const auto& processName : allowlist.getProcessNames())
        {
            if (looksLikeSyncRoomDestination (processName))
                continue;

            const auto pids = findPidsByProcessName (processName);

            DWORD chosenPid = 0;
            for (const auto pid : pids)
            {
                if (pid == 0 || pid == selfPid)
                    continue;

                const auto imagePath = getProcessImagePath (pid);
                const auto exeName = getProcessExeNameFromSnapshot (pid);
                if (looksLikeSyncRoomDestination (imagePath)
                    || looksLikeSyncRoomDestination (exeName))
                    continue;

                chosenPid = pid;
                if (imagePath.isNotEmpty())
                    break;
            }

            // Always surface allowlisted names (running or not) so the capture
            // dropdown reflects what the user just saved.
            upgradeOrAddAllowlistedCandidate (apps, seenKeys, processName, chosenPid);
        }
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

void WasapiLoopbackCapture::setError (const juce::String& text)
{
    const juce::ScopedLock sl (errorLock);
    lastError = text;
}

juce::Array<WasapiLoopbackCapture::DeviceInfo> WasapiLoopbackCapture::getRenderDevices()
{
    juce::Array<DeviceInfo> list;

    DeviceInfo systemMix;
    systemMix.id = systemMixId;
    systemMix.name = juce::String (L"システム再生音");
    systemMix.isDefault = true;
    list.add (systemMix);

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
    WasapiComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED (enumerator->GetDefaultAudioEndpoint (eRender, eConsole,
                                                        defaultDevice.resetAndGetAddressOf())))
    {
        LPWSTR id = nullptr;
        if (SUCCEEDED (defaultDevice->GetId (&id)) && id != nullptr)
        {
            defaultId = juce::String (id);
            CoTaskMemFree (id);
        }
    }

    juce::Array<DeviceInfo> apps;
    juce::Array<AppSessionCandidate> candidates;
    juce::StringArray seenAppKeys;

    if (supportsApplicationLoopback())
    {
        WasapiComPtr<IMMDeviceCollection> collectionForSessions;
        // Include unplugged endpoints too: some apps keep sessions there briefly.
        if (SUCCEEDED (enumerator->EnumAudioEndpoints (eRender,
                                                       DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED,
                                                       collectionForSessions.resetAndGetAddressOf())))
        {
            UINT sessionDeviceCount = 0;
            collectionForSessions->GetCount (&sessionDeviceCount);

            for (UINT i = 0; i < sessionDeviceCount; ++i)
            {
                WasapiComPtr<IMMDevice> device;
                if (SUCCEEDED (collectionForSessions->Item (i, device.resetAndGetAddressOf())) && device)
                    appendAudioSessions (device.get(), candidates, seenAppKeys);
            }
        }

        appendAllowlistedRunningProcesses (candidates, seenAppKeys);

        for (const auto& c : candidates)
        {
            if (isWeakDisplayName (c.displayName))
                continue;

            DeviceInfo info;
            info.id = c.groupKey;
            info.name = juce::String (L"アプリ: ") + c.displayName;
            if (c.pid == 0)
                info.name += juce::String (L" （未起動）");
            apps.add (info);
        }

        struct AppNameComparator
        {
            int compareElements (const DeviceInfo& a, const DeviceInfo& b) const
            {
                return a.name.compareNatural (b.name);
            }
        };

        AppNameComparator comparator;
        apps.sort (comparator);
        list.addArray (apps);
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

        const bool isDefaultEndpoint = (info.id == defaultId);
        info.name = juce::String (L"デバイス: ") + info.name
                  + (isDefaultEndpoint ? juce::String (L" （既定エンドポイント）") : juce::String());
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

    pendingDeviceId = deviceId.isEmpty() ? juce::String (systemMixId) : deviceId;
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

    const juce::String effectiveId = deviceId.isEmpty() ? juce::String (systemMixId) : deviceId;

    if (effectiveId.startsWith (pidIdPrefix) || effectiveId.startsWith (appIdPrefix))
    {
        if (! supportsApplicationLoopback())
        {
            setError (juce::String (L"アプリ単位キャプチャには Windows 10 version 2004（ビルド 19041）以降が必要です。"));
            return false;
        }

        DWORD matchedPid = parsePidFromCaptureId (effectiveId);

        if (matchedPid == 0 && effectiveId.startsWith (appIdPrefix))
        {
            WasapiComPtr<IMMDeviceEnumerator> enumerator;
            HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                           __uuidof (IMMDeviceEnumerator),
                                           (void**) enumerator.resetAndGetAddressOf());
            if (FAILED (hr))
            {
                setError ("MMDeviceEnumerator failed: " + hresultToString (hr));
                return false;
            }

            matchedPid = findBestPidForAppKey (enumerator.get(), effectiveId);
        }

        if (matchedPid == 0)
        {
            setError (juce::String (L"指定アプリのプロセスが見つかりません。音を再生中のアプリで「更新」してから選び直してください。"));
            return false;
        }

        if (! openProcessLoopback (matchedPid))
            return false;

        activeDeviceId = effectiveId;
        return true;
    }

    const juce::String endpointId = (effectiveId == systemMixId) ? juce::String() : effectiveId;
    if (! openEndpointLoopback (endpointId))
        return false;

    activeDeviceId = effectiveId;
    return true;
}

bool WasapiLoopbackCapture::openProcessLoopback (juce::uint32 processId)
{
    auto activateProcessClient = [processId]() -> ProcessLoopbackActivateResult
    {
        ProcessLoopbackActivateResult result;

        auto* handler = new ActivateCompletionHandler();

        AUDIOCLIENT_ACTIVATION_PARAMS activationParams {};
        activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        activationParams.ProcessLoopbackParams.TargetProcessId = processId;
        activationParams.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

        PROPVARIANT activateParams {};
        PropVariantInit (&activateParams);
        activateParams.vt = VT_BLOB;
        activateParams.blob.cbSize = (ULONG) sizeof (activationParams);
        activateParams.blob.pBlobData = reinterpret_cast<BYTE*> (&activationParams);

        WasapiComPtr<IActivateAudioInterfaceAsyncOperation> asyncOp;
        const HRESULT hr = ActivateAudioInterfaceAsync (VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                                        __uuidof (IAudioClient),
                                                        &activateParams,
                                                        handler,
                                                        asyncOp.resetAndGetAddressOf());
        result.asyncCallHr = hr;
        if (FAILED (hr))
        {
            handler->Release();
            return result;
        }

        if (! handler->wait (8000))
        {
            result.timedOut = handler->timedOut;
            handler->Release();
            return result;
        }

        result.activateHr = handler->resultHr;
        if (FAILED (handler->resultHr) || handler->audioClient == nullptr)
        {
            handler->Release();
            return result;
        }

        *result.client.resetAndGetAddressOf() = handler->audioClient;
        handler->audioClient = nullptr;
        handler->Release();
        return result;
    };

    auto activation = activateProcessClient();
    native->audioClient = std::move (activation.client);
    if (! native->audioClient)
    {
        if (activation.timedOut)
        {
            setError (juce::String (L"アプリ単位ループバックの有効化がタイムアウトしました。PID ")
                      + juce::String ((juce::uint32) processId)
                      + juce::String (L" で再生中か確認してください。"));
        }
        else if (FAILED (activation.asyncCallHr))
        {
            setError (juce::String (L"ActivateAudioInterfaceAsync 失敗 (PID ")
                      + juce::String ((juce::uint32) processId) + "): "
                      + hresultToString (activation.asyncCallHr));
        }
        else if (FAILED (activation.activateHr))
        {
            setError (juce::String (L"プロセスループバック有効化失敗 (PID ")
                      + juce::String ((juce::uint32) processId) + "): "
                      + hresultToString (activation.activateHr));
        }
        else
        {
            setError (juce::String (L"IAudioClient の取得に失敗しました (PID ")
                      + juce::String ((juce::uint32) processId) + ")");
        }

        return false;
    }

    // Do NOT call GetMixFormat — unsupported on the process-loopback virtual device.
    WAVEFORMATEX captureFormat = makeProcessLoopbackFormat();
    native->mixFormat = (WAVEFORMATEX*) CoTaskMemAlloc (sizeof (WAVEFORMATEX));
    if (native->mixFormat == nullptr)
    {
        setError ("CoTaskMemAlloc failed for process loopback format");
        return false;
    }
    *native->mixFormat = captureFormat;

    native->eventHandle = CreateEventW (nullptr, FALSE, FALSE, nullptr);
    if (native->eventHandle == nullptr)
    {
        setError ("CreateEvent failed for process loopback");
        return false;
    }

    // Match Microsoft Application Loopback sample: explicit format, hnsBufferDuration=0.
    HRESULT hr = native->audioClient->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                                  AUDCLNT_STREAMFLAGS_LOOPBACK
                                                      | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                                                      | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                                  0,
                                                  0,
                                                  native->mixFormat,
                                                  nullptr);

    if (FAILED (hr))
    {
        // Fallback: PCM 16-bit @ 44100 as in the official sample.
        WAVEFORMATEX pcm16 {};
        pcm16.wFormatTag = WAVE_FORMAT_PCM;
        pcm16.nChannels = 2;
        pcm16.nSamplesPerSec = 44100;
        pcm16.wBitsPerSample = 16;
        pcm16.nBlockAlign = (WORD) (pcm16.nChannels * pcm16.wBitsPerSample / 8);
        pcm16.nAvgBytesPerSec = pcm16.nSamplesPerSec * pcm16.nBlockAlign;
        pcm16.cbSize = 0;
        *native->mixFormat = pcm16;

        // Must re-activate; a failed Initialize leaves the client unusable.
        native->audioClient.reset();
        auto retryActivation = activateProcessClient();
        native->audioClient = std::move (retryActivation.client);
        if (! native->audioClient)
        {
            setError ("Process loopback re-activate failed after Initialize: " + hresultToString (hr));
            return false;
        }

        hr = native->audioClient->Initialize (AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK
                                                  | AUDCLNT_STREAMFLAGS_EVENTCALLBACK
                                                  | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                              0,
                                              0,
                                              native->mixFormat,
                                              nullptr);
        if (FAILED (hr))
        {
            setError ("Process loopback Initialize failed: " + hresultToString (hr));
            return false;
        }
    }

    hr = native->audioClient->SetEventHandle (native->eventHandle);
    if (FAILED (hr))
    {
        setError ("SetEventHandle (process) failed: " + hresultToString (hr));
        return false;
    }

    hr = native->audioClient->GetService (__uuidof (IAudioCaptureClient),
                                          (void**) native->captureClient.resetAndGetAddressOf());
    if (FAILED (hr))
    {
        setError ("GetService IAudioCaptureClient (process) failed: " + hresultToString (hr));
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

    return true;
}

bool WasapiLoopbackCapture::openEndpointLoopback (const juce::String& deviceId)
{
    HRESULT hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   __uuidof (IMMDeviceEnumerator),
                                   (void**) native->enumerator.resetAndGetAddressOf());
    if (FAILED (hr))
    {
        setError ("MMDeviceEnumerator failed: " + hresultToString (hr));
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
        setError ("GetDevice failed: " + hresultToString (hr));
        return false;
    }

    hr = native->device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr,
                                   (void**) native->audioClient.resetAndGetAddressOf());
    if (FAILED (hr))
    {
        setError ("Activate IAudioClient failed: " + hresultToString (hr));
        return false;
    }

    hr = native->audioClient->GetMixFormat (&native->mixFormat);
    if (FAILED (hr) || native->mixFormat == nullptr)
    {
        setError ("GetMixFormat failed: " + hresultToString (hr));
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
            setError ("Re-Activate IAudioClient failed: " + hresultToString (hr));
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
            setError ("GetMixFormat (retry) failed: " + hresultToString (hr));
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
            setError ("IAudioClient::Initialize (loopback) failed: " + hresultToString (hr));
            return false;
        }
    }

    hr = native->audioClient->GetService (__uuidof (IAudioCaptureClient),
                                          (void**) native->captureClient.resetAndGetAddressOf());
    if (FAILED (hr))
    {
        setError ("GetService IAudioCaptureClient failed: " + hresultToString (hr));
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
