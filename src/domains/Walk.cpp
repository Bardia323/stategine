#include "sg/domains/Walk.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

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

std::vector<Solid> solids_here(const State& s, const Vec3d& near, double reach) {
    std::vector<Solid> out;
    const std::vector<Vec3d> copies = images(s, near, reach);
    for (const Element& e : s.elements()) {
        if (!e.alive) continue;
        if (e.kind != kinds::wall && !(e.kind == kinds::mesh && e.params.num("solid", 0.0) > 0.5)) continue;
        // (A wall said not `solid` is not: as a thing's `solid` is read.)
        if (e.kind == kinds::wall && e.params.has(Key{"solid"}) && e.params.num("solid", 1.0) < 0.5) continue;
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

std::vector<Solid> solids_of(const State& s, const Vec3d& near = {}, double reach = 0) { return solids_here(s, near, reach); }

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
// wall it is cut in, the wall or floor it hangs on - and the ball itself (its
// centre, `at`) is within the opening's width: it passes where its eye would
// cross the doorway (crossed_portal), and nowhere else - never slipping past
// a jamb into the plane, where it would stand past the doorway without
// having gone through. What is beside it or under it still stands.
bool opened(const std::vector<Opening>& open, const V3& touch, const V3& at) {
    for (const Opening& o : open) {
        const Vec3d l = local_of(o.at, vd(touch)), c = local_of(o.at, vd(at));
        if (l.x < 0.05 && l.x > -0.6 && std::fabs(l.z) < o.hw && std::fabs(l.y) < o.hh && std::fabs(c.z) < o.hw) return true;
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
            if (opened(open, at - n * (r - depth), at)) continue;  // a doorway lets them through what it opens
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

// The ground its terrain says is under (x, z) - the highest, if it has more
// than one (Spatial3D::terrain): land, stood on whichever way its walker is
// turned, under the world's own up.
struct Grounds {
    std::vector<const Spatial3D::Height*> heights;
    explicit Grounds(const State& space) {
        if (const auto* s3 = dynamic_cast<const Spatial3D*>(&space))
            for (const Element& e : space.elements())
                if (e.alive && e.kind == Key{"terrain"})
                    if (const Spatial3D::Height* h = s3->ground(e.id)) heights.push_back(h);
    }
    bool any() const { return !heights.empty(); }
    bool under(double x, double z, double& h) const {
        bool found = false;
        for (const Spatial3D::Height* f : heights) {
            const double v = (*f)(x, z);
            if (!found || v > h) h = v, found = true;
        }
        return found;
    }
};
}  // namespace

namespace {
// One ray against what was gathered for it (and maybe for others with it):
// the solids, the land, and the land's element.
RayHit ray_through(const std::vector<Solid>& solids, const Grounds& land, Key land_id, const RayAsk& ask) {
    RayHit out;
    const V3 o = v3(ask.eye), d = spatial::normalize(v3(ask.dir));
    double best = ask.reach;
    V3 bn;
    Key id;
    for (const Solid& s : solids) {
        double t;
        V3 n;
        if (ray_solid(s, o, d, t, n) && t < best) best = t, bn = n, id = s.id;
    }
    // The land: marched until under it, then halved down to where it is met.
    double gh;
    if (land.any() && land.under(o.x, o.z, gh) && o.y > gh) {
        const double step = 0.25;
        double was = 0;
        for (double t = step; t < best; t += step) {
            const V3 p = o + d * t;
            if (land.under(p.x, p.z, gh) && p.y <= gh) {
                double a = was, b = t;
                for (int k = 0; k < 12; ++k) {
                    const double m = (a + b) * 0.5;
                    const V3 q = o + d * m;
                    double g2;
                    (land.under(q.x, q.z, g2) && q.y <= g2 ? b : a) = m;
                }
                const V3 q = o + d * b;
                double hx0, hx1, hz0, hz1;
                land.under(q.x - 0.05, q.z, hx0), land.under(q.x + 0.05, q.z, hx1), land.under(q.x, q.z - 0.05, hz0), land.under(q.x, q.z + 0.05, hz1);
                best = b, bn = spatial::normalize(V3{-(hx1 - hx0) / 0.1, 1.0, -(hz1 - hz0) / 0.1}), id = land_id;
                break;
            }
            was = t;
        }
    }
    if (best >= ask.reach) return out;
    out.hit = true, out.at = vd(o + d * best), out.normal = vd(bn), out.dist = best, out.what = id;
    return out;
}
}  // namespace

std::vector<RayHit> rays(const State& space, const std::vector<RayAsk>& asks) {
    std::vector<RayHit> out(asks.size());
    if (asks.empty()) return out;
    // What stops them gathered once, round all of them: a ball holding
    // every one from its eye to its reach.
    V3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    for (const RayAsk& a : asks)
        for (const V3 p : {v3(a.eye), v3(a.eye) + spatial::normalize(v3(a.dir)) * a.reach})
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    const V3 mid = (lo + hi) * 0.5;
    const std::vector<Solid> solids = solids_of(space, vd(mid), spatial::length(hi - lo) * 0.5);
    const Grounds land(space);
    Key land_id;
    for (const Element& e : space.elements())
        if (e.alive && e.kind == Key{"terrain"}) {
            land_id = e.id;
            break;
        }
    // Each ray on its own, its answer where it was asked: a wide batch
    // across the cores, the same answers whatever ran them.
    const std::size_t n = asks.size();
    const unsigned lanes = n < 256 ? 1u : std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    if (lanes == 1) {
        for (std::size_t i = 0; i < n; ++i) out[i] = ray_through(solids, land, land_id, asks[i]);
        return out;
    }
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < lanes; ++w)
        pool.emplace_back([&, w] {
            for (std::size_t i = w; i < n; i += lanes) out[i] = ray_through(solids, land, land_id, asks[i]);
        });
    for (std::thread& t : pool) t.join();
    return out;
}

bool ray(const State& space, const Vec3d& eye, const Vec3d& dir, double reach, Vec3d& hit, Vec3d& normal, double* dist, Key* what) {
    const RayHit r = rays(space, {RayAsk{eye, dir, reach}}).front();
    if (!r.hit) return false;
    hit = r.at, normal = r.normal;
    if (dist) *dist = r.dist;
    if (what) *what = r.what;
    return true;
}

std::vector<std::string> space_faults(const State& space, double radius, double height) {
    std::vector<std::string> out;
    const std::vector<Opening> open = openings_of(space);
    static const Key hole_kind{"hole"};
    for (const Element& e : space.elements()) {
        if (!e.alive || e.kind != hole_kind) continue;
        const double hh = e.params.num(keys::h, 2.0) * 0.5;
        if (hh * 2.0 < height) continue;  // a hatch, not walked upright
        // (Its local x is the way into the room it was a doorway of; y up; its foot its middle less half its height.)
        const Pose at = world_pose(space, e);
        const Vec3d front = place_in(at, {1.0, -hh + height, 0.0}), past = place_in(at, {-1.0, -hh + height, 0.0});
        const std::vector<Solid> solids = solids_of(space, front, 3.0);
        V3 eye = v3(front);
        const V3 up{0, 1, 0}, to = spatial::normalize(v3(past) - eye);
        bool through = false;
        for (int i = 0; i < 120 && !through; ++i) {
            eye = eye + to * (radius * 0.25);
            clear_pass(solids, open, eye, up, height, radius);
            through = local_of(at, vd(eye)).x < -0.5;
        }
        if (!through) {
            char b[96];
            std::snprintf(b, sizeof b, "%.2f m from its plane", local_of(at, vd(eye)).x);
            out.push_back("hole " + e.id.str() + " of " + space.id().str() + " (where two rooms were made one) cannot be walked through: a walker steered through its middle stops " +
                          b + " - something solid stands across it (a thing's box over the opening, a wall not opened there)");
        }
    }
    return out;
}

void stand_clear(const State& space, Element& walker, double radius) {
    const double height = walker.params.num("height", 1.65);
    const Pose ground = standing(walker);
    const V3 up = v3(up_of(ground));
    V3 eye = v3(position_of(walker));
    const std::vector<Solid> solids = solids_of(space, vd(eye), height + 1.0);
    const std::vector<Opening> open = openings_of(space);
    for (int pass = 0; pass < 2; ++pass) clear_pass(solids, open, eye, up, height, radius);
    // Feet on the land, never in it.
    const Grounds land(space);
    double gh;
    if (land.under(eye.x, eye.z, gh) && eye.y - height < gh) eye.y = gh + height;
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
    // Stepped up onto a stair, it stands that much lower (`stoop`), its feet
    // already on the step, and straightens on the clock: its eye rises with
    // it, never the whole step in a frame.
    double stoop = walker.params.num("stoop", 0.0);
    {
        const double was = stoop;
        stoop *= std::exp(-10.0 * dt);
        if (stoop < 1e-4) stoop = 0.0;
        eye = eye + (ground * V3{0, 1, 0}) * (was - stoop);
    }
    double body = height - stoop;

    // The pull where its middle is, and the ground turned to stand against
    // it - about its feet, easing (a share of the way each second, on its
    // feet more than in the air: never a step it would be seen to take).
    const V3 feet = eye - up * (body - r);
    const V3 g = pull.evaluate(eye - up * (body * 0.5), time - dt, {{"gravity", field::Response::Acceleration, 1.0}}).acceleration;
    if (spatial::length(g) > 1e-6) {
        const V3 want = spatial::normalize(-g);
        const double c = std::clamp(spatial::dot(up, want), -1.0, 1.0), angle = std::acos(c);
        if (angle > 1e-9) {
            V3 axis = spatial::cross(up, want);
            if (spatial::length(axis) < 1e-9) axis = ground * V3{1, 0, 0};
            const double share = 1.0 - std::exp(-(grounded ? 8.0 : 3.0) * dt);
            ground = spatial::orthonormal(turned(ground, axis, angle * share));
            up = ground * V3{0, 1, 0};
            eye = feet + up * (body - r);
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
    // On its feet on land steeper than it stands on (`max_slope`, radians;
    // 0.8 unless it says) it cannot go up it, and slides down it ever faster
    // (`slide`, its speed; `sliding`): what it means to do, so.
    V3 meant = want;
    double slide = 0.0;
    {
        const Grounds here(space);
        double h0, hx, hz;
        const V3 at = eye - up * body;
        if (grounded && here.any() && up.y > 0.9 && here.under(at.x, at.z, h0) && here.under(at.x + 0.25, at.z, hx) &&
            here.under(at.x, at.z + 0.25, hz) && at.y - h0 < 0.1) {
            const V3 rise{(hx - h0) / 0.25, 0, (hz - h0) / 0.25};
            const double steep = std::atan(spatial::length(rise));
            if (steep > walker.params.num("max_slope", 0.8)) {
                const V3 downhill = spatial::normalize(V3{-rise.x, 0, -rise.z});
                const double climbing = -spatial::dot(meant, downhill);
                if (climbing > 0) meant = meant + downhill * climbing;
                slide = std::min(walker.params.num("slide") + 9.8 * (std::sin(steep) - 0.3 * std::cos(steep)) * dt, 12.0);  // (as down a slope with some grip)
                meant = meant + downhill * slide;
            }
        }
    }
    walker.params.set("slide", slide).set("sliding", slide > 0.0 ? 1.0 : 0.0);
    // On its feet it goes as it means to, at once; in the air it steers a little.
    const double grip = grounded ? 1.0 : std::min(1.0, 1.5 * dt);
    flat = flat + (meant - flat) * grip;
    v = flat + up * along_up;
    bool jumped = false;
    if (in.jump && grounded) v = v + up * 4.6, grounded = false, jumped = true;
    else if (in.thrust > 0 && !grounded) {
        const V3 look = v3(forward_of(walker));
        v = v + look * (in.thrust * dt);
    }
    v = v + g * dt;

    // Moved, in steps no longer than half its radius, and stopped by what is
    // solid: its feet, its middle and its head each a ball.
    const std::vector<Solid> solids = solids_of(space, vd(eye), spatial::length(v * dt) + body + 2.0);
    const std::vector<Opening> open = openings_of(space);
    const Grounds land(space);
    const V3 travel = v * dt;
    const int steps = std::max(1, static_cast<int>(std::ceil(spatial::length(travel) / (r * 0.5))));
    bool landed = false;
    // Whether the walker could stand with its eye at `e`: nothing solid in
    // its feet, its middle or its head (but what a doorway opens).
    const auto clear_at = [&](V3 e) {
        for (const Solid& d : solids)
            for (const double down : {body - r, (body - r) * 0.5, 0.0}) {
                V3 n;
                double depth;
                const V3 at = e - up * down;
                if (push_out(d, at, r, n, depth) && !opened(open, at - n * (r - depth), at)) return false;
            }
        return true;
    };
    // How high a step it takes in its stride: a stair, a kerb.
    constexpr double kStep = 0.36;
    for (int i = 0; i < steps; ++i) {
        const V3 moved = eye + travel * (1.0 / steps);
        eye = moved;
        bool stopped_low = false;
        for (int pass = 0; pass < 2; ++pass) clear_pass(solids, open, eye, up, body, r, &v, &landed, &stopped_low);
        // On the land: lifted onto it where it rises under the feet.
        double gh;
        if (land.under(eye.x, eye.z, gh) && eye.y - body < gh) {
            eye.y = gh + body, landed = true;
            if (v.y < 0) v.y = 0;
        }
        // Stopped at the feet by something low, on one's feet: step up onto
        // it, if there is room, and down onto its top - the feet, at once;
        // the eye stays where it was, the walker that much lower (stoop).
        if (stopped_low && (grounded || landed) && clear_at(moved + up * kStep)) {
            V3 top = moved + up * kStep;
            for (double down = 0.0; down < kStep && clear_at(top - up * 0.02); down += 0.02) top = top - up * 0.02;
            const double rise = spatial::dot(top - moved, up), taken = std::min(rise, std::max(0.0, 0.6 * height - stoop));
            stoop += taken, body = height - stoop;
            eye = moved + up * (rise - taken);
            landed = true;
            const double along_up = spatial::dot(v, up);
            if (along_up < 0) v = v - up * along_up;
        }
    }
    // Walking down a slope of the land, the feet keep to it (a stride's drop,
    // not a fall).
    {
        double gh;
        if (!landed && grounded && land.under(eye.x, eye.z, gh) && eye.y - body - gh < 0.5 && v.y <= 0.5) {
            eye.y = gh + body, landed = true;
            if (v.y < 0) v.y = 0;
        }
    }
    // Off its feet only a moment (a step off a kerb), it is still on them
    // for `grace` seconds (0.12 unless it says) - but not once it has jumped.
    const double air = landed ? 0.0 : jumped ? 1e9 : walker.params.num("air") + dt;
    grounded = landed || air < walker.params.num("grace", 0.12);
    walker.params.set("air", std::min(air, 1e3));

    // In a space that wraps, back in the cell round the origin.
    set_position(walker, wrapped(space, vd(eye)));
    walker.params.set(keys::vx, v.x).set(keys::vy, v.y).set(keys::vz, v.z);
    walker.params.set(keys::yaw, yaw).set(keys::pitch, pitch).set("grounded", grounded ? 1.0 : 0.0).set("stoop", stoop);
    // (Level ground is written as a heading: set_standing.)
    double gy, gp, gr;
    spatial::to_euler(ground, gy, gp, gr);
    set_standing(walker, Pose{{}, gy, gp, gr});
}

}  // namespace sg
