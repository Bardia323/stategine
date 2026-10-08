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
#include <vector>

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

// --- reloading: a source made again over the world it made -------------------------------
// A source edited while the world runs is made again by one edit that
// replaces only what changed. `before` is the plan the source made, `after`
// the plan it makes now (compiled against the graph as it is, `conform` on:
// what it declared is there, and is what is to be replaced). Each declaration
// - a state with what it holds, a functor with its maps, a transition, an
// embedding, a seam, a drive, a port, an edit ... - is compared by its facts
// (Facts.hpp): with what the source said before, and with what the graph
// has. One that is the same in both is not touched. One that differs is made
// again:
//
//   - a state is kept, not swapped: the same object, so its line on the
//     Temporal goes on where it was, an embedding open on it stays open, and
//     focus stays where it was. Its params are carried - what was there
//     stays, what the source newly says is added (as `from_text`, not exact,
//     would carry them) - and its elements and arrows become what the source
//     says: those taken out of the source go, those changed are made again.
//     A state changes its kind never: another kind is another state.
//   - a functor, a transition, an embedding, a seam, a drive or an edit is
//     taken away and declared again, by the same name;
//   - one taken out of the source is taken away. A state, a port or a lens
//     is not taken from a running graph: such a reload is refused.
//
// All or nothing, as `apply`: if any of it fails, or the graph it makes is
// less valid than the one it found, everything is put back. A source that
// did not change changes nothing at all - not a param's stamp, not a time.
// What a reloaded state starts from (StateGraph::keep_default) is left as it
// was: going back to its start is going back to it as it was made.
struct Reloaded {
    bool ok = true;
    std::string why;                   // what stopped it, if it did
    std::vector<std::string> changed;  // what was made again, made or taken away: "state room", "transition go"
};

Reloaded reload(const Plan& before, const Plan& after, StateGraph& g, const Natives& natives, Bindings* bindings = nullptr,
                const Kinds& kinds = Kinds::standard());

}  // namespace sg::dsl
