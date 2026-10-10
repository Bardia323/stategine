// Stategine - pictures: a picture file read as pixels, for a program to hand
// the engine (`Texture::set_reader(sg::pictures::read)`).
//
// The engine reads no image format of its own: a picture is the program's to
// give, as a device is. This is that device, ready made, for those who want
// one - a library beside the engine (`stategine::pictures`), never under it,
// knowing nothing of states. It reads what materials come as: PNG (8 or 16
// bits, taken to 8), JPEG, TGA, BMP and binary PNM (PPM, PGM), by stb_image
// (third_party/stb, public domain or MIT).
#pragma once

#include <string>
#include <vector>

namespace sg::pictures {

// A picture file as RGBA rows, top first, a byte a channel - grey and
// colour without alpha given full alpha. False when it cannot be read: no
// such file, a format not read here, a damaged one (`last_error` says which).
// Safe to call from many threads at once.
bool read(const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba);

// Why the last `read` on this thread failed.
std::string last_error();

}  // namespace sg::pictures
