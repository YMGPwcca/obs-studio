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

add_subdirectory(libobs)
add_subdirectory(libobs-d3d11)
add_subdirectory(libobs-winrt)
add_subdirectory(plugins)

message_configuration()
