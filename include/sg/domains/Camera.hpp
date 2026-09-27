// Stategine - a camera: a state of its own that sees a world.
//
// A camera is not a part of the room it looks at. It is a small state - one
// object, its lens - that could exist with no world at all, and meets a world
// only through the graph:
//
// Objects:  lens   where it stands and which way it looks (x, y, z, yaw,
//                  pitch, roll, fov), and the picture it makes (feed_w x
//                  feed_h). The lens is a portal with `feed` = 1 and `eye` = 1:
//                  what is embedded in it is seen *from* it.
// Arrows:   lens --aim-->  lens   stand and look somewhere else  {x, y, z, yaw, pitch, roll, fov}
//           lens --zoom--> lens   a wider or narrower view       {fov}
//
// It sees a world by filming it (sg::film): the world embedded in the lens.
// The renderer draws an open embedding in an `eye` portal from the portal's
// own pose, as a picture; a screen shows that picture by `shows` = the
// embedding's name, as it shows a deck's. So a camera can film the very room
// its screen stands in, and pointed at that screen, see itself seeing: each
// frame's picture holds the one before (the video feedback of a camcorder
// turned on its monitor). No state here knows another: the camera does not
// know the room, the room does not know the camera, the screen knows only
// the name of what it shows.
//
// Where it stands is its own (aimed by its arrow), or a world's: filmed with a
// `rig` - an element of the world - the embedding's `out` functor carries the
// rig's pose onto the lens, so a camera set on a shelf, or held, goes where
// that goes.
#pragma once

#include <algorithm>
#include <string>

#include "sg/core/StateGraph.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg {

class Camera : public State {
public:
    static Key lens_id() { return Key{"lens"}; }
    static Key aim_event() { return Key{"camera.aim"}; }
    static Key zoom_event() { return Key{"camera.zoom"}; }

    explicit Camera(Key id, double fov = 60.0, int w = 640, int h = 480) : State(id) {
        Element& lens = add_element(lens_id(), kinds::portal);
        lens.params.set(keys::x, 0.0).set(keys::y, 0.0).set(keys::z, 0.0);
        lens.params.set(keys::yaw, 0.0).set(keys::pitch, 0.0).set(keys::roll, 0.0).set(keys::fov, fov);
        // A portal that shows no panel of its own (w = h = 0): its picture is
        // for a screen elsewhere to show.
        lens.params.set(keys::w, 0.0).set(keys::h, 0.0);
        lens.params.set("feed", 1.0).set("eye", 1.0).set("live", 1.0);
        lens.params.set("feed_w", static_cast<double>(w)).set("feed_h", static_cast<double>(h));

        // lens --aim--> lens: whatever of its pose the event names.
        loop(Key{"aim"}, lens_id(), aim_event(), [](State&, Element& e, Element*, const Event& ev) {
            for (Key k : {keys::x, keys::y, keys::z, keys::yaw, keys::pitch, keys::roll, keys::fov})
                if (ev.args.has(k)) e.params.set(k, ev.args.num(k));
        });
        // lens --zoom--> lens: a field of view, kept to what a lens can be.
        loop(Key{"zoom"}, lens_id(), zoom_event(), [](State&, Element& e, Element*, const Event& ev) {
            if (!ev.args.has(keys::fov)) return;
            e.params.set(keys::fov, std::clamp(ev.args.num(keys::fov), 5.0, 150.0));
        });
    }

    Key kind() const override { return Key{"camera"}; }

    const Element& lens() const { return element(lens_id()); }
};

// The name a camera's filming of `world` goes by.
inline Key film_name(Key camera, Key world) { return Key{camera.str() + ".film." + world.str()}; }

// `camera` films `world`: the world embedded in the camera's lens, taking no
// input. Nothing runs from the camera into the world: the only functor is the
// rig's, from the world onto the lens (Live, so it runs every frame). With a `rig`,
// the world's element whose pose the camera takes - carried onto the lens by
// the embedding's `out` functor (`<camera>.rig`), every frame the rig or what
// it stands on moves. Open the embedding (Engine::open_embed) to roll; a
// screen shows the picture with `shows` = the returned name.
inline Key film(StateGraph& g, Key camera, Key world, Key rig = Key{}) {
    const Key name = film_name(camera, world);
    Key out;
    if (!rig.empty()) {
        out = Key{name.str() + ".rig"};
        // The rig's pose is its world pose - through whatever it stands on -
        // so the transport reads more than its two elements (Continuous).
        const StateGraph* graph = &g;
        Functor f(out, world, camera);
        f.on_object(rig, Camera::lens_id(), [graph, world](const Element& r, Element& lens) {
            const State* w = graph->find(world);
            const Pose p = w ? world_pose(*w, r) : local_pose(r);
            lens.params.set(keys::x, p.position.x).set(keys::y, p.position.y).set(keys::z, p.position.z).set(keys::yaw, p.yaw);
            lens.params.set(keys::pitch, r.params.num(keys::pitch));
        });
        g.set_functor(std::move(f));
    }
    g.set_focus(g.embed(name, camera, Camera::lens_id(), world, Key{}, out, EmbedSync::Live).name, false);
    if (!rig.empty()) g.set_propagation(name, Propagation::Continuous);
    return name;
}

}  // namespace sg
