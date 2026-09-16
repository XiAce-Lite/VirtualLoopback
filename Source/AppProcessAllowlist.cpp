#include "AppProcessAllowlist.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <tlhelp32.h>
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

    bool isWindowsSystemImagePath (const juce::String& imagePath)
    {
        if (imagePath.isEmpty())
            return false;

        wchar_t winDir[MAX_PATH] = {};
        if (GetWindowsDirectoryW (winDir, MAX_PATH) == 0)
            return false;

        auto normalised = imagePath.replaceCharacter ('/', '\\');
        return normalised.startsWithIgnoreCase (juce::String (winDir) + "\\");
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
            "dllhost", "ctfmon", "explorer" // explorer is shell; rarely a capture target
        };

        for (auto* n : kNames)
            if (nameWithoutExt.equalsIgnoreCase (n))
                return true;

        return false;
    }

    struct EnumWindowsState
    {
        juce::Array<DWORD> pids;
    };

    BOOL CALLBACK collectVisibleAppWindowProc (HWND hwnd, LPARAM lParam)
    {
        auto* state = reinterpret_cast<EnumWindowsState*> (lParam);
        if (state == nullptr)
            return TRUE;

        if (! IsWindowVisible (hwnd))
            return TRUE;

        // Skip owned windows (tooltips, popups owned by another HWND).
        if (GetWindow (hwnd, GW_OWNER) != nullptr)
            return TRUE;

        // Skip untitled top-level windows (many background helpers).
        if (GetWindowTextLengthW (hwnd) <= 0)
            return TRUE;

        // Skip pure tool windows without app presence in the taskbar sense.
        const LONG_PTR exStyle = GetWindowLongPtrW (hwnd, GWL_EXSTYLE);
        if ((exStyle & WS_EX_TOOLWINDOW) != 0 && (exStyle & WS_EX_APPWINDOW) == 0)
            return TRUE;

        DWORD pid = 0;
        GetWindowThreadProcessId (hwnd, &pid);
        if (pid == 0 || pid == GetCurrentProcessId())
            return TRUE;

        if (! state->pids.contains (pid))
            state->pids.add (pid);

        return TRUE;
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

juce::StringArray AppProcessAllowlist::getRunningProcessNames()
{
    juce::StringArray names;

#if JUCE_WINDOWS
    EnumWindowsState windowState;
    EnumWindows (collectVisibleAppWindowProc, reinterpret_cast<LPARAM> (&windowState));

    // Map PID → exe via snapshot (works even when OpenProcess is denied).
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
                auto exe = juce::String (entry.szExeFile);
                if (exe.endsWithIgnoreCase (".exe"))
                    exe = exe.dropLastCharacters (4);
                if (exe.isNotEmpty())
                    pidToExe.set ((juce::uint32) entry.th32ProcessID, exe);
            }
            while (Process32NextW (snap, &entry));
        }
        CloseHandle (snap);
    }

    for (const auto pid : windowState.pids)
    {
        const auto exe = pidToExe[(juce::uint32) pid];
        if (exe.isEmpty()
            || isObviousSystemProcessName (exe)
            || isHiddenSyncRoomDestinationName (exe))
            continue;

        const auto imagePath = getImagePathForPid (pid);
        if (isWindowsSystemImagePath (imagePath))
            continue;

        names.addIfNotAlreadyThere (exe, true);
    }
#endif

    names.sort (true);
    return names;
}
