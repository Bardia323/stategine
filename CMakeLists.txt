cmake_minimum_required(VERSION 3.16)

# The version lives in one header; read it rather than repeat it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/sg/Version.hpp" _sg_version_line
     REGEX "^#define SG_VERSION_STRING ")
string(REGEX REPLACE "^#define SG_VERSION_STRING \"([^\"]+)\".*" "\\1" _sg_version
       "${_sg_version_line}")
project(stategine VERSION ${_sg_version} LANGUAGES CXX)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib")

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
option(SG_BUILD_NET_BENCH "Build the standalone networking benchmark" OFF)
if(EMSCRIPTEN)
  # Driver probing is native only; validation and rollback retain exceptions.
  set(SG_BUILD_GL OFF)
  add_compile_options(-fexceptions)
  add_link_options(-fexceptions -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
endif()

# --- the library ---------------------------------------------------------------
# A header says what a thing is; its .cpp, what it does. Each .cpp is
# compiled once, on its own and all at once, and the linker brings them
# together: a changed body recompiles its own file, not every file that
# includes its header.
file(GLOB _sg_sources CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/algebra/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/domains/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/physics/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/src/spatial/*.cpp)
add_library(stategine STATIC ${_sg_sources})
add_library(stategine::stategine ALIAS stategine)
target_include_directories(stategine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(stategine PUBLIC cxx_std_17)

# Derived networking machinery above core. The network is an ordinary State;
# no transport or reconciliation capability is added to the engine.
add_library(stategine_net STATIC src/net/Cellular.cpp src/net/Reconcile.cpp src/net/CpuBackend.cpp
    src/net/Partition.cpp src/net/Exchange.cpp src/net/Distributed.cpp src/net/Placement.cpp src/net/Transport.cpp src/net/IceSession.cpp
    src/net/Canonical.cpp src/net/Integrity.cpp src/net/Epoch.cpp src/net/Verify.cpp src/net/Agreement.cpp src/net/Protocol.cpp src/net/Prediction.cpp)
add_library(stategine::net ALIAS stategine_net)
target_link_libraries(stategine_net PUBLIC stategine)
include(cmake/network_integrity.cmake)
include(cmake/network_gpu.cmake)

# Drawing (sg/gl, sg/render): a library of its own on top, for what shows a
# world. It needs no window: GL is loaded through the window's own
# glfwGetProcAddress, and sg/gl/Window.hpp - GLFW's - stays a header.
file(GLOB _sg_render_sources CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/src/render/*.cpp)
add_library(stategine_render STATIC ${_sg_render_sources})
add_library(stategine::render ALIAS stategine_render)
target_link_libraries(stategine_render PUBLIC stategine)
if(NOT EMSCRIPTEN)
  file(GLOB _sg_gl_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/gl/*.cpp)
  add_library(stategine_gl STATIC ${_sg_gl_sources})
  add_library(stategine::gl ALIAS stategine_gl)
  target_link_libraries(stategine_gl PUBLIC stategine_render)
endif()

# A game's modules, built the same way: stategine_module(), and
# stategine_check_modules() to hold them to it.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/StategineModules.cmake)

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
# The GPU backends of the algebra (include/sg/gpu): each built in where its
# toolchain is found, and nothing otherwise - every backend then runs on the
# CPU, and says it is not here.
add_library(stategine_gpu INTERFACE)
add_library(stategine::gpu ALIAS stategine_gpu)
include(cmake/gpu.cmake OPTIONAL)

add_library(stategine_dev INTERFACE)
target_link_libraries(stategine_dev INTERFACE stategine stategine_render stategine_warnings
                      stategine_static_runtime)
if(TARGET stategine_gl)
  target_link_libraries(stategine_dev INTERFACE stategine_gl)
endif()

# The DSL (sg/dsl): a notation for the construction the engine already has, and
# its compiler, sgc. The library reads a source, holds it to the ontology and
# lowers it to a Plan of the engine's own calls; sgc writes them as C++, and
# `apply` makes them on a live graph (a world rewriting itself by graph.edit).
# There is no runtime here: the one that runs the result is the engine.
option(SG_BUILD_DSL "Build the DSL compiler (sgc) and its library" ON)
if(CMAKE_CROSSCOMPILING)
  set(_sg_build_sgc OFF)
else()
  set(_sg_build_sgc ON)
endif()
option(SG_BUILD_SGC "Build the native host DSL executable" ${_sg_build_sgc})
set(STATEGINE_SGC_EXECUTABLE "" CACHE FILEPATH "Native sgc used when cross compiling")
if(SG_BUILD_DSL)
  file(GLOB _sg_dsl_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/dsl/*.cpp)
  add_library(stategine_dsl STATIC ${_sg_dsl_sources})
  add_library(stategine::dsl ALIAS stategine_dsl)
  target_link_libraries(stategine_dsl PUBLIC stategine)
  if(SG_BUILD_SGC)
    if(CMAKE_CROSSCOMPILING)
      message(FATAL_ERROR "sgc is a host tool: disable SG_BUILD_SGC and supply STATEGINE_SGC_EXECUTABLE")
    endif()
    add_executable(sgc tools/sgc.cpp)
    target_link_libraries(sgc PRIVATE stategine_dsl stategine_warnings stategine_static_runtime)
  elseif(STATEGINE_SGC_EXECUTABLE AND EXISTS "${STATEGINE_SGC_EXECUTABLE}")
    add_executable(sgc IMPORTED GLOBAL)
    set_target_properties(sgc PROPERTIES IMPORTED_LOCATION "${STATEGINE_SGC_EXECUTABLE}")
  elseif(SG_BUILD_TESTS OR SG_BUILD_EXAMPLES)
    message(FATAL_ERROR "Supply an existing native STATEGINE_SGC_EXECUTABLE")
  endif()
  include(cmake/StategineDsl.cmake)
endif()

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

    # Light through a doorway: a lamp in one room lights the floor of the
    # room glued to it, through the opening only.
    if(SG_BUILD_DSL)
    add_executable(sg_doorway_light_gl tests/doorway_light_gl.cpp)
    target_link_libraries(sg_doorway_light_gl PRIVATE stategine_dev glfw)
    target_link_libraries(sg_doorway_light_gl PRIVATE stategine_dsl)
    stategine_compile_dsl(sg_doorway_light_gl NAME doorway_light SOURCES tests/dsl/doorway_light.sg)
    if(WIN32)
      target_link_libraries(sg_doorway_light_gl PRIVATE opengl32)
    endif()
    add_test(NAME sg_doorway_light_gl COMMAND sg_doorway_light_gl)
    endif()

    # Things of one shape drawn in one call: the same picture, far fewer calls.
    add_executable(sg_instancing_gl tests/instancing_gl.cpp)
    target_link_libraries(sg_instancing_gl PRIVATE stategine_dev glfw)
    if(WIN32)
      target_link_libraries(sg_instancing_gl PRIVATE opengl32)
    endif()
    add_test(NAME sg_instancing_gl COMMAND sg_instancing_gl)

    # What is not seen is not drawn: shadow maps hold what their lamp sees, and specks are left out.
    add_executable(sg_culling_gl tests/culling_gl.cpp)
    target_link_libraries(sg_culling_gl PRIVATE stategine_dev glfw)
    if(WIN32)
      target_link_libraries(sg_culling_gl PRIVATE opengl32)
    endif()
    add_test(NAME sg_culling_gl COMMAND sg_culling_gl)
  endif()
endif()

if(SG_BUILD_TESTS)
  enable_testing()
  add_executable(sg_tests tests/test_engine.cpp)
  target_link_libraries(sg_tests PRIVATE stategine_dev)
  if(EMSCRIPTEN)
    target_sources(sg_tests PRIVATE src/gl/Shaders.cpp)
  endif()
  add_test(NAME sg_tests COMMAND sg_tests)

  # The laws, compiled (sg/algebra) - the same reports as the verifier's, on
  # every backend built in (sg/gpu).
  add_executable(sg_algebra tests/test_algebra.cpp)
  target_link_libraries(sg_algebra PRIVATE stategine_dev stategine_gpu)
  add_test(NAME sg_algebra COMMAND sg_algebra)

  # The core, built with no domain and no renderer in sight. This target exists
  # to fail: if anything in core/ grows a dependency on rooms or on pixels, the
  # build breaks here rather than the claim quietly becoming untrue.
  add_executable(sg_core_only tests/core_only.cpp)
  target_link_libraries(sg_core_only PRIVATE stategine_dev)
  add_test(NAME sg_core_only COMMAND sg_core_only)

  # Physics (sg/physics): rigid bodies - resting, stacking, tipping, rolling,
  # a table turned over by its edge, a mug carried, and how long a step takes -
  # and ropes: hanging, lying over edges, calming down, coming to rest.
  add_executable(sg_rigid tests/test_rigid.cpp)
  target_link_libraries(sg_rigid PRIVATE stategine_dev)
  add_test(NAME sg_rigid COMMAND sg_rigid)
  add_executable(sg_rooms tests/test_room.cpp)
  target_link_libraries(sg_rooms PRIVATE stategine_dev)
  add_test(NAME sg_rooms COMMAND sg_rooms)
  add_executable(sg_spatial tests/test_spatial.cpp)
  target_link_libraries(sg_spatial PRIVATE stategine_dev)
  add_test(NAME sg_spatial COMMAND sg_spatial)
  add_executable(sg_fields tests/test_fields.cpp)
  target_link_libraries(sg_fields PRIVATE stategine_dev)
  add_test(NAME sg_fields COMMAND sg_fields)
  add_executable(sg_rope tests/test_rope.cpp)
  target_link_libraries(sg_rope PRIVATE stategine_dev)
  add_test(NAME sg_rope COMMAND sg_rope)

  # The laws on live data, each one shown both holding and breaking.
  add_executable(sg_laws tests/test_laws.cpp)
  target_link_libraries(sg_laws PRIVATE stategine_dev)
  add_test(NAME sg_laws COMMAND sg_laws)

  # Work kept - a portal's carry, the laws' answers, validate's - is the same
  # as work done again: each run both ways and held to one answer.
  add_executable(sg_incremental tests/test_incremental.cpp)
  target_link_libraries(sg_incremental PRIVATE stategine_dev)
  add_test(NAME sg_incremental COMMAND sg_incremental)

  # Who may change what: watching is const (checked by the compiler), the
  # legitimate ways still work - an embedding acting on a state other than
  # its host - and structure is counted while values are not.
  add_executable(sg_authority tests/test_authority.cpp)
  target_link_libraries(sg_authority PRIVATE stategine_dev)
  add_test(NAME sg_authority COMMAND sg_authority)

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

  # The engine's own headers kept to its rules: each part includes only what
  # it builds on, and a header says what a thing is.
  set_property(GLOBAL PROPERTY STATEGINE_MODULES
    "sg/core=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/core:"
    "sg/algebra=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/algebra:sg/core"
    "sg/domains=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/domains:sg/core"
    "sg/spatial=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/spatial:"
    "sg/physics=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/physics:sg/core,sg/spatial"
    "sg/net=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/net:sg/core"
    "sg/gl=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/gl:sg/core,sg/domains,sg/render,sg/spatial"
    "sg/render=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/render:sg/core,sg/domains,sg/spatial"
    "sg/web=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/web:sg/core,sg/domains,sg/render,sg/net,sg/dsl,sg/spatial"
    "sg/gpu=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/gpu:sg/core,sg/algebra"
    "sg/dsl=${CMAKE_CURRENT_SOURCE_DIR}/include/sg/dsl:sg/core,sg/domains")
  # Check implementation includes too: renderer executors may depend on the
  # neutral plan, while no world/numerical/notation module may depend on web.
  set_property(GLOBAL APPEND PROPERTY STATEGINE_MODULES
    "sg/core=${CMAKE_CURRENT_SOURCE_DIR}/src/core:"
    "sg/domains=${CMAKE_CURRENT_SOURCE_DIR}/src/domains:sg/core,sg/spatial"
    "sg/spatial=${CMAKE_CURRENT_SOURCE_DIR}/src/spatial:sg/core"
    "sg/physics=${CMAKE_CURRENT_SOURCE_DIR}/src/physics:sg/core,sg/spatial"
    "sg/net=${CMAKE_CURRENT_SOURCE_DIR}/src/net:sg/core"
    "sg/render=${CMAKE_CURRENT_SOURCE_DIR}/src/render:sg/core,sg/domains,sg/spatial"
    "sg/gl=${CMAKE_CURRENT_SOURCE_DIR}/src/gl:sg/core,sg/domains,sg/render,sg/spatial"
    "sg/dsl=${CMAKE_CURRENT_SOURCE_DIR}/src/dsl:sg/core,sg/domains"
    "sg/web=${CMAKE_CURRENT_SOURCE_DIR}/src/web:sg/core,sg/domains,sg/render,sg/net,sg/dsl,sg/spatial")
  stategine_check_modules(NAME sg_shape)
  set_property(GLOBAL PROPERTY STATEGINE_MODULES "")

  # The DSL: what it says and refuses, the vertical slice compiled by sgc and
  # run by the engine, one construction read three ways, and a world that
  # compiles from inside (its source and its compiler states, its change an edit).
  if(SG_BUILD_DSL)
    add_executable(sg_net tests/test_net.cpp)
    target_link_libraries(sg_net PRIVATE stategine_net stategine_dsl stategine_warnings stategine_static_runtime)
    stategine_compile_dsl(sg_net NAME network SOURCES tests/dsl/network.sg)
    add_test(NAME sg_net COMMAND sg_net)
    add_executable(sg_net_numerics tests/test_net_numerics.cpp)
    target_link_libraries(sg_net_numerics PRIVATE stategine_net stategine_dsl stategine_warnings stategine_static_runtime)
    stategine_compile_dsl(sg_net_numerics NAME network_numerics SOURCES tests/dsl/network_numerics.sg)
    add_test(NAME sg_net_numerics COMMAND sg_net_numerics)

    add_executable(sg_net_distributed tests/test_net_distributed.cpp examples/Distributed.cpp)
    target_link_libraries(sg_net_distributed PRIVATE stategine_net stategine_dsl stategine_warnings stategine_static_runtime)
    stategine_compile_dsl(sg_net_distributed NAME distributed SOURCES examples/distributed.sg)
    stategine_compile_dsl(sg_net_distributed NAME distributed_vectors SOURCES tests/dsl/distributed_vectors.sg)
    add_test(NAME sg_net_distributed COMMAND sg_net_distributed)

    add_executable(sg_net_placement tests/test_net_placement.cpp examples/Distributed.cpp)
    target_link_libraries(sg_net_placement PRIVATE stategine_net stategine_dsl stategine_warnings stategine_static_runtime)
    stategine_compile_dsl(sg_net_placement NAME distributed SOURCES examples/distributed.sg)
    add_test(NAME sg_net_placement COMMAND sg_net_placement)

    add_executable(sg_net_agreement tests/test_net_agreement.cpp)
    target_link_libraries(sg_net_agreement PRIVATE stategine_net stategine_warnings stategine_static_runtime)
    add_test(NAME sg_net_agreement COMMAND sg_net_agreement)

    if(NOT EMSCRIPTEN)
    add_executable(sg_net_transport tests/test_net_transport.cpp examples/Udp.cpp)
    target_link_libraries(sg_net_transport PRIVATE stategine_net stategine_warnings stategine_static_runtime)
    if(WIN32)
      target_link_libraries(sg_net_transport PRIVATE ws2_32)
    endif()
    add_test(NAME sg_net_transport COMMAND sg_net_transport)

    add_executable(sg_net_peer examples/net_peer.cpp examples/Distributed.cpp examples/Udp.cpp)
    target_link_libraries(sg_net_peer PRIVATE stategine_net stategine_dsl stategine_warnings stategine_static_runtime)
    if(WIN32)
      target_link_libraries(sg_net_peer PRIVATE ws2_32)
    endif()
    stategine_compile_dsl(sg_net_peer NAME distributed SOURCES examples/distributed.sg)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
      add_test(NAME sg_net_processes COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/net_processes.py
        $<TARGET_FILE:sg_net_peer> ${CMAKE_CURRENT_BINARY_DIR}/out/network/processes)
      set_tests_properties(sg_net_processes PROPERTIES TIMEOUT 180)
    endif()

    endif() # native socket/process fixtures

    add_executable(sg_projected tests/projected.cpp examples/Fields.cpp)
    target_link_libraries(sg_projected PRIVATE stategine_dev stategine_dsl)
    stategine_compile_dsl(sg_projected NAME fields SOURCES examples/fields.sg)
    add_test(NAME sg_projected COMMAND sg_projected)
    if(TARGET glfw)
      add_executable(sg_projected_gl tests/projected.cpp examples/Fields.cpp)
      target_compile_definitions(sg_projected_gl PRIVATE SG_PROJECTED_GL)
      target_link_libraries(sg_projected_gl PRIVATE stategine_dev stategine_dsl glfw)
      stategine_compile_dsl(sg_projected_gl NAME fields SOURCES examples/fields.sg)
      if(WIN32)
        target_link_libraries(sg_projected_gl PRIVATE opengl32)
      endif()
      add_test(NAME sg_projected_gl COMMAND sg_projected_gl)
    endif()
    add_executable(sg_dsl tests/dsl/test_dsl.cpp)
    target_link_libraries(sg_dsl PRIVATE stategine_dev stategine_dsl)
    stategine_compile_dsl(sg_dsl NAME scenario SOURCES tests/dsl/scenario.sg)
    stategine_compile_dsl(sg_dsl NAME world SOURCES src/dsl/compiler.sg tests/dsl/desk.sg)
    stategine_compile_dsl(sg_dsl NAME kitchen SOURCES tests/dsl/kitchen.sg)
    if(EMSCRIPTEN)
      target_compile_definitions(sg_dsl PRIVATE SG_DSL_DIR="/tests/dsl")
      target_link_options(sg_dsl PRIVATE "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/tests/dsl@/tests/dsl")
    else()
      target_compile_definitions(sg_dsl PRIVATE SG_DSL_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/dsl")
    endif()
    add_test(NAME sg_dsl COMMAND sg_dsl)

    # Two invariants held hard: applying a plan is all or nothing (and so is a
    # world compiling from inside), and facts keep every value the engine holds
    # as data, so declarations that differ in one are not equivalent.
    add_executable(sg_dsl_strong tests/dsl/test_dsl_strong.cpp)
    target_link_libraries(sg_dsl_strong PRIVATE stategine_dev stategine_dsl)
    stategine_compile_dsl(sg_dsl_strong NAME strongworld SOURCES src/dsl/compiler.sg tests/dsl/desk.sg)
    add_test(NAME sg_dsl_strong COMMAND sg_dsl_strong)
  endif()

  add_executable(sg_bench tests/bench.cpp)
  target_link_libraries(sg_bench PRIVATE stategine_dev)
  target_compile_options(sg_bench PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>)

  # How the costs grow with the world, and what each shortcut saves (or not).
  add_executable(sg_bench_scale tests/bench_scale.cpp)
  target_link_libraries(sg_bench_scale PRIVATE stategine_dev)
  target_compile_options(sg_bench_scale PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2>)
endif()

# A standalone DSL world, with the same peer protocol and optional GL view.
if(NOT EMSCRIPTEN AND SG_BUILD_DSL AND (SG_BUILD_EXAMPLES OR SG_BUILD_TESTS))
  add_executable(sg_net_keys examples/net_keys.cpp examples/Signed.cpp)
  target_link_libraries(sg_net_keys PRIVATE stategine_net stategine_warnings stategine_static_runtime)
  add_executable(sg_net_verified_peer examples/net_verified_peer.cpp examples/Signed.cpp examples/Udp.cpp examples/NetIo.cpp)
  target_link_libraries(sg_net_verified_peer PRIVATE stategine_net stategine_dsl stategine_warnings stategine_static_runtime)
  stategine_compile_dsl(sg_net_verified_peer NAME verified SOURCES examples/verified.sg)
  if(WIN32)
    target_link_libraries(sg_net_verified_peer PRIVATE ws2_32)
  endif()
  if(SG_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME sg_net_verified_processes COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/net_verified_processes.py
      $<TARGET_FILE:sg_net_verified_peer> $<TARGET_FILE:sg_net_keys> ${CMAKE_CURRENT_BINARY_DIR}/out/network/verified-processes)
    set_tests_properties(sg_net_verified_processes PROPERTIES TIMEOUT 120)
  endif()
  add_executable(sg_net_pong examples/pong/main.cpp examples/pong/Pong.cpp examples/pong/Motion.cpp examples/pong/Secure.cpp examples/Signed.cpp examples/Udp.cpp examples/NetIo.cpp)
  target_link_libraries(sg_net_pong PRIVATE stategine_net stategine_dsl stategine_warnings stategine_static_runtime)
  stategine_compile_dsl(sg_net_pong NAME pong SOURCES examples/pong/pong.sg)
  if(WIN32)
    target_link_libraries(sg_net_pong PRIVATE ws2_32)
  endif()
  if(TARGET glfw)
    target_sources(sg_net_pong PRIVATE examples/pong/View.cpp)
    target_compile_definitions(sg_net_pong PRIVATE SG_PONG_GL)
    target_link_libraries(sg_net_pong PRIVATE stategine_gl glfw)
    if(WIN32)
      target_link_libraries(sg_net_pong PRIVATE opengl32)
    endif()
  endif()
  if(SG_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME sg_net_pong COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/net_pong.py
      $<TARGET_FILE:sg_net_pong> ${CMAKE_CURRENT_BINARY_DIR}/out/pong/tests $<TARGET_FILE:sg_net_keys>)
    set_tests_properties(sg_net_pong PROPERTIES TIMEOUT 180)
  endif()
endif()

include(cmake/network_ice.cmake)
include(cmake/web.cmake)
if(SG_BUILD_NET_BENCH)
  if(NOT SG_BUILD_DSL)
    message(FATAL_ERROR "sg_net_bench requires the DSL for its declared worlds")
  endif()
  add_executable(sg_net_bench benchmarks/network.cpp)
  target_link_libraries(sg_net_bench PRIVATE stategine_net stategine_dsl stategine_warnings stategine_static_runtime)
endif()

# Native programs stay at the build root; archives and checks have their folders.
if(SG_BUILD_TESTS AND NOT EMSCRIPTEN)
  foreach(_check sg_tests sg_algebra sg_core_only sg_rigid sg_rooms sg_spatial sg_fields
      sg_rope sg_laws sg_incremental sg_authority sg_looks_gl sg_doorway_light_gl
      sg_instancing_gl sg_culling_gl sg_net sg_net_numerics sg_net_distributed sg_net_placement
      sg_net_agreement sg_net_transport sg_projected sg_projected_gl sg_dsl
      sg_dsl_strong sg_net_ice sg_web_rtc_peer sg_view_plan sg_cross_platform
      sg_browser_render stategine_shape)
    if(TARGET ${_check})
      set_target_properties(${_check} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/laws")
    endif()
  endforeach()
endif()
