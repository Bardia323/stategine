#include "sg/domains/Physics.hpp"

#include "sg/domains/Ragdoll.hpp"

#include <set>

namespace sg::physics {

namespace {
// The kinds and names a step looks for in every element, named once.
const sg::Key kBody{"body"}, kLimb{"limb"}, kSupport{"support"}, kCable{"cable"}, kField{"field"}, kShape{"shape"};
}  // namespace

const std::string& text_of(const sg::Element& e, sg::Key k) {
    static const std::string none;
    const std::string* t = e.params.text(k);
    return t ? *t : none;
}

sg::Vec3d panel_turn(const sg::Vec3d& v, double yaw, double pitch, double roll) {
    const double cr = std::cos(roll), sr = std::sin(roll);
    const sg::Vec3d a{v.x, v.y * cr - v.z * sr, v.y * sr + v.z * cr};
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    const sg::Vec3d b{a.x * cp - a.y * sp, a.x * sp + a.y * cp, a.z};
    return sg::rotate_xz(b, yaw);
}

double angle_to(double a, double b) {
    double d = std::fmod(b - a, 2 * kPi);
    if (d > kPi) d -= 2 * kPi;
    if (d < -kPi) d += 2 * kPi;
    return d;
}

Physics::Physics(sg::Key id, double room_w, double room_d, double room_h) : sg::State(id) {
    params().set("room_w", room_w).set("room_d", room_d).set("room_h", room_h).set("g", 9.81);
    // The room's finishes stand a few millimetres off its shell: that is
    // where the floor is, and how close a thing can come to a wall.
    params().set("floor", 0.004).set("skin", 0.005);
    // `shell`: whether its space is the one box room_w x room_h x room_d
    // (1), or only what it is given - floors, ceilings and walls as
    // supports, the rooms of a space made one (0: physics::lay_space).
    params().set("shell", 1.0);
    //   rigid --step--> rigid
    add_element(sg::Key{"rigid"}, sg::Key{"solver"});
    says(moved_event());
    says(grabbed_event());
    says(knocked_event());
    says(cords_event());
    loop(sg::Key{"step.rigid"}, sg::Key{"rigid"}, step_event(),
         [](sg::State& s, sg::Element& solver, sg::Element*, const sg::Event& ev) {
             if (ev.args.num(sg::keys::dt) <= 0.0) return;  // no time, no change
             std::vector<sg::Key> falling;
             for (const auto& e : s.elements())
                 if (e.kind == kBody && e.params.get_or<std::string>("mode", "") == mode::fall) falling.push_back(e.id);
             static_cast<Physics&>(s).step_rigid(ev.args.num(sg::keys::dt), ev.args.num(sg::keys::time) - ev.args.num(sg::keys::dt));
             // What knocked this step, said: a body's own (`hit`), and heard as what it is
             // made of - one sound a kind a step (the cup and the brush in it landing
             // together are one clatter), as hard as the hardest of them, where that was.
             std::vector<std::pair<std::string, sg::Params>> heard;
             for (auto& e : s.elements()) {
                 if (e.kind != kBody) continue;
                 if (const double hit = e.params.num("hit"); hit > 0) {
                     s.emit(sg::Event{hit_event(), sg::Params{}.set("id", e.id.str()).set("hit", hit)});
                     e.params.set("hit", 0.0);
                     const std::string& m = text_of(e, sg::Key{"sound"});
                     if (m.empty()) continue;
                     sg::Params said;
                     said.set("id", e.id.str()).set("hit", hit).set("sound", "hit." + m).set("gain", std::clamp(hit / 1.5, 0.05, 1.0))
                         .set("pitch", 0.92 + 0.16 * std::fmod(hit * 7.31, 1.0))
                         .set(sg::keys::x, e.params.num(sg::keys::x)).set(sg::keys::y, e.params.num(sg::keys::y)).set(sg::keys::z, e.params.num(sg::keys::z));
                     auto it = std::find_if(heard.begin(), heard.end(), [&](const auto& h) { return h.first == m; });
                     if (it == heard.end()) heard.push_back({m, said});
                     else if (hit > it->second.num("hit")) it->second = said;
                 }
             }
             // (A kind knocked again within a sixth of a second - a bounce's rattle - is the
             // same sound still ringing: not said again, on this physics' own time.)
             const double heard_at = ev.args.num(sg::keys::time);
             for (auto& [m, said] : heard) {
                 const sg::Key last{"heard." + m};
                 if (s.params().has(last) && heard_at - s.params().num(last) < 0.16) continue;
                 s.params().set(last, heard_at);
                 s.emit(sg::Event{knocked_event(), said});
             }
             for (sg::Key id : falling)
                 if (const sg::Element* e = s.find(id); e && e->params.get_or<std::string>("mode", "") == mode::rest)
                     s.emit(sg::Event{landed_event(), sg::Params{}.set("id", id.str())});
             // Anything somewhere else than when it last said so - moved
             // by this step, or put there since - is said moved.
             uint64_t h = 1469598103934665603ULL;
             const auto mix = [&h](double v) {
                 uint64_t q = 0;
                 std::memcpy(&q, &v, sizeof q);
                 h = (h ^ q) * 1099511628211ULL;
             };
             for (const auto& e : s.elements()) {
                 if (&e == &solver) continue;
                 for (sg::Key k : {sg::keys::x, sg::keys::y, sg::keys::z, sg::keys::yaw}) mix(e.params.num(k));
             }
             const double now = static_cast<double>(h >> 11);
             if (solver.params.num("said") == now) return;
             solver.params.set("said", now);
             s.emit(moved_event());
         });
    //   rigid --knock--> rigid   someone walked into a limb: on meeting, what
    //   they carry; leaning on (met within a quarter second), their lean.
    loop(sg::Key{"knock.limb"}, sg::Key{"rigid"}, knock_event(), [](sg::State& s, sg::Element&, sg::Element*, const sg::Event& ev) {
        auto& self = static_cast<Physics&>(s);
        sg::Element* e = self.find(sg::Key{ev.args.get_or<std::string>("id", "")});
        if (!e || e->kind != kLimb) return;
        const bool met = e->params.num("leaning") <= 0.0;
        e->params.set("leaning", 0.25);
        self.knock(*e, met ? sg::rigid::V3{ev.args.num(sg::keys::x), ev.args.num(sg::keys::y), ev.args.num(sg::keys::z)}
                           : sg::rigid::V3{ev.args.num("lx"), ev.args.num("ly"), ev.args.num("lz")});
    });
    //   rigid --ride--> rigid
    loop(sg::Key{"ride.rigid"}, sg::Key{"rigid"}, ride_event(),
         [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { static_cast<Physics&>(s).ride_all(); });
}

sg::Element& Physics::add_body(sg::Key id, double w, double h, double thick, double drag, const std::string& shape) {
    sg::Element& b = add_element(id, kBody);
    b.params.set("shape", shape);
    b.params.set(sg::keys::x, 0.0).set(sg::keys::y, 0.0).set(sg::keys::z, 0.0);
    b.params.set(sg::keys::yaw, 0.0).set(sg::keys::pitch, kFlat).set(sg::keys::roll, 0.0);
    b.params.set(sg::keys::vx, 0.0).set(sg::keys::vy, 0.0).set(sg::keys::vz, 0.0);
    b.params.set(sg::keys::w, w).set(sg::keys::h, h).set("thick", thick).set("drag", drag);
    b.params.set("mode", mode::rest).set("then", mode::rest).set("spin", 0.0).set("phase", 0.0);
    b.params.set("wx", 0.0).set("wy", 0.0).set("wz", 0.0).set("hit", 0.0).set("still", 0.0);
    for (const char* k : {"tx", "ty", "tz", "tyaw", "tpitch", "troll"}) b.params.set(k, 0.0);

    const std::string name = id.str();
    loop(sg::Key{"step." + name}, id, step_event(),
         [](sg::State& s, sg::Element& e, sg::Element*, const sg::Event& ev) {
             static_cast<Physics&>(s).step(e, ev.args.num(sg::keys::dt));
         });
    loop(sg::Key{"grab." + name}, id, grab_event(),
         [](sg::State& s, sg::Element& e, sg::Element*, const sg::Event& ev) {
             if (!names(ev, e)) return;
             s.emit(sg::Event{grabbed_event(), sg::Params{}.set("id", e.id.str())});
             e.params.set("mode", mode::held).set("by", std::string("hand"));
             for (sg::Key k : {sg::keys::vx, sg::keys::vy, sg::keys::vz}) e.params.set(k, 0.0);
             aim(e, sg::position_of(e), e.params.num(sg::keys::yaw), e.params.num(sg::keys::pitch),
                 e.params.num(sg::keys::roll));
             // A solid thing too heavy to hold up whole is held by where
             // the hand took it (`gx`, `gy`, `gz`, in the room): the aim
             // is then where that point is to go.
             const std::string shape = e.params.get_or<std::string>("shape", "");
             // (So is anything not made to be carried - a whiteboard on
             // its castors - or anything the hand asks to take so, `free`.)
             const bool free = (shape == "rigid" || shape == "box") && ev.args.has(sg::Key{"gx"}) &&
                               (e.params.num("mass", 0.6) > kCarry || e.params.num("carry", 1.0) < 0.5 ||
                                ev.args.num("free", 0.0) > 0.5);
             e.params.set("free", free ? 1.0 : 0.0);
             if (free) {
                 const sg::rigid::M3 r = sg::rigid::from_euler(e.params.num(sg::keys::yaw), e.params.num(sg::keys::pitch),
                                                       e.params.num(sg::keys::roll));
                 const sg::rigid::V3 at{ev.args.num("gx"), ev.args.num("gy"), ev.args.num("gz")};
                 const sg::rigid::V3 local = sg::rigid::transpose(r) * (at - sg::rigid::V3{e.params.num(sg::keys::x), e.params.num(sg::keys::y),
                                                                               e.params.num(sg::keys::z)});
                 e.params.set("gx", local.x).set("gy", local.y).set("gz", local.z);
                 e.params.set("tx", at.x).set("ty", at.y).set("tz", at.z);
             }
         });
    loop(sg::Key{"hold." + name}, id, hold_event(),
         [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
             if (!names(ev, e) || e.params.get_or<std::string>("mode", "") != mode::held) return;
             aim(e, {ev.args.num(sg::keys::x), ev.args.num(sg::keys::y), ev.args.num(sg::keys::z)},
                 ev.args.num(sg::keys::yaw), ev.args.num(sg::keys::pitch), ev.args.num(sg::keys::roll));
         });
    loop(sg::Key{"drop." + name}, id, drop_event(),
         [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
             if (!names(ev, e)) return;
             e.params.set("mode", mode::fall);
             e.params.set(sg::keys::vx, ev.args.num(sg::keys::vx))
                 .set(sg::keys::vy, ev.args.num(sg::keys::vy))
                 .set(sg::keys::vz, ev.args.num(sg::keys::vz));
             e.params.set("spin", ev.args.num("spin", 1.5));
             // (`keep`: let go as it is moving now, turning as it turns.)
             if (solid_shape(e.params.get_or<std::string>("shape", "")) && ev.args.num("keep", 0.0) < 0.5) {
                 // Out of the hand end over end, a little each way.
                 const double s = ev.args.num("spin", 1.5);
                 const double h = std::fmod(std::fabs(e.params.num(sg::keys::x) * 97.1 + e.params.num(sg::keys::z) * 31.7), 1.0);
                 // Turning end over end about the axis across its flight,
                 // with a little twist about the vertical and its length.
                 const double vx = ev.args.num(sg::keys::vx), vz = ev.args.num(sg::keys::vz);
                 const double v = std::hypot(vx, vz);
                 const double ax = v > 1e-6 ? vz / v : 1.0, az = v > 1e-6 ? -vx / v : 0.0;
                 e.params.set("wx", ax * s * (1.2 + h) + (h - 0.5) * s)
                     .set("wy", (h - 0.5) * s * 1.5)
                     .set("wz", az * s * (1.2 + h) - (h - 0.5) * s * 0.7);
             }
         });
    loop(sg::Key{"pin." + name}, id, pin_event(),
         [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
             if (!names(ev, e)) return;
             aim(e, {ev.args.num(sg::keys::x), ev.args.num(sg::keys::y), ev.args.num(sg::keys::z)},
                 ev.args.num(sg::keys::yaw), 0.0, ev.args.num(sg::keys::roll));
             e.params.set("mode", mode::send).set("then", mode::pinned);
         });
    loop(sg::Key{"lay." + name}, id, lay_event(),
         [](sg::State& s, sg::Element& e, sg::Element*, const sg::Event& ev) {
             if (names(ev, e)) lay(static_cast<Physics&>(s), e, ev);
         });
    const auto fed_along = [](const sg::Element& e, const sg::Event& ev) {
        return names(ev, e) && e.params.get_or<std::string>("mode", "") == mode::held &&
               e.params.get_or<std::string>("by", "") != "hand";
    };
    loop(sg::Key{"feed." + name}, id, feed_event(),
         [fed_along](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
             if (!fed_along(e, ev)) return;
             place(e, {ev.args.num(sg::keys::x), ev.args.num(sg::keys::y), ev.args.num(sg::keys::z)},
                   ev.args.num(sg::keys::yaw), kFlat, 0.0, mode::held);
         });
    loop(sg::Key{"fed." + name}, id, fed_event(),
         [fed_along](sg::State& s, sg::Element& e, sg::Element*, const sg::Event& ev) {
             if (fed_along(e, ev)) lay(static_cast<Physics&>(s), e, ev);
         });
    return b;
}

void Physics::lay(Physics& self, sg::Element& e, const sg::Event& ev) {
    const double x = ev.args.num(sg::keys::x), z = ev.args.num(sg::keys::z);
    // A sheet lies face up; a book on its cover; a stick as it is.
    const std::string shape = e.params.get_or<std::string>("shape", "sheet");
    // (A thing of many parts stands as it was made.)
    const double pitch = shape == "sheet" ? kFlat : shape == "box" || shape == "rigid" ? 0.0 : kFlat;
    const double roll = shape == "box" ? kFlat : 0.0;
    sg::Element there = e;
    there.params.set(sg::keys::x, x).set(sg::keys::z, z).set(sg::keys::yaw, ev.args.num(sg::keys::yaw));
    there.params.set(sg::keys::pitch, pitch).set(sg::keys::roll, roll);
    const double top = self.top_under(there, ev.args.num(sg::keys::y) + 0.05);
    aim(e, {x, top + extents(there).y + 0.0005, z}, ev.args.num(sg::keys::yaw), pitch, roll);
    e.params.set("mode", mode::send).set("then", mode::rest);
}

const sg::rigid::Body* Physics::solid_of(sg::Key id) const {
    if (solids_layout_ != world_.layout()) {
        solids_.clear();
        for (std::size_t i = 0; i < world_.bodies.size(); ++i) solids_[sg::Key{world_.bodies[i].id}] = i;
        solids_layout_ = world_.layout();
    }
    const auto it = solids_.find(id);
    return it == solids_.end() ? nullptr : &world_.bodies[it->second];
}

auto Physics::lying(std::size_t i) const -> const Lying& {
    const auto& list = elements();
    if (lying_structure_ != structure() || lying_.size() != list.size()) {
        lying_.assign(list.size(), Lying{});
        lying_structure_ = structure();
    }
    const sg::Element& e = list[i];
    Lying& l = lying_[i];
    if (l.stamp != e.params.stamp()) {
        static const sg::Key mode_key{"mode"};
        const std::string& m = text_of(e, mode_key);
        l.solid = solid_shape(text_of(e, kShape));
        l.mode = m == mode::rest ? 'r' : m == mode::held ? 'h' : m == mode::fall ? 'f' : 0;
        l.ext = extents(e);
        l.stamp = e.params.stamp();
    }
    return l;
}

const sg::rigid::Body* Physics::solid_at(std::size_t i) const {
    lying(i);
    Lying& l = lying_[i];
    if (l.layout != world_.layout()) {
        const sg::rigid::Body* b = solid_of(elements()[i].id);
        l.body = b ? std::size_t(b - world_.bodies.data()) : ~std::size_t{0};
        l.layout = world_.layout();
    }
    return l.body < world_.bodies.size() ? &world_.bodies[l.body] : nullptr;
}

double Physics::top_at(double x, double z, double below, sg::Key self) const {
    // (No shell: the floors are its supports.)
    double top = params().num("shell", 1.0) > 0.5 ? params().num("floor") : -1e30;
    const auto& list = elements();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const sg::Element& e = list[i];
        if (!e.alive || e.id == self) continue;
        double t = -1.0;
        if (e.kind == kSupport) {
            const sg::Vec3d local = sg::rotate_xz({x - e.params.num(sg::keys::x), 0.0,
                                                   z - e.params.num(sg::keys::z)},
                                                  -e.params.num(sg::keys::yaw));
            if (std::fabs(local.x) <= e.params.num(sg::keys::sx) * 0.5 &&
                std::fabs(local.z) <= e.params.num(sg::keys::sz) * 0.5)
                t = e.params.num("top");
        } else if (e.kind == kBody) {
            const Lying& l = lying(i);
            if (l.solid) {
                // A solid thing is something to lie on however it is moving -
                // held in the hand, or falling - as it is turned now.
                if (l.mode != 0) {
                    // Its own surface there, as the physics has it turned; its
                    // box, before the physics has it.
                    if (const sg::rigid::Body* b = solid_at(i)) {
                        t = solid_top(*b, x, z, below);
                    } else {
                        const sg::Vec3d ext = l.ext;
                        if (std::fabs(x - e.params.num(sg::keys::x)) < ext.x && std::fabs(z - e.params.num(sg::keys::z)) < ext.z)
                            t = e.params.num(sg::keys::y) + ext.y;
                    }
                }
            } else if (l.mode == 'r') {
                const double reach = std::min(e.params.num(sg::keys::w), e.params.num(sg::keys::h)) * 0.5;
                if (std::hypot(x - e.params.num(sg::keys::x), z - e.params.num(sg::keys::z)) < reach)
                    t = e.params.num(sg::keys::y) + e.params.num("thick") * 0.5;
            }
        }
        if (t <= below && t > top) top = t;
    }
    return top;
}

double Physics::top_under(const sg::Element& e, double below) const {
    double top = top_at(e.params.num(sg::keys::x), e.params.num(sg::keys::z), below, e.id);
    const sg::Vec3d me = extents(e);
    const double x = e.params.num(sg::keys::x), z = e.params.num(sg::keys::z);
    const auto& list = elements();
    for (std::size_t i = 0; i < list.size(); ++i) {
        const sg::Element& o = list[i];
        if (!o.alive || o.id == e.id || o.kind != kBody) continue;
        const Lying& l = lying(i);
        if (l.mode != 'r') continue;
        // A solid body: the top of its parts right under here (the
        // whiteboard's tray, not the top of its board).
        if (const sg::rigid::Body* rb = solid_at(i)) {
            const double t = sg::rigid::surface_at(*rb, x, z, below);
            if (t > top) top = t;
            continue;
        }
        const sg::Vec3d ext = l.ext;
        if (std::fabs(o.params.num(sg::keys::x) - x) >= me.x + ext.x - 1e-4) continue;
        if (std::fabs(o.params.num(sg::keys::z) - z) >= me.z + ext.z - 1e-4) continue;
        const double t = o.params.num(sg::keys::y) + ext.y;
        if (t <= below && t > top) top = t;
    }
    return top;
}

void Physics::haul(sg::Element& b, const sg::Vec3d& p, double yaw, double pitch, double roll, const std::string& m) {
    if (!b.params.has(sg::Key{"was_x"}))
        b.params.set("was_x", b.params.num(sg::keys::x)).set("was_y", b.params.num(sg::keys::y)).set("was_z", b.params.num(sg::keys::z))
            .set("was_yaw", b.params.num(sg::keys::yaw)).set("was_pitch", b.params.num(sg::keys::pitch))
            .set("was_roll", b.params.num(sg::keys::roll));
    place(b, p, yaw, pitch, roll, m);
}

void Physics::forget_was(sg::Element& e) {
    for (const char* k : {"was_x", "was_y", "was_z", "was_yaw", "was_pitch", "was_roll"}) e.params.erase(sg::Key{k});
}

void Physics::place(sg::Element& b, const sg::Vec3d& p, double yaw, double pitch, double roll, const std::string& m) {
    sg::set_position(b, p);
    b.params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch).set(sg::keys::roll, roll);
    b.params.set("mode", m);
    aim(b, p, yaw, pitch, roll);
}

