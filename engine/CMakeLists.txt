add_executable(obs-engine)

target_sources(obs-engine PRIVATE main.cpp)

target_compile_features(obs-engine PRIVATE cxx_std_20)
target_compile_definitions(obs-engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(obs-engine PRIVATE OBS::libobs)

define_graphic_modules(obs-engine)
set_target_properties_obs(obs-engine PROPERTIES FOLDER "engine")

# Building the engine target directly should also produce the D3D11 backend
# and the capture module used by the default safe-module allowlist.
if(TARGET libobs-d3d11)
  add_dependencies(obs-engine libobs-d3d11)
endif()
if(TARGET win-capture)
  add_dependencies(obs-engine win-capture)
endif()
