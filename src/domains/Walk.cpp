#include "sg/domains/Walk.hpp"

#include <algorithm>
#include <cmath>

#include "sg/spatial/Math.hpp"

namespace sg {

namespace {
using spatial::M3;
using spatial::V3;
V3 v3(const Vec3d& v) { return {v.x, v.y, v.z}; }
Vec3d vd(const V3& v) { return {v.x, v.y, v.z}; }
spatial::Transform transform_of(const Pose& p) {
    return spatial::Transform{spatial::from_euler(p.yaw, p.pitch, p.roll), v3(p.position)};
}

// What stops a walker: a box (turned any way) or a ball.
struct Solid {
    Key id;
    V3 centre;
    M3 turn;
    V3 half;
    bool ball;
    double reach;  // how far its furthest point is from its middle
};

std::vector<Solid> solids_of(const State& s, const Vec3d& near = {}, double reach = 0) {
    std::vector<Solid> out;
    const std::vector<Vec3d> copies = images(s, near, reach);
    for (const Element& e : s.elements()) {
        if (!e.alive) continue;
        if (e.kind != kinds::wall && !(e.kind == kinds::mesh && e.params.num("solid", 0.0) > 0.5)) continue;
        const Pose w = world_pose(s, e);
        const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
        Solid d;
        d.id = e.id;
        d.centre = v3(place_in(w, {0.0, sy * 0.5, 0.0}));
        d.turn = spatial::from_euler(w.yaw, w.pitch, w.roll);
        d.half = {sx * 0.5, sy * 0.5, sz * 0.5};
        d.ball = e.params.get_or<std::string>("shape", "") == "sphere";
        d.reach = spatial::length(d.half);
        for (const Vec3d& o : copies) {
            Solid c = d;
            c.centre = d.centre + v3(o);
            out.push_back(c);
        }
    }
    return out;
}

// Out of a solid: the way, and how far, a ball at `p` of radius `r` must go.
bool push_out(const Solid& d, V3 p, double r, V3& normal, double& depth) {
    const V3 rel = p - d.centre;
    if (spatial::length(rel) > d.reach + r) return false;
    if (d.ball) {
        const double l = spatial::length(rel), R = d.half.x;
        if (l >= R + r) return false;
        normal = l > 1e-12 ? rel * (1.0 / l) : V3{0, 1, 0};
        depth = R + r - l;
        return true;
    }
    const V3 q = spatial::transpose(d.turn) * rel;
    const V3 c{std::clamp(q.x, -d.half.x, d.half.x), std::clamp(q.y, -d.half.y, d.half.y), std::clamp(q.z, -d.half.z, d.half.z)};
    V3 off = q - c;
    double l = spatial::length(off);
    if (l >= r) return false;
    if (l > 1e-9) {
        normal = d.turn * (off * (1.0 / l));
        depth = r - l;
    } else {
        // Its middle inside: out by the nearest face.
        const double g[3] = {d.half.x - std::fabs(q.x), d.half.y - std::fabs(q.y), d.half.z - std::fabs(q.z)};
        const int a = g[0] <= g[1] && g[0] <= g[2] ? 0 : g[1] <= g[2] ? 1 : 2;
        const double side[3] = {q.x < 0 ? -1.0 : 1.0, q.y < 0 ? -1.0 : 1.0, q.z < 0 ? -1.0 : 1.0};
        normal = d.turn * V3{a == 0 ? side[0] : 0.0, a == 1 ? side[1] : 0.0, a == 2 ? side[2] : 0.0};
        depth = g[a] + r;
    }
    return true;
}

// The doorways of a space, as their openings: whoever is inside one, close in
// front of it or just through, is not stopped by what stands behind it.
struct Opening {
    Pose at;
    double hw, hh;
};
std::vector<Opening> openings_of(const State& s) {
    std::vector<Opening> out;
    for (const Element& e : s.elements())
        if (e.alive && e.kind == kinds::portal && e.params.num("walk", 0.0) > 0.5)
            out.push_back({world_pose(s, e), e.params.num(keys::w, 1.0) * 0.5, e.params.num(keys::h, 2.0) * 0.5});
    return out;
}
// Whether the surface a ball touches at `touch` is opened there by a doorway:
// the point is in a doorway's opening, on its plane or just behind it - the
// wall it is cut in, the wall or floor it hangs on. What is beside it or under
// it still stands.
bool opened(const std::vector<Opening>& open, const V3& touch) {
    for (const Opening& o : open) {
        const Vec3d l = local_of(o.at, vd(touch));
        if (l.x < 0.05 && l.x > -0.6 && std::fabs(l.z) < o.hw && std::fabs(l.y) < o.hh) return true;
    }
    return false;
}

// Along a ray, where it first meets a solid: how far, and the way out there.
bool ray_solid(const Solid& d, V3 o, V3 dir, double& t, V3& n) {
    const V3 rel = o - d.centre;
    if (d.ball) {
        const double R = d.half.x, b = spatial::dot(rel, dir), c = spatial::dot(rel, rel) - R * R, disc = b * b - c;
        if (disc < 0) return false;
        t = -b - std::sqrt(disc);
        if (t < 0) return false;
        n = spatial::normalize(rel + dir * t);
        return true;
    }
    const M3 back = spatial::transpose(d.turn);
    const V3 lo = back * rel, ld = back * dir;
    const double half[3] = {d.half.x, d.half.y, d.half.z}, oo[3] = {lo.x, lo.y, lo.z}, dd[3] = {ld.x, ld.y, ld.z};
    double t0 = 0, t1 = 1e18;
    int axis = -1;
    double sign = 1;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(dd[i]) < 1e-12) {
            if (oo[i] < -half[i] || oo[i] > half[i]) return false;
            continue;
        }
        double a = (-half[i] - oo[i]) / dd[i], b = (half[i] - oo[i]) / dd[i];
        double s = -1;
        if (a > b) std::swap(a, b), s = 1;
        if (a > t0) t0 = a, axis = i, sign = s;
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    if (axis < 0) return false;  // starting inside
    t = t0;
    n = d.turn * V3{axis == 0 ? sign : 0.0, axis == 1 ? sign : 0.0, axis == 2 ? sign : 0.0};
    return true;
}

// One pass of standing clear: each ball of the walker out of each solid it is
// in, but where a doorway opens what it touches. What it stopped against, if
// asked: its velocity loses what ran into them, `landed` says one was under
// its feet, `stopped_low` that one stopped its feet from the side.
void clear_pass(const std::vector<Solid>& solids, const std::vector<Opening>& open, V3& eye, const V3& up, double height, double r,
                V3* v = nullptr, bool* landed = nullptr, bool* stopped_low = nullptr) {
    for (const Solid& d : solids)
        for (const double down : {height - r, (height - r) * 0.5, 0.0}) {
            V3 n;
            double depth;
            const V3 at = eye - up * down;
            if (!push_out(d, at, r, n, depth)) continue;
            if (opened(open, at - n * (r - depth))) continue;  // a doorway lets them through what it opens
            const double rise = spatial::dot(n, up);
            if (stopped_low && down == height - r && rise < 0.6 && rise > -0.3) *stopped_low = true;
            eye = eye + n * depth;
            if (v) {
                const double into = spatial::dot(*v, n);
                if (into < 0) *v = *v - n * into;
            }
            if (landed && rise > 0.6) *landed = true;
        }
}

// The turn of `m` taken `angle` further about `axis`.
M3 turned(const M3& m, V3 axis, double angle) { return spatial::axis_angle(spatial::normalize(axis), angle) * m; }
}  // namespace

