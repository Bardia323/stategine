# The renderer drawn by Vulkan or D3D12 (sg/gl/Native.hpp, src/gl/native):
# the GL calls the renderer makes answered by a native backend, GL still the
# default. Opt in: it downloads what turns GLSL into SPIR-V and HLSL
# (glslang, SPIRV-Cross) and what reaches Vulkan (its headers, volk, VMA),
# and needs a `glfw` target (the program's window) to link.
option(SG_NATIVE_GPU "Build the Vulkan and D3D12 backends behind sg/gl" OFF)

# Beside a program D3D12 may draw, the shader compiler it builds its HLSL
# with (DXC, dxcompiler.dll and dxil.dll, from the Windows SDK where it is
# installed). Without it the backend looks for the SDK's at run time, and
# where there is none D3D12 does not start. And DLSS's own file
# (nvngx_dlss.dll, cmake/dlss.cmake), for Vulkan's upscaler: without it a
# program asking for DLSS is drawn with TAA. Nothing where the backends are
# not built.
function(stategine_native_runtime target)
  if(NOT SG_NATIVE_GPU OR NOT WIN32 OR NOT TARGET stategine_native)
    return()
  endif()
  if(SG_DLSS_RUNTIME AND EXISTS "${SG_DLSS_RUNTIME}")
    add_custom_command(TARGET ${target} POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SG_DLSS_RUNTIME}" $<TARGET_FILE_DIR:${target}>)
  endif()
  find_path(SG_DXC_DIR dxcompiler.dll PATHS "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64" NO_DEFAULT_PATH)
  if(SG_DXC_DIR AND EXISTS "${SG_DXC_DIR}/dxil.dll")
    add_custom_command(TARGET ${target} POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SG_DXC_DIR}/dxcompiler.dll" "${SG_DXC_DIR}/dxil.dll"
                               $<TARGET_FILE_DIR:${target}>)
  endif()
endfunction()

if(NOT SG_NATIVE_GPU OR EMSCRIPTEN OR NOT TARGET stategine_gl)
  return()
endif()

include(FetchContent)
set(SG_KHRONOS_TAG vulkan-sdk-1.4.350.1)
FetchContent_Declare(sg_glslang GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
                     GIT_TAG ${SG_KHRONOS_TAG} GIT_SHALLOW TRUE)
FetchContent_Declare(sg_spirv_cross GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
                     GIT_TAG ${SG_KHRONOS_TAG} GIT_SHALLOW TRUE)
FetchContent_Declare(sg_vulkan_headers GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
                     GIT_TAG ${SG_KHRONOS_TAG} GIT_SHALLOW TRUE)
FetchContent_Declare(sg_volk GIT_REPOSITORY https://github.com/zeux/volk.git
                     GIT_TAG ${SG_KHRONOS_TAG} GIT_SHALLOW TRUE)
FetchContent_Declare(sg_vma GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
                     GIT_TAG v3.3.0 GIT_SHALLOW TRUE)

# Only the libraries: no tools, tests, installs or other languages.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sg_glslang sg_spirv_cross sg_vulkan_headers)
FetchContent_GetProperties(sg_volk)
if(NOT sg_volk_POPULATED)
  FetchContent_Populate(sg_volk)
endif()
FetchContent_GetProperties(sg_vma)
if(NOT sg_vma_POPULATED)
  FetchContent_Populate(sg_vma)
endif()

file(GLOB _sg_native_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/gl/native/*.cpp)
# D3D12 is Windows' own: elsewhere the backend is Vulkan alone, and asking
# for D3D12 says it is not there.
if(NOT WIN32)
  list(FILTER _sg_native_sources EXCLUDE REGEX "/D3D12\\.cpp$")
endif()
add_library(stategine_native STATIC ${_sg_native_sources} ${sg_volk_SOURCE_DIR}/volk.c)
set_source_files_properties(${sg_volk_SOURCE_DIR}/volk.c PROPERTIES LANGUAGE CXX)
target_include_directories(stategine_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${sg_volk_SOURCE_DIR}
                           ${sg_vma_SOURCE_DIR}/include ${sg_glslang_SOURCE_DIR})
target_include_directories(stategine_native SYSTEM PRIVATE ${sg_spirv_cross_SOURCE_DIR})
target_link_libraries(stategine_native PRIVATE glslang glslang-default-resource-limits SPIRV spirv-cross-hlsl
                      spirv-cross-glsl spirv-cross-core Vulkan::Headers PUBLIC glfw)
target_compile_definitions(stategine_native PRIVATE VK_NO_PROTOTYPES)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/gl/native/Vma.cpp ${sg_volk_SOURCE_DIR}/volk.c
                            PROPERTIES COMPILE_OPTIONS "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>")
# (Vulkan's structures are made as `T x{sType}`, the rest of them zero.)
target_link_libraries(stategine_native PRIVATE stategine_warnings)
target_compile_options(stategine_native PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wno-missing-field-initializers>)
target_link_libraries(stategine_gl PUBLIC stategine_native)
target_compile_definitions(stategine_gl PUBLIC SG_NATIVE_GPU=1)
if(WIN32)
  target_link_libraries(stategine_native PRIVATE advapi32)  # (where the driver keeps NGX: its registry keys)
else()
  target_link_libraries(stategine_native PRIVATE ${CMAKE_DL_LIBS})
endif()
include(${CMAKE_CURRENT_LIST_DIR}/dlss.cmake)
