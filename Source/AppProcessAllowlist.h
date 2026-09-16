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
    static AppProcessAllowlist& get();

    void reload();
    bool save() const;

    bool isEnabled() const noexcept { return enabled; }
    void setEnabled (bool shouldBeEnabled);

    juce::StringArray getProcessNames() const;
    void setProcessNames (const juce::StringArray& names);

    juce::String getProcessNamesAsText() const;
    void setProcessNamesFromText (const juce::String& text);

    /** Running user-facing app process names without .exe (Windows).
        Prefers processes that own a visible top-level window; skips Windows system images.
        Empty on other platforms.
    */
    static juce::StringArray getRunningProcessNames();

    juce::File getSettingsDirectory() const;
    juce::File getSettingsFile() const;

private:
    AppProcessAllowlist();

    bool enabled = false;
    juce::StringArray processNames;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppProcessAllowlist)
};
