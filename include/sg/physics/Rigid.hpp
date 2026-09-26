// Rigid bodies: things that fall, tumble, stack, slide, tip over and are
// picked up - a mug, a book, a chair, a table turned on its back.
//
// A body is one or more convex hulls (boxes, and cylinders as prisms of many
// sides) fixed together, with its mass spread through their volume. Things
// that do not move - the floor, the walls, a bookcase - are bodies too, of no
// mass. Each step:
//
//   collide   every pair that can touch, by their boxes - found by sweeping
//             along x (sweep and prune: only boxes whose spans along x meet
//             are compared, and a thing at rest is never compared with
//             another at rest); then, hull against
//             hull, the axis they are least deep along (faces of either, or
//             an edge of each: the separating axis test), and where they
//             touch - the face of one clipped by the other's, up to four
//             points (a manifold). Kept from step to step, so what was
//             pushed last time is pushed again at once (warm starting).
//   solve     in substeps: gravity; impulses at every point of contact, over
//             and over, each one's total kept within its limits - never
//             pulling, friction within mu of the push - with soft springs
//             to undo what has sunk in; the bodies moved; the same impulses
//             again without the springs, so undoing a sink adds no energy
//             (a soft step, as Box2D v3 takes it); bounces last.
//   sweep     something that went far this step for its size - a stone
//             thrown, a pebble flicked spinning - is swept along its way,
//             turning as it went, against what does not move, and stopped
//             where it first met it: never let through a thin wall
//             (conservative advancement, by the gap between the hulls).
//   sleep     bodies touching each other are an island; an island that has
//             lain still a moment sleeps, costs nothing, and wakes when
//             something moving touches it.
//
// Joints hold two bodies together (or one to the room): at a point (a
// ball), and turning only about an axis (a hinge - a door, a wheel), with
// limits to how far, a motor to drive it and a spring to draw it back; or
// two points drawn to a length by a spring. They are solved with the
// contacts, softly as they are, and join islands: what is joined sleeps and
// wakes together.
//
// A body can be driven (World::drive): moved by the game to where it should
// be by the end of the next step - furniture hauled, a lift, a board pulled
// by its stand - at the speed that takes. For that step it is as heavy as
// the room: what it meets is pushed out of its way, what lies on it goes
// with it by friction, and what sleeps against it wakes.
//
// A sensor is a body nothing bumps into: things pass through it, and the
// world keeps what is inside each one (World::inside, entered, left) - a
// doorway that notices who walks through, a pressure plate. A hull can be
// cast along a line (World::cast): what it would meet first, and how far.
//
// A walker (World::walk) is someone on their feet among it all: an upright
// body that is not a rigid one - it slides along what it walks into, steps
// up a stair and down again, stands on what is gentle enough and not on
// what is too steep, falls off an edge, rides what it stands on (a moving
// platform, a lift, a boat) and shoves what is light enough aside.
//
// A hand holds a body by a soft spring at a point of it, as strong as the
// arm: a mug comes up whole and is held square to the eye; a table taken by
// its edge lifts that edge and turns over on the other one.
//
// Coordinates are the world's: y up, metres, seconds, kilograms. Turns are
// the renderer's (from_euler): yaw about y, then pitch, then roll.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace sg::rigid {

// --- vectors and turns ---------------------------------------------------------------
struct V3 {
    double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
inline V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline V3 operator*(double k, V3 a) { return a * k; }
inline V3& operator+=(V3& a, V3 b) { return a = a + b; }
inline V3& operator-=(V3& a, V3 b) { return a = a - b; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) {
    const double l = length(a);
    return l > 1e-12 ? a * (1.0 / l) : V3{0, 1, 0};
}

// Row-major 3x3.
struct M3 {
    std::array<double, 9> a{1, 0, 0, 0, 1, 0, 0, 0, 1};
    double operator()(int r, int c) const { return a[static_cast<std::size_t>(r * 3 + c)]; }
    double& operator()(int r, int c) { return a[static_cast<std::size_t>(r * 3 + c)]; }
    V3 col(int c) const { return {(*this)(0, c), (*this)(1, c), (*this)(2, c)}; }
};
inline V3 operator*(const M3& m, V3 v) {
    return {m.a[0] * v.x + m.a[1] * v.y + m.a[2] * v.z, m.a[3] * v.x + m.a[4] * v.y + m.a[5] * v.z,
            m.a[6] * v.x + m.a[7] * v.y + m.a[8] * v.z};
}
inline M3 operator*(const M3& p, const M3& q) {
    M3 c;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c(i, j) = p(i, 0) * q(0, j) + p(i, 1) * q(1, j) + p(i, 2) * q(2, j);
    return c;
}
inline M3 operator+(const M3& p, const M3& q) {
    M3 c;
    for (std::size_t i = 0; i < 9; ++i) c.a[i] = p.a[i] + q.a[i];
    return c;
}
inline M3 operator*(const M3& p, double k) {
    M3 c;
    for (std::size_t i = 0; i < 9; ++i) c.a[i] = p.a[i] * k;
    return c;
}
inline M3 transpose(const M3& m) {
    M3 t;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) t(i, j) = m(j, i);
    return t;
}
inline M3 zero3() {
    M3 z;
    z.a.fill(0.0);
    return z;
}
inline M3 outer(V3 u, V3 v) {
    M3 m;
    m.a = {u.x * v.x, u.x * v.y, u.x * v.z, u.y * v.x, u.y * v.y, u.y * v.z, u.z * v.x, u.z * v.y, u.z * v.z};
    return m;
}
inline M3 skew(V3 v) {
    M3 m;
    m.a = {0, -v.z, v.y, v.z, 0, -v.x, -v.y, v.x, 0};
    return m;
}
inline M3 inverse(const M3& m) {
    const double c00 = m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1), c01 = m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2),
                 c02 = m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0);
    const double det = m(0, 0) * c00 + m(0, 1) * c01 + m(0, 2) * c02;
    if (std::fabs(det) < 1e-18) return zero3();
    const double k = 1.0 / det;
    M3 r;
    r.a = {c00 * k,
           (m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2)) * k,
           (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) * k,
           c01 * k,
           (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) * k,
           (m(0, 2) * m(1, 0) - m(0, 0) * m(1, 2)) * k,
           c02 * k,
           (m(0, 1) * m(2, 0) - m(0, 0) * m(2, 1)) * k,
           (m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0)) * k};
    return r;
}
// A turn of `angle` about the unit `axis` (Rodrigues).
inline M3 axis_angle(V3 k, double angle) {
    const double c = std::cos(angle), s = std::sin(angle), t = 1 - c;
    M3 q;
    q.a = {t * k.x * k.x + c,       t * k.x * k.y - s * k.z, t * k.x * k.z + s * k.y,
           t * k.x * k.y + s * k.z, t * k.y * k.y + c,       t * k.y * k.z - s * k.x,
           t * k.x * k.z - s * k.y, t * k.y * k.z + s * k.x, t * k.z * k.z + c};
    return q;
}
// Back to a rotation, after many small turns have been multiplied in.
inline M3 orthonormal(const M3& m) {
    V3 x = normalize(m.col(0)), y = m.col(1);
    y = normalize(y - x * dot(x, y));
    const V3 z = cross(x, y);
    M3 r;
    r.a = {x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z};
    return r;
}
// The turn that takes the identity to `r`, as axis times angle.
inline V3 log_map(const M3& r) {
    const double c = std::clamp((r(0, 0) + r(1, 1) + r(2, 2) - 1.0) * 0.5, -1.0, 1.0);
    const double angle = std::acos(c);
    const V3 v{r(2, 1) - r(1, 2), r(0, 2) - r(2, 0), r(1, 0) - r(0, 1)};
    if (angle < 1e-6) return v * 0.5;
    if (angle > 3.14159265358979 - 1e-4) {
        // Half a turn: the axis is the column of r + I that is longest.
        M3 p = r + M3{};
        V3 best = p.col(0);
        for (int i = 1; i < 3; ++i)
            if (length(p.col(i)) > length(best)) best = p.col(i);
        return normalize(best) * angle;
    }
    return v * (angle / (2.0 * std::sin(angle)));
}

// The renderer's turn: R = Ry(-yaw) Rz(pitch) Rx(roll), as a mesh is turned.
inline M3 from_euler(double yaw, double pitch, double roll) {
    const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll),
                 sr = std::sin(roll);
    M3 ry, rz, rx;
    ry.a = {cy, 0, -sy, 0, 1, 0, sy, 0, cy};
    rz.a = {cp, -sp, 0, sp, cp, 0, 0, 0, 1};
    rx.a = {1, 0, 0, 0, cr, -sr, 0, sr, cr};
    return ry * (rz * rx);
}
inline void to_euler(const M3& m, double& yaw, double& pitch, double& roll) {
    pitch = std::asin(std::clamp(m(1, 0), -1.0, 1.0));
    if (std::fabs(std::cos(pitch)) > 1e-6) {
        roll = std::atan2(-m(1, 2), m(1, 1));
        yaw = std::atan2(m(2, 0), m(0, 0));
    } else {
        roll = 0.0;
        yaw = std::atan2(-m(0, 2), m(2, 2));
    }
}

// --- hulls -------------------------------------------------------------------------
// A convex solid: its corners, its faces (each a plane and its corners in
// order round it, seen from outside), its edges (each between two faces).
struct Hull {
    struct Face {
        V3 n;                 // outward
        std::vector<int> vi;  // counter-clockwise, seen from outside
    };
    struct Edge {
        int a, b, fa, fb;
    };
    std::vector<V3> v;
    std::vector<Face> f;
    std::vector<Edge> e;
    V3 centre;      // of its volume
    double volume = 0;
    double friction = -1;  // its own, where it differs from its body's (castors roll; a tray grips)
    M3 cov;         // second moment about its centre, per unit density

