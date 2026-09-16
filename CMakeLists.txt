cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.2" CACHE STRING "Minimum macOS version" FORCE)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal macOS build" FORCE)

project(VirtualLoopback VERSION 1.0.1 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "14.2")

include(FetchContent)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(VirtualLoopback
    COMPANY_NAME "XiAceLite"
    COMPANY_WEBSITE "https://github.com/XiAce-Lite"
    BUNDLE_ID com.XiAceLite.VirtualLoopback
    PLUGIN_MANUFACTURER_CODE XiAL
    PLUGIN_CODE VLbk
    FORMATS VST3 AU
    PRODUCT_NAME "VirtualLoopback"
    DESCRIPTION "PC playback loopback (WASAPI / Core Audio Tap)"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE
    HARDENED_RUNTIME_ENABLED FALSE
    APP_SANDBOX_ENABLED FALSE
    PLIST_TO_MERGE
        "<key>NSAudioCaptureUsageDescription</key><string>VirtualLoopback needs System Audio Recording permission to capture playback from Chrome and other apps into your DAW.</string>"
)

juce_generate_juce_header(VirtualLoopback)

# AU and VST3 must not share one CFBundleIdentifier on macOS.
# Same ID can make hosts see AU while silently failing VST3 validation.
if(APPLE)
    set_target_properties(VirtualLoopback_AU PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.XiAceLite.VirtualLoopback.au"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.XiAceLite.VirtualLoopback.au"
    )
    set_target_properties(VirtualLoopback_VST3 PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.XiAceLite.VirtualLoopback.vst3"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.XiAceLite.VirtualLoopback.vst3"
    )
endif()

set(VL_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/WasapiLoopbackCapture.cpp
    Source/AppProcessAllowlist.cpp
)

if(APPLE)
    list(APPEND VL_SOURCES Source/CoreAudioTapCapture.mm)
endif()

target_sources(VirtualLoopback PRIVATE ${VL_SOURCES})

target_compile_definitions(VirtualLoopback
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(VirtualLoopback
    PRIVATE
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
)
