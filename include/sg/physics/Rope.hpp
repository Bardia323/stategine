// A rope: a cord, a cable, a string - anything long, thin and limp that hangs
// between two ends, lies over the edges of things and on the floor, and never
// passes through anything.
//
// A rope is joints joined by links of one length. Each step, in substeps when
// an end has moved far (so it is led, not yanked):
//
//   move      every joint keeps its motion (verlet: where it is less where it
//             was), damped as a cord is - the air slows all of it a little,
//             and the rope slows itself: each joint's motion is drawn towards
//             its neighbours', so a pull runs along it once and dies instead
//             of rippling back and forth - and no faster than `fastest`;
//             gravity.
//   hold      over a few passes: each joint eased towards the line through
//             its neighbours (a rope resists bending, evenly: it lies in
//             curves, it does not kink); each link held to its length, swept
//             one way then back, so a pull is felt the same from either end;
//             no joint further from either end than the rope between them
//             (so however many links, it never stretches);
//             then out of the ground - on the floor, on the top of any block
//             it came down on, out of the side of one it is in, over any lump.
//             A link cannot pass through a block either: going over an edge
//             it leaves the top at the edge, and one end over a block and the
//             other under it puts the lower out past the nearest edge.
//   lie       a joint lying on something loses most of its slide (friction).
//
// Everything is in the caller's frame (y up, metres, seconds). The rope is
// plain data: where its joints are, and were. What it hangs from and what it
// lies on are handed to each step, so it can live in any state - as elements'
// params, stepped by that state's arrow - and be run again from any moment.
//
//   rope::Ground g{floor, blocks, lumps};
//   auto r = rope::Rope::laid(a, b, 1.2, 36, g);
//   r.step(dt, a_was, a, b_was, b, g);
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "sg/core/Core.hpp"

namespace sg::rope {

// Something the rope lies on and cannot pass through: a box, turned `yaw`
// about y, its footprint `sx` x `sz` about (`x`, `z`), from `base` up to `top`.
struct Block {
    double x = 0, z = 0, yaw = 0, sx = 0, sz = 0, base = 0, top = 0;
};
// Something small lying about, as its box (along the axes): the rope goes over it.
struct Lump {
    Vec3d lo, hi;
};
struct Ground {
    double floor = 0;
    std::vector<Block> blocks;
    std::vector<Lump> lumps;
};

struct Settings {
    double gravity = 9.81;
    double air = 5.0;         // how much the air slows it, per second
    double inner = 0.7;       // how much each joint's motion is drawn to its neighbours' (0..1)
    double fastest = 4.0;     // no joint faster than this, m/s
    double bend = 0.08;       // how far each pass eases a joint towards straight (0..1)
    double friction = 0.85;   // how much of a lying joint's slide it loses a step (0..1)
    double lift = 0.003;      // how far above what it lies on a joint's middle is
    int passes = 16;
    int max_substeps = 4;
};

inline double length_of(const Vec3d& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

struct Rope {
    std::vector<Vec3d> p, o;  // the joints now, and a step ago
    double length = 1.0;

    int links() const { return static_cast<int>(p.size()) - 1; }
    double link() const { return length / std::max(1, links()); }

    // Laid from `a` to `b` at its whole length - a curve bowed out to the side
    // just enough to be that long, cut into equal links - lifted out of
    // anything it starts in, and let fall for `settle` steps.
    static Rope laid(const Vec3d& a, const Vec3d& b, double length, int links, const Ground& g, const Settings& s = {},
                     int settle = 200);

    // Where a thing on the end may be pulled to: no further from where the
    // rope is fixed than `share` of its length.
    static Vec3d within(const Vec3d& fixed, const Vec3d& t, double length, double share = 0.93);

    // Is (x, z) over a block's footprint?
    static bool inside(const Block& k, const Vec3d& q, double margin = 0.0);

    // Does any link pass through a block? (For checking.)
    bool through(const Ground& g, double margin = 0.005) const;

    // A step of `dt`, its ends going from `a0`, `b0` to `a`, `b`. How far
    // its most-moved joint went.
    double step(double dt, const Vec3d& a0, const Vec3d& a, const Vec3d& b0, const Vec3d& b, const Ground& g,
                const Settings& s = {});

private:
    // A joint on the floor and on the tops of things, not in them.
    static void settle_joint(Vec3d& q, const Vec3d& was, const Ground& g, const Settings& s);

    // Link `i` not through a block: over an edge it leaves the top at the
    // edge (the joint on it slides out to it); one end over a block and the
    // other under it, the lower is put out past its nearest edge.
    void keep_link(int i, const Ground& g);
};

}  // namespace sg::rope
