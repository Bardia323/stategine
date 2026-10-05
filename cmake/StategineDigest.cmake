# stategine_code_digest(<target> <MACRO> <files...>)
#
# A digest of the code that makes some derived data - a modeller's meshes, a
# brush's paint - as a header `<MACRO>.hpp` defining <MACRO> as a string: so a
# disk cache keyed on it (sg::cache) misses as soon as any of those files
# changes, with nothing to remember to bump. The header is written again only
# when the digest changes, so an edit elsewhere recompiles nothing.
set(_sg_digest_script "${CMAKE_CURRENT_LIST_DIR}/digest.cmake" CACHE INTERNAL "the script stategine_code_digest runs")

function(stategine_code_digest target macro)
  set(dir "${CMAKE_CURRENT_BINARY_DIR}/sg_digest/${target}")
  set(out "${dir}/${macro}.hpp")
  string(REPLACE ";" "|" files "${ARGN}")
  add_custom_command(OUTPUT "${out}"
    COMMAND ${CMAKE_COMMAND} "-DOUT=${out}" "-DMACRO=${macro}" "-DFILES=${files}" -P "${_sg_digest_script}"
    DEPENDS ${ARGN} "${_sg_digest_script}"
    COMMENT "digest of the code behind ${macro}"
    VERBATIM)
  target_sources(${target} PRIVATE "${out}")
  target_include_directories(${target} PRIVATE "${dir}")
endfunction()
