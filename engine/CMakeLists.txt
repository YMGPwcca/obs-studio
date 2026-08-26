add_executable(obs-engine)

target_sources(
  obs-engine
  PRIVATE
    config.cpp
    config.hpp
    host.cpp
    protocol.cpp
    protocol.hpp
    runtime.cpp
    runtime.hpp
    validation.hpp
)

target_compile_features(obs-engine PRIVATE cxx_std_20)
target_compile_definitions(obs-engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(obs-engine PRIVATE OBS::libobs)

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

# Building the host target directly must also stage the D3D11 backend and the
# default Windows capture module into the normal OBS runtime layout.
if(TARGET libobs-d3d11)
  add_dependencies(obs-engine libobs-d3d11)
endif()
if(TARGET win-capture)
  add_dependencies(obs-engine win-capture)
endif()
