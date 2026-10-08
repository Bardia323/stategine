// Stategine - a packed picture, read by a real card: every block of every
// level decodes on the card exactly as sg::render::bc7_decode says, so what
// the packer measures is what is seen. Pictures of every kind a map is made
// of - painted ground with grain, hard edges, flat patches, height of its own,
// pure noise - at sizes that are and are not powers of two. And a painted
// thing seen up close, lit across its relief, is the same picture worn packed
// as worn as its pixels.
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sg/gl/Renderer.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/render/Pack.hpp"
#include "sg/domains/Texture.hpp"
#include "sg/sg.hpp"

namespace {

uint32_t next(uint32_t& s) {
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return s;
}

std::vector<unsigned char> picture(int w, int h, uint32_t seed) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
            const int kind = ((x / 16) + (y / 16) * 3) % 5;
            for (int c = 0; c < 4; ++c) {
                int v = 0;
                switch (kind) {
                    case 0: v = 100 + 60 * c + static_cast<int>(next(seed) % 9) - 4 + x % 7; break;            // ground and grain
                    case 1: v = ((x + 2 * y) % 11 < 5) ? 30 + 50 * c : 220 - 40 * c; break;                    // hard edges
                    case 2: v = 77 + 33 * c; break;                                                             // flat
                    case 3: v = c == 3 ? (y * 13) % 256 : 128 + static_cast<int>(60 * std::sin(x * 0.3 + c)); break;  // height its own way
                    default: v = static_cast<int>(next(seed) & 255); break;                                    // noise
                }
                p[c] = static_cast<unsigned char>(v < 0 ? 0 : v > 255 ? 255 : v);
            }
        }
    return px;
}

// A box wearing a painted texture - grain, strokes with hard edges, height of
// its own under a grazing lamp - seen from near and far: drawn with its maps
// packed, and as their pixels, the pictures are one within a level.
int seen_the_same() {
    const int W = 480, H = 270;
    sg::Texture::define("pack.painted", [](const sg::Params&, int cell, double u, double v) {
        const double grain = std::sin(u * 517.0 + cell) * std::cos(v * 431.0) * 0.03;
        const bool stroke = std::fabs(u * 1.7 - v - 0.2) < 0.06;
        const double r = stroke ? 0.85 : 0.45 + 0.2 * std::sin(u * 9.0) + grain, g = stroke ? 0.2 : 0.4 + 0.15 * v + grain,
                     b = stroke ? 0.15 : 0.3 + 0.1 * std::cos(v * 7.0) + grain;
        const double h = stroke ? 0.9 : 0.4 + 0.3 * std::sin(u * 23.0) * std::sin(v * 19.0) + grain * 2.0;
        return std::array<double, 4>{r, g, b, h};
    });
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 20.0).set("room_d", 20.0).set("room_h", 6.0);
    g.set_initial("room");
    room.light("lamp", {10.0, 2.2, 12.5}).params.set(sg::keys::intensity, 2.0).set("outer", 1.5);
    sg::Element& box = room.mesh("box", 10.0, 0.0, 10.0);
    box.params.set(sg::keys::sx, 2.0).set(sg::keys::sy, 2.0).set(sg::keys::sz, 2.0);
    auto& paint = g.add<sg::Texture>("paint", 256);
    paint.element(sg::Texture::map_id()).params.set("generator", std::string("pack.painted")).set("relief", 0.02);
    g.set_focus(g.embed("wears.box", "room", "box", paint.id(), sg::Key{}, sg::Key{}).name, false);
    sg::Element& eye = room.camera();
    int failures = 0;
    for (const double dist : {1.6, 3.0, 8.0}) {
        eye.params.set(sg::keys::x, 10.0).set(sg::keys::y, 1.2).set(sg::keys::z, 11.0 + dist);
        eye.params.set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, -0.05).set("fov", 60.0);
        std::vector<unsigned char> pictures[2];
        for (const bool packed : {false, true}) {
            sg::render::GLQuality q;
            q.pack = packed;
            sg::render::GLWorldView view(q);
            view.set_fixed_step(1.0 / 60.0);
            view.prepare(g);
            view.bind_surface("box", &paint);
            view.warm({&room}, W, H);
            for (int i = 0; i < 3; ++i) view.render(room, W, H);
            std::vector<unsigned char>& px = pictures[packed ? 1 : 0];
            px.resize(static_cast<std::size_t>(W) * H * 3);
            sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        }
        double se = 0, cell[3][3] = {};
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                for (int c = 0; c < 3; ++c) {
                    const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 3 + c;
                    const double d = double(pictures[1][i]) - double(pictures[0][i]);
                    se += d * d;
                    cell[y * 3 / H][x * 3 / W] += d / (W / 3.0 * H / 3.0 * 3.0);
                }
        const double rmse = std::sqrt(se / (W * H * 3.0));
        double shift = 0;
        for (auto& row : cell)
            for (double m : row) shift = std::max(shift, std::fabs(m));
        std::printf("seen from %.1f m: rmse %.2f levels, largest shift of a ninth of the view %.2f levels\n", dist, rmse, shift);
        if (rmse > 1.5 || shift > 0.3) {
            std::printf("FAIL: worn packed, the box from %.1f m is another picture\n", dist);
            ++failures;
        }
    }
    return failures;
}

}  // namespace

