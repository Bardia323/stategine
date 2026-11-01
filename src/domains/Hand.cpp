// The hand (sg/domains/Hand.hpp): taking up, carrying, setting down and
// throwing the loose things of a room; working the parts of it that move.
#include "sg/domains/Hand.hpp"

#include <algorithm>
#include <cmath>

#include "sg/domains/Loose.hpp"
#include "sg/domains/Walk.hpp"

namespace sg {

namespace {

using Phys = physics::Physics;
constexpr double kPi = 3.14159265358979323846;
const Key kBody{"body"}, kSupport{"support"};

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// What the physics' arrows take: a body, and where it is to be.
Params pose_args(Key id, const Vec3d& p, double yaw, double pitch, double roll) {
    return Params{}.set("id", id.str()).set(keys::x, p.x).set(keys::y, p.y).set(keys::z, p.z).set(keys::yaw, yaw).set(keys::pitch, pitch).set(keys::roll, roll);
}

bool is_solid(const Element& b) { return Phys::solid_shape(b.params.get_or<std::string>("shape", "")); }
bool held_free(const Phys& ph, Key id) {
    const Element* b = ph.find(id);
    return b && b->params.num("free", 0.0) > 0.5;
}

// The slab test: where a ray from `o` along `d` enters a box `half` round
// the origin (in the box's own frame), and by which face; negative if not.
double ray_slab(const double o[3], const double d[3], const double half[3], int* axis = nullptr, int* sign = nullptr) {
    double t0 = -1e9, t1 = 1e9;
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(d[k]) < 1e-9) {
            if (std::fabs(o[k]) > half[k]) return -1;
            continue;
        }
        double a = (-half[k] - o[k]) / d[k], b = (half[k] - o[k]) / d[k];
        int s = -1;
        if (a > b) std::swap(a, b), s = 1;
        if (a > t0) {
            t0 = a;
            if (axis) *axis = k;
            if (sign) *sign = s;
        }
        t1 = std::min(t1, b);
    }
    return t0 > t1 || t0 <= 0 ? -1 : t0;
}

// A body, if the ray meets it: a solid by its own hulls, as the physics has
// them; paper by its face, with a little slack.
double ray_body(const Phys& ph, const Element& b, const Vec3d& eye, const Vec3d& dir, double reach) {
    if (is_solid(b)) {
        const rigid::Body* rb = ph.solids().find(b.id.str());
        double best = -1;
        if (rb)
            for (const rigid::Body::Placed& p : rb->world) {
                const double t = rigid::ray_hull(p, {eye.x, eye.y, eye.z}, {dir.x, dir.y, dir.z}, reach);
                if (t >= 0 && (best < 0 || t < best)) best = t;
            }
        if (rb || best >= 0) return best;
    }
    const Vec3d c = position_of(b);
    const double yaw = b.params.num(keys::yaw), pitch = b.params.num(keys::pitch), roll = b.params.num(keys::roll);
    const Vec3d ax[3] = {physics::panel_turn({1, 0, 0}, yaw, pitch, roll), physics::panel_turn({0, 1, 0}, yaw, pitch, roll),
                         physics::panel_turn({0, 0, 1}, yaw, pitch, roll)};
    const Vec3d e = eye - c;
    const double o[3] = {dot(e, ax[0]), dot(e, ax[1]), dot(e, ax[2])}, d[3] = {dot(dir, ax[0]), dot(dir, ax[1]), dot(dir, ax[2])};
    const double half[3] = {b.params.num("thick") * 0.5 + 0.01, b.params.num(keys::h) * 0.5 + 0.01, b.params.num(keys::w) * 0.5 + 0.01};
    return ray_slab(o, d, half);
}

