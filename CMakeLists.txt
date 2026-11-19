cmake_minimum_required(VERSION 3.22)

# Su macOS: binario universale (Apple Silicon + Intel). Va impostato prima di project().
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Vocalis VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------- eSpeak-NG (fonetica + voci robot)
set(USE_MBROLA     OFF CACHE BOOL "" FORCE)
set(USE_LIBPCAUDIO OFF CACHE BOOL "" FORCE)
set(USE_LIBSONIC   OFF CACHE BOOL "" FORCE)
set(USE_ASYNC      OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTS   OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/espeak-ng EXCLUDE_FROM_ALL)
set(ESPEAK_DATA_DIR ${CMAKE_BINARY_DIR}/third_party/espeak-ng/espeak-ng-data)

# ---------------------------------------------------------------- WORLD (vocoder per pitch/formanti)
add_library(world STATIC
    third_party/World/src/cheaptrick.cpp
    third_party/World/src/common.cpp
    third_party/World/src/d4c.cpp
    third_party/World/src/dio.cpp
    third_party/World/src/fft.cpp
    third_party/World/src/harvest.cpp
    third_party/World/src/matlabfunctions.cpp
    third_party/World/src/stonemask.cpp
    third_party/World/src/synthesis.cpp)
target_include_directories(world PUBLIC third_party/World/src)
if(NOT MSVC)
    target_compile_options(world PRIVATE -w -O3)
endif()

# ---------------------------------------------------------------- ONNX Runtime (voce neurale Kokoro)
# -DVOCALIS_ORT_DIR=<cartella estratta del pacchetto onnxruntime-...tgz> attiva il motore neurale.
set(VOCALIS_ORT_DIR "" CACHE PATH "Cartella di ONNX Runtime (include/ e lib/)")
set(VOCALIS_HAS_ORT OFF)
if(VOCALIS_ORT_DIR AND EXISTS "${VOCALIS_ORT_DIR}/include/onnxruntime_cxx_api.h")
    if(APPLE)
        file(GLOB ORT_LIBS "${VOCALIS_ORT_DIR}/lib/libonnxruntime.*.dylib")
    else()
        file(GLOB ORT_LIBS "${VOCALIS_ORT_DIR}/lib/libonnxruntime.so.*")
    endif()
    list(FILTER ORT_LIBS EXCLUDE REGEX "providers")
    list(GET ORT_LIBS 0 ORT_LIB)
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ORT_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${VOCALIS_ORT_DIR}/include")
    set(VOCALIS_HAS_ORT ON)
    message(STATUS "Vocalis: motore neurale ATTIVO (ONNX Runtime: ${ORT_LIB})")
else()
    message(STATUS "Vocalis: motore neurale non incluso (manca VOCALIS_ORT_DIR)")
endif()

set(VOCALIS_CORE_SOURCES
    Source/Engine/ESpeakCore.cpp
    Source/Engine/ESpeakEngine.cpp
    Source/Engine/Phonemizer.cpp
    Source/Engine/KokoroVoices.cpp
    Source/Engine/KokoroAligner.cpp
    Source/Engine/KokoroEngine.cpp
    Source/Engine/Syllabifier.cpp
    Source/DSP/TimeStretch.cpp
    Source/DSP/Quantizer.cpp
    Source/DSP/GrooveExtractor.cpp
    Source/DSP/EffectsChain.cpp
    Source/DSP/WorldVoice.cpp
    Source/DSP/Loudness.cpp
    Source/Render/Renderer.cpp)

function(vocalis_link_core target)
    target_include_directories(${target} PRIVATE Source)
    target_link_libraries(${target} PRIVATE espeak-ng world)
    if(VOCALIS_HAS_ORT)
        target_link_libraries(${target} PRIVATE onnxruntime)
        target_compile_definitions(${target} PRIVATE VOCALIS_HAS_ORT=1)
    endif()
endfunction()

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
    ${VOCALIS_CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/VocalisLookAndFeel.cpp
    Source/UI/SyllableEditor.cpp
    Source/UI/GrooveView.cpp)

vocalis_link_core(Vocalis)

target_compile_definitions(Vocalis PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Vocalis
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# eSpeak-data in Contents/Resources di ogni bundle; ONNX Runtime in Contents/Frameworks (macOS) o accanto al binario
add_dependencies(Vocalis data)
foreach(fmt ${VOCALIS_FORMATS})
    if(fmt STREQUAL "Standalone" AND NOT APPLE)
        set(_res "$<TARGET_FILE_DIR:Vocalis_Standalone>/Resources")
    else()
        set(_res "$<TARGET_FILE_DIR:Vocalis_${fmt}>/../Resources")
    endif()
    add_custom_command(TARGET Vocalis_${fmt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_res}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ESPEAK_DATA_DIR}" "${_res}/espeak-ng-data"
        COMMENT "Copio i dati eSpeak nel bundle ${fmt}")

    if(VOCALIS_HAS_ORT)
        if(APPLE)
            set(_fw "$<TARGET_FILE_DIR:Vocalis_${fmt}>/../Frameworks")
            set_target_properties(Vocalis_${fmt} PROPERTIES BUILD_RPATH "@loader_path/../Frameworks" INSTALL_RPATH "@loader_path/../Frameworks")
        else()
            set(_fw "$<TARGET_FILE_DIR:Vocalis_${fmt}>")
            set_target_properties(Vocalis_${fmt} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
        endif()
        add_custom_command(TARGET Vocalis_${fmt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_fw}"
            COMMAND ${CMAKE_COMMAND} -E copy "${ORT_LIB}" "${_fw}/"
            COMMENT "Copio ONNX Runtime nel bundle ${fmt}")
    endif()
endforeach()
endif()

# ---------------------------------------------------------------- Strumenti di test
option(VOCALIS_BUILD_TESTS "Build CLI test tools" ON)
if(VOCALIS_BUILD_TESTS)
    juce_add_console_app(VocalisCLI PRODUCT_NAME "VocalisCLI")
    target_sources(VocalisCLI PRIVATE Tests/VocalisCLI.cpp ${VOCALIS_CORE_SOURCES})
    vocalis_link_core(VocalisCLI)
    target_compile_definitions(VocalisCLI PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(VocalisCLI PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    add_dependencies(VocalisCLI data)
    if(VOCALIS_HAS_ORT)
        get_filename_component(_ortdir "${ORT_LIB}" DIRECTORY)
        set_target_properties(VocalisCLI PROPERTIES BUILD_RPATH "${_ortdir}")
    endif()
endif()

if(VOCALIS_BUILD_PLUGIN AND VOCALIS_BUILD_TESTS)
    juce_add_console_app(VocalisHostTest PRODUCT_NAME "VocalisHostTest")
    target_sources(VocalisHostTest PRIVATE Tests/HostTest.cpp)
    # usa lo stesso codice compilato del plugin (SharedCode) con le sue stesse definizioni
    target_include_directories(VocalisHostTest PRIVATE $<TARGET_PROPERTY:Vocalis,INCLUDE_DIRECTORIES>)
    target_compile_definitions(VocalisHostTest PRIVATE $<TARGET_PROPERTY:Vocalis,COMPILE_DEFINITIONS>)
    target_link_libraries(VocalisHostTest PRIVATE Vocalis)
    if(VOCALIS_HAS_ORT)
        get_filename_component(_ortdir "${ORT_LIB}" DIRECTORY)
        set_target_properties(VocalisHostTest PROPERTIES BUILD_RPATH "${_ortdir}")
    endif()
endif()
