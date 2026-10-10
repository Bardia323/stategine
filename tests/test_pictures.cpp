// Stategine - pictures: a file read back as it was written.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "sg/domains/Modeler.hpp"
#include "sg/pictures/Pictures.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}
}  // namespace

int main() {
    // A picture the engine writes (sg::sculpt::png), read back to the byte -
    // its rows top first, given full alpha.
    const int w = 7, h = 5;
    std::vector<unsigned char> rgb(static_cast<std::size_t>(w) * h * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i) rgb[i] = static_cast<unsigned char>((i * 37 + 11) % 256);
    const auto dir = std::filesystem::temp_directory_path() / "sg_pictures";
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "round.png").string();
    {
        std::ofstream o(path, std::ios::binary);
        o << sg::sculpt::png(rgb, w, h);
    }
    int rw = 0, rh = 0;
    std::vector<unsigned char> rgba;
    const bool read = sg::pictures::read(path, rw, rh, rgba);
    check(read && rw == w && rh == h, "a PNG is read at its size");
    bool same = read && rgba.size() == static_cast<std::size_t>(w) * h * 4;
    for (int i = 0; same && i < w * h; ++i)
        same = rgba[i * 4] == rgb[i * 3] && rgba[i * 4 + 1] == rgb[i * 3 + 1] && rgba[i * 4 + 2] == rgb[i * 3 + 2] && rgba[i * 4 + 3] == 255;
    check(same, "every pixel as it was written, top row first, opaque");

    const bool missing = sg::pictures::read((dir / "no such.png").string(), rw, rh, rgba);
    check(!missing && rw == 0 && !sg::pictures::last_error().empty(), "a file that is not there is not read, and says why");
    std::filesystem::remove_all(dir);
    return failures ? 1 : 0;
}
