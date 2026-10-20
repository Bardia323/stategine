// Stategine - a mesh's wear from its own shape (render::wear): a box's
// edges are convex and its faces' middles are not; a step's inner corner
// is concave and shut from the sky by the riser over it; a thing's foot is
// shut by the ground it stands on; made at the size it is seen.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/domains/Modeler.hpp"
#include "sg/render/Geometry.hpp"

namespace {
int failed = 0;
void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    failed += !ok;
}
// The mean of channel `ch` over the corners within `r` of `at`.
double near(const std::vector<float>& corners, const std::vector<float>& wear, double x, double y, double z, double r, int ch) {
    double s = 0;
    int n = 0;
    for (std::size_t i = 0; i < corners.size() / 8; ++i) {
        const double dx = corners[i * 8] - x, dy = corners[i * 8 + 1] - y, dz = corners[i * 8 + 2] - z;
        if (dx * dx + dy * dy + dz * dz > r * r) continue;
        s += wear[i * 4 + ch], ++n;
    }
    return n ? s / n : -1;
}
}  // namespace

int main() {
    // A step: a slab 2 x 0.5 x 2 and a riser 2 x 0.5 x 1 on its back half,
    // one surface (remeshed), finely enough to have corners everywhere - a
    // hair rough, so its flat spans are not made a few large faces.
    const auto m = sg::sculpt::build("box 2 0.5 2\nbox 2 0.5 1 at=0,0.5,-0.5\nremesh res=0.04 rough=0.0004 grain=0.03 oct=1\n");
    if (!m.errors.empty()) std::printf("%s\n", m.errors.c_str());
    std::vector<float> corners;
    for (const auto& p : m.parts) corners.insert(corners.end(), p.corners.begin(), p.corners.end());
    check(corners.size() > 8 * 300, "the step is meshed");
    const auto w = sg::render::wear(corners, 32, 0.1);
    check(w.size() == corners.size() / 2, "four numbers a corner");
    bool made = true;
    for (std::size_t i = 3; i < w.size(); i += 4) made = made && w[i] == 0.0f;
    check(made, "each corner says it was made (w 0)");
    // The slab's front top edge: convex. The middle of its top, in front of
    // the riser: flat. Where the slab's top meets the riser: concave, and
    // shut by the riser over it.
    const double edge = near(corners, w, 0.0, 0.5, 1.0, 0.08, 0), flat = near(corners, w, 0.0, 0.5, 0.5, 0.08, 0);
    check(edge > 0.2 && edge > flat * 4.0 + 0.1, "an edge is convex (" + std::to_string(edge) + " against " + std::to_string(flat) + ")");
    const double crease = near(corners, w, 0.0, 0.5, 0.0, 0.08, 1), open_crease = near(corners, w, 0.0, 0.5, 0.5, 0.08, 1);
    check(crease > 0.2 && crease > open_crease * 4.0, "an inner corner is concave (" + std::to_string(crease) + ")");
    const double shut = near(corners, w, 0.0, 0.5, 0.05, 0.08, 2), top = near(corners, w, 0.0, 1.0, -0.5, 0.08, 2);
    check(shut > top + 0.15, "an inner corner is shut from the sky, the top of the riser open (" + std::to_string(shut) + " / " + std::to_string(top) + ")");
    const double foot = near(corners, w, 0.0, 0.05, 1.0, 0.08, 2), high = near(corners, w, 0.0, 0.45, 1.0, 0.08, 2);
    check(foot > high + 0.1, "its foot is shut by the ground it stands on (" + std::to_string(foot) + " / " + std::to_string(high) + ")");
    // Made at the size it is seen: a unit box drawn 10 m wide spreads its
    // edges over the same 10 cm, so as a share of it they are narrower.
    std::vector<float> unit = corners;
    for (std::size_t i = 0; i < unit.size() / 8; ++i) unit[i * 8] /= 2.0f, unit[i * 8 + 1] /= 1.0f, unit[i * 8 + 2] /= 2.0f;
    const float size[3] = {2.0f, 1.0f, 2.0f};
    const auto ws = sg::render::wear(unit, 32, 0.1, size);
    double diff = 0;
    for (std::size_t i = 0; i < w.size(); ++i) diff = std::max(diff, std::abs(static_cast<double>(w[i] - ws[i])));
    check(diff < 0.02, "a fitted mesh at its size wears as the mesh made that size");
    std::printf("%s\n", failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}
