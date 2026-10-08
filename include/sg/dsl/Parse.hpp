// Stategine DSL - reading a source document.
//
// The text is read into a Program (Ast.hpp) and nothing else: no name is
// resolved here, no rule of the ontology is applied (Check.hpp does that).
// What the grammar has no word for has no syntax - and where a known bad
// pattern would be written (an `on_update`, a callback, a direct write into
// another state) the reader says why it is not in the language.
#pragma once

#include <string>
#include <vector>

#include "sg/dsl/Ast.hpp"

namespace sg::dsl {

struct Parsed {
    Program program;
    std::vector<Diagnostic> errors;
    bool ok() const { return errors.empty(); }
};

// `file` is only a name for the messages.
Parsed parse(const std::string& text, const std::string& file = "<source>");

// A transition's guard alone, as written after `when` (Ast.hpp: GuardAst):
// what a guard's text in generated code and in the facts is read back by.
struct ParsedGuard {
    GuardAst guard;
    std::vector<Diagnostic> errors;
    bool ok() const { return errors.empty(); }
};
ParsedGuard parse_guard(const std::string& text, const std::string& file = "<guard>");

}  // namespace sg::dsl