sg::Vec3d Physics::extents(const sg::Element& e) {
    const double w = e.params.num(sg::keys::w) * 0.5, h = e.params.num(sg::keys::h) * 0.5;
    const double t = e.params.num("thick") * 0.5;
    const double yaw = e.params.num(sg::keys::yaw);
    static const std::string sheet = "sheet";
    const std::string* held = e.params.text(kShape);
    const std::string& shape = held ? *held : sheet;
    if (solid_shape(shape)) {
        // Its three half-lengths - w along x, thick up y, h along z - turned
        // as it is turned, as the renderer turns a mesh.
        const double yaw = e.params.num(sg::keys::yaw), pitch = e.params.num(sg::keys::pitch),
                     roll = e.params.num(sg::keys::roll);
        const sg::Vec3d a = box_turn({1, 0, 0}, yaw, pitch, roll), b = box_turn({0, 1, 0}, yaw, pitch, roll),
                        c = box_turn({0, 0, 1}, yaw, pitch, roll);
        return {std::fabs(a.x) * w + std::fabs(b.x) * t + std::fabs(c.x) * h,
                std::fabs(a.y) * w + std::fabs(b.y) * t + std::fabs(c.y) * h,
                std::fabs(a.z) * w + std::fabs(b.z) * t + std::fabs(c.z) * h};
    }
    if (shape == "stick") {
        const double c = std::fabs(std::cos(yaw)), s = std::fabs(std::sin(yaw));
        return {c * w + s * h, t, s * w + c * h};
    }
    const double pitch = e.params.num(sg::keys::pitch), roll = e.params.num(sg::keys::roll);
    const sg::Vec3d f = panel_turn({1, 0, 0}, yaw, pitch, roll);
    const sg::Vec3d u = panel_turn({0, 1, 0}, yaw, pitch, roll);
    const sg::Vec3d s = panel_turn({0, 0, 1}, yaw, pitch, roll);
    return {std::fabs(f.x) * t + std::fabs(u.x) * h + std::fabs(s.x) * w,
            std::fabs(f.y) * t + std::fabs(u.y) * h + std::fabs(s.y) * w,
            std::fabs(f.z) * t + std::fabs(u.z) * h + std::fabs(s.z) * w};
}

