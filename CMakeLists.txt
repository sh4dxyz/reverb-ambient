cmake_minimum_required (VERSION 3.22)

project (HorizonNoir VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set_property (GLOBAL PROPERTY USE_FOLDERS ON)

option (HN_BUILD_PLUGIN "Build the JUCE plugin (downloads JUCE)" ON)
option (HN_BUILD_TESTS "Build the DSP unit tests" ON)

if (MSVC)
    # Sources contain UTF-8; static runtime so the plugin has no VC++ redistributable dependency.
    add_compile_options (/utf-8)
    set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

# ---------------------------------------------------------------------------------------
# DSP unit tests (plain C++, no JUCE)
# ---------------------------------------------------------------------------------------
if (HN_BUILD_TESTS)
    enable_testing ()
    add_executable (DspTests tests/DspTests.cpp)
    target_include_directories (DspTests PRIVATE src)
    add_test (NAME DspTests COMMAND DspTests)
endif ()

# ---------------------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------------------
if (HN_BUILD_PLUGIN)
    include (FetchContent)
    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable (JUCE)

    set (HN_FORMATS VST3 Standalone)
    if (APPLE)
        list (APPEND HN_FORMATS AU)
    endif ()

    juce_add_plugin (HorizonNoir
        PRODUCT_NAME "Horizon Noir"
        COMPANY_NAME "sh4d"
        BUNDLE_ID "com.sh4d.horizonnoir"
        PLUGIN_MANUFACTURER_CODE Sh4d
        PLUGIN_CODE HzNr
        FORMATS ${HN_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Reverb
        AU_MAIN_TYPE kAudioUnitType_Effect
        DESCRIPTION "Dark ambient FDN reverb with shimmer and infinite freeze")

    juce_add_binary_data (HorizonNoirAssets SOURCES
        assets/fonts/Syncopate-Bold.ttf
        assets/fonts/Rajdhani-SemiBold.ttf
        assets/fonts/Rajdhani-Medium.ttf)

    target_sources (HorizonNoir PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/NebulaRenderer.cpp
        src/plugin/HorizonLookAndFeel.cpp
        src/plugin/DragExportZone.cpp)

    target_include_directories (HorizonNoir PRIVATE src src/plugin)

    target_compile_definitions (HorizonNoir PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)

    target_link_libraries (HorizonNoir
        PRIVATE
            HorizonNoirAssets
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_opengl
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif ()
