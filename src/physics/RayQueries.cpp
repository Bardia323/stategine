#include "sg/physics/Rigid.hpp"
namespace sg::rigid {
const Body* World::ray(V3 o, V3 d, double reach, double* at, bool dynamic_only, const std::string& skip, V3* normal) const {
    const Body* best = nullptr;
    double bt = reach;
    // What the ray's boxes say it may meet, through the index - by number,
    // so what is met first is what every body tried in turn would find.
    std::vector<std::size_t> met;
    if (sweep) {
        refit_index();
        met = broadphase_.query(spatial::Ray{o, d, 0.0, reach});
    } else {
        met.resize(bodies.size());
        for (std::size_t i = 0; i < met.size(); ++i) met[i] = i;
    }
    for (const std::size_t i : met) {
        const Body& b = bodies[i];
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

bool World::hover(const std::string& id, V3 eye, V3 dir, double reach, const M3& turn, V3& com_at, double clear) const {
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

double World::reach_of(const Body& b) {
    double r = 0;
    for (const Hull& h : b.hulls)
        for (const V3& v : h.v) r = std::max(r, length(v - b.com_local));
    return r;
}

V3 World::walk_into(V3 p, double radius, double y0, double y1, V3 moved, double dt, std::vector<std::string>* shoved) {
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

} // namespace sg::rigid
