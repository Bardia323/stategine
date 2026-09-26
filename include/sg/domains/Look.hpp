// Stategine - Look: how a state is shown, as a state of its own.
//
// A look is a state whose elements are the passes of a renderer - shadow,
// scene, bright, blur, composite - and whose parameters are what those passes
// read. A parameter whose name starts with `u` is a shader uniform; `.x/.y/.z`
// components make a vector (`uFogColor.x`, `.y`, `.z`). Anything else is a
// setting the renderer reads itself (`passes`, `clear.x`). A pass may carry its
// own shader source in `fs` (and `vs`); without one it uses the renderer's.
//
// Nothing here knows about GL. A look describes; a renderer interprets, the
// same way it interprets a room.
//
// A state *wears* a look through an embedding into its `look` element - the
// ordinary way one state lives inside another - so looks are reachable in the
// graph, drawn in its DOT, and checked by `validate` like everything else. A
// state may wear several and switch between them by setting which is active;
// the renderer fades between looks over the incoming look's `fade` seconds.
//
//   auto& calm = graph.add<sg::LookState>("calm");
//   auto& alert = graph.add<sg::LookState>("alert");
//   alert.uniform(sg::passes::composite, "uTint", 1.0, 0.45, 0.4).fade(0.4);
//   sg::wear(graph, "hall", "calm");     // the first worn is active
//   sg::wear(graph, "hall", "alert");
//   sg::set_look(hall, "alert");         // fades from calm to alert
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/StateGraph.hpp"

namespace sg {

namespace kinds {
inline const Key look{"look"};            // a look state
inline const Key pass{"pass"};            // one of its passes
inline const Key look_slot{"look_slot"};  // where a state wears its looks
}  // namespace kinds

// The passes a look describes, in the order a frame runs them.
namespace passes {
inline const Key shadow{"shadow"}, scene{"scene"}, bright{"bright"}, blur{"blur"},
    composite{"composite"};

inline const std::vector<Key>& all() {
    static const std::vector<Key> list{shadow, scene, bright, blur, composite};
    return list;
}
}  // namespace passes

namespace look_keys {
inline const Key vs{"vs"}, fs{"fs"};  // shader sources, per pass
inline const Key fade{"fade"};        // seconds to fade in, on the look state
inline const Key active{"active"};    // which worn look is shown, on the slot
}  // namespace look_keys

inline bool is_uniform_key(Key k) { return !k.str().empty() && k.str()[0] == 'u'; }

class LookState : public State {
public:
    explicit LookState(Key id) : State(id) {
        for (Key p : passes::all()) add_element(p, kinds::pass);
        params().set(look_keys::fade, 0.6);
    }

    Key kind() const override { return kinds::look; }

    // A uniform the pass's shader reads.
    LookState& uniform(Key pass, const std::string& name, double v) {
        element(pass).params.set(Key{name}, v);
        return *this;
    }

    LookState& uniform(Key pass, const std::string& name, double x, double y, double z) {
        Params& p = element(pass).params;
        p.set(Key{name + ".x"}, x).set(Key{name + ".y"}, y).set(Key{name + ".z"}, z);
        return *this;
    }

    // A knob the renderer reads itself, not a uniform.
    LookState& setting(Key pass, const std::string& name, double v) {
        element(pass).params.set(Key{name}, v);
        return *this;
    }

    // Replace a pass's shaders. An empty vertex shader keeps the renderer's.
    LookState& shader(Key pass, std::string fs, std::string vs = {}) {
        element(pass).params.set(look_keys::fs, std::move(fs));
        if (!vs.empty()) element(pass).params.set(look_keys::vs, std::move(vs));
        return *this;
    }

    LookState& fade(double seconds) {
        params().set(look_keys::fade, seconds);
        return *this;
    }

    double fade_seconds() const { return params().num(look_keys::fade, 0.6); }
};

// --- wearing a look ---------------------------------------------------------------
inline Key look_slot_id() { return Key{"look"}; }

// `host` may be shown with `look`. The first look a state wears is its active one.
inline const Embedding& wear(StateGraph& g, Key host, Key look) {
    State& h = g.state(host);
    Element* slot = h.find(look_slot_id());
    if (!slot) slot = &h.add_element(look_slot_id(), kinds::look_slot);
    if (!slot->params.has(look_keys::active)) slot->params.set(look_keys::active, look.str());
    return g.embed(Key{host.str() + "/look:" + look.str()}, host, look_slot_id(), look, Key{},
                   Key{}, EmbedSync::Commit);
}

// Show `host` with another look it wears. A parameter write, so an arrow can
// do it as well as a caller.
inline void set_look(State& host, Key look) {
    host.element(look_slot_id()).params.set(look_keys::active, look.str());
}

inline Key active_look(const State& s) {
    const Element* slot = s.find(look_slot_id());
    if (!slot) return Key{};
    return Key{slot->params.get_or<std::string>(look_keys::active, "")};
}

inline std::vector<Key> worn_looks(const StateGraph& g, Key host) {
    std::vector<Key> out;
    for (const Embedding& e : g.embeddings())
        if (e.host == host && e.portal == look_slot_id()) out.push_back(e.guest);
    return out;
}

// --- changing looks ------------------------------------------------------------------
// What is on screen for one thing: a blend of looks, as weights that sum to
// one. A settled thing has one look at weight 1.
//
// A change of look is not a cut followed by a fade from somewhere else; it is
// weight flowing, a little each frame, towards the look now wanted. The blend
// is the state, and it only ever moves continuously. So a change that is
// undone half way flows straight back along the path it came - from 50% to 0%,
// never via a jump - and a change to a third look leaves from the blend on
// screen, not from either end of it. Forward and back are the same path
// walked in opposite directions: `advance(d)` towards B then `advance(d)`
// back towards A is exactly where it started.
struct LookMix {
    struct Part {
        const LookState* look;
        float weight;
    };
    std::vector<Part> parts;

