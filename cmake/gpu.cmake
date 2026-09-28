# Stategine - the algebra's GPU backends (include/sg/gpu, src/gpu).
#
# Each is built where its toolchain is found, and adds itself to the
# `stategine::gpu` target with the definition that switches it on in
# sg/gpu/AlgebraBackend.hpp. Where none is found, the target is empty and
# every backend runs on the CPU (and says it is not here).
option(SG_GPU "Build the algebra's GPU backends where their toolchains are found" ON)
if(NOT SG_GPU)
  return()
endif()

set(SG_GPU_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/gpu)
set(SG_GPU_BUILT "")
include(CheckLanguage)

# NVIDIA: CUDA.
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  add_library(stategine_gpu_cuda STATIC ${SG_GPU_SRC}/cuda.cu)
  target_include_directories(stategine_gpu_cuda PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${SG_GPU_SRC})
  set_target_properties(stategine_gpu_cuda PROPERTIES CUDA_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(stategine_gpu INTERFACE stategine_gpu_cuda)
  target_compile_definitions(stategine_gpu INTERFACE SG_WITH_CUDA)
  list(APPEND SG_GPU_BUILT cuda)
endif()

# AMD: ROCm, the same kernel as HIP.
check_language(HIP)
if(CMAKE_HIP_COMPILER)
  enable_language(HIP)
  add_library(stategine_gpu_rocm STATIC ${SG_GPU_SRC}/rocm.hip)
  target_include_directories(stategine_gpu_rocm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${SG_GPU_SRC})
  set_target_properties(stategine_gpu_rocm PROPERTIES HIP_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
  target_link_libraries(stategine_gpu INTERFACE stategine_gpu_rocm)
  target_compile_definitions(stategine_gpu INTERFACE SG_WITH_ROCM)
  list(APPEND SG_GPU_BUILT rocm)
endif()

# Vulkan: the compute shader, compiled to SPIR-V and built in.
find_package(Vulkan QUIET)
find_program(SG_GLSLANG NAMES glslangValidator)
find_program(SG_GLSLC NAMES glslc)
if(Vulkan_FOUND AND (SG_GLSLANG OR SG_GLSLC))
  set(SG_SPV ${CMAKE_CURRENT_BINARY_DIR}/gpu/sg_algebra.spv)
  set(SG_SPV_H ${CMAKE_CURRENT_BINARY_DIR}/gpu/sg_algebra_spv.h)
  if(SG_GLSLANG)
    set(SG_GLSL_CMD ${SG_GLSLANG} -V ${SG_GPU_SRC}/algebra.comp -o ${SG_SPV})
  else()
    set(SG_GLSL_CMD ${SG_GLSLC} -fshader-stage=compute ${SG_GPU_SRC}/algebra.comp -o ${SG_SPV})
  endif()
  add_custom_command(OUTPUT ${SG_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/gpu
    COMMAND ${SG_GLSL_CMD}
    DEPENDS ${SG_GPU_SRC}/algebra.comp VERBATIM)
  add_custom_command(OUTPUT ${SG_SPV_H}
    COMMAND ${CMAKE_COMMAND} -DIN=${SG_SPV} -DOUT=${SG_SPV_H} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spv.cmake
    DEPENDS ${SG_SPV} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_spv.cmake VERBATIM)
  add_library(stategine_gpu_vulkan STATIC ${SG_GPU_SRC}/vulkan.cpp ${SG_SPV_H})
  target_include_directories(stategine_gpu_vulkan PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/gpu)
  target_compile_features(stategine_gpu_vulkan PRIVATE cxx_std_17)
  target_link_libraries(stategine_gpu_vulkan PUBLIC Vulkan::Vulkan)
  target_link_libraries(stategine_gpu INTERFACE stategine_gpu_vulkan)
  target_compile_definitions(stategine_gpu INTERFACE SG_WITH_VULKAN)
  list(APPEND SG_GPU_BUILT vulkan)
endif()

# Apple: Metal, its kernel compiled from source when first used.
if(APPLE)
  enable_language(OBJCXX)
  add_library(stategine_gpu_metal STATIC ${SG_GPU_SRC}/metal.mm)
  target_include_directories(stategine_gpu_metal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_features(stategine_gpu_metal PRIVATE cxx_std_17)
  target_compile_options(stategine_gpu_metal PRIVATE -fobjc-arc)
  target_link_libraries(stategine_gpu_metal PUBLIC "-framework Metal" "-framework Foundation")
  target_link_libraries(stategine_gpu INTERFACE stategine_gpu_metal)
  target_compile_definitions(stategine_gpu INTERFACE SG_WITH_METAL)
  list(APPEND SG_GPU_BUILT metal)
endif()

if(SG_GPU_BUILT)
  message(STATUS "stategine: GPU backends built in: ${SG_GPU_BUILT}")
else()
  message(STATUS "stategine: no GPU toolchain found; the algebra's batches run on the CPU")
endif()
