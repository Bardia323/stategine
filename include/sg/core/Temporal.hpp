// Stategine - time, as a state of its own.
//
// Time is not the engine's: it is a state like any other. A `Temporal` holds
// timelines - an element each, with a time and a frame count as its params,
// and an arrow that moves them on by the `dt` it is fired with. Being a
// state, time is snapshotted, restored, put back to its start and written out
// like everything else - nothing about "when" lives outside the graph.
//
// A state that changes with time says so in the graph: `sg::drive(graph,
// clock, state, trigger)` gives it a line on the clock and declares that each
// time the state steps, its line advances and the state's arrows on `trigger`
// are fired with {dt, time, frame}. That is what
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
#include "sg/core/StateGraph.hpp"

namespace sg {

class Temporal : public State {
public:
    explicit Temporal(Key id) : State(id) {}

    Key kind() const override { return Key{"temporal"}; }

    // A line of time: one element holding a time and a frame count, moved on
    // by its own arrow. Each driven state keeps its time on a line of its
    // own, so one clock can keep a whole world's time while every state's
    // time is the sum of its own steps - a state set aside loses none.
    // Asking again for a line that is there is that line.
    Element& timeline(Key line) {
        if (Element* e = find(line)) return *e;
        Element& e = add_element(line, timeline_kind());
        e.params.set(keys::time, 0.0).set(keys::frame, int64_t{0});
        loop(Key{"advance." + line.str()}, line, advance_event(line),
             [](State&, Element& n, Element*, const Event& ev) {
                 const double dt = ev.args.num(keys::dt);
                 n.params.set(keys::time, n.params.num(keys::time) + dt);
                 n.params.set(keys::frame, n.params.get_or<int64_t>(keys::frame, 0) + 1);
             });
        return e;
    }
    bool has_timeline(Key line) const {
        const Element* e = find(line);
        return e && e->kind == timeline_kind();
    }

    static Key timeline_kind() {
        static const Key k{"timeline"};
        return k;
    }
    static Key advance_event(Key line) { return Key{"advance." + line.str()}; }

    double time(Key line) const { return element(line).params.num(keys::time); }
    // The frame count is how many steps were taken, which is not a matter of
    // time: an additive drive does not hand it on (see drive_event).
    int64_t frame(Key line) const { return element(line).params.get_or<int64_t>(keys::frame, 0); }
};

// The line a drive keeps its state's time on: its own `line`, or else the
// state's name.
inline Key timeline_of(const Drive& d) { return d.line.empty() ? d.state : d.line; }

// Declare a drive and give it its line on the clock, in one: `state` changes
// with the time `clock` keeps for it.
inline const Drive& drive(StateGraph& g, Temporal& clock, Key state, Key trigger,
                          bool additive = false) {
    clock.timeline(state);
    return g.drive(Drive{Key{clock.id().str() + ">" + state.str()}, clock.id(), state, trigger,
                         additive, state});
}

// What a drive hands its state's arrows, the clock having moved: dt, the
// time the line now says, and - unless the drive is additive - the frame.
// A claim that two steps are one step cannot be kept by an arrow that counts
// steps, so an additive drive gives it nothing to count. The engine and the
// laws both build the event here, so what is checked is what runs.
inline Event drive_event(const Drive& d, double dt, double time, int64_t frame) {
    Params p;
    p.set(keys::dt, dt).set(keys::time, time);
    if (!d.additive) p.set(keys::frame, frame);
    Event ev{d.trigger, std::move(p)};
    ev.source = d.clock;
    return ev;
}

inline Event drive_event(const Drive& d, const State& clock, double dt) {
    const Element& line = clock.element(timeline_of(d));
    return drive_event(d, dt, line.params.num(keys::time), line.params.get_or<int64_t>(keys::frame, 0));
}

}  // namespace sg