    // A solid of `sides` corners round (`sides` 4 is a box), `half` wide
    // across x and z and high in y, its base at `centre.y - half.y`; the top
    // `taper` times as wide as the bottom. Turned by `turn`, then moved to
    // `centre`.
    static Hull prism(V3 centre, V3 half, int sides, const M3& turn = M3{}, double taper = 1.0) {
        Hull h;
        const int n = std::max(3, sides);
        const double k = n == 4 ? std::sqrt(2.0) : 1.0 / std::cos(3.14159265358979 / n);  // corners outside the circle
        for (int level = 0; level < 2; ++level) {
            const double s = level ? taper : 1.0;
            for (int i = 0; i < n; ++i) {
                const double a = (i + 0.5) * 2 * 3.14159265358979 / n + (n == 4 ? 0.0 : 0.0);
                const double cx = n == 4 ? (i == 0 || i == 3 ? 1.0 : -1.0) : std::cos(a) * k;
                const double cz = n == 4 ? (i < 2 ? 1.0 : -1.0) : std::sin(a) * k;
                // A cylinder's prism is drawn out to touch its circle at the
                // middle of each side only a little: halfway between the
                // circle and the polygon round it.
                const double r = n == 4 ? 1.0 : (1.0 + 1.0 / k) * 0.5;
                const V3 local{cx * half.x * s * r, level ? half.y : -half.y, cz * half.z * s * r};
                h.v.push_back(centre + turn * local);
            }
        }
        Face bottom, top;
        for (int i = 0; i < n; ++i) bottom.vi.push_back(i), top.vi.push_back(n + i);
        h.f.push_back(bottom);
        h.f.push_back(top);
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            h.f.push_back(Face{{}, {i, j, n + j, n + i}});
        }
        h.finish();
        return h;
    }
    static Hull box(V3 centre, V3 half, const M3& turn = M3{}) { return prism(centre, half, 4, turn); }

    // Normals, orders, edges and mass, from the corners and faces.
    void finish() {
        V3 mid;
        for (const V3& p : v) mid += p;
        mid = mid * (1.0 / static_cast<double>(v.size()));
        for (Face& face : f) {
            // Newell's normal, then turned outward.
            V3 nn, fc;
            for (std::size_t i = 0; i < face.vi.size(); ++i) {
                const V3 p = v[static_cast<std::size_t>(face.vi[i])], q = v[static_cast<std::size_t>(face.vi[(i + 1) % face.vi.size()])];
                nn += V3{(p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y)};
                fc += p;
            }
            fc = fc * (1.0 / static_cast<double>(face.vi.size()));
            if (dot(nn, fc - mid) < 0) {
                std::reverse(face.vi.begin(), face.vi.end());
                nn = -nn;
            }
            face.n = normalize(nn);
        }
        e.clear();
        std::unordered_map<uint64_t, std::size_t> seen;
        for (std::size_t fi = 0; fi < f.size(); ++fi) {
            const auto& vi = f[fi].vi;
            for (std::size_t i = 0; i < vi.size(); ++i) {
                const int a = vi[i], b = vi[(i + 1) % vi.size()];
                const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint32_t>(std::max(a, b));
                auto it = seen.find(key);
                if (it == seen.end()) {
                    seen[key] = e.size();
                    e.push_back(Edge{a, b, static_cast<int>(fi), -1});
                } else {
                    e[it->second].fb = static_cast<int>(fi);
                }
            }
        }
        // Volume, centre and second moment: a tetrahedron from `mid` to each
        // triangle of each face, summed (Blow and Binstock).
        volume = 0;
        V3 c;
        M3 C = zero3();
        M3 canon;
        canon.a = {2, 1, 1, 1, 2, 1, 1, 1, 2};
        canon = canon * (1.0 / 120.0);
        for (const Face& face : f)
            for (std::size_t i = 1; i + 1 < face.vi.size(); ++i) {
                const V3 a = v[static_cast<std::size_t>(face.vi[0])] - mid, b = v[static_cast<std::size_t>(face.vi[i])] - mid,
                         d = v[static_cast<std::size_t>(face.vi[i + 1])] - mid;
                M3 A;
                A.a = {a.x, b.x, d.x, a.y, b.y, d.y, a.z, b.z, d.z};
                const double det = dot(a, cross(b, d));
                volume += det / 6.0;
                c += (a + b + d) * (det / 24.0);
                C = C + A * canon * transpose(A) * det;
            }
        if (volume > 1e-15) c = c * (1.0 / volume);
        // About its own centre.
        cov = C + outer(c, c) * (-volume);
        centre = mid + c;
    }
};

// --- bodies ------------------------------------------------------------------------
struct Body {
    std::string id;
    std::vector<Hull> hulls;  // in the body's own frame
    V3 x;                     // where its frame is
    M3 r;                     // how its frame is turned
    V3 v, w;                  // its centre's velocity; its spin, in the room
    double mass = 0;          // 0: it does not move
    double friction = 0.55, restitution = 0.2;
    bool awake = true;
    double idle = 0;          // how long it has been still
    double hit = 0;           // the hardest knock since it was last asked
    bool grabbed = false;
    double radius = -1;       // how far any of it is from its frame; worked out when first asked
    bool sensor = false;      // nothing bumps into it: what is inside it is only noted
    bool driven = false;      // this step, moved to `drive_x`, `drive_r` by the game
    V3 drive_x;
    M3 drive_r;
    int contacts = 0;         // how many things it touches, this step

    // Derived: where its mass is, and how it resists turning.
    V3 com_local;
    M3 inv_inertia_local = zero3();
    double inv_mass = 0;

    // Where everything is now, in the room.
    struct Placed {
        std::vector<V3> v, n;
        std::vector<double> d;
        V3 centre, lo, hi;
    };
    std::vector<Placed> world;
    V3 lo, hi;

    bool dynamic() const { return inv_mass > 0; }
    V3 com() const { return x + r * com_local; }
    M3 inv_inertia() const { return r * inv_inertia_local * transpose(r); }

    // Mass `m` spread evenly through the hulls; 0 makes it fixed.
    void set_mass(double m) {
        mass = m;
        double vol = 0;
        V3 c;
        for (const Hull& h : hulls) vol += h.volume, c += h.centre * h.volume;
        if (m <= 0 || vol <= 1e-12) {
            inv_mass = 0;
            inv_inertia_local = zero3();
            com_local = vol > 1e-12 ? c * (1.0 / vol) : V3{};
            return;
        }
        c = c * (1.0 / vol);
        const double density = m / vol;
        M3 C = zero3();
        for (const Hull& h : hulls) C = C + (h.cov + outer(h.centre - c, h.centre - c) * h.volume) * density;
        const double tr = C(0, 0) + C(1, 1) + C(2, 2);
        M3 I = C * -1.0;
        I(0, 0) += tr, I(1, 1) += tr, I(2, 2) += tr;
        // Nothing thin spins without limit: at least a little inertia every way.
        const double least = m * 1e-5;
        for (int i = 0; i < 3; ++i) I(i, i) = std::max(I(i, i), least);
        com_local = c;
        inv_mass = 1.0 / m;
        inv_inertia_local = inverse(I);
    }

    void place() {
        world.resize(hulls.size());
        lo = {1e18, 1e18, 1e18}, hi = {-1e18, -1e18, -1e18};
        for (std::size_t i = 0; i < hulls.size(); ++i) {
            const Hull& h = hulls[i];
            Placed& p = world[i];
            p.v.resize(h.v.size());
            p.lo = {1e18, 1e18, 1e18}, p.hi = {-1e18, -1e18, -1e18};
            for (std::size_t j = 0; j < h.v.size(); ++j) {
                const V3 q = x + r * h.v[j];
                p.v[j] = q;
                p.lo = {std::min(p.lo.x, q.x), std::min(p.lo.y, q.y), std::min(p.lo.z, q.z)};
                p.hi = {std::max(p.hi.x, q.x), std::max(p.hi.y, q.y), std::max(p.hi.z, q.z)};
            }
            p.n.resize(h.f.size());
            p.d.resize(h.f.size());
            for (std::size_t j = 0; j < h.f.size(); ++j) {
                p.n[j] = r * h.f[j].n;
                p.d[j] = dot(p.n[j], p.v[static_cast<std::size_t>(h.f[j].vi[0])]);
            }
            p.centre = x + r * h.centre;
            lo = {std::min(lo.x, p.lo.x), std::min(lo.y, p.lo.y), std::min(lo.z, p.lo.z)};
            hi = {std::max(hi.x, p.hi.x), std::max(hi.y, p.hi.y), std::max(hi.z, p.hi.z)};
        }
    }
};

// --- where two hulls touch -------------------------------------------------------------
struct Touch {
    V3 p;         // the point, halfway between the surfaces
    double sep;   // how far apart there (below 0: how deep)
    uint32_t id;  // which corners and sides made it: the same point next step has the same id
};

namespace detail {

inline bool minkowski_face(V3 a, V3 b, V3 c, V3 d) {
    // Do the arcs a-b and c-d cross on the sphere of directions?
    const V3 bxa = cross(b, a), dxc = cross(d, c);
    const double cba = dot(c, bxa), dba = dot(d, bxa), adc = dot(a, dxc), bdc = dot(b, dxc);
    return cba * dba < 0 && adc * bdc < 0 && cba * bdc > 0;
}

// The closest points of two segments.
inline void closest(V3 p1, V3 q1, V3 p2, V3 q2, V3& c1, V3& c2) {
    const V3 d1 = q1 - p1, d2 = q2 - p2, rr = p1 - p2;
    const double a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, rr);
    double s = 0, t = 0;
    if (a <= 1e-12 && e <= 1e-12) {
    } else if (a <= 1e-12) {
        t = std::clamp(f / e, 0.0, 1.0);
    } else {
        const double c = dot(d1, rr);
        if (e <= 1e-12) {
            s = std::clamp(-c / a, 0.0, 1.0);
        } else {
            const double b = dot(d1, d2), den = a * e - b * b;
            s = den > 1e-12 ? std::clamp((b * f - c * e) / den, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0) t = 0, s = std::clamp(-c / a, 0.0, 1.0);
            else if (t > 1) t = 1, s = std::clamp((b - c) / a, 0.0, 1.0);
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

}  // namespace detail

// Where hull `ha` (placed `pa`) touches hull `hb` (placed `pb`), or would
// within `margin`: the normal from a to b, and up to four points. None if
// they are farther apart than that.
inline int touch(const Hull& ha, const Body::Placed& pa, const Hull& hb, const Body::Placed& pb, double margin, V3& normal,
                 std::array<Touch, 4>& out) {
    // Faces of a.
    double fa_sep = -1e18;
    int fa = -1;
    for (std::size_t i = 0; i < pa.n.size(); ++i) {
        double least = 1e18;
        for (const V3& q : pb.v) least = std::min(least, dot(pa.n[i], q));
        const double s = least - pa.d[i];
        if (s > fa_sep) fa_sep = s, fa = static_cast<int>(i);
        if (s > margin) return 0;
    }
    double fb_sep = -1e18;
    int fb = -1;
    for (std::size_t i = 0; i < pb.n.size(); ++i) {
        double least = 1e18;
        for (const V3& q : pa.v) least = std::min(least, dot(pb.n[i], q));
        const double s = least - pb.d[i];
        if (s > fb_sep) fb_sep = s, fb = static_cast<int>(i);
        if (s > margin) return 0;
    }
    // Edge against edge, where they could make a face of the difference.
    double e_sep = -1e18;
    V3 e_n, e_pa, e_qa, e_pb, e_qb;
    for (const Hull::Edge& ea : ha.e) {
        if (ea.fb < 0) continue;
        const V3 u1 = pa.n[static_cast<std::size_t>(ea.fa)], u2 = pa.n[static_cast<std::size_t>(ea.fb)];
        const V3 a0 = pa.v[static_cast<std::size_t>(ea.a)], a1 = pa.v[static_cast<std::size_t>(ea.b)];
        const V3 da = a1 - a0;
        for (const Hull::Edge& eb : hb.e) {
            if (eb.fb < 0) continue;
            const V3 v1 = pb.n[static_cast<std::size_t>(eb.fa)], v2 = pb.n[static_cast<std::size_t>(eb.fb)];
            if (!detail::minkowski_face(u1, u2, -v1, -v2)) continue;
            const V3 b0 = pb.v[static_cast<std::size_t>(eb.a)], b1 = pb.v[static_cast<std::size_t>(eb.b)];
            V3 n = cross(da, b1 - b0);
            const double l = length(n);
            if (l < 1e-6 * std::sqrt(dot(da, da) * dot(b1 - b0, b1 - b0)) || l < 1e-12) continue;
            n = n * (1.0 / l);
            if (dot(n, a0 - pa.centre) < 0) n = -n;
            const double s = dot(n, b0 - a0);
            if (s > e_sep) e_sep = s, e_n = n, e_pa = a0, e_qa = a1, e_pb = b0, e_qb = b1;
            if (s > margin) return 0;
        }
    }

    // A face, unless an edge is clearly nearer the truth.
    const double face_sep = std::max(fa_sep, fb_sep);
    if (e_sep > -1e17 && e_sep > 0.98 * face_sep + 0.002 && e_sep > face_sep) {
        V3 c1, c2;
        detail::closest(e_pa, e_qa, e_pb, e_qb, c1, c2);
        normal = e_n;
        out[0] = Touch{(c1 + c2) * 0.5, dot(c2 - c1, e_n), 0xE0000000u};
        return 1;
    }
    const bool on_a = fa_sep >= 0.98 * fb_sep + 0.0005 || fb < 0;
    const Hull& rh = on_a ? ha : hb;
    const Body::Placed& rp = on_a ? pa : pb;
    const Hull& ih = on_a ? hb : ha;
    const Body::Placed& ip = on_a ? pb : pa;
    const int rf = on_a ? fa : fb;
    const V3 rn = rp.n[static_cast<std::size_t>(rf)];
    const double rd = rp.d[static_cast<std::size_t>(rf)];
    // The face of the other most against it.
    int inc = 0;
    double least = 1e18;
    for (std::size_t i = 0; i < ip.n.size(); ++i) {
        const double k = dot(ip.n[i], rn);
        if (k < least) least = k, inc = static_cast<int>(i);
    }
    // Each corner of the clipped face keeps where it came from: a corner of
    // the incident face, or where a side of the reference face cut an edge.
    struct C {
        V3 p;
        uint32_t id;
    };
    std::vector<C> poly, next;
    poly.reserve(16), next.reserve(16);
    for (int i : ih.f[static_cast<std::size_t>(inc)].vi) poly.push_back(C{ip.v[static_cast<std::size_t>(i)], static_cast<uint32_t>(i) & 0xFFu});
    // Clipped by the sides of the reference face.
    const auto& rv = rh.f[static_cast<std::size_t>(rf)].vi;
    for (std::size_t i = 0; i < rv.size() && !poly.empty(); ++i) {
        const V3 p0 = rp.v[static_cast<std::size_t>(rv[i])], p1 = rp.v[static_cast<std::size_t>(rv[(i + 1) % rv.size()])];
        const V3 side = normalize(cross(p1 - p0, rn));
        // (Outward from the face: its corners run counter-clockwise about rn.)
        const double sd = dot(side, p0);
        next.clear();
        for (std::size_t j = 0; j < poly.size(); ++j) {
            const C a = poly[j], b = poly[(j + 1) % poly.size()];
            const double da = dot(side, a.p) - sd, db = dot(side, b.p) - sd;
            if (da <= 0) next.push_back(a);
            if ((da < 0) != (db < 0) && std::fabs(da - db) > 1e-15)
                next.push_back(C{a.p + (b.p - a.p) * (da / (da - db)), 0x100u | (static_cast<uint32_t>(i) << 9) | (a.id & 0xFFu) | ((b.id & 0xFFu) << 16)});
        }
        poly.swap(next);
    }
    std::vector<Touch> pts;
    const uint32_t faces = (on_a ? 0x80000000u : 0u) | ((static_cast<uint32_t>(rf) & 0x3Fu) << 25);
    for (const C& q : poly) {
        const double s = dot(rn, q.p) - rd;
        if (s <= margin) pts.push_back(Touch{q.p - rn * (s * 0.5), s, faces ^ (q.id * 2654435761u >> 7)});
    }
    if (pts.empty()) return 0;
    normal = on_a ? rn : -rn;
    // At most four: the deepest, the farthest from it, then the two that
    // make the most of the area.
    if (pts.size() > 4) {
        std::array<Touch, 4> keep;
        std::size_t i0 = 0;
        for (std::size_t i = 1; i < pts.size(); ++i)
            if (pts[i].sep < pts[i0].sep) i0 = i;
        keep[0] = pts[i0];
        std::size_t i1 = 0;
        double best = -1;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const V3 d = pts[i].p - keep[0].p;
            if (dot(d, d) > best) best = dot(d, d), i1 = i;
        }
        keep[1] = pts[i1];
        std::size_t i2 = 0;
        best = -1;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const double a = dot(cross(pts[i].p - keep[0].p, keep[1].p - keep[0].p), rn);
            if (std::fabs(a) > best) best = std::fabs(a), i2 = i;
        }
        keep[2] = pts[i2];
        const double side2 = dot(cross(keep[2].p - keep[0].p, keep[1].p - keep[0].p), rn);
        std::size_t i3 = 0;
        best = -1e18;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            // The opposite side of the line 0-1 from point 2, as far as can be.
            const double a = -dot(cross(pts[i].p - keep[0].p, keep[1].p - keep[0].p), rn) * (side2 >= 0 ? 1 : -1);
            if (a > best) best = a, i3 = i;
        }
        keep[3] = pts[i3];
        out = keep;
        return 4;
    }
    for (std::size_t i = 0; i < pts.size(); ++i) out[i] = pts[i];
    return static_cast<int>(pts.size());
}

