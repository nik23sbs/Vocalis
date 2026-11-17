cmake_minimum_required(VERSION 3.22)

# Su macOS: binario universale (Apple Silicon + Intel). Va impostato prima di project().
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Vocalis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- eSpeak-NG
set(USE_MBROLA     OFF CACHE BOOL "" FORCE)
set(USE_LIBPCAUDIO OFF CACHE BOOL "" FORCE)
set(USE_LIBSONIC   OFF CACHE BOOL "" FORCE)
set(USE_ASYNC      OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/espeak-ng EXCLUDE_FROM_ALL)
set(ESPEAK_DATA_DIR ${CMAKE_BINARY_DIR}/third_party/espeak-ng/espeak-ng-data)

# ---------------------------------------------------------------- JUCE
add_subdirectory(third_party/JUCE)

option(VOCALIS_BUILD_PLUGIN "Build the plugin" ON)
set(VOCALIS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VOCALIS_FORMATS AU)
endif()

if(VOCALIS_BUILD_PLUGIN)
juce_add_plugin(Vocalis
    COMPANY_NAME "Vocalis"
    PLUGIN_MANUFACTURER_CODE Vcls
    PLUGIN_CODE Vcl1
    FORMATS ${VOCALIS_FORMATS}
    PRODUCT_NAME "Vocalis"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(Vocalis PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Engine/ESpeakEngine.cpp
    Source/Engine/Syllabifier.cpp
    Source/DSP/TimeStretch.cpp
    Source/DSP/Quantizer.cpp
    Source/DSP/GrooveExtractor.cpp
    Source/DSP/EffectsChain.cpp
    Source/Render/Renderer.cpp
    Source/UI/VocalisLookAndFeel.cpp
    Source/UI/WaveformView.cpp
    Source/UI/GrooveView.cpp)

target_include_directories(Vocalis PRIVATE Source)

target_compile_definitions(Vocalis PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Vocalis
    PRIVATE
        espeak-ng
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# I dati di eSpeak (dizionari, fonemi) vanno dentro ogni bundle: Contents/Resources/espeak-ng-data
add_dependencies(Vocalis data)
foreach(fmt ${VOCALIS_FORMATS})
    # Binario in <bundle>/Contents/<arch o MacOS>/ -> i dati vanno in <bundle>/Contents/Resources
    if(fmt STREQUAL "Standalone" AND NOT APPLE)
        set(_res "$<TARGET_FILE_DIR:Vocalis_Standalone>/Resources")
    else()
        set(_res "$<TARGET_FILE_DIR:Vocalis_${fmt}>/../Resources")
    endif()
    add_custom_command(TARGET Vocalis_${fmt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_res}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ESPEAK_DATA_DIR}" "${_res}/espeak-ng-data"
        COMMENT "Copio i dati eSpeak nel bundle ${fmt}")
endforeach()
endif()

# Tool di test da riga di comando (render offline senza DAW)
option(VOCALIS_BUILD_TESTS "Build CLI test tool" ON)
if(VOCALIS_BUILD_TESTS)
    juce_add_console_app(VocalisCLI PRODUCT_NAME "VocalisCLI")
    target_sources(VocalisCLI PRIVATE
        Tests/VocalisCLI.cpp
        Source/Engine/ESpeakEngine.cpp
        Source/Engine/Syllabifier.cpp
        Source/DSP/TimeStretch.cpp
        Source/DSP/Quantizer.cpp
        Source/DSP/GrooveExtractor.cpp
        Source/DSP/EffectsChain.cpp
        Source/Render/Renderer.cpp)
    target_include_directories(VocalisCLI PRIVATE Source)
    target_compile_definitions(VocalisCLI PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(VocalisCLI PRIVATE espeak-ng juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
    add_dependencies(VocalisCLI data)
endif()

if(VOCALIS_BUILD_PLUGIN AND VOCALIS_BUILD_TESTS)
    juce_add_console_app(VocalisHostTest PRODUCT_NAME "VocalisHostTest")
    target_sources(VocalisHostTest PRIVATE Tests/HostTest.cpp)
    # usa lo stesso codice compilato del plugin (SharedCode) con le sue stesse definizioni
    target_include_directories(VocalisHostTest PRIVATE $<TARGET_PROPERTY:Vocalis,INCLUDE_DIRECTORIES>)
    target_compile_definitions(VocalisHostTest PRIVATE $<TARGET_PROPERTY:Vocalis,COMPILE_DEFINITIONS>)
    target_link_libraries(VocalisHostTest PRIVATE Vocalis)
endif()
