#include "sg/pictures/Pictures.hpp"

#include <cstring>

// stb_image, built here once: only the formats materials come as, and file
// names as UTF-8 on Windows (as everywhere else). Its own warnings are its own.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_PNM
#define STBI_WINDOWS_UTF8
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace sg::pictures {

namespace {
thread_local std::string failed;
}  // namespace

bool read(const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba) {
    int channels = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!px) {
        const char* why = stbi_failure_reason();
        failed = path + ": " + (why ? why : "cannot be read");
        w = h = 0;
        return false;
    }
    rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(px);
    failed.clear();
    return true;
}

std::string last_error() { return failed; }

}  // namespace sg::pictures
