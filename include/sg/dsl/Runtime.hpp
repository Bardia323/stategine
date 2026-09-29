// Stategine DSL - what generated code stands on.
//
// Generated code calls the engine's own API and nothing else, apart from these
// few checks and the input table. None has a meaning of its own: each asks
// the graph, or the state, whether what the source says is so, and throws if
// it is not - a build that disagrees with its source is not built.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "sg/core/Engine.hpp"

namespace sg::dsl {

// A state the source names but does not build (`extern state`): it must be
// there, and be what the source says (kind empty: any).
State& expect_state(StateGraph& g, const std::string& id, const std::string& kind = {});

// An element the state's own class makes (a look's passes, a camera's lens):
// there, and of that kind.
Element& expect_element(State& s, const std::string& id, const std::string& kind);

// An arrow the state's own class makes: there, with these ends and trigger.
const Morphism& expect_arrow(State& s, const std::string& name, const std::string& from, const std::string& to,
                             const std::string& trigger);

// A functor the source names but does not build (`extern functor`).
const Functor& expect_functor(StateGraph& g, const std::string& name, const std::string& from, const std::string& to);

// The clock a drive names: a Temporal.
Temporal& clock(StateGraph& g, const std::string& id);

// An input device's table: which key fires which event. The adapter that reads
// a real device calls `press`, and the engine is fired - the only thing done.
// Nothing here reaches a state; the engine routes the event to whoever holds
// the focus.
struct Binding {
    Key event;
    Params args;
};

class Bindings {
public:
    Bindings& add(const std::string& device, const std::string& key, Key event, Params args = {});
    const Binding* find(const std::string& device, const std::string& key) const;
    // Fires the bound event through the engine. False if the key is not bound.
    bool press(Engine& e, const std::string& device, const std::string& key) const;
    std::size_t size() const { return table_.size(); }

private:
    std::map<std::pair<std::string, std::string>, Binding> table_;
};

}  // namespace sg::dsl