bool ray(const State& space, const Vec3d& eye, const Vec3d& dir, double reach, Vec3d& hit, Vec3d& normal, double* dist, Key* what) {
    const V3 o = v3(eye), d = spatial::normalize(v3(dir));
    double best = reach;
    V3 bn;
    Key id;
    for (const Solid& s : solids_of(space, eye, reach)) {
        double t;
        V3 n;
        if (ray_solid(s, o, d, t, n) && t < best) best = t, bn = n, id = s.id;
    }
    if (best >= reach) return false;
    hit = vd(o + d * best), normal = vd(bn);
    if (dist) *dist = best;
    if (what) *what = id;
    return true;
}

void stand_clear(const State& space, Element& walker, double radius) {
    const double height = walker.params.num("height", 1.65);
    const Pose ground = standing(walker);
    const V3 up = v3(up_of(ground));
    V3 eye = v3(position_of(walker));
    const std::vector<Solid> solids = solids_of(space, vd(eye), height + 1.0);
    const std::vector<Opening> open = openings_of(space);
    for (int pass = 0; pass < 2; ++pass) clear_pass(solids, open, eye, up, height, radius);
    set_position(walker, vd(eye));
}

std::vector<field::Source> fields_of(const State& s) {
    std::vector<field::Source> out;
    if (s.params().has(Key{"g"})) out.push_back(field::Source::directional("gravity", {0, -s.params().num("g"), 0}));
    for (const Element& e : s.elements()) {
        if (!e.alive || e.kind != Key{"field"}) continue;
        const auto& p = e.params;
        const std::string channel = p.get_or<std::string>("channel", "gravity");
        const std::string shape = p.get_or<std::string>("field_shape", "directional");
        const spatial::Transform at = transform_of(world_pose(s, e));
        const double strength = p.num("strength", 1.0);
        field::Source f;
        if (shape == "radial") {
            f = field::Source::radial(channel, {}, strength, p.num("exponent"));
        } else if (shape == "plane") {
            f = field::Source::plane(channel, {}, {p.num("nx"), p.num("ny", 1.0), p.num("nz")}, strength);
        } else if (shape == "box") {
            f = field::Source::box(channel, at, {p.num(keys::sx, 1.0) * 0.5, p.num(keys::sy, 1.0) * 0.5, p.num(keys::sz, 1.0) * 0.5}, strength);
            if (p.has(Key{"face_px"}) || p.has(Key{"face_nx"}) || p.has(Key{"face_py"}) || p.has(Key{"face_ny"}) ||
                p.has(Key{"face_pz"}) || p.has(Key{"face_nz"}))
                f.faces = std::array<double, 6>{p.num("face_px", 1.0), p.num("face_nx", 1.0), p.num("face_py", 1.0),
                                                p.num("face_ny", 1.0), p.num("face_pz", 1.0), p.num("face_nz", 1.0)};
        } else {
            f = field::Source::directional(channel, {p.num("dx"), p.num("dy"), p.num("dz")});
            f.strength = strength;
        }
        f.pose = at;
        f.exponent = p.num("exponent");
        // (In a space that wraps, its copies in the neighbouring cells pull too: below.)
        f.value.scalar = p.num("scalar");
        f.softening = p.num("softening", 0.01);
        if (p.has(Key{"radius"})) {
            const double r = std::max(0.0, p.num("radius"));
            f.bounds = spatial::Aabb{{-r, -r, -r}, {r, r, r}};
        }
        out.push_back(std::move(f));
    }
    // A space that wraps: what pulls in the cells round this one pulls here too.
    const Vec3d period = period_of(s);
    if (period.x > 0 || period.y > 0 || period.z > 0) {
        const std::size_t own = out.size();
        const double most = std::max({period.x, period.y, period.z});
        for (const Vec3d& o : images(s, {}, most)) {
            if (o.x == 0 && o.y == 0 && o.z == 0) continue;
            for (std::size_t i = 0; i < own; ++i) {
                if (out[i].shape == field::Source::Directional && !out[i].bounds) continue;  // everywhere already
                field::Source f = out[i];
                f.pose.translation = f.pose.translation + v3(o);
                out.push_back(std::move(f));
            }
        }
    }
    return out;
}

