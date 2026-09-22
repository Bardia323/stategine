// Stategine - the spatial domain: things with a position that integrate.
//
// One class covers both the 2D and the 3D case, because the only difference is
// how many axes the integrator touches. Nothing here draws: a spatial state is
// data plus arrows, and a renderer (terminal, OpenGL, anything) is a separate
// object that reads it. That separation is what lets the same state show up as
// a map on a wall, a debug view in the console and a lit room at once.
#pragma once

#include <cmath>
#include <functional>
#include <string>

#include "sg/core/State.hpp"

namespace sg {

class SpatialState : public State {
public:
    SpatialState(Key id, int dims) : State(id), dims_(dims < 3 ? 2 : 3) {
        step_event_ = dims_ == 3 ? Key{"space3.step"} : Key{"space2.step"};
        Element& cam = add_element(camera_id(), kinds::camera);
        cam.params.set(keys::x, 0.0).set(keys::y, 0.0).set(keys::z, 0.0);
        cam.params.set(keys::yaw, 0.0).set(keys::pitch, 0.0).set(keys::fov, 70.0);
    }

    Key kind() const override { return dims_ == 3 ? Key{"space3d"} : Key{"space2d"}; }

    int dims() const { return dims_; }
    Key step_event() const { return step_event_; }
    static Key camera_id() { return Key{"camera"}; }

    // A body: position, velocity, colour, glyph - the vocabulary every renderer
    // and every functor in the engine already understands.
    Element& body(Key id, Vec3d pos, Key kind = Key{}, char glyph = '*') {
        Element& e = add_element(id, kind.empty() ? default_kind() : kind);
        set_position(e, pos);
        e.params.set(keys::vx, 0.0).set(keys::vy, 0.0).set(keys::vz, 0.0);
        e.params.set(keys::glyph, std::string(1, glyph));
        add_integrator(id);
        return e;
    }

    // Endomorphism on one body: velocity integrates into position. Registered
    // per element so the arrow really is an arrow of this category, visible in
    // the DOT output and checkable by validate().
    Morphism& add_integrator(Key id) {
        const int dims = dims_;
        return loop(Key{"move." + id.str()}, id, step_event_,
                    [dims](State&, Element& e, Element*, const Event& ev) {
                        const double dt = ev.args.num(keys::dt);
                        if (dt == 0.0) return;
                        e.params.set(keys::x, e.params.num(keys::x) + e.params.num(keys::vx) * dt);
                        e.params.set(keys::y, e.params.num(keys::y) + e.params.num(keys::vy) * dt);
                        if (dims == 3)
                            e.params.set(keys::z,
                                         e.params.num(keys::z) + e.params.num(keys::vz) * dt);
                    });
    }

    void on_update(const Tick& t) override {
        if (!integrate_) return;
        emit(Event{step_event_, Params{}.set(keys::dt, t.dt)});
    }

    // Off for states that are edited rather than simulated (a map, a menu).
    void set_integrating(bool on) { integrate_ = on; }
    bool integrating() const { return integrate_; }

    Element& camera() { return element(camera_id()); }
    const Element& camera() const { return element(camera_id()); }

protected:
    virtual Key default_kind() const { return dims_ == 3 ? kinds::mesh : kinds::sprite; }

private:
    int dims_;
    Key step_event_;
    bool integrate_ = true;
};

// --- the two everyday shapes -------------------------------------------------
class Spatial2D : public SpatialState {
public:
    explicit Spatial2D(Key id, int cols = 40, int rows = 16) : SpatialState(id, 2) {
        params().set(keys::w, static_cast<int64_t>(cols));
        params().set(keys::h, static_cast<int64_t>(rows));
    }

    // Convenience with the 2D reading of the axes.
    Element& sprite(Key id, double x, double y, char glyph = '*') {
        return body(id, {x, y, 0.0}, kinds::sprite, glyph);
    }

