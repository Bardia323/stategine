// Stategine DSL - computations that belong in C++.
//
// The DSL declaration - an arrow's ends and trigger, a functor's objects, an
// edit's event - stays the semantic source of truth. A native is only the
// inside of one already declared: an arrow's handler, an object's transport,
// an edit's rewrite. It is looked up by name, once, when the graph is built.
//
// Natives are trusted extensions with restricted declared interfaces. They are
// not a security sandbox. What a native is handed is what its type gives it: an
// arrow's handler its own state and the elements of its arrow; a transport, the
// two elements it carries between. Neither is handed the graph, a clock, an
// engine or another state, so ordinary code written to that interface has no
// way to make a state, rewrite the graph, or call into another state. But it
// is C++: a closure can capture whatever its author gave it (a pointer to the
// graph, a file, a thread), and nothing here stops that. What contains a
// native is the engine's own checking - the laws re-run it on restored data, the
// graph is sealed while they do - review of what is registered, and the fact
// that a source can only *name* a native the host registered. (An edit is the
// graph's lawful way to rewrite itself: it is applied by the engine at the
// start of a frame.)
//
// The name a source bound a native by is kept where the engine holds it
// (Morphism::native, Functor::native_of, Edit::native), so two declarations that
// name different natives are different declarations. That is a binding, not a
// proof: two C++ functions of one name are not shown to be equivalent.
#pragma once

#include <functional>
#include <map>
#include <string>

#include "sg/core/StateGraph.hpp"

namespace sg::dsl {

class Natives {
public:
    Natives& arrow(const std::string& name, Morphism::Handler h);
    Natives& transport(const std::string& name, Transport t);
    Natives& edit(const std::string& name, Edit::Apply a);

    // By name; a native no one registered is an error where the graph is built.
    Morphism::Handler arrow(const std::string& name) const;
    Transport transport(const std::string& name) const;
    Edit::Apply edit(const std::string& name) const;

    bool has_arrow(const std::string& name) const { return arrows_.count(name) != 0; }
    bool has_transport(const std::string& name) const { return transports_.count(name) != 0; }
    bool has_edit(const std::string& name) const { return edits_.count(name) != 0; }

private:
    std::map<std::string, Morphism::Handler> arrows_;
    std::map<std::string, Transport> transports_;
    std::map<std::string, Edit::Apply> edits_;
};

}  // namespace sg::dsl
