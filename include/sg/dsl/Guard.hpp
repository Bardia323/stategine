// Stategine DSL - a transition's guard, as the notation says it.
//
// `transition shop -[buy]-> till when from.stock > 0 and arg.n <= from.stock`
// lowers to the engine's own Transition::Guard: a plain function of the state
// the transition leaves and the event that asks, and of nothing else - no
// world, no clock, no other state - so it is pure, and the same guard on the
// same state and event always answers the same. It keeps its text, the
// comparison it is (canonical: one way of writing each guard), which the
// graph's facts print, which generated code reads back, and which is all a
// guard is: the text and the function are made one from the other.
//
// What it compares:
//   - two numbers (a flag, a whole number and a number are all numbers) by
//     their values; two texts (the state's id, words) letter by letter;
//   - anything that is not there - a param, an element, an argument - is
//     nothing: `==` nothing is false, `!=` it is true, and nothing is less or
//     more than anything; a number and a text likewise.
#pragma once

#include <string>

#include "sg/core/StateGraph.hpp"
#include "sg/dsl/Ast.hpp"

namespace sg::dsl {

// The guard's canonical text: `from[crt.door].walk != 1 and not arg.n == 2`.
std::string guard_text(const GuardAst& g);

// The engine's guard: the function, and its text. Empty for Kind::None.
Transition::Guard make_guard(const GuardAst& g);

// Read from its text (as written, or canonical) - what generated code calls.
// Throws std::invalid_argument for text that is not a guard.
Transition::Guard guard(const std::string& text);

}  // namespace sg::dsl
