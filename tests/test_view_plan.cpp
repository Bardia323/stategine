#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/render/ViewPlan.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace sgen {
void build_view_plan(sg::StateGraph &, const sg::dsl::Natives &, sg::dsl::Bindings &);
}
namespace {
void require(bool good, const char *reason) {
    if (!good)
        throw std::runtime_error(reason);
}
std::string fingerprint(const sg::render::ViewPlan &plan) {
    std::ostringstream out;
    const auto vector = [&](const auto &v) { out << v.x << ',' << v.y << ',' << v.z << ';'; };
    const auto pose = [&](const sg::Pose &p) {
        vector(p.position);
        out << p.yaw << ';';
    };
    const auto instance = [&](const sg::render::DrawInstance &d) {
        out << d.element << ';';
        pose(d.pose);
        vector(d.size);
        out << d.colour.r << ',' << d.colour.g << ',' << d.colour.b << ';' << d.pitch << ',' << d.roll << ','
            << d.roughness << ',' << d.surface << ',' << d.emissive << ';';
        for (float n : d.model.m)
            out << n << ',';
    };
    out.precision(17);
    out << plan.time << ';' << plan.surface << ';' << plan.rooms.size() << ';';
    for (const auto &room : plan.rooms) {
        out << room.room << ';' << room.look << ';' << room.time << ';';
        pose(room.placement);
        vector(room.camera.eye);
        vector(room.camera.forward);
        vector(room.camera.up);
        out << room.camera.fov << ',' << room.camera.near_plane << ',' << room.camera.far_plane << ';';
        for (const auto &clip : room.clips) {
            vector(clip.normal);
            out << clip.offset << ';';
        }
        out << room.instances.size() << ';';
        for (const auto &d : room.instances)
            instance(d);
        out << room.lights.size() << ';';
        for (const auto *light : room.lights)
            out << light << ';';
        out << room.portals.size() << ';';
        for (const auto &p : room.portals)
            out << p.host << ',' << p.portal << ',' << p.guest << ',' << p.embedding << ',' << p.seam << ',' << p.feed
                << ',' << p.feed_eye << ';';
    }
    out << plan.sprites.size() << ';';
    for (const auto &d : plan.sprites)
        instance(d);
    return out.str();
}
} // namespace
int main() {
    try {
        sg::StateGraph g;
        sg::dsl::Bindings b;
        sgen::build_view_plan(g, {}, b);
        sg::Engine e(g);
        e.set_strict(true);
        e.start();
        e.open_embed("picture");
        e.tick(0.25);
        const auto revision = g.revision();
        const auto snapshot = sg::dsl::facts(g);
        auto a = sg::render::view_plan(g, g.state("room"));
        require(a.rooms.size() == 1 && a.rooms[0].instances.size() == 1, "derived room layout");
        require(a.time == 0.25 && !a.rooms[0].portals.empty(), "declared time and embedding");
        const auto output = fingerprint(a);
        a = {};
        auto again = sg::render::view_plan(g, g.state("room"));
        require(fingerprint(again) == output, "equivalent complete derived output after reconstruction");
        require(sg::dsl::facts(g) == snapshot && g.revision() == revision, "view queries cannot change world data");
        const auto &panel = g.state("room").element("panel");
        require(sg::render::declared_world(g, g.state("room"), panel, g.state("guest")), "graph connection");
        e.close_embed("picture");
        const auto before = sg::verify(g);
        if (!before.holds())
            std::cerr << before.str();
        require(before.holds(), "declared seam laws");
        g.drop_embedding("picture");
        g.drop_seam("doorway");
        for (const char *f : {"doorway.ab", "doorway.ba", "doorway.glue.ab", "doorway.glue.ba"})
            g.drop_functor(f);
        require(!sg::render::declared_world(g, g.state("room"), panel, g.state("guest")),
                "stale binding cannot connect worlds");
        require(g.revision() >= revision, "graph remains authority");
        for (int i = 0; i < 31; ++i) {
            auto disposable = sg::render::view_plan(g, g.state("room"));
            (void)disposable;
        }
        require(sg::render::semantic_time(&g, g.state("room")) == 0.25, "render schedules cannot advance time");
        const auto laws = sg::verify(g);
        if (!laws.holds())
            std::cerr << laws.str();
        require(g.validate().empty() && laws.holds(), "strict laws");
        std::cout << "reconstruction, graph supremacy and one time ontology pass\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
