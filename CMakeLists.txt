
cmake_minimum_required(VERSION 3.22)

project(MuseScoreJACKBridge
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "" CACHE PATH "Path to JUCE source")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Set JUCE_DIR to your JUCE source directory"
    )
endif()

add_subdirectory("${JUCE_DIR}" JUCE)

find_package(PkgConfig REQUIRED)

pkg_check_modules(
    JACK REQUIRED IMPORTED_TARGET jack
)

juce_add_plugin(MuseScoreJACKBridge
    COMPANY_NAME "GraffesMusic"
    BUNDLE_ID "org.musescore.jackbridge"
    PLUGIN_MANUFACTURER_CODE MsBr
    PLUGIN_CODE MsJB
    PRODUCT_NAME "MuseScoreJACKBridge"

    FORMATS VST3

    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    VST3_CAN_REPLACE_VST2 FALSE

    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(
    MuseScoreJACKBridge
)

target_sources(MuseScoreJACKBridge
    PRIVATE
        Source/MidiQueue.h
        Source/JackClient.h
        Source/JackClient.cpp
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
)

target_compile_definitions(
    MuseScoreJACKBridge
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(
    MuseScoreJACKBridge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        ${CMAKE_DL_LIBS}

    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_features(
    MuseScoreJACKBridge
    PRIVATE
        cxx_std_17
)