void Physics::collide(sg::Element& e, bool floors) const {
    const sg::Vec3d ext = extents(e);
    double x = e.params.num(sg::keys::x), y = e.params.num(sg::keys::y), z = e.params.num(sg::keys::z);
    const double x0 = x, y0 = y, z0 = z;
    const double skin = params().num("skin"), floor = params().num("floor");
    const auto shell = [skin](double v, double half, double size) {
        half += skin;
        return half * 2 >= size ? size * 0.5 : std::clamp(v, half, size - half);
    };
    if (params().num("shell", 1.0) > 0.5) {
        x = shell(x, ext.x, params().num("room_w"));
        if (floors) y = std::max(shell(y, ext.y, params().num("room_h")), floor + ext.y);
        z = shell(z, ext.z, params().num("room_d"));
    }

    for (const auto& s : elements()) {
        if (s.kind != kSupport || !s.alive) continue;
        const double top = s.params.num("top"), base = s.params.num("base");
        if (y - ext.y >= top || y + ext.y <= base) continue;
        const double syaw = s.params.num(sg::keys::yaw);
        const sg::Vec3d local = sg::rotate_xz({x - s.params.num(sg::keys::x), 0.0, z - s.params.num(sg::keys::z)}, -syaw);
        const double hx = s.params.num(sg::keys::sx) * 0.5 + ext.x;
        const double hz = s.params.num(sg::keys::sz) * 0.5 + ext.z;
        if (std::fabs(local.x) >= hx || std::fabs(local.z) >= hz) continue;
        const double up = top - (y - ext.y), down = (y + ext.y) - base;
        const double px = hx - std::fabs(local.x), pz = hz - std::fabs(local.z);
        const double least = std::min({up, down, px, pz});
        if (!floors && (least == up || least == down)) continue;
        if (least == up) {
            y += up;
        } else if (least == px || least == pz) {
            const sg::Vec3d push = least == px ? sg::Vec3d{local.x >= 0 ? px : -px, 0, 0}
                                               : sg::Vec3d{0, 0, local.z >= 0 ? pz : -pz};
            const sg::Vec3d world = sg::rotate_xz(push, syaw);
            x += world.x;
            z += world.z;
        } else {
            y -= down;
        }
    }
    if (x != x0) e.params.set(sg::keys::vx, 0.0);
    if (z != z0) e.params.set(sg::keys::vz, 0.0);
    if (y != y0) e.params.set(sg::keys::vy, 0.0);
    e.params.set(sg::keys::x, x).set(sg::keys::y, y).set(sg::keys::z, z);
}

sg::Vec3d Physics::box_turn(sg::Vec3d v, double yaw, double pitch, double roll) {
    const double cr = std::cos(roll), sr = std::sin(roll);
    v = {v.x, v.y * cr - v.z * sr, v.y * sr + v.z * cr};
    const double cp = std::cos(pitch), sp = std::sin(pitch);
    v = {v.x * cp - v.y * sp, v.x * sp + v.y * cp, v.z};
    const double cy = std::cos(yaw), sy = std::sin(yaw);
    return {v.x * cy - v.z * sy, v.y, v.x * sy + v.z * cy};
}

void Physics::add_hull(sg::Element& e, bool cylinder, sg::Vec3d centre, sg::Vec3d half, double yaw, double pitch, double roll, double taper, double friction) {
    const std::string h = e.params.get_or<std::string>("hulls", "");
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s%c %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f", h.empty() ? "" : ";",
                  cylinder ? 'c' : 'b', centre.x, centre.y, centre.z, half.x, half.y, half.z, yaw, pitch, roll, taper, friction);
    e.params.set("hulls", h + buf);
}

std::vector<sg::rigid::Hull> Physics::hulls_of(const sg::Element& e) {
    std::vector<sg::rigid::Hull> out;
    const sg::rigid::V3 half{e.params.num(sg::keys::w) * 0.5, e.params.num("thick") * 0.5, e.params.num(sg::keys::h) * 0.5};
    if (e.params.get_or<std::string>("shape", "") == "box") {
        out.push_back(sg::rigid::Hull::box({}, half));
        return out;
    }
    const std::string text = e.params.get_or<std::string>("hulls", "");
    std::size_t from = 0;
    while (from < text.size()) {
        const std::size_t end = std::min(text.find(';', from), text.size());
        const std::string one = text.substr(from, end - from);
        from = end + 1;
        char kind = 'b';
        double v[11] = {};
        v[10] = -1.0;
        if (std::sscanf(one.c_str(), " %c %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf", &kind, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                        &v[6], &v[7], &v[8], &v[9], &v[10]) < 7)
            continue;
        const sg::rigid::M3 turn = sg::rigid::from_euler(v[6], v[7], v[8]);
        if (kind == 'c') out.push_back(sg::rigid::Hull::prism({v[0], v[1], v[2]}, {v[3], v[4], v[5]}, 20, turn, v[9] > 0 ? v[9] : 1.0));
        else out.push_back(sg::rigid::Hull::box({v[0], v[1], v[2]}, {v[3], v[4], v[5]}, turn));
        out.back().friction = v[10];
    }
    if (out.empty()) out.push_back(sg::rigid::Hull::box({}, half));
    return out;
}

sg::Element& Physics::add_limb(sg::Key id, const sg::Vec3d& origin, double yaw, int group) {
    sg::Element& e = add_element(id, kLimb);
    e.params.set("ox", origin.x).set("oy", origin.y).set("oz", origin.z).set("oyaw", yaw).set("group", double(group));
    e.params.set("knock_n", 0.0).set("kx", 0.0).set("ky", 0.0).set("kz", 0.0).set("mass", 1.0);
    return e;
}

bool Physics::place_limbs(std::vector<Limb>& out) {
    // Each limb where it was put last step (kept in its params: the solver's
    // copy is only a cache) and where its owner has it now.
    bool into = false;
    for (auto& e : elements()) {
        if (e.kind != kLimb || !e.alive) continue;
        sg::rigid::Body* b = solid_of(e.id);
        if (!b) b = &world_.add(sg::Ragdoll::body(e, e.id.str(), int(e.params.num("group"))));
        sg::rigid::V3 x;
        sg::rigid::M3 r;
        sg::Ragdoll::pose_in(e, {e.params.num("ox"), e.params.num("oy"), e.params.num("oz")}, e.params.num("oyaw"), x, r);
        const bool was = e.params.has(sg::Key{"was_lx"});
        b->x = was ? sg::rigid::V3{e.params.num("was_lx"), e.params.num("was_ly"), e.params.num("was_lz")} : x;
        b->r = was ? sg::spatial::from_quat(e.params.num("was_lqw", 1.0), e.params.num("was_lqx"), e.params.num("was_lqy"), e.params.num("was_lqz")) : r;
        b->v = {}, b->w = {};
        b->idle = 0, b->awake = true, b->hit = 0;  // (a limb is never still of its own: its owner moves it)
        b->place();
        out.push_back({b, x, r});
        double qw, qx, qy, qz;
        sg::spatial::to_quat(r, qw, qx, qy, qz);
        e.params.set("was_lx", x.x).set("was_ly", x.y).set("was_lz", x.z).set("was_lqw", qw).set("was_lqx", qx).set("was_lqy", qy).set("was_lqz", qz);
        if (into || sg::rigid::length(x - b->x) < 1e-6) continue;
        // Moved: into something lying at rest? (Then the room steps, and it
        // is pushed aside.)
        const sg::rigid::V3 from = b->x;
        const sg::rigid::M3 from_r = b->r;
        b->x = x, b->r = r;
        b->place();
        for (const sg::rigid::Body& t : world_.bodies) {
            // (Its box first: naming a body is the dear part.)
            if (!t.dynamic() || t.awake) continue;
            if (t.hi.x < b->lo.x || t.lo.x > b->hi.x || t.hi.y < b->lo.y || t.lo.y > b->hi.y || t.hi.z < b->lo.z || t.lo.z > b->hi.z) continue;
            if (const sg::Element* te = find(sg::Key{t.id}); te == nullptr || te->kind != kBody) continue;
            into = true;
            break;
        }
        b->x = from, b->r = from_r;
        b->place();
    }
    return into;
}

