#include "sg/domains/Fog.hpp"

#include <cmath>

namespace sg {

Element& add_fog(Spatial3D& world, Key id, Vec3d base, Vec3d size, double density, double soft) {
    Element& e = world.add_element(id, kinds::fog);
    set_position(e, base);
    e.params.set(keys::sx, size.x).set(keys::sy, size.y).set(keys::sz, size.z).set(Key{"density"}, density).set(Key{"soft"}, soft);
    return e;
}

std::vector<const Element*> fogs_of(const State& world) {
    std::vector<const Element*> out;
    for (const Element& e : world.elements())
        if (e.alive && e.kind == kinds::fog) out.push_back(&e);
    return out;
}

std::vector<std::string> fog_faults(const State& world) {
    std::vector<std::string> out;
    for (const Element* e : fogs_of(world)) {
        const std::string name = "fog " + world.id().str() + "." + e->id.str() + ": ";
        const Params& p = e->params;
        bool numbers = true;
        for (const char* k : {"sx", "sy", "sz", "density", "soft", "fall", "drift", "wisp", "takes", "breath", "breath_rate"})
            if (p.has(Key{k}) && !std::isfinite(p.num(Key{k}))) numbers = false;
        if (!numbers) {
            out.push_back(name + "a number of it is not one");
            continue;
        }
        if (!(p.num(keys::sx, 1.0) > 0.0 && p.num(keys::sy, 1.0) > 0.0 && p.num(keys::sz, 1.0) > 0.0)) out.push_back(name + "its box has no size");
        if (p.num(Key{"density"}, 0.3) < 0.0) out.push_back(name + "it is thinner than none");
        if (p.num(Key{"soft"}, 0.5) < 0.0 || p.num(Key{"fall"}, 0.0) < 0.0 || p.num(Key{"wisp"}, 2.0) <= 0.0)
            out.push_back(name + "its edges, its thinning or its wisps are less than none");
        const double drift = p.num(Key{"drift"}, 0.0);
        if (drift < 0.0 || drift > 1.0) out.push_back(name + "its drift is not between none (0) and the most (1)");
        const double takes = p.num(Key{"takes"}, 0.0);
        if (takes < 0.0 || takes > 1.0) out.push_back(name + "what it takes of the light is not between none (0) and all (1)");
        const double breath = p.num(Key{"breath"}, 0.0);
        if (breath < 0.0 || breath > 0.5) out.push_back(name + "its breath (how far its edge swells) is not between none (0) and half (0.5)");
    }
    return out;
}

}  // namespace sg
