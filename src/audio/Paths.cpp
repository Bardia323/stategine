#include "sg/audio/Paths.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>

#include "sg/audio/Sound.hpp"
#include "sg/domains/Room.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/domains/Walk.hpp"

namespace sg::audio {

namespace {

constexpr double kFar = std::numeric_limits<double>::infinity();

// How far a way is from going straight through an opening facing `n`: 0
// head on, pi/2 along its face.
double off_straight(const Vec3d& way, const Vec3d& n) {
    const double l = length(way), ln = length(n);
    if (l < 1e-9 || ln < 1e-9) return 0.0;
    const double c = std::fabs(way.x * n.x + way.y * n.y + way.z * n.z) / (l * ln);
    return std::acos(std::clamp(c, 0.0, 1.0));
}

const std::map<std::string, Acoustic>& table() {
    // Absorption by band (as a room's reverb meets it) and what a wall of it
    // lets through, as a gain (a plaster partition takes ~30 dB off the middle).
    static const std::map<std::string, Acoustic> t = {
        {"plaster", {{0.02, 0.03, 0.05}, {0.1, 0.03, 0.01}}},
        {"stone", {{0.02, 0.03, 0.05}, {0.03, 0.01, 0.003}}},
        {"rubble", {{0.03, 0.04, 0.06}, {0.03, 0.01, 0.003}}},
        {"cobble", {{0.03, 0.04, 0.06}, {0.03, 0.01, 0.003}}},
        {"brick", {{0.02, 0.03, 0.05}, {0.04, 0.012, 0.004}}},
        {"concrete", {{0.01, 0.02, 0.03}, {0.03, 0.01, 0.003}}},
        {"marble", {{0.01, 0.01, 0.02}, {0.03, 0.01, 0.003}}},
        {"tile", {{0.01, 0.01, 0.02}, {0.05, 0.02, 0.006}}},
        {"planks", {{0.15, 0.1, 0.08}, {0.2, 0.08, 0.03}}},
        {"floorboards", {{0.15, 0.1, 0.08}, {0.2, 0.08, 0.03}}},
        {"wood", {{0.15, 0.1, 0.08}, {0.2, 0.08, 0.03}}},
        {"bark", {{0.1, 0.1, 0.1}, {0.2, 0.08, 0.03}}},
        {"glass", {{0.2, 0.06, 0.03}, {0.2, 0.1, 0.05}}},
        {"ice", {{0.02, 0.03, 0.04}, {0.05, 0.02, 0.008}}},
        {"iron", {{0.02, 0.03, 0.04}, {0.05, 0.02, 0.01}}},
        {"rust", {{0.03, 0.04, 0.05}, {0.05, 0.02, 0.01}}},
        {"metal", {{0.02, 0.03, 0.04}, {0.05, 0.02, 0.01}}},
        {"paintedmetal", {{0.02, 0.03, 0.04}, {0.05, 0.02, 0.01}}},
        {"gold", {{0.02, 0.03, 0.04}, {0.05, 0.02, 0.01}}},
        {"hull", {{0.02, 0.03, 0.04}, {0.03, 0.01, 0.005}}},
        {"slate", {{0.02, 0.03, 0.05}, {0.05, 0.02, 0.008}}},
        {"rooftiles", {{0.02, 0.03, 0.05}, {0.08, 0.03, 0.01}}},
        {"fabric", {{0.1, 0.4, 0.6}, {0.6, 0.4, 0.2}}},
        {"linen", {{0.08, 0.3, 0.5}, {0.6, 0.4, 0.2}}},
        {"carpet", {{0.05, 0.3, 0.6}, {0.5, 0.3, 0.15}}},
        {"paper", {{0.1, 0.2, 0.3}, {0.8, 0.6, 0.4}}},
        {"sand", {{0.15, 0.4, 0.6}, {0.02, 0.006, 0.002}}},
        {"earth", {{0.15, 0.4, 0.55}, {0.02, 0.006, 0.002}}},
        {"grass", {{0.2, 0.5, 0.6}, {0.02, 0.006, 0.002}}},
        {"snow", {{0.4, 0.7, 0.9}, {0.02, 0.006, 0.002}}},
        // Nothing there: the open sky over a court takes everything and sends nothing back.
        {"open", {{1.0, 1.0, 1.0}, {1.0, 1.0, 1.0}}},
    };
    return t;
}

}  // namespace

Bands air(double metres) {
    const double m = std::max(0.0, metres);
    return {std::exp(-0.0002 * m), std::exp(-0.0017 * m), std::exp(-0.0182 * m)};
}

const Acoustic& acoustic(const std::string& material) {
    const auto& t = table();
    auto it = t.find(material);
    return it == t.end() ? t.at("plaster") : it->second;
}

bool known_acoustic(const std::string& material) { return table().count(material) != 0; }

Reverberation eyring(double volume, const std::vector<std::pair<double, Acoustic>>& surfaces) {
    Reverberation r;
    double s = 0.0;
    Bands sum{0.0, 0.0, 0.0};
    for (const auto& kv : surfaces) {
        const double a = std::max(0.0, kv.first);
        s += a;
        sum.low += a * kv.second.absorb.low;
        sum.mid += a * kv.second.absorb.mid;
        sum.high += a * kv.second.absorb.high;
    }
    r.volume = volume;
    r.surface = s;
    if (volume <= 0.0 || s <= 0.0) return r;
    const auto t60 = [&](double absorbed) {
        const double mean = std::clamp(absorbed / s, 1e-4, 0.999);
        return std::clamp(0.161 * volume / (-s * std::log(1.0 - mean)), 0.05, 20.0);
    };
    r.t60 = {t60(sum.low), t60(sum.mid), t60(sum.high)};
    return r;
}

bool reverberation_of(const State& place, const std::string& floor, const std::string& walls, const std::string& ceiling,
                      Reverberation& out) {
    const auto* room = dynamic_cast<const Room*>(&place);
    if (!room) return false;
    const plan::Outline ol = room->outline();
    if (ol.poly.size() < 3) return false;
    // The floor's area and the length round it, from its own edge.
    double area = 0.0, round = 0.0;
    for (std::size_t i = 0; i < ol.poly.size(); ++i) {
        const plan::P2& a = ol.poly[i];
        const plan::P2& b = ol.poly[(i + 1) % ol.poly.size()];
        area += a.x * b.z - b.x * a.z;
        round += std::hypot(b.x - a.x, b.z - a.z);
    }
    area = std::fabs(area) * 0.5;
    const double h = room->h();
    out = eyring(area * h, {{area, acoustic(floor)}, {area, acoustic(ceiling)}, {round * h, acoustic(walls)}});
    return true;
}

Bands through(double passes) {
    const double p = std::clamp(passes, 0.0, 1.0);
    return {std::sqrt(p), p, p * p};
}

Bands bend(double angle) {
    const double s = std::sin(std::clamp(angle, 0.0, 1.5707963267948966));
    const double s2 = s * s;
    return {1.0, 1.0 - 0.12 * s2, 1.0 - 0.45 * s2};
}

void Paths::build(const StateGraph& g, Key ear_place, const Vec3d& ear) {
    // Which openings there are changes only with the graph.
    if (g.revision() != revision_) {
        revision_ = g.revision();
        openings_.clear();
        sides_of_.clear();
        for (std::size_t si = 0; si < g.seams().size(); ++si) {
            const Seam& s = g.seams()[si];
            const std::size_t n = std::min(s.boundary_a.size(), s.boundary_b.size());
            for (std::size_t i = 0; i < n; ++i) {
                Opening o;
                o.seam = s.name;
                o.seam_index = si;
                o.place[0] = s.a;
                o.place[1] = s.b;
                o.portal[0] = s.boundary_a[i];
                o.portal[1] = s.boundary_b[i];
                openings_.push_back(o);
            }
        }
        for (std::size_t i = 0; i < openings_.size(); ++i)
            for (int k = 0; k < 2; ++k) sides_of_[openings_[i].place[k]].push_back(static_cast<int>(2 * i) + k);
        found_ = false;
    }
    // What an opening is read from: its two doorways and the seam's first
    // (what says how much it passes), and what each hangs on - by their
    // stamps, so an opening nothing touched is not read again.
    const auto stamp_of = [&](uint64_t h, Key place, Key portal) {
        const State* s = g.find(place);
        const Element* e = s ? s->find(portal) : nullptr;
        for (int k = 0; k < 8 && e; ++k) {
            h = mix_stamp(h, (e->params.stamp() << 1) | (e->alive ? 1u : 0u));
            const std::string* parent = e->params.text(sg::keys::parent);
            e = parent && !parent->empty() ? s->find(Key{*parent}) : nullptr;
        }
        return mix_stamp(h, s ? 1u : 0u);
    };
    bool moved = !found_;
    // Where each is now, and how open: a doorway moves with what it hangs on,
    // and a door swings.
    for (Opening& o : openings_) {
        const Seam& seam = g.seams()[o.seam_index];
        uint64_t stamp = stamp_of(stamp_of(0x9e3779b97f4a7c15ull, o.place[0], o.portal[0]), o.place[1], o.portal[1]);
        if (!seam.boundary_a.empty() && !seam.boundary_b.empty())
            stamp = stamp_of(stamp_of(stamp, seam.a, seam.boundary_a.front()), seam.b, seam.boundary_b.front());
        if (stamp == o.stamp) continue;
        o.stamp = stamp;
        moved = true;
        // What a seam lets through, and how much of it, is the graph's to say
        // (its doorways' `admits`, `opening`, `muffle`), and may change as they do.
        o.aperture = g.passes(seam, Channel::Sound);
        o.live = o.aperture > 0.0;
        if (!o.live) continue;
        o.fade = g.fade(seam);
        for (int k = 0; k < 2 && o.live; ++k) {
            const State* s = g.find(o.place[k]);
            const Element* e = s ? s->find(o.portal[k]) : nullptr;
            if (!e || !e->alive) {
                o.live = false;
                break;
            }
            const Pose p = world_pose(*s, *e);
            o.pos[k] = p.position;
            o.normal[k] = turn(p, Vec3d{1, 0, 0});
        }
    }

    // Nothing moved, the ear where it was: the ways are as they were found.
    if (!moved && ear_place == ear_place_ && ear.x == ear_.x && ear.y == ear_.y && ear.z == ear_.z) return;
    found_ = true;
    ear_place_ = ear_place;
    ear_ = ear;
    landed_.clear();
    nearest_.clear();
    const std::size_t sides = openings_.size() * 2;
    std::vector<double> dist(sides, kFar);
    std::vector<Landing> best(sides);
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;

    // From the ear, across each opening of the place it is in.
    if (auto it = sides_of_.find(ear_place); it != sides_of_.end()) {
        for (int sd : it->second) {
            const Opening& o = openings_[static_cast<std::size_t>(sd / 2)];
            if (!o.live) continue;
            const int k = sd % 2, land = 2 * (sd / 2) + (1 - k);
            const Vec3d way = o.pos[k] - ear;
            const double d = length(way);
            if (d >= dist[static_cast<std::size_t>(land)]) continue;
            Landing l;
            l.opening = sd / 2;
            l.side = 1 - k;
            l.dist = d;
            l.bands = through(o.aperture) * bend(off_straight(way, o.normal[k]));
            l.first = unit(way);
            dist[static_cast<std::size_t>(land)] = d;
            best[static_cast<std::size_t>(land)] = l;
            queue.push({d, land});
        }
    }
    // And on, across each place, through each opening of it.
    while (!queue.empty()) {
        const Item top = queue.top();
        queue.pop();
        const int at = top.second;
        if (top.first > dist[static_cast<std::size_t>(at)]) continue;
        const Landing here = best[static_cast<std::size_t>(at)];
        const Opening& o = openings_[static_cast<std::size_t>(here.opening)];
        const Key place = o.place[here.side];
        landed_[place].push_back(here);
        auto jt = sides_of_.find(place);
        if (jt == sides_of_.end()) continue;
        for (int sd : jt->second) {
            if (sd / 2 == here.opening) continue;
            const Opening& o2 = openings_[static_cast<std::size_t>(sd / 2)];
            if (!o2.live) continue;
            const int k2 = sd % 2, land = 2 * (sd / 2) + (1 - k2);
            const Vec3d way = o2.pos[k2] - o.pos[here.side];
            const double d = here.dist + length(way);
            if (d >= dist[static_cast<std::size_t>(land)]) continue;
            Landing l = here;
            l.opening = sd / 2;
            l.side = 1 - k2;
            l.dist = d;
            l.bands = here.bands * bend(off_straight(way, o.normal[here.side])) * through(o2.aperture) *
                      bend(off_straight(way, o2.normal[k2]));
            dist[static_cast<std::size_t>(land)] = d;
            best[static_cast<std::size_t>(land)] = l;
            queue.push({d, land});
        }
    }
    for (const auto& kv : landed_) {
        double nearest = kFar;
        for (const Landing& l : kv.second) nearest = std::min(nearest, l.dist);
        nearest_[kv.first] = nearest;
    }
}

Route Paths::to(Key place, const Vec3d& at) const {
    Route r;
    auto it = landed_.find(place);
    if (it == landed_.end()) return r;
    double best = kFar;
    for (const Landing& l : it->second) {
        const Opening& o = openings_[static_cast<std::size_t>(l.opening)];
        const Vec3d way = at - o.pos[l.side];
        const double d = l.dist + length(way);
        if (d >= best) continue;
        best = d;
        r.heard = true;
        r.length = d;
        r.bands = l.bands * bend(off_straight(way, o.normal[l.side]));
        r.toward = l.first;
    }
    return r;
}

Route Paths::to(Key place) const {
    Route r;
    auto it = landed_.find(place);
    if (it == landed_.end()) return r;
    double best = kFar;
    for (const Landing& l : it->second) {
        if (l.dist >= best) continue;
        best = l.dist;
        r.heard = true;
        r.length = l.dist;
        r.bands = l.bands;
        r.toward = l.first;
    }
    return r;
}

double Paths::fade_between(Key a, Key b) const {
    for (const Opening& o : openings_)
        if ((o.place[0] == a && o.place[1] == b) || (o.place[0] == b && o.place[1] == a)) return o.fade;
    return 1.0;
}

Bands occlusion(const State& place, const Vec3d& from, const Vec3d& to) {
    Bands out;
    const Vec3d d = to - from;
    const double whole = length(d);
    if (whole < 0.05) return out;
    const Vec3d dir = d * (1.0 / whole);
    Vec3d at = from;
    double left = whole;
    for (int i = 0; i < 4 && left > 0.05; ++i) {
        Vec3d hit, normal;
        double dist = 0.0;
        Key what;
        if (!ray(place, at, dir, left, hit, normal, &dist, &what)) break;
        if (dist >= left - 0.05) break;
        const Element* e = place.find(what);
        if (e && e->kind == Key{"wall"}) {
            const std::string* m = e->params.text(Key{"material"});
            if (!m) m = e->params.text(Key{"mat"});
            out = out * acoustic(m ? *m : std::string("plaster")).transmit;
        }
        // On from just inside it: a ray that starts in a solid passes out of it.
        at = at + dir * (dist + 0.01);
        left -= dist + 0.01;
    }
    return out;
}

}  // namespace sg::audio
