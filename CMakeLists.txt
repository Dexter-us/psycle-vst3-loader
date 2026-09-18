cmake_minimum_required(VERSION 3.20)

project(PsycleVst3Loader VERSION 0.1.0 LANGUAGES CXX)

option(PSYCLE_FETCH_VST3_SDK "Download the Steinberg VST3 SDK when VST3_SDK_DIR is not set" ON)
set(VST3_SDK_DIR "" CACHE PATH "Path to a checkout of steinbergmedia/vst3sdk")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT VST3_SDK_DIR)
    if(NOT PSYCLE_FETCH_VST3_SDK)
        message(FATAL_ERROR "Set VST3_SDK_DIR or enable PSYCLE_FETCH_VST3_SDK")
    endif()

    include(FetchContent)
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.1_build_84
        GIT_SHALLOW TRUE
    )
    FetchContent_Populate(vst3sdk)
    set(VST3_SDK_DIR "${vst3sdk_SOURCE_DIR}")
endif()

if(EXISTS "${VST3_SDK_DIR}/CMakeLists.txt")
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
    add_subdirectory("${VST3_SDK_DIR}" "${CMAKE_BINARY_DIR}/vst3sdk" EXCLUDE_FROM_ALL)
else()
    message(FATAL_ERROR "VST3_SDK_DIR does not contain the VST3 SDK CMakeLists.txt")
endif()

smtg_enable_vst3_sdk()

add_subdirectory(src)