// How far apart two hulls are at least: the widest gap along any axis that
// could part them - a face of either, or across an edge of each. Never more
// than they truly are apart; below 0, they may touch.
inline double apart(const Hull& ha, const Body::Placed& pa, const Hull& hb, const Body::Placed& pb) {
    const auto gap = [&](V3 n) {
        double a0 = 1e18, a1 = -1e18, b0 = 1e18, b1 = -1e18;
        for (const V3& q : pa.v) a0 = std::min(a0, dot(n, q)), a1 = std::max(a1, dot(n, q));
        for (const V3& q : pb.v) b0 = std::min(b0, dot(n, q)), b1 = std::max(b1, dot(n, q));
        return std::max(b0 - a1, a0 - b1);
    };
    double best = -1e18;
    for (const V3& n : pa.n) best = std::max(best, gap(n));
    for (const V3& n : pb.n) best = std::max(best, gap(n));
    for (const Hull::Edge& ea : ha.e)
        for (const Hull::Edge& eb : hb.e) {
            const V3 n = cross(pa.v[static_cast<std::size_t>(ea.b)] - pa.v[static_cast<std::size_t>(ea.a)],
                               pb.v[static_cast<std::size_t>(eb.b)] - pb.v[static_cast<std::size_t>(eb.a)]);
            const double l = length(n);
            if (l > 1e-9) best = std::max(best, gap(n * (1.0 / l)));
        }
    return best;
}

// Where a ray first meets a hull, or -1; and the face it meets there.
// Which of a thing's parts make its shape. A thing made of many parts (a
// chair: seat, back, legs, five castors, a knob) is too many hulls to collide
// cheaply, and the small ones mostly do not matter - but the outermost always
// do: they are what meets the floor, a wall, another thing, however it lies.
// So: every part of any size (at least `small` of the biggest's volume), and
// every smaller one that reaches the outside of the whole, on any side, within
// `margin` - every castor and foot, the top rail of a chair's back - the
// lowest first, up to `most` in all. Each part as its box in the thing's own
// frame (`lo`, `hi`) and its volume; the answer is their indices.
struct PartBox {
    V3 lo, hi;
    double volume = 0;
};
inline std::vector<std::size_t> outline(const std::vector<PartBox>& parts, std::size_t most = 28, double margin = 0.01,
                                        double small = 0.01) {
    double biggest = 0;
    V3 lo{1e18, 1e18, 1e18}, hi{-1e18, -1e18, -1e18};
    for (const PartBox& p : parts) {
        biggest = std::max(biggest, p.volume);
        lo = {std::min(lo.x, p.lo.x), std::min(lo.y, p.lo.y), std::min(lo.z, p.lo.z)};
        hi = {std::max(hi.x, p.hi.x), std::max(hi.y, p.hi.y), std::max(hi.z, p.hi.z)};
    }
    std::vector<std::size_t> keep, rest;
    for (std::size_t i = 0; i < parts.size(); ++i) (parts[i].volume >= biggest * small ? keep : rest).push_back(i);
    std::sort(rest.begin(), rest.end(), [&](std::size_t a, std::size_t b) { return parts[a].lo.y < parts[b].lo.y; });
    for (std::size_t i : rest) {
        if (keep.size() >= most) break;
        const PartBox& q = parts[i];
        if (q.lo.x < lo.x + margin || q.lo.y < lo.y + margin || q.lo.z < lo.z + margin || q.hi.x > hi.x - margin ||
            q.hi.y > hi.y - margin || q.hi.z > hi.z - margin)
            keep.push_back(i);
    }
    if (keep.size() > most) keep.resize(most);
    return keep;
}

inline double ray_hull(const Body::Placed& p, V3 o, V3 d, double reach, V3* normal = nullptr) {
    double t0 = 0, t1 = reach;
    int face = -1;
    for (std::size_t i = 0; i < p.n.size(); ++i) {
        const double den = dot(p.n[i], d), dist = p.d[i] - dot(p.n[i], o);
        if (std::fabs(den) < 1e-12) {
            if (dist < 0) return -1;
            continue;
        }
        const double t = dist / den;
        if (den < 0) {
            if (t > t0) t0 = t, face = static_cast<int>(i);
        } else {
            t1 = std::min(t1, t);
        }
        if (t0 > t1) return -1;
    }
    if (normal && face >= 0) *normal = p.n[static_cast<std::size_t>(face)];
    return t0;
}

// How high the surface of `b` is at (x, z), no higher than `below`; -1 if it
// is not there. A face steeper than a sheet stays put on is nothing to lie on.
inline double surface_at(const Body& b, double x, double z, double below) {
    if (x < b.lo.x || x > b.hi.x || z < b.lo.z || z > b.hi.z || b.lo.y > below) return -1.0;
    double top = -1.0;
    const double from = std::min(below, b.hi.y) + 0.001;
    for (const Body::Placed& p : b.world) {
        V3 n;
        const double t = ray_hull(p, {x, from, z}, {0, -1, 0}, from - b.lo.y + 0.01, &n);
        if (t >= 0 && n.y > std::cos(0.45)) top = std::max(top, from - t);
    }
    return top;
}

// Something light lying on a body that moves goes with it - friction, as
// cheaply as it can be had: stuck to it while the surface tilts less than it
// would slide at and speeds up or slows down less than its grip holds, let
// go when it does more. `ride` works out the one step: the body's pose and
// velocity before (`x0`, `r0`, `v0`) and now, a thing at `p` turned `face`
// (its up along `up` in its own frame). True if it stays on, moved.
inline bool ride(const Body& b, V3 x0, const M3& r0, V3 v0, double dt, V3& p, M3& face, V3 up_local, V3* let_go_v = nullptr) {
    const M3 turn = b.r * transpose(r0);
    p = b.x + turn * (p - x0);
    face = turn * face;
    const V3 dv = b.v - v0;
    const double shove = std::sqrt(dv.x * dv.x + dv.z * dv.z) / std::max(dt, 1e-4);
    if ((face * up_local).y > std::cos(0.45) && shove < 0.55 * 9.81) return true;
    if (let_go_v) *let_go_v = b.v + cross(b.w, p - b.com());
    return false;
}

// --- joints ---------------------------------------------------------------------------
// Two bodies held together at a point, `b` empty for the room itself. Made
// by World::ball, hinge and spring, from where things are now.
struct Joint {
    enum Kind { Ball, Hinge, Spring };
    Kind kind = Ball;
    std::string a, b;   // b empty: the room
    V3 la, lb;          // the point, in each one's frame (the room's, for the room)
    V3 axis_a, axis_b;  // a hinge's axis, in each frame
    V3 ref_a, ref_b;    // across the axis, in each frame: its angle is between them
    // A hinge turns only between `lower` and `upper` (radians) if `limit`;
    // a motor drives it at `speed` with at most `torque`; a spring draws it
    // to `target`, `hertz` stiff, `damping` damped (1: just no overshoot).
    bool limit = false;
    double lower = 0, upper = 0;
    bool motor = false;
    double speed = 0, torque = 0;
    bool spring = false;
    double target = 0, hertz = 2, damping = 1;
    double rest = 0;    // a Spring joint's length (hertz and damping as above)
    // What it pushed with last substep, to start the next from (per substep).
    V3 point;
    double tilt1 = 0, tilt2 = 0, drive = 0, pull = 0, low = 0, high = 0;
    int moving = -1;
};

