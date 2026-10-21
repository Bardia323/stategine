// Stategine - NVIDIA's DLSS on the Vulkan device: the card's own upscaler,
// asked through NGX (the driver's library for it) to make a picture of the
// screen's size from a jittered one drawn at fewer pixels, its depth and how
// each pixel moved, and the pictures before. Nothing in the world knows of
// it: it is the backend's, as a blit is.
//
// The NGX core comes with NVIDIA's driver (libnvidia-ngx.so.1; _nvngx.dll
// in the driver's store on Windows) and is loaded when first asked for, by
// name - no NVIDIA library is linked. DLSS itself is a file of the SDK's
// that ships beside the program (nvngx_dlss.dll, libnvidia-ngx-dlss.so.*).
// Where either is missing, or the card has no DLSS, `start` says why and
// nothing else changes.
#pragma once

#include <volk.h>

#include <string>
#include <vector>

namespace sg::gl::native {

class Dlss {
public:
    // What the card's DLSS needs of the instance and the device, asked
    // before either is made (nothing where there is no NGX).
    static void wanted(std::vector<std::string>& instance, std::vector<std::string>& device);

    ~Dlss();
    // NGX started on this device: false, with why, where it cannot be.
    bool start(VkInstance, VkPhysicalDevice, VkDevice, std::string& why);

    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        int w = 0, h = 0;
        bool depth = false;
        bool written = false;  // (the picture made: written by DLSS)
    };
    struct Ask {
        Image color, depth, motion, output;
        float jitter_x = 0, jitter_y = 0;  // the jitter, in the drawn pixels
        float motion_x = 1, motion_y = 1;  // what the motion is multiplied by to be pixels from here to where it was
        bool reset = false;                // what was made of the frames before let go
        bool reversed = false;             // depth near 1, far 0
        int quality = 0;                   // 0 quality, 1 balanced, 2 performance, 3 ultra performance, 4 native (DLAA)
        float frame_ms = 0;
        float exposure = 0;                // what the eye multiplies the light by (0: DLSS finds its own)
    };
    // Recorded into `cmd`: the picture made into `output`. The feature is
    // made again when the sizes or the quality change.
    bool evaluate(VkCommandBuffer cmd, const Ask&, std::string& why);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace sg::gl::native
