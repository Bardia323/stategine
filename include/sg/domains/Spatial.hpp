// Stategine - the spatial domain: things with a position that integrate.
//
// One class covers both the 2D and the 3D case, because the only difference is
// how many axes the integrator touches. Nothing here draws: a spatial state is
// data plus arrows, and a renderer (terminal, OpenGL, anything) is a separate
// object that reads it. That separation is what lets the same state show up as
// a map on a wall, a debug view in the console and a lit room at once.
#pragma once

#include <cmath>
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
    const double yaw = camera.params.num(keys::yaw);
    const double pitch = camera.params.num(keys::pitch);
    const double cp = std::cos(pitch);
    return {cp * std::cos(yaw), std::sin(pitch), cp * std::sin(yaw)};
}

inline double distance(const Vec3d& a, const Vec3d& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline double distance_to(const SpatialState& s, Key element_id) {
    const Element* e = s.find(element_id);
    if (!e) return 1e9;
    return distance(position_of(s.element(SpatialState::camera_id())), position_of(*e));
}

// True when the camera is near the element and pointed at it.
inline bool looking_at(const SpatialState& s, Key element_id, double max_dist = 3.0,
                       double min_facing = 0.8) {
    const Element* e = s.find(element_id);
    if (!e) return false;
    const Element& cam = s.element(SpatialState::camera_id());
    const Vec3d eye = position_of(cam);
    const Vec3d target = position_of(*e);
    const Vec3d d{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len > max_dist || len < 1e-6) return len <= max_dist;
    const Vec3d f = forward_of(cam);
    return (d.x * f.x + d.y * f.y + d.z * f.z) / len >= min_facing;
}

}  // namespace sg
