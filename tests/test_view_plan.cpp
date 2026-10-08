#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/render/ViewPlan.hpp"
#include <algorithm>
#include <cmath>
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
// A sun's shadow box moves in whole texels of its map: an eye that moves less
// than a texel - any way, the sun's way too - sees the same map, to the last
// bit, so it is not drawn again; one that moves a texel's worth sees another.
void sun_box_steps() {
    using sg::render::DrawLight;
    using sg::render::ViewCamera;
    DrawLight sun;
    sun.sun = true;
    sun.dir = sg::spatial::projection::normalize({0.43f, -0.71f, 0.29f});
    sun.extent = 20.0f;
    const int size = 2048;
    const float texel = 2.0f * sun.extent / static_cast<float>(size);
    const auto map_at = [&](double x, double y, double z) {
        float bias = 0.0f;
        ViewCamera eye;
        eye.eye = {x, y, z};
        eye.forward = {0.6, 0.0, -0.8};
        const auto m = sg::render::shadow_projection(sun, eye, size, bias);
        return std::vector<float>(m.m, m.m + 16);
    };
    const auto first = map_at(3.1, 1.7, -2.3);
    int same = 0, tried = 0;
    for (int i = 1; i <= 40; ++i) {
        const double s = 0.001 * i;  // a millimetre at a time, slantwise
        same += map_at(3.1 + s, 1.7 + s * 0.5, -2.3 - s * 0.7) == first;
        ++tried;
    }
    // A texel is about 2 cm here: of 40 steps of a millimetre, most stay in it.
    require(same >= tried / 4, "a sun's map stays the same while the eye moves less than a texel");
    require(map_at(3.1 + 3.0 * texel, 1.7, -2.3 + 3.0 * texel) != first, "and moves when the eye moves texels");
}

// A lamp's range: its light ends there, smoothly, and a lamp left out for
// reaching nothing lit nothing. The law: a gated lamp kept is one whose sphere
// meets its gate - some point of the opening is lit by it - and one left out
// lights no point of the opening at all. Likewise a box.
void lamp_ranges() {
    using sg::render::DrawLight;
    using sg::render::range_window;
    using V = sg::spatial::projection::Vec3;
    require(range_window(0.0f, 0.0f) == 1.0f && range_window(1e6f, 0.0f) == 1.0f, "no range: for ever");
    require(range_window(0.0f, 10.0f) == 1.0f && range_window(10.0f, 10.0f) == 0.0f && range_window(12.0f, 10.0f) == 0.0f,
            "a range: whole at the lamp, nothing from the range on");
    require(range_window(2.0f, 10.0f) > 0.99f, "all but whole where it lights");
    for (float d = 0.0f; d < 10.0f; d += 0.25f)
        require(range_window(d + 0.25f, 10.0f) <= range_window(d, 10.0f), "it only falls");
    // A doorway 2 m wide and 2.2 high, its middle at (5, 1.1, 0), across x.
    DrawLight gated;
    gated.gated = true;
    gated.gate_at = {5.0f, 1.1f, 0.0f};
    gated.gate_across = {1.0f, 0.0f, 0.0f};
    gated.gate_in = {0.0f, 0.0f, 1.0f};
    gated.gate_w = 1.0f;
    gated.gate_h = 1.1f;
    const auto lit_somewhere = [](const DrawLight &l) {
        for (int i = 0; i <= 40; ++i)
            for (int j = 0; j <= 44; ++j) {
                const V q{l.gate_at.x + l.gate_across.x * (-l.gate_w + 2.0f * l.gate_w * static_cast<float>(i) / 40.0f),
                          l.gate_at.y - l.gate_h + 2.0f * l.gate_h * static_cast<float>(j) / 44.0f,
                          l.gate_at.z + l.gate_across.z * (-l.gate_w + 2.0f * l.gate_w * static_cast<float>(i) / 40.0f)};
                const V d{q.x - l.pos.x, q.y - l.pos.y, q.z - l.pos.z};
                if (range_window(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), l.range) > 0.0f) return true;
            }
        return false;
    };
    for (float x = -4.0f; x <= 14.0f; x += 1.5f)
        for (float y = -2.0f; y <= 5.0f; y += 1.75f)
            for (float z = -9.0f; z <= -0.5f; z += 1.25f)
                for (float range : {0.0f, 0.5f, 2.0f, 4.0f, 7.5f}) {
                    DrawLight l = gated;
                    l.pos = {x, y, z};
                    l.range = range;
                    const bool kept = sg::render::light_meets_gate(l);
                    const bool lit = lit_somewhere(l);
                    // Left out, it lit nothing there; kept, it lights some of it
                    // (but for a sphere that only grazes the opening, between
                    // the points looked at).
                    require(kept || !lit, "a gated lamp left out lights none of its opening");
                    if (kept && !lit) {
                        // The nearest point of the opening is just within reach.
                        const float u = std::clamp(x - 5.0f, -1.0f, 1.0f), v = std::clamp(y - 1.1f, -1.1f, 1.1f);
                        const float dx = 5.0f + u - x, dy = 1.1f + v - y, dz = -z;
                        require(range - std::sqrt(dx * dx + dy * dy + dz * dz) < 0.1f, "a gated lamp kept meets its gate");
                    }
                }
    DrawLight loose = gated;
    loose.gated = false;
    loose.pos = {100.0f, 0.0f, 0.0f};
    loose.range = 1.0f;
    require(sg::render::light_meets_gate(loose), "a light not gated has no gate to meet");
    DrawLight lamp;
    lamp.pos = {20.0f, 2.0f, 5.0f};
    lamp.range = 5.0f;
    require(!sg::render::light_meets_box(lamp, {0, 0, 0}, {14, 4, 12}), "a lamp out of reach of a room lights none of it");
    lamp.range = 6.5f;
    require(sg::render::light_meets_box(lamp, {0, 0, 0}, {14, 4, 12}), "a lamp within reach of a room may light it");
    lamp.range = 0.0f;
    require(sg::render::light_meets_box(lamp, {0, 0, 0}, {1, 1, 1}), "a lamp with no range reaches everywhere");
    lamp.sun = true;
    lamp.range = 1.0f;
    require(sg::render::light_meets_box(lamp, {0, 0, 0}, {1, 1, 1}), "a sun reaches everywhere");
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
        lamp_ranges();
        sun_box_steps();
        std::cout << "reconstruction, graph supremacy and one time ontology pass\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