void Physics::knock(sg::Element& limb, sg::rigid::V3 j) {
    // Into its owner's frame, counted.
    const sg::rigid::V3 k = sg::rigid::transpose(sg::rigid::from_euler(limb.params.num("oyaw"), 0, 0)) * j;
    limb.params.set("kx", k.x).set("ky", k.y).set("kz", k.z).set("knock_n", limb.params.num("knock_n") + 1.0);
}

void Physics::knock_limbs() {
    // What each limb took this step from the loose things it met (not the
    // walls and furniture it is pressed against: those are its owner's
    // ragdoll's to meet): a knock, if it was more than a brush.
    for (std::size_t i = 0; i < world_.bodies.size(); ++i) {
        sg::Element* e = find(sg::Key{world_.bodies[i].id});
        if (!e || e->kind != kLimb) continue;
        sg::rigid::V3 j{};
        for (const sg::rigid::World::Took& t : world_.took(i)) {
            const sg::rigid::Body& other = world_.bodies[t.from];
            const sg::Element* oe = find(sg::Key{other.id});
            if (other.dynamic() && oe && oe->kind == kBody) j = j + t.impulse;
        }
        if (sg::rigid::length(j) >= 0.4) knock(*e, j);
    }
}

sg::rigid::V3 Physics::walk_into(sg::rigid::V3 p, double radius, double y0, double y1, sg::rigid::V3 moved, double dt, std::vector<Shove>* limbs) {
    std::vector<std::string> shoved;
    const sg::rigid::V3 q = world_.walk_into(p, radius, y0, y1, moved, dt, &shoved);
    // Someone walked into: what they carry (70 kg at their speed, three
    // tenths of it given: walking into someone sways them, sprinting makes
    // them step) and how hard they lean - harder the faster they come, so
    // walking leans less than feet brace against and sprinting more - once a
    // body; said, for the walker to tell; the limb's own arrow (knock_event)
    // decides which it takes.
    std::set<int> met;
    for (const std::string& id : shoved) {
        const sg::Element* e = find(sg::Key{id});
        if (!e || e->kind != kLimb || !limbs) continue;
        if (!met.insert(int(e->params.num("group"))).second) continue;
        const double l = sg::rigid::length(moved);
        if (l < 1e-6) continue;  // (standing against it, not walking into it: nothing to tell)
        const double speed = dt > 0 ? l / dt : 0.0;
        limbs->push_back({id, dt > 0 ? moved * (70.0 * 0.3 / dt) : sg::rigid::V3{}, l > 1e-9 ? moved * ((300.0 + 100.0 * speed) * dt / l) : sg::rigid::V3{}});
    }
    for (const std::string& id : shoved) {
        const sg::rigid::Body* b = world_.find(id);
        sg::Element* e = find(sg::Key{id});
        if (e && e->kind == kLimb) continue;
        if (!b || !e || e->params.get_or<std::string>("mode", "") == mode::held) continue;
        e->params.set(sg::keys::vx, b->v.x).set(sg::keys::vy, b->v.y).set(sg::keys::vz, b->v.z);
        e->params.set("wx", b->w.x).set("wy", b->w.y).set("wz", b->w.z).set("mode", mode::fall).set("still", 0.0);
    }
    return q;
}

void Physics::step_rigid(double dt, double time) {
    if (dt <= 0.0) return;
    dt = std::min(dt, 0.05);
    bool any = statics();
    // Whatever lies on, or is stuck to, something that has moved goes
    // with it - before anything is stepped from where it was.
    for (auto& e : elements())
        if (e.kind == kBody) ride(e);
    uint64_t in = 1469598103934665603ULL;
    const auto mix = [&](double v) {
        uint64_t q = 0;
        std::memcpy(&q, &v, sizeof q);
        in = (in ^ q) * 1099511628211ULL;
    };
    // Solver inputs are derived afresh from this state's declared data.
    world_.fields = {sg::field::Source::directional("gravity", {0, -params().num("g", 9.81), 0})};
    mix(params().num("g", 9.81));
    for (const auto& e : elements()) {
        if (!e.alive || e.kind != kField) continue;
        const auto& p = e.params;
        const std::string channel = p.get_or<std::string>("channel", "gravity");
        const std::string shape = p.get_or<std::string>("field_shape", "directional");
        const sg::spatial::V3 at{p.num("x"), p.num("y"), p.num("z")};
        sg::field::Source source;
        if (shape == "radial") source = sg::field::Source::radial(channel, at, p.num("strength", 1), p.num("exponent"));
        else if (shape == "plane") source = sg::field::Source::plane(channel, at, {p.num("nx"), p.num("ny", 1), p.num("nz")}, p.num("strength", 1));
        else {
            source = sg::field::Source::directional(channel, {p.num("dx"), p.num("dy"), p.num("dz")});
            source.pose.translation = at;
            source.strength = p.num("strength", 1);
        }
        source.value.scalar = p.num("scalar");
        source.softening = p.num("softening", 0.01);
        if (p.has("radius")) {
            const double r = std::max(0.0, p.num("radius"));
            source.bounds = sg::spatial::Aabb{{-r, -r, -r}, {r, r, r}};
        }
        mix(static_cast<double>(p.stamp()));
        world_.fields.push_back(std::move(source));
    }
    for (auto& e : elements()) {
        if (e.kind != kBody || !solid_shape(e.params.get_or<std::string>("shape", ""))) continue;
        const std::string& id = e.id.str();
        const std::string m = e.params.get_or<std::string>("mode", mode::rest);
        sg::rigid::Body* b = solid_of(e.id);
        const bool sim = e.alive && (m == mode::rest || m == mode::fall || m == mode::held);
        if (!sim) {
            if (b) world_.remove(id);
            continue;
        }
        const bool fresh = !b;
        if (!b) {
            sg::rigid::Body nb;
            nb.id = id;
            nb.hulls = hulls_of(e);
            nb.friction = e.params.num("friction", 0.55);
            nb.restitution = e.params.num("bounce", 0.2);
            nb.set_mass(e.params.num("mass", 0.6));
            b = &world_.add(std::move(nb));
        }
        // Where and how it is, as its element says.
        const sg::rigid::V3 x{e.params.num(sg::keys::x), e.params.num(sg::keys::y), e.params.num(sg::keys::z)};
        const sg::rigid::M3 r = sg::rigid::from_euler(e.params.num(sg::keys::yaw), e.params.num(sg::keys::pitch), e.params.num(sg::keys::roll));
        // Put there from outside, and not driven there: what lies on it goes
        // with it, as it lay (a thing at rest, not made just now).
        const auto put_there = [&] {
            const Carry c = Carry::between(b->x, b->r, x, r);
            if (!fresh && m == mode::rest && c.moved()) carry(*b, c);
            b->x = x, b->r = r;
            b->place();
        };
        if (e.params.has(sg::Key{"was_x"})) {
            // Hauled: from where it was, driven where it is now - all of
            // it read from its element, the same however often it is run.
            b->x = {e.params.num("was_x"), e.params.num("was_y"), e.params.num("was_z")};
            b->r = sg::rigid::from_euler(e.params.num("was_yaw"), e.params.num("was_pitch"), e.params.num("was_roll"));
            b->place();
            forget_was(e);
            if (m != mode::held && nudged(*b, x, r)) {
                world_.drive(*b, x, r);
                any = true;
                mix(1.0);
            } else {
                put_there();
            }
        } else if (sg::rigid::length(b->x - x) > 0 || b->r.a != r.a) {
            // Put somewhere from outside (a command, a reset): put there.
            put_there();
        }
        b->awake = m != mode::rest;
        b->v = b->awake ? sg::rigid::V3{e.params.num(sg::keys::vx), e.params.num(sg::keys::vy), e.params.num(sg::keys::vz)} : sg::rigid::V3{};
        b->w = b->awake ? sg::rigid::V3{e.params.num("wx"), e.params.num("wy"), e.params.num("wz")} : sg::rigid::V3{};
        b->idle = e.params.num("still", 0.0);
        any = any || b->awake;
        for (sg::Key k : {sg::keys::x, sg::keys::y, sg::keys::z, sg::keys::yaw, sg::keys::pitch, sg::keys::roll})
            mix(e.params.num(k));
        mix(b->v.x), mix(b->v.y), mix(b->v.z), mix(b->w.x), mix(b->w.y), mix(b->w.z), mix(b->idle), mix(b->awake ? 1.0 : 0.0);
        // In the hand: pulled there by a spring.
        if (m == mode::held) {
            const bool free = e.params.num("free", 0.0) > 0.5;
            const double mass = e.params.num("mass", 0.6);
            sg::rigid::World::Grab* g = world_.grabbing(id);
            if (!g) g = &world_.grab(id, {}, x, 0.0);
            g->local = free ? sg::rigid::V3{e.params.num("gx"), e.params.num("gy"), e.params.num("gz")} : b->com_local;
            g->target = within_cords(id, {e.params.num("tx"), e.params.num("ty"), e.params.num("tz")});
            // Something standing on the floor (a whiteboard on castors) is
            // pushed and pulled along it, never lifted: the hand pulls
            // only across.
            if (free && e.params.num("grounded", 0.0) > 0.5) g->target.y = (b->x + b->r * g->local).y;
            if (free) {
                // As strong as an arm: enough to lift one end of a table,
                // not to hold a wardrobe up; and no more than that thing
                // can take without being pushed into what it is against.
                g->force = std::min(650.0, mass * 90.0);
                g->hertz = 4.0;
                g->turns = false;
            } else {
                g->force = mass * (9.81 * 2.0 + 50.0);
                g->hertz = 7.0;
                g->turns = true;
                g->turn = sg::rigid::from_euler(e.params.num("tyaw"), e.params.num("tpitch"), e.params.num("troll"));
                g->torque = mass * 4.0 + 0.02;
            }
            mix(g->target.x), mix(g->target.y), mix(g->target.z), mix(e.params.num("tyaw")), mix(free ? 1.0 : 0.0);
        } else if (world_.grabbing(id)) {
            world_.release(id);
            b->awake = m != mode::rest;
        }
    }
    // Someone's limbs: where their owners have them - driven there, if the
    // room steps (pushing what is in their way), else simply put there.
    for (auto& e : elements())
        if (e.kind == kLimb && e.params.num("leaning") > 0) e.params.set("leaning", std::max(0.0, e.params.num("leaning") - dt));
    std::vector<Limb> limbs;
    if (place_limbs(limbs)) any = true;
    for (const Limb& l : limbs) {
        // (Where it was and where it goes: both are what this step starts from.)
        for (double v : {l.body->x.x, l.body->x.y, l.body->x.z, l.x.x, l.x.y, l.x.z}) mix(v);
        for (double v : l.body->r.a) mix(v);
        for (double v : l.r.a) mix(v);
        if (any) {
            l.body->awake = true;
            world_.drive(*l.body, l.x, l.r);
        } else {
            l.body->x = l.x, l.body->r = l.r;
            l.body->v = {}, l.body->w = {};
            l.body->place();
        }
    }
    if (!any) {
        cables(dt);
        return;
    }
    // The same start as last step's (a law trying the arrow twice): from
    // the same contacts. Otherwise, remember what this one starts from.
    {
        auto it = std::find_if(memo_.begin(), memo_.end(), [&](const auto& m) { return m.first == in; });
        if (it != memo_.end()) {
            world_.set_contacts(it->second);
        } else {
            memo_.emplace_back(in, world_.contacts());
            if (memo_.size() > 16) memo_.erase(memo_.begin());
        }
    }
    // Sheets and prints lying on something that moves go with it -
    // friction, as cheaply as it can be had: stuck to it while it tilts
    // less than they would slide at and speeds up or slows down less
    // than their grip, let go (sliding off, as fast as it was going
    // under them) when it does more.
    struct Rider {
        sg::Element* e;
        std::size_t body;
        sg::rigid::V3 x;
        sg::rigid::M3 r;
        sg::rigid::V3 v;
    };
    std::vector<Rider> riders;
    for (std::size_t i = 0; i < world_.bodies.size(); ++i) {
        const sg::rigid::Body& b = world_.bodies[i];
        if (!b.dynamic() || !b.awake) continue;
        for (auto& s : elements()) {
            if (s.kind != kBody || !s.alive || s.params.get_or<std::string>("shape", "") != "sheet" ||
                s.params.get_or<std::string>("mode", "") != mode::rest)
                continue;
            const double sx = s.params.num(sg::keys::x), sz = s.params.num(sg::keys::z);
            if (sx < b.lo.x || sx > b.hi.x || sz < b.lo.z || sz > b.hi.z) continue;
            // (By its middle, which lies on the surface however that is tipped.)
            const double sy = s.params.num(sg::keys::y);
            if (std::fabs(sy - solid_top(b, sx, sz, sy + 0.01)) > 0.015) continue;
            riders.push_back(Rider{&s, i, b.x, b.r, b.v});
        }
    }
    world_.step(dt, time);
    if (!limbs.empty()) knock_limbs();
    for (const Rider& rd : riders) {
        const sg::rigid::Body& b = world_.bodies[rd.body];
        sg::Element& s = *rd.e;
        sg::rigid::V3 at{s.params.num(sg::keys::x), s.params.num(sg::keys::y), s.params.num(sg::keys::z)};
        sg::rigid::M3 face = sg::rigid::from_euler(s.params.num(sg::keys::yaw), s.params.num(sg::keys::pitch), s.params.num(sg::keys::roll));
        sg::rigid::V3 v;
        if (sg::rigid::ride(b, rd.x, rd.r, rd.v, dt, at, face, {1, 0, 0}, &v)) {
            double yaw = 0, pitch = 0, roll = 0;
            sg::rigid::to_euler(face, yaw, pitch, roll);
            s.params.set(sg::keys::x, at.x).set(sg::keys::y, at.y).set(sg::keys::z, at.z);
            s.params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch).set(sg::keys::roll, roll);
        } else {
            // Sliding off, as fast as it was going under it.
            s.params.set(sg::keys::x, at.x).set(sg::keys::y, at.y + 0.002).set(sg::keys::z, at.z);
            s.params.set(sg::keys::vx, v.x).set(sg::keys::vy, v.y).set(sg::keys::vz, v.z);
            s.params.set("mode", mode::fall).set("spin", 0.0);
        }
    }
    for (sg::rigid::Body& b : world_.bodies) {
        if (!b.dynamic()) continue;
        sg::Element* e = find(sg::Key{b.id});
        if (!e || e->kind != kBody) continue;
        double yaw = 0, pitch = 0, roll = 0;
        sg::rigid::to_euler(b.r, yaw, pitch, roll);
        e->params.set(sg::keys::x, b.x.x).set(sg::keys::y, b.x.y).set(sg::keys::z, b.x.z);
        e->params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch).set(sg::keys::roll, roll);
        e->params.set(sg::keys::vx, b.v.x).set(sg::keys::vy, b.v.y).set(sg::keys::vz, b.v.z);
        e->params.set("wx", b.w.x).set("wy", b.w.y).set("wz", b.w.z).set("still", b.idle);
        if (b.hit > 0) {
            e->params.set("hit", std::max(e->params.num("hit"), b.hit));
            b.hit = 0;
        }
        if (e->params.get_or<std::string>("mode", mode::rest) != mode::held) e->params.set("mode", b.awake ? mode::fall : mode::rest);
    }
    cables(dt);
}

