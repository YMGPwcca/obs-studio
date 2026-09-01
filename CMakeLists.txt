cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-studio VERSION ${OBS_VERSION_CANONICAL})

if(NOT CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
  message(FATAL_ERROR "The windows-minimal branch only supports Windows hosts.")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/architecture.cmake")
if(NOT OBS_PARENT_ARCHITECTURE STREQUAL CMAKE_VS_PLATFORM_NAME)
  return()
endif()

include(compilerconfig)
include(defaults)
include(helpers)

# This branch is intentionally a headless libobs runtime. The proprietary
# controller lives out-of-process and communicates with a separate host.
option(ENABLE_FRONTEND "Enable the OBS Studio Qt frontend" OFF)
option(ENABLE_SCRIPTING "Enable scripting support" OFF)
option(ENABLE_HEVC "Enable HEVC encoders" ON)
option(ENABLE_PLUGINS "Enable OBS runtime plugins" ON)
option(ENABLE_BROWSER_SOURCE "Build obs-browser (large CEF dependency)" OFF)
option(ENABLE_WEBSOCKET "Build obs-websocket" OFF)
option(ENABLE_DECKLINK_CAPTIONS "Build DeckLink captions OBS frontend UI plugin" OFF)
option(ENABLE_PHASE2_TEST_HOOKS "Enable non-shipping Phase-2 deterministic test hooks" OFF)

add_subdirectory(libobs)
add_subdirectory(libobs-d3d11)
add_subdirectory(libobs-winrt)

# Keep the stock frontend API ABI out of the default headless runtime. Only
# opt-in plugins that explicitly depend on OBS frontend semantics get it.
if(ENABLE_PLUGINS AND (ENABLE_DECKLINK_CAPTIONS OR ENABLE_WEBSOCKET OR ENABLE_BROWSER_SOURCE))
  add_subdirectory(frontend/api)
endif()

add_subdirectory(plugins)
add_subdirectory(engine)

message_configuration()
