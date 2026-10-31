// Stategine - pictures sent by their levels, against a real GL context
// (gl::Texture::create_packed's `streamed`, Quality::stream_textures).
//
// A picture packed for the card, sent streamed: only its levels of 256
// pixels or less at first, each finer one as it is drawn with, no more a
// frame than the frame may send - and, all sent, it is the picture sent
// whole. A box wearing one, drawn with its pictures streamed, comes in a few
// frames to the picture it has drawn without.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/render/Pack.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
std::vector<unsigned char> pattern(int w, int h) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
            p[0] = static_cast<unsigned char>(128 + 120 * std::sin(x * 0.05)), p[1] = static_cast<unsigned char>((x ^ y) & 0xff);
            p[2] = static_cast<unsigned char>(128 + 120 * std::cos(y * 0.031)), p[3] = 255;
        }
    return px;
}
}  // namespace

int main() {
    const int W = 480, H = 270;
    sg::gl::Window window(W, H, "stream");
    if (!sg::gl::Texture::packs()) {
        std::printf("[ok] this card takes no packed pictures: nothing to stream\n");
        return 0;
    }
    // The picture itself, streamed level by level.
    const int N = 1024;
    const std::vector<unsigned char> rgba = pattern(N, N);
    const sg::render::Packed packed = sg::render::pack(rgba.data(), N, N, false);
    sg::gl::Texture t;
    t.create_packed(packed, true);
    check(t.finest() == 2, "made streamed: its levels of 256 pixels or less sent (" + std::to_string(t.finest()) + " the finest)");
    sg::gl::Texture::stream_budget = 0;
    t.bind(0);
    check(t.finest() == 2, "drawn with when the frame may send nothing: as it was");
    sg::gl::Texture::stream_budget = packed.levels[1].size;
    t.bind(0);
    check(t.finest() == 1, "drawn with when the frame may send one level: that one");
    sg::gl::Texture::stream_budget = std::size_t{64} << 20;
    t.bind(0);
    check(t.finest() == 0, "and the next frame, the rest");

    // Held no finer than 512 (Quality::texture_most): its finest level never sent, streamed or not.
    {
        sg::gl::Texture held;
        held.create_packed(packed, false, 512);
        check(held.finest() == 1 && held.finest_held() == 1, "held no finer than 512: from its second level (" + std::to_string(held.finest()) + ")");
        sg::gl::Texture both;
        both.create_packed(packed, true, 512);
        sg::gl::Texture::stream_budget = std::size_t{64} << 20;
        both.bind(0);
        check(both.finest() == 1, "streamed and held no finer than 512: drawn with, it comes to 512 and no further (" + std::to_string(both.finest()) + ")");
    }
    // A box wearing one: streamed, it comes to the picture it has whole.
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    g.set_initial("room");
    room.params().set("room_w", 8.0).set("room_d", 8.0);
    room.light("lamp", {4, 2.8, 4}).params.set(sg::keys::intensity, 1.2);
    room.picture(sg::Key{"photo"}, N, N, rgba);
    sg::Element& box = room.fixture(sg::Key{"box"}, 4.0, 0.0, 3.0);
    box.params.set(sg::keys::sx, 2.4).set(sg::keys::sy, 2.4).set(sg::keys::sz, 0.2).set("skin", std::string("photo")).set("tile", 2.4);
    room.camera().params.set(sg::keys::x, 4.0).set(sg::keys::y, 1.2).set(sg::keys::z, 6.5).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, 0.0);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    const auto picture = [&](bool streamed, int frames) {
        sg::render::GLQuality q;
        q.stream_textures = streamed;
        sg::render::GLWorldView view(q);
        view.prepare(g);
        for (int i = 0; i < frames; ++i) view.render(room, W, H), window.swap();
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    const std::vector<unsigned char> whole = picture(false, 12);
    const std::vector<unsigned char> streamed = picture(true, 12);
    int most = 0;
    double mean = 0;
    for (std::size_t i = 0; i < px.size(); ++i) {
        const int d = std::abs(int(whole[i]) - int(streamed[i]));
        most = std::max(most, d), mean += d;
    }
    mean /= static_cast<double>(px.size());
    check(mean < 0.25 && most < 24, "a box wearing one, streamed, comes to the picture it has whole (" + std::to_string(mean) + " levels a pixel apart, " +
                                         std::to_string(most) + " at most)");
    return ok ? 0 : 1;
}