    int cols() const { return static_cast<int>(params().num(keys::w, 40)); }
    int rows() const { return static_cast<int>(params().num(keys::h, 16)); }
};

class Spatial3D : public SpatialState {
public:
    explicit Spatial3D(Key id) : SpatialState(id, 3) {
        camera().params.set(keys::z, -10.0);
        // camera --orbit--> camera
        loop(Key{"orbit"}, camera_id(), Key{"space3.orbit"},
             [](State&, Element& c, Element*, const Event& ev) {
                 c.params.set(keys::yaw, c.params.num(keys::yaw) + ev.args.num(Key{"dyaw"}));
                 c.params.set(keys::pitch, c.params.num(keys::pitch) + ev.args.num(Key{"dpitch"}));
             });
    }

    Element& mesh(Key id, double x, double y, double z, char glyph = '#') {
        return body(id, {x, y, z}, kinds::mesh, glyph);
    }

    // A light is an element like any other, so morphisms can move it.
    Element& light(Key id, Vec3d pos, double r = 1.0, double g = 0.93, double b = 0.82) {
        Element& e = add_element(id, kinds::light);
        set_position(e, pos);
        e.params.set(keys::r, r).set(keys::g, g).set(keys::b, b);
        e.params.set(keys::intensity, 1.0);
        return e;
    }

    // Level geometry: a solid box that blocks movement and takes plaster.
    // Walls are elements like everything else, so they can be anchored, moved
    // by a morphism, or edited from an interface.
    Element& wall(Key id, Vec3d centre, double sx, double sy, double sz, double yaw = 0.0) {
        Element& e = add_element(id, kinds::wall);
        set_position(e, centre);
        e.params.set(keys::sx, sx).set(keys::sy, sy).set(keys::sz, sz).set(keys::yaw, yaw);
        return e;
    }

    // A frame other elements hang off. Move it and the whole group moves.
    Element& anchor(Key id, Vec3d pos, double yaw = 0.0) {
        Element& e = add_element(id, kinds::anchor);
        set_position(e, pos);
        e.params.set(keys::yaw, yaw);
        return e;
    }