// --- walkers --------------------------------------------------------------------------
// Someone on their feet: where they stand, how big they are, how high a
// stair they take in their stride and how steep a slope they stand on.
struct Walker {
    V3 at;                 // where their feet are
    double radius = 0.3, height = 1.8;
    double step = 0.3;     // the highest stair taken in a stride
    double slope = 0.8;    // the steepest ground stood on, radians from level
    double mass = 70;      // what they shove with
    double vy = 0;         // falling
    bool grounded = false;
    V3 ground{0, 1, 0};    // which way the ground under them faces
    std::string on;        // what they stand on
    V3 on_x;               // and where that was, and how turned, last step
    M3 on_r;
    double turned = 0;     // how far what they stand on turned them this step (about y)
};

// A direction across `n`, and another across both.
inline void across_of(V3 n, V3& p1, V3& p2) {
    p1 = normalize(std::fabs(n.x) > 0.57 ? V3{n.y, -n.x, 0} : V3{0, n.z, -n.y});
    p2 = cross(n, p1);
}

// --- the world ------------------------------------------------------------------------
class World {
public:
    V3 gravity{0, -9.81, 0};
    int substeps = 4;
    int iterations = 2;        // per substep, with springs; and as many again without
    double margin = 0.012;     // how far apart two things may be and still count as touching
    double slop = 0.0005;       // how deep a thing may rest in another without being pushed out
    double contact_hertz = 30; // how stiff the springs that push things apart are
    double max_push = 2.0;     // and how fast they may push, m/s
    double sleep_after = 0.5;  // s still before an island sleeps
    // Pairs found by sweeping along x; false: every awake body against every
    // other, as it was (to compare - the same pairs either way).
    bool sweep = true;

    std::vector<Body> bodies;

