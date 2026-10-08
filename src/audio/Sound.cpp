#include "sg/audio/Sound.hpp"

#include <algorithm>
#include <set>
#include <sstream>

#include "sg/audio/Curve.hpp"

namespace sg::audio {

namespace {

const char* const kBusNames[kBuses] = {"sfx", "ambient", "music", "voice", "ui"};

// A seam's own word on something, where seams carry params of their own (the
// shared contract: `admits`, `fade`); where they carry none, it says nothing
// and its doorways speak for it.
template <class S>
auto own_text(const S& s, Key k, int) -> decltype(s.params.text(k)) {
    return s.params.text(k);
}
template <class S>
const std::string* own_text(const S&, Key, long) {
    return nullptr;
}
template <class S>
auto own_num(const S& s, Key k, int) -> decltype(s.params.num(k, -1.0)) {
    return s.params.num(k, -1.0);
}
template <class S>
double own_num(const S&, Key, long) {
    return -1.0;
}

const Element* doorway_of(const StateGraph& g, Key state, const std::vector<Key>& boundary) {
    if (boundary.empty()) return nullptr;
    const State* s = g.find(state);
    return s ? s->find(boundary.front()) : nullptr;
}

bool names_word(const std::string& list, const std::string& word) {
    std::istringstream in(list);
    std::string w;
    while (in >> w)
        if (w == word) return true;
    return false;
}

}  // namespace

const char* bus_name(Bus b) { return kBusNames[static_cast<int>(b)]; }

Bus bus_of(const std::string& word, Bus fallback) {
    for (int i = 0; i < kBuses; ++i)
        if (word == kBusNames[i]) return static_cast<Bus>(i);
    return fallback;
}

Key bus_key(Bus b) { return Key{std::string("bus.") + bus_name(b)}; }
Key attend_bus_key(Bus b) { return Key{std::string("attend.bus.") + bus_name(b)}; }

Element& sound_slot(State& host) {
    if (Element* e = host.find(sound_slot_id())) return *e;
    return host.add_element(sound_slot_id(), kinds::slot);
}

const Embedding& wear_sound(StateGraph& g, Key host, Key look) {
    Element& slot = sound_slot(g.state(host));
    if (!slot.params.has(keys::active)) slot.params.set(keys::active, look.str());
    return g.embed(sound_embedding(host, look));
}

Embedding sound_embedding(Key host, Key look) {
    Embedding e;
    e.name = Key{host.str() + "/sound:" + look.str()};
    e.host = host;
    e.portal = sound_slot_id();
    e.guest = look;
    e.sync = EmbedSync::Commit;
    e.focus = false;
    return e;
}

Key active_sound_look(const State& host) {
    const Element* slot = host.find(sound_slot_id());
    if (!slot) return Key{};
    const std::string* a = slot->params.text(keys::active);
    return a && !a->empty() ? Key{*a} : Key{};
}

void set_sound_look(State& host, Key look) { sound_slot(host).params.set(keys::active, look.str()); }

std::vector<Key> in_sound_slot(const StateGraph& g, Key host) {
    std::vector<Key> out;
    for (std::size_t i : g.embeddings_hosted_by(host)) {
        const Embedding& e = g.embeddings()[i];
        if (e.portal == sound_slot_id()) out.push_back(e.guest);
    }
    return out;
}

bool admits(const StateGraph& g, const Seam& s, const std::string& what) {
    if (const std::string* t = own_text(s, keys::admits, 0)) return names_word(*t, what);
    // Either doorway that says what it lets through is heeded: a wall of
    // glass on one side keeps out what the other side would let in.
    for (const Element* d : {doorway_of(g, s.a, s.boundary_a), doorway_of(g, s.b, s.boundary_b)})
        if (d)
            if (const std::string* t = d->params.text(keys::admits))
                if (!names_word(*t, what)) return false;
    return true;
}

double seam_fade(const StateGraph& g, const Seam& s) {
    const double own = own_num(s, keys::fade, 0);
    if (own >= 0.0) return own;
    for (const Element* d : {doorway_of(g, s.a, s.boundary_a), doorway_of(g, s.b, s.boundary_b)})
        if (d && d->params.has(keys::fade)) return std::max(0.0, d->params.num(keys::fade, 1.0));
    return 1.0;
}

std::vector<std::string> sound_defects(const StateGraph& g, const std::function<bool(const std::string&)>& known) {
    std::vector<std::string> out;
    // Every sound names something there is to hear.
    for (Key id : g.ids()) {
        const State* s = g.find(id);
        if (!s) continue;
        for (const Element& e : s->elements()) {
            const std::string* name = e.params.text(keys::sound);
            if (!name) continue;
            if (name->empty() || !known(*name))
                out.push_back("state " + id.str() + ": " + e.id.str() + " sounds as \"" + *name +
                              "\", which is no sound and no stream");
        }
    }
    // Every curve in order, every number a number.
    for (std::string& d : curve_defects(g)) out.push_back(std::move(d));
    // Every place a sound can reach through an opening says how it sounds.
    std::set<Key> places;
    for (const Seam& sm : g.seams()) {
        if (!admits(g, sm, "sound")) continue;
        places.insert(sm.a);
        places.insert(sm.b);
    }
    for (Key p : places) {
        const State* s = g.find(p);
        if (!s) continue;
        const Key look = active_sound_look(*s);
        bool worn = false;
        if (!look.empty() && g.find(look))
            for (Key k : in_sound_slot(g, p)) worn = worn || k == look;
        if (!worn)
            out.push_back("place " + p.str() + " is heard through a seam but wears no sound look" +
                          (look.empty() ? std::string{} : " (its slot names " + look.str() + ")"));
    }
    return out;
}

}  // namespace sg::audio
