// Stategine - a picture packed the way a card keeps it.
//
// What a thing wears is a picture of RGBA pixels, four bytes each; a card
// keeps such a picture just as well in BC7 (BPTC) blocks - sixteen pixels in
// sixteen bytes, a quarter of the size - and reads it as fast. Packed here,
// the picture is a quarter of the bytes to send and a quarter of the memory
// it takes there, and its mipmap chain is made here too, every level, as the
// card would make it: each pixel the mean of the two by two under it, in
// light where the picture is sRGB.
//
// Packing chooses, block by block, the BC7 mode that keeps the picture best
// (one line through colour and height together, or colour and height apart),
// fitted by least squares: what comes back is within a level or two of what
// went in, where a picture's own grain is many times that.
//
// It is presentation and nothing else: the pixels are the surface's, and the
// packed picture is derived from them and from this code alone, so it is kept
// on disk by a digest of both (sg::cache) and made again only when they move.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace sg::render {

struct Packed {
    struct Level {
        int w = 0, h = 0;
        std::size_t at = 0, size = 0;  // where its blocks are in `bytes`, and how many bytes
    };
    int w = 0, h = 0;
    bool srgb = false;
    std::vector<Level> levels;  // the full size first, down to one pixel
    std::string bytes;          // every level's blocks, one after another, rows of blocks top first
};

// Whether a picture of this size is packed: whole blocks at its full size, and
// big enough for the saving to be worth a texture of its own kind.
bool packable(int w, int h);

// The picture (RGBA rows, top first), packed, with its mipmap chain. Pure:
// the same pixels make the same bytes, on any machine.
Packed pack(const unsigned char* rgba, int w, int h, bool srgb);

// The same, kept on disk (sg::cache, kind "packed") by a digest of the pixels,
// the size, whether they are sRGB, and this code.
Packed pack_kept(const unsigned char* rgba, int w, int h, bool srgb);

// One block of four by four pixels (RGBA, rows top first) as BC7, and back.
// What packing a block leaves comes back: its sixteen pixels' four channels'
// differences, squared and summed.
long bc7_encode(const unsigned char px[64], unsigned char out[16]);
void bc7_decode(const unsigned char in[16], unsigned char px[64]);

// The next level of a mipmap chain: half the size (at least one), each pixel
// the mean of the two by two under it - in light, where the picture is sRGB.
std::vector<unsigned char> mip_down(const unsigned char* rgba, int w, int h, bool srgb, int& next_w, int& next_h);

}  // namespace sg::render