void Physics::ride_all() {
    for (auto& e : elements())
        if (e.kind == kBody) ride(e);
}

void Physics::ride(sg::Element& e) {
    const std::string m = e.params.get_or<std::string>("mode", mode::rest);
    if (m != mode::rest && m != mode::pinned) {
        if (!e.params.get_or<std::string>("on", "").empty()) e.params.set("on", std::string());
        return;
    }
    std::string on = e.params.get_or<std::string>("on", "");
    const auto note = [&](const sg::Element& s) {
        e.params.set("on", s.id.str())
            .set("on_x", s.params.num(sg::keys::x))
            .set("on_z", s.params.num(sg::keys::z))
            .set("on_yaw", s.params.num(sg::keys::yaw))
            .set("on_top", s.params.num("top"));
    };
    if (on.empty()) {
        const sg::Element* s = support_of(e, m == mode::pinned);
        if (s) note(*s);
        else e.params.set("on", std::string("-"));
        return;
    }
    if (on == "-") return;
    const sg::Element* s = find(sg::Key{on});
    if (!s || !s->alive) {
        e.params.set("on", std::string());
        return;
    }
    const double ox = e.params.num("on_x"), oz = e.params.num("on_z"), oyaw = e.params.num("on_yaw"), otop = e.params.num("on_top");
    const double nx = s->params.num(sg::keys::x), nz = s->params.num(sg::keys::z), nyaw = s->params.num(sg::keys::yaw),
                 ntop = s->params.num("top");
    if (nx == ox && nz == oz && nyaw == oyaw && ntop == otop) return;
    const double turn = nyaw - oyaw;
    const sg::Vec3d rel = sg::rotate_xz({e.params.num(sg::keys::x) - ox, 0, e.params.num(sg::keys::z) - oz}, turn);
    e.params.set(sg::keys::x, nx + rel.x).set(sg::keys::z, nz + rel.z).set(sg::keys::y, e.params.num(sg::keys::y) + ntop - otop);
    e.params.set(sg::keys::yaw, e.params.num(sg::keys::yaw) + turn);
    note(*s);
}

const sg::Element* Physics::support_of(const sg::Element& e, bool pinned) const {
    const double x = e.params.num(sg::keys::x), y = e.params.num(sg::keys::y), z = e.params.num(sg::keys::z);
    const bool solid = solid_shape(e.params.get_or<std::string>("shape", ""));
    const double bottom = solid ? y : y - extents(e).y;
    const sg::Element* best = nullptr;
    double score = 1e9;
    for (const auto& s : elements()) {
        if (s.kind != kSupport || !s.alive) continue;
        const sg::Vec3d local = sg::rotate_xz({x - s.params.num(sg::keys::x), 0, z - s.params.num(sg::keys::z)}, -s.params.num(sg::keys::yaw));
        const double hx = s.params.num(sg::keys::sx) * 0.5, hz = s.params.num(sg::keys::sz) * 0.5;
        const double base = s.params.num("base"), top = s.params.num("top");
        if (pinned) {
            const double dx = std::max(0.0, std::fabs(local.x) - hx), dz = std::max(0.0, std::fabs(local.z) - hz);
            const double dy = std::max({0.0, base - y, y - top});
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < 0.035 && d < score) score = d, best = &s;
        } else {
            if (std::fabs(local.x) > hx + 0.01 || std::fabs(local.z) > hz + 0.01) continue;
            // Its top just under the thing's bottom (a little under: a
            // thing may lie on something that lies on it).
            if (top > bottom + 0.02 || top < bottom - (solid ? 0.6 : 0.08)) continue;
            if (-top < score) score = -top, best = &s;
        }
    }
    return best;
}

sg::Element& Physics::add_cable(sg::Key id, sg::Key anchor, sg::Vec3d a, sg::Key body, sg::Vec3d b, double len, int n) {
    sg::Element& c = add_element(id, kCable);
    c.params.set("anchor", anchor.str()).set("body", body.str()).set("len", len).set("n", static_cast<double>(n));
    c.params.set("ax", a.x).set("ay", a.y).set("az", a.z).set("bx", b.x).set("by", b.y).set("bz", b.z).set("still", 0.0);
    for (int i = 0; i <= n; ++i)
        add_element(strand(id, i), sg::Key{"strand"});
    lay_cable(c, true);
    return c;
}

std::vector<sg::Key> Physics::cable_ids() const {
    std::vector<sg::Key> out;
    for (const auto& e : elements())
        if (e.kind == kCable) out.push_back(e.id);
    return out;
}

sg::rigid::V3 Physics::within_cords(const std::string& body, sg::rigid::V3 t) const {
    for (const auto& c : elements()) {
        if (c.kind != kCable || c.params.get_or<std::string>("body", "") != body) continue;
        sg::Vec3d a, b;
        if (!cable_ends(c, a, b)) continue;
        const sg::Vec3d w = sg::rope::Rope::within(a, {t.x, t.y, t.z}, c.params.num("len"));
        t = {w.x, w.y, w.z};
    }
    return t;
}