    // --- bodies ----------------------------------------------------------------------
    Body& add(Body b) {
        b.place();
        index_[b.id] = bodies.size();
        bodies.push_back(std::move(b));
        return bodies.back();
    }
    Body* find(const std::string& id) {
        auto it = index_.find(id);
        return it == index_.end() ? nullptr : &bodies[it->second];
    }
    const Body* find(const std::string& id) const {
        auto it = index_.find(id);
        return it == index_.end() ? nullptr : &bodies[it->second];
    }
    void remove(const std::string& id) {
        auto it = index_.find(id);
        if (it == index_.end()) return;
        const std::size_t i = it->second;
        release(id);
        unjoin(id);
        bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(i));
        manifolds_.clear();
        index_.clear();
        for (std::size_t k = 0; k < bodies.size(); ++k) index_[bodies[k].id] = k;
    }
    void clear() {
        bodies.clear();
        index_.clear();
        manifolds_.clear();
        grabs_.clear();
        joints.clear();
    }

    // Put a body somewhere, as if it had always been there (not moved
    // through what is between), still.
    void teleport(Body& b, V3 x, const M3& r) {
        b.x = x;
        b.r = r;
        b.v = b.w = {};
        b.place();
        wake_near(b);
    }
    // A fixed thing has moved: whatever sleeps on it wakes.
    void moved(Body& b, V3 x, const M3& r) {
        const V3 lo = b.lo, hi = b.hi;
        b.x = x;
        b.r = r;
        b.place();
        wake_box({std::min(lo.x, b.lo.x), std::min(lo.y, b.lo.y), std::min(lo.z, b.lo.z)},
                 {std::max(hi.x, b.hi.x), std::max(hi.y, b.hi.y), std::max(hi.z, b.hi.z)});
    }
    void wake(Body& b) {
        if (!b.dynamic()) return;
        b.awake = true;
        b.idle = 0;
    }
    void wake_box(V3 lo, V3 hi) {
        for (Body& o : bodies)
            if (o.dynamic() && !o.awake && o.lo.x <= hi.x + margin && o.hi.x >= lo.x - margin && o.lo.y <= hi.y + margin &&
                o.hi.y >= lo.y - margin && o.lo.z <= hi.z + margin && o.hi.z >= lo.z - margin)
                wake(o);
    }
    void wake_near(const Body& b) { wake_box(b.lo, b.hi); }
    bool any_awake() const {
        for (const Body& b : bodies)
            if ((b.dynamic() && b.awake) || b.driven) return true;
        return false;
    }

    // --- joints ----------------------------------------------------------------------
    std::vector<Joint> joints;

    // `a` and `b` (or the room, `b` empty) held together at `at`, each free
    // to turn about it.
    Joint& ball(const std::string& a, const std::string& b, V3 at) { return join(Joint::Ball, a, b, at, {0, 1, 0}); }
    // Held at `at`, and turning only about `axis` - as they stand now is angle 0.
    Joint& hinge(const std::string& a, const std::string& b, V3 at, V3 axis) { return join(Joint::Hinge, a, b, at, normalize(axis)); }
    // The point `pa` of `a` and `pb` of `b` (or of the room) drawn to the
    // length they are apart now, by a spring `hertz` stiff.
    Joint& spring(const std::string& a, const std::string& b, V3 pa, V3 pb, double hertz, double damping = 1.0) {
        Joint& j = join(Joint::Spring, a, b, pa, {0, 1, 0});
        const Body* B = b.empty() ? nullptr : find(b);
        j.lb = B ? transpose(B->r) * (pb - B->x) : pb;
        j.rest = length(pb - pa), j.hertz = hertz, j.damping = damping;
        return j;
    }
    // How far a hinge has turned from where it was made: `a` against `b`
    // (a door against its frame), radians about the axis.
    double angle(const Joint& j) const {
        const Body* A = find(j.a);
        const Body* B = j.b.empty() ? nullptr : find(j.b);
        if (!A) return 0;
        const V3 axis = A->r * j.axis_a, ra = A->r * j.ref_a, rb = B ? B->r * j.ref_b : j.ref_b;
        return std::atan2(dot(cross(rb, ra), axis), dot(ra, rb));
    }
    // --- driving ---------------------------------------------------------------------
    // `b` moved to `x`, turned `r`, by the end of the next step, at the speed
    // that takes; what sleeps where it goes wakes. (Called each frame it is
    // moved: once stepped, it is itself again - fixed, or free.)
    void drive(Body& b, V3 x, const M3& r) {
        b.driven = true, b.drive_x = x, b.drive_r = r;
        const V3 lo = b.lo, hi = b.hi;
        const V3 was_x = b.x;
        const M3 was_r = b.r;
        b.x = x, b.r = r;
        b.place();
        wake_box({std::min(lo.x, b.lo.x), std::min(lo.y, b.lo.y), std::min(lo.z, b.lo.z)},
                 {std::max(hi.x, b.hi.x), std::max(hi.y, b.hi.y), std::max(hi.z, b.hi.z)});
        b.x = was_x, b.r = was_r;
        b.place();
    }

    // --- sensors ---------------------------------------------------------------------
    // What is inside a sensor now, as (sensor, thing) - and what came in and
    // went out this step.
    using Inside = std::pair<std::string, std::string>;
    const std::set<Inside>& inside() const { return inside_; }
    const std::vector<Inside>& entered() const { return entered_; }
    const std::vector<Inside>& left() const { return left_; }

    // --- casts -----------------------------------------------------------------------
    // The hull `shape`, turned `turn`, carried from `from` to `to` without
    // turning: the first thing it would meet (not `skip`, not a sensor), how
    // far along the way (0..1, in `at`) and which way that thing faces there
    // (towards the shape, in `normal`); none if it meets nothing.
    const Body* cast(const Hull& shape, const M3& turn, V3 from, V3 to, double* at = nullptr, V3* normal = nullptr,
                     const std::string& skip = {}) const {
        Body probe;
        probe.hulls.push_back(shape);
        probe.r = turn;
        const V3 way = to - from;
        const double length_of_way = length(way);
        const auto pose_at = [&](double t) {
            probe.x = from + way * t;
            probe.place();
        };
        pose_at(0.0);
        V3 lo = probe.lo, hi = probe.hi;
        pose_at(1.0);
        lo = {std::min(lo.x, probe.lo.x), std::min(lo.y, probe.lo.y), std::min(lo.z, probe.lo.z)};
        hi = {std::max(hi.x, probe.hi.x), std::max(hi.y, probe.hi.y), std::max(hi.z, probe.hi.z)};
        const double near = 1e-4;
        const Body* best = nullptr;
        double first = 1.0;
        for (const Body& o : bodies) {
            if (o.sensor || (!skip.empty() && o.id == skip)) continue;
            if (o.hi.x < lo.x || o.lo.x > hi.x || o.hi.y < lo.y || o.lo.y > hi.y || o.hi.z < lo.z || o.lo.z > hi.z) continue;
            const auto gap = [&] {
                double least = 1e18;
                for (std::size_t hb = 0; hb < o.hulls.size(); ++hb) least = std::min(least, apart(probe.hulls[0], probe.world[0], o.hulls[hb], o.world[hb]));
                return least;
            };
            double t = 0;
            for (int k = 0; k < 48 && t < first; ++k) {
                pose_at(t);
                const double g = gap();
                if (g < near) {
                    first = t, best = &o;
                    break;
                }
                if (length_of_way < 1e-12) break;
                t += std::max(g / length_of_way, 1e-6);
            }
        }
        if (at) *at = first;
        if (best && normal) {
            // Which way it faces there: the axis it would be met along.
            pose_at(first);
            V3 n{0, 1, 0};
            double least = 1e18;
            for (std::size_t hb = 0; hb < best->hulls.size(); ++hb) {
                std::array<Touch, 4> t;
                V3 nn;
                if (touch(probe.hulls[0], probe.world[0], best->hulls[hb], best->world[hb], 0.05, nn, t) > 0 && t[0].sep < least)
                    least = t[0].sep, n = nn * -1.0;
            }
            *normal = n;
        }
        return best;
    }

    // --- walkers ---------------------------------------------------------------------
    // One step of a walker wanting to go `move` (across the ground; its
    // height is the ground's): carried by what they stand on, then across -
    // sliding along what they meet, the bottom `step` of them passing over
    // what is lower (a stair) - then down onto the ground, if it is within
    // a stride below and not too steep, or falling.
    void walk(Walker& w, V3 move, double dt) {
        const double skin = 0.002;
        w.turned = 0;
        // Carried by what they stand on: however it moved and turned since.
        if (!w.on.empty()) {
            if (const Body* p = find(w.on)) {
                const M3 dr = p->r * transpose(w.on_r);
                w.at = p->x + dr * (w.at - w.on_x);
                w.turned = std::atan2(dr(0, 2), dr(0, 0)) * -1.0;
            }
        }
        const double body_h = w.height - w.step;
        const Hull body = Hull::prism({}, {w.radius, body_h * 0.5, w.radius}, 8);
        const auto body_at = [&](V3 feet) { return feet + V3{0, w.step + body_h * 0.5, 0}; };
        // On a slope too steep to stand on, not up it.
        V3 left{move.x, 0, move.z};
        if (!w.grounded && w.ground.y < std::cos(w.slope) && w.ground.y > 0.05) {
            const V3 away = normalize(V3{w.ground.x, 0, w.ground.z});
            const double into = dot(left, away);
            if (into < 0) left = left - away * into;
        }
        for (int k = 0; k < 4 && length(left) > 1e-7; ++k) {
            const V3 from = body_at(w.at);
            double t = 1;
            V3 n;
            const Body* hit = cast(body, M3{}, from, from + left, &t, &n);
            const double len = length(left);
            if (!hit) {
                w.at += left;
                break;
            }
            const double go = std::max(0.0, t * len - skin);
            w.at += left * (go / len);
            left = left * (1.0 - go / len);
            // Something loose in the way is shoved, as much as it is light.
            if (Body* b = find(hit->id); b && b->dynamic() && dt > 0) {
                wake(*b);
                const V3 dir = left * (1.0 / std::max(length(left), 1e-9));
                b->v += dir * (len / dt) * std::clamp(w.mass / (w.mass + b->mass), 0.05, 0.9) * 0.5;
            }
            // Along what stopped them.
            V3 nh{n.x, 0, n.z};
            const double l = length(nh);
            if (l < 1e-6) break;
            nh = nh * (1.0 / l);
            left = left - nh * dot(left, nh);
        }
        // Down: the ground within a stride below, or a fall.
        if (w.grounded) w.vy = 0;
        w.vy += gravity.y * dt;
        const double fall = std::max(0.0, -w.vy * dt);
        const Hull sole = Hull::prism({}, {w.radius * 0.9, 0.01, w.radius * 0.9}, 8);
        const V3 top = w.at + V3{0, w.step, 0};
        const double reach = w.step + fall + (w.grounded ? w.step : 0.0);  // kept to the ground going down a stair
        double t = 1;
        V3 n{0, 1, 0};
        const Body* under = cast(sole, M3{}, top, top - V3{0, reach, 0}, &t, &n);
        if (under) {
            // Met within the stride: stood on, if it is gentle enough; on
            // what is too steep, held to it but not standing.
            w.at.y = top.y - reach * t - 0.01;
            w.ground = n;
            w.vy = 0;
            w.grounded = n.y >= std::cos(w.slope);
            if (w.grounded) w.on = under->id, w.on_x = under->x, w.on_r = under->r;
            else w.on.clear();
        } else {
            w.at.y -= fall;
            w.grounded = false;
            w.on.clear();
            w.ground = {0, 1, 0};
        }
    }

    // Whatever holds `id` to anything, let go.
    void unjoin(const std::string& id) {
        joints.erase(std::remove_if(joints.begin(), joints.end(), [&](const Joint& j) { return j.a == id || j.b == id; }), joints.end());
    }

    // --- the hand ----------------------------------------------------------------------
    // Hold `id` by the point `local` of it (in its own frame), pulling that
    // point to `target` with at most `force` newtons. With `upright`, the
    // body is turned to `turn` as well, with at most `torque`.
    struct Grab {
        std::string id;
        V3 local, target;
        double force = 400;
        bool turns = false;
        M3 turn;
        double torque = 30;
        double hertz = 6, damping = 1.0;
        // Solver scratch.
        V3 impulse, spin;
    };
    Grab& grab(const std::string& id, V3 local, V3 target, double force) {
        release(id);
        Grab g;
        g.id = id, g.local = local, g.target = target, g.force = force;
        grabs_.push_back(g);
        if (Body* b = find(id)) wake(*b), b->grabbed = true;
        return grabs_.back();
    }
    Grab* grabbing(const std::string& id) {
        for (Grab& g : grabs_)
            if (g.id == id) return &g;
        return nullptr;
    }
    void release(const std::string& id) {
        grabs_.erase(std::remove_if(grabs_.begin(), grabs_.end(), [&](const Grab& g) { return g.id == id; }), grabs_.end());
        if (Body* b = find(id)) b->grabbed = false, wake(*b);
    }

    // What a ray meets first, and where along it.
    const Body* ray(V3 o, V3 d, double reach, double* at = nullptr, bool dynamic_only = true, const std::string& skip = {},
                    V3* normal = nullptr) const {
        const Body* best = nullptr;
        double bt = reach;
        for (const Body& b : bodies) {
            if ((dynamic_only && !b.dynamic()) || b.sensor) continue;
            if (!skip.empty() && b.id == skip) continue;
            for (const Body::Placed& p : b.world) {
                V3 n;
                const double t = ray_hull(p, o, d, bt, &n);
                if (t >= 0 && t < bt) {
                    bt = t, best = &b;
                    if (normal) *normal = n;
                }
            }
        }
        if (at) *at = bt;
        return best;
    }

    // Putting something down: where to hold the middle of `id`'s mass, turned
    // `turn`, so that it hangs just clear above the top the eye is on - the
    // floor, a table, a shelf - or false if the eye is on no top within
    // `reach` (a wall, the air). Let go there, it drops the last centimetre
    // and stands - or does not, as it is shaped.
    bool hover(const std::string& id, V3 eye, V3 dir, double reach, const M3& turn, V3& com_at, double clear = 0.012) const {
        const Body* b = find(id);
        if (!b) return false;
        double t = 0;
        V3 n;
        if (!ray(eye, dir, reach, &t, false, id, &n) || n.y < 0.7) return false;
        double low = 1e18;
        for (const Hull& h : b->hulls)
            for (const V3& v : h.v) low = std::min(low, (turn * (v - b->com_local)).y);
        com_at = eye + dir * t + V3{0, clear - low, 0};
        return true;
    }
    // How far from its middle a body reaches: to hold it clear of the eye.
    static double reach_of(const Body& b) {
        double r = 0;
        for (const Hull& h : b.hulls)
            for (const V3& v : h.v) r = std::max(r, length(v - b.com_local));
        return r;
    }

    // --- someone walking -----------------------------------------------------------------
    // A walker is an upright circle at (x, z), `radius` round, from `y0` to
    // `y1`: out of every moving body in the way that stands higher than a
    // step. What they walk into is shoved - at the height of their hips, so
    // a floor lamp walked into tips over - as hard as they walk, the lighter
    // it is the more; the walker is kept out of it all the same.
    V3 walk_into(V3 p, double radius, double y0, double y1, V3 moved, double dt, std::vector<std::string>* shoved = nullptr) {
        const double step_up = 0.3;
        for (Body& b : bodies) {
            // (What is in their hands does not push them about; a sensor
            // is walked through.)
            if (!b.dynamic() || b.grabbed || b.sensor) continue;
            if (b.hi.x < p.x - radius || b.lo.x > p.x + radius || b.hi.z < p.z - radius || b.lo.z > p.z + radius) continue;
            if (b.hi.y < y0 + step_up || b.lo.y > y1) continue;
            for (const Body::Placed& h : b.world) {
                if (h.hi.y < y0 + step_up || h.lo.y > y1) continue;
                if (h.hi.x < p.x - radius || h.lo.x > p.x + radius || h.hi.z < p.z - radius || h.lo.z > p.z + radius) continue;
                // Its shadow on the floor: the convex outline of its corners.
                std::vector<std::array<double, 2>> pts;
                for (const V3& q : h.v) pts.push_back({q.x, q.z});
                std::sort(pts.begin(), pts.end());
                std::vector<std::array<double, 2>> ring(pts.size() * 2);
                std::size_t k = 0;
                const auto turn = [](const std::array<double, 2>& o, const std::array<double, 2>& a, const std::array<double, 2>& c) {
                    return (a[0] - o[0]) * (c[1] - o[1]) - (a[1] - o[1]) * (c[0] - o[0]);
                };
                for (std::size_t i = 0; i < pts.size(); ++i) {
                    while (k >= 2 && turn(ring[k - 2], ring[k - 1], pts[i]) <= 0) --k;
                    ring[k++] = pts[i];
                }
                for (std::size_t i = pts.size() - 1, t = k + 1; i > 0; --i) {
                    while (k >= t && turn(ring[k - 2], ring[k - 1], pts[i - 1]) <= 0) --k;
                    ring[k++] = pts[i - 1];
                }
                ring.resize(k > 1 ? k - 1 : k);
                if (ring.size() < 3) continue;
                // The nearest point of the outline, and whether p is inside.
                bool inside = true;
                double best = 1e18;
                std::array<double, 2> near{};
                for (std::size_t i = 0; i < ring.size(); ++i) {
                    const auto& a = ring[i];
                    const auto& c = ring[(i + 1) % ring.size()];
                    const double ex = c[0] - a[0], ez = c[1] - a[1], len2 = ex * ex + ez * ez;
                    if ((ex * (p.z - a[1]) - ez * (p.x - a[0])) < 0) inside = false;
                    const double t = len2 > 1e-12 ? std::clamp(((p.x - a[0]) * ex + (p.z - a[1]) * ez) / len2, 0.0, 1.0) : 0.0;
                    const double qx = a[0] + ex * t, qz = a[1] + ez * t;
                    const double d2 = (p.x - qx) * (p.x - qx) + (p.z - qz) * (p.z - qz);
                    if (d2 < best) best = d2, near = {qx, qz};
                }
                const double d = std::sqrt(best);
                if (!inside && d >= radius) continue;
                V3 n = d > 1e-9 ? V3{(p.x - near[0]) / d, 0, (p.z - near[1]) / d} : V3{-moved.x, 0, -moved.z};
                if (inside) n = -n;
                n = normalize(n);
                const double depth = inside ? radius + d : radius - d;
                p += n * depth;
                // Shoved: the part of the walk that went into it.
                const double into = -dot(moved, n) / std::max(dt, 1e-3);
                if (into <= 0) continue;
                wake(b);
                const double give = std::min(1.0, 18.0 / b.mass);
                const V3 at{near[0], std::clamp(y0 + 0.9, h.lo.y, h.hi.y), near[1]};
                const V3 r = at - b.com();
                const V3 dir = -n;
                const double k_eff = b.inv_mass + dot(cross(r, dir), b.inv_inertia() * cross(r, dir));
                const double want = into * give - dot(b.v + cross(b.w, r), dir);
                if (want <= 0 || k_eff <= 0) continue;
                const V3 j = dir * (want / k_eff);
                b.v += j * b.inv_mass;
                b.w += b.inv_inertia() * cross(r, j);
                if (shoved) shoved->push_back(b.id);
            }
        }
        return p;
    }

    // --- a step ------------------------------------------------------------------------
    void step(double dt) {
        if (dt <= 0) return;
        dt = std::min(dt, 1.0 / 20.0);
        if (!any_awake()) return;
        for (Body& b : bodies)
            if (b.dynamic() && b.awake) b.place();
        stepped_ = dt;
        // The driven: as fast as takes them there this step.
        for (Body& b : bodies) {
            if (!b.driven) continue;
            b.v = ((b.drive_x + b.drive_r * b.com_local) - b.com()) * (1.0 / dt);
            b.w = log_map(b.drive_r * transpose(b.r)) * (1.0 / dt);
        }
        from_x_.resize(bodies.size());
        from_r_.resize(bodies.size());
        for (std::size_t i = 0; i < bodies.size(); ++i) from_x_[i] = bodies[i].x, from_r_[i] = bodies[i].r;
        collide();
        const double h = dt / substeps;
        prepare(h);
        for (int s = 0; s < substeps; ++s) {
            integrate_velocities(h);
            warm_start();
            solve(h, true);
            integrate_positions(h);
            relax(h);
        }
        restitution();
        for (Body& b : bodies) {
            if (b.driven) {
                // Where it was sent, exactly; then itself again.
                b.x = b.drive_x, b.r = b.drive_r;
                b.place();
                b.driven = false;
                b.v = b.w = {};  // stopped where it was taken: a hand hauling it holds it
                continue;
            }
            if (b.dynamic() && b.awake) b.place();
        }
        sweep_fast();
        sleep(dt);
    }

