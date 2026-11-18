cmake_minimum_required(VERSION 3.22)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Mac architectures")
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(XoxicoaSampler VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(XSMP_BUILD_TESTS "Build the offline test harness + UI snapshot tool" OFF)
set(XSMP_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout (otherwise fetched from GitHub)")

if(XSMP_JUCE_DIR)
    add_subdirectory(${XSMP_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(XoxicoaSampler
    COMPANY_NAME              "Xoxicoa"
    PRODUCT_NAME              "Xoxicoa Sampler"
    BUNDLE_ID                 com.xoxicoa.sampler
    PLUGIN_MANUFACTURER_CODE  Xoxi
    PLUGIN_CODE               XxSm
    FORMATS                   AU VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           Instrument Sampler
    AU_MAIN_TYPE              kAudioUnitType_MusicDevice)

target_sources(XoxicoaSampler PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(XoxicoaSampler PRIVATE Source ThirdParty)

target_compile_definitions(XoxicoaSampler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(XoxicoaSampler
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(XSMP_BUILD_TESTS)
    foreach(tool XoxicoaSamplerTest XoxicoaSamplerUISnapshot)
        juce_add_console_app(${tool} PRODUCT_NAME "${tool}")
        target_include_directories(${tool} PRIVATE Source ThirdParty tests)
        target_compile_definitions(${tool} PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(${tool} PRIVATE juce::juce_audio_utils juce::juce_dsp
            PUBLIC juce::juce_recommended_config_flags)
    endforeach()
    target_sources(XoxicoaSamplerTest PRIVATE tests/SamplerTest.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_sources(XoxicoaSamplerUISnapshot PRIVATE tests/UISnapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
endif()
