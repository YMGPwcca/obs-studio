cmake_minimum_required(VERSION 3.28...3.30)

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
if(ENABLE_DECKLINK_CAPTIONS)
  add_obs_plugin(decklink-captions PLATFORMS WINDOWS WITH_MESSAGE)
else()
  target_disable(decklink-captions)
endif()
add_obs_plugin(image-source PLATFORMS WINDOWS)
add_obs_plugin(nv-filters PLATFORMS WINDOWS)

# obs-browser consumes the obs-websocket API target even when the websocket
# plugin itself is disabled, so configure the submodule whenever either feature
# is requested. Its CMake defines the API target before honoring ENABLE_WEBSOCKET.
if(ENABLE_WEBSOCKET OR ENABLE_BROWSER_SOURCE)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/obs-websocket/CMakeLists.txt")
    message(FATAL_ERROR "Required submodule 'obs-websocket' not available.")
  endif()
  add_subdirectory(obs-websocket)
endif()

if(ENABLE_BROWSER_SOURCE)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/obs-browser/CMakeLists.txt")
    message(FATAL_ERROR "Required submodule 'obs-browser' not available.")
  endif()
  # The obs-browser submodule uses its upstream ENABLE_BROWSER switch. The
  # outer option controls whether this headless branch includes that submodule.
  set(ENABLE_BROWSER ON)
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

add_obs_plugin(obs-x264 PLATFORMS WINDOWS)
add_obs_plugin(rtmp-services PLATFORMS WINDOWS)
add_obs_plugin(text-freetype2 PLATFORMS WINDOWS)
add_obs_plugin(vlc-video PLATFORMS WINDOWS WITH_MESSAGE)
add_obs_plugin(win-capture PLATFORMS WINDOWS)
add_obs_plugin(win-dshow PLATFORMS WINDOWS)
add_obs_plugin(win-wasapi PLATFORMS WINDOWS)