    float weight(const LookState* l) const {
        for (const Part& p : parts)
            if (p.look == l) return p.weight;
        return 0.0f;
    }

    bool settled() const { return parts.size() == 1; }

    // Shaders cannot be averaged. A pass that cannot dissolve between programs
    // uses the look with the most weight.
    const LookState& shown() const {
        const Part* best = &parts.front();
        for (const Part& p : parts)
            if (p.weight > best->weight) best = &p;
        return *best->look;
    }

    // Move `step` of the way (in weight) towards `target`. Every other look
    // gives up weight in proportion to what it holds, so the blend travels in
    // a straight line towards the target and retraces it when turned back.
    void toward(const LookState& target, float step) {
        float held = weight(&target);
        if (held == 0.0f) parts.push_back(Part{&target, 0.0f});
        const float next = std::min(1.0f, held + step);
        const float rest = 1.0f - held;
        const float scale = rest > 1e-6f ? (1.0f - next) / rest : 0.0f;
        for (Part& p : parts) p.weight = p.look == &target ? next : p.weight * scale;
        parts.erase(std::remove_if(parts.begin(), parts.end(),
                                   [](const Part& p) { return p.weight < 1e-5f; }),
                    parts.end());
        if (parts.empty() || next >= 1.0f) {
            parts.assign(1, Part{&target, 1.0f});
            return;
        }
        float sum = 0.0f;
        for (const Part& p : parts) sum += p.weight;
        for (Part& p : parts) p.weight /= sum;  // keep the sum at one against rounding
    }
};

// Tracks, for each thing being shown, the blend on screen and the look it is
// heading for. Knows nothing about GL, so any renderer can use it - and it can
// be tested without a window.
class LookFader {
public:
    explicit LookFader(const LookState& standard) : standard_(&standard) {}

    // Once per frame, before any mix() of that frame.
    void advance(double dt) {
        dt_ = dt;
        ++frame_;
    }

    // The look `s` is shown in: its active worn look if the graph has it, the
    // standard one otherwise.
    const LookState& look_of(const StateGraph* g, const State& s) const {
        if (g) {
            const Key id = active_look(s);
            if (!id.empty())
                if (const auto* l = dynamic_cast<const LookState*>(g->find(id))) return *l;
        }
        return *standard_;
    }

    // `who` is heading for `target`; the blend moves towards it at the rate the
    // target's `fade` sets. A first sighting starts at the target. Moves at
    // most once per frame, however often it is asked.
    const LookMix& mix(Key who, const LookState& target) {
        auto it = fades_.find(who);
        if (it == fades_.end()) {
            Fade f;
            f.mix.parts.assign(1, LookMix::Part{&target, 1.0f});
            f.frame = frame_;
            return fades_.emplace(who, std::move(f)).first->second.mix;
        }
        Fade& f = it->second;
        if (f.frame != frame_) {
            f.frame = frame_;
            f.mix.toward(target,
                         static_cast<float>(dt_ / std::max(1e-3, target.fade_seconds())));
        }
        return f.mix;
    }

    // A value of a pass, from a look, else the standard look, else `fallback`.
    double value(const LookState& l, Key pass, Key k, double fallback) const {
        if (const Element* e = l.find(pass))
            if (e->params.has(k)) return e->params.num(k, fallback);
        if (const Element* e = standard_->find(pass))
            if (e->params.has(k)) return e->params.num(k, fallback);
        return fallback;
    }

    // The same value, as the blend on screen has it.
    double value(const LookMix& m, Key pass, Key k, double fallback) const {
        double v = 0.0;
        for (const LookMix::Part& p : m.parts) v += p.weight * value(*p.look, pass, k, fallback);
        return v;
    }

    const LookState& standard() const { return *standard_; }

private:
    struct Fade {
        LookMix mix;
        uint64_t frame = 0;
    };

    const LookState* standard_;
    std::unordered_map<Key, Fade> fades_;
    double dt_ = 0.0;
    uint64_t frame_ = 0;
};

// What stops the looks in a graph from being shown as declared. GL-free: a
// renderer adds its own checks on the shaders once it has compiled them.
inline std::vector<std::string> look_defects(const StateGraph& g) {
    std::vector<std::string> out;
    for (const Embedding& e : g.embeddings()) {
        if (e.portal != look_slot_id()) continue;
        const State* guest = g.find(e.guest);
        if (guest && guest->kind() != kinds::look)
            out.push_back(e.host.str() + " wears " + e.guest.str() + ", which is not a look");
    }
    for (Key id : g.ids()) {
        const State& s = g.state(id);
        if (s.kind() == kinds::look) {
            for (const auto& el : s.elements()) {
                bool known = false;
                for (Key p : passes::all()) known = known || el.id == p;
                if (!known)
                    out.push_back("look " + id.str() + ": no pass named " + el.id.str());
            }
            continue;
        }
        if (!s.find(look_slot_id())) continue;
        const Key active = active_look(s);
        bool worn = false;
        for (Key k : worn_looks(g, id)) worn = worn || k == active;
        if (!worn)
            out.push_back(id.str() + " shows look " + (active.empty() ? "<none>" : active.str()) +
                          ", which it does not wear");
    }
    return out;
}

}  // namespace sg
