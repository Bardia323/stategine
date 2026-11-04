# The renderer in a browser (sg/gl on WebGL 2, src/gl/webgl): the GL calls
# the renderer makes answered by WebGL 2, its shaders made GLSL ES as they
# are given (glslang, SPIRV-Cross: cmake/shader_tools.cmake). On by itself in
# a browser's build; on the desktop, to try the browser's shaders on the
# desktop's GL (which takes GLSL ES too) before a browser sees them.
option(SG_WEBGL "Answer sg/gl's GL calls as WebGL 2 does (on in a browser's build)" ${EMSCRIPTEN})
if(NOT SG_WEBGL OR NOT TARGET stategine_gl)
  return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/shader_tools.cmake)
target_sources(stategine_gl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/gl/webgl/WebGL.cpp)
target_include_directories(stategine_gl PRIVATE ${sg_glslang_SOURCE_DIR})
target_include_directories(stategine_gl SYSTEM PRIVATE ${sg_spirv_cross_SOURCE_DIR})
target_link_libraries(stategine_gl PRIVATE glslang glslang-default-resource-limits SPIRV spirv-cross-glsl spirv-cross-core)
# (Said to the shaders' text too: a skin's joints are a picture there, not a buffer.)
target_compile_definitions(stategine_looks PUBLIC SG_WEBGL=1)
if(EMSCRIPTEN)
  # WebGL 2 and nothing older; its entry points found by name (gl::load).
  target_link_options(stategine_gl PUBLIC -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sGL_ENABLE_GET_PROC_ADDRESS=1)
endif()
