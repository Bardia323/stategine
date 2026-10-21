// Stategine - the eye's lens (README *The lens*), against a real GL context.
//
// Three white posts on a dark field before a far dark wall with small bright
// lamps on it: one post nearer than the focus, one at it, one beyond. A
// camera that says nothing of its lens - or says `fstop` 0 and `shutter` 0 -
// is drawn exactly as before. A thin lens at f/1.2 focused on the middle
// post keeps it sharp (its edges as steep as before, nothing of it bled onto
// the soft wall behind it), and softens the near and far ones by about the
// circle of confusion the thin lens says; the far lamps become discs about
// as wide as theirs. A camera moving sideways with its shutter open half
// the frame blurs edges along the motion by about how far they went in that
// time, and not at all with it shut - nor with it open and the camera still.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

const int W = 960, H = 540;

struct Picture {
    std::vector<unsigned char> px;
    double luma(int x, int y) const {
        x = std::clamp(x, 0, W - 1), y = std::clamp(y, 0, H - 1);
        const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
        return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    }
    // A lamp's warm light, not a white post's.
    bool warm(int x, int y) const {
        const unsigned char* p = &px[(static_cast<std::size_t>(y) * W + x) * 3];
        return p[0] > p[2] + 25;
    }
    // A row's luma averaged over a few rows round it (y from the top).
    std::vector<double> row(int y_top) const {
        std::vector<double> r(W, 0.0);
        for (int dy = -2; dy <= 2; ++dy)
            for (int x = 0; x < W; ++x) r[x] += luma(x, H - 1 - (y_top + dy)) / 5.0;
        return r;
    }
    double differs(const Picture& o) const {
        double most = 0;
        for (std::size_t i = 0; i < px.size(); ++i) most = std::max(most, std::abs(double(px[i]) - double(o.px[i])));
        return most;
    }
};

// Bright runs along a row: where each begins and ends.
struct Run {
    int a = 0, b = 0;
    int width() const { return b - a; }
};
std::vector<Run> runs(const std::vector<double>& r, double over) {
    std::vector<Run> out;
    for (int x = 0; x < W;) {
        if (r[x] <= over) {
            ++x;
            continue;
        }
        Run run{x, x};
        while (x < W && r[x] > over) ++x;
        run.b = x;
        out.push_back(run);
    }
    return out;
}

// How wide an edge is: from 10% to 90% of the way from the dark beside it to
// the post's middle, looking out from the post's middle at `mid` towards `way`.
double edge(const std::vector<double>& r, int mid, int way, double dark) {
    const double top = r[mid];
    int x10 = mid, x90 = mid;
    for (int x = mid; x >= 0 && x < W; x += way) {
        const double t = (r[x] - dark) / std::max(top - dark, 1e-6);
        if (t >= 0.9) x90 = x;
        if (t >= 0.1) x10 = x;
        else break;
    }
    return std::abs(x10 - x90) + 1.0;
}

// The 10%..90% width of a disc's edge, as a part of its diameter: the share
// of a disc on one side of a line, as the line crosses it.
double disc_edge() {
    const auto share = [](double x) {  // x in -1..1 (the radius 1)
        const double c = std::clamp(x, -1.0, 1.0);
        return (std::asin(c) + c * std::sqrt(1.0 - c * c)) / M_PI + 0.5;
    };
    const auto at = [&](double s) {
        double lo = -1.0, hi = 1.0;
        for (int i = 0; i < 60; ++i) (share(0.5 * (lo + hi)) < s ? lo : hi) = 0.5 * (lo + hi);
        return 0.5 * (lo + hi);
    };
    return (at(0.9) - at(0.1)) / 2.0;
}

}  // namespace

