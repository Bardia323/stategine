# ONNX Runtime for what the engine runs as a learned model - MotionBricks'
# motion (src/domains/BricksModel.cpp): its C API's headers, and the library
# itself, the file that ships beside a program (libonnxruntime.so.*;
# onnxruntime.dll), fetched from Microsoft's release at one version, checked
# by its hash, into the build's _deps - never the source tree (MIT, its
# LICENSE fetched with it). Nothing of it is linked: the library is opened
# at run time and its one entry (OrtGetApiBase) looked up by name, so a
# program without it - or a build for the web - runs, and says what it
# cannot do. Off, or where it cannot be fetched, the engine is built without it.
option(SG_ONNXRUNTIME "Fetch ONNX Runtime for learned models (MotionBricks' motion)" ON)

set(SG_ORT_VERSION 1.20.1)
set(SG_ORT_RUNTIME "" CACHE INTERNAL "ONNX Runtime's own file, to ship beside a program")

function(_sg_ort_fetch)
  if(NOT SG_ONNXRUNTIME OR EMSCRIPTEN)
    return()
  endif()
  set(_base "https://github.com/microsoft/onnxruntime/releases/download/v${SG_ORT_VERSION}")
  if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(_name "onnxruntime-win-x64-${SG_ORT_VERSION}")
    set(_archive "${_name}.zip")
    set(_hash 78d447051e48bd2e1e778bba378bec4ece11191c9e538cf7b2c4a4565e8f5581)
    set(_runtime "lib/onnxruntime.dll")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_name "onnxruntime-linux-x64-${SG_ORT_VERSION}")
    set(_archive "${_name}.tgz")
    set(_hash 67db4dc1561f1e3fd42e619575c82c601ef89849afc7ea85a003abbac1a1a105)
    set(_runtime "lib/libonnxruntime.so.${SG_ORT_VERSION}")
  else()
    message(STATUS "stategine: no ONNX Runtime for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
    return()
  endif()
  set(_root "${FETCHCONTENT_BASE_DIR}/onnxruntime-${SG_ORT_VERSION}")
  set(_dir "${_root}/${_name}")
  if(NOT EXISTS "${_dir}/${_runtime}" OR NOT EXISTS "${_dir}/include/onnxruntime_c_api.h")
    set(_at "${_root}/${_archive}")
    message(STATUS "stategine: fetching ONNX Runtime ${SG_ORT_VERSION}: ${_archive}")
    file(DOWNLOAD "${_base}/${_archive}" "${_at}.part" EXPECTED_HASH SHA256=${_hash} STATUS _status TLS_VERIFY ON)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
      file(REMOVE "${_at}.part")
      message(WARNING "stategine: ONNX Runtime not fetched (${_status}): the engine is built without it")
      return()
    endif()
    file(RENAME "${_at}.part" "${_at}")
    file(ARCHIVE_EXTRACT INPUT "${_at}" DESTINATION "${_root}")
    file(REMOVE "${_at}")
  endif()
  target_include_directories(stategine SYSTEM PRIVATE "${_dir}/include")
  get_filename_component(_file "${_runtime}" NAME)
  target_compile_definitions(stategine PRIVATE SG_ONNXRUNTIME=1 "SG_ORT_LIBRARY=\"${_dir}/${_runtime}\"" "SG_ORT_FILE=\"${_file}\"")
  if(NOT WIN32)
    target_link_libraries(stategine PUBLIC ${CMAKE_DL_LIBS})
  endif()
  set(SG_ORT_RUNTIME "${_dir}/${_runtime}" CACHE INTERNAL "ONNX Runtime's own file, to ship beside a program")
endfunction()
_sg_ort_fetch()

# Beside a program that runs a learned model, ONNX Runtime's own file: the
# program finds it there first (sg/domains/Bricks.hpp says where else).
function(stategine_ort_runtime target)
  if(SG_ORT_RUNTIME AND EXISTS "${SG_ORT_RUNTIME}")
    add_custom_command(TARGET ${target} POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SG_ORT_RUNTIME}" $<TARGET_FILE_DIR:${target}>)
  endif()
endfunction()
