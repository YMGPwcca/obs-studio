cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_PLUGINS "Enable OBS runtime plugins" ON)
option(ENABLE_BROWSER_SOURCE "Build obs-browser (large CEF dependency)" OFF)
option(ENABLE_WEBSOCKET "Build obs-websocket" OFF)

if(NOT ENABLE_PLUGINS)
  set_property(GLOBAL APPEND PROPERTY OBS_FEATURES_DISABLED "Plugin Support")
  return()
endif()

set_property(GLOBAL APPEND PROPERTY OBS_FEATURES_ENABLED "Plugin Support")

# windows-minimal intentionally builds only modules that are useful on Windows.
# Platform-specific Linux/macOS/BSD modules and Qt frontend helpers are skipped.
# Source directories remain in the fork to keep rebasing from upstream simple.

add_obs_plugin(aja PLATFORMS WINDOWS WITH_MESSAGE)
add_obs_plugin(coreaudio-encoder PLATFORMS WINDOWS)
add_obs_plugin(decklink PLATFORMS WINDOWS WITH_MESSAGE)
add_obs_plugin(decklink-captions PLATFORMS WINDOWS WITH_MESSAGE)
add_obs_plugin(image-source PLATFORMS WINDOWS)
add_obs_plugin(nv-filters PLATFORMS WINDOWS)

if(ENABLE_BROWSER_SOURCE)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/obs-browser/CMakeLists.txt")
    message(FATAL_ERROR "Required submodule 'obs-browser' not available.")
  endif()
  add_subdirectory(obs-browser)
endif()

add_obs_plugin(obs-ffmpeg PLATFORMS WINDOWS)
add_obs_plugin(obs-filters PLATFORMS WINDOWS)
add_obs_plugin(obs-libfdk PLATFORMS WINDOWS)
add_obs_plugin(obs-nvenc PLATFORMS WINDOWS ARCHITECTURES x64 x86_64)
add_obs_plugin(obs-outputs PLATFORMS WINDOWS)
add_obs_plugin(obs-qsv11 PLATFORMS WINDOWS ARCHITECTURES x64 x86_64)
add_obs_plugin(obs-text PLATFORMS WINDOWS)
add_obs_plugin(obs-transitions PLATFORMS WINDOWS)
add_obs_plugin(obs-vst PLATFORMS WINDOWS WITH_MESSAGE)
add_obs_plugin(obs-webrtc PLATFORMS WINDOWS)

if(ENABLE_WEBSOCKET)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/obs-websocket/CMakeLists.txt")
    message(FATAL_ERROR "Required submodule 'obs-websocket' not available.")
  endif()
  add_subdirectory(obs-websocket)
endif()

add_obs_plugin(obs-x264 PLATFORMS WINDOWS)
add_obs_plugin(rtmp-services PLATFORMS WINDOWS)
add_obs_plugin(text-freetype2 PLATFORMS WINDOWS)
add_obs_plugin(vlc-video PLATFORMS WINDOWS WITH_MESSAGE)
add_obs_plugin(win-capture PLATFORMS WINDOWS)
add_obs_plugin(win-dshow PLATFORMS WINDOWS)
add_obs_plugin(win-wasapi PLATFORMS WINDOWS)
