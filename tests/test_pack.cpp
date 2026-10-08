// Stategine - a picture packed for the card (sg/render/Pack.hpp): what comes
// back is the picture, within a level or two, on every kind of block a
// painted map is made of; its mipmap chain is the card's; it is the same
// bytes every time, and kept on disk as made.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "sg/core/Cache.hpp"
#include "sg/render/Pack.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

uint32_t next(uint32_t& s) {
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return s;
}

// What a painted map is made of, in one picture: a smooth ground with grain,
// strokes with hard edges across it, height going its own way, flat patches,
// and a corner of pure noise.
std::vector<unsigned char> painted(int w, int h, uint32_t seed) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
            const double u = x / double(w), v = y / double(h);
            const int grain = static_cast<int>(next(seed) % 9) - 4;
            int r = static_cast<int>(120 + 60 * std::sin(u * 7) + grain), g = static_cast<int>(100 + 40 * v * 3 + grain),
                b = static_cast<int>(80 + 50 * std::cos(v * 5) + grain);
            int a = static_cast<int>(128 + 100 * std::sin((u + v) * 11)) + static_cast<int>(next(seed) % 5) - 2;
            // A stroke: a band of another colour, its edge hard.
            if (std::fabs(u * 2 - v - 0.3) < 0.05) r = 230, g = 40, b = 30, a = 250;
            if (x < w / 8 && y < h / 8) r = 90, g = 140, b = 60, a = 7;  // flat
            if (x >= w - w / 8 && y >= h - h / 8) {
                r = static_cast<int>(next(seed) & 255), g = static_cast<int>(next(seed) & 255), b = static_cast<int>(next(seed) & 255),
                a = static_cast<int>(next(seed) & 255);
            }
            p[0] = static_cast<unsigned char>(std::clamp(r, 0, 255));
            p[1] = static_cast<unsigned char>(std::clamp(g, 0, 255));
            p[2] = static_cast<unsigned char>(std::clamp(b, 0, 255));
            p[3] = static_cast<unsigned char>(std::clamp(a, 0, 255));
        }
    return px;
}

// Level 0 of `p`, decoded.
std::vector<unsigned char> unpacked(const sg::render::Packed& p, int level = 0) {
    const auto& l = p.levels[level];
    const int bw = (l.w + 3) / 4, bh = (l.h + 3) / 4;
    std::vector<unsigned char> out(static_cast<std::size_t>(l.w) * l.h * 4);
    unsigned char px[64];
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            sg::render::bc7_decode(reinterpret_cast<const unsigned char*>(p.bytes.data() + l.at + (static_cast<std::size_t>(by) * bw + bx) * 16), px);
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const int sx = bx * 4 + x, sy = by * 4 + y;
                    if (sx < l.w && sy < l.h) std::memcpy(&out[(static_cast<std::size_t>(sy) * l.w + sx) * 4], px + (y * 4 + x) * 4, 4);
                }
        }
    return out;
}

struct Error {
    double rmse = 0;
    int most = 0;
};

Error compare(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, int x0, int y0, int x1, int y1, int w) {
    double se = 0, n = 0;
    Error e;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            for (int c = 0; c < 4; ++c) {
                const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4 + c;
                const int d = int(a[i]) - int(b[i]);
                se += d * d, n += 1, e.most = std::max(e.most, std::abs(d));
            }
    e.rmse = std::sqrt(se / n);
    return e;
}

void test_blocks_round_trip() {
    // A flat block of any colour comes back within a level.
    uint32_t seed = 7;
    for (int k = 0; k < 200; ++k) {
        unsigned char px[64], back[64], blk[16];
        const unsigned char c[4] = {static_cast<unsigned char>(next(seed)), static_cast<unsigned char>(next(seed)), static_cast<unsigned char>(next(seed)),
                                    static_cast<unsigned char>(next(seed))};
        for (int i = 0; i < 16; ++i) std::memcpy(px + i * 4, c, 4);
        sg::render::bc7_encode(px, blk);
        sg::render::bc7_decode(blk, back);
        int most = 0;
        for (int i = 0; i < 64; ++i) most = std::max(most, std::abs(int(px[i]) - int(back[i])));
        check(most <= 1, "a flat block comes back within a level (off by " + std::to_string(most) + ")");
    }
    // Two colours, any two, in any pattern: on one line, so exactly.
    for (int k = 0; k < 200; ++k) {
        unsigned char px[64], back[64], blk[16], a[4], b[4];
        for (int c = 0; c < 4; ++c) a[c] = static_cast<unsigned char>(next(seed) & 0xfe), b[c] = static_cast<unsigned char>(next(seed) | 1);
        const uint32_t mask = next(seed);
        for (int i = 0; i < 16; ++i) std::memcpy(px + i * 4, (mask >> i) & 1 ? a : b, 4);
        sg::render::bc7_encode(px, blk);
        sg::render::bc7_decode(blk, back);
        int most = 0;
        for (int i = 0; i < 64; ++i) most = std::max(most, std::abs(int(px[i]) - int(back[i])));
        check(most <= 2, "a block of two colours comes back within two levels (off by " + std::to_string(most) + ")");
    }
}

