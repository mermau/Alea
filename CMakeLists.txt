cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel) su macOS
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(ALEA VERSION 1.3.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: usa una copia locale se indicata (-DJUCE_DIR=...), altrimenti la scarica
set(JUCE_DIR "" CACHE PATH "Cartella di JUCE (opzionale)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(ALEA
    COMPANY_NAME "Collateral Effects"
    BUNDLE_ID com.collateraleffects.alea
    PLUGIN_MANUFACTURER_CODE Clfx
    PLUGIN_CODE Alea
    PRODUCT_NAME "ALEA"
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE          # riceve i CC dalla tastiera controller
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Filter
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    ICON_BIG resources/icon.png
    MICROPHONE_PERMISSION_ENABLED TRUE        # macOS: senza, la Standalone riceve solo silenzio
    MICROPHONE_PERMISSION_TEXT "ALEA usa l'ingresso della scheda audio per elaborare il segnale della chitarra."
    COPY_PLUGIN_AFTER_BUILD TRUE)

juce_add_binary_data(ALEAFonts SOURCES
    resources/fonts/ArchivoBlack-Regular.ttf
    resources/fonts/Inter-SemiBold.otf)

target_sources(ALEA PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ALEA PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ALEA
    PRIVATE ALEAFonts juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Test del modello DSP (senza JUCE)
option(ALEA_BUILD_TESTS "Compila i test del circuito" ON)
if(ALEA_BUILD_TESTS)
    add_executable(circuit_test tests/circuit_test.cpp)
    target_include_directories(circuit_test PRIVATE Source)
endif()

# Solo per sviluppo: salva l'interfaccia in PNG (cmake -DALEA_SNAPSHOT=ON)
option(ALEA_SNAPSHOT "Compila lo strumento di screenshot dell'interfaccia" OFF)
if(ALEA_SNAPSHOT)
    juce_add_console_app(gui_snapshot PRODUCT_NAME "gui_snapshot")
    target_sources(gui_snapshot PRIVATE tests/gui_snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(gui_snapshot PRIVATE Source)
    target_compile_definitions(gui_snapshot PRIVATE JucePlugin_Name="ALEA" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(gui_snapshot PRIVATE ALEAFonts juce::juce_audio_utils juce::juce_dsp)
endif()