// Where the eye's ray first meets something to put a thing on or against:
// the side of something that stands there (a wall, a cabinet), which way it
// faces; the top of something; the floor.
struct Hit {
    enum Kind { None, Top, Side } kind = None;
    double t = 1e9;
    Vec3d at, normal;
};
Hit aim_at(const Phys& ph, const Vec3d& eye, const Vec3d& dir, double reach, const std::string& holding) {
    Hit best;
    const Element* held = holding.empty() ? nullptr : ph.find(Key{holding});
    const double sheet_w = held ? held->params.num(keys::w) : 0.0, sheet_h = held ? held->params.num(keys::h) : 0.0;
    for (const Element& e : ph.elements()) {
        if (e.kind != kSupport || !e.alive) continue;
        const double yaw = e.params.num(keys::yaw), base = e.params.num("base"), top = e.params.num("top");
        const Vec3d c{e.params.num(keys::x), (base + top) * 0.5, e.params.num(keys::z)};
        const Vec3d o = rotate_xz(eye - c, -yaw), d = rotate_xz(dir, -yaw);
        const double half[3] = {e.params.num(keys::sx) * 0.5, (top - base) * 0.5, e.params.num(keys::sz) * 0.5};
        const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
        int axis = -1, sign = 0;
        const double t = ray_slab(oo, dd, half, &axis, &sign);
        if (t <= 0 || t > reach || t >= best.t) continue;
        if (axis == 1) {
            if (sign < 0) best = Hit{Hit::Top, t, eye + dir * t, {0, 1, 0}};
            continue;
        }
        // A side a sheet fits on; the edge of a top is the top it is on.
        Vec3d at = eye + dir * t;
        const double side_w = axis == 0 ? half[2] * 2 : half[0] * 2;
        if (top - at.y < 0.12 || top - base < sheet_h + 0.02 || side_w < sheet_w + 0.02) {
            best = Hit{Hit::Top, t, {at.x, std::max(top, at.y), at.z}, {0, 1, 0}};
            continue;
        }
        at.y = std::clamp(at.y, base + sheet_h * 0.5 + 0.01, top - sheet_h * 0.5 - 0.005);
        const Vec3d n = rotate_xz(axis == 0 ? Vec3d{double(sign), 0, 0} : Vec3d{0, 0, double(sign)}, yaw);
        best = Hit{Hit::Side, t, at, n};
    }
    {
        double t = 0;
        rigid::V3 n;
        if (ph.solids().ray({eye.x, eye.y, eye.z}, {dir.x, dir.y, dir.z}, reach, &t, true, holding, &n) && n.y > 0.7 && t < best.t)
            best = Hit{Hit::Top, t, eye + dir * t, {0, 1, 0}};
    }
    if (std::fabs(dir.y) > 1e-9) {
        const double tf = (ph.params().num("floor") - eye.y) / dir.y;
        if (tf > 0 && tf <= reach && tf < best.t) best = Hit{Hit::Top, tf, eye + dir * tf, {0, 1, 0}};
    }
    return best;
}

// Across turns it about the vertical, up and down tips it toward and away
// from the eye (x ahead, y up, z to the side).
rigid::M3 turned(const rigid::M3& rel, double dx, double dy) {
    return rigid::axis_angle({0, 1, 0}, -dx * 0.008) * rigid::axis_angle({0, 0, 1}, -dy * 0.008) * rel;
}

}  // namespace

std::vector<std::string> Hand::missing_ports(const StateGraph& g, const Room& room) {
    std::vector<std::string> out;
    if (!g.has_port(room.id(), work_event(room)))
        out.push_back("the hand cannot work " + room.id().str() + "'s moving parts: it has no port " + work_event(room).str() + " - declare it (`port " +
                      room.id().str() + " event " + work_event(room).str() + "`)");
    if (const physics::Bodies* ph = physics::bodies_of(g, room.id()))
        for (Key k : {Phys::grab_event(), Phys::hold_event(), Phys::drop_event(), Phys::pin_event(), Phys::lay_event()})
            if (!g.has_port(ph->id(), k))
                out.push_back("the hand cannot take up " + room.id().str() + "'s loose things: " + ph->id().str() + " has no port " + k.str() + " - declare it (`port " +
                              ph->id().str() + " event " + k.str() + "`)");
    return out;
}