bool Physics::cable_ends(const sg::Element& c, sg::Vec3d& a, sg::Vec3d& b) const {
    const sg::Element* s = find(sg::Key{c.params.get_or<std::string>("anchor", "")});
    const sg::Element* d = find(sg::Key{c.params.get_or<std::string>("body", "")});
    if (!s || !d) return false;
    a = sg::Vec3d{s->params.num(sg::keys::x), s->params.num("base"), s->params.num(sg::keys::z)} +
        sg::rotate_xz({c.params.num("ax"), c.params.num("ay"), c.params.num("az")}, s->params.num(sg::keys::yaw));
    const sg::rigid::M3 r = sg::rigid::from_euler(d->params.num(sg::keys::yaw), d->params.num(sg::keys::pitch), d->params.num(sg::keys::roll));
    const sg::rigid::V3 at = sg::rigid::V3{d->params.num(sg::keys::x), d->params.num(sg::keys::y), d->params.num(sg::keys::z)} +
                         r * sg::rigid::V3{c.params.num("bx"), c.params.num("by"), c.params.num("bz")};
    b = {at.x, at.y, at.z};
    return true;
}

sg::rope::Ground Physics::cable_ground(const sg::Element& c, const sg::Vec3d& a, const sg::Vec3d& b) const {
    sg::rope::Ground g;
    g.floor = params().num("floor");
    const double len = c.params.num("len");
    const double lo_x = std::min(a.x, b.x) - len, hi_x = std::max(a.x, b.x) + len;
    const double lo_z = std::min(a.z, b.z) - len, hi_z = std::max(a.z, b.z) + len;
    for (const auto& s : elements()) {
        if (s.kind != kSupport || !s.alive) continue;
        const double r = std::hypot(s.params.num(sg::keys::sx), s.params.num(sg::keys::sz)) * 0.5;
        const double x = s.params.num(sg::keys::x), z = s.params.num(sg::keys::z);
        if (x + r < lo_x || x - r > hi_x || z + r < lo_z || z - r > hi_z || s.params.num("base") > std::max(a.y, b.y) + 0.05) continue;
        g.blocks.push_back({x, z, s.params.num(sg::keys::yaw), s.params.num(sg::keys::sx), s.params.num(sg::keys::sz),
                            s.params.num("base"), s.params.num("top")});
    }
    const std::string own = c.params.get_or<std::string>("body", "");
    for (const auto& e : elements()) {
        if (e.kind != kBody || !e.alive || e.id.str() == own || !solid_shape(e.params.get_or<std::string>("shape", "")))
            continue;
        const std::string m = e.params.get_or<std::string>("mode", "");
        if (m != mode::rest && m != mode::fall) continue;
        sg::Vec3d lo, hi;
        if (const sg::rigid::Body* rb = solid_of(e.id)) {
            lo = {rb->lo.x, rb->lo.y, rb->lo.z}, hi = {rb->hi.x, rb->hi.y, rb->hi.z};
        } else {
            // Not yet in the solver: its box, turned any way, standing on its middle.
            const double r = std::hypot(e.params.num(sg::keys::w), e.params.num(sg::keys::h)) * 0.5;
            const double x = e.params.num(sg::keys::x), y = e.params.num(sg::keys::y), z = e.params.num(sg::keys::z);
            lo = {x - r, y, z - r}, hi = {x + r, y + e.params.num("thick"), z + r};
        }
        if (hi.y - lo.y > 0.4) continue;  // big things are supports, or in the way already
        if (hi.x < lo_x || lo.x > hi_x || hi.z < lo_z || lo.z > hi_z) continue;
        g.lumps.push_back({lo, hi});
    }
    return g;
}

sg::rope::Rope Physics::rope_of(const sg::Element& c) const {
    sg::rope::Rope r;
    r.length = c.params.num("len");
    const int n = static_cast<int>(c.params.num("n"));
    for (int i = 0; i <= n; ++i) {
        const sg::Element& s = element(strand(c.id, i));
        r.p.push_back(sg::position_of(s));
        r.o.push_back({s.params.num("ox"), s.params.num("oy"), s.params.num("oz")});
    }
    return r;
}

void Physics::keep_rope(const sg::Element& c, const sg::rope::Rope& r) {
    bool changed = false;
    for (std::size_t i = 0; i < r.p.size(); ++i) {
        sg::Element& s = element(strand(c.id, static_cast<int>(i)));
        const auto before = s.params.stamp();
        sg::set_position(s, r.p[i]);
        s.params.set("ox", r.o[i].x).set("oy", r.o[i].y).set("oz", r.o[i].z);
        // A segment carries both its ends as its own data. Its presentation
        // transport never has to reach into the physics for another strand.
        if (i + 1 < r.p.size())
            s.params.set("end_x", r.p[i + 1].x).set("end_y", r.p[i + 1].y).set("end_z", r.p[i + 1].z);
        changed = changed || s.params.stamp() != before;
    }
    // Carry the final changed geometry too: the step that puts the cable to
    // sleep must not suppress its last update to the room.
    if (changed) emit(cords_event());
}

void Physics::lay_cable(sg::Element& c, bool) {
    sg::Vec3d a, b;
    if (!cable_ends(c, a, b)) return;
    keep_rope(c, sg::rope::Rope::laid(a, b, c.params.num("len"), static_cast<int>(c.params.num("n")), cable_ground(c, a, b)));
}

void Physics::cables(double dt) {
    for (auto& c : elements())
        if (c.kind == kCable) settle_cable(c, dt);
}

void Physics::settle_cable(sg::Element& c, double dt) {
    sg::Vec3d a, b;
    if (!cable_ends(c, a, b)) return;
    const double len = c.params.num("len");
    // The thing at the end, if it has got further than the cord reaches:
    // in the hand, the hand's pull is kept within reach (within_cords);
    // anything else - thrown, falling - is held back. Either way it stops
    // going away.
    {
        const sg::Vec3d d = b - a;
        const double far = sg::length(d);
        sg::Element* body = find(sg::Key{c.params.get_or<std::string>("body", "")});
        if (body && far > len * 0.97) {
            const sg::Vec3d out = d * (1.0 / far), back = d * ((far - len * 0.97) / far);
            sg::rigid::Body* rb = solid_of(body->id);
            if (body->params.get_or<std::string>("mode", "") != mode::held) {
                sg::set_position(*body, sg::position_of(*body) - back);
                if (rb) rb->x = rb->x - sg::rigid::V3{back.x, back.y, back.z}, rb->place();
                b = b - back;
            }
            if (rb) {
                const double away = rb->v.x * out.x + rb->v.y * out.y + rb->v.z * out.z;
                if (away > 0) rb->v = rb->v - sg::rigid::V3{out.x, out.y, out.z} * away;
            }
        }
    }
    // Asleep once neither end has moved for a moment.
    const bool had = c.params.has(sg::Key{"pax"});
    const sg::Vec3d a0 = had ? sg::Vec3d{c.params.num("pax"), c.params.num("pay"), c.params.num("paz")} : a;
    const sg::Vec3d b0 = had ? sg::Vec3d{c.params.num("pbx"), c.params.num("pby"), c.params.num("pbz")} : b;
    const bool ends_still = had && a0.x == a.x && a0.y == a.y && a0.z == a.z && b0.x == b.x && b0.y == b.y && b0.z == b.z;
    if (ends_still && c.params.num("still") > 0.75) return;
    c.params.set("pax", a.x).set("pay", a.y).set("paz", a.z).set("pbx", b.x).set("pby", b.y).set("pbz", b.z);
    sg::rope::Rope r = rope_of(c);
    const double moved = r.step(dt, a0, a, b0, b, cable_ground(c, a, b));
    keep_rope(c, r);
    c.params.set("still", ends_still || moved < 2e-5 ? c.params.num("still") + dt : 0.0);
}

void Physics::carry(const sg::rigid::Body& host, const Carry& c) {
    // Solid things lying on it, and on those in turn: at rest, they stay so.
    for (std::size_t i : world_.resting_on(host)) {
        sg::rigid::Body& rb = world_.bodies[i];
        sg::Element* e = find(sg::Key{rb.id});
        if (!e || e->params.get_or<std::string>("mode", "") != mode::rest) continue;
        c.body(rb.x, rb.r);
        rb.place();
        double yaw = 0, pitch = 0, roll = 0;
        sg::rigid::to_euler(rb.r, yaw, pitch, roll);
        e->params.set(sg::keys::x, rb.x.x).set(sg::keys::y, rb.x.y).set(sg::keys::z, rb.x.z);
        e->params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch).set(sg::keys::roll, roll);
    }
    // Sheets and prints are not in the solver: they lie on its top, or not.
    for (auto& e : elements()) {
        if (e.kind != kBody || !e.alive || solid_shape(text_of(e, kShape)) || text_of(e, sg::Key{"mode"}) != mode::rest) continue;
        const double y = e.params.num(sg::keys::y);
        const double top = sg::rigid::surface_at(host, e.params.num(sg::keys::x), e.params.num(sg::keys::z), y + 0.01);
        if (top > -0.5 && std::fabs(y - extents(e).y - top) <= 0.02) c.flat(e);
    }
}

bool Physics::nudged(const sg::rigid::Body& b, const sg::rigid::V3& x, const sg::rigid::M3& r) {
    return sg::rigid::length(b.x - x) < 0.3 && sg::rigid::length(sg::rigid::log_map(r * sg::rigid::transpose(b.r))) < 0.35;
}

