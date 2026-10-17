#include "sg/domains/Face.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

#include "sg/domains/Being.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg {

namespace {

const Key kFaces{"faces"}, kFacesIdle{"faces_idle"}, kSkin{"skin"};

// Its idle picture at its time `time` of n: its time cut into short cells,
// each a toss whether the face changes there - a face held anywhere from a
// moment to a few seconds - and at a change another face than the one
// before, which by the cell's own toss. A function of its time alone.
int idle_at(double time, int n, double cell, double change) {
    const auto toss = [](long long c, unsigned salt) {
        unsigned long long h = static_cast<unsigned long long>(c) * 0x9E3779B97F4A7C15ull + salt * 0xBF58476D1CE4E5B9ull;
        h ^= h >> 31, h *= 0x94D049BB133111EBull, h ^= h >> 29;
        return double(h % 1000003ull) / 1000003.0;
    };
    if (n <= 1 || cell <= 0) return 0;
    const auto changes = [&](long long c) { return c <= 0 || toss(c, 1) < change; };
    const long long now = static_cast<long long>(std::floor(std::max(time, 0.0) / cell));
    long long last = now;
    while (!changes(last)) --last;
    long long before = last - 1;
    while (before > 0 && !changes(before)) --before;
    const auto pick = [&](long long c) { return c <= 0 ? 0 : std::min(n - 1, int(toss(c, 2) * n)); };
    const int f = pick(last), was = pick(before);
    return f == was && last > 0 ? (f + 1) % n : f;
}

// The picture for a face's params at `time`, the mouth `open`.
int shown_at(const Params& p, double time, double open) {
    const int n = std::max(1, static_cast<int>(p.num("idle"))), m = std::max(0, static_cast<int>(p.num("talk")));
    if (m > 0 && open > p.num("speaks", 0.05)) {
        std::vector<double> widths;
        std::istringstream in(p.get_or<std::string>("widths", ""));
        for (double w; in >> w;) widths.push_back(w);
        // (Evenly, unsaid: the nearest of m + 1 openings, shut the first.)
        for (int j = static_cast<int>(widths.size()); j < m; ++j) widths.push_back((j + 0.5) / m);
        int k = 0;
        for (int j = 0; j < m; ++j)
            if (open >= widths[static_cast<std::size_t>(j)]) k = j + 1;
        return k == 0 ? 0 : n + k - 1;
    }
    return idle_at(time, n, p.num("cell", 0.35), p.num("change", 0.38));
}

bool on(const Element& e, const std::string& anchor) { return e.params.get_or<std::string>(keys::parent, "") == anchor; }

bool is_face(const Element& e) {
    const std::string id = e.id.str();
    return id.size() >= 9 && id.compare(id.size() - 9, 9, "skin_face") == 0;
}

// The skins on `anchor` a face is made on: its face, if it has one; else all.
template <class Fn>
void face_skins(Spatial3D& host, Key anchor, Fn fn) {
    const std::string a = anchor.str();
    bool any_face = false;
    for (const Element& e : host.elements()) any_face = any_face || (on(e, a) && is_face(e));
    for (Element& e : host.elements())
        if (on(e, a) && !e.params.has(kFaces) && (!any_face || is_face(e))) fn(e);
}

bool read_picture(const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba) {
    const Being::Files& f = Being::files();
    std::string bytes;
    return f.read && f.decode && f.read(path, bytes) && f.decode(bytes, w, h, rgba);
}

void wear(Element& e, int k) {
    std::istringstream in(e.params.get_or<std::string>(kFaces, ""));
    std::vector<std::string> names;
    for (std::string s; in >> s;) names.push_back(s);
    if (names.empty()) return;
    const std::string& want = names[static_cast<std::size_t>(std::clamp(k, 0, static_cast<int>(names.size()) - 1))];
    if (e.params.get_or<std::string>(kSkin, "") != want) e.params.set(kSkin, want);
}

}  // namespace

Face::Face(Key id, int idle, int talk) : State(std::move(id)) {
    add_element(face_id(), Key{"face"})
        .params.set("idle", static_cast<double>(std::max(1, idle)))
        .set("talk", static_cast<double>(std::max(0, talk)))
        .set("open", 0.0)
        .set("shown", 0.0);
    // face --tick--> face {dt, time}: the picture for now, by its own time
    // (written only when it changes).
    loop(Key{"turn"}, face_id(), tick_event(), [](State&, Element& f, Element*, const Event& ev) {
        if (ev.args.num(keys::dt) <= 0) return;
        const int k = shown_at(f.params, ev.args.num(keys::time), f.params.num("open"));
        if (f.params.num("shown") != k) f.params.set("shown", static_cast<double>(k));
    });
}

int Face::at(double time, double open) const { return shown_at(element(face_id()).params, time, open); }