int main() {
    sg::gl::Window window(480, 270, "pack");
    if (!sg::gl::Texture::packs()) {
        std::printf("this card takes no BPTC: nothing to check\n");
        return 0;
    }
    int failures = 0;
    long modes[9] = {};  // how many blocks of each mode were read
    for (const auto& [w, h] : std::vector<std::pair<int, int>>{{256, 256}, {192, 128}, {768, 512}, {132, 260}}) {
        for (const bool srgb : {false, true}) {
            const auto px = picture(w, h, static_cast<uint32_t>(w * 31 + h));
            const sg::render::Packed p = sg::render::pack(px.data(), w, h, srgb);
            sg::gl::Texture t;
            t.create_packed(p);
            t.bind(0);
            const sg::gl::GLenum err = sg::gl::glGetError();
            if (err != 0) {
                std::printf("FAIL: %dx%d made with GL error 0x%x\n", w, h, err);
                ++failures;
                continue;
            }
            for (std::size_t level = 0; level < p.levels.size(); ++level) {
                const auto& l = p.levels[level];
                // The card's reading of the blocks. An sRGB picture's are read
                // as the numbers stored: the blocks are what is compared.
                std::vector<unsigned char> card(static_cast<std::size_t>(l.w) * l.h * 4 + 64);
                sg::gl::glGetTexImage(sg::gl::GL_TEXTURE_2D, static_cast<sg::gl::GLint>(level), sg::gl::GL_RGBA, sg::gl::GL_UNSIGNED_BYTE, card.data());
                const int bw = (l.w + 3) / 4, bh = (l.h + 3) / 4;
                long wrong = 0;
                int most = 0;
                for (int by = 0; by < bh; ++by)
                    for (int bx = 0; bx < bw; ++bx) {
                        unsigned char ours[64];
                        const auto* block = reinterpret_cast<const unsigned char*>(p.bytes.data() + l.at + (static_cast<std::size_t>(by) * bw + bx) * 16);
                        int mode = 0;
                        while (mode < 8 && !((block[0] >> mode) & 1)) ++mode;
                        ++modes[mode];
                        sg::render::bc7_decode(block, ours);
                        for (int y = 0; y < 4; ++y)
                            for (int x = 0; x < 4; ++x) {
                                const int sx = bx * 4 + x, sy = by * 4 + y;
                                if (sx >= l.w || sy >= l.h) continue;
                                for (int c = 0; c < 4; ++c) {
                                    const int d = std::abs(int(ours[(y * 4 + x) * 4 + c]) - int(card[(static_cast<std::size_t>(sy) * l.w + sx) * 4 + c]));
                                    if (d) ++wrong, most = std::max(most, d);
                                }
                            }
                    }
                if (wrong) {
                    std::printf("FAIL: %dx%d%s level %zu: %ld values the card reads otherwise (by up to %d)\n", w, h, srgb ? " sRGB" : "", level, wrong, most);
                    ++failures;
                }
            }
        }
    }
    // Every mode the packer writes was among them.
    for (const int m : {5, 6, 7})
        if (modes[m] == 0) {
            std::printf("FAIL: no block of mode %d was read\n", m);
            ++failures;
        }
    failures += seen_the_same();
    std::printf("blocks read: mode 5 %ld, mode 6 %ld, mode 7 %ld\n", modes[5], modes[6], modes[7]);
    if (failures) return 1;
    std::printf("pack on the card: every block read as decoded\n");
    return 0;
}
