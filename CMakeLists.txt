cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
project(FluTube VERSION 1.0.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── JUCE ─────────────────────────────────────────────────────────────────────
# Use a locally cached copy when available (e.g. already fetched by drum-pack).
# Override by passing -DJUCE_SOURCE_DIR=/path/to/juce to cmake.
if(NOT DEFINED JUCE_SOURCE_DIR)
    set(JUCE_SOURCE_DIR "$ENV{HOME}/drum-pack/build/_deps/juce-src" CACHE PATH
        "Path to JUCE source. Defaults to drum-pack's fetched copy.")
endif()

if(EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "FluTube: using local JUCE at ${JUCE_SOURCE_DIR}")
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce-build" EXCLUDE_FROM_ALL)
else()
    message(STATUS "FluTube: local JUCE not found, fetching from GitHub...")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ── RubberBand (optional — high-quality pitch shift + time stretch) ───────────
# Priority:
#  1. System install via Homebrew:  brew install rubberband
#  2. FetchContent from GitHub (requires network access)
#  3. Fall back to simple linear-resampling pitch shift (changes speed too)
#
option(FLUTUBE_USE_RUBBERBAND "Enable RubberBand pitch shifting" ON)

set(FLUTUBE_HAS_RUBBERBAND OFF)

if(FLUTUBE_USE_RUBBERBAND)
    # 1. Try Homebrew / system install
    find_library(RB_LIB rubberband
        PATHS /opt/homebrew/lib /usr/local/lib
        NO_DEFAULT_PATH)
    find_path(RB_INCLUDE rubberband/RubberBandStretcher.h
        PATHS /opt/homebrew/include /usr/local/include
        NO_DEFAULT_PATH)

    if(RB_LIB AND RB_INCLUDE)
        message(STATUS "FluTube: found system RubberBand at ${RB_LIB}")
        add_library(rubberband_system INTERFACE)
        target_link_libraries(rubberband_system INTERFACE "${RB_LIB}")
        target_include_directories(rubberband_system INTERFACE "${RB_INCLUDE}")
        set(RUBBERBAND_TARGET rubberband_system)
        set(FLUTUBE_HAS_RUBBERBAND ON)

    else()
        # 2. Try local FetchContent path (user may have cloned rubberband manually)
        set(RB_LOCAL_DIR "$ENV{HOME}/rubberband" CACHE PATH "Local RubberBand source")
        if(EXISTS "${RB_LOCAL_DIR}/single/RubberBandSingle.cpp")
            message(STATUS "FluTube: found local RubberBand at ${RB_LOCAL_DIR}")
            add_library(rubberband_single STATIC
                "${RB_LOCAL_DIR}/single/RubberBandSingle.cpp")
            target_include_directories(rubberband_single PUBLIC "${RB_LOCAL_DIR}")
            target_compile_definitions(rubberband_single PUBLIC HAVE_VDSP=1 USE_SPEEX=1)
            if(APPLE)
                target_link_libraries(rubberband_single PUBLIC "-framework Accelerate")
            endif()
            set(RUBBERBAND_TARGET rubberband_single)
            set(FLUTUBE_HAS_RUBBERBAND ON)
        else()
            message(STATUS "FluTube: RubberBand not found — falling back to linear-resampling pitch shift.")
            message(STATUS "  To enable: brew install rubberband  (then re-run cmake)")
        endif()
    endif()
endif()

# ── Plugin target ──────────────────────────────────────────────────────────────
if(APPLE)
    set(FLUTUBE_FORMATS AU VST3)
    set(FLUTUBE_COPY TRUE)
else()
    set(FLUTUBE_FORMATS VST3)
    set(FLUTUBE_COPY FALSE)
endif()

juce_add_plugin(FluTube
    COMPANY_NAME                "Volta Labs"
    PLUGIN_MANUFACTURER_CODE    VLab
    PLUGIN_CODE                 FTu1
    FORMATS                     ${FLUTUBE_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT             TRUE
    NEEDS_MIDI_OUTPUT            FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS  TRUE
    COPY_PLUGIN_AFTER_BUILD      ${FLUTUBE_COPY}
    PRODUCT_NAME                "FluTube"
    BUNDLE_ID                    "com.voltalabs.flutube"
    VST3_CATEGORIES              "Instrument|Sampler"
    AU_MAIN_TYPE                 "kAudioUnitType_MusicDevice"
)

juce_generate_juce_header(FluTube)

# Copy Resources/ into each built bundle (all platforms).
# juce_add_bundle_resources_directory only works for Xcode; use POST_BUILD for Makefile/Ninja.
foreach(_fmt ${FLUTUBE_FORMATS})
    add_custom_command(TARGET FluTube_${_fmt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/Resources"
            "$<TARGET_FILE_DIR:FluTube_${_fmt}>/../Resources"
        COMMENT "Copying Resources into FluTube_${_fmt}"
        VERBATIM
    )
endforeach()

# Mac-only: also copy into the installed location and chmod yt-dlp.
# Note: JUCE's COPY_PLUGIN_AFTER_BUILD runs before POST_BUILD fires.
if(APPLE)
    foreach(_fmt AU VST3)
        if(_fmt STREQUAL "AU")
            set(_install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/Components/FluTube.component")
        else()
            set(_install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/FluTube.vst3")
        endif()
        add_custom_command(TARGET FluTube_${_fmt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Resources"
                "${_install_dir}/Contents/Resources"
            COMMAND chmod +x
                "${_install_dir}/Contents/Resources/yt-dlp"
            COMMENT "Installing Resources into ${_install_dir}"
            VERBATIM
        )
    endforeach()
endif()

set_target_properties(FluTube PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/FluTube.entitlements"
)

target_sources(FluTube PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/YouTubeLoader.cpp
    Source/SampleEngine.cpp
    Source/WaveformDisplay.cpp
    Source/LoFiChain.cpp
    Source/TapeDelay.cpp
    Source/VinylNoise.cpp
    Source/Hum.cpp
    Source/Bitcrush.cpp
    Source/HazyEffect.cpp
    Source/WavyEffect.cpp
)

target_include_directories(FluTube PRIVATE Source)

# Pass RubberBand availability to the compiler
if(FLUTUBE_HAS_RUBBERBAND)
    target_compile_definitions(FluTube PRIVATE FLUTUBE_HAS_RUBBERBAND=1)
    message(STATUS "FluTube: RubberBand pitch shift enabled")
else()
    message(STATUS "FluTube: using fallback linear-resampling pitch shift (install rubberband for better quality)")
endif()

target_compile_definitions(FluTube PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    _USE_MATH_DEFINES
)

set(FLUTUBE_LINK_LIBS
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
)

if(FLUTUBE_HAS_RUBBERBAND)
    list(APPEND FLUTUBE_LINK_LIBS ${RUBBERBAND_TARGET})
endif()

target_link_libraries(FluTube
    PRIVATE ${FLUTUBE_LINK_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)
