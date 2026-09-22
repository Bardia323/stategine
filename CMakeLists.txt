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

add_executable(sg_bench tests/bench.cpp)
target_link_libraries(sg_bench PRIVATE stategine)
target_compile_options(sg_bench PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>)
