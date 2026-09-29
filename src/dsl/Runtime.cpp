#include "sg/dsl/Runtime.hpp"

#include <stdexcept>

namespace sg::dsl {

State& expect_state(StateGraph& g, const std::string& id, const std::string& kind) {
    State* s = g.find(Key{id});
    if (!s) throw std::runtime_error("the source names state " + id + ", which the graph does not hold");
    if (!kind.empty() && s->kind().str() != kind && kind != "state")
        throw std::runtime_error("state " + id + " is " + s->kind().str() + ", not " + kind);
    return *s;
}

Element& expect_element(State& s, const std::string& id, const std::string& kind) {
    Element* e = s.find(Key{id});
    if (!e) throw std::runtime_error("state " + s.id().str() + " has no element " + id);
    if (!kind.empty() && e->kind.str() != kind)
        throw std::runtime_error("element " + s.id().str() + "." + id + " is " + e->kind.str() + ", not " + kind);
    return *e;
}

const Morphism& expect_arrow(State& s, const std::string& name, const std::string& from, const std::string& to,
                             const std::string& trigger) {
    const Morphism* m = s.morphism(Key{name});
    if (!m) throw std::runtime_error("state " + s.id().str() + " has no arrow " + name);
    if (m->from.str() != from || cod(*m).str() != (to.empty() ? from : to) || m->trigger.str() != trigger)
        throw std::runtime_error("arrow " + s.id().str() + "." + name + " is " + m->from.str() + " -> " + cod(*m).str() + " on " +
                                 m->trigger.str() + ", not " + from + " -> " + (to.empty() ? from : to) + " on " + trigger);
    return *m;
}

const Functor& expect_functor(StateGraph& g, const std::string& name, const std::string& from, const std::string& to) {
    const Functor* f = g.functor(Key{name});
    if (!f) throw std::runtime_error("the source names functor " + name + ", which the graph does not hold");
    if (f->from().str() != from || f->to().str() != to)
        throw std::runtime_error("functor " + name + " is " + f->from().str() + " -> " + f->to().str() + ", not " + from + " -> " + to);
    return *f;
}

Temporal& clock(StateGraph& g, const std::string& id) {
    State& s = expect_state(g, id);
    Temporal* t = dynamic_cast<Temporal*>(&s);
    if (!t) throw std::runtime_error("state " + id + " keeps no time: a drive names a Temporal");
    return *t;
}

Bindings& Bindings::add(const std::string& device, const std::string& key, Key event, Params args) {
    table_[{device, key}] = Binding{event, std::move(args)};
    return *this;
}

const Binding* Bindings::find(const std::string& device, const std::string& key) const {
    auto it = table_.find({device, key});
    return it == table_.end() ? nullptr : &it->second;
}

bool Bindings::press(Engine& e, const std::string& device, const std::string& key) const {
    const Binding* b = find(device, key);
    if (!b) return false;
    e.fire(Event{b->event, b->args});
    return true;
}

}  // namespace sg::dsl
