#include "sg/core/Temporal.hpp"

namespace sg {

Element& Temporal::timeline(Key line) {
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

bool Temporal::has_timeline(Key line) const {
    const Element* e = find(line);
    return e && e->kind == timeline_kind();
}

const Drive& drive(StateGraph& g, Temporal& clock, Key state, Key trigger, bool additive, Keeps keeps) {
    clock.timeline(state);
    return g.drive(Drive{Key{clock.id().str() + ">" + state.str()}, clock.id(), state, trigger,
                         additive, state, keeps});
}

Event drive_event(const Drive& d, double dt, double time, int64_t frame) {
    Params p;
    p.set(keys::dt, dt).set(keys::time, time);
    if (!d.additive) p.set(keys::frame, frame);
    Event ev{d.trigger, std::move(p)};
    ev.source = d.clock;
    return ev;
}

Event drive_event(const Drive& d, const State& clock, double dt) {
    const Element& line = clock.element(timeline_of(d));
    return drive_event(d, dt, line.params.num(keys::time), line.params.get_or<int64_t>(keys::frame, 0));
}

}  // namespace sg
