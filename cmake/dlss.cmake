# NVIDIA's DLSS for the Vulkan backend (src/gl/native/Dlss.cpp): the SDK's
# headers - what NGX's names and structures are - and DLSS itself, the file
# that ships beside a program (nvngx_dlss.dll; libnvidia-ngx-dlss.so.*), each
# fetched from NVIDIA's SDK (github.com/NVIDIA/DLSS) at one commit, checked by
# its hash, into the build's _deps - never the source tree: the SDK is under
# NVIDIA's own licence (its LICENSE.txt is fetched beside it). Nothing of it
# is linked: NGX's functions are the driver's, looked up by name. Off, or
# where it cannot be fetched, the backend is built without it and says so.
option(SG_DLSS "Fetch NVIDIA's DLSS SDK for the Vulkan backend (SG_NATIVE_GPU)" ON)

# The SDK's release (v310.9.16) and where its files are.
set(SG_DLSS_VERSION 310.9.16)
set(SG_DLSS_COMMIT 9981c6350dc58c0ce21f024fd65e31e851c31689)
set(SG_DLSS_RUNTIME "" CACHE INTERNAL "DLSS's own file, to ship beside a program")

function(_sg_dlss_fetch)
  if(NOT SG_DLSS OR NOT TARGET stategine_native)
    return()
  endif()
  set(_root "${FETCHCONTENT_BASE_DIR}/dlss-${SG_DLSS_VERSION}")
  set(_url "https://raw.githubusercontent.com/NVIDIA/DLSS/${SG_DLSS_COMMIT}")
  set(_files
      "include/nvsdk_ngx_defs.h" ea23f33497cd274860d1c25a97644fce807dcb0037c594547203343103fad03e
      "include/nvsdk_ngx_params.h" 943bc8cc5cdae03b6303016fbad3183636f2335ae27a2d18776798c3b4efabbc
      "include/nvsdk_ngx_defs_vk.h" 0a24d0861ace7d6b9362a67b7f08bea1b33ea123c5ce730b19133de8a891d031
      "LICENSE.txt" d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0)
  if(WIN32)
    set(_runtime "lib/Windows_x86_64/rel/nvngx_dlss.dll")
    list(APPEND _files "${_runtime}" d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(_runtime "lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.${SG_DLSS_VERSION}")
    list(APPEND _files "${_runtime}" 30982c937afed85c96b8d313323de9a650a2eec6de1bb0f714b21750f4ce98f6)
  else()
    message(STATUS "stategine: no DLSS for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
    return()
  endif()
  list(LENGTH _files _n)
  math(EXPR _last "${_n} - 1")
  foreach(_i RANGE 0 ${_last} 2)
    math(EXPR _h "${_i} + 1")
    list(GET _files ${_i} _file)
    list(GET _files ${_h} _hash)
    set(_at "${_root}/${_file}")
    if(EXISTS "${_at}")
      file(SHA256 "${_at}" _has)
      if(_has STREQUAL _hash)
        continue()
      endif()
    endif()
    message(STATUS "stategine: fetching DLSS ${SG_DLSS_VERSION}: ${_file}")
    file(DOWNLOAD "${_url}/${_file}" "${_at}.part" EXPECTED_HASH SHA256=${_hash} STATUS _status TLS_VERIFY ON)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
      file(REMOVE "${_at}.part")
      message(WARNING "stategine: DLSS not fetched (${_status}): the Vulkan backend is built without it")
      return()
    endif()
    file(RENAME "${_at}.part" "${_at}")
  endforeach()
  target_include_directories(stategine_native SYSTEM PRIVATE "${_root}/include")
  get_filename_component(_dir "${_root}/${_runtime}" DIRECTORY)
  target_compile_definitions(stategine_native PRIVATE SG_DLSS=1 "SG_DLSS_DIR=\"${_dir}\"")
  set(SG_DLSS_RUNTIME "${_root}/${_runtime}" CACHE INTERNAL "DLSS's own file, to ship beside a program")
endfunction()
_sg_dlss_fetch()
