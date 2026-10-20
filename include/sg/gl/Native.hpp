// Stategine - the renderer drawn by Vulkan or D3D12 instead of OpenGL.
//
// Nothing that draws changes: the renderer, and whatever draws beside it,
// call GL as ever (sg/gl/GL.hpp), and those calls are answered by a native
// backend that does what GL would. GL stays the default; `SG_GPU=vulkan` or
// `SG_GPU=d3d12` (or use_api, before the window is made) asks for another.
// Built only where the engine was configured with SG_NATIVE_GPU.
#pragma once

#include <string>

struct GLFWwindow;

namespace sg::gl {

enum class Api { gl, vulkan, d3d12 };

// What is asked for: SG_GPU (gl, vulkan or vk, d3d12 or dx12) over use_api.
Api asked_api();
void use_api(Api);
const char* api_name(Api);
// What answers the GL calls now.
Api current_api();

// The calls answered by `api` for `window` (made with GLFW_NO_API). False,
// with why, where it cannot be - and the calls are left as they were.
bool start_native(Api api, GLFWwindow* window, std::string& why);
void stop_native();
// The frame shown (glfwSwapBuffers' part).
void present_native(bool vsync);

}  // namespace sg::gl
