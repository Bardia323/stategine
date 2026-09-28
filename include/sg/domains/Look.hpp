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

const std::vector<Key>& all();
}  // namespace passes

namespace look_keys {
inline const Key vs{"vs"}, fs{"fs"};  // shader sources, per pass
inline const Key fade{"fade"};        // seconds to fade in, on the look state
inline const Key active{"active"};    // which worn look is shown, on the slot
}  // namespace look_keys

inline bool is_uniform_key(Key k) { return !k.str().empty() && k.str()[0] == 'u'; }

class LookState : public State {
public:
    explicit LookState(Key id);

    Key kind() const override { return kinds::look; }

    // A uniform the pass's shader reads.
    LookState& uniform(Key pass, const std::string& name, double v);

    LookState& uniform(Key pass, const std::string& name, double x, double y, double z);

    // A knob the renderer reads itself, not a uniform.
    LookState& setting(Key pass, const std::string& name, double v);

    // Replace a pass's shaders. An empty vertex shader keeps the renderer's.
    LookState& shader(Key pass, std::string fs, std::string vs = {});

    // How long it takes to fade in. 0: it is cut to - and cut from.
    LookState& fade(double seconds) {
        params().set(look_keys::fade, seconds);
        return *this;
    }

    double fade_seconds() const { return params().num(look_keys::fade, 0.6); }
};

// --- wearing a look ---------------------------------------------------------------
inline Key look_slot_id() { return Key{"look"}; }

// `host` may be shown with `look`. The first look a state wears is its active one.
const Embedding& wear(StateGraph& g, Key host, Key look);

// Show `host` with another look it wears. A parameter write, so an arrow can
// do it as well as a caller.
void set_look(State& host, Key look);

Key active_look(const State& s);

std::vector<Key> worn_looks(const StateGraph& g, Key host);

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

    float weight(const LookState* l) const;

    bool settled() const { return parts.size() == 1; }

    // Shaders cannot be averaged. A pass that cannot dissolve between programs
    // uses the look with the most weight.
    const LookState& shown() const;

    // Move `step` of the way (in weight) towards `target`. Every other look
    // gives up weight in proportion to what it holds, so the blend travels in
    // a straight line towards the target and retraces it when turned back.
    void toward(const LookState& target, float step);
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
    const LookState& look_of(const StateGraph* g, const State& s) const;

    // `who` is heading for `target`; the blend moves towards it at the rate the
    // target's `fade` sets. A first sighting starts at the target. Moves at
    // most once per frame, however often it is asked.
    const LookMix& mix(Key who, const LookState& target);

    // `who` is at `target` now, no blend on the way: arrived where it was
    // already being shown in that look (through a doorway drawn in it).
    void settle(Key who, const LookState& target);

    // A value of a pass, from a look, else the standard look, else `fallback`.
    double value(const LookState& l, Key pass, Key k, double fallback) const;

    // The same value, as the blend on screen has it.
    double value(const LookMix& m, Key pass, Key k, double fallback) const;

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
std::vector<std::string> look_defects(const StateGraph& g);

}  // namespace sg
