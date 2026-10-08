#include "sg/domains/Camera.hpp"

namespace sg {

Camera::Camera(Key id, double fov, int w, int h) : State(id) {
    Element& lens = add_element(lens_id(), kinds::portal);
    lens.params.set(keys::x, 0.0).set(keys::y, 0.0).set(keys::z, 0.0);
    lens.params.set(keys::yaw, 0.0).set(keys::pitch, 0.0).set(keys::roll, 0.0).set(keys::fov, fov);
    // A portal that shows no panel of its own (w = h = 0): its picture is
    // for a screen elsewhere to show.
    lens.params.set(keys::w, 0.0).set(keys::h, 0.0);
    lens.params.set("feed", 1.0).set("eye", 1.0).set("live", 1.0).set(keys::open, true);
    lens.params.set("feed_w", static_cast<double>(w)).set("feed_h", static_cast<double>(h));

    // lens --aim--> lens: whatever of its pose the event names.
    loop(Key{"aim"}, lens_id(), aim_event(), [](State&, Element& e, Element*, const Event& ev) {
        for (Key k : {keys::x, keys::y, keys::z, keys::yaw, keys::pitch, keys::roll, keys::fov})
            if (ev.args.has(k)) e.params.set(k, ev.args.num(k));
    });
    // lens --roll--> lens: running or not - what it films is seen while
    // it runs (its filming follows the lens: sg::film).
    loop(Key{"roll"}, lens_id(), Key{"camera.roll"}, [](State&, Element& e, Element*, const Event& ev) {
        if (ev.args.has(Key{"on"})) e.params.set(keys::open, ev.args.get_or<bool>("on", true));
    });
    // lens --zoom--> lens: a field of view, kept to what a lens can be.
    loop(Key{"zoom"}, lens_id(), zoom_event(), [](State&, Element& e, Element*, const Event& ev) {
        if (!ev.args.has(keys::fov)) return;
        e.params.set(keys::fov, std::clamp(ev.args.num(keys::fov), 5.0, 150.0));
    });
}

Key film(StateGraph& g, Key camera, Key world, Key rig) {
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
    // Open while the camera runs (the lens's `open`, its own arrow's).
    g.embed(film_embedding(camera, world, rig));
    return name;
}

Embedding film_embedding(Key camera, Key world, Key rig) {
    Embedding e;
    e.name = film_name(camera, world);
    e.host = camera;
    e.portal = Camera::lens_id();
    e.guest = world;
    if (!rig.empty()) e.out = Key{e.name.str() + ".rig"};
    e.sync = EmbedSync::Live;
    e.focus = false;
    e.follows = true;
    if (!rig.empty()) e.propagate = Propagation::Continuous;
    // Its rig's functor is made for it alone: taken away, it goes too.
    e.cleanup = Cleanup::Cascade;
    return e;
}

}  // namespace sg
