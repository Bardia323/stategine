// A rope on its own: it hangs between its ends at its length, lies on the
// floor and over a block's edge and never through it, goes over a lump, calms
// down after a jerk instead of rippling, comes to rest, and the same steps
// give the same rope.
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/physics/Rope.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}
using sg::Vec3d;
using namespace sg::rope;

double stretch(const Rope& r) {
    double worst = 0;
    for (int i = 0; i < r.links(); ++i)
        worst = std::max(worst, std::fabs(length_of(r.p[static_cast<std::size_t>(i + 1)] - r.p[static_cast<std::size_t>(i)]) - r.link()) / r.link());
    return worst;
}
double fastest(const Rope& r, double dt) {
    double v = 0;
    for (std::size_t i = 0; i < r.p.size(); ++i) v = std::max(v, length_of(r.p[i] - r.o[i]) / dt);
    return v;
}
}  // namespace

int main() {
    const double dt = 1.0 / 60.0;

    // Hanging between two posts, 1 m apart, 1.4 m long: it sags, keeps its length.
    {
        Ground g;
        g.floor = -10;
        const Vec3d a{0, 1, 0}, b{1, 1, 0};
        Rope r = Rope::laid(a, b, 1.4, 40, g);
        for (int i = 0; i < 300; ++i) r.step(dt, a, a, b, b, g);
        double low = 1e9;
        for (const Vec3d& q : r.p) low = std::min(low, q.y);
        check(low < 0.6 && low > 0.4, "hung between two ends it sags as far as its length lets it");
        check(stretch(r) < 0.03, "and its links keep their length (" + std::to_string(stretch(r)) + ")");
    }

    // Off the edge of a table to the floor: over the edge, never through the table.
    Ground table;
    table.floor = 0;
    table.blocks.push_back(Block{0.5, 0, 0, 1.0, 0.8, 0.7, 0.75});
    {
        const Vec3d a{0.2, 0.76, 0}, b{1.6, 0.0, 0.1};
        Rope r = Rope::laid(a, b, 2.0, 50, table);
        for (int i = 0; i < 300; ++i) r.step(dt, a, a, b, b, table);
        bool above_floor = true;
        for (const Vec3d& q : r.p) above_floor &= q.y >= -1e-9;
        check(above_floor, "it lies on the floor, not in it");
        check(!r.through(table), "and runs over the table's edge, not through the table");
        // Dragged about by its far end: still never through it.
        bool ever = false;
        Vec3d end = b;
        for (int i = 0; i < 240; ++i) {
            const Vec3d next = Vec3d{0.2 + 1.4 * std::cos(i * 0.05), 0.3 + 0.3 * std::sin(i * 0.11), 1.2 * std::sin(i * 0.05)};
            r.step(dt, a, a, end, next, table);
            end = next;
            ever |= r.through(table);
        }
        check(!ever, "dragged round it every way, it never passes through it");
    }

    // Over something lying on the floor.
    {
        Ground g;
        g.lumps.push_back(Lump{{0.4, 0, -0.3}, {0.6, 0.05, 0.3}});
        const Vec3d a{0, 0, 0}, b{1, 0, 0};
        Rope r = Rope::laid(a, b, 1.05, 30, g);
        for (int i = 0; i < 200; ++i) r.step(dt, a, a, b, b, g);
        bool over = true;
        for (const Vec3d& q : r.p)
            if (q.x > 0.4 && q.x < 0.6 && std::fabs(q.z) < 0.3) over &= q.y >= 0.05;
        check(over, "it goes over a thing lying in its way");
    }

    // Jerked, then held still: it calms down within a second - no ripples.
    {
        Ground g;
        g.floor = -10;
        const Vec3d a{0, 1.5, 0};
        Vec3d b{0.8, 1.5, 0};
        Rope r = Rope::laid(a, b, 1.2, 36, g);
        for (int i = 0; i < 20; ++i) {  // shaken hard, within its reach
            const Vec3d next = Vec3d{0.8, 1.5 + 0.12 * ((i % 2) ? 1 : -1), 0.1 * ((i % 4) < 2 ? 1 : -1)};
            r.step(dt, a, a, b, next, g);
            b = next;
        }
        const double jerked = fastest(r, dt);
        for (int i = 0; i < 60; ++i) r.step(dt, a, a, b, b, g);
        const double after = fastest(r, dt);
        check(jerked > 0.3 && after < jerked * 0.1, "jerked and let be, it calms within a second (" + std::to_string(jerked) + " -> " +
                                                           std::to_string(after) + " m/s)");
        for (int i = 0; i < 240; ++i) r.step(dt, a, a, b, b, g);
        check(r.step(dt, a, a, b, b, g) < 1e-4, "and comes to rest");
        check(stretch(r) < 0.03, "without stretching (" + std::to_string(stretch(r)) + ")");
    }

    // Where the far end may be pulled: no further than the rope reaches.
    {
        const Vec3d t = Rope::within({0, 0, 0}, {5, 0, 0}, 1.0);
        check(std::fabs(t.x - 0.93) < 1e-9, "a thing on the end can be pulled no further than the rope reaches");
    }

    // The same steps give the same rope.
    {
        Rope r1 = Rope::laid({0, 1, 0}, {1, 0, 0}, 1.5, 30, table), r2 = r1;
        for (int i = 0; i < 100; ++i) {
            const Vec3d b0{1 + 0.01 * i, 0, 0}, b1{1 + 0.01 * (i + 1), 0, 0};
            r1.step(dt, {0, 1, 0}, {0, 1, 0}, b0, b1, table);
            r2.step(dt, {0, 1, 0}, {0, 1, 0}, b0, b1, table);
        }
        bool same = true;
        for (std::size_t i = 0; i < r1.p.size(); ++i) same &= r1.p[i].x == r2.p[i].x && r1.p[i].y == r2.p[i].y;
        check(same, "the same steps give the same rope");
    }

    std::printf("\n%s\n", failures == 0 ? "all tests passed" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