private:
    struct Point {
        V3 p, ra, rb;        // at collision time; arms from the centres of mass
        V3 la, lb;           // the same point in each body's frame, to find it again
        double sep = 0;      // at collision time
        double pn = 0, pt1 = 0, pt2 = 0;
        double mn = 0, mt1 = 0, mt2 = 0;
        double vn0 = 0, most = 0;
        uint32_t id = 0;
    };
    struct Manifold {
        std::size_t a = 0, b = 0;
        int ha = 0, hb = 0;
        V3 n, t1, t2;
        std::vector<Point> pts;
        double friction = 0.5, restitution = 0.2;
        bool live = false;
        int moving = -1;  // which of the two moved when its impulses were found (1: a, 2: b)
    };

    // Per body scratch for a step (kept apart from Body's public face).
    std::unordered_map<std::string, std::size_t> index_;
    std::unordered_map<uint64_t, Manifold> manifolds_;

public:
    // What each contact was pushed with last step: to start a step again
    // from where it started once before.
    using Contacts = std::unordered_map<uint64_t, Manifold>;
    Contacts contacts() const { return manifolds_; }
    // The pairs of hulls touching now, by their keys, in order.
    std::vector<uint64_t> touching() const {
        std::vector<uint64_t> out;
        for (const auto& [k, m] : manifolds_) out.push_back(k);
        std::sort(out.begin(), out.end());
        return out;
    }
    void set_contacts(Contacts c) { manifolds_ = std::move(c); }

