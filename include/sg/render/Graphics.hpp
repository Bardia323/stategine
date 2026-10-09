// Stategine - how a program draws, as a state.
//
// The settings a renderer draws by (Quality: shadow maps, antialiasing,
// mirrors, glow, exposure ...) kept as a state of their own: one element,
// `quality`, its params the settings by the names render.conf gives them.
// A program makes it from its render.conf at start (load_quality); from
// then on it is the truth of how things are drawn, changed as any state is -
// through the graph, by its own arrow:
//
//   quality --set--> quality   graphics.set {key: value ...}: whichever
//                              settings it names, kept to what they may be
//
// - from a menu, a command, a port. A renderer follows it (GLWorldView::
// follow): it reads the state, as it reads every state it draws, and draws
// the next frame by it. Nothing writes the renderer; nothing in the state
// knows one. Like any state it is reached from where it is asked: a functor
// carrying a menu's or a command's event to `graphics.set`.
#pragma once

#include "sg/core/State.hpp"
#include "sg/render/Defaults.hpp"

namespace sg::render {

// The settings as params (by render.conf's names), and back: what a set of
// params does not name keeps `base`'s.
void quality_to_params(const Quality& q, Params& out);
Quality quality_from_params(const Params& p, Quality base = {});
// Each setting kept to what it may be: a shadow map a power of two from 256
// to 8192, samples 0 to 8, a reflection's share of the screen 0.1 to 1 ...
Quality kept_to_bounds(Quality q);

class Graphics : public State {
public:
    static Key quality_id() { return Key{"quality"}; }
    static Key set_event() { return Key{"graphics.set"}; }

    explicit Graphics(Key id, const Quality& start = {});

    Key kind() const override { return Key{"graphics"}; }

    // How things are drawn now.
    Quality quality() const;
    // Moves whenever a setting does.
    uint64_t stamp() const { return element(quality_id()).params.stamp(); }
};

}  // namespace sg::render
