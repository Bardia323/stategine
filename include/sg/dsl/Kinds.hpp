// Stategine DSL - the kinds of state a source may name.
//
// `state x : camera` says which of the engine's states x is. A kind is a
// C++ class the engine (or a project) already has; the DSL only names it and
// says what to build. What the class builds into itself - a LookState's
// passes, a Camera's lens and its arrows - is the kind's own, found by asking
// a scratch instance, never written a second time here.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sg/core/State.hpp"

namespace sg::dsl {

struct KindInfo {
    std::string name;  // as the DSL writes it: "look"
    std::string cpp;   // as generated code writes it: "sg::LookState"
    std::function<std::unique_ptr<State>(Key)> make;
};

class Kinds {
public:
    // The engine's own: state, temporal, spatial2d, spatial3d, look, camera, console, save.
    static const Kinds& standard();

    Kinds& add(KindInfo k);
    const KindInfo* find(const std::string& name) const;
    std::vector<std::string> names() const;

private:
    std::map<std::string, KindInfo> kinds_;
};

}  // namespace sg::dsl
