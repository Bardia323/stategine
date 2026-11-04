# What turns the renderer's GLSL into SPIR-V and back into another language
# (glslang, SPIRV-Cross): for the native backends (HLSL, Vulkan's SPIR-V) and
# for the browser's GL (GLSL ES). Only the libraries: no tools, tests,
# installs or other languages.
include_guard(GLOBAL)
include(FetchContent)
set(SG_KHRONOS_TAG vulkan-sdk-1.4.350.1)
FetchContent_Declare(sg_glslang GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
                     GIT_TAG ${SG_KHRONOS_TAG} GIT_SHALLOW TRUE)
FetchContent_Declare(sg_spirv_cross GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
                     GIT_TAG ${SG_KHRONOS_TAG} GIT_SHALLOW TRUE)
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
FetchContent_MakeAvailable(sg_glslang sg_spirv_cross)
