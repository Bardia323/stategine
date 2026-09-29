// Stategine DSL - a Plan made on a live graph: a world rewriting itself.
//
// Compiled ahead of time, a program is C++ (Emit.hpp). Compiled from inside -
// its source is a state, a compiler is a state, and the compiler asks the
// graph to change - the same steps are made on the graph the world runs on,
// by the one lawful way it has: a `graph.edit`, applied by the engine at the
// start of a frame. Each step is the very call the emitted C++ makes.
//
// Nothing here may be called from an arrow, a listener or a law's trial (the
// graph refuses it: the engine's own seal); it is what an edit's `apply` calls.
#pragma once

#include <string>

#include "sg/dsl/Kinds.hpp"
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Plan.hpp"
#include "sg/dsl/Runtime.hpp"

namespace sg::dsl {

struct Applied {
    bool ok = true;
    std::string why;  // what stopped it, if it did
};

// Checked whole before anything is touched: a name already taken, a state or
// functor that is not there, a native nobody registered. A plan that passes
// is made; one that does not changes nothing.
Applied apply(const Plan& plan, StateGraph& g, const Natives& natives, Bindings* bindings = nullptr,
              const Kinds& kinds = Kinds::standard());

}  // namespace sg::dsl
