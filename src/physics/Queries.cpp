#include "sg/physics/Rigid.hpp"
namespace sg::rigid {
const Body* World::cast(const Hull& shape, const M3& turn, V3 from, V3 to, double* at, V3* normal, const std::string& skip) const {
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

void World::walk(Walker& w, V3 move, double dt, double time) {
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
    // The upright walker uses only the vertical component of its gravity response.
    field::Solver fields_at;
    std::vector<field::Source> sources=fields;
    for(const Body& b:bodies) for(auto s:b.fields) {
        s.pose=spatial::Transform{b.r,b.x}*s.pose; sources.push_back(std::move(s));
    }
    fields_at.rebuild(std::move(sources));
    w.vy += fields_at.evaluate(w.at,time,{{"gravity",field::Response::Acceleration,1}}).acceleration.y * dt;
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

std::vector<std::size_t> World::resting_on(const Body& host, double within) const {
    std::vector<std::size_t> out;
    std::vector<char> taken(bodies.size(), 0);
    std::vector<const Body*> below{&host};
    while (!below.empty()) {
        const Body& h = *below.back();
        below.pop_back();
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            const Body& o = bodies[i];
            if (taken[i] || &o == &host || &o == &h || !o.dynamic() || o.sensor) continue;
            if (o.hi.x < h.lo.x || o.lo.x > h.hi.x || o.hi.z < h.lo.z || o.lo.z > h.hi.z || o.lo.y > h.hi.y + within || o.hi.y < h.lo.y) continue;
            bool lies = false;
            for (const Body::Placed& p : o.world) {
                const double top = surface_at(h, p.centre.x, p.centre.z, p.lo.y + within);
                if (top > -0.5 && std::fabs(p.lo.y - top) <= within) {
                    lies = true;
                    break;
                }
            }
            if (!lies) continue;
            taken[i] = 1;
            out.push_back(i);
            below.push_back(&o);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace sg::rigid
