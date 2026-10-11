// Stategine - every crossing one picture, against a real GL context.
//
// Rooms made as any project makes them, glued by the standard doorway
// (walkway) in the ways a world can be glued - straight through, turned a
// quarter, a room glued to itself - and every crossing walked and seen
// (check_crossings): none may jump or flicker. And a doorway shown from the
// wrong eye is caught, so the check is one that can fail.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "sg/domains/Atlas.hpp"
#include "sg/gl/Crossings.hpp"
#include "sg/gl/Window.hpp"
#include "sg/sg.hpp"

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// A room `w` by `d` with a door on wall `side`, things in it to see, and a
// lamp: its own colours, so two rooms are told apart.
sg::Room& room(sg::StateGraph& g, const std::string& id, double w, double d, int side, double along, double tint) {
    auto& r = g.add<sg::Room>(sg::Key{id}, w, d, 3.0, "rect", 6, id + ".");
    r.add_opening("door", side, along, 1.0, 2.1, 0.0, true);
    r.lay_walls();
    for (int i = 0; i < 4; ++i) {
        sg::Element& box = r.fixture(id + ".box" + std::to_string(i), 1.0 + (w - 2.0) * (i % 2), 0.0, 1.0 + (d - 2.0) * (i / 2));
        box.params.set(sg::keys::sx, 0.6).set(sg::keys::sy, 0.8 + 0.3 * i).set(sg::keys::sz, 0.6);
        box.params.set(sg::keys::r, tint).set(sg::keys::g, 0.4 + 0.1 * i).set(sg::keys::b, 1.0 - tint);
    }
    r.light(id + ".lamp", {w * 0.5, 2.6, d * 0.5});
    return r;
}

std::vector<std::string> walk(sg::render::GLWorldView& view, sg::StateGraph& g) {
    view.prepare(g);
    sg::render::CrossingOptions o;
    if (const char* d = std::getenv("SG_CROSSINGS_DUMP")) o.dump = d;
    const auto out = sg::render::check_crossings(view, g, o);
    for (const auto& s : out) std::printf("    %s\n", s.c_str());
    return out;
}

}  // namespace

