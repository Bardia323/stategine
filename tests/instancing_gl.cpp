// Stategine - things of one shape drawn in one call, against a real GL context.
//
// A room of thousands of boxes and cylinders, of many sizes, colours and
// materials, under a lamp: drawn in batches (one call a shape) and one call a
// thing, the pictures are the same, and the batches take a handful of calls.
// Prints how long a frame of each takes.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace {

std::vector<unsigned char> pixels(int w, int h) {
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3);
    sg::gl::glReadPixels(0, 0, w, h, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
    return px;
}

}  // namespace

int main() {
    const int W = 480, H = 270;
    sg::gl::Window window(W, H, "instancing");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    g.set_initial("room");
    room.light("lamp", {7, 3.6, 6}).params.set(sg::keys::intensity, 1.4).set("outer", 1.5);
    // A field of things: 60 x 60, every one different.
    int n = 0;
    for (int i = 0; i < 60; ++i)
        for (int j = 0; j < 60; ++j, ++n) {
            const double x = 0.5 + i * 0.22, z = 0.5 + j * 0.19;
            sg::Element& e = room.fixture(sg::Key{"t" + std::to_string(n)}, x, 0.0, z);
            const double k = std::sin(n * 12.9898) * 43758.5453, f = k - std::floor(k);
            e.params.set(sg::keys::sx, 0.08 + 0.1 * f).set(sg::keys::sy, 0.1 + 0.6 * f * f).set(sg::keys::sz, 0.1)
                .set(sg::keys::yaw, f * 6.28).set(sg::keys::r, f).set(sg::keys::g, 1 - f).set(sg::keys::b, 0.5)
                .set("roughness", 0.3 + 0.6 * f).set("surface", static_cast<double>(n % 8)).set("mirror", n % 11 == 0 ? 0.4 : 0.0);
            if (n % 5 == 0) e.params.set("shape", std::string("cylinder"));
        }
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 7.0).set(sg::keys::y, 3.2).set(sg::keys::z, 13.5).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, -0.5);

    int culled = 0;
    const auto run = [&](bool instancing, bool merged, double& ms, int& draws, int& batched, bool on_card = false) {
        sg::render::GLQuality q;
        q.instancing = instancing;
        q.proxies = merged;
        q.gpu_cull = on_card;
        sg::render::GLWorldView view(q);
        view.set_fixed_step(1.0 / 60.0);
        view.prepare(g);
        for (int i = 0; i < 5; ++i) view.render(room, W, H);  // warm
        sg::gl::glFinish();
        const auto t0 = std::chrono::steady_clock::now();
        const int frames = 60;
        for (int i = 0; i < frames; ++i) view.render(room, W, H);
        sg::gl::glFinish();
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
        draws = view.times().draws, batched = view.times().instanced + view.times().proxied;
        view.set_timing(true);
        view.render(room, W, H);
        culled = view.times().culled_on_card;
        const auto& t = view.times();
        std::printf("  %s: shadows %.2f  scene %.2f (cpu %.2f)  post %.2f ms\n", !instancing ? "one call each" : merged ? "merged" : "batched", t.shadows,
                    t.scene, t.scene_cpu, t.post);
        view.set_timing(false);
        return pixels(W, H);
    };

    double ms_one = 0, ms_batch = 0, ms_merged = 0;
    int draws_one = 0, draws_batch = 0, draws_merged = 0, b0 = 0, batched = 0, merged_things = 0;
    const auto one = run(false, false, ms_one, draws_one, b0);
    const auto batch = run(true, false, ms_batch, draws_batch, batched);
    // And merged, a room's cells each a mesh (Quality::proxies): the same picture.
    const auto merged = run(true, true, ms_merged, draws_merged, merged_things);

    double diff = 0;
    int worst = 0;
    for (std::size_t i = 0; i < one.size(); ++i) {
        const int d = std::abs(static_cast<int>(one[i]) - static_cast<int>(batch[i]));
        diff += d;
        worst = std::max(worst, d);
    }
    diff /= static_cast<double>(one.size());
    std::printf("%d things: one call each %.2f ms/frame (%d calls), batched %.2f ms/frame (%d calls, %d batched)\n", n, ms_one,
                draws_one, ms_batch, draws_batch, batched);
    std::printf("pictures differ by %.3f on average, %d at most\n", diff, worst);

    bool ok = true;
    const auto check = [&](bool c, const char* what) {
        std::printf("[%s] %s\n", c ? "ok" : "FAIL", what);
        ok = ok && c;
    };
    check(diff < 0.25 && worst < 24, "batched, the picture is the one drawn a call a thing");
    check(draws_batch < 20 && batched > 1000, "and it takes a call a shape, not a call a thing");
    double merged_diff = 0;
    int merged_worst = 0;
    for (std::size_t i = 0; i < one.size(); ++i) {
        const int d = std::abs(static_cast<int>(one[i]) - static_cast<int>(merged[i]));
        merged_diff += d, merged_worst = std::max(merged_worst, d);
    }
    merged_diff /= static_cast<double>(one.size());
    std::printf("merged: %.2f ms/frame (%d calls, %d merged); differs by %.3f on average, %d at most\n", ms_merged, draws_merged, merged_things,
                merged_diff, merged_worst);
    check(merged_diff < 0.25 && merged_worst < 24, "merged a cell at a time, the picture is the same");
    check(draws_merged <= draws_batch && merged_things > 1000, "and it takes no more calls than batched");
    // And merged, its things culled by the card itself where it can (GL 4.3).
    double ms_card = 0;
    int draws_card = 0, card_things = 0;
    const auto on_card = run(true, true, ms_card, draws_card, card_things, true);
    double card_diff = 0;
    int card_worst = 0;
    for (std::size_t i = 0; i < one.size(); ++i) {
        const int d = std::abs(static_cast<int>(one[i]) - static_cast<int>(on_card[i]));
        card_diff += d, card_worst = std::max(card_worst, d);
    }
    card_diff /= static_cast<double>(one.size());
    std::printf("culled on the card: %.2f ms/frame (%d calls, %d culled); differs by %.3f on average, %d at most\n", ms_card, draws_card, culled,
                card_diff, card_worst);
    check(card_diff < 0.25 && card_worst < 24, "culled on the card, the picture is the same");
    // A thing in a merged cell moved: drawn where it is now, on its own, the
    // frame it moves - the picture of it all drawn a call a thing.
    {
        sg::render::GLQuality q;
        q.proxies = true;
        sg::render::GLWorldView view(q);
        view.prepare(g);
        for (int i = 0; i < 10; ++i) view.render(room, W, H);
        sg::Element& moved = *room.find(sg::Key{"t1830"});
        moved.params.set(sg::keys::x, moved.params.num(sg::keys::x) + 0.9).set(sg::keys::sy, 1.4);
        view.render(room, W, H);
        const int merged_now = view.times().proxied;
        const auto after = pixels(W, H);
        sg::render::GLQuality q1;
        q1.instancing = false;
        sg::render::GLWorldView one_by_one(q1);
        one_by_one.prepare(g);
        for (int i = 0; i < 5; ++i) one_by_one.render(room, W, H);
        const auto truth = pixels(W, H);
        int most = 0;
        double mean = 0;
        for (std::size_t i = 0; i < truth.size(); ++i) {
            const int d = std::abs(static_cast<int>(truth[i]) - static_cast<int>(after[i]));
            mean += d, most = std::max(most, d);
        }
        mean /= static_cast<double>(truth.size());
        std::printf("moved: %d merged, differs by %.3f on average, %d at most\n", merged_now, mean, most);
        check(mean < 0.25 && most < 24 && merged_now > 0, "a thing moved in a merged cell is drawn where it is now, the frame it moves (its cell drawn as batched until merged again)");
    }
    return ok ? 0 : 1;
}