private:
    std::vector<Manifold*> live_;
    std::vector<Grab> grabs_;

    static uint64_t pair_key(std::size_t a, std::size_t b, int ha, int hb) {
        return (static_cast<uint64_t>(a) << 40) ^ (static_cast<uint64_t>(b) << 16) ^ (static_cast<uint64_t>(ha) << 8) ^
               static_cast<uint64_t>(hb);
    }

    void collide() {
        for (auto& [k, m] : manifolds_) m.live = false;
        live_.clear();
        for (Body& b : bodies) b.contacts = 0;
        // What is inside a sensor is found again for whatever moves; what
        // lies still in one stays in it.
        const std::set<Inside> was = inside_;
        for (auto it = inside_.begin(); it != inside_.end();) {
            const Body* s = find(it->first);
            const Body* o = find(it->second);
            it = !s || !o || moves(*s) || moves(*o) ? inside_.erase(it) : std::next(it);
        }
        if (sweep) sweep_pairs();
        else every_pair();
        entered_.clear();
        left_.clear();
        for (const Inside& i : inside_)
            if (!was.count(i)) entered_.push_back(i);
        for (const Inside& i : was)
            if (!inside_.count(i)) left_.push_back(i);
        for (auto it = manifolds_.begin(); it != manifolds_.end();)
            it = it->second.live ? std::next(it) : manifolds_.erase(it);
        for (auto& [k, m] : manifolds_) {
            live_.push_back(&m);
            ++bodies[m.a].contacts, ++bodies[m.b].contacts;
        }
        // The solver meets the contacts in one order, whatever found them
        // and however the table of them grew: by the pair, then the hulls.
        std::sort(live_.begin(), live_.end(), [](const Manifold* x, const Manifold* y) {
            if (x->a != y->a) return x->a < y->a;
            if (x->b != y->b) return x->b < y->b;
            if (x->ha != y->ha) return x->ha < y->ha;
            return x->hb < y->hb;
        });
    }

    Joint& join(Joint::Kind kind, const std::string& a, const std::string& b, V3 at, V3 axis) {
        Joint j;
        j.kind = kind, j.a = a, j.b = b;
        const Body* A = find(a);
        const Body* B = b.empty() ? nullptr : find(b);
        const M3 ta = A ? transpose(A->r) : M3{}, tb = B ? transpose(B->r) : M3{};
        j.la = A ? ta * (at - A->x) : at;
        j.lb = B ? tb * (at - B->x) : at;
        V3 p1, p2;
        across_of(axis, p1, p2);
        j.axis_a = ta * axis, j.axis_b = tb * axis;
        j.ref_a = ta * p1, j.ref_b = tb * p1;
        joints.push_back(j);
        if (Body* x = find(a)) wake(*x);
        if (Body* x = b.empty() ? nullptr : find(b)) wake(*x);
        return joints.back();
    }

    // The joints, held: at their points, about their axes, within their
    // limits, driven and sprung - a soft step as the contacts take, stiffer
    // (`joint_hertz`); `springs` false, only what moves now is held (the
    // relaxing pass), as for the contacts. What sleeps does not move.
    double joint_hertz = 60.0;
    struct Held {
        Body* A;
        Body* B;
        V3 ra, rb, pa, pb;
        double ma, mb;
        M3 ia, ib;
    };
    bool held(Joint& j, Held& h) {
        h.A = find(j.a);
        h.B = j.b.empty() ? nullptr : find(j.b);
        if (!h.A) return false;
        const bool ma = moves(*h.A), mb = h.B && moves(*h.B);
        if (!ma && !mb) return false;
        h.ma = ma ? h.A->inv_mass : 0.0, h.mb = mb ? h.B->inv_mass : 0.0;
        h.ia = ma ? h.A->inv_inertia() : zero3(), h.ib = mb ? h.B->inv_inertia() : zero3();
        h.pa = h.A->x + h.A->r * j.la;
        h.pb = h.B ? h.B->x + h.B->r * j.lb : j.lb;
        h.ra = h.pa - h.A->com();
        h.rb = h.B ? h.pb - h.B->com() : V3{};
        const int moving_now = (ma ? 1 : 0) | (mb ? 2 : 0);
        if (j.moving != moving_now) {
            // Found against something that could not move then: not handed on.
            j.point = {}, j.tilt1 = j.tilt2 = j.drive = j.pull = j.low = j.high = 0;
            j.moving = moving_now;
        }
        return true;
    }
    void push(Held& h, V3 j) {
        if (h.ma > 0) h.A->v -= j * h.ma, h.A->w -= h.ia * cross(h.ra, j);
        if (h.mb > 0) h.B->v += j * h.mb, h.B->w += h.ib * cross(h.rb, j);
    }
    void twist(Held& h, V3 t) {
        if (h.ma > 0) h.A->w -= h.ia * t;
        if (h.mb > 0) h.B->w += h.ib * t;
    }
    V3 spin_between(const Held& h) const { return (h.mb > 0 ? h.B->w : V3{}) - (h.ma > 0 ? h.A->w : V3{}); }
    V3 speed_between(const Held& h) const {
        const V3 vb = h.mb > 0 ? h.B->v + cross(h.B->w, h.rb) : V3{};
        const V3 va = h.ma > 0 ? h.A->v + cross(h.A->w, h.ra) : V3{};
        return vb - va;
    }
    static double soft(double hz, double zeta, double h, double& ms, double& is) {
        const double omega = 2.0 * 3.14159265358979 * hz, a1 = 2.0 * zeta + h * omega, a2 = h * omega * a1, a3 = 1.0 / (1.0 + a2);
        ms = a2 * a3, is = a3;
        return omega / a1;
    }

    void warm_joints() {
        for (Joint& j : joints) {
            Held h;
            if (!held(j, h)) continue;
            push(h, j.point);
            if (j.kind == Joint::Hinge) {
                const V3 axis = h.A->r * j.axis_a;
                V3 p1, p2;
                across_of(axis, p1, p2);
                twist(h, p1 * j.tilt1 + p2 * j.tilt2 - axis * (j.drive + j.pull + j.low - j.high));
            } else if (j.kind == Joint::Spring) {
                const V3 d = h.pb - h.pa;
                const double len = length(d);
                if (len > 1e-9) push(h, d * (j.pull / len));
            }
        }
    }

    void solve_joints(double hstep, bool springs) {
        double ms = 1, is = 0;
        const double bias_rate = springs ? soft(joint_hertz, 5.0, hstep, ms, is) : 0.0;
        if (!springs) ms = 1, is = 0;
        for (Joint& j : joints) {
            Held h;
            if (!held(j, h)) continue;
            const auto effective = [&](V3 d) {  // along d, at the points
                const V3 xa = cross(h.ra, d), xb = cross(h.rb, d);
                const double k = h.ma + h.mb + dot(xa, h.ia * xa) + dot(xb, h.ib * xb);
                return k > 1e-12 ? 1.0 / k : 0.0;
            };
            const auto turning = [&](V3 d) {  // about d
                const double k = dot(d, h.ia * d) + dot(d, h.ib * d);
                return k > 1e-12 ? 1.0 / k : 0.0;
            };
            if (j.kind == Joint::Spring) {
                const V3 d = h.pb - h.pa;
                const double len = length(d);
                if (len < 1e-9) continue;
                const V3 n = d * (1.0 / len);
                double sms = 1, sis = 0;
                const double rate = soft(j.hertz, j.damping, hstep, sms, sis);
                if (!springs) continue;  // a spring is a force, not a correction
                const double lambda = -effective(n) * sms * (dot(speed_between(h), n) + rate * (len - j.rest)) - sis * j.pull;
                j.pull += lambda;
                push(h, n * lambda);
                continue;
            }
            if (j.kind == Joint::Hinge) {
                const V3 axis = h.A->r * j.axis_a, other = h.B ? h.B->r * j.axis_b : j.axis_b;
                V3 p1, p2;
                across_of(axis, p1, p2);
                const V3 w = spin_between(h);
                // Only about the axis: the other two ways, held.
                const V3 tilt = cross(axis, other);
                for (int k = 0; k < 2; ++k) {
                    const V3 d = k ? p2 : p1;
                    double& acc = k ? j.tilt2 : j.tilt1;
                    const double lambda = -turning(d) * ms * (dot(w, d) + bias_rate * dot(tilt, d)) - is * acc;
                    acc += lambda;
                    twist(h, d * lambda);
                }
                const double m_axis = turning(axis);
                const double theta = angle(j);
                // Along the axis, what turns is `a` against `b`: a twist of
                // `ax` turns `a` forward.
                const V3 ax = axis * -1.0;
                // The motor: towards its speed, with no more than its torque.
                if (j.motor) {
                    const double most = j.torque * hstep;
                    const double want = -m_axis * (dot(spin_between(h), ax) - j.speed);
                    const double total = std::clamp(j.drive + want, -most, most);
                    twist(h, ax * (total - j.drive));
                    j.drive = total;
                }
                // The spring: drawn back towards its angle.
                if (j.spring && springs) {
                    double sms = 1, sis = 0;
                    const double rate = soft(j.hertz, j.damping, hstep, sms, sis);
                    const double lambda = -m_axis * sms * (dot(spin_between(h), ax) + rate * std::remainder(theta - j.target, 2 * 3.14159265358979)) -
                                          sis * j.pull;
                    j.pull += lambda;
                    twist(h, ax * lambda);
                }
                // The limits: never past them (closed at once from a gap,
                // pushed out softly from within, as a contact is).
                if (j.limit) {
                    for (int side = 0; side < 2; ++side) {
                        const double gap = side ? j.upper - theta : theta - j.lower;
                        const double s = side ? -1.0 : 1.0;
                        double& acc = side ? j.high : j.low;
                        double bias = 0, lms = 1, lis = 0;
                        if (gap > 0) bias = gap / hstep;
                        else if (springs) bias = std::max(bias_rate * gap, -4.0), lms = ms, lis = is;
                        const double lambda = -m_axis * lms * (s * dot(spin_between(h), ax) + bias) - lis * acc;
                        const double total = std::max(acc + lambda, 0.0);
                        twist(h, ax * (s * (total - acc)));
                        acc = total;
                    }
                }
            }
            // The points held together: every way at once.
            {
                const V3 cdot = speed_between(h);
                const V3 c = h.pb - h.pa;
                const M3 xa = skew(h.ra), xb = skew(h.rb);
                const M3 k = M3{} * (h.ma + h.mb) + xa * h.ia * transpose(xa) + xb * h.ib * transpose(xb);
                const V3 imp = inverse(k) * (cdot + c * bias_rate) * -ms - j.point * is;
                j.point = j.point + imp;
                push(h, imp);
            }
        }
    }

    std::set<Inside> inside_;
    std::vector<Inside> entered_, left_;

    // --- through nothing ---------------------------------------------------------------
    // Where each body was when the step began.
    std::vector<V3> from_x_;
    std::vector<M3> from_r_;
    // Each moving body that went far this step for its size - further than
    // half its narrowest - swept from where it was to where it is, turning
    // as it went, against what does not move near its way; stopped where it
    // first comes within touching of one (conservative advancement: each
    // stride no longer than the gap left, at the fastest any of it moves).
    // What it was already touching at the start does not stop it.
    void sweep_fast() {
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            Body& b = bodies[i];
            if (!moves(b) || b.sensor || i >= from_x_.size()) continue;
            if (b.radius < 0) {
                b.radius = 0;
                for (const Hull& h : b.hulls)
                    for (const V3& v : h.v) b.radius = std::max(b.radius, length(v));
            }
            const double radius = b.radius;
            const double narrow = std::min({b.hi.x - b.lo.x, b.hi.y - b.lo.y, b.hi.z - b.lo.z});
            const V3 x0 = from_x_[i], x1 = b.x;
            // Most things move a little: known at once, from where it went
            // and how fast it spins (a bound: at most as far as that).
            if (length(x1 - x0) + (length(b.w) + 1.0) * stepped_ * radius < 0.5 * narrow) continue;
            const M3 r0 = from_r_[i], r1 = b.r;
            const V3 dx = x1 - x0, turn = log_map(r1 * transpose(r0));
            const double angle = length(turn);
            const double reach = length(dx) + angle * radius;
            if (reach < 0.5 * narrow) continue;
            const V3 axis = angle > 1e-12 ? turn * (1.0 / angle) : V3{0, 1, 0};
            const auto pose_at = [&](double t) {
                b.x = x0 + dx * t;
                b.r = angle > 1e-12 ? orthonormal(axis_angle(axis, angle * t) * r0) : r0;
                b.place();
            };
            // Its way, as a box: where it was, where it is, and what its
            // turning sweeps out.
            V3 lo = b.lo, hi = b.hi;
            pose_at(0.0);
            lo = {std::min(lo.x, b.lo.x) - angle * radius, std::min(lo.y, b.lo.y) - angle * radius, std::min(lo.z, b.lo.z) - angle * radius};
            hi = {std::max(hi.x, b.hi.x) + angle * radius, std::max(hi.y, b.hi.y) + angle * radius, std::max(hi.z, b.hi.z) + angle * radius};
            const auto gap_to = [&](const Body& o) {
                double least = 1e18;
                for (std::size_t ha = 0; ha < b.hulls.size(); ++ha)
                    for (std::size_t hb = 0; hb < o.hulls.size(); ++hb)
                        least = std::min(least, apart(b.hulls[ha], b.world[ha], o.hulls[hb], o.world[hb]));
                return least;
            };
            const double near = margin * 0.5;
            double first = 1.0;
            for (const Body& o : bodies) {
                if (&o == &b || moves(o) || o.sensor) continue;
                if (o.hi.x < lo.x || o.lo.x > hi.x || o.hi.y < lo.y || o.lo.y > hi.y || o.hi.z < lo.z || o.lo.z > hi.z) continue;
                double t = 0;
                pose_at(0.0);
                if (gap_to(o) < near) continue;  // touching it already: not a thing it flies into
                for (int k = 0; k < 32 && t < first; ++k) {
                    pose_at(t);
                    const double gap = gap_to(o);
                    if (gap < near) {
                        first = t;
                        break;
                    }
                    t += std::max((gap - near * 0.5) / reach, 1e-4);
                }
            }
            pose_at(first);
            if (first >= 1.0) b.x = x1, b.r = r1, b.place();
        }
    }

    // Looked at this step: what moves by itself, and what is driven.
    static bool moving(const Body& b) { return (b.dynamic() && b.awake) || b.driven; }
    // How far past its box a moving body is looked for: the margin, and as
    // far as it can go this step, so nothing is passed through - a gap is
    // closed no faster than it can be (a contact that may not yet be
    // touching), however long the step.
    double stepped_ = 1.0 / 60.0;
    double looked_for(const Body& b) const { return margin + length(b.v) * stepped_; }

    // A moving body `i` and any other `j`: if `i`'s box, grown by its reach,
    // meets `j`'s, they are a pair. (Of two moving bodies, the one met first
    // by number is the one grown.)
    void consider(std::size_t i, std::size_t j) {
        const Body& a = bodies[i];
        Body& b = bodies[j];
        // Two things neither of which gives: nothing between them to solve.
        if (!moves(a) && !moves(b) && !a.sensor && !b.sensor && !(b.dynamic() && !b.awake)) return;
        const double r = looked_for(a);
        if (b.hi.x < a.lo.x - r || b.lo.x > a.hi.x + r || b.hi.y < a.lo.y - r || b.lo.y > a.hi.y + r || b.hi.z < a.lo.z - r ||
            b.lo.z > a.hi.z + r)
            return;
        if (b.dynamic() && !b.awake) {
            b.place();
            // Something driven into it, or out from under it: it wakes now,
            // and gives this very step.
            if (a.driven) wake(b);
        }
        pair(std::min(i, j), std::max(i, j), r);
    }

    // Every moving body against every other.
    void every_pair() {
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            if (!moving(bodies[i])) continue;
            for (std::size_t j = 0; j < bodies.size(); ++j) {
                if (j == i || (moving(bodies[j]) && j < i)) continue;  // (the pair is met from the other side)
                consider(i, j);
            }
        }
    }

    // Sweep and prune: the bodies in order of where their spans along x
    // begin (a moving one's grown by its reach), kept from step to step and
    // put back in order by insertion - little moves in a step, so that is
    // nearly a pass over them. Going along, those whose spans have not
    // ended yet are open; a body meets only the open ones, and a body at
    // rest only the open ones that move.
    void sweep_pairs() {
        const std::size_t n = bodies.size();
        span_lo_.resize(n);
        span_hi_.resize(n);
        for (std::size_t k = 0; k < n; ++k) {
            const Body& b = bodies[k];
            const double g = moving(b) ? looked_for(b) : 0.0;
            span_lo_[k] = b.lo.x - g, span_hi_[k] = b.hi.x + g;
        }
        const auto before = [&](std::size_t a, std::size_t b) { return span_lo_[a] < span_lo_[b] || (span_lo_[a] == span_lo_[b] && a < b); };
        if (order_.size() != n) {
            order_.resize(n);
            for (std::size_t k = 0; k < n; ++k) order_[k] = k;
            std::sort(order_.begin(), order_.end(), before);
        } else {
            for (std::size_t i = 1; i < n; ++i) {
                const std::size_t v = order_[i];
                std::size_t j = i;
                for (; j > 0 && before(v, order_[j - 1]); --j) order_[j] = order_[j - 1];
                order_[j] = v;
            }
        }
        const auto prune = [&](std::vector<std::size_t>& open, double at) {
            for (std::size_t i = 0; i < open.size();)
                if (span_hi_[open[i]] < at) open[i] = open.back(), open.pop_back();
                else ++i;
        };
        open_moving_.clear();
        open_still_.clear();
        for (std::size_t k : order_) {
            prune(open_moving_, span_lo_[k]);
            prune(open_still_, span_lo_[k]);
            const bool mk = moving(bodies[k]);
            for (std::size_t m : open_moving_) consider(mk ? std::min(m, k) : m, mk ? std::max(m, k) : k);
            if (mk)
                for (std::size_t st : open_still_) consider(k, st);
            (mk ? open_moving_ : open_still_).push_back(k);
        }
    }
    std::vector<std::size_t> order_, open_moving_, open_still_;  // the sweep's scratch
    std::vector<double> span_lo_, span_hi_;

    void pair(std::size_t ia, std::size_t ib, double reach) {
        Body& a = bodies[ia];
        Body& b = bodies[ib];
        if (a.sensor || b.sensor) {
            // Inside it, if no axis parts them at all; nothing is pushed.
            if (a.sensor && b.sensor) return;
            for (std::size_t ha = 0; ha < a.hulls.size(); ++ha)
                for (std::size_t hb = 0; hb < b.hulls.size(); ++hb)
                    if (apart(a.hulls[ha], a.world[ha], b.hulls[hb], b.world[hb]) < 0) {
                        inside_.insert(a.sensor ? Inside{a.id, b.id} : Inside{b.id, a.id});
                        return;
                    }
            return;
        }
        for (std::size_t ha = 0; ha < a.hulls.size(); ++ha)
            for (std::size_t hb = 0; hb < b.hulls.size(); ++hb) {
                const Body::Placed& pa = a.world[ha];
                const Body::Placed& pb = b.world[hb];
                if (pa.hi.x + reach < pb.lo.x || pb.hi.x + reach < pa.lo.x || pa.hi.y + reach < pb.lo.y || pb.hi.y + reach < pa.lo.y ||
                    pa.hi.z + reach < pb.lo.z || pb.hi.z + reach < pa.lo.z)
                    continue;
                V3 n;
                std::array<Touch, 4> t;
                const int k = touch(a.hulls[ha], pa, b.hulls[hb], pb, reach, n, t);
                if (k == 0) continue;
                const uint64_t key = pair_key(ia, ib, static_cast<int>(ha), static_cast<int>(hb));
                Manifold& m = manifolds_[key];
                const bool fresh = m.pts.empty() && !m.live;
                std::vector<Point> old;
                old.swap(m.pts);
                m.a = ia, m.b = ib, m.ha = static_cast<int>(ha), m.hb = static_cast<int>(hb);
                m.n = n;
                const double fa = a.hulls[ha].friction >= 0 ? a.hulls[ha].friction : a.friction;
                const double fb = b.hulls[hb].friction >= 0 ? b.hulls[hb].friction : b.friction;
                m.friction = std::sqrt(fa * fb);
                m.restitution = std::max(a.restitution, b.restitution);
                m.live = true;
                const M3 rat = transpose(a.r);
                for (int i = 0; i < k; ++i) {
                    Point p;
                    p.p = t[static_cast<std::size_t>(i)].p;
                    p.sep = t[static_cast<std::size_t>(i)].sep;
                    p.la = rat * (p.p - a.x);
                    p.id = t[static_cast<std::size_t>(i)].id;
                    // The same point from last step, if there was one: what
                    // it was pushed with then, it is pushed with now.
                    if (!fresh)
                        for (const Point& o : old)
                            if (o.id == p.id) {
                                p.pn = o.pn, p.pt1 = o.pt1, p.pt2 = o.pt2;
                                break;
                            }
                    m.pts.push_back(p);
                }
                // Friction's two directions, kept steady from step to step.
                V3 t1 = std::fabs(n.y) < 0.9 ? cross(n, V3{0, 1, 0}) : cross(n, V3{1, 0, 0});
                t1 = normalize(t1);
                if (!old.empty() && length(m.t1) > 0.5) {
                    // Carry last step's, turned onto this normal.
                    const V3 k1 = m.t1 - n * dot(m.t1, n);
                    if (length(k1) > 0.3) t1 = normalize(k1);
                }
                m.t1 = t1;
                m.t2 = cross(n, t1);
            }
    }

    // Anchors, masses, and the velocities things meet with.
    void prepare(double h) {
        (void)h;
        for (Manifold* m : live_) {
            Body& a = bodies[m->a];
            Body& b = bodies[m->b];
            const V3 ca = a.com(), cb = b.com();
            const M3 ia = moves(a) ? a.inv_inertia() : zero3(), ib = moves(b) ? b.inv_inertia() : zero3();
            const double ma = moves(a) ? a.inv_mass : 0.0, mb = moves(b) ? b.inv_mass : 0.0;
            // Impulses found while one of them slept were found against a
            // thing that could not move - a wall, as far as the other knew:
            // woken, it must not be handed them (a marker on a tray the
            // board was pulled from under would be flung off). Nor the
            // other way. Whatever moves now starts from nothing.
            const int moving = (moves(a) ? 1 : 0) | (moves(b) ? 2 : 0);
            if (m->moving != moving) {
                for (Point& p : m->pts) p.pn = p.pt1 = p.pt2 = 0;
                m->moving = moving;
            }
            const M3 rbt = transpose(b.r);
            for (Point& p : m->pts) {
                p.ra = p.p - ca;
                p.rb = p.p - cb;
                p.lb = rbt * (p.p - b.x);
                const auto eff = [&](V3 d) {
                    const V3 xa = cross(p.ra, d), xb = cross(p.rb, d);
                    const double k = ma + mb + dot(xa, ia * xa) + dot(xb, ib * xb);
                    return k > 1e-12 ? 1.0 / k : 0.0;
                };
                p.mn = eff(m->n), p.mt1 = eff(m->t1), p.mt2 = eff(m->t2);
                const V3 dv = (b.v + cross(b.w, p.rb)) - (a.v + cross(a.w, p.ra));
                p.vn0 = dot(dv, m->n);
                p.most = 0;
            }
        }
        for (Grab& g : grabs_) g.impulse = {}, g.spin = {};
    }

    // What gives when pushed: free, awake, and not being driven.
    static bool moves(const Body& b) { return b.dynamic() && b.awake && !b.driven; }

    void integrate_velocities(double h) {
        for (Body& b : bodies) {
            if (!moves(b)) continue;
            b.v += gravity * h;
            // Air, and the losses that bring everything to rest in the end.
            b.v = b.v * (1.0 / (1.0 + 0.02 * h));
            // Rolling and twisting lose more where it rests on something: a
            // mug on its side rolls to a stop.
            b.w = b.w * (1.0 / (1.0 + (b.contacts ? 0.1 : 0.05) * h));
            // No spin so fast a step turns it past what can be followed.
            const double wl = length(b.w);
            if (wl > 60) b.w = b.w * (60 / wl);
        }
    }

    void apply(Body& a, Body& b, V3 ra, V3 rb, V3 j) {
        if (moves(a)) {
            a.v -= j * a.inv_mass;
            a.w -= a.inv_inertia() * cross(ra, j);
        }
        if (moves(b)) {
            b.v += j * b.inv_mass;
            b.w += b.inv_inertia() * cross(rb, j);
        }
    }

    void warm_start() {
        for (Grab& g : grabs_) g.impulse = {}, g.spin = {};
        warm_joints();
        for (Manifold* m : live_) {
            Body& a = bodies[m->a];
            Body& b = bodies[m->b];
            for (Point& p : m->pts) apply(a, b, p.ra, p.rb, m->n * p.pn + m->t1 * p.pt1 + m->t2 * p.pt2);
        }
    }

    // How far apart a point is now, the bodies having moved since it was found.
    double separation(const Manifold& m, const Point& p) const {
        const Body& a = bodies[m.a];
        const Body& b = bodies[m.b];
        const V3 pa = a.x + a.r * p.la, pb = b.x + b.r * p.lb;
        return p.sep + dot(pb - pa, m.n);
    }

    void solve(double h, bool springs) {
        // Soft contact: a spring of `contact_hertz`, damped ten times over.
        const double zeta = 10.0, omega = 2.0 * 3.14159265358979 * contact_hertz;
        const double a1 = 2.0 * zeta + h * omega, a2 = h * omega * a1, a3 = 1.0 / (1.0 + a2);
        const double bias_rate = omega / a1, mass_scale = a2 * a3, impulse_scale = a3;
        for (int it = 0; it < iterations; ++it) {
            // The hand is a spring itself: pulled with it, not relaxed.
            if (springs) solve_grabs(h);
            solve_joints(h, springs);
            for (Manifold* m : live_) {
                Body& a = bodies[m->a];
                Body& b = bodies[m->b];
                double total = 0;
                for (Point& p : m->pts) {
                    const double s = separation(*m, p);
                    double bias = 0, ms = 1, is = 0;
                    if (s > 0) {
                        bias = s / h;  // a gap: it may be closed, no faster
                    } else if (springs) {
                        bias = std::max(bias_rate * std::min(0.0, s + slop), -max_push);
                        ms = mass_scale, is = impulse_scale;
                    }
                    const V3 dv = (b.v + cross(b.w, p.rb)) - (a.v + cross(a.w, p.ra));
                    const double vn = dot(dv, m->n);
                    double j = -p.mn * ms * (vn + bias) - is * p.pn;
                    const double pn = std::max(p.pn + j, 0.0);
                    j = pn - p.pn;
                    p.pn = pn;
                    p.most = std::max(p.most, pn);
                    total += pn;
                    apply(a, b, p.ra, p.rb, m->n * j);
                }
                for (Point& p : m->pts) {
                    const double limit = m->friction * p.pn;
                    const V3 dv = (b.v + cross(b.w, p.rb)) - (a.v + cross(a.w, p.ra));
                    const double v1 = dot(dv, m->t1), v2 = dot(dv, m->t2);
                    double n1 = p.pt1 - p.mt1 * v1, n2 = p.pt2 - p.mt2 * v2;
                    // Friction is a disc, not a square.
                    const double l = std::sqrt(n1 * n1 + n2 * n2);
                    if (l > limit && l > 0) n1 *= limit / l, n2 *= limit / l;
                    const double j1 = n1 - p.pt1, j2 = n2 - p.pt2;
                    p.pt1 = n1, p.pt2 = n2;
                    apply(a, b, p.ra, p.rb, m->t1 * j1 + m->t2 * j2);
                }
                (void)total;
            }
        }
    }

    void relax(double h) { solve(h, false); }

    void solve_grabs(double h) {
        const bool springs = true;
        for (Grab& g : grabs_) {
            Body* b = find(g.id);
            if (!b || !b->dynamic()) continue;
            const double omega = 2.0 * 3.14159265358979 * g.hertz;
            const double a1 = 2.0 * g.damping + h * omega, a2 = h * omega * a1, a3 = 1.0 / (1.0 + a2);
            const double bias_rate = springs ? omega / a1 : 0.0, ms = springs ? a2 * a3 : 1.0, is = springs ? a3 : 0.0;
            const M3 ii = b->inv_inertia();
            // The point: pulled to the target.
            const V3 r = b->r * (g.local - b->com_local);
            const V3 p = b->com() + r;
            const V3 c = p - g.target;
            const V3 cdot = b->v + cross(b->w, r);
            const M3 rx = skew(r);
            M3 k = M3{} * b->inv_mass + rx * ii * transpose(rx);
            const M3 kinv = inverse(k);
            V3 j = kinv * (cdot + c * bias_rate) * -ms - g.impulse * is;
            V3 total = g.impulse + j;
            const double most = g.force * h;
            if (length(total) > most) total = total * (most / length(total));
            j = total - g.impulse;
            g.impulse = total;
            b->v += j * b->inv_mass;
            b->w += ii * cross(r, j);
            if (g.turns) {
                // And turned: the turn still to make, undone by a spring.
                const V3 err = log_map(g.turn * transpose(b->r)) * -1.0;
                const M3 kinv2 = inverse(ii);
                V3 t = kinv2 * (b->w + err * bias_rate) * -ms - g.spin * is;
                V3 tt = g.spin + t;
                const double most_t = g.torque * h;
                if (length(tt) > most_t) tt = tt * (most_t / length(tt));
                t = tt - g.spin;
                g.spin = tt;
                b->w += ii * t;
            }
        }
    }

    void integrate_positions(double h) {
        for (Body& b : bodies) {
            if (!moves(b) && !b.driven) continue;
            const V3 c = b.com() + b.v * h;
            const double wl = length(b.w);
            if (wl > 1e-12) b.r = orthonormal(axis_angle(b.w * (1.0 / wl), wl * h) * b.r);
            b.x = c - b.r * b.com_local;
        }
    }

    // Bounces, from how fast each point met, once the step is done.
    void restitution() {
        for (Manifold* m : live_) {
            Body& a = bodies[m->a];
            Body& b = bodies[m->b];
            for (Point& p : m->pts) {
                // How hard it struck: for whoever makes the sound.
                if (p.vn0 < -0.4 && p.most > 0) {
                    if (moves(a)) a.hit = std::max(a.hit, -p.vn0);
                    if (moves(b)) b.hit = std::max(b.hit, -p.vn0);
                }
                if (m->restitution <= 0 || p.vn0 > -1.0 || p.most <= 0) continue;
                const V3 dv = (b.v + cross(b.w, p.rb)) - (a.v + cross(a.w, p.ra));
                const double vn = dot(dv, m->n);
                double j = -p.mn * (vn + m->restitution * p.vn0);
                const double pn = std::max(p.pn + j, 0.0);
                j = pn - p.pn;
                p.pn = pn;
                apply(a, b, p.ra, p.rb, m->n * j);
            }
        }
    }

    // Islands: everything moving that touches, through anything else that
    // moves. An island still for long enough sleeps whole.
    void sleep(double dt) {
        std::vector<std::size_t> parent(bodies.size());
        for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
        const auto root = [&](std::size_t i) {
            while (parent[i] != i) i = parent[i] = parent[parent[i]];
            return i;
        };
        for (Manifold* m : live_) {
            const Body& a = bodies[m->a];
            const Body& b = bodies[m->b];
            if (!a.dynamic() || !b.dynamic()) continue;
            bool touching = false;
            for (const Point& p : m->pts) touching |= p.pn > 0 || p.sep < slop;
            if (!touching) continue;
            // Something sleeping that something awake is on or against: awake.
            if (a.awake != b.awake) {
                const Body& moving = a.awake ? a : b;
                if (length(moving.v) > 0.05 || length(moving.w) > 0.2 || moving.grabbed) wake(bodies[a.awake ? m->b : m->a]);
            }
            parent[root(m->a)] = root(m->b);
        }
        // Joined things are one island: a door and its frame, a chain.
        for (const Joint& j : joints) {
            Body* A = find(j.a);
            Body* B = j.b.empty() ? nullptr : find(j.b);
            if (!A) continue;
            // A motor driving it, or a spring not yet where it draws it: it
            // is still on its way, however slowly (a door closing the last
            // degree), and does not sleep.
            if (j.kind == Joint::Hinge && ((j.motor && j.speed != 0.0) ||
                                           (j.spring && std::fabs(std::remainder(angle(j) - j.target, 2 * 3.14159265358979)) > 0.002))) {
                if (A->dynamic()) wake(*A);
                if (B && B->dynamic()) wake(*B);
            }
            if (!B || !A->dynamic() || !B->dynamic()) continue;
            if (A->awake != B->awake) wake(A->awake ? *B : *A);
            parent[root(index_[A->id])] = root(index_[B->id]);
        }
        std::unordered_map<std::size_t, double> least;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            Body& b = bodies[i];
            if (!moves(b)) continue;
            const bool still = length(b.v) < 0.04 && length(b.w) < 0.12 && !b.grabbed;
            b.idle = still ? b.idle + dt : 0.0;
            const std::size_t r = root(i);
            auto it = least.find(r);
            least[r] = it == least.end() ? b.idle : std::min(it->second, b.idle);
        }
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            Body& b = bodies[i];
            if (!moves(b)) continue;
            if (least[root(i)] >= sleep_after) {
                b.awake = false;
                b.v = b.w = {};
            }
        }
    }
};

}  // namespace sg::rigid
