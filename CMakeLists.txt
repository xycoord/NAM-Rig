cmake_minimum_required(VERSION 3.22)

project(NamRig VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(libs/JUCE)
# CLAP support (formats beyond Standalone; VST3 deferred — CLAP covers Reaper).
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

# ---- Vendored DSP (framework-free) -----------------------------------------
# The NAM core has no embeddable CMake target of its own; we compile its
# sources into a static lib (the LV2 plugin does the same). Float samples
# throughout to match JUCE's buffers.

set(NAM_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore)
set(ADT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/AudioDSPTools)

file(GLOB NAM_CORE_SOURCES
    ${NAM_CORE_DIR}/NAM/*.cpp
    ${NAM_CORE_DIR}/NAM/wavenet/*.cpp)

add_library(nam_core OBJECT ${NAM_CORE_SOURCES})
target_include_directories(nam_core SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/eigen
    ${NAM_CORE_DIR}/Dependencies/nlohmann
    ${NAM_CORE_DIR})
target_compile_definitions(nam_core PUBLIC
    NAM_SAMPLE_FLOAT
    NAM_ENABLE_A2_FAST)
if(WIN32)
    # The core's own CMake sets these for its targets; we compile its
    # sources ourselves, so mirror them.
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Note: ImpulseResponse.cpp/wav.cpp are NOT compiled — they hardcode double
# and break with DSP_SAMPLE=float. IR convolution uses juce::dsp::Convolution
# instead (docs/plan.md watchlist).
add_library(adt OBJECT
    ${ADT_DIR}/dsp/dsp.cpp
    ${ADT_DIR}/dsp/RecursiveLinearFilter.cpp)
target_include_directories(adt SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/eigen
    ${ADT_DIR})
target_compile_definitions(adt PUBLIC DSP_SAMPLE_FLOAT)

# ---- Engine (no JUCE anywhere) ----------------------------------------------
add_library(namrig_engine STATIC
    src/state/PathResolve.cpp
    src/engine/Architectures.cpp
    src/engine/ResamplingNam.cpp
    src/engine/ModelSlot.cpp
    src/engine/Tuner.cpp
    src/engine/Engine.cpp)
target_include_directories(namrig_engine PUBLIC src)
target_link_libraries(namrig_engine PUBLIC nam_core adt)

# Plugin formats are shared modules; everything they swallow must be PIC
# (the standalone doesn't care, the .clap link fails without it).
set_target_properties(nam_core adt namrig_engine
    PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Embedded UI font (Barlow, OFL) — identical rendering on every platform.
juce_add_binary_data(NamRigAssets SOURCES
    assets/fonts/Barlow-Regular.ttf
    assets/fonts/Barlow-Medium.ttf
    assets/fonts/Barlow-SemiBold.ttf)
set_target_properties(NamRigAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    src/state/Library.cpp
    src/state/Parameters.cpp
    src/ui/Editor.cpp)

target_include_directories(NamRig PRIVATE src)

target_compile_definitions(NamRig PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # JACK backend (via pipewire-jack at runtime): direct graph scheduling,
    # lower latency than the ALSA bridge, and patchable routing. Linux-only:
    # JACK also exists on macOS, where setting this makes JUCE look for
    # headers we don't ship.
    $<$<PLATFORM_ID:Linux>:JUCE_JACK=1>
    $<$<PLATFORM_ID:Linux>:JUCE_ALSA=1>
    # Our own standalone shell (src/standalone/): native title bar,
    # input unmuted by default.
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

# nam_core/adt are OBJECT libs: their objects (and the static initializers
# that register model architectures) only reach DIRECT consumers, so the
# final targets must link them explicitly alongside namrig_engine.
# PUBLIC so the OBJECT libraries' objects (and their architecture-registry
# static initializers) reach every final binary: the standalone now, the
# VST3/CLAP targets in milestone 5. PRIVATE would strand them in the
# shared-code static lib, where the linker drops them.
target_link_libraries(NamRig
    PUBLIC
        namrig_engine
        nam_core
        adt
        NamRigAssets
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- CLAP ------------------------------------------------------------------
clap_juce_extensions_plugin(TARGET NamRig
    CLAP_ID "com.xycoord.namrig"
    CLAP_FEATURES audio-effect distortion "guitar amp")

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
    target_link_libraries(namrig_tests PRIVATE namrig_engine nam_core adt Catch2::Catch2WithMain)
    target_compile_definitions(namrig_tests PRIVATE
        NAMRIG_TEST_MODELS_DIR="${NAM_CORE_DIR}/example_models"
        _USE_MATH_DEFINES) # M_PI on MSVC

    enable_testing()
    add_test(NAME namrig_tests COMMAND namrig_tests)
endif()
