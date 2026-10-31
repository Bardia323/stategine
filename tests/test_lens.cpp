// A lens: the glass a camera sees through, kept as data and mounted by the
// camera's own arrow (sg/domains/Lens.hpp).
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/domains/Camera.hpp"
#include "sg/domains/Lens.hpp"
#include "sg/domains/Spatial.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

}  // namespace

int main() {
    // The engine's own, by name.
    for (const std::string& n : sg::lens_names()) check(sg::lens_named(n).has_value(), "the engine's lens " + n + " reads");
    const auto tele = sg::lens_named("tele135");
    const auto cam = sg::lens_named("camcorder");
    check(tele && near(tele->focal, 135.0) && near(tele->barrel, 0.0) && near(tele->fstop, 2.8), "the long lens: 135 mm, f/2.8, nothing bowed");
    check(cam && cam->barrel > 0.1 && near(cam->fstop, 0.0), "the camcorder's: bowed, all sharp");

    // A focal length's view: a 50 on a full frame held level is about 27 degrees tall.
    check(near(sg::lens_fov(50.0), 26.991, 1e-3), "50 mm over 24 mm of film: 27 degrees");
    check(near(sg::lens_fov(113.6, 36.0), 18.0, 0.01), "113.6 mm over a frame held upright: 18 degrees");

    // A project's, as a file; and what it cannot read is said.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sg_lens_test";
    std::filesystem::create_directories(dir / "lenses");
    std::ofstream(dir / "lenses" / "petzval.lens") << "# a swirly old portrait lens\nfocal = 85\nfstop = 2.2\nvignette = 0.6\nwobble = 3\n";
    std::vector<std::string> problems;
    const auto pz = sg::lens_named("petzval", (dir / "program").string(), &problems);
    check(pz && pz->name == "petzval" && near(pz->vignette, 0.6), "a project's lens read from its lenses folder");
    check(problems.size() == 1, "a key no lens has is said, the rest read");
    problems.clear();
    check(!sg::lens_named("no_such_lens", {}, &problems) && !problems.empty(), "a lens that is not there: none, and said");
    std::filesystem::remove_all(dir);

    // A camera mounts one by its own arrow; another in its place leaves
    // nothing of the first behind.
    {
        sg::StateGraph g;
        auto& c = g.add<sg::Camera>(sg::Key{"cam"});
        sg::Engine engine(g);
        engine.start(sg::Key{"cam"});
        engine.fire(sg::mount_event(sg::Camera::mount_event(), *tele));
        engine.tick(0.0);
        const sg::Params& p = c.lens().params;
        check(p.is(sg::Key{"lens"}, "tele135") && near(p.num(sg::keys::fov), sg::lens_fov(135.0)), "mounted: its name and its view");
        check(near(p.num(sg::Key{"fstop"}), 2.8), "mounted: its aperture");
        engine.fire(sg::mount_event(sg::Camera::mount_event(), *cam));
        engine.tick(0.0);
        check(p.is(sg::Key{"lens"}, "camcorder") && near(p.num(sg::Key{"barrel"}), 0.14), "another mounted in its place");
        check(!p.has(sg::Key{"fstop"}), "the last lens's aperture taken off with it");
        const sg::LawReport r = sg::verify(g);
        check(r.ok(), "the camera's laws hold with its mount");
    }

    // A world's own eye mounts one too.
    {
        sg::StateGraph g;
        auto& w = g.add<sg::Spatial3D>(sg::Key{"room"});
        sg::Engine engine(g);
        engine.start(sg::Key{"room"});
        engine.fire(sg::mount_event(sg::Key{"space3.mount"}, *tele, 36.0));
        engine.tick(0.0);
        check(w.camera().params.is(sg::Key{"lens"}, "tele135") && near(w.camera().params.num(sg::keys::fov), sg::lens_fov(135.0, 36.0)),
              "a room's eye mounts a lens, its view over a frame held upright");
    }

    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
