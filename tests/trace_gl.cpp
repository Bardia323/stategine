// Stategine - light traced along rays (src/gl/trace, docs/trace.md), against
// a real GL context.
//
// A field under a low sun, a pale wall standing in it, seen from straight
// above. A look that says `trace` 0 draws exactly as one that says nothing
// of it. Traced: the wall's shadow falls on the field - and the sun lays no
// maps for it; a wall that says `trace` 0 is not met by rays. And the tree of
// boxes rays go down: every thing in it once, inside every box over it.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "../src/gl/trace/Bvh.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}

// Every thing once in the leaves, and inside every box over it.
bool tree_holds(const sg::render::trace::Tree& t, const std::vector<sg::render::trace::Box>& boxes) {
    using sg::render::trace::kNodeFloats;
    std::vector<int> seen(boxes.size(), 0);
    struct At {
        uint32_t node;
        float lo[3], hi[3];
    };
    std::vector<At> todo{{0, {-1e30f, -1e30f, -1e30f}, {1e30f, 1e30f, 1e30f}}};
    while (!todo.empty()) {
        const At at = todo.back();
        todo.pop_back();
        for (int side = 0; side < 2; ++side) {
            const float* c = &t.nodes[static_cast<std::size_t>(at.node) * kNodeFloats + side * 8];
            if (c[0] > c[4]) continue;
            for (int k = 0; k < 3; ++k)
                if (c[k] < at.lo[k] - 1e-4f || c[4 + k] > at.hi[k] + 1e-4f) return false;
            const uint32_t ref = static_cast<uint32_t>(c[3]), n = static_cast<uint32_t>(c[7]);
            if (n == 0) {
                todo.push_back({ref, {c[0], c[1], c[2]}, {c[4], c[5], c[6]}});
                continue;
            }
            for (uint32_t i = ref; i < ref + n; ++i) {
                const auto& b = boxes[t.order[i]];
                for (int k = 0; k < 3; ++k)
                    if (b.lo[k] < c[k] - 1e-4f || b.hi[k] > c[4 + k] + 1e-4f) return false;
                ++seen[t.order[i]];
            }
        }
    }
    for (int s : seen)
        if (s != 1) return false;
    return true;
}
}  // namespace

int main() {
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(-50.0f, 50.0f), r(0.01f, 3.0f);
        std::vector<sg::render::trace::Box> boxes(5000);
        for (auto& b : boxes) {
            const float p[3] = {u(rng), u(rng), u(rng)};
            for (int k = 0; k < 3; ++k) b.lo[k] = p[k], b.hi[k] = p[k] + r(rng);
        }
        // (And many in one place: a split that cannot be by where they are.)
        for (int i = 0; i < 300; ++i) boxes.push_back(boxes.front());
        check(tree_holds(sg::render::trace::build(boxes, 4), boxes), "the tree holds every thing once, inside every box over it");
    }

    const int W = 320, H = 240;
    sg::gl::Window window(W, H, "trace");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("field");
    g.set_initial("field");
    field.params().set("sky", 1.0).set("far", 300.0);
    sg::Element& ground = field.fixture(sg::Key{"ground"}, 0.0, -1.0, 0.0);
    ground.params.set(sg::keys::sx, 200.0).set(sg::keys::sy, 1.0).set(sg::keys::sz, 200.0).set("r", 0.6).set("g", 0.6).set("b", 0.6).set("roughness", 1.0);
    // The wall: along z, three metres high; the sun low in +x, its shadow on
    // the field toward -x.
    sg::Element& wall = field.fixture(sg::Key{"wall"}, 0.0, 0.0, 5.0);
    wall.params.set(sg::keys::sx, 0.4).set(sg::keys::sy, 3.0).set(sg::keys::sz, 6.0).set("r", 0.8).set("g", 0.8).set("b", 0.8);
    sg::Element& sun = field.light(sg::Key{"sun"}, {0.0, 50.0, 0.0});
    const double up = 0.45;
    sun.params.set("sun", 1.0).set("r", 1.0).set("g", 1.0).set("b", 1.0).set(sg::keys::intensity, 1.0);
    sun.params.set("dx", -std::cos(up)).set("dy", -std::sin(up)).set("dz", 0.0);
    auto& look = g.add<sg::LookState>("look");
    look.uniform(sg::passes::scene, "uFogDensity", 0.0);
    sg::wear(g, "field", "look");
    // From straight above the field beside the wall, the picture's top +z.
    sg::Element& eye = field.camera();
    eye.params.set(sg::keys::x, -3.0).set(sg::keys::y, 9.0).set(sg::keys::z, 5.0).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, -1.5);

    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    const auto shot = [&](sg::render::GLWorldView& view, int frames) {
        for (int i = 0; i < frames; ++i) {
            view.render(field, W, H);
            window.swap();
        }
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        if (const char* dir = std::getenv("SG_TRACE_SHOTS")) {
            static int kept = 0;
            const std::string path = std::string(dir) + "/trace" + std::to_string(kept++) + ".ppm";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", W, H);
                for (int j = H - 1; j >= 0; --j) std::fwrite(&px[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
        }
        return px;
    };
    // How bright a patch is, x and y its middle as parts of the picture.
    const auto at = [&](double x, double y) {
        double sum = 0;
        int n = 0;
        const int cx = static_cast<int>(x * W), cy = static_cast<int>(y * H);
        for (int j = cy - 3; j <= cy + 3; ++j)
            for (int i = cx - 3; i <= cx + 3; ++i, ++n) {
                const unsigned char* p = &px[(static_cast<std::size_t>(j) * W + i) * 3];
                sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
            }
        return sum / n;
    };

    // A look that says `trace` 0 draws as one that says nothing of it.
    std::vector<unsigned char> plain, zero;
    {
        sg::render::GLWorldView view;
        view.prepare(g);
        plain = shot(view, 6);
    }
    look.setting(sg::passes::scene, "trace", 0.0);
    {
        sg::render::GLWorldView view;
        view.prepare(g);
        zero = shot(view, 6);
    }
    check(plain == zero, "a look that says trace 0 draws exactly as one that says nothing of it");

    // Traced. The eye is over (-3, 5); the picture's right is -x... found by
    // where the wall is: its shadow lies between it and the far side.
    look.setting(sg::passes::scene, "trace", 1.0);
    sg::render::GLWorldView view;
    view.prepare(g);
    shot(view, 24);
    // Under the eye, the middle of the wall's shadow; far up the picture,
    // past the wall's end, the open field.
    const double shade = at(0.5, 0.5), open = at(0.5, 0.92);
    std::printf("in the wall's shadow %.0f; the open field %.0f\n", shade, open);
    check(shade < 0.55 * open, "the wall's shadow falls on the field, traced");
    check(view.times().shadow_maps == 0, "and the sun lays no maps for it (" + std::to_string(view.times().shadow_maps) + ")");

    // A wall that says it is not traced is not met: no shadow of it.
    wall.params.set("trace", 0.0);
    shot(view, 3);
    const double gone = at(0.5, 0.5);
    std::printf("the wall not traced, three frames on: %.0f\n", gone);
    check(gone > 0.85 * open, "a thing that says trace 0 casts nothing rays meet - and its shadow leaves nothing behind");

    std::printf("%s\n", ok ? "all ok" : "FAILED");
    return ok ? 0 : 1;
}
