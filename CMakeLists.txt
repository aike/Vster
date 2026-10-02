cmake_minimum_required(VERSION 3.22)
project(Vster VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(external/JUCE)

# --- ASIO SDK (user-supplied; Steinberg's license forbids redistribution) ---
# Download from https://www.steinberg.net/developers/ and unzip so that
# ${ASIOSDK_DIR}/common/iasiodrv.h exists. Only the headers are needed.
set(ASIOSDK_DIR "${CMAKE_SOURCE_DIR}/sdk/asiosdk" CACHE PATH "Path to the Steinberg ASIO SDK")
option(VSTER_ENABLE_ASIO "Enable ASIO support (requires the Steinberg ASIO SDK)" ON)

set(VSTER_USE_ASIO 0)
if(VSTER_ENABLE_ASIO)
    if(EXISTS "${ASIOSDK_DIR}/common/iasiodrv.h")
        set(VSTER_USE_ASIO 1)
        message(STATUS "ASIO SDK found at ${ASIOSDK_DIR} — ASIO enabled")
    else()
        message(WARNING
            "ASIO SDK not found (expected ${ASIOSDK_DIR}/common/iasiodrv.h). "
            "Building WITHOUT ASIO (WASAPI only). Download the ASIO SDK from "
            "https://www.steinberg.net/developers/ and unzip it to sdk/asiosdk, "
            "or pass -DASIOSDK_DIR=<path>.")
    endif()
endif()

juce_add_gui_app(Vster
    PRODUCT_NAME "VSTer"
    COMPANY_NAME "keisuke"
    VERSION ${PROJECT_VERSION})

juce_generate_juce_header(Vster)

target_sources(Vster PRIVATE
    Source/Main.cpp
    Source/Engine/AudioEngine.cpp
    Source/Engine/EngineCallback.cpp
    Source/Engine/PluginChain.cpp
    Source/Plugins/PluginHostManager.cpp
    Source/Plugins/PluginWindowManager.cpp
    Source/Session/SessionManager.cpp
    Source/UI/MainComponent.cpp
    Source/UI/TransportBar.cpp
    Source/UI/SlotComponent.cpp
    Source/UI/LevelMeterComponent.cpp)

target_compile_definitions(Vster PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_ASIO=${VSTER_USE_ASIO}
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Vster,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Vster,JUCE_VERSION>")

if(VSTER_USE_ASIO)
    target_include_directories(Vster PRIVATE "${ASIOSDK_DIR}/common")
endif()

if(MSVC)
    target_compile_options(Vster PRIVATE /utf-8)
endif()

target_link_libraries(Vster PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