FaceCount faces_from(Spatial3D& host, Key anchor, const std::string& dir) {
    constexpr int kMostIdle = 32, kMostTalk = 16;
    FaceCount made;
    face_skins(host, anchor, [&](Element& e) {
        const std::string base = e.params.get_or<std::string>(kSkin, "");
        const Spatial3D::Picture* own = base.empty() ? nullptr : host.picture(Key{base});
        if (!own) return;
        const int w = own->w, h = own->h;
        const auto load = [&](const std::string& name) {
            if (host.picture(Key{base + "." + name})) return true;
            int pw = 0, ph = 0;
            std::vector<unsigned char> rgba;
            if (!read_picture(dir + "/" + name + ".png", pw, ph, rgba) || pw != w || ph != h) return false;
            host.picture(Key{base + "." + name}, pw, ph, std::move(rgba));
            return true;
        };
        FaceCount c;
        std::string names;
        while (c.idle < kMostIdle && load("idle" + std::to_string(c.idle))) names += base + ".idle" + std::to_string(c.idle++) + " ";
        while (c.idle > 0 && c.talk < kMostTalk && load("talk" + std::to_string(c.talk + 1))) names += base + ".talk" + std::to_string(++c.talk) + " ";
        if (c.idle == 0) return;
        e.params.set(kFaces, names).set(kFacesIdle, static_cast<double>(c.idle));
        made = c;
    });
    return made;
}

FaceCount mouths_from(Spatial3D& host, Key anchor, const std::string& map, int levels) {
    int mw = 0, mh = 0;
    std::vector<unsigned char> where;
    if (levels < 1 || !read_picture(map, mw, mh, where)) return {};
    FaceCount made;
    face_skins(host, anchor, [&](Element& e) {
        const std::string base = e.params.get_or<std::string>(kSkin, "");
        const Spatial3D::Picture* own = base.empty() ? nullptr : host.picture(Key{base});
        if (!own || own->w != mw || own->h != mh) return;
        std::string names = base + " ";
        for (int k = 1; k <= levels; ++k) {
            // Open as far as a: inside it where the map says less than a (its
            // edge soft over a little of the way) - the dark red of a mouth,
            // and under the upper lip a band of teeth; just outside it, the
            // lips, a little paler (a lip's rim catches the light).
            const double a = static_cast<double>(k) / levels;
            std::vector<unsigned char> rgba = own->rgba;
            for (std::size_t i = 0, n = static_cast<std::size_t>(mw) * mh; i < n; ++i) {
                const unsigned char m = where[i * 4];
                if (m == 255) continue;
                const bool upper = where[i * 4 + 1] > 127;
                const double o = m / 127.0;
                const double in = std::clamp((a - o) / 0.12 + 0.5, 0.0, 1.0);
                const double rim = std::clamp(1.0 - std::fabs(o - a) / 0.35, 0.0, 1.0) * (1.0 - in);
                // (The teeth: the inside's outer third on the upper lip's side.)
                const double teeth = upper ? std::clamp((o - a * 0.55) / (a * 0.2 + 1e-6), 0.0, 1.0) * in : 0.0;
                static constexpr double mouth[3] = {70.0, 14.0, 18.0}, tooth[3] = {205.0, 196.0, 176.0}, lip[3] = {120.0, 92.0, 88.0};
                for (int c = 0; c < 3; ++c) {
                    double v = own->rgba[i * 4 + c];
                    v += (lip[c] - v) * rim * 0.55;
                    const double inside = mouth[c] + (tooth[c] - mouth[c]) * teeth;
                    rgba[i * 4 + c] = static_cast<unsigned char>(std::clamp(v + (inside - v) * in, 0.0, 255.0));
                }
            }
            const std::string name = base + ".mouth" + std::to_string(k);
            host.picture(Key{name}, mw, mh, std::move(rgba));
            names += name + " ";
        }
        e.params.set(kFaces, names).set(kFacesIdle, 1.0);
        made = {1, levels};
    });
    return made;
}

FaceCount face_count(const Spatial3D& host, Key anchor) {
    for (const Element& e : host.elements())
        if (on(e, anchor.str()) && e.params.has(kFaces)) {
            std::istringstream in(e.params.get_or<std::string>(kFaces, ""));
            int n = 0;
            for (std::string s; in >> s;) ++n;
            const int idle = static_cast<int>(e.params.num(kFacesIdle));
            return {idle, n - idle};
        }
    return {};
}

void wear_face(Spatial3D& host, Key anchor, int k) {
    for (Element& e : host.elements())
        if (on(e, anchor.str()) && e.params.has(kFaces)) wear(e, k);
}

bool wears_face(StateGraph& g, const Face& face, Spatial3D& host, Key anchor) {
    Key worn;
    for (const Element& e : host.elements())
        if (on(e, anchor.str()) && e.params.has(kFaces)) worn = e.id;
    if (worn.empty()) return false;
    Functor wears(Key{face.id().str() + ".wears"}, face.id(), host.id());
    wears.on_object(Face::face_id(), worn, [](const Element& f, Element& e) { wear(e, static_cast<int>(f.params.num("shown"))); });
    const Key name = wears.name();
    g.set_functor(std::move(wears));
    g.keep(name);
    g.functor(name)->apply(face, host);
    return true;
}

Functor& voices(StateGraph& g, const Face& face, Key state, Key element, Key param) {
    Functor voice(Key{face.id().str() + ".voice"}, state, face.id());
    voice.on_object(element, Face::face_id(), [param](const Element& v, Element& f) {
        if (f.params.num("open") != v.params.num(param)) f.params.set("open", v.params.num(param));
    });
    const Key name = voice.name();
    g.set_functor(std::move(voice));
    g.keep(name);
    return *g.functor(name);
}

}  // namespace sg
