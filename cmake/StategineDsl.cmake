# Stategine - the DSL, compiled with the build.
#
#   stategine_compile_dsl(<target> NAME <name> SOURCES <file.sg>... [NAMESPACE <ns>])
#
# runs sgc on the sources and adds the C++ it writes to <target>: a function
#
#     <ns>::build_<name>(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&)
#
# that makes the construction the sources declare, through the engine's own
# API. The generated file lives in the build directory; the source is the
# source of truth. <target> is linked to stategine::dsl.

function(stategine_compile_dsl target)
  cmake_parse_arguments(D "" "NAME;NAMESPACE" "SOURCES" ${ARGN})
  if(NOT D_NAME)
    message(FATAL_ERROR "stategine_compile_dsl(${target}): NAME is required")
  endif()
  if(NOT D_NAMESPACE)
    set(D_NAMESPACE sgen)
  endif()
  set(_srcs "")
  foreach(_s ${D_SOURCES})
    get_filename_component(_abs "${_s}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND _srcs "${_abs}")
  endforeach()
  set(_out "${CMAKE_CURRENT_BINARY_DIR}/dsl/${D_NAME}.cpp")
  set(_args -o "${_out}" --name ${D_NAME} --ns ${D_NAMESPACE} --facts "${CMAKE_CURRENT_BINARY_DIR}/dsl/${D_NAME}.facts")
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.20 AND (CMAKE_GENERATOR MATCHES "Ninja" OR CMAKE_GENERATOR MATCHES "Makefiles"))
    list(APPEND _args --deps "${_out}.d")
    add_custom_command(OUTPUT "${_out}"
      COMMAND sgc ${_args} ${_srcs}
      DEPENDS sgc ${_srcs}
      DEPFILE "${_out}.d"
      COMMENT "sgc ${D_NAME}"
      VERBATIM)
  else()
    add_custom_command(OUTPUT "${_out}"
      COMMAND sgc ${_args} ${_srcs}
      DEPENDS sgc ${_srcs}
      COMMENT "sgc ${D_NAME}"
      VERBATIM)
  endif()
  target_sources(${target} PRIVATE "${_out}")
  target_link_libraries(${target} PUBLIC stategine::dsl)
endfunction()
