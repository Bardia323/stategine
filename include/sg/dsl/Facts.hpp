// Stategine DSL - what a construction declares, in canonical words.
//
// A fact is one line saying one thing the graph is made of, in a form that
// does not depend on how it was built or in what order:
//
//     drive time>desert clock=time state=desert trigger=desert.day additive=0 line=desert keeps=always
//     port desert desert.hour
//     embed void/look:void.look host=void portal=look guest=void.look ...
//
// The facts of a graph built by hand and the facts of a Plan are written by
// the same functions, so they can be held side by side: what a DSL program
// declares is - or is not - what the C++ it replaces declared. That is how a
// declaration is ported before it is removed: the DSL's facts are all in the
// graph's, then the C++ goes, and the graph's facts are as they were.
#pragma once

#include <string>
#include <vector>

#include "sg/core/StateGraph.hpp"
#include "sg/dsl/Plan.hpp"

namespace sg::dsl {

enum class Scope {
    Relations,  // transitions, functors, embeddings, seams, drives, ports, kept, edits, lenses
    Whole       // and the states: their params, elements, arrows, what they say
};

// Sorted, one fact to a line.
std::vector<std::string> facts(const StateGraph& g, Scope scope = Scope::Whole);

// What the plan declares. Where a plan leaves a thing to the state's own
// class (a look's passes) it says only what the source said.
std::vector<std::string> facts(const Plan& plan);

// The facts of `plan` that the graph does not have. Every value the engine
// holds as data is in a fact: a transition's `enter` constants, an arrow's
// affine rows, a transport's stages, a composite's chain, an embedding's every
// field, a drive's line and keeps, an edit's reply. A native computation is
// there by the name the source bound it to (`native:exit_won`), so `native foo`
// against `native bar` differs. C++ built by hand names none: a fact of the plan
// with a native name is met by the graph's unnamed native (see `unverified`),
// and by nothing else.
std::vector<std::string> missing(const Plan& plan, const StateGraph& g);

// The facts of `plan` that the graph has only as an unnamed native: the graph's
// C++ was not shown to be the computation the source names. Not an error - a
// declaration being ported from a lambda cannot be told more - but not proven.
std::vector<std::string> unverified(const Plan& plan, const StateGraph& g);

// The lines that differ, `-` only in `before`, `+` only in `after`.
std::vector<std::string> difference(const std::vector<std::string>& before, const std::vector<std::string>& after);

// A graph's facts as text, and read back as lines (for a golden file).
std::string to_text(const std::vector<std::string>& facts);
std::vector<std::string> from_text(const std::string& text);

}  // namespace sg::dsl
