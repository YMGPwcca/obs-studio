add_executable(obs-engine)

target_sources(
  obs-engine
  PRIVATE
    config.cpp
    config.hpp
    events.cpp
    events.hpp
    host.cpp
    obs_source_update_private.hpp
    properties.cpp
    properties.hpp
    properties_sensitive.cpp
    protocol.cpp
    protocol.hpp
    protocol_filter_v2.cpp
    protocol_phase2_v2.cpp
    protocol_phase2_v2.hpp
    protocol_v2.cpp
    protocol_v2.hpp
    revision.hpp
    runtime.cpp
    runtime_filter_v2.cpp
    runtime.hpp
    runtime_interaction_v2.cpp
    runtime_media_v2.cpp
    runtime_properties_v2.cpp
    runtime_phase2_common.cpp
    runtime_phase2_common.hpp
    runtime_item_v2.cpp
    runtime_canvas_v2.cpp
    runtime_scene_v2.cpp
    runtime_source_settle_v2.cpp
    runtime_source_v2.cpp
    runtime_v2.cpp
    source_event_capture.hpp
    validation.hpp
)

# Keep the accepted Task-10 protocol core byte-for-byte unchanged. The
# Task-11 router owns the public entry point while this translation unit keeps
# the existing implementation under a private link symbol.
set_source_files_properties(protocol_v2.cpp PROPERTIES COMPILE_DEFINITIONS "handle_v2_request=handle_v2_request_core")

target_compile_features(obs-engine PRIVATE cxx_std_20)
target_compile_definitions(obs-engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(obs-engine PRIVATE OBS::libobs)

# Task 5 queue-policy verification. EXCLUDE_FROM_ALL keeps this helper out of
# normal builds/install packages; CI builds it explicitly when testing the
# event dispatcher in isolation.
add_executable(obs-engine-events-test EXCLUDE_FROM_ALL events_test.cpp events.cpp events.hpp protocol.hpp)
target_compile_features(obs-engine-events-test PRIVATE cxx_std_20)
target_compile_definitions(obs-engine-events-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(obs-engine-events-test PRIVATE OBS::libobs)
set_target_properties(obs-engine-events-test PROPERTIES FOLDER "engine/tests")

# Task 7 properties-schema verification. This exercises the generic libobs
# property bridge without shipping a test executable in the runtime package.
add_executable(
  obs-engine-properties-test
  EXCLUDE_FROM_ALL
  properties_test.cpp
  properties.cpp
  properties.hpp
  properties_sensitive.cpp
  protocol.hpp
)
target_compile_features(obs-engine-properties-test PRIVATE cxx_std_20)
target_compile_definitions(obs-engine-properties-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(obs-engine-properties-test PRIVATE OBS::libobs)
set_target_properties(obs-engine-properties-test PROPERTIES FOLDER "engine/tests")

# Task 8 deterministic concurrency source. This module exists only for the
# dedicated A-F CI regression job. EXCLUDE_FROM_ALL keeps it out of normal
# builds and, because it has no install rule, out of runtime packages.
add_library(obs-engine-task8-concurrency-plugin MODULE EXCLUDE_FROM_ALL task8_concurrency_source.cpp)
target_compile_features(obs-engine-task8-concurrency-plugin PRIVATE cxx_std_20)
target_compile_definitions(
  obs-engine-task8-concurrency-plugin
  PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(obs-engine-task8-concurrency-plugin PRIVATE OBS::libobs)
set_target_properties(
  obs-engine-task8-concurrency-plugin
  PROPERTIES FOLDER "engine/tests" PREFIX "" OUTPUT_NAME "task8-concurrency-source"
)

# Task 9 deterministic interaction source. It records every libobs interaction
# callback to the engine log so CI can validate the neutral protocol mapping.
# It is explicitly excluded from normal builds and has no install rule.
add_library(obs-engine-task9-interaction-plugin MODULE EXCLUDE_FROM_ALL task9_interaction_source.cpp)
target_compile_features(obs-engine-task9-interaction-plugin PRIVATE cxx_std_20)
target_compile_definitions(
  obs-engine-task9-interaction-plugin
  PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(obs-engine-task9-interaction-plugin PRIVATE OBS::libobs)
set_target_properties(
  obs-engine-task9-interaction-plugin
  PROPERTIES FOLDER "engine/tests" PREFIX "" OUTPUT_NAME "task9-interaction-source"
)

# Task 10 deterministic controllable-media source. It is built and staged only
# by the Task-10 integration workflow and has no install rule.
add_library(obs-engine-task10-media-plugin MODULE EXCLUDE_FROM_ALL task10_media_source.cpp)
target_compile_features(obs-engine-task10-media-plugin PRIVATE cxx_std_20)
target_compile_definitions(
  obs-engine-task10-media-plugin
  PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(obs-engine-task10-media-plugin PRIVATE OBS::libobs)
set_target_properties(
  obs-engine-task10-media-plugin
  PROPERTIES FOLDER "engine/tests" PREFIX "" OUTPUT_NAME "task10-media-source"
)

# Task 11 deterministic parent/filter source. It is built and staged only by
# the Task-11 integration workflow and has no install rule.
add_library(obs-engine-task11-filter-plugin MODULE EXCLUDE_FROM_ALL task11_filter_source.cpp)
target_compile_features(obs-engine-task11-filter-plugin PRIVATE cxx_std_20)
target_compile_definitions(
  obs-engine-task11-filter-plugin
  PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
)
target_link_libraries(obs-engine-task11-filter-plugin PRIVATE OBS::libobs)
set_target_properties(
  obs-engine-task11-filter-plugin
  PROPERTIES FOLDER "engine/tests" PREFIX "" OUTPUT_NAME "task11-filter-source"
)

# Restrict implicit dependent-DLL resolution before main() runs. 0xA00 is
# LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32. Runtime
# plugin loads are separately constrained by libobs plus the host's safe-module
# allowlist and SetDefaultDllDirectories() hardening.
target_link_options(obs-engine PRIVATE "/DEPENDENTLOADFLAG:0xA00")

# Mirror the current OBS frontend pattern: pass the staged graphics backend
# filename into the host at compile time rather than hard-coding a DLL name.
if(TARGET OBS::libobs-d3d11)
  target_compile_definitions(
    obs-engine
    PRIVATE DL_D3D11="$<$<IF:$<PLATFORM_ID:Windows>,TARGET_FILE_NAME,TARGET_SONAME_FILE_NAME>:OBS::libobs-d3d11>"
  )
else()
  message(FATAL_ERROR "obs-engine requires the libobs D3D11 graphics backend")
endif()

set_target_properties_obs(obs-engine PROPERTIES FOLDER "engine")

# obs-studio normally owns Windows third-party dependency bundling. This branch
# intentionally omits that frontend, so make the headless host the bundle root
# instead. Reusing OBS's dependency walker keeps FFmpeg, zlib, wxi, and plugin
# runtime dependencies synchronized with the targets actually linked by this
# build instead of maintaining a manual DLL allowlist here.
_bundle_dependencies(obs-engine)

# Building the host target directly must also stage the D3D11 backend and the
# default Windows capture module into the normal OBS runtime layout.
if(TARGET libobs-d3d11)
  add_dependencies(obs-engine libobs-d3d11)
endif()
if(TARGET win-capture)
  add_dependencies(obs-engine win-capture)
endif()
