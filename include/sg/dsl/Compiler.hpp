// Stategine DSL - a world that compiles, from inside.
//
// The notation is not a state. A particular source document may be one, and a
// compiler may be one: a state whose arrows are native computations. Neither
// is privileged, and neither is a second runtime. What they do is what any
// states do, through the graph:
//
//     source          the text, held by an element of an ordinary state
//        | says source.compile          (a functor carries it to ...)
//     compiler        idle -> compiling -> success | error
//        | says compiler.change         (the graph declares it an edit ...)
//     graph.edit      applied by the engine at the start of the next frame
//        |
//     StateGraph      new states, arrows, functors, embeddings, ... : the very
//                     objects the C++ API would have made
//        | the answer, compiler.change.done, heard by the compiler
//
// `compiler.sg` (beside this file) declares the two states and the edit, in
// the notation, with natives for the insides of their arrows. This registers
// those natives. Nothing here changes the graph but the edit, which the engine
// applies by its own rule - never inside an arrow, a listener or a trial.
//
// A program compiled from inside does no IO: `file(...)` is refused. Native
// computations a program may name are those the host registered in
// `available`, and no others - a source cannot bring code into the world.
#pragma once

#include "sg/dsl/Apply.hpp"
#include "sg/dsl/Compile.hpp"
#include "sg/dsl/Natives.hpp"

namespace sg::dsl {

// Registers source_set_text, source_request, compile_begin, compile_valid,
// compile_invalid, compile_settled (arrows), compile_apply (the edit) and
// compile_reload (the edit that makes an edited source again, `text` over
// `before`, replacing only what changed: Apply.hpp, reload) into
// `into`. `available` are the natives a compiled program may name; `bindings`
// (may be null) receives the input tables of programs that bind. Both must
// outlive the world.
void register_compiler(Natives& into, const Natives& available, Bindings* bindings = nullptr, const Kinds& kinds = Kinds::standard());

// What a live graph already holds, as Options.live_states / live_functors say.
Options options_for(const StateGraph& g, const Kinds& kinds = Kinds::standard());

}  // namespace sg::dsl
