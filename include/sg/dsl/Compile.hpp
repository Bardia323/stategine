// Stategine DSL - from what was read to a Plan, refusing what the ontology refuses.
//
// Three things happen here, in this order, and nothing is lowered from a
// program that fails an earlier one:
//
//   resolution   every name is a state, element, arrow, functor or event that
//                exists (in the program, in the kind's own class, or declared
//                `extern` and built elsewhere), and its type is the type
//                the place it is used wants
//   ontology     the engine's rules, said once, as errors: no second clock,
//                no direct write into another state, no IO in a transport,
//                nothing that could not be a declaration of the graph
//   lowering     each declaration becomes the engine's own call (Plan.hpp);
//                sugar (`wear`, `film`, `when`, `bind`, a transport alias)
//                disappears here and leaves only what it stands for
//
// The compiler builds the model. The engine's laws, `validate` and
// `sg::verify` check it, where the model is built.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "sg/dsl/Ast.hpp"
#include "sg/dsl/Kinds.hpp"
#include "sg/dsl/Plan.hpp"

namespace sg::dsl {

struct Options {
    const Kinds* kinds = nullptr;  // the standard ones when null
    // Reads a file the source names with file("..."). Empty: none may be read
    // - a program that is itself a piece of the world (compiled from inside)
    // does no IO; external effects cross a declared device.
    std::function<bool(const std::string& path, const std::string& from_file, std::string& out)> read_file;
    // States a live graph already holds, by id and kind: what `extern state`
    // may name when compiling into a running world. Empty for a whole build.
    std::map<std::string, std::string> live_states;
    // Functors a live graph already holds, by name (`extern functor`).
    std::map<std::string, std::pair<std::string, std::string>> live_functors;
    // The program is being held beside a graph that still has what it declares
    // (a declaration being ported from C++): a name the graph holds is not a
    // conflict, it is what is to be compared (Facts.hpp: `missing`).
    bool conform = false;
};

struct Compiled {
    Plan plan;
    std::vector<Diagnostic> errors;
    bool ok() const { return errors.empty(); }
    // All the errors, one message each.
    std::string report() const;
};

// One program or several, made into one Plan: states and functors meet across
// files by name.
Compiled compile(const std::vector<Program>& programs, const Options& o = {});

// Parse and compile one text.
Compiled compile_source(const std::string& text, const std::string& file = "<source>", const Options& o = {});

}  // namespace sg::dsl