void test_painted_picture() {
    const int W = 256, H = 256;
    const auto px = painted(W, H, 1234);
    const sg::render::Packed p = sg::render::pack(px.data(), W, H, true);
    check(p.levels.size() == 9, "a mipmap chain to one pixel: 9 levels of 256");
    check(p.levels[0].size == static_cast<std::size_t>(W / 4) * (H / 4) * 16, "sixteen bytes a block");
    check(p.bytes.size() * 3 <= px.size() + 256, "packed, with every level, is a third the size of the picture");
    const auto back = unpacked(p);
    // The painted part (all but the noise corner): within a level or so.
    const Error ground = compare(px, back, 0, 0, W, H - H / 8, W);
    check(ground.rmse < 1.5, "a painted picture comes back within a level (rmse " + std::to_string(ground.rmse) + ")");
    const Error flat = compare(px, back, 0, 0, W / 8, H / 8, W);
    check(flat.most <= 1, "a flat patch comes back flat (off by " + std::to_string(flat.most) + ")");
    // Pure noise is the worst a block can be, and still near.
    const Error noise = compare(px, back, W - W / 8, H - H / 8, W, H, W);
    check(noise.rmse < 60, "pure noise is kept as well as a line through it can (rmse " + std::to_string(noise.rmse) + ")");
    // The same pixels, the same bytes.
    const sg::render::Packed again = sg::render::pack(px.data(), W, H, true);
    check(again.bytes == p.bytes, "packing is a function of the pixels");
}

void test_mipmaps() {
    // Black and white, half and half: their mean, in light, is not 128 but 188.
    const int W = 4, H = 4;
    std::vector<unsigned char> px(W * H * 4);
    for (int i = 0; i < W * H; ++i) {
        const unsigned char v = (i % 2) ? 255 : 0;
        px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v;
        px[i * 4 + 3] = v;
    }
    int nw = 0, nh = 0;
    const auto light = sg::render::mip_down(px.data(), W, H, true, nw, nh);
    check(nw == 2 && nh == 2, "half the size");
    check(std::abs(int(light[0]) - 188) <= 1, "sRGB is averaged in light (" + std::to_string(light[0]) + ")");
    check(std::abs(int(light[3]) - 128) <= 1, "height is averaged as it is (" + std::to_string(light[3]) + ")");
    const auto plain = sg::render::mip_down(px.data(), W, H, false, nw, nh);
    check(std::abs(int(plain[0]) - 128) <= 1, "a picture of plain numbers is averaged as numbers");
    // Odd and one-wide sizes go down to one pixel.
    std::vector<unsigned char> thin(1 * 6 * 4, 9);
    const auto one = sg::render::mip_down(thin.data(), 1, 6, true, nw, nh);
    check(nw == 1 && nh == 3 && one[0] == 9, "a picture one pixel wide halves its height only");
}

void test_kept() {
    const std::string dir = (std::filesystem::temp_directory_path() / "sg_pack_cache").string();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    sg::cache::set_folder(dir);
    const auto px = painted(128, 128, 99);
    const sg::render::Packed made = sg::render::pack_kept(px.data(), 128, 128, true);
    const sg::render::Packed kept = sg::render::pack_kept(px.data(), 128, 128, true);
    check(made.bytes == kept.bytes && made.levels.size() == kept.levels.size(), "what is kept is what was made");
    check(sg::cache::stats().hits >= 1, "and it was read back, not made again");
    sg::cache::set_folder("");
    std::filesystem::remove_all(dir, ec);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--bench") {
        const int W = 1024, H = 1024;
        const auto px = painted(W, H, 5);
        const auto t0 = std::chrono::steady_clock::now();
        const sg::render::Packed p = sg::render::pack(px.data(), W, H, true);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const auto back = unpacked(p);
        const Error e = compare(px, back, 0, 0, W, H - H / 8, W);
        std::printf("packed %dx%d (and its mipmaps) in %.0f ms: %.2f Mpx/s, rmse %.3f\n", W, H, ms, W * H * 4.0 / 3.0 / ms / 1000.0, e.rmse);
        return 0;
    }
    test_blocks_round_trip();
    test_painted_picture();
    test_mipmaps();
    test_kept();
    if (failures) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("pack: all passed\n");
    return 0;
}
