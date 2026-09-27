// Stategine - time, as a state of its own.
//
// Time is not the engine's: it is a state like any other. A `Temporal` holds
// one element, `now`, with the time and the frame count as its params, and one
// arrow, `advance`, that moves them on by the `dt` it is fired with. Being a
// state, time is snapshotted, restored, put back to its start and written out
// like everything else - nothing about "when" lives outside the graph.
//
// A state that changes with time says so in the graph: `graph.drive(name,
// clock, state, trigger)` declares that each time the clock advances, the
// state's arrows on `trigger` are fired with {dt, time, frame}. That is what
// on_update used to be, and now it is an arrow the laws can see. Seen from
// category theory, time is a monoid of durations - one object, arrows `dt`,
// composed by adding - and a drive is its action on the state; the laws hold
// the action to what an action is:
//
//     step(0)            ==  id               (no time, no change)
//     step(a) ; step(b)  ==  step(a + b)      (declared `additive`)
//
// The second is a claim only an exact flow keeps - an Euler step taken twice
// by halves is not one step whole - so a drive makes it only when it is true.
// Without it, time is a sequence of steps, which composes by construction.
#pragma once

#include "sg/core/State.hpp"

namespace sg {

class Temporal : public State {
public:
    explicit Temporal(Key id) : State(id) {
        Element& now = add_element(now_id(), Key{"now"});
        now.params.set(keys::time, 0.0).set(keys::frame, int64_t{0});
        loop(Key{"advance"}, now_id(), advance_event(),
             [](State&, Element& n, Element*, const Event& ev) {
                 const double dt = ev.args.num(keys::dt);
                 n.params.set(keys::time, n.params.num(keys::time) + dt);
                 n.params.set(keys::frame, n.params.get_or<int64_t>(keys::frame, 0) + 1);
             });
    }

    Key kind() const override { return Key{"temporal"}; }

    static Key now_id() {
        static const Key k{"now"};
        return k;
    }
    static Key advance_event() {
        static const Key k{"advance"};
        return k;
    }

    double time() const { return element(now_id()).params.num(keys::time); }
    int64_t frame() const { return element(now_id()).params.get_or<int64_t>(keys::frame, 0); }
};

}  // namespace sg
