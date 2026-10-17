#include "sg/domains/Director.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "sg/domains/Spatial.hpp"
#include "sg/domains/Walk.hpp"

namespace sg {

namespace {

const Key kScript{"script"}, kRolling{"rolling"}, kStart{"start"};

int ease_of(const std::string& s) {
    if (s == "smooth") return 1;
    if (s == "in") return 2;
    if (s == "out") return 3;
    if (s == "cut") return 4;
    return s == "linear" ? 0 : -1;
}

// How far along a segment is at u (0..1), by its ease.
double shaped(double u, int ease) {
    switch (ease) {
        case 1: return u * u * (3.0 - 2.0 * u);
        case 2: return u * u;
        case 3: return 1.0 - (1.0 - u) * (1.0 - u);
        case 4: return u >= 1.0 ? 1.0 : 0.0;
        default: return u;
    }
}

// A track's value at t, or nothing before its first key.
bool value_at(const std::vector<Director::Point>& keys, double t, double& v) {
    if (keys.empty() || t < keys.front().t) return false;
    if (t >= keys.back().t) {
        v = keys.back().v;
        return true;
    }
    const auto next = std::upper_bound(keys.begin(), keys.end(), t, [](double x, const Director::Point& p) { return x < p.t; });
    const Director::Point& b = *next;
    const Director::Point& a = *(next - 1);
    const double span = b.t - a.t;
    const double u = span > 0 ? (t - a.t) / span : 1.0;
    v = a.v + (b.v - a.v) * shaped(u, b.ease);
    return true;
}

}  // namespace

Director::Director(Key id, const std::string& script) : State(id) {
    add_element(board_id(), "board").params.set(kScript, script).set(kRolling, 1.0);
    // The script played to the director's own time: a function of the script,
    // the board and the time its line has reached (the event's), so the laws
    // can re-run it. No time passed, nothing changes. The scene's time is the
    // line's since it was rolled (`start`, taken on the first step after).
    loop("play", board_id(), tick_event(), [](State& s, Element& b, Element*, const Event& e) {
        const double dt = e.args.num("dt");
        if (dt == 0.0 || b.params.num(kRolling) == 0.0) return;
        const double t = e.args.num("time");
        if (!b.params.has(kStart)) b.params.set(kStart, t - dt);
        const double start = b.params.num(kStart);
        static_cast<Director&>(s).play(t - dt - start, t - start);
    });
    // Rolled (`on` 1) the scene starts again from its top; cut (`on` 0) it
    // holds where it is, and what it staged is its own.
    loop("roll", board_id(), roll_event(), [](State&, Element& b, Element*, const Event& e) {
        const bool on = e.args.num("on", 1.0) != 0.0;
        b.params.set(kRolling, on ? 1.0 : 0.0);
        if (on) b.params.erase(kStart);
    });
    set_script(script);
}

void Director::set_script(const std::string& text) {
    element(board_id()).params.set(kScript, text);
    for (const auto& c : parsed().cues) says(c.word);
}

std::string Director::script() const {
    const std::string* s = element(board_id()).params.text(kScript);
    return s ? *s : std::string();
}

double Director::length() const { return parsed().length; }

Element& Director::mark(Key name) {
    if (Element* e = find(name)) return *e;
    return add_element(name, "mark");
}

const Director::Parsed& Director::parsed() const {
    const std::string text = script();
    if (parsed_ && text == parsed_text_) return *parsed_;
    auto p = std::make_shared<Parsed>();
    std::istringstream lines(text);
    std::string line;
    int n = 0;
    while (std::getline(lines, line)) {
        ++n;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream w(line);
        std::string ts, who;
        if (!(w >> ts)) continue;
        double t = 0;
        try {
            t = std::stod(ts);
        } catch (...) {
            p->errors.push_back("line " + std::to_string(n) + ": a time first, not '" + ts + "'");
            continue;
        }
        if (!(w >> who)) {
            p->errors.push_back("line " + std::to_string(n) + ": a mark or `say` after the time");
            continue;
        }
        p->length = std::max(p->length, t);
        if (who == "say") {
            std::string word, arg;
            if (!(w >> word)) {
                p->errors.push_back("line " + std::to_string(n) + ": `say` what?");
                continue;
            }
            Parsed::Cue c{t, Key{word}, {}};
            while (w >> arg) {
                const auto eq = arg.find('=');
                if (eq == std::string::npos) {
                    p->errors.push_back("line " + std::to_string(n) + ": '" + arg + "' is not arg=value");
                    continue;
                }
                const std::string k = arg.substr(0, eq), v = arg.substr(eq + 1);
                char* end = nullptr;
                const double d = std::strtod(v.c_str(), &end);
                if (!v.empty() && end && *end == 0) c.args.set(Key{k}, d);
                else c.args.set(Key{k}, v);
            }
            p->cues.push_back(std::move(c));
            continue;
        }
        std::vector<std::pair<std::string, double>> kv;
        int ease = 0;
        std::string pair;
        while (w >> pair) {
            const auto eq = pair.find('=');
            if (eq == std::string::npos) {
                p->errors.push_back("line " + std::to_string(n) + ": '" + pair + "' is not key=value");
                continue;
            }
            const std::string k = pair.substr(0, eq), v = pair.substr(eq + 1);
            if (k == "ease") {
                ease = ease_of(v);
                if (ease < 0) p->errors.push_back("line " + std::to_string(n) + ": no ease '" + v + "'");
                ease = std::max(ease, 0);
                continue;
            }
            try {
                kv.push_back({k, std::stod(v)});
            } catch (...) {
                p->errors.push_back("line " + std::to_string(n) + ": " + k + " is not a number");
            }
        }
        auto& tracks = p->tracks[Key{who}];
        for (const auto& [k, v] : kv) tracks[Key{k}].push_back({t, v, ease});
    }
    for (auto& [mark, tracks] : p->tracks)
        for (auto& [k, keys] : tracks)
            std::stable_sort(keys.begin(), keys.end(), [](const Point& a, const Point& b) { return a.t < b.t; });
    std::stable_sort(p->cues.begin(), p->cues.end(), [](const Parsed::Cue& a, const Parsed::Cue& b) { return a.t < b.t; });
    parsed_ = std::move(p);
    parsed_text_ = text;
    return *parsed_;
}

std::map<Key, Params> Director::at(double t) const {
    static const Key ax{"aim_x"}, ay{"aim_y"}, az{"aim_z"};
    std::map<Key, Params> out;
    for (const auto& [mark, tracks] : parsed().tracks) {
        Params p;
        for (const auto& [k, keys] : tracks) {
            double v = 0;
            if (value_at(keys, t, v)) p.set(k, v);
        }
        if (p.has(ax) && p.has(ay) && p.has(az)) {
            // Looking from where it stands to the aim: the heading sg::forward_of turns back.
            const double dx = p.num(ax) - p.num(keys::x), dy = p.num(ay) - p.num(keys::y), dz = p.num(az) - p.num(keys::z);
            p.set(keys::yaw, std::atan2(dz, dx));
            p.set(keys::pitch, std::atan2(dy, std::hypot(dx, dz)));
        }
        p.erase(ax);
        p.erase(ay);
        p.erase(az);
        if (!p.empty()) out.emplace(mark, std::move(p));
    }
    return out;
}

void Director::play(double from, double to) {
    // (Only marks it has: an arrow never adds an element. One the script
    // names that is not is a fault.)
    // A mark that moved is said moved, and what it is bound to is carried
    // it with the word; one that did not is not, so a thing the script is
    // done with is its own.
    for (auto& [mark, p] : at(to))
        if (Element* m = find(mark)) {
            bool moved = false;
            for (const auto& kv : p)
                if (!m->params.has(kv.first) || m->params.num(kv.first) != p.num(kv.first)) {
                    m->params.set(kv.first, kv.second);
                    moved = true;
                }
            if (moved) emit(moved_event(mark));
        }
    // Each cue passed in (from, to] said once.
    for (const Parsed::Cue& c : parsed().cues)
        if (c.t > from && c.t <= to) emit(Event{c.word, c.args});
}

std::vector<std::string> Director::faults() const {
    std::vector<std::string> out;
    for (const std::string& e : parsed().errors) out.push_back(id().str() + ": " + e);
    for (const auto& [mark, tracks] : parsed().tracks)
        if (!find(mark)) out.push_back(id().str() + ": the script moves '" + mark.str() + "', which is no mark of it");
    return out;
}

Functor& bind(StateGraph& g, Director& d, Key mark, Key state, Key element) {
    d.mark(mark);
    d.says(d.moved_event(mark));
    Functor f{bind_name(d.id(), mark), d.id(), state};
    f.on_object(mark, element, transport::copy_all);
    f.on_event(d.moved_event(mark), Key{state.str() + ".directed"});
    return g.set_functor(std::move(f));
}

Functor& action(StateGraph& g, Director& d, Key state, Key event) {
    Functor f{Key{d.id().str() + ".action." + state.str()}, state, d.id()};
    f.on_event(event, d.roll_event());
    return g.set_functor(std::move(f));
}

Functor& cue(StateGraph& g, Director& d, Key word, Key state, Key event) {
    d.says(word);
    Functor f{Key{d.id().str() + ".cue." + word.str()}, d.id(), state};
    f.on_event(word, event);
    return g.set_functor(std::move(f));
}

// --- coverage -------------------------------------------------------------------------------

bool sees(const Spatial3D& room, const Element& a, const Element& live, const Vec3d& p, double edge) {
    const Vec3d eye = position_of(a), fwd = forward_of(a);
    const Vec3d d = p - eye;
    const double l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    // (Under it, or nearly: not watched, only passed beneath - unless it is
    // one that looks close: `near`.)
    if (std::hypot(d.x, d.z) < a.params.num("near", 1.1)) return false;
    // (Too far: a figure a few lines high is not him seen - unless it is one
    // that looks far: `far`.)
    if (std::hypot(d.x, d.z) > a.params.num("far", 8.0)) return false;
    const double ahead = d.x * fwd.x + d.y * fwd.y + d.z * fwd.z;
    if (ahead < 0.3) return false;
    const double fl = std::max(1e-6, std::hypot(fwd.x, fwd.z));
    const Vec3d right{-fwd.z / fl, 0.0, fwd.x / fl};
    const Vec3d up{-right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y};
    const double aspect = live.params.num("feed_w", 4.0) / std::max(1.0, live.params.num("feed_h", 3.0));
    // (Its width is its angle, its picture cut to the set's shape top and
    // bottom; a little in from its edges: a man at the edge is half out of it.)
    const double tv = std::tan(a.params.num(keys::fov, 60.0) * 0.5 * 3.14159265358979323846 / 180.0) * edge;
    if (std::fabs((d.x * right.x + d.z * right.z) / ahead) > tv) return false;
    if (std::fabs((d.x * up.x + d.y * up.y + d.z * up.z) / ahead) > tv / aspect) return false;
    // Nothing solid between (from a little out of the camera, to a little short of it).
    Vec3d hit, n;
    double dist = 0;
    const Vec3d dir = d * (1.0 / std::max(1e-6, l));
    return !(ray(room, eye + dir * 0.25, dir, l - 0.25, hit, n, &dist) && dist < l - 0.25 - 0.45);
}

bool cover(Spatial3D& room, const std::string& prefix, const Vec3d& feet, double going, double time) {
    Element* live = room.find(Key{prefix});
    if (!live) return false;
    const Vec3d& f = feet;
    const Vec3d go = heading(going);
    // (Each camera where it is for him now: hung, or carried along with him.)
    const auto posed = [&](const Element& a) {
        Element c = a;
        const std::string axis = a.params.get_or<std::string>("track", "");
        if (axis.empty()) return c;
        const double v = std::clamp((axis == "x" ? f.x : f.z) + a.params.num("track_off"), a.params.num("track_lo"), a.params.num("track_hi"));
        c.params.set(axis == "x" ? keys::x : keys::z, v);
        return c;
    };
    const auto him = [&](const Element& a, double edge) {
        // (What of him it frames, `frames`: his feet, his head and shoulders, him
        // from the chest up, or all of him.)
        const std::string what = a.params.get_or<std::string>("frames", "");
        const double lo = what == "feet" ? 0.1 : what == "head" ? 1.62 : what == "chest" ? 1.15 : 0.5, hi = what == "feet" ? 0.3 : what == "head" ? 1.75 : 1.6;
        return sees(room, a, *live, f + Vec3d{0, lo, 0}, edge) && sees(room, a, *live, f + Vec3d{0, hi, 0}, edge);
    };
    const auto his_way = [&](const Element& a) {
        if (!a.params.has(Key{"going"})) return true;
        const Vec3d w = heading(a.params.num("going"));
        return go.x * w.x + go.z * w.z > 0.7;
    };
    // (Where he goes, in the picture too: for a hung one that sees all of
    // him; one carried with him goes there with him, one at his feet sees
    // them go.)
    const Vec3d to = f + go * 1.6 + Vec3d{0, 0.7, 0};
    const auto ahead = [&](const Element& a) {
        return a.params.has(Key{"track"}) || a.params.get_or<std::string>("frames", "") == "feet" || sees(room, a, *live, to);
    };
    const int was = static_cast<int>(live->params.num("cam", -1.0));
    const auto cam = [&](int k) { return room.find(Key{prefix + std::to_string(k)}); };
    bool keep = false;
    if (was >= 0) {
        // (Kept while he is well inside its picture, and where he goes is in it too.)
        const Element a = posed(*cam(was));
        keep = (him(a, 0.55) && ahead(a) && his_way(a)) || time - live->params.num("cut_at", -99.0) < 0.6;
    }
    int pick = was;
    if (!keep) {
        double best = -1e9;
        int found = -1;
        for (int k = 0; k < 32; ++k) {
            const Element* hung = cam(k);
            if (!hung) break;
            const Element a = posed(*hung);
            if (!him(a, 0.88) || !his_way(a)) continue;
            // Both, first; then the one looking the way he goes (behind him);
            // the one already watching, if it is as good.
            const Vec3d fw = forward_of(a);
            const double behind = (fw.x * go.x + fw.z * go.z) / std::max(1e-6, std::hypot(fw.x, fw.z));
            const double score = (a.params.num("first") > 0.5 ? 100.0 : 0.0) + (ahead(a) ? 10.0 : 0.0) + behind + (k == was ? 0.5 : 0.0);
            if (score > best) best = score, found = k;
        }
        // (Seeing only him, not where he goes: kept by the one that has him, unless another sees both.)
        const bool still_has = was >= 0 && him(posed(*cam(was)), 0.88) && his_way(*cam(was));
        if (found >= 0 && found != was && !(still_has && best < 10.0)) pick = found, live->params.set("cut_at", time);
    }
    if (pick < 0) return false;
    if (pick != was) live->params.set("shake_zoom", 1.0);  // (cut: as it hangs, not as close as the last was brought)
    const Element a = posed(*cam(pick));
    const double fov = a.params.num(keys::fov) * live->params.num("shake_zoom", 1.0);
    const Vec3d at = position_of(a) + Vec3d{live->params.num("shake_x"), live->params.num("shake_y"), live->params.num("shake_z")};
    const double yaw = a.params.num(keys::yaw) + live->params.num("shake_yaw"), pitch = a.params.num(keys::pitch) + live->params.num("shake_pitch");
    if (live->params.num(keys::x) != at.x || live->params.num(keys::y) != at.y || live->params.num(keys::z) != at.z || live->params.num(keys::yaw) != yaw || live->params.num(keys::pitch) != pitch || live->params.num(keys::fov) != fov || pick != was) {
        set_position(*live, at);
        live->params.set(keys::yaw, yaw).set(keys::pitch, pitch).set(keys::fov, fov).set("cam", static_cast<double>(pick));
    }
    return true;
}

}  // namespace sg
