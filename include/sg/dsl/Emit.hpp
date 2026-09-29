// Stategine DSL - a Plan as C++17, for a normal compiler.
//
// Boring on purpose: each step is the engine call it names, one after the
// other, and nothing else. Read the output beside the source and it is the
// same construction. It is built by a function
//
//     void <ns>::build_<name>(sg::StateGraph& graph, const sg::dsl::Natives& natives,
//                             sg::dsl::Bindings& bindings);
//
// that a project calls where it builds its world.
#pragma once

#include <string>

#include "sg/dsl/Plan.hpp"

namespace sg::dsl {

std::string emit_cpp(const Plan& plan, const std::string& name, const std::string& ns = "sgen");

}  // namespace sg::dsl
