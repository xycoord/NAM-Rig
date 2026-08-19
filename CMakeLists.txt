cmake_minimum_required(VERSION 3.22)

project(NamRig VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Milestone 1: standalone only. CLAP + VST3 targets arrive in milestone 5.
add_subdirectory(libs/JUCE)

juce_add_plugin(NamRig
    COMPANY_NAME "xycoord"
    PRODUCT_NAME "NAM Rig"
    BUNDLE_ID com.xycoord.namrig
    PLUGIN_MANUFACTURER_CODE Xyco
    PLUGIN_CODE NamR
    FORMATS Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE)

target_sources(NamRig PRIVATE
    src/PluginProcessor.cpp
    src/standalone/StandaloneApp.cpp
    src/state/Parameters.cpp
    src/ui/Editor.cpp)

target_include_directories(NamRig PRIVATE src)

target_compile_definitions(NamRig PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # JACK backend (via pipewire-jack at runtime): direct graph scheduling,
    # lower latency than the ALSA bridge, and patchable routing.
    JUCE_JACK=1
    JUCE_ALSA=1
    # Our own standalone shell (src/standalone/): native title bar,
    # input unmuted by default.
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

target_link_libraries(NamRig
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Tests -----------------------------------------------------------------
option(NAMRIG_BUILD_TESTS "Build unit tests" ON)

if(NAMRIG_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.13.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)

    add_executable(namrig_tests
        tests/engine_tests.cpp)
    target_include_directories(namrig_tests PRIVATE src)
    target_link_libraries(namrig_tests PRIVATE Catch2::Catch2WithMain)

    enable_testing()
    add_test(NAME namrig_tests COMMAND namrig_tests)
endif()
