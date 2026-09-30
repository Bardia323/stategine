#include "sg/physics/Rigid.hpp"
namespace sg::rigid {
namespace detail {

bool minkowski_face(V3 a, V3 b, V3 c, V3 d) {
    // Do the arcs a-b and c-d cross on the sphere of directions?
    const V3 bxa = cross(b, a), dxc = cross(d, c);
    const double cba = dot(c, bxa), dba = dot(d, bxa), adc = dot(a, dxc), bdc = dot(b, dxc);
    return cba * dba < 0 && adc * bdc < 0 && cba * bdc > 0;
}

void closest(V3 p1, V3 q1, V3 p2, V3 q2, V3& c1, V3& c2) {
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

} // namespace detail

int touch(const Hull& ha, const Body::Placed& pa, const Hull& hb, const Body::Placed& pb, double margin, V3& normal, std::array<Touch, 4>& out) {
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

double apart(const Hull& ha, const Body::Placed& pa, const Hull& hb, const Body::Placed& pb) {
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

std::vector<std::size_t> outline(const std::vector<PartBox>& parts, std::size_t most, double margin, double small) {
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

double ray_hull(const Body::Placed& p, V3 o, V3 d, double reach, V3* normal) {
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

double surface_at(const Body& b, double x, double z, double below) {
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

bool ride(const Body& b, V3 x0, const M3& r0, V3 v0, double dt, V3& p, M3& face, V3 up_local, V3* let_go_v) {
    const M3 turn = b.r * transpose(r0);
    p = b.x + turn * (p - x0);
    face = turn * face;
    const V3 dv = b.v - v0;
    const double shove = std::sqrt(dv.x * dv.x + dv.z * dv.z) / std::max(dt, 1e-4);
    if ((face * up_local).y > std::cos(0.45) && shove < 0.55 * 9.81) return true;
    if (let_go_v) *let_go_v = b.v + cross(b.w, p - b.com());
    return false;
}

} // namespace sg::rigid
