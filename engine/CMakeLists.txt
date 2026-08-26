add_executable(obs-engine)

target_sources(obs-engine PRIVATE host.cpp)

target_compile_features(obs-engine PRIVATE cxx_std_20)
target_compile_definitions(obs-engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(obs-engine PRIVATE OBS::libobs)

define_graphic_modules(obs-engine)
set_target_properties_obs(obs-engine PROPERTIES FOLDER "engine")

# Building the host target directly must also stage the D3D11 backend and the
# default Windows capture module into the normal OBS runtime layout.
if(TARGET libobs-d3d11)
  add_dependencies(obs-engine libobs-d3d11)
endif()
if(TARGET win-capture)
  add_dependencies(obs-engine win-capture)
endif()
