cmake_minimum_required(VERSION 3.16)

# The version lives in one header; read it rather than repeat it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/sg/Version.hpp" _sg_version_line
     REGEX "^#define SG_VERSION_STRING ")
string(REGEX REPLACE "^#define SG_VERSION_STRING \"([^\"]+)\".*" "\\1" _sg_version
       "${_sg_version_line}")
project(stategine VERSION ${_sg_version} LANGUAGES CXX)

# Built on its own, stategine builds its examples and tests. Pulled into a
# project (FetchContent, add_subdirectory), it is only a library: no demos, no
# GLFW download, no flags forced onto the project's own code.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(SG_TOP_LEVEL ON)
else()
  set(SG_TOP_LEVEL OFF)
endif()
option(SG_BUILD_EXAMPLES "Build the examples and demos" ${SG_TOP_LEVEL})
option(SG_BUILD_TESTS "Build the tests, law checks and compile-fail cases" ${SG_TOP_LEVEL})
option(SG_BUILD_GL "Build the OpenGL room example (downloads GLFW)" ${SG_TOP_LEVEL})

# --- the library ---------------------------------------------------------------
# Header-only: an include path and a language level, nothing else.
add_library(stategine INTERFACE)
add_library(stategine::stategine ALIAS stategine)
target_include_directories(stategine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(stategine INTERFACE cxx_std_17)

# Opt-in extras, for projects that want what the engine's own targets use.
#   stategine::warnings        -Wall -Wextra, or /W4
#   stategine::static_runtime  MinGW: link the runtime in, so binaries run
#                              outside the toolchain shell (no-op elsewhere)
add_library(stategine_warnings INTERFACE)
add_library(stategine::warnings ALIAS stategine_warnings)
target_compile_options(stategine_warnings INTERFACE
                       $<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall -Wextra>)

add_library(stategine_static_runtime INTERFACE)
add_library(stategine::static_runtime ALIAS stategine_static_runtime)
if(MINGW)
  target_link_options(stategine_static_runtime INTERFACE -static-libgcc -static-libstdc++
                      -static -lpthread)
endif()

# Everything this repository builds for itself uses both.
add_library(stategine_dev INTERFACE)
target_link_libraries(stategine_dev INTERFACE stategine stategine_warnings
                      stategine_static_runtime)

if(SG_BUILD_EXAMPLES)
  add_executable(sg_demo examples/demo.cpp)
  target_link_libraries(sg_demo PRIVATE stategine_dev)

  add_executable(sg_room examples/room.cpp)
  target_link_libraries(sg_room PRIVATE stategine_dev)
endif()

# --- the OpenGL example (GLFW fetched on demand) -----------------------------
if(SG_BUILD_EXAMPLES AND SG_BUILD_GL)
  include(FetchContent)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(glfw)

  add_executable(sg_room3d examples/room3d.cpp)
  target_link_libraries(sg_room3d PRIVATE stategine_dev glfw)
  if(WIN32)
    target_link_libraries(sg_room3d PRIVATE opengl32)
  endif()

  # Looks against a real GL context: broken shaders are named, fall back to
  # the built-ins, and anything prepare() did not see is counted as late.
  # Needs a display, so it only exists alongside the GL example.
  if(SG_BUILD_TESTS)
    add_executable(sg_looks_gl tests/looks_gl.cpp)
    target_link_libraries(sg_looks_gl PRIVATE stategine_dev glfw)
    if(WIN32)
      target_link_libraries(sg_looks_gl PRIVATE opengl32)
    endif()
    add_test(NAME sg_looks_gl COMMAND sg_looks_gl)
  endif()
endif()

if(SG_BUILD_TESTS)
  enable_testing()
  add_executable(sg_tests tests/test_engine.cpp)
  target_link_libraries(sg_tests PRIVATE stategine_dev)
  add_test(NAME sg_tests COMMAND sg_tests)

  # The core, built with no domain and no renderer in sight. This target exists
  # to fail: if anything in core/ grows a dependency on rooms or on pixels, the
  # build breaks here rather than the claim quietly becoming untrue.
  add_executable(sg_core_only tests/core_only.cpp)
  target_link_libraries(sg_core_only PRIVATE stategine_dev)
  add_test(NAME sg_core_only COMMAND sg_core_only)

  # The laws on live data, each one shown both holding and breaking.
  add_executable(sg_laws tests/test_laws.cpp)
  target_link_libraries(sg_laws PRIVATE stategine_dev)
  add_test(NAME sg_laws COMMAND sg_laws)

  # --- typed composition: mistakes that must not compile -------------------------
  # Each case builds as a control, so the file itself is known good. With
  # SG_EXPECT_FAIL it gains one ill-typed line, and the test passes only when the
  # compiler refuses it *with Stategine's own message* - failing for any other
  # reason (a typo, a missing header) does not count.
  function(sg_compile_fail name expect)
    add_library(sg_cf_${name}_control OBJECT tests/compile_fail/${name}.cpp)
    target_link_libraries(sg_cf_${name}_control PRIVATE stategine_dev)
    add_library(sg_cf_${name} OBJECT EXCLUDE_FROM_ALL tests/compile_fail/${name}.cpp)
    target_link_libraries(sg_cf_${name} PRIVATE stategine_dev)
    target_compile_definitions(sg_cf_${name} PRIVATE SG_EXPECT_FAIL)
    add_test(NAME compile_fail_${name}
             COMMAND ${CMAKE_COMMAND} --build ${PROJECT_BINARY_DIR} --target sg_cf_${name}
                     --config $<CONFIG>)
    set_tests_properties(compile_fail_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
  endfunction()

  sg_compile_fail(arrow_mismatch "g \\* f needs cod\\(f\\) == dom\\(g\\)")
  sg_compile_fail(functor_mismatch "compose\\(G, F\\) needs cod\\(F\\) == dom\\(G\\)")
  sg_compile_fail(arrow_leaves_state "an arrow inside a state cannot leave it")
  sg_compile_fail(functor_object_map "needs X to be an object of F's source")
  sg_compile_fail(lens_halves "needs out to run back along in")
  sg_compile_fail(not_parallel "only parallel arrows can commute")

  add_executable(sg_bench tests/bench.cpp)
  target_link_libraries(sg_bench PRIVATE stategine_dev)
  target_compile_options(sg_bench PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>)
endif()
