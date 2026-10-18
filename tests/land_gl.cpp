// Stategine - land as it is worn and as it goes, against a real GL context.
//
// A terrain that wears a photographed ground (`skin`, its normal and surface
// maps) is that picture laid over it, not the land's own colour - and laid
// twice (`tile_break`), its repeats broken; one that says where its water
// stands (`wet_level`, `wet_band`) is darker by the waterline than up the
// bank; and one that drifts with its world's time (`drifts`: its ground a
// function of that time, given again) is drawn as it now is a few frames on,
// never waiting for it - and as it was until then. The ground round an eye
// that moves on is sampled at the same points of the world, as high.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
struct Rgb {
    double r = 0, g = 0, b = 0;
    double sum() const { return r + g + b; }
};
}  // namespace

int main() {
    const int W = 320, H = 180;
    sg::gl::Window window(W, H, "land");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("field");
    g.set_initial("field");
    field.params().set("sky", 1.0).set("far", 300.0);
    // A gentle slope down to the south (-z), the eye over it looking down.
    double lift = 0.0;
    field.terrain(sg::Key{"land"}, [](double, double z) { return 0.05 * z; });
    const auto el = [&](const char* id) -> sg::Params& { return field.find(sg::Key{id})->params; };
    el("land").set("r", 0.6).set("g", 0.5).set("b", 0.4).set("surface", 0.0).set("cells", 80.0).set("reach", 120.0);
    field.light(sg::Key{"sun"}, {0, 50, 0});
    el("sun").set("sun", 1.0).set(sg::keys::intensity, 0.08).set("dx", 0.3).set("dy", -1.0).set("dz", 0.2);
    sg::Element& eye = field.camera();
    eye.params.set(sg::keys::x, 0.0).set(sg::keys::y, 12.0).set(sg::keys::z, 0.0).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, -1.2);

    // A picture: red and blue stripes, a metre each, across x.
    std::vector<unsigned char> stripes(64 * 64 * 4);
    for (int j = 0; j < 64; ++j)
        for (int i = 0; i < 64; ++i) {
            unsigned char* p = &stripes[(j * 64 + i) * 4];
            const bool red = (i / 32) == 0;
            p[0] = red ? 230 : 20, p[1] = 30, p[2] = red ? 20 : 230, p[3] = 255;
        }
    field.picture(sg::Key{"stripes"}, 64, 64, stripes);

    sg::render::GLWorldView view;
    view.prepare(g);
    view.set_fixed_step(1.0 / 60);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    const auto shot = [&](int frames) {
        for (int i = 0; i < frames; ++i) view.render(field, W, H), window.swap();
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
    };
    const auto at = [&](double x, double y) {
        Rgb c;
        int n = 0;
        for (int j = static_cast<int>(y * H) - 3; j <= static_cast<int>(y * H) + 3; ++j)
            for (int i = static_cast<int>(x * W) - 3; i <= static_cast<int>(x * W) + 3; ++i) {
                const unsigned char* p = &px[(static_cast<std::size_t>(j) * W + i) * 3];
                c.r += p[0], c.g += p[1], c.b += p[2], ++n;
            }
        c.r /= n, c.g /= n, c.b /= n;
        return c;
    };

    shot(8);
    const Rgb plain = at(0.5, 0.5);
    // Worn, laid every 2 m: red and blue across the screen, not its own tan.
    el("land").set("skin", std::string("stripes")).set("tile", 2.0).set("tile_break", 0.0);
    shot(8);
    double redness = 0.0, blueness = 0.0;
    for (int k = 0; k < 40; ++k) {
        const Rgb c = at(0.1 + 0.02 * k, 0.5);
        redness = std::max(redness, c.r - c.b), blueness = std::max(blueness, c.b - c.r);
    }
    std::printf("plain %.0f %.0f %.0f; worn: most red over blue %.0f, blue over red %.0f\n", plain.r, plain.g, plain.b, redness, blueness);
    check(redness > 60.0 && blueness > 60.0, "a land that wears a picture is that picture laid over it");
    // Its repeats broken by a second laying: the stripes no longer every 2 m alike.
    const std::vector<unsigned char> once = px;
    el("land").set("tile_break", 1.0);
    shot(8);
    double moved = 0.0;
    for (std::size_t i = 0; i < px.size(); ++i) moved += std::abs(static_cast<int>(px[i]) - static_cast<int>(once[i]));
    moved /= static_cast<double>(px.size());
    check(moved > 5.0, "laid twice, its repeats broken (" + std::to_string(moved) + " levels a pixel moved)");
    field.find(sg::Key{"land"})->params.erase(sg::Key{"skin"});

    // Wet by its water: the bank's foot darker than up it. The eye looks
    // down the slope; its water stands at the height the middle of the
    // picture's ground is.
    shot(8);
    const Rgb dry = at(0.5, 0.5);
    el("land").set("wet_level", 0.05 * 9.0).set("wet_band", 0.6);
    shot(8);
    const Rgb wet = at(0.5, 0.5);
    std::printf("the ground at the waterline: dry %.0f, wet %.0f\n", dry.sum(), wet.sum());
    check(wet.sum() < 0.92 * dry.sum(), "damp by its water: darker at the waterline (the levels near white, toned: less apart than the light)");
    el("land").set("wet_band", 0.0);

    // Drifting: given again as its world's time goes (a 3 m rise), drawn as
    // it was the frame it is given - never waited for - and as it now is a
    // few frames on.
    el("land").set("drifts", 1.0).set("skin", std::string("stripes")).set("tile_break", 0.0);
    shot(8);
    const std::vector<unsigned char> before = px;
    lift = 3.0;
    const auto t0 = std::chrono::steady_clock::now();
    field.terrain(sg::Key{"land"}, [lift](double, double z) { return 0.05 * z + lift; });
    shot(1);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    double same = 0.0;
    for (std::size_t i = 0; i < px.size(); ++i) same += std::abs(static_cast<int>(px[i]) - static_cast<int>(before[i]));
    same /= static_cast<double>(px.size());
    shot(30);
    double after = 0.0;
    for (std::size_t i = 0; i < px.size(); ++i) after += std::abs(static_cast<int>(px[i]) - static_cast<int>(before[i]));
    after /= static_cast<double>(px.size());
    std::printf("drifting: the frame it is given %.2f ms, %.2f levels apart; thirty on, %.2f\n", ms, same, after);
    check(after > same + 1.0, "a land that drifts is drawn as it now is, a few frames on");

    // The lens: a camera that keeps a thing sharp (`focus`) softens what is
    // far from it on the screen - the stripes at the frame's edge blur, the
    // ones round the thing do not.
    el("land").set("tile", 0.6);
    sg::Element& marker = field.fixture(sg::Key{"marker"}, 0.0, 3.2, 4.4);
    marker.params.set(sg::keys::sx, 0.05).set(sg::keys::sy, 0.05).set(sg::keys::sz, 0.05);
    const auto contrast = [&](double x, double y) {
        int lo = 255, hi = 0;
        for (int j = static_cast<int>(y * H) - 6; j <= static_cast<int>(y * H) + 6; ++j)
            for (int i = static_cast<int>(x * W) - 12; i <= static_cast<int>(x * W) + 12; ++i) {
                const unsigned char* p = &px[(static_cast<std::size_t>(j) * W + i) * 3];
                lo = std::min(lo, static_cast<int>(p[0])), hi = std::max(hi, static_cast<int>(p[0]));
            }
        return hi - lo;
    };
    shot(8);
    const int sharp_middle = contrast(0.5, 0.5), sharp_edge = contrast(0.12, 0.2);
    field.camera().params.set("focus", std::string("marker")).set("focus_up", 0.0).set("focus_radius", 0.08).set("focus_fall", 0.2).set("focus_blur", 4.0);
    shot(8);
    const int soft_middle = contrast(0.5, 0.5), soft_edge = contrast(0.12, 0.2);
    std::printf("the stripes' contrast: sharp %d middle, %d edge; focused %d middle, %d edge\n", sharp_middle, sharp_edge, soft_middle, soft_edge);
    check(soft_middle > 0.85 * sharp_middle && soft_edge < 0.6 * sharp_edge, "a lens keeps what it is on sharp, and softens what is far from it");

    // The ground sampled round an eye, and again round it a few metres on:
    // the points of the second that the first had too are the same points of
    // the world, as high - the far ground does not slide under its samples as
    // the eye goes (dunes do not wobble); it is about as many cells as asked
    // for. And the ground drawn changes as smoothly as the eye moves: walked
    // a quarter metre at a time, no place of it rises or falls more than a
    // little a step - not where its spacing doubles, points coming and going
    // there (slid onto the coarser lattice first), as it did by a third of a
    // metre and more.
    {
        const auto dunes = [](double x, double z) { return 20.0 * std::sin(x * 0.04) * std::cos(z * 0.031) + 3.0 * std::sin(x * 0.3 + z * 0.2); };
        const std::vector<float> a = sg::render::GLWorldView::sample_ground(dunes, 360, 2200.0, 0.35, 100.0, 40.0);
        const std::vector<float> b = sg::render::GLWorldView::sample_ground(dunes, 360, 2200.0, 0.35, 104.0, 36.0);
        std::map<std::pair<long long, long long>, float> first;
        for (std::size_t i = 0; i + 8 <= a.size(); i += 8) first[{std::llround(a[i] * 1000.0), std::llround(a[i + 2] * 1000.0)}] = a[i + 1];
        std::size_t far_shared = 0, far_moved = 0;
        for (std::size_t i = 0; i + 8 <= b.size(); i += 8) {
            const auto it = first.find({std::llround(b[i] * 1000.0), std::llround(b[i + 2] * 1000.0)});
            if (it == first.end() || std::hypot(b[i] - 104.0, b[i + 2] - 36.0) < 500.0) continue;
            ++far_shared;
            if (std::fabs(it->second - b[i + 1]) > 1e-3f) ++far_moved;
        }
        const double cells = std::sqrt(static_cast<double>(a.size() / 48));
        std::printf("the ground round an eye: as many cells as %.0f a side; moved on, %zu far corners shared, %zu of them moved\n", cells, far_shared, far_moved);
        check(far_shared > 1000 && far_moved < far_shared / 20, "the ground round a moving eye: its points stay where they are in the world, as high");
        check(cells > 200.0 && cells < 450.0, "the ground round an eye: about as many cells as asked");

        // (The height of the ground as drawn - its triangles as they are - at
        // a place: the triangles over it found in a grid of 4 m bins.)
        struct Drawn {
            std::map<std::pair<int, int>, std::vector<std::size_t>> bins;
            std::vector<float> v;
        };
        const auto drawn = [](std::vector<float> v) {
            Drawn d;
            d.v = std::move(v);
            for (std::size_t t = 0; t + 24 <= d.v.size(); t += 24) {
                const float* p = &d.v[t];
                const float x0 = std::min({p[0], p[8], p[16]}), x1 = std::max({p[0], p[8], p[16]});
                const float z0 = std::min({p[2], p[10], p[18]}), z1 = std::max({p[2], p[10], p[18]});
                if (std::hypot(0.5 * (x0 + x1) - 100.0, 0.5 * (z0 + z1) - 40.0) > 330.0) continue;
                for (int bx = static_cast<int>(std::floor(x0 / 4.0)); bx <= static_cast<int>(std::floor(x1 / 4.0)); ++bx)
                    for (int bz = static_cast<int>(std::floor(z0 / 4.0)); bz <= static_cast<int>(std::floor(z1 / 4.0)); ++bz) d.bins[{bx, bz}].push_back(t);
            }
            return d;
        };
        const auto height_at = [](const Drawn& d, double x, double z) {
            const auto it = d.bins.find({static_cast<int>(std::floor(x / 4.0)), static_cast<int>(std::floor(z / 4.0))});
            if (it == d.bins.end()) return std::nan("");
            for (std::size_t t : it->second) {
                const float* p = &d.v[t];
                const double ax = p[0], az = p[2], bx = p[8], bz = p[10], cx = p[16], cz = p[18];
                const double det = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
                if (std::fabs(det) < 1e-9) continue;  // (folded flat)
                const double u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / det, w = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / det;
                if (u < -1e-9 || w < -1e-9 || u + w > 1.0 + 1e-9) continue;
                return u * p[1] + w * p[9] + (1.0 - u - w) * p[17];
            }
            return std::nan("");
        };
        // (Places from 5 to 300 m round the walk, where the spacings double.)
        std::vector<std::pair<double, double>> places;
        for (int k = 0; k < 4000; ++k) {
            const double r = 5.0 + 295.0 * std::fmod(k * 0.618034, 1.0), t = k * 2.39996;
            places.push_back({100.0 + r * std::cos(t), 40.0 + r * std::sin(t)});
        }
        Drawn last = drawn(sg::render::GLWorldView::sample_ground(dunes, 360, 2200.0, 0.35, 100.0, 40.0, 0.25));
        double worst = 0.0, worst_far = 0.0;
        int holes = 0;
        for (int step = 1; step <= 40; ++step) {
            const double ex = 100.0 + 0.25 * step, ez = 40.0 + 0.1 * step;
            Drawn now = drawn(sg::render::GLWorldView::sample_ground(dunes, 360, 2200.0, 0.35, ex, ez, 0.25));
            for (const auto& [x, z] : places) {
                const double h0 = height_at(last, x, z), h1 = height_at(now, x, z);
                if (std::isnan(h0) || std::isnan(h1)) {
                    ++holes;
                    continue;
                }
                if (std::fabs(h1 - h0) > worst) worst = std::fabs(h1 - h0), worst_far = std::hypot(x - ex, z - ez);
            }
            last = std::move(now);
        }
        std::printf("the ground drawn, walked ten metres a quarter at a time: %.3f m the most any place moved in a step (%.1f m off); %d places not on it\n", worst, worst_far,
                    holes);
        check(holes == 0, "the ground round a moving eye: no gap in it");
        check(worst < 0.03, "the ground round a moving eye changes as smoothly as it moves: no place pops");
    }

    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