int main() {
    sg::gl::Window window(W, H, "lens");
    sg::StateGraph g;
    auto& field = g.add<sg::Spatial3D>("field");
    g.set_initial("field");
    field.params().set("far", 100.0).set("enclosed", 0.0);
    const auto box = [&](const char* id, double x, double y, double z, double sx, double sy, double sz, double grey) -> sg::Element& {
        sg::Element& e = field.fixture(sg::Key{id}, x, y, z);
        e.params.set(sg::keys::sx, sx).set(sg::keys::sy, sy).set(sg::keys::sz, sz).set("r", grey).set("g", grey).set("b", grey)
            .set("roughness", 1.0).set("surface", 0.0);
        return e;
    };
    box("ground", 20.0, -0.5, 0.0, 60.0, 1.0, 60.0, 0.03);
    box("wall", 40.0, 0.0, 0.0, 0.5, 60.0, 80.0, 0.03);
    // The posts, looked at from x = 0 along +x (+z to the right): near, at
    // the focus, beyond it.
    const double s = 3.0, near = 1.5, far = 8.0;
    box("near", near, 0.0, 0.22, 0.06, 3.0, 0.06, 0.9).params.set("emissive", 0.45);
    box("focused", s, 0.0, 0.0, 0.06, 3.0, 0.06, 0.9).params.set("emissive", 0.45);
    box("far", far, 0.0, -1.2, 0.06, 3.0, 0.06, 0.9).params.set("emissive", 0.45);
    // Small bright lamps on the far wall.
    for (int i = 0; i < 5; ++i) {
        sg::Element& lamp = box(("lamp" + std::to_string(i)).c_str(), 39.6, 2.5 + 0.6 * (i % 2), -4.0 + 2.0 * i, 0.1, 0.1, 0.1, 1.0);
        lamp.params.set("emissive", 40.0).set("r", 1.0).set("g", 0.75).set("b", 0.45);
    }
    auto& look = g.add<sg::LookState>("look");
    look.uniform(sg::passes::scene, "uFogDensity", 0.0).uniform(sg::passes::scene, "uAmbient", 0.0).setting(sg::passes::scene, "clear.x", 0.01).setting(sg::passes::scene, "clear.y", 0.01).setting(sg::passes::scene, "clear.z", 0.01);
    look.uniform(sg::passes::composite, "uGrain", 0.0).uniform(sg::passes::composite, "uVignette", 0.0).uniform(sg::passes::composite, "uBloomStrength", 0.0);
    look.fade(0.0);
    sg::wear(g, "field", "look");
    sg::Element& eye = field.camera();
    const double fov = 20.0;
    eye.params.set(sg::keys::x, 0.0).set(sg::keys::y, 1.5).set(sg::keys::z, 0.0).set(sg::keys::yaw, 0.0).set(sg::keys::pitch, 0.0).set(sg::keys::fov, fov);

    sg::render::GLWorldView view;
    const auto problems = view.prepare(g);
    for (const auto& p : problems) std::printf("  ! %s\n", p.c_str());
    check(problems.empty(), "the looks build");
    view.set_frame_delta(1.0 / 30.0);
    const char* keep = std::getenv("SG_LENS_SHOTS");
    const auto shot = [&](const char* name, int frames, double move = 0.0) {
        for (int i = 0; i < frames; ++i) {
            if (move != 0.0) eye.params.set(sg::keys::z, eye.params.num(sg::keys::z) + move);
            view.render(field, W, H);
            window.swap();
        }
        Picture p{std::vector<unsigned char>(static_cast<std::size_t>(W) * H * 3)};
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, p.px.data());
        if (keep) {
            const std::string path = std::string(keep) + "/" + name + ".ppm";
            if (FILE* f = std::fopen(path.c_str(), "wb")) {
                std::fprintf(f, "P6 %d %d 255\n", W, H);
                for (int j = H - 1; j >= 0; --j) std::fwrite(&p.px[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
                std::fclose(f);
            }
        }
        return p;
    };

    // Saying nothing, and saying its lens is a pinhole and its shutter shut,
    // are the same picture.
    const Picture plain = shot("pinhole", 3);
    eye.params.set("fstop", 0.0).set("shutter", 0.0);
    const Picture said = shot("pinhole_said", 3);
    check(plain.differs(said) == 0.0, "a camera that says nothing new is drawn exactly as before");

    // The posts in the middle row, sharp: the near one widest.
    const int row_y = H / 2;
    const std::vector<double> sharp = plain.row(row_y);
    const double dark = sharp[5];
    std::vector<Run> posts = runs(sharp, dark + 60.0);
    check(posts.size() == 3, "three posts, sharp (" + std::to_string(posts.size()) + " found)");
    if (posts.size() != 3) return 1;
    std::sort(posts.begin(), posts.end(), [](const Run& a, const Run& b) { return a.width() > b.width(); });
    const Run near_post = posts[0], focus_post = posts[1], far_post = posts[2];

    // The thin lens: f/1.2 focused on the middle post.
    const double stop = 1.2;
    eye.params.set("fstop", stop).set("focus_distance", s);
    const Picture lens = shot("lens", 3);
    // What it says each is blurred by: c = A |d - s| / d * f / (s - f), over
    // the sensor (36 mm wide, as tall as the picture makes it).
    const double sensor_h = 0.036 * H / W, f = 0.5 * sensor_h / std::tan(fov * M_PI / 360.0), A = f / stop;
    const auto circle = [&](double d) { return A * std::abs(d - s) / d * f / (s - f) / sensor_h * H; };
    const std::vector<double> soft = lens.row(row_y);
    const double soft_dark = soft[5];
    const auto edges = [&](const std::vector<double>& r, const Run& p, double dk) {
        const int mid = (p.a + p.b) / 2;
        return 0.5 * (edge(r, mid, -1, dk) + edge(r, mid, 1, dk));
    };
    const double k = disc_edge();
    const double was_focus = edges(sharp, focus_post, dark), now_focus = edges(soft, focus_post, soft_dark);
    std::printf("  focused post's edge: %.1f px, through the lens %.1f px\n", was_focus, now_focus);
    check(now_focus <= was_focus + 1.0, "the post at the focus stays sharp");
    // (Nothing of the sharp post bled onto the wall behind it, a little way off.)
    const double beside_was = 0.5 * (sharp[focus_post.a - 6] + sharp[focus_post.b + 6]);
    const double beside_now = 0.5 * (soft[focus_post.a - 6] + soft[focus_post.b + 6]);
    std::printf("  beside the focused post: %.1f, through the lens %.1f\n", beside_was, beside_now);
    check(beside_now < beside_was + 6.0, "no halo of the sharp post on the soft wall behind it");
    for (const auto& [name, post, d] : {std::tuple{"near", near_post, near}, std::tuple{"far", far_post, far}}) {
        // A post's edge through the lens is its sharp edge and a disc's, together.
        const double c = circle(d), was = edges(sharp, post, dark), now = edges(soft, post, soft_dark);
        const double expect = std::sqrt(was * was + (k * c) * (k * c));
        std::printf("  %s post: circle %.1f px; edge %.1f px sharp, %.1f px through the lens (a disc's: %.1f)\n", name, c, was, now, expect);
        check(now > 0.75 * expect && now < 1.3 * expect, std::string("the ") + name + " post is blurred by about its circle of confusion");
    }
    // The lamps far off: discs about as wide as their circle.
    {
        const double c = circle(39.55);
        // The brightest pixel of the picture's lamps, and the row through it.
        int bx = 0, by = 0;
        double best = -1;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (lens.warm(x, y) && lens.luma(x, y) > best) best = lens.luma(x, y), bx = x, by = y;
        const double edge_level = 0.5 * best;
        int a = bx, b = bx;
        while (a > 0 && lens.luma(a - 1, by) > edge_level) --a;
        while (b < W - 1 && lens.luma(b + 1, by) > edge_level) ++b;
        int t = by, u = by;
        while (t > 0 && lens.luma(bx, t - 1) > edge_level) --t;
        while (u < H - 1 && lens.luma(bx, u + 1) > edge_level) ++u;
        const double across = b - a + 1, down = u - t + 1;
        std::printf("  a far lamp: circle %.1f px; its disc %.0f x %.0f px\n", c, across, down);
        check(across > 0.75 * c && across < 1.3 * c && down > 0.75 * c && down < 1.3 * c, "a far lamp is a disc about as wide as its circle");
    }
    // Focused on the post itself (`focus`, as far off along the view as it
    // stands) is focused where it stands; saying neither, on what is in the
    // middle of the picture - the same post.
    eye.params.set("focus_distance", 0.0).set("focus", std::string("focused")).set("focus_up", 0.0);
    const Picture on_post = shot("lens_on_post", 3);
    check(on_post.differs(lens) <= 1.0, "focused on a thing: as far off as it stands");
    // (What is in the middle is the post's face, three centimetres nearer.)
    eye.params.set("focus", std::string("")).set("focus_distance", s - 0.03);
    const Picture on_face = shot("lens_on_face", 3);
    eye.params.set("focus_distance", 0.0);
    const Picture middle = shot("lens_middle", 3);
    std::printf("  focused on the middle: differs by %.0f from focused on the post's face\n", middle.differs(on_face));
    check(middle.differs(on_face) <= 2.0, "focused on nothing said: on what is in the middle of the picture");
    eye.params.set("focus_distance", s);

    // Motion: the camera goes sideways five centimetres a frame (a thirtieth
    // of a second), the shutter open half of it.
    eye.params.set("fstop", 0.0);
    const double step = 0.05;
    eye.params.set("shutter", 0.5);
    const Picture still_open = shot("still_open", 3);
    check(still_open.differs(plain) == 0.0, "the shutter open, nothing moving: the picture as it is");
    // (Each from the same place, to the same place.)
    eye.params.set(sg::keys::z, -3.0 * step).set("shutter", 0.0);
    shot("start", 1);
    const Picture moving_shut = shot("moving_shut", 3, step);
    eye.params.set(sg::keys::z, -3.0 * step).set("shutter", 0.5);
    shot("start", 1);
    const Picture moving_open = shot("moving_open", 3, step);
    // Open a sixtieth of a second, a frame a thirtieth: half the frame, the same.
    eye.params.set(sg::keys::z, -3.0 * step).set("shutter", 0.0).set("shutter_time", 1.0 / 60.0);
    shot("start", 1);
    const Picture moving_timed = shot("moving_timed", 3, step);
    check(moving_timed.differs(moving_open) == 0.0, "open a sixtieth of a second at thirty frames a second: half the frame's blur");
    // A frame told no time (a still drawn alone) is a still.
    view.set_frame_delta(0.0);
    eye.params.set(sg::keys::z, -3.0 * step);
    shot("start", 1);
    const Picture moving_still = shot("moving_untimed", 3, step);
    check(moving_still.differs(moving_shut) == 0.0, "a frame told no time: no blur");
    view.set_frame_delta(1.0 / 30.0);
    // How far each post's picture went in a frame: the step over its distance, in pixels.
    const double px_per_m = H / (2.0 * std::tan(fov * M_PI / 360.0));
    const std::vector<double> shut_row = moving_shut.row(row_y), open_row = moving_open.row(row_y);
    std::vector<Run> moved_posts = runs(shut_row, dark + 60.0);
    std::sort(moved_posts.begin(), moved_posts.end(), [](const Run& a, const Run& b) { return a.width() > b.width(); });
    // (Taken while its last frame stood a step behind: the last of each the same.)
    const double frames_apart = moving_shut.differs(moving_open);
    check(frames_apart > 0.0, "open, a moving picture is not the shut one");
    if (moved_posts.size() == 3) {
        for (const auto& [name, post, d] : {std::tuple{"near", moved_posts[0], near}, std::tuple{"focused", moved_posts[1], s}, std::tuple{"far", moved_posts[2], far}}) {
            const double went = step * px_per_m / d * 0.5;  // in the half of the frame the shutter was open
            const double was = edges(shut_row, post, dark), now = edges(open_row, post, dark);
            const double expect = std::sqrt(was * was + (0.8 * went) * (0.8 * went));
            std::printf("  %s post moving: %.1f px in the shutter's time; edge %.1f px shut, %.1f px open (expected %.1f)\n", name, went, was, now, expect);
            check(was <= 3.0, std::string("shut, the moving ") + name + " post is sharp");
            check(now > 0.75 * expect && now < 1.3 * expect, std::string("open, the moving ") + name + " post is blurred by how far it went");
        }
    } else {
        check(false, "three posts, moving");
    }
    return ok ? 0 : 1;
}