bool Hand::frame(Engine& engine, const StateGraph& g, const Room& room, const HandButtons& b, double dt) {
    const bool click = b.left && !was_left_, rclick = b.right && !was_right_;
    was_left_ = b.left, was_right_ = b.right;
    const Element& cam = room.camera();
    const Vec3d eye_here = position_of(cam), dir_here = forward_of(cam);

    // A part worked: dragged while the button is held, let go when it is not.
    if (!working_.empty()) {
        const auto* there = dynamic_cast<const Room*>(g.find(working_in_));
        if (working_in_ != room.id() || !b.left || !b.free) {
            if (there) engine.send(there->id(), Event{work_event(*there), Params{}.set("part", working_.str()).set("phase", 2.0).set("held", worked_for_)});
            working_ = Key{};
            return false;
        }
        worked_for_ += dt;
        if (b.dx != 0.0 || b.dy != 0.0)
            engine.send(room.id(), Event{work_event(room), Params{}.set("part", working_.str()).set("phase", 1.0).set("dx", b.dx).set("dy", b.dy).set("held", worked_for_)});
        return true;
    }

    // The eye in the frame of the physics the room's loose things are in.
    const Pose frame = physics::physics_frame(room);
    const Vec3d eye = frame.position + rotate_xz(eye_here, frame.yaw), dir = rotate_xz(dir_here, frame.yaw);
    const double yaw = std::atan2(dir.z, dir.x);

    // Something in hand in another room (carried through a doorway): let go there.
    if (!held_.empty() && held_in_ != room.id()) let_go(engine, g);
    const physics::Bodies* ph = physics::bodies_of(g, room.id());
    if (!b.free) return false;

    if (!held_.empty() && ph) {
        const Element* body = ph->find(held_);
        if (!body) {
            held_ = Key{}, inspecting_ = false;
            return false;
        }
        const bool solid = is_solid(*body);
        const double turn_dx = b.turn && !inspecting_ ? b.dx : 0.0, turn_dy = b.turn && !inspecting_ ? b.dy : 0.0;
        if (inspecting_) {
            inspect_yaw_ = std::remainder(inspect_yaw_ + b.dx * 0.006, 2 * kPi);
            inspect_pitch_ = std::remainder(inspect_pitch_ + b.dy * 0.006, 2 * kPi);
        }
        if (grab_reach_ >= 0.0) {
            if (held_free(*ph, held_)) hold_dist_ = std::max(grab_reach_, 0.5);
            grab_reach_ = -1.0;
        }
        const bool free = solid && held_free(*ph, held_);
        if (b.look && !free) {
            inspecting_ = !inspecting_;
            inspect_yaw_ = inspect_pitch_ = 0;
            if (solid) {
                const rigid::Body* rb = ph->solids().find(held_.str());
                inspect_dist_ = rb ? std::clamp(rigid::World::reach_of(*rb) * 1.8 + 0.12, 0.2, 1.2) : 0.3;
            } else {
                inspect_dist_ = 0.26;
            }
        }
        const auto tell = [&](Key event, Params p) { engine.send(ph->id(), Event{event, std::move(p)}); };
        if (inspecting_ && solid) {
            // Brought up close; the mouse turns it over every way, the wheel brings it nearer.
            inspect_dist_ = clampd(inspect_dist_ - b.wheel * 0.03, 0.14, 1.4);
            const rigid::M3 look = rigid::from_euler(yaw, 0, 0) * turned(hold_rel_, -inspect_yaw_ / 0.008, -inspect_pitch_ / 0.008);
            double ty = 0, tp = 0, tr = 0;
            rigid::to_euler(look, ty, tp, tr);
            tell(Phys::hold_event(), pose_args(held_, eye + dir * inspect_dist_, ty, tp, tr));
            if (click || rclick) {
                // Done looking: it stays turned as it was looked at.
                hold_rel_ = turned(hold_rel_, -inspect_yaw_ / 0.008, -inspect_pitch_ / 0.008);
                inspecting_ = false;
            }
            return true;
        }
        if (inspecting_) {
            // Paper up close and square to the eye; the mouse turns it over.
            inspect_dist_ = clampd(inspect_dist_ - b.wheel * 0.03, 0.14, 0.6);
            tell(Phys::hold_event(), pose_args(held_, eye + dir * inspect_dist_, std::atan2(-dir.z, -dir.x) + inspect_yaw_, std::asin(-dir.y) + inspect_pitch_, hold_roll_));
            if (click || rclick) inspecting_ = false;
            return true;
        }
        // (The wheel, while a solid is on its way down: back in front.)
        if (placing_ && b.wheel != 0.0) placing_ = false;
        else hold_dist_ = clampd(hold_dist_ - b.wheel * 0.05, 0.3, 1.2);
        if (!solid && b.shift) hold_rel_ = hold_rel_ * rigid::axis_angle({1, 0, 0}, turn_dx * 0.008);
        else hold_rel_ = turned(hold_rel_, turn_dx, turn_dy);
        if (free) {
            // Held where it was taken: that point follows the hand - it drags,
            // lifts at that end, tips over. Pulled too far from it, let go.
            hold_dist_ = std::max(hold_dist_, 0.5);
            const Vec3d p = eye + dir * hold_dist_;
            const rigid::M3 r = rigid::from_euler(body->params.num(keys::yaw), body->params.num(keys::pitch), body->params.num(keys::roll));
            const rigid::V3 gp = rigid::V3{body->params.num(keys::x), body->params.num(keys::y), body->params.num(keys::z)} +
                                 r * rigid::V3{body->params.num("gx"), body->params.num("gy"), body->params.num("gz")};
            tell(Phys::hold_event(), pose_args(held_, p, 0, 0, 0));
            if (rclick || !b.left || rigid::length(gp - rigid::V3{p.x, p.y, p.z}) > 1.2) {
                tell(Phys::drop_event(), Params{}
                                             .set("id", held_.str())
                                             .set(keys::vx, body->params.num(keys::vx))
                                             .set(keys::vy, body->params.num(keys::vy))
                                             .set(keys::vz, body->params.num(keys::vz))
                                             .set("keep", 1.0));
                held_ = Key{};
            }
            return b.turn;
        }
        if (solid) {
            // Carried whole, standing as it was made, out in front of the eye
            // and a little below it. On its way down (a click), looking at a
            // top, it hangs just clear above the spot, as it will stand: a
            // click again, and it does.
            if (b.tip) hold_rel_ = rigid::axis_angle({1, 0, 0}, kPi * 0.5) * hold_rel_;
            const rigid::M3 turn = rigid::from_euler(yaw, 0.0, 0.0) * hold_rel_;
            double ty = 0, tp = 0, tr = 0;
            rigid::to_euler(turn, ty, tp, tr);
            const rigid::Body* rb = ph->solids().find(held_.str());
            const double clear = rb ? rigid::World::reach_of(*rb) : 0.1;
            rigid::V3 at{};
            const bool over = placing_ && ph->solids().hover(held_.str(), {eye.x, eye.y, eye.z}, {dir.x, dir.y, dir.z}, kReach + clear, turn, at);
            const Vec3d p = over ? Vec3d{at.x, at.y, at.z} : eye + dir * std::max(hold_dist_, clear + 0.3) + Vec3d{0, -0.06, 0};
            tell(Phys::hold_event(), pose_args(held_, p, ty, tp, tr));
        } else {
            // Paper faces the eye, then is turned from there (Q a quarter in its plane).
            if (b.tip) hold_rel_ = rigid::axis_angle({1, 0, 0}, kPi * 0.5) * hold_rel_;
            const rigid::M3 turn = rigid::from_euler(std::atan2(-dir.z, -dir.x), std::asin(-dir.y), 0.0) * hold_rel_;
            double ty = 0, tp = 0, tr = 0, ry = 0, rp = 0;
            rigid::to_euler(turn, ty, tp, tr);
            rigid::to_euler(hold_rel_, ry, rp, hold_roll_);
            tell(Phys::hold_event(), pose_args(held_, eye + dir * hold_dist_, ty, tp, tr));
        }
        if (click && solid && !placing_) {
            placing_ = true;
        } else if (click && solid) {
            // Let go where it hangs: it drops the last centimetre and stands,
            // or topples, as it is shaped.
            tell(Phys::drop_event(), Params{}
                                         .set("id", held_.str())
                                         .set(keys::vx, body->params.num(keys::vx) * 0.3)
                                         .set(keys::vy, 0.0)
                                         .set(keys::vz, body->params.num(keys::vz) * 0.3)
                                         .set("keep", 1.0));
            held_ = Key{};
        } else if (click) {
            const Hit hit = aim_at(*ph, eye, dir, 3.0, held_.str());
            if (hit.kind == Hit::Side) {
                const Vec3d at = hit.at + hit.normal * (body->params.num("thick") * 0.5 + 0.004);
                tell(Phys::pin_event(), pose_args(held_, at, std::atan2(hit.normal.z, hit.normal.x), 0.0, hold_roll_));
            } else if (hit.kind == Hit::Top) {
                tell(Phys::lay_event(), pose_args(held_, hit.at, yaw + kPi + hold_roll_, physics::kFlat, 0));
            } else {
                tell(Phys::drop_event(), Params{}.set("id", held_.str()));
            }
            held_ = Key{};
        } else if (rclick) {
            tell(Phys::drop_event(), Params{}
                                         .set("id", held_.str())
                                         .set(keys::vx, dir.x * 3.0)
                                         .set(keys::vy, dir.y * 3.0 + 1.0)
                                         .set(keys::vz, dir.z * 3.0)
                                         .set("spin", 4.0));
            held_ = Key{};
        }
        return b.turn;
    }

    if (!click) return false;
    // What the hand is on: the nearest loose thing within reach...
    double aim_t = kReach;
    Key aim;
    if (ph) {
        for (const Element& e : ph->elements()) {
            if (e.kind != kBody || !e.alive) continue;
            const double t = ray_body(*ph, e, eye, dir, aim_t);
            if (t > 0 && t < aim_t) aim_t = t, aim = e.id;
        }
    }
    // ...or a part of the room that moves, if it is nearer.
    {
        Vec3d hit, n;
        double dist = 0;
        Key what;
        if (ray(room, eye_here, dir_here, kReach, hit, n, &dist, &what) && dist < aim_t)
            if (const Element* e = room.find(what); e && e->params.has(Key{"joint"})) {
                working_ = what, working_in_ = room.id(), worked_for_ = 0.0;
                engine.send(room.id(), Event{work_event(room), Params{}.set("part", what.str()).set("phase", 0.0).set("held", 0.0)});
                return true;
            }
    }
    // (Nothing on the ray: the loose thing nearest it, a few degrees off - a
    // toothbrush lying flat is a thin mark to hit.)
    if (aim.empty() && ph) {
        double best = 0.14;
        for (const Element& e : ph->elements()) {
            if (e.kind != kBody || !e.alive) continue;
            const Vec3d v = position_of(e) - eye;
            const double t = dot(v, dir);
            if (t < 0.2 || t > aim_t) continue;
            const double off = length(v - dir * t) / t;
            if (off < best) best = off, aim = e.id;
        }
        if (!aim.empty()) aim_t = length(position_of(ph->element(aim)) - eye);
    }
    if (aim.empty() || !ph) return false;
    const Element& body = ph->element(aim);
    held_ = aim, held_in_ = room.id();
    hold_roll_ = 0.0;
    inspecting_ = false, placing_ = false;
    // A solid is carried as it stood, facing as it faced; paper faces the eye.
    hold_rel_ = is_solid(body) ? rigid::transpose(rigid::from_euler(yaw, 0, 0)) *
                                     rigid::from_euler(body.params.num(keys::yaw), body.params.num(keys::pitch), body.params.num(keys::roll))
                               : rigid::M3{};
    if (is_solid(body)) hold_dist_ = 0.5;
    // Taken hold of where the hand is: something heavy stays held there and
    // hangs from it (Shift: taken by that point anyway - to drag it, tip it).
    const Vec3d grip = eye + dir * aim_t;
    engine.send(ph->id(), Event{Phys::grab_event(), Params{}.set("id", held_.str()).set("gx", grip.x).set("gy", grip.y).set("gz", grip.z).set("free", b.shift ? 1.0 : 0.0)});
    grab_reach_ = aim_t;
    return false;
}

void Hand::let_go(Engine& engine, const StateGraph& g) {
    if (!working_.empty()) {
        if (const auto* there = dynamic_cast<const Room*>(g.find(working_in_)))
            engine.send(there->id(), Event{work_event(*there), Params{}.set("part", working_.str()).set("phase", 2.0).set("held", worked_for_)});
        working_ = Key{};
    }
    if (held_.empty()) return;
    if (const physics::Bodies* ph = physics::bodies_of(g, held_in_)) engine.send(ph->id(), Event{Phys::drop_event(), Params{}.set("id", held_.str()).set("keep", 1.0)});
    held_ = Key{}, inspecting_ = false;
}

}  // namespace sg
