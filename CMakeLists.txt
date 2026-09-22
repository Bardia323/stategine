cmake_minimum_required(VERSION 3.16)
project(stategine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Header-only core.
add_library(stategine INTERFACE)
target_include_directories(stategine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(stategine INTERFACE /W4)
else()
  target_compile_options(stategine INTERFACE -Wall -Wextra)
  # MinGW: link the runtime in, so the binaries run outside the toolchain shell.
  if(WIN32)
    target_link_options(stategine INTERFACE -static-libgcc -static-libstdc++
                        -static -lpthread)
  endif()
endif()

add_executable(sg_demo examples/demo.cpp)
target_link_libraries(sg_demo PRIVATE stategine)

add_executable(sg_room examples/room.cpp)
target_link_libraries(sg_room PRIVATE stategine)

# --- the OpenGL example (GLFW fetched on demand) -----------------------------
option(SG_BUILD_GL "Build the OpenGL room example (downloads GLFW)" ON)

if(SG_BUILD_GL)
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
  target_link_libraries(sg_room3d PRIVATE stategine glfw)
  if(WIN32)
    target_link_libraries(sg_room3d PRIVATE opengl32)
  endif()
endif()

enable_testing()
add_executable(sg_tests tests/test_engine.cpp)
target_link_libraries(sg_tests PRIVATE stategine)
add_test(NAME sg_tests COMMAND sg_tests)

# The core, built with no domain and no renderer in sight. This target exists
# to fail: if anything in core/ grows a dependency on rooms or on pixels, the
# build breaks here rather than the claim quietly becoming untrue.
add_executable(sg_core_only tests/core_only.cpp)
target_link_libraries(sg_core_only PRIVATE stategine)
add_test(NAME sg_core_only COMMAND sg_core_only)

# The laws on live data, each one shown both holding and breaking.
add_executable(sg_laws tests/test_laws.cpp)
target_link_libraries(sg_laws PRIVATE stategine)
add_test(NAME sg_laws COMMAND sg_laws)

# --- typed composition: mistakes that must not compile -------------------------
# Each case builds as a control, so the file itself is known good. With
# SG_EXPECT_FAIL it gains one ill-typed line, and the test passes only when the
# compiler refuses it *with Stategine's own message* - failing for any other
# reason (a typo, a missing header) does not count.
function(sg_compile_fail name expect)
  add_library(sg_cf_${name}_control OBJECT tests/compile_fail/${name}.cpp)
  target_link_libraries(sg_cf_${name}_control PRIVATE stategine)
  add_library(sg_cf_${name} OBJECT EXCLUDE_FROM_ALL tests/compile_fail/${name}.cpp)
  target_link_libraries(sg_cf_${name} PRIVATE stategine)
  target_compile_definitions(sg_cf_${name} PRIVATE SG_EXPECT_FAIL)
  add_test(NAME compile_fail_${name}
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target sg_cf_${name}
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
target_link_libraries(sg_bench PRIVATE stategine)
target_compile_options(sg_bench PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>)