bool Physics::statics() {
    static const sg::Key kWasX{"was_x"};
    bool drove = false;
    // (No shell, none laid: its floors and walls are its supports.)
    if (!shell_ && params().num("shell", 1.0) > 0.5) {
        shell_ = true;
        const double W = params().num("room_w"), D = params().num("room_d"), H = params().num("room_h");
        const double floor = params().num("floor"), skin = params().num("skin");
        const auto wall = [&](const char* id, sg::rigid::V3 c, sg::rigid::V3 half) {
            sg::rigid::Body b;
            b.id = id;
            b.hulls.push_back(sg::rigid::Hull::box({}, half));
            b.x = c;
            b.friction = 0.6;
            b.set_mass(0);
            world_.add(std::move(b));
        };
        wall("shell.floor", {W * 0.5, floor - 1.0, D * 0.5}, {W * 0.5 + 1, 1.0, D * 0.5 + 1});
        wall("shell.ceiling", {W * 0.5, H + 1.0, D * 0.5}, {W * 0.5 + 1, 1.0, D * 0.5 + 1});
        wall("shell.west", {skin - 1.0, H * 0.5, D * 0.5}, {1.0, H * 0.5 + 1, D * 0.5 + 1});
        wall("shell.east", {W - skin + 1.0, H * 0.5, D * 0.5}, {1.0, H * 0.5 + 1, D * 0.5 + 1});
        wall("shell.north", {W * 0.5, H * 0.5, skin - 1.0}, {W * 0.5 + 1, H * 0.5 + 1, 1.0});
        wall("shell.south", {W * 0.5, H * 0.5, D - skin + 1.0}, {W * 0.5 + 1, H * 0.5 + 1, 1.0});
    }
    if (support_structure_ != structure() || support_seen_.size() != elements().size()) {
        support_structure_ = structure();
        support_seen_.assign(elements().size(), 0);
    }
    for (std::size_t i = 0; i < elements().size(); ++i) {
        auto& s = elements()[i];
        if (s.kind != kSupport) continue;
        const std::string& id = s.id.str();
        sg::rigid::Body* b = solid_of(s.id);
        // (As it was when last read, and its body there: nothing to do.)
        // (One moved from outside, `was_x`, is always read: its move is to be driven.)
        if (b && s.alive && support_seen_[i] == s.params.stamp() && support_seen_[i] != 0 && !s.params.has(kWasX)) continue;
        support_seen_[i] = s.params.stamp();
        if (!s.alive) {
            if (b) world_.remove(id);
            support_at_.erase(id);
            continue;
        }
        const double base = s.params.num("base"), top = std::max(s.params.num("top"), base + 0.004);
        const std::array<double, 6> now{s.params.num(sg::keys::x),
                                        (base + top) * 0.5,
                                        s.params.num(sg::keys::z),
                                        s.params.num(sg::keys::yaw),
                                        s.params.num(sg::keys::sx) * 1000 + s.params.num(sg::keys::sz),
                                        top - base};
        auto it = support_at_.find(id);
        // Hauled since last step: from where it was (its element says),
        // driven where it is.
        if (b && s.params.has(sg::Key{"was_x"})) {
            const sg::rigid::V3 from{s.params.num("was_x"), (base + top) * 0.5, s.params.num("was_z")};
            const sg::rigid::M3 fr = sg::rigid::from_euler(s.params.num("was_yaw"), 0, 0);
            forget_was(s);
            const sg::rigid::V3 c{now[0], now[1], now[2]};
            const sg::rigid::M3 r = sg::rigid::from_euler(now[3], 0, 0);
            b->x = from, b->r = fr;
            b->place();
            if (nudged(*b, c, r)) {
                world_.drive(*b, c, r);
                drove = true;
            } else {
                world_.moved(*b, c, r);
            }
            support_at_[id] = now;
            continue;
        }
        if (b && it != support_at_.end() && it->second == now) continue;
        const bool resized = !b || it == support_at_.end() || it->second[4] != now[4] || it->second[5] != now[5];
        const sg::rigid::V3 c{now[0], now[1], now[2]};
        const sg::rigid::M3 r = sg::rigid::from_euler(now[3], 0, 0);
        if (resized) {
            if (b) world_.remove(id);
            sg::rigid::Body nb;
            nb.id = id;
            nb.hulls.push_back(
                sg::rigid::Hull::box({}, {s.params.num(sg::keys::sx) * 0.5, (top - base) * 0.5, s.params.num(sg::keys::sz) * 0.5}));
            nb.x = c;
            nb.r = r;
            nb.friction = 0.6;
            nb.set_mass(0);
            sg::rigid::Body& made = world_.add(std::move(nb));
            // (Made again at a new size, not made at the start: what
            // lies on it may have to move.)
            if (it != support_at_.end()) world_.wake_near(made);
        } else {
            world_.moved(*b, c, r);
        }
        support_at_[id] = now;
    }
    return drove;
}

bool Physics::names(const sg::Event& ev, const sg::Element& e) {
    return ev.args.get_or<std::string>("id", "") == e.id.str();
}

void Physics::aim(sg::Element& e, const sg::Vec3d& p, double yaw, double pitch, double roll) {
    e.params.set("tx", p.x).set("ty", p.y).set("tz", p.z);
    e.params.set("tyaw", yaw).set("tpitch", pitch).set("troll", roll);
}

bool Physics::ease(sg::Element& e, double rate, double dt) {
    const double k = 1.0 - std::exp(-rate * dt);
    double err = 0.0;
    for (auto [now, want] : {std::pair{sg::keys::x, "tx"}, {sg::keys::y, "ty"}, {sg::keys::z, "tz"}}) {
        const double d = e.params.num(sg::Key{want}) - e.params.num(now);
        e.params.set(now, e.params.num(now) + d * k);
        err = std::max(err, std::fabs(d));
    }
    for (auto [now, want] :
         {std::pair{sg::keys::yaw, "tyaw"}, {sg::keys::pitch, "tpitch"}, {sg::keys::roll, "troll"}}) {
        const double d = angle_to(e.params.num(now), e.params.num(sg::Key{want}));
        e.params.set(now, e.params.num(now) + d * k);
        err = std::max(err, std::fabs(d) * 0.1);
    }
    return err < 0.003;
}

void Physics::step(sg::Element& e, double dt) {
    if (dt <= 0.0) return;
    dt = std::min(dt, 0.05);
    ride(e);
    const std::string m = e.params.get_or<std::string>("mode", mode::rest);
    const double thick = e.params.num("thick");
    // A solid body is the rigid step's, unless it is being flown somewhere.
    if (solid_shape(text_of(e, kShape)) && m != mode::send) return;

    if (m == mode::held) {
        ease(e, 22.0, dt);
        collide(e);
        return;
    }
    if (m == mode::send) {
        if (ease(e, 9.0, dt)) {
            for (auto [now, want] : {std::pair{sg::keys::x, "tx"}, {sg::keys::y, "ty"}, {sg::keys::z, "tz"},
                                     {sg::keys::yaw, "tyaw"}, {sg::keys::pitch, "tpitch"},
                                     {sg::keys::roll, "troll"}})
                e.params.set(now, e.params.num(sg::Key{want}));
            e.params.set("mode", e.params.get_or<std::string>("then", mode::rest));
        }
        return;
    }
    if (m == mode::rest) {
        // Whatever it lay on may have been taken away from under it.
        const double y = e.params.num(sg::keys::y);
        const double top = top_under(e, y);
        // (Within the few millimetres the floor and walls are kept clear by.)
        const double give = e.params.get_or<std::string>("shape", "") == "box" ? 0.003 : 0.001;
        if (y - extents(e).y > top + give) {
            e.params.set("mode", mode::fall);
            e.params.set("spin", 0.0);
        }
        return;
    }
    if (m != mode::fall) return;
    if (e.params.get_or<std::string>("shape", "") == "box") {
        tumble(e, dt);
        return;
    }

    // Falling: gravity against drag, with a flutter that grows with drag -
    // a sheet side-slips on its way down, a print mostly just drops.
    const double drag = e.params.num("drag");
    double vx = e.params.num(sg::keys::vx), vy = e.params.num(sg::keys::vy), vz = e.params.num(sg::keys::vz);
    const double phase = e.params.num("phase") + dt * 5.5;
    vy -= params().num("g") * dt;
    const double damp = std::exp(-drag * dt);
    vx *= damp;
    vy *= damp;
    vz *= damp;
    vx += std::cos(phase) * drag * 0.9 * dt;
    vz += std::sin(phase * 0.73) * drag * 0.9 * dt;
    e.params.set("phase", phase);

    double x = e.params.num(sg::keys::x) + vx * dt;
    double y = e.params.num(sg::keys::y) + vy * dt;
    double z = e.params.num(sg::keys::z) + vz * dt;

    // It turns face up as it falls, and spins down.
    const double spin = e.params.num("spin") * std::exp(-1.2 * dt);
    e.params.set("spin", spin);
    e.params.set(sg::keys::pitch, e.params.num(sg::keys::pitch) +
                                      angle_to(e.params.num(sg::keys::pitch), kFlat) * (1 - std::exp(-4.0 * dt)));
    e.params.set(sg::keys::roll, e.params.num(sg::keys::roll) + spin * dt);

    // Out of anything solid it has moved into; what it is stopped by, it
    // stops moving into.
    const double before_y = e.params.num(sg::keys::y);
    e.params.set(sg::keys::x, x).set(sg::keys::y, y).set(sg::keys::z, z);
    e.params.set(sg::keys::vx, vx).set(sg::keys::vy, vy).set(sg::keys::vz, vz);
    collide(e);
    x = e.params.num(sg::keys::x);
    y = e.params.num(sg::keys::y);
    z = e.params.num(sg::keys::z);
    vx = e.params.num(sg::keys::vx);
    vy = e.params.num(sg::keys::vy);
    vz = e.params.num(sg::keys::vz);

    const double top = top_under(e, std::max(before_y, y) + 0.02);
    // Landed once its lowest point reaches what is under it (collision may
    // already have set it there, still slightly tilted).
    if (y - extents(e).y <= top + 0.001) {
        y = top + thick * 0.5 + 0.0005;
        vx = vy = vz = 0;
        e.params.set(sg::keys::pitch, kFlat);
        e.params.set("mode", mode::rest);
    }
    e.params.set(sg::keys::x, x).set(sg::keys::y, y).set(sg::keys::z, z);
    e.params.set(sg::keys::vx, vx).set(sg::keys::vy, vy).set(sg::keys::vz, vz);
}

