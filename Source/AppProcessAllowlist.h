#pragma once

#include <JuceHeader.h>

//==============================================================================
/** Optional process-name allowlist for per-app capture candidates.
    Stored under the user application data folder (not per-project).
    Process names are without extension (e.g. SyncRoomChatToolV2).
*/
class AppProcessAllowlist
{
public:
    struct RunningAppInfo
    {
        juce::String processName;  // allowlist / capture key (no .exe)
        juce::String displayName;  // Task Manager-like label for UI
    };

    static AppProcessAllowlist& get();

    void reload();
    bool save() const;

    bool isEnabled() const noexcept { return enabled; }
    void setEnabled (bool shouldBeEnabled);

    juce::StringArray getProcessNames() const;
    void setProcessNames (const juce::StringArray& names);

    juce::String getProcessNamesAsText() const;
    void setProcessNamesFromText (const juce::String& text);

    /** Task Manager "Apps"-ish candidates (Windows). Empty on other platforms.
        @param includeBroaderApps  false = visible-window apps only.
                                   true  = also Program Files / user-profile installs
                                   (still skips services, helpers, IME, etc.).
    */
    static juce::Array<RunningAppInfo> getRunningApps (bool includeBroaderApps = false);

    juce::File getSettingsDirectory() const;
    juce::File getSettingsFile() const;

private:
    AppProcessAllowlist();

    bool enabled = false;
    juce::StringArray processNames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppProcessAllowlist)
};
