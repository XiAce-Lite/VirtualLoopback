#include "AppProcessAllowlist.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <tlhelp32.h>
 #pragma comment (lib, "version.lib")
#endif

namespace
{
    juce::String normaliseProcessName (juce::String name)
    {
        name = name.trim();
        if (name.endsWithIgnoreCase (".exe"))
            name = name.dropLastCharacters (4);
        return name;
    }

    // Hidden policy: Yamaha SYNCROOM / SYNCROOM2 themselves are never pickable.
    // SyncRoomChatTool* must remain available.
    bool isHiddenSyncRoomDestinationName (const juce::String& nameWithoutExt)
    {
        const auto n = nameWithoutExt.trim().toLowerCase();
        if (n.contains ("chattool") || n.contains ("syncroomchat"))
            return false;
        return n == "syncroom" || n == "syncroom2";
    }

#if JUCE_WINDOWS
    juce::String getImagePathForPid (DWORD pid)
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

    juce::String normalisedPath (juce::String path)
    {
        return path.replaceCharacter ('/', '\\');
    }

    bool pathStartsWithDir (const juce::String& imagePath, const juce::File& dir)
    {
        if (imagePath.isEmpty() || dir.getFullPathName().isEmpty())
            return false;

        auto prefix = normalisedPath (dir.getFullPathName());
        if (! prefix.endsWithChar ('\\'))
            prefix << '\\';

        return normalisedPath (imagePath).startsWithIgnoreCase (prefix);
    }

    bool isWindowsSystemImagePath (const juce::String& imagePath)
    {
        if (imagePath.isEmpty())
            return false;

        wchar_t winDir[MAX_PATH] = {};
        if (GetWindowsDirectoryW (winDir, MAX_PATH) == 0)
            return false;

        return pathStartsWithDir (imagePath, juce::File (winDir));
    }

    bool isLikelyUserAppImagePath (const juce::String& imagePath)
    {
        if (imagePath.isEmpty() || isWindowsSystemImagePath (imagePath))
            return false;

        if (pathStartsWithDir (imagePath, juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)))
            return true;

       #ifdef _WIN64
        {
            wchar_t pf86[MAX_PATH] = {};
            if (GetEnvironmentVariableW (L"ProgramFiles(x86)", pf86, MAX_PATH) > 0
                && pathStartsWithDir (imagePath, juce::File (pf86)))
                return true;
        }
       #endif