void Physics::tumble(sg::Element& e, double dt) {
    struct V {
        double x, y, z;
    };
    const auto add = [](V a, V b) { return V{a.x + b.x, a.y + b.y, a.z + b.z}; };
    const auto mul = [](V a, double k) { return V{a.x * k, a.y * k, a.z * k}; };
    const auto dotv = [](V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    const auto cross = [](V a, V b) { return V{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
    using M = std::array<double, 9>;  // row-major
    const auto mv = [](const M& m, V v) {
        return V{m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
                 m[6] * v.x + m[7] * v.y + m[8] * v.z};
    };
    const auto mtv = [](const M& m, V v) {
        return V{m[0] * v.x + m[3] * v.y + m[6] * v.z, m[1] * v.x + m[4] * v.y + m[7] * v.z,
                 m[2] * v.x + m[5] * v.y + m[8] * v.z};
    };
    const auto mm = [](const M& a, const M& b) {
        M c{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) c[static_cast<std::size_t>(i * 3 + j)] += a[static_cast<std::size_t>(i * 3 + k)] * b[static_cast<std::size_t>(k * 3 + j)];
        return c;
    };
    // The renderer's turn, R = Ry(-yaw) Rz(pitch) Rx(roll), as a matrix;
    // and back again.
    const auto from_euler = [&](double yaw, double pitch, double roll) {
        const double cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch),
                     cr = std::cos(roll), sr = std::sin(roll);
        const M ry{cy, 0, -sy, 0, 1, 0, sy, 0, cy};
        const M rz{cp, -sp, 0, sp, cp, 0, 0, 0, 1};
        const M rx{1, 0, 0, 0, cr, -sr, 0, sr, cr};
        return mm(ry, mm(rz, rx));
    };
    const auto to_euler = [](const M& m, double& yaw, double& pitch, double& roll) {
        pitch = std::asin(std::clamp(m[3], -1.0, 1.0));
        if (std::fabs(std::cos(pitch)) > 1e-5) {
            roll = std::atan2(-m[5], m[4]);
            yaw = std::atan2(m[6], m[0]);
        } else {
            roll = 0.0;
            yaw = std::atan2(-m[2], m[8]);
        }
    };
    // Its box: half-lengths along its own x (w), y (thick) and z (h).
    const double hx = e.params.num(sg::keys::w) * 0.5, hy = e.params.num("thick") * 0.5,
                 hz = e.params.num(sg::keys::h) * 0.5;
    const double mass = std::max(0.05, e.params.num("mass", 0.6));
    const double ix = mass / 3.0 * (hy * hy + hz * hz), iy = mass / 3.0 * (hx * hx + hz * hz),
                 iz = mass / 3.0 * (hx * hx + hy * hy);

    V x{e.params.num(sg::keys::x), e.params.num(sg::keys::y), e.params.num(sg::keys::z)};
    V v{e.params.num(sg::keys::vx), e.params.num(sg::keys::vy), e.params.num(sg::keys::vz)};
    V w{e.params.num("wx"), e.params.num("wy"), e.params.num("wz")};
    M r = from_euler(e.params.num(sg::keys::yaw), e.params.num(sg::keys::pitch), e.params.num(sg::keys::roll));
    const double g = params().num("g"), skin = params().num("skin");
    const double room_w = params().num("room_w"), room_d = params().num("room_d");
    const bool boxed = params().num("shell", 1.0) > 0.5;
    double hit = e.params.num("hit");
    int touching = 0;

    const int steps = 10;
    const double h = dt / steps;
    for (int step = 0; step < steps; ++step) {
        v.y -= g * h;
        x = add(x, mul(v, h));
        // Turn by w*h (Rodrigues), which keeps the matrix a rotation.
        const double wl = std::sqrt(dotv(w, w));
        if (wl > 1e-9) {
            const double a = wl * h;
            const V k = mul(w, 1.0 / wl);
            const double c = std::cos(a), s1 = std::sin(a), t = 1 - c;
            const M q{t * k.x * k.x + c,        t * k.x * k.y - s1 * k.z, t * k.x * k.z + s1 * k.y,
                      t * k.x * k.y + s1 * k.z, t * k.y * k.y + c,        t * k.y * k.z - s1 * k.x,
                      t * k.x * k.z - s1 * k.y, t * k.y * k.z + s1 * k.x, t * k.z * k.z + c};
            r = mm(q, r);
        }
        // The inverse inertia in the world: R I^-1 R^T.
        const auto inv_i = [&](V a) {
            V b = mtv(r, a);
            b = V{b.x / ix, b.y / iy, b.z / iz};
            return mv(r, b);
        };
        touching = 0;
        // The contacts this substep: each corner in something, with its
        // normal and how deep. Solved by sequential impulses, each one's
        // total kept (normal never pulling, friction within mu of it) so
        // the solver can take back what it gave too much of.
        struct Contact {
            V arm, n, t1, t2;
            double depth, kn, kt1, kt2, pn = 0, pt1 = 0, pt2 = 0, bounce = 0;
        };
        std::array<Contact, 8> cs{};
        int nc = 0;
        for (int c = 0; c < 8; ++c) {
            const V local{(c & 1) ? hx : -hx, (c & 2) ? hy : -hy, (c & 4) ? hz : -hz};
            const V arm = mv(r, local);
            const V p = add(x, arm);
            V n{0, 0, 0};
            double depth = 0;
            const double top = top_at(p.x, p.z, p.y + 0.05, e.id);
            // Speculative: a corner within a few millimetres counts too,
            // allowed to close its gap but not to pass it - or a body
            // rocks for ever between its edges.
            const double margin = 0.003;
            depth = -margin;
            if (p.y < top + margin) n = V{0, 1, 0}, depth = top - p.y;
            if (boxed) {
                if (p.x < skin + margin && skin - p.x > depth) n = V{1, 0, 0}, depth = skin - p.x;
                if (p.x > room_w - skin - margin && p.x - (room_w - skin) > depth) n = V{-1, 0, 0}, depth = p.x - (room_w - skin);
                if (p.z < skin + margin && skin - p.z > depth) n = V{0, 0, 1}, depth = skin - p.z;
                if (p.z > room_d - skin - margin && p.z - (room_d - skin) > depth) n = V{0, 0, -1}, depth = p.z - (room_d - skin);
            }
            if (depth <= -margin || (n.x == 0 && n.y == 0 && n.z == 0)) continue;
            Contact& k = cs[static_cast<std::size_t>(nc++)];
            k.arm = arm, k.n = n, k.depth = depth;
            k.t1 = std::fabs(n.y) > 0.5 ? V{1, 0, 0} : V{0, 1, 0};
            k.t2 = cross(n, k.t1);
            const auto eff = [&](V d) { const V rd = cross(arm, d); return 1.0 / mass + dotv(d, cross(inv_i(rd), arm)); };
            k.kn = eff(n), k.kt1 = eff(k.t1), k.kt2 = eff(k.t2);
            const double vn = dotv(add(v, cross(w, arm)), n);
            if (depth > -0.0005 && vn < -0.4) hit = std::max(hit, -vn);  // a knock worth hearing
            // Restitution for a hard landing only; a gap may be closed in
            // this substep, no faster.
            k.bounce = depth < 0 ? depth / h : vn < -1.0 ? -0.25 * vn : 0.0;
            ++touching;
        }
        const auto push = [&](const Contact& k, V d, double j) {
            v = add(v, mul(d, j / mass));
            w = add(w, inv_i(cross(k.arm, mul(d, j))));
        };
        for (int iter = 0; iter < 8; ++iter)
            for (int ci = 0; ci < nc; ++ci) {
                Contact& k = cs[static_cast<std::size_t>(ci)];
                const V vc = add(v, cross(w, k.arm));
                const double jn = (k.bounce - dotv(vc, k.n)) / k.kn;
                const double pn = std::max(0.0, k.pn + jn);
                push(k, k.n, pn - k.pn);
                k.pn = pn;
                const double mu = 0.45 * k.pn;
                const V vc2 = add(v, cross(w, k.arm));
                const double p1 = std::clamp(k.pt1 - dotv(vc2, k.t1) / k.kt1, -mu, mu);
                push(k, k.t1, p1 - k.pt1);
                k.pt1 = p1;
                const V vc3 = add(v, cross(w, k.arm));
                const double p2 = std::clamp(k.pt2 - dotv(vc3, k.t2) / k.kt2, -mu, mu);
                push(k, k.t2, p2 - k.pt2);
                k.pt2 = p2;
            }
        // Pseudo-velocities for undoing what has sunk in: kept apart from
        // the real ones (split impulse), so correcting a corner turns the
        // body as well as lifting it, and adds no energy.
        V pv{0, 0, 0}, pw{0, 0, 0};
        for (int iter = 0; iter < 4; ++iter)
            for (int ci = 0; ci < nc; ++ci) {
                Contact& k = cs[static_cast<std::size_t>(ci)];
                if (k.depth <= 0) continue;
                const double want = 0.3 * std::max(0.0, k.depth - 0.0005) / h;
                const double have = dotv(add(pv, cross(pw, k.arm)), k.n);
                if (have >= want) continue;
                const double jp = (want - have) / k.kn;
                pv = add(pv, mul(k.n, jp / mass));
                pw = add(pw, inv_i(cross(k.arm, mul(k.n, jp))));
            }
        // The correction, moved through and then forgotten.
        x = add(x, mul(pv, h));
        const double pl = std::sqrt(dotv(pw, pw));
        if (pl > 1e-9) {
            const double a = pl * h;
            const V k = mul(pw, 1.0 / pl);
            const double c = std::cos(a), s1 = std::sin(a), t = 1 - c;
            const M q{t * k.x * k.x + c,        t * k.x * k.y - s1 * k.z, t * k.x * k.z + s1 * k.y,
                      t * k.x * k.y + s1 * k.z, t * k.y * k.y + c,        t * k.y * k.z - s1 * k.x,
                      t * k.x * k.z - s1 * k.y, t * k.y * k.z + s1 * k.x, t * k.z * k.z + c};
            r = mm(q, r);
        }
        // Air, and the small losses that bring anything to rest.
        v = mul(v, std::exp(-0.05 * h));
        w = mul(w, std::exp(-(touching ? 0.3 : 0.05) * h));
    }

    double yaw, pitch, roll;
    to_euler(r, yaw, pitch, roll);
    e.params.set(sg::keys::x, x.x).set(sg::keys::y, x.y).set(sg::keys::z, x.z);
    e.params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch).set(sg::keys::roll, roll);
    // The furniture's sides: out of them, as anything is.
    e.params.set(sg::keys::vx, v.x).set(sg::keys::vy, v.y).set(sg::keys::vz, v.z);
    collide(e, false);
    v = V{e.params.num(sg::keys::vx), e.params.num(sg::keys::vy), e.params.num(sg::keys::vz)};
    e.params.set("wx", w.x).set("wy", w.y).set("wz", w.z).set("hit", hit);

    // Asleep once it has lain still a moment on at least three corners.
    const double still = (touching >= 3 && std::sqrt(dotv(v, v)) < 0.03 && std::sqrt(dotv(w, w)) < 0.25)
                             ? e.params.num("still") + dt
                             : 0.0;
    e.params.set("still", still);
    if (still > 0.25) {
        e.params.set(sg::keys::vx, 0.0).set(sg::keys::vy, 0.0).set(sg::keys::vz, 0.0);
        e.params.set("wx", 0.0).set("wy", 0.0).set("wz", 0.0).set("still", 0.0);
        e.params.set("mode", mode::rest);
    }
}

}  // namespace sg::physics
