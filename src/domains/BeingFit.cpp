// A being fitted to another: the same height, facing the same way, standing on
// the same floor - a model made anywhere, in any units, facing any way, put
// where a reference body (the engine's own humanoid, say) would be.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>

#include "sg/domains/Being.hpp"
#include "sg/spatial/Math.hpp"

namespace sg {

namespace {

const Key kJoint{"joint"}, kPart{"part"}, kClip{"clip"}, kSkin{"skin"};

// Every line of every clip, rewritten by `fn` (time, joint, the rest of the line).
void rewrite(Being& b, const std::function<std::string(double, const std::string&, const std::string&)>& fn) {
    for (Element& c : b.elements()) {
        if (c.kind != kClip) continue;
        std::istringstream in(c.params.get_or<std::string>("keys", ""));
        std::string out;
        for (std::string line; std::getline(in, line);) {
            std::istringstream ls(line.substr(0, line.find('#')));
            double t;
            std::string j;
            if (!(ls >> t >> j)) continue;
            std::string rest;
            std::getline(ls, rest);
            out += fn(t, j, rest) + "\n";
        }
        c.params.set("keys", out);
    }
}

std::string num(double v) {
    char s[40];
    std::snprintf(s, sizeof s, "%.7g", v);
    return s;
}

bool root(const Element& j) { return j.kind == kJoint && j.params.get_or<std::string>("parent_joint", "").empty(); }

}  // namespace

void Being::extent(double& low, double& high) const {
    // As it is posed now: its skins if it has any, else its parts, else its joints.
    low = 1e18, high = -1e18;
    bool skin = false;
    for (const Element& s : elements()) {
        if (s.kind != kSkin) continue;
        const std::vector<float> tris = skinned(s.id);
        for (std::size_t i = 1; i < tris.size(); i += 8) low = std::min(low, double(tris[i])), high = std::max(high, double(tris[i])), skin = true;
    }
    if (skin) return;
    bool part = false;
    for (const Element& p : elements()) {
        if (p.kind != kPart) continue;
        low = std::min(low, p.params.num("py")), high = std::max(high, p.params.num("py") + p.params.num(keys::sy)), part = true;
    }
    if (part) return;
    for (const Element& j : elements())
        if (j.kind == kJoint) low = std::min(low, j.params.num("py")), high = std::max(high, j.params.num("py"));
    if (low > high) low = high = 0;
}

double Being::height() const {
    double low, high;
    extent(low, high);
    return high - low;
}

Vec3d Being::facing() const {
    // Where its toes are from its ankles - a low joint's children, or what it
    // shows on it, ahead of it - across the floor; else the way a pose
    // faces, x.
    double low, high;
    extent(low, high);
    const double ankle = low + 0.15 * (high - low);
    Vec3d sum{};
    for (const Element& j : elements()) {
        if (j.kind != kJoint || j.params.get_or<std::string>("parent_joint", "").empty() || j.params.num("py") > ankle) continue;
        const Vec3d at{j.params.num("px"), 0, j.params.num("pz")};
        for (const Element& k : elements()) {
            const bool toe = k.kind == kJoint && k.params.get_or<std::string>("parent_joint", "") == j.id.str();
            const bool shoe = k.kind == kPart && k.params.get_or<std::string>("joint", "") == j.id.str();
            if (toe || shoe) sum = sum + Vec3d{k.params.num("px") - at.x, 0, k.params.num("pz") - at.z};
        }
    }
    const double l = std::sqrt(sum.x * sum.x + sum.z * sum.z);
    return l > 1e-6 ? Vec3d{sum.x / l, 0, sum.z / l} : Vec3d{1, 0, 0};
}

double Being::extent_low() const {
    double low, high;
    extent(low, high);
    return low;
}

void Being::resize(double k) {
    if (!(k > 0) || k == 1.0) return;
    // Every length: where each joint stands on its parent, what it shows and
    // where, every travel its clips make, every skin's size.
    for (Element& e : elements()) {
        if (e.kind == kJoint) {
            for (Key c : {keys::x, keys::y, keys::z}) e.params.set(c, e.params.num(c) * k);
            if (e.params.has(Key{"tx"})) for (const char* c : {"tx", "ty", "tz"}) e.params.set(c, e.params.num(c) * k);
        } else if (e.kind == kPart) {
            for (Key c : {keys::sx, keys::sy, keys::sz}) e.params.set(c, e.params.num(c) * k);
            for (const char* c : {"ox", "oy", "oz"}) e.params.set(c, e.params.num(c) * k);
        } else if (e.kind == kSkin) {
            e.params.set("rig", e.params.num("rig", 1.0) * k);
        } else if (e.kind == Key{"goal"}) {
            for (Key c : {keys::x, keys::y, keys::z}) e.params.set(c, e.params.num(c) * k);
        }
    }
    rewrite(*this, [&](double t, const std::string& j, const std::string& rest) {
        std::istringstream r(rest);
        std::string word;
        r >> word;
        if (word != "p") return num(t) + " " + j + rest;
        double x = 0, y = 0, z = 0;
        r >> x >> y >> z;
        return num(t) + " " + j + " p " + num(x * k) + " " + num(y * k) + " " + num(z * k);
    });
    // (A skin is scaled about where it is bound: its joints carry it.)
    resolve();
}

void Being::face(double yaw) {
    if (yaw == 0.0) return;
    // Its roots turned about the up: where they stand, how they are turned at
    // rest and now, and every key of every clip that moves them.
    const spatial::M3 r = spatial::from_euler(yaw, 0, 0);
    double rq[4];
    spatial::to_quat(r, rq[0], rq[1], rq[2], rq[3]);
    const auto turned = [&](double w, double x, double y, double z, double out[4]) {
        out[0] = rq[0] * w - rq[1] * x - rq[2] * y - rq[3] * z;
        out[1] = rq[0] * x + rq[1] * w + rq[2] * z - rq[3] * y;
        out[2] = rq[0] * y - rq[1] * z + rq[2] * w + rq[3] * x;
        out[3] = rq[0] * z + rq[1] * y - rq[2] * x + rq[3] * w;
    };
    std::vector<std::string> roots;
    for (Element& e : elements()) {
        if (!root(e)) continue;
        roots.push_back(e.id.str());
        const spatial::V3 at = r * spatial::V3{e.params.num(keys::x), e.params.num(keys::y), e.params.num(keys::z)};
        e.params.set(keys::x, at.x).set(keys::y, at.y).set(keys::z, at.z);
        double q[4];
        if (!e.params.has(Key{"rest_qw"})) {
            const spatial::M3 m = spatial::from_euler(e.params.num("rest_yaw"), e.params.num("rest_pitch"), e.params.num("rest_roll"));
            double w, x, y, z;
            spatial::to_quat(m, w, x, y, z);
            e.params.set("rest_qw", w).set("rest_qx", x).set("rest_qy", y).set("rest_qz", z);
        }
        turned(e.params.num("rest_qw", 1), e.params.num("rest_qx"), e.params.num("rest_qy"), e.params.num("rest_qz"), q);
        e.params.set("rest_qw", q[0]).set("rest_qx", q[1]).set("rest_qy", q[2]).set("rest_qz", q[3]);
        turned(e.params.num("qw", 1), e.params.num("qx"), e.params.num("qy"), e.params.num("qz"), q);
        e.params.set("qw", q[0]).set("qx", q[1]).set("qy", q[2]).set("qz", q[3]);
    }
    rewrite(*this, [&](double t, const std::string& j, const std::string& rest) {
        if (std::find(roots.begin(), roots.end(), j) == roots.end()) return num(t) + " " + j + rest;
        std::istringstream r2(rest);
        std::string word;
        r2 >> word;
        if (word == "p") {
            double x = 0, y = 0, z = 0;
            r2 >> x >> y >> z;
            const spatial::V3 at = r * spatial::V3{x, y, z};
            return num(t) + " " + j + " p " + num(at.x) + " " + num(at.y) + " " + num(at.z);
        }
        double w = 1, x = 0, y = 0, z = 0;
        if (word == "q") {
            r2 >> w >> x >> y >> z;
        } else {
            double pitch = 0, roll = 0;
            r2 >> pitch >> roll;
            const double d = 3.14159265358979 / 180.0;
            spatial::to_quat(spatial::from_euler(std::atof(word.c_str()) * d, pitch * d, roll * d), w, x, y, z);
        }
        double q[4];
        turned(w, x, y, z, q);
        return num(t) + " " + j + " q " + num(q[0]) + " " + num(q[1]) + " " + num(q[2]) + " " + num(q[3]);
    });
    resolve();
}

void Being::lift(double dy) {
    if (dy == 0.0) return;
    std::vector<std::string> roots;
    for (Element& e : elements())
        if (root(e)) roots.push_back(e.id.str()), e.params.set(keys::y, e.params.num(keys::y) + dy);
    rewrite(*this, [&](double t, const std::string& j, const std::string& rest) {
        std::istringstream r(rest);
        std::string word;
        r >> word;
        if (word != "p" || std::find(roots.begin(), roots.end(), j) == roots.end()) return num(t) + " " + j + rest;
        double x = 0, y = 0, z = 0;
        r >> x >> y >> z;
        return num(t) + " " + j + " p " + num(x) + " " + num(y + dy) + " " + num(z);
    });
    resolve();
}

void fit(Being& model, const Being& reference) {
    // As tall, facing the same way, standing on the same floor.
    const double h = model.height(), want = reference.height();
    if (h > 1e-9 && want > 1e-9) model.resize(want / h);
    const Vec3d f = model.facing(), g = reference.facing();
    // (A turn by yaw takes x toward +z, as from_euler has it.)
    const double yaw = std::atan2(g.z, g.x) - std::atan2(f.z, f.x);
    model.face(yaw);
    // The floor where it will stand: as its clips start it, not as it was
    // bound - a clip that carries its root (Mixamo's hips) stands it at the
    // height the clip says, which its bind pose need not.
    double dy = 0;
    for (const Element& j : model.elements()) {
        if (!root(j)) continue;
        double first = 1e18;
        for (const Element& c : model.elements()) {
            if (c.kind != kClip) continue;
            std::istringstream in(c.params.get_or<std::string>("keys", ""));
            for (std::string line; std::getline(in, line);) {
                std::istringstream ls(line);
                double t, x, y, z;
                std::string name, word;
                if (ls >> t >> name >> word && name == j.id.str() && word == "p" && (ls >> x >> y >> z) && t < first) first = t, dy = y - j.params.num(keys::y);
            }
            if (first < 1e18) break;  // (the first clip that carries it)
        }
        break;
    }
    model.lift(reference.extent_low() - (model.extent_low() + dy));
}

}  // namespace sg