        if (pathStartsWithDir (imagePath, juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)))
            return true;

        if (pathStartsWithDir (imagePath, juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                             .getChildFile ("AppData")
                                             .getChildFile ("Local")))
            return true;

        if (pathStartsWithDir (imagePath, juce::File::getSpecialLocation (juce::File::userHomeDirectory)))
            return true;

        return false;
    }

    bool isObviousSystemProcessName (const juce::String& nameWithoutExt)
    {
        static const char* kNames[] = {
            "System", "Idle", "smss", "csrss", "wininit", "winlogon", "services",
            "lsass", "svchost", "fontdrvhost", "dwm", "conhost", "Memory Compression",
            "Registry", "sihost", "taskhostw", "RuntimeBroker", "SearchHost",
            "SearchIndexer", "ShellExperienceHost", "StartMenuExperienceHost",
            "TextInputHost", "ApplicationFrameHost", "SystemSettings",
            "SecurityHealthService", "MsMpEng", "NisSrv", "WmiPrvSE",
            "dllhost", "ctfmon", "explorer",
            "SearchApp", "WidgetService", "Widgets", "LockApp", "UserOOBEBroker",
            "backgroundTaskHost", "OpenWith", "PickerHost", "ConsentUxClient",
            "CompPkgSrv", "SgrmBroker", "spoolsv", "dasHost", "taskeng"
        };

        for (auto* n : kNames)
            if (nameWithoutExt.equalsIgnoreCase (n))
                return true;

        return false;
    }

    bool isHelperOrNoiseProcessName (const juce::String& nameWithoutExt)
    {
        const auto n = nameWithoutExt.toLowerCase();

        if (n.contains ("crashpad") || n.contains ("crashhandler") || n.endsWith ("_handler"))
            return true;

        if (n.contains ("atok") || n.startsWith ("ime") || n.contains ("skytree")
            || n.contains ("googleime") || n.contains ("msctf"))
            return true;

        if (n.contains ("msedgewebview") || n.contains ("webview2")
            || n.contains ("identity_helper") || n.contains ("elevation_service")
            || n.contains ("notification_helper") || n.contains ("browser_broker")
            || n.contains ("gpu-process") || n.contains ("utility"))
            return true;

        // Generic helpers / updaters (keep SyncRoomChatTool* etc.).
        if ((n.contains ("helper") || n.contains ("updater") || n.contains ("install"))
            && ! n.contains ("chattool") && ! n.contains ("syncroomchat"))
            return true;

        if (n.endsWith ("svc") || n.endsWith ("service"))
            return true;

        return false;
    }

    bool shouldHideProcessName (const juce::String& nameWithoutExt)
    {
        return nameWithoutExt.isEmpty()
            || isObviousSystemProcessName (nameWithoutExt)
            || isHiddenSyncRoomDestinationName (nameWithoutExt)
            || isHelperOrNoiseProcessName (nameWithoutExt);
    }

    juce::String getVersionStringField (const juce::String& imagePath, const wchar_t* field)
    {
        if (imagePath.isEmpty())
            return {};

        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeW (imagePath.toWideCharPointer(), &handle);
        if (size == 0)
            return {};

        juce::HeapBlock<juce::uint8> buffer ((size_t) size);
        if (! GetFileVersionInfoW (imagePath.toWideCharPointer(), 0, size, buffer.getData()))
            return {};

        struct LANGANDCODEPAGE { WORD language; WORD codePage; };
        LANGANDCODEPAGE* translate = nullptr;
        UINT translateBytes = 0;
        if (! VerQueryValueW (buffer.getData(), L"\\VarFileInfo\\Translation",
                              (LPVOID*) &translate, &translateBytes)
            || translate == nullptr || translateBytes < sizeof (LANGANDCODEPAGE))
            return {};

        wchar_t subBlock[128] = {};
        _snwprintf_s (subBlock, _TRUNCATE, L"\\StringFileInfo\\%04x%04x\\%s",
                      translate[0].language, translate[0].codePage, field);

        wchar_t* value = nullptr;
        UINT valueLen = 0;
        if (! VerQueryValueW (buffer.getData(), subBlock, (LPVOID*) &value, &valueLen)
            || value == nullptr || valueLen == 0)
            return {};

        return juce::String (value).trim();
    }

    juce::String friendlyNameFromImage (const juce::String& imagePath, const juce::String& processName)
    {
        auto fileDescription = getVersionStringField (imagePath, L"FileDescription");
        if (fileDescription.isNotEmpty()
            && ! fileDescription.equalsIgnoreCase (processName)
            && ! fileDescription.endsWithIgnoreCase (".exe"))
            return fileDescription;

        auto productName = getVersionStringField (imagePath, L"ProductName");
        if (productName.isNotEmpty()
            && ! productName.equalsIgnoreCase (processName))
            return productName;

        return processName;
    }

    bool isUsefulWindowTitle (const juce::String& title, const juce::String& processName)
    {
        const auto t = title.trim();
        if (t.isEmpty() || t.length() < 2)
            return false;
        if (t.equalsIgnoreCase (processName) || t.equalsIgnoreCase (processName + ".exe"))
            return false;
        // Skip generic host titles.
        if (t.containsIgnoreCase ("MSCTFIME") || t.containsIgnoreCase ("Default IME"))
            return false;
        return true;
    }

    struct EnumWindowsState
    {
        juce::Array<DWORD> pids;
        juce::HashMap<juce::uint32, juce::String> bestTitleByPid;
    };

    BOOL CALLBACK collectVisibleAppWindowProc (HWND hwnd, LPARAM lParam)
    {
        auto* state = reinterpret_cast<EnumWindowsState*> (lParam);
        if (state == nullptr)
            return TRUE;

        if (! IsWindowVisible (hwnd))
            return TRUE;

        if (GetWindow (hwnd, GW_OWNER) != nullptr)
            return TRUE;

        if (GetWindowTextLengthW (hwnd) <= 0)
            return TRUE;

        const LONG_PTR exStyle = GetWindowLongPtrW (hwnd, GWL_EXSTYLE);
        if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0)
            return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId (hwnd, &pid);
        if (pid == 0 || pid == GetCurrentProcessId())
            return TRUE;

        wchar_t titleW[512] = {};
        GetWindowTextW (hwnd, titleW, 511);
        const auto title = juce::String (titleW).trim();

        if (! state->pids.contains (pid))
            state->pids.add (pid);

        if (title.isNotEmpty())
        {
            const auto key = (juce::uint32) pid;
            const auto existing = state->bestTitleByPid[key];
            if (title.length() > existing.length())
                state->bestTitleByPid.set (key, title);
        }

        return TRUE;
    }

    juce::String makeDisplayName (const juce::String& processName,
                                  const juce::String& imagePath,
                                  const juce::String& windowTitle)
    {
        if (isUsefulWindowTitle (windowTitle, processName))
        {
            // Prefer short-ish titles; very long URLs etc. fall back to version info.
            if (windowTitle.length() <= 64)
                return windowTitle;
        }

        const auto fromFile = friendlyNameFromImage (imagePath, processName);
        if (fromFile.isNotEmpty() && fromFile != processName)
            return fromFile;

        if (isUsefulWindowTitle (windowTitle, processName))
            return windowTitle;

        return processName;
    }

    void upsertApp (juce::Array<AppProcessAllowlist::RunningAppInfo>& apps,
                    juce::StringArray& seenProcessNames,
                    const juce::String& processName,
                    const juce::String& displayName)
    {
        if (processName.isEmpty() || shouldHideProcessName (processName))
            return;

        const int existing = seenProcessNames.indexOf (processName, true);
        if (existing < 0)
        {
            seenProcessNames.add (processName);
            AppProcessAllowlist::RunningAppInfo info;
            info.processName = processName;
            info.displayName = displayName.isNotEmpty() ? displayName : processName;
            apps.add (info);
            return;
        }

        auto& prev = apps.getReference (existing);
        // Prefer a friendlier / longer display name when we learn more.
        if (prev.displayName.equalsIgnoreCase (prev.processName)
            && ! displayName.equalsIgnoreCase (processName)
            && displayName.isNotEmpty())
        {
            prev.displayName = displayName;
        }
        else if (displayName.length() > prev.displayName.length()
                 && ! displayName.equalsIgnoreCase (processName))
        {
            prev.displayName = displayName;
        }
    }
