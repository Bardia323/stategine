# Stategine - modules: a game's states, built the way the engine is.
#
# A module is a state of its own (or a few that belong together), and the
# compiler is the interface to the machine. Each module is a library: its
# headers say what a thing is, its .cpp files what it does. The compiler takes
# every file on its own and all at once; the linker is the one pass that
# brings them together - so a change recompiles only the files that changed.
#
#   stategine_module(<name> [USES <module>...] [DIR <dir>] [ROOT <dir>]
#                    [EXCLUDE <file>...] [INCLUDED_AS <name>])
#
# makes the library <name> of every .cpp and .c in DIR (default:
# ROOT/<name>; ROOT defaults to ${CMAKE_CURRENT_SOURCE_DIR}/src and is the
# include root, so its files are included as "<name>/file.hpp"), linked to
# the modules it USES and to the engine. EXCLUDE leaves out files by name
# (a main.cpp, what only a program needs). INCLUDED_AS is the folder name its
# headers are included by, when the library is named otherwise.
#
#   stategine_check_modules([NAME <test>])
#
# after the modules, adds one test that holds them to the engine's rules:
# a module includes only itself, the engine and what it USES (never what
# uses it), and its headers keep only short bodies, templates and what a
# comment says why (`// inline: ...`) - see tools/shape.cpp.

set(STATEGINE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE INTERNAL "")

function(stategine_module name)
  cmake_parse_arguments(M "" "DIR;ROOT;INCLUDED_AS" "USES;EXCLUDE" ${ARGN})
  if(NOT M_ROOT)
    set(M_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/src)
  endif()
  if(NOT M_DIR)
    set(M_DIR ${M_ROOT}/${name})
  endif()
  get_filename_component(M_DIR "${M_DIR}" ABSOLUTE)
  if(NOT M_INCLUDED_AS)
    file(RELATIVE_PATH M_INCLUDED_AS "${M_ROOT}" "${M_DIR}")
  endif()
  file(GLOB _src CONFIGURE_DEPENDS ${M_DIR}/*.cpp ${M_DIR}/*.c)
  foreach(_x ${M_EXCLUDE})
    list(FILTER _src EXCLUDE REGEX "/${_x}([.]c|[.]cpp)?$")
  endforeach()
  # What a module uses is named by its library; what it includes, by folder.
  set(_uses_as "")
  foreach(_u ${M_USES})
    get_property(_as GLOBAL PROPERTY STATEGINE_MODULE_${_u}_AS)
    if(NOT _as)
      message(FATAL_ERROR "stategine_module(${name}): ${_u} is not a module declared before it")
    endif()
    list(APPEND _uses_as ${_as})
  endforeach()
  if(_src)
    add_library(${name} STATIC ${_src})
    target_include_directories(${name} PUBLIC ${M_ROOT})
    target_link_libraries(${name} PUBLIC ${M_USES} stategine::stategine)
  else()
    add_library(${name} INTERFACE)
    target_include_directories(${name} INTERFACE ${M_ROOT})
    target_link_libraries(${name} INTERFACE ${M_USES} stategine::stategine)
  endif()
  string(REPLACE ";" "," _uses_list "${_uses_as}")
  set_property(GLOBAL APPEND PROPERTY STATEGINE_MODULES "${M_INCLUDED_AS}=${M_DIR}:${_uses_list}")
  set_property(GLOBAL PROPERTY STATEGINE_MODULE_${name}_AS "${M_INCLUDED_AS}")
endfunction()

function(stategine_check_modules)
  cmake_parse_arguments(C "" "NAME" "" ${ARGN})
  if(NOT C_NAME)
    set(C_NAME modules_are_their_own)
  endif()
  if(NOT TARGET stategine_shape)
    if(CMAKE_CROSSCOMPILING)
      get_filename_component(_host_tools "${STATEGINE_SGC_EXECUTABLE}" DIRECTORY)
      find_program(STATEGINE_SHAPE_EXECUTABLE NAMES stategine_shape stategine_shape.exe HINTS "${_host_tools}" "${_host_tools}/laws" NO_DEFAULT_PATH)
      if(NOT STATEGINE_SHAPE_EXECUTABLE)
        message(FATAL_ERROR "Cross module checks need a native STATEGINE_SHAPE_EXECUTABLE beside sgc")
      endif()
      add_executable(stategine_shape IMPORTED GLOBAL)
      set_target_properties(stategine_shape PROPERTIES IMPORTED_LOCATION "${STATEGINE_SHAPE_EXECUTABLE}" CROSSCOMPILING_EMULATOR "")
    else()
    add_executable(stategine_shape ${STATEGINE_SOURCE_DIR}/tools/shape.cpp)
    target_compile_features(stategine_shape PRIVATE cxx_std_17)
    if(MINGW)
      target_link_options(stategine_shape PRIVATE -static)
    endif()
    endif()
  endif()
  get_property(_modules GLOBAL PROPERTY STATEGINE_MODULES)
  add_test(NAME ${C_NAME} COMMAND stategine_shape ${_modules})
endfunction()