    // A portal: the element an embedded state is displayed on.
    Element& portal(Key id, Vec3d pos, double width, double height, double yaw = 0.0) {
        Element& e = add_element(id, kinds::portal);
        set_position(e, pos);
        e.params.set(keys::w, width).set(keys::h, height).set(keys::yaw, yaw);
        e.params.set(keys::open, false);
        return e;
    }
};

// --- camera queries ----------------------------------------------------------
// Gameplay asks these ("am I close enough to use the map?"), so they live with
// the domain rather than in a renderer.
inline Vec3d forward_of(const Element& camera) {
    const double pitch = camera.params.num(keys::pitch);
    const Vec3d flat = heading(camera.params.num(keys::yaw));
    const double cp = std::cos(pitch);
    return {flat.x * cp, std::sin(pitch), flat.z * cp};
}

inline double distance(const Vec3d& a, const Vec3d& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// A position plus a heading: what an anchor, a doorway or a camera carries.
struct Pose {
    Vec3d position;
    double yaw = 0.0;
};

// --- anchors: a group of elements with a frame of its own -----------------------
// An element may carry `parent`, naming another element it is placed relative
// to. Its stored pose is then local to that anchor, and moving the anchor moves
// the whole group - a wing of a building, a vehicle and its contents, a room
// that can slide around inside a larger space. Renderers and collision both go
// through world_pose, so nothing has to know which elements are anchored.
inline Pose local_pose(const Element& e) {
    return Pose{position_of(e), e.params.num(keys::yaw)};
}

inline Pose compose_pose(const Pose& parent, const Pose& local) {
    const Vec3d turned = rotate_xz(local.position, parent.yaw);
    return Pose{{parent.position.x + turned.x, parent.position.y + turned.y,
                 parent.position.z + turned.z},
                parent.yaw + local.yaw};
}

// The pose of an element in the state's own coordinates, following the parent
// chain. Depth is bounded, so a cycle cannot hang the frame.
inline Pose world_pose(const State& s, const Element& e, int max_depth = 8) {
    Pose p = local_pose(e);
    const Element* cur = &e;
    for (int i = 0; i < max_depth; ++i) {
        const std::string parent_id = cur->params.get_or<std::string>(keys::parent, "");
        if (parent_id.empty()) break;
        const Element* parent = s.find(Key{parent_id});
        if (!parent) break;
        p = compose_pose(local_pose(*parent), p);
        cur = parent;
    }
    return p;
}

inline Vec3d world_position(const State& s, const Element& e) { return world_pose(s, e).position; }

// Attach an element to an anchor. Its current pose is read as local from then on.
inline Element& attach_to(Element& e, Key anchor) {
    e.params.set(keys::parent, anchor.str());
    return e;
}

// --- camera queries, continued ---------------------------------------------------
// These go through world_pose: "am I close enough to use that panel" has to be
// asked about where the panel actually is, not where it sits inside its group.
inline double distance_to(const SpatialState& s, Key element_id) {
    const Element* e = s.find(element_id);
    if (!e) return 1e9;
    return distance(world_position(s, s.element(SpatialState::camera_id())),
                    world_position(s, *e));
}

// True when the camera is near the element and pointed at it.
inline bool looking_at(const SpatialState& s, Key element_id, double max_dist = 3.0,
                       double min_facing = 0.8) {
    const Element* e = s.find(element_id);
    if (!e) return false;
    const Element& cam = s.element(SpatialState::camera_id());
    const Vec3d eye = world_position(s, cam);
    const Vec3d target = world_position(s, *e);
    const Vec3d d{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len > max_dist || len < 1e-6) return len <= max_dist;
    const Vec3d f = forward_of(cam);
    return (d.x * f.x + d.y * f.y + d.z * f.z) / len >= min_facing;
}

// A room together with where it sits, in some chosen room's coordinates. The
// pose is not a property of the room - it is the answer to "seen from where?".
struct PlacedRoom {
    Spatial3D* room = nullptr;
    Pose pose;
};

// --- walls -----------------------------------------------------------------------
// Push `mover` out of any wall element it has ended up inside. Walls are boxes
// with a pose, so anchored ones are handled without knowing they are anchored.
//
// A wall stands on its base, so anything whose base is at or above head height
// is walked under, not into: that is what makes a lintel over a doorway a
// doorway rather than a blocked wall.
inline void resolve_wall_collisions(const State& s, Element& mover, double radius,
                                    double head = 1.9, const Pose& frame = Pose{}) {
    for (const auto& e : s.elements()) {
        if (e.kind != kinds::wall || !e.alive) continue;
        // `frame` places the whole room: a neighbouring room's walls are solid
        // too, and they are solid where that room actually sits relative to
        // the one the walker is standing in.
        const Pose w = compose_pose(frame, world_pose(s, e));
        if (w.position.y >= head) continue;

        const double hx = e.params.num(keys::sx, 1.0) * 0.5 + radius;
        const double hz = e.params.num(keys::sz, 1.0) * 0.5 + radius;

        // Into the wall's own frame.
        const Vec3d p = position_of(mover);
        const Vec3d local = rotate_xz({p.x - w.position.x, 0.0, p.z - w.position.z}, -w.yaw);
        const double lx = local.x, lz = local.z;
        if (std::fabs(lx) >= hx || std::fabs(lz) >= hz) continue;

        // Out through whichever face is closest.
        const double push_x = hx - std::fabs(lx);
        const double push_z = hz - std::fabs(lz);
        double ox = 0, oz = 0;
        if (push_x < push_z) {
            ox = lx >= 0 ? push_x : -push_x;
        } else {
            oz = lz >= 0 ? push_z : -push_z;
        }
        const Vec3d push = rotate_xz({ox, 0.0, oz}, w.yaw);
        mover.params.set(keys::x, p.x + push.x);
        mover.params.set(keys::z, p.z + push.z);
    }
}

// --- portals between spaces ---------------------------------------------------
// Two doorways, one in each room, joined back to back. Because each room is its
// own state with its own coordinates, everything about "the same place in the
// other room" is this one transform: rotate by the difference between the two
// doorway yaws (plus half a turn, since they face each other), then translate
// into the far doorway's frame.
// The turn that takes you from one doorway to the other. A viewer walks into
// `here` against its facing (-n_here) and comes out of `there` along its facing
// (+n_there); with facing n = (sin yaw, cos yaw), that works out to:
// A doorway faces the way its yaw points, exactly like a camera does. You walk
// into `here` against its facing and come out of `there` along it, so the turn
// between the two frames is:
inline double portal_delta(double here_yaw, double there_yaw) {
    return there_yaw - here_yaw + 3.14159265358979;
}

inline double portal_delta(const Element& here, const Element& there) {
    return portal_delta(here.params.num(keys::yaw), there.params.num(keys::yaw));
}

// One rotation, applied to the position and the heading alike - getting those
// two out of step is what makes a portal look subtly wrong.
inline Pose through_portal(const Pose& here, const Pose& there, const Vec3d& pos, double yaw) {
    const double delta = portal_delta(here.yaw, there.yaw);
    const Vec3d turned =
        rotate_xz({pos.x - here.position.x, 0.0, pos.z - here.position.z}, delta);
    return Pose{{there.position.x + turned.x, pos.y, there.position.z + turned.z}, yaw + delta};
}

inline Pose through_portal(const Element& here, const Element& there, const Vec3d& pos,
                           double yaw) {
    return through_portal(local_pose(here), local_pose(there), pos, yaw);
}

// The same transform as a transport, ready to hang on a functor: it carries a
// camera (or anything with a pose) from one room's frame into the other's.
// Used twice in a portal - once by the View embedding that aims the window's
// virtual camera, once by the transition taken when you walk through.
inline std::function<void(const Element&, Element&)> portal_carry(const Element& here,
                                                                  const Element& there) {
    const Vec3d hp = position_of(here);
    const Vec3d tp = position_of(there);
    const double delta = portal_delta(here, there);
    return [hp, tp, delta](const Element& src, Element& dst) {
        const Vec3d p = position_of(src);
        const Vec3d turned = rotate_xz({p.x - hp.x, 0.0, p.z - hp.z}, delta);
        set_position(dst, {tp.x + turned.x, p.y, tp.z + turned.z});
        dst.params.set(keys::yaw, src.params.num(keys::yaw) + delta);
        dst.params.set(keys::pitch, src.params.num(keys::pitch));
        dst.params.set(keys::fov, src.params.num(keys::fov, 70.0));
    };
}

// Did the step from `from` to `to` pass through the doorway's opening, front to
// back? Used to fire the transition that actually changes state.
inline bool crossed_portal(const Element& portal, const Vec3d& from, const Vec3d& to) {
    const Vec3d p = position_of(portal);
    const double yaw = portal.params.num(keys::yaw);
    const Vec3d face = heading(yaw);  // the way it faces
    const Vec3d side = across(yaw);   // along the opening
    const double nx = face.x, nz = face.z, tx = side.x, tz = side.z;
    const double d0 = (from.x - p.x) * nx + (from.z - p.z) * nz;
    const double d1 = (to.x - p.x) * nx + (to.z - p.z) * nz;
    if (!(d0 > 0.0 && d1 <= 0.0)) return false;  // only front to back
    const double span = d0 - d1;
    const double t = span > 1e-9 ? d0 / span : 0.0;
    const double hx = from.x + (to.x - from.x) * t;
    const double hz = from.z + (to.z - from.z) * t;
    const double lateral = (hx - p.x) * tx + (hz - p.z) * tz;
    const double half_w = portal.params.num(keys::w, 2.0) * 0.5;
    if (std::fabs(lateral) > half_w) return false;
    const double half_h = portal.params.num(keys::h, 2.0) * 0.5;
    return std::fabs(to.y - p.y) <= half_h + 0.9;
}

}  // namespace sg