#endif
}

AppProcessAllowlist& AppProcessAllowlist::get()
{
    static AppProcessAllowlist instance;
    return instance;
}

AppProcessAllowlist::AppProcessAllowlist()
{
    reload();
}

juce::File AppProcessAllowlist::getSettingsDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("VirtualLoopback");
}

juce::File AppProcessAllowlist::getSettingsFile() const
{
    return getSettingsDirectory().getChildFile ("app_process_allowlist.txt");
}

void AppProcessAllowlist::reload()
{
    enabled = false;
    processNames.clear();

    const auto file = getSettingsFile();
    if (! file.existsAsFile())
        return;

    juce::StringArray lines;
    file.readLines (lines);

    for (auto line : lines)
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar ('#') || line.startsWithChar (';'))
            continue;

        if (line.startsWithIgnoreCase ("enabled="))
        {
            const auto value = line.fromFirstOccurrenceOf ("=", false, false).trim().toLowerCase();
            enabled = (value == "1" || value == "true" || value == "yes" || value == "on");
            continue;
        }

        const auto name = normaliseProcessName (line);
        if (name.isNotEmpty()
            && ! isHiddenSyncRoomDestinationName (name)
            && ! processNames.contains (name, true))
            processNames.add (name);
    }
}

bool AppProcessAllowlist::save() const
{
    auto dir = getSettingsDirectory();
    if (! dir.exists())
        dir.createDirectory();

    juce::StringArray out;
    out.add ("# VirtualLoopback process allowlist");
    out.add ("# One process name per line (no .exe). Example: SyncRoomChatToolV2");
    out.add ("enabled=" + juce::String (enabled ? "1" : "0"));
    out.add ("");

    for (const auto& name : processNames)
    {
        const auto n = normaliseProcessName (name);
        if (n.isNotEmpty() && ! isHiddenSyncRoomDestinationName (n))
            out.add (n);
    }

    return getSettingsFile().replaceWithText (out.joinIntoString ("\n") + "\n");
}