void walk(const State& space, const field::Solver& pull, Element& walker, const Stride& in, double dt, double time) {
    if (dt <= 0) return;
    const double height = walker.params.num("height", 1.65), r = walker.params.num("radius", 0.3);
    M3 ground = spatial::from_euler(0, 0, 0);
    {
        const Pose g = standing(walker);
        ground = spatial::from_euler(g.yaw, g.pitch, g.roll);
    }
    V3 up = ground * V3{0, 1, 0};
    V3 eye = v3(position_of(walker));
    V3 v{walker.params.num(keys::vx), walker.params.num(keys::vy), walker.params.num(keys::vz)};
    bool grounded = walker.params.num("grounded", 0.0) > 0.5;

    // The pull where its middle is, and the ground turned to stand against
    // it - about its feet, quickly when on its feet, slowly in the air.
    const V3 feet = eye - up * (height - r);
    const V3 g = pull.evaluate(eye - up * (height * 0.5), time - dt, {{"gravity", field::Response::Acceleration, 1.0}}).acceleration;
    if (spatial::length(g) > 1e-6) {
        const V3 want = spatial::normalize(-g);
        const double c = std::clamp(spatial::dot(up, want), -1.0, 1.0), angle = std::acos(c);
        if (angle > 1e-9) {
            V3 axis = spatial::cross(up, want);
            if (spatial::length(axis) < 1e-9) axis = ground * V3{1, 0, 0};
            const double most = (grounded ? 8.0 : 3.0) * dt;
            ground = spatial::orthonormal(turned(ground, axis, std::min(angle, most)));
            up = ground * V3{0, 1, 0};
            eye = feet + up * (height - r);
        }
    }

    // The look, within its ground; where it means to go, along it.
    double yaw = walker.params.num(keys::yaw) + in.turn;
    const double pitch = std::clamp(walker.params.num(keys::pitch) + in.look, -1.5, 1.5);
    // (Across a heading is to the right of whoever faces along it: with y
    // up, facing x, z is on the right.)
    const V3 ahead = ground * V3{std::cos(yaw), 0, std::sin(yaw)}, right = ground * V3{-std::sin(yaw), 0, std::cos(yaw)};
    const V3 want = (ahead * in.forward + right * in.right) * in.speed;
    const double along_up = spatial::dot(v, up);
    V3 flat = v - up * along_up;
    // On its feet it goes as it means to, at once; in the air it steers a little.
    const double grip = grounded ? 1.0 : std::min(1.0, 1.5 * dt);
    flat = flat + (want - flat) * grip;
    v = flat + up * along_up;
    if (in.jump && grounded) v = v + up * 4.6, grounded = false;
    else if (in.thrust > 0 && !grounded) {
        const V3 look = v3(forward_of(walker));
        v = v + look * (in.thrust * dt);
    }
    v = v + g * dt;

    // Moved, in steps no longer than half its radius, and stopped by what is
    // solid: its feet, its middle and its head each a ball.
    const std::vector<Solid> solids = solids_of(space, vd(eye), spatial::length(v * dt) + height + 2.0);
    const std::vector<Opening> open = openings_of(space);
    const V3 travel = v * dt;
    const int steps = std::max(1, static_cast<int>(std::ceil(spatial::length(travel) / (r * 0.5))));
    bool landed = false;
    // Whether the walker could stand with its eye at `e`: nothing solid in
    // its feet, its middle or its head (but what a doorway opens).
    const auto clear_at = [&](V3 e) {
        for (const Solid& d : solids)
            for (const double down : {height - r, (height - r) * 0.5, 0.0}) {
                V3 n;
                double depth;
                const V3 at = e - up * down;
                if (push_out(d, at, r, n, depth) && !opened(open, at - n * (r - depth))) return false;
            }
        return true;
    };
    // How high a step it takes in its stride: a stair, a kerb.
    constexpr double kStep = 0.36;
    for (int i = 0; i < steps; ++i) {
        const V3 moved = eye + travel * (1.0 / steps);
        eye = moved;
        bool stopped_low = false;
        for (int pass = 0; pass < 2; ++pass) clear_pass(solids, open, eye, up, height, r, &v, &landed, &stopped_low);
        // Stopped at the feet by something low, on one's feet: step up onto
        // it, if there is room, and down onto its top.
        if (stopped_low && (grounded || landed) && clear_at(moved + up * kStep)) {
            eye = moved + up * kStep;
            for (double down = 0.0; down < kStep && clear_at(eye - up * 0.02); down += 0.02) eye = eye - up * 0.02;
            landed = true;
            const double along_up = spatial::dot(v, up);
            if (along_up < 0) v = v - up * along_up;
        }
    }
    grounded = landed;

    // In a space that wraps, back in the cell round the origin.
    set_position(walker, wrapped(space, vd(eye)));
    walker.params.set(keys::vx, v.x).set(keys::vy, v.y).set(keys::vz, v.z);
    walker.params.set(keys::yaw, yaw).set(keys::pitch, pitch).set("grounded", grounded ? 1.0 : 0.0);
    // Level ground is a heading, kept as one: only a ground that has turned
    // is written as the walker's own.
    double gy, gp, gr;
    spatial::to_euler(ground, gy, gp, gr);
    if (std::fabs(gp) < 1e-12 && std::fabs(gr) < 1e-12 && !walker.params.has(Key{"stand_w"})) {
        walker.params.set(keys::yaw, yaw + gy);
    } else {
        set_standing(walker, Pose{{}, gy, gp, gr});
    }
}

}  // namespace sg
