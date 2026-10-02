# The network's CUDA backend uses device-only PTX and the C driver API.
# Its host code is compiled by the project's compiler, including MinGW;
# no C++ ABI crosses between the project's compiler and a vendor compiler.
option(SG_NET_CUDA "Build the network CUDA backend when the toolkit is found" ON)
if(EMSCRIPTEN OR NOT SG_NET_CUDA)
  return()
endif()
find_package(CUDAToolkit QUIET)
if(NOT CUDAToolkit_FOUND OR NOT TARGET CUDA::nvrtc OR NOT TARGET CUDA::cuda_driver)
  message(STATUS "stategine: network CPU backend (CUDA toolkit not found)")
  return()
endif()
add_executable(sg_net_ptx tools/network_ptx.cpp)
target_compile_features(sg_net_ptx PRIVATE cxx_std_17)
target_link_libraries(sg_net_ptx PRIVATE CUDA::nvrtc stategine_static_runtime)
if(WIN32)
  file(GLOB _sg_nvrtc_dlls "${CUDAToolkit_BIN_DIR}/nvrtc*.dll")
  if(_sg_nvrtc_dlls)
    add_custom_command(TARGET sg_net_ptx POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_sg_nvrtc_dlls} "$<TARGET_FILE_DIR:sg_net_ptx>" VERBATIM)
  endif()
endif()
set(_sg_ptx "${CMAKE_CURRENT_BINARY_DIR}/gpu/network.ptx")
set(_sg_ptx_header "${CMAKE_CURRENT_BINARY_DIR}/gpu/network_ptx.h")
add_custom_command(OUTPUT "${_sg_ptx}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/gpu"
  COMMAND sg_net_ptx "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/network_cuda.cu" "${_sg_ptx}"
  DEPENDS sg_net_ptx "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/network_cuda.cu" VERBATIM)
add_custom_command(OUTPUT "${_sg_ptx_header}"
  COMMAND ${CMAKE_COMMAND} "-DIN=${_sg_ptx}" "-DOUT=${_sg_ptx_header}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_network_ptx.cmake"
  DEPENDS "${_sg_ptx}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_network_ptx.cmake" VERBATIM)
target_sources(stategine_net PRIVATE src/gpu/network_cuda.cpp "${_sg_ptx_header}")
target_include_directories(stategine_net PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gpu")
target_link_libraries(stategine_net PUBLIC CUDA::cuda_driver)
target_compile_definitions(stategine_net PRIVATE SG_NET_WITH_CUDA)
message(STATUS "stategine: network CUDA backend (matrix-free PTX)")
