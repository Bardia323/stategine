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

// The card's own upscaler, the one thing a native backend does that GL has
// no call for: NVIDIA's DLSS, on Vulkan (src/gl/native/Dlss.cpp). It makes
// `output` (a texture of the screen's size) from `color`, drawn at fewer
// pixels (or as many: DLAA) moved by `jitter` (in its own pixels), its
// `depth`, how each of its pixels moved since the last frame (`motion`, RG:
// times `motion_x/y`, pixels from where it is to where it was) and the
// pictures before - let go where `reset` says. Textures are GL's names.
// Machinery, as a blit is: the renderer asks for it only where its quality
// says (Quality::upscaler), and where it cannot be (not Vulkan, no NVIDIA
// card, no DLSS beside the program) false says why, and nothing is done.
struct Upscale {
    unsigned color = 0, depth = 0, motion = 0, output = 0;
    float jitter_x = 0, jitter_y = 0;
    float motion_x = 1, motion_y = 1;
    bool reset = false, reversed = false;
    int quality = 0;  // 0 quality, 1 balanced, 2 performance, 3 ultra performance, 4 native (DLAA)
    float frame_ms = 0;
    // What the eye multiplies the light by to show it (0: DLSS finds its own).
    float exposure = 0;
};
bool can_upscale(std::string& why);
bool upscale(const Upscale&, std::string& why);

}  // namespace sg::gl
