#include "sg/dsl/Kinds.hpp"

#include "sg/domains/Camera.hpp"
#include "sg/domains/Console.hpp"
#include "sg/domains/Look.hpp"
#include "sg/domains/Save.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/core/Temporal.hpp"

namespace sg::dsl {

namespace {

template <class T>
KindInfo kind(const char* name, const char* cpp) {
    return KindInfo{name, cpp, [](Key id) { return std::unique_ptr<State>(new T(id)); }};
}

}  // namespace

const Kinds& Kinds::standard() {
    static const Kinds k = [] {
        Kinds out;
        out.add(kind<State>("state", "sg::State"));
        out.add(kind<Temporal>("temporal", "sg::Temporal"));
        out.add(kind<Spatial2D>("spatial2d", "sg::Spatial2D"));
        out.add(kind<Spatial3D>("spatial3d", "sg::Spatial3D"));
        out.add(kind<LookState>("look", "sg::LookState"));
        out.add(kind<Camera>("camera", "sg::Camera"));
        out.add(kind<ConsoleState>("console", "sg::ConsoleState"));
        out.add(kind<Save>("save", "sg::Save"));
        return out;
    }();
    return k;
}

Kinds& Kinds::add(KindInfo k) {
    const std::string name = k.name;
    kinds_[name] = std::move(k);
    return *this;
}

const KindInfo* Kinds::find(const std::string& name) const {
    auto it = kinds_.find(name);
    return it == kinds_.end() ? nullptr : &it->second;
}

std::vector<std::string> Kinds::names() const {
    std::vector<std::string> out;
    for (const auto& kv : kinds_) out.push_back(kv.first);
    return out;
}

}  // namespace sg::dsl
