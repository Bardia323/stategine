#include "sg/domains/Director.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "sg/domains/Spatial.hpp"

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

}  // namespace sg
