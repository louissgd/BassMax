cmake_minimum_required(VERSION 3.22)
project(BassMax VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- 定位 JUCE ----
# 优先级: 本目录下预置的 JUCE/  ->  -DJUCE_DIR=<path>  ->  FetchContent 自动拉取
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
  message(STATUS "Using bundled JUCE at ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
  add_subdirectory(JUCE)
elseif(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
  message(STATUS "Using JUCE_DIR=${JUCE_DIR}")
  add_subdirectory("${JUCE_DIR}" JUCE)
else()
  message(STATUS "Fetching JUCE 8.0.15 via FetchContent")
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(BassMax
  COMPANY_NAME "CleanRoomAudio"
  BUNDLE_ID "com.cleanroomaudio.bassmax"
  PLUGIN_MANUFACTURER_CODE Crma
  PLUGIN_CODE Bmx1
  FORMATS VST3 Standalone
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  PRODUCT_NAME "BassMax"
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx)

target_sources(BassMax PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp)

target_include_directories(BassMax PRIVATE Source)

target_compile_definitions(BassMax PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

# 源码含中文注释 (UTF-8); 必须让 MSVC 以 UTF-8 解析, 否则 GBK 误读会破坏注释后的代码
if(MSVC)
  target_compile_options(BassMax PRIVATE /utf-8)
endif()

target_link_libraries(BassMax PRIVATE
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_basics
 PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)
