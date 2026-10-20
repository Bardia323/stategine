#include "sg/domains/Wear.hpp"

#include <cmath>

namespace sg {

Element& add_leak(Spatial3D& world, Key id, Vec3d at, double reach) {
    Element& e = world.add_element(id, kinds::leak);
    set_position(e, at);
    e.params.set(Key{"reach"}, reach);
    return e;
}

std::vector<const Element*> leaks_of(const State& world) {
    std::vector<const Element*> out;
    for (const Element& e : world.elements())
        if (e.alive && e.kind == kinds::leak) out.push_back(&e);
    return out;
}

std::vector<std::string> wear_faults(const State& world) {
    std::vector<std::string> out;
    if (world.params().has(Key{"age"})) {
        const double age = world.params().num(Key{"age"});
        if (!std::isfinite(age) || age < 0.0 || age > 1.0) out.push_back("wear " + world.id().str() + ": its age is not between new (0) and long neglected (1)");
    }
    for (const Element* e : leaks_of(world)) {
        const std::string name = "leak " + world.id().str() + "." + e->id.str() + ": ";
        const double reach = e->params.num(Key{"reach"}, 1.0);
        if (!std::isfinite(reach) || !std::isfinite(e->params.num(keys::x)) || !std::isfinite(e->params.num(keys::y)) || !std::isfinite(e->params.num(keys::z)))
            out.push_back(name + "a number of it is not one");
        else if (reach <= 0.0)
            out.push_back(name + "it reaches no way at all");
    }
    return out;
}

}  // namespace sg