int main() {
    sg::gl::Window window(160, 90, "crossings");
    {
        sg::StateGraph g;
        room(g, "hall", 8, 6, 2, 4.0, 0.8);
        room(g, "annex", 5, 7, 0, 2.5, 0.2);
        g.set_initial("hall");
        sg::walkway(g, "door", "hall", "hall.door", "annex", "annex.door");
        sg::render::GLWorldView view;
        check(walk(view, g).empty(), "through a doorway straight across, both ways: one picture");
    }
    {
        // Air that scatters its lamps' light on both sides: the air before
        // the doorway is this side's, lit by this side's lamp, and beyond
        // it the other's - and walking through is still one picture.
        sg::StateGraph g;
        room(g, "hall", 8, 6, 2, 4.0, 0.8);
        room(g, "annex", 5, 7, 0, 2.5, 0.2);
        for (const char* id : {"hall", "annex"}) {
            auto& look = g.add<sg::LookState>(sg::Key{std::string(id) + ".air"});
            look.uniform(sg::passes::scene, "uFogDensity", 0.03).setting(sg::passes::scene, "scatter", 0.2);
            sg::wear(g, sg::Key{id}, look.id());
        }
        g.set_initial("hall");
        sg::walkway(g, "door", "hall", "hall.door", "annex", "annex.door");
        sg::render::GLWorldView view;
        check(walk(view, g).empty(), "through a doorway with lit air on both sides, both ways: one picture");
    }
    {
        sg::StateGraph g;
        room(g, "hall", 8, 6, 2, 4.0, 0.8);
        room(g, "annex", 5, 7, 1, 3.0, 0.2);
        g.set_initial("hall");
        sg::walkway(g, "door", "hall", "hall.door", "annex", "annex.door");
        sg::render::GLWorldView view;
        check(walk(view, g).empty(), "through a doorway into a room turned a quarter, both ways: one picture");
    }
    {
        sg::StateGraph g;
        auto& r = room(g, "loop", 6, 6, 1, 3.0, 0.5);
        r.add_opening("far", 3, 3.0, 1.0, 2.1, 0.0, true);
        r.lay_walls();
        g.set_initial("loop");
        sg::walkway(g, "round", "loop", "loop.door", "loop", "loop.far", {}, /*wraps=*/true);
        sg::render::GLWorldView view;
        check(walk(view, g).empty(), "through a room glued to itself, both ways: one picture");
    }
    {
        // Glued into one space (Quality::glue): the hall and the annex are
        // two presheaves that agree at their doorway; glued, the annex
        // stands where the doorway puts it, drawn there, not as a view - and
        // walking through is still one picture, the same picture as seen
        // through the doorway.
        sg::StateGraph g;
        auto& hall = room(g, "hall", 8, 6, 2, 4.0, 0.8);
        auto& annex = room(g, "wing", 5, 7, 0, 2.5, 0.2);
        g.set_initial("hall");
        sg::walkway(g, "door", "hall", "hall.door", "wing", "wing.door");
        // The two rooms alone say where one stands in the other: no graph.
        sg::Pose b_in_a;
        const bool glues = sg::glue_rooms(hall, hall.element("hall.door"), annex, annex.element("wing.door"), b_in_a);
        sg::Element there = annex.element("wing.door");
        const sg::Pose door_there = sg::compose_pose(b_in_a, sg::world_pose(annex, there));
        const sg::Pose door_here = sg::world_pose(hall, hall.element("hall.door"));
        check(glues && sg::distance(door_there.position, door_here.position) < 1e-6,
              "two rooms glued at their doorway, from the rooms alone: the far doorway lands on the near one");
        const auto space = sg::glue_space(g, sg::Key{"wing"});
        check(space.size() == 2 && space.front().room == &annex && !space.front().doorways.empty(),
              "the space round the annex: both rooms, the annex first, glued by its doorway");
        const auto from_hall = sg::glue_space(g, sg::Key{"hall"});
        const auto pose_of = [](const std::vector<sg::PlacedRoom>& v, const sg::Spatial3D* r) {
            for (const auto& p : v)
                if (p.room == r) return p.pose;
            return sg::Pose{};
        };
        check(sg::distance(pose_of(space, &annex).position, pose_of(from_hall, &annex).position) < 1e-9 &&
                  sg::distance(pose_of(space, &hall).position, pose_of(from_hall, &hall).position) < 1e-9,
              "one frame for the space, whichever of its rooms it is asked from");
        sg::render::GLQuality q;
        q.glue = true;
        sg::render::GLWorldView glued(q);
        check(walk(glued, g).empty(), "glued into one space, through the doorway both ways: one picture");
        // (Both fresh, both made ready the same way.)
        sg::render::GLWorldView glued_fresh(q), seen_fresh;
        glued_fresh.prepare(g), seen_fresh.prepare(g);
        hall.camera().params.set(sg::keys::x, 4.0).set(sg::keys::y, 1.6).set(sg::keys::z, 3.0).set(sg::keys::yaw, 1.5707963).set(sg::keys::pitch, 0.0);
        const auto picture = [&](sg::render::GLWorldView& v) {
            for (int i = 0; i < 4; ++i) v.render(hall, 160, 90);
            std::vector<unsigned char> px(160 * 90 * 3);
            sg::gl::glBindFramebuffer(sg::gl::GL_FRAMEBUFFER, 0);
            sg::gl::glReadPixels(0, 0, 160, 90, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
            return px;
        };
        const auto a = picture(glued_fresh), b = picture(seen_fresh);
        if (const char* d = std::getenv("SG_GLUE_DUMP")) {
            for (int k = 0; k < 2; ++k) {
                FILE* f = std::fopen((std::string(d) + (k ? "/seen.ppm" : "/glued.ppm")).c_str(), "wb");
                if (!f) continue;
                std::fprintf(f, "P6 160 90 255 ");
                const auto& px = k ? b : a;
                for (int y = 89; y >= 0; --y) std::fwrite(&px[static_cast<std::size_t>(y) * 160 * 3], 1, 160 * 3, f);
                std::fclose(f);
            }
        }
        double diff = 0;
        for (std::size_t i = 0; i < a.size(); ++i) diff += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        diff /= static_cast<double>(a.size());
        std::printf("    (glued against seen through the doorway: %.2f levels apart on average)\n", diff);
        check(diff < 4.0, "the space glued is the picture seen through the doorway");
    }
    {
        // A room glued to itself (a ring that does not close) is never
        // folded onto itself: its doorway stays a doorway.
        sg::StateGraph g;
        auto& r = room(g, "loop", 6, 6, 1, 3.0, 0.5);
        r.add_opening("far", 3, 3.0, 1.0, 2.1, 0.0, true);
        r.lay_walls();
        g.set_initial("loop");
        sg::walkway(g, "round", "loop", "loop.door", "loop", "loop.far", {}, /*wraps=*/true);
        const auto space = sg::glue_space(g, sg::Key{"loop"});
        check(space.size() == 1 && space.front().doorways.empty(), "a room glued to itself stays one room, its doorway a doorway");
    }
    {
        // Shown from the wrong eye - a metre off - the view through the
        // doorway is not what is walked into.
        sg::StateGraph g;
        auto& hall = room(g, "hall", 8, 6, 2, 4.0, 0.8);
        auto& annex = room(g, "annex", 5, 7, 0, 2.5, 0.2);
        g.set_initial("hall");
        sg::walkway(g, "door", "hall", "hall.door", "annex", "annex.door");
        sg::render::GLWorldView view;
        view.prepare(g);
        view.render(hall, 160, 90);  // the seams bound as the graph says
        view.bind_world(sg::Key{"hall.door"}, &annex, [&](const sg::Element& from, sg::Element& to) {
            sg::portal_carry(hall.element("hall.door"), annex.element("annex.door"))(from, to);
            to.params.set(sg::keys::x, to.params.num(sg::keys::x) + 1.0);
        }, sg::Key{"annex.door"});
        const auto out = sg::render::check_crossings(view, g);
        bool jumped = false;
        for (const auto& s : out) jumped = jumped || s.find("hall -> annex") != std::string::npos && s.find("jumps") != std::string::npos;
        check(jumped, "a doorway shown from the wrong eye is a jump, and named");
    }
    {
        // A world in a glass: a ball of 0.3 m in the hall onto one of 10 m -
        // flown into, one picture; shown through a carry that forgets the
        // scale, not one, and named.
        const auto build = [](sg::StateGraph& g) {
            auto& hall = room(g, "hall", 8, 6, 2, 4.0, 0.8);
            auto& inner = g.add<sg::Spatial3D>("inner");
            inner.params().set("far", 400.0);
            for (int i = 0; i < 6; ++i) {
                sg::Element& b = inner.fixture("inner.block" + std::to_string(i), -4.0 + 1.6 * i, -2.0, (i % 2) * 2.0 - 1.0);
                b.params.set(sg::keys::sx, 1.0).set(sg::keys::sy, 1.5 + i).set(sg::keys::sz, 1.0);
                b.params.set(sg::keys::r, 0.2 + 0.12 * i).set(sg::keys::g, 0.5).set(sg::keys::b, 0.9 - 0.12 * i);
            }
            inner.fixture("inner.ground", 0.0, -2.5, 0.0).params.set(sg::keys::sx, 14.0).set(sg::keys::sy, 0.5).set(sg::keys::sz, 14.0);
            inner.light("inner.lamp", {0.0, 6.0, 0.0});
            inner.portal("inner.sky", {0.0, 0.0, 0.0}, 0.0, 0.0).params.set("ball", 10.0).set("ball_out", 1.0).set("window", 0.0);
            hall.portal("glass", {4.0, 1.4, 3.0}, 0.0, 0.0).params.set("ball", 0.3);
            g.set_initial("hall");
            sg::glue_doorway(g, "globe", "hall", "glass", "inner", "inner.sky");
        };
        {
            sg::StateGraph g;
            build(g);
            sg::render::GLWorldView view;
            check(walk(view, g).empty(), "into a world in a glass, at its scale: one picture");
        }
        {
            sg::StateGraph g;
            build(g);
            auto& hall = static_cast<sg::Spatial3D&>(g.state("hall"));
            auto& inner = static_cast<sg::Spatial3D&>(g.state("inner"));
            sg::render::GLWorldView view;
            view.prepare(g);
            view.render(hall, 160, 90);
            view.bind_world(sg::Key{"glass"}, &inner, sg::portal_carry(hall.element("glass"), inner.element("inner.sky")), sg::Key{"inner.sky"});
            const auto out = sg::render::check_crossings(view, g);
            bool named = false;
            for (const auto& s : out) named = named || s.find("hall -> inner") != std::string::npos;
            check(named, "a world in a glass shown at the wrong scale is named");
        }
    }
    return failures ? 1 : 0;
}