void AppProcessAllowlist::setEnabled (bool shouldBeEnabled)
{
    enabled = shouldBeEnabled;
}

juce::StringArray AppProcessAllowlist::getProcessNames() const
{
    return processNames;
}

void AppProcessAllowlist::setProcessNames (const juce::StringArray& names)
{
    processNames.clear();
    for (const auto& name : names)
    {
        const auto n = normaliseProcessName (name);
        if (n.isNotEmpty()
            && ! isHiddenSyncRoomDestinationName (n)
            && ! processNames.contains (n, true))
            processNames.add (n);
    }
}

juce::String AppProcessAllowlist::getProcessNamesAsText() const
{
    return processNames.joinIntoString ("\n");
}

void AppProcessAllowlist::setProcessNamesFromText (const juce::String& text)
{
    juce::StringArray lines;
    lines.addLines (text);
    setProcessNames (lines);
}

juce::Array<AppProcessAllowlist::RunningAppInfo> AppProcessAllowlist::getRunningApps (bool includeBroaderApps)
{
    juce::Array<RunningAppInfo> apps;

#if JUCE_WINDOWS
    juce::HashMap<juce::uint32, juce::String> pidToExe;
    HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof (entry);
        if (Process32FirstW (snap, &entry))
        {
            do
            {
                const auto exe = normaliseProcessName (juce::String (entry.szExeFile));
                if (exe.isNotEmpty())
                    pidToExe.set ((juce::uint32) entry.th32ProcessID, exe);
            }
            while (Process32NextW (snap, &entry));
        }
        CloseHandle (snap);
    }

    EnumWindowsState windowState;
    EnumWindows (collectVisibleAppWindowProc, reinterpret_cast<LPARAM> (&windowState));

    juce::StringArray seen;
    juce::Array<DWORD> windowedPids = windowState.pids;

    for (const auto pid : windowedPids)
    {
        const auto exe = pidToExe[(juce::uint32) pid];
        if (shouldHideProcessName (exe))
            continue;

        const auto imagePath = getImagePathForPid (pid);
        if (isWindowsSystemImagePath (imagePath))
            continue;

        const auto title = windowState.bestTitleByPid[(juce::uint32) pid];
        upsertApp (apps, seen, exe, makeDisplayName (exe, imagePath, title));
    }

    if (includeBroaderApps)
    {
        for (juce::HashMap<juce::uint32, juce::String>::Iterator it (pidToExe); it.next();)
        {
            const auto pid = (DWORD) it.getKey();
            if (pid == 0 || pid == GetCurrentProcessId())
                continue;

            const auto exe = it.getValue();
            if (shouldHideProcessName (exe))
                continue;

            // Browser / shell hosts without a real app window stay out of the broader list.
            if ((exe.equalsIgnoreCase ("msedge") || exe.equalsIgnoreCase ("chrome")
                 || exe.equalsIgnoreCase ("firefox") || exe.equalsIgnoreCase ("opera"))
                && ! windowedPids.contains (pid))
                continue;

            const auto imagePath = getImagePathForPid (pid);
            if (! isLikelyUserAppImagePath (imagePath))
                continue;

            const auto title = windowState.bestTitleByPid[(juce::uint32) pid];
            upsertApp (apps, seen, exe, makeDisplayName (exe, imagePath, title));
        }
    }

    struct DisplayNameComparator
    {
        int compareElements (const RunningAppInfo& a, const RunningAppInfo& b) const
        {
            return a.displayName.compareNatural (b.displayName);
        }
    };

    DisplayNameComparator comparator;
    apps.sort (comparator);
#endif

    return apps;
}
