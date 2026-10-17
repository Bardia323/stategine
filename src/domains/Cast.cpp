#include "sg/domains/Cast.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "sg/core/Temporal.hpp"
#include "sg/domains/Being.hpp"
#include "sg/domains/Ragdoll.hpp"
#include "sg/domains/Walk.hpp"

namespace sg {

namespace {

// The clips a being has, as they are named.
std::vector<std::string> clips_of(const Being& b) {
    std::vector<std::string> out;
    for (const Element& e : b.elements())
        if (e.kind == Key{"clip"}) out.push_back(e.id.str().substr(5));
    return out;
}

// The walk read in beside a being's own clips (the same skeleton: no joint
// made twice), before it is fitted - read after, it would stand the joints
// back as the file has them, in its own units. The name of its clip, if any.
std::string read_walk(Being& b, const std::string& walk, std::string* why) {
    if (walk.empty() || (Being::files().stamp && Being::files().stamp(walk) == 0)) return {};
    const std::vector<std::string> before = clips_of(b);
    std::string err;
    if (!b.import_gltf(walk, "", &err)) {
        if (why) *why = "its walk " + walk + ": " + err;
        return {};
    }
    for (const std::string& c : clips_of(b))
        if (std::find(before.begin(), before.end(), c) == before.end()) return c;
    return {};
}

// Fitted to the engine's humanoid: as tall as said, facing x, on the floor.
void fit_to(Being& b, double height) {
    Being reference(Key{"reference"});
    humanoid(reference, height);
    fit(b, reference);
}

}  // namespace

Key spot_of(const Spatial3D& host, Key being) { return Key{host.id().str() + "." + being.str() + "_spot"}; }

Key cast(StateGraph& g, Spatial3D& host, Temporal& clock, Key name, const Casting& how, std::string* why) {
    auto& b = g.add<Being>(name);
    std::string err;
    if (!b.import_gltf(how.glb, "", &err)) {
        if (why) *why = err;
        return {};
    }
    // (Its first clip: what it does standing.)
    const std::vector<std::string> own = clips_of(b);
    const std::string stand = own.empty() ? std::string() : own.front();
    const std::string stride = read_walk(b, how.walk, why);
    fit_to(b, how.height);
    // Where it stands: an element of the place its body rides.
    Element& spot = host.portal(spot_of(host, b.id()), how.at, 0.6, how.height, how.yaw);
    spot.params.set("frame", 0.0).set(keys::open, false);
    const Key body = show(g, b, host.id(), spot.id);
    g.set_focus(g.embed(Key{name.str() + ".here"}, host.id(), spot.id, b.id(), Key{}, body).name, false);
    drive(g, clock, b.id(), b.live_event(), false, how.always ? Keeps::Always : Keeps::WhileActive);
    Params& self = b.element(Being::self_id()).params;
    if (!stand.empty()) self.set("stand_clip", stand);
    // Its feet set down on the ground (whatever its skeleton: by its shape),
    // so they stay where they are put as it goes on over them.
    if (how.plant)
        for (Key ankle : b.ankles()) b.plant(ankle.str());
    // How fast its walk goes as made: as said, else as its own feet say it.
    const double pace_of = how.stride > 0 || stride.empty() ? how.stride : b.ground_speed(stride);
    // (A walker stands on the floor, its lowest foot on it, whatever its clip
    // makes of its hips: its walk read beside its own clips leaves its
    // standing a little lower than it is - whoever shows it grounds it.)
    if (!stride.empty()) self.set("grounded", 1.0).set("stride", pace_of > 0.05 ? pace_of : 0.88);
    if (!stand.empty() && !stride.empty()) {
        // Standing and walking blended by pace (`go`: still, the walk at a
        // stride), steered from outside at its ports: how fast it goes.
        b.blend("go", stand + " 0\n" + stride + " " + std::to_string(self.num("stride")) + "\n", 0.0, 0.0, 6.0);
        b.hear(Event{b.play_event(), Params{}.set("clip", std::string("go")).set("fade", 0.0)});
        g.port(b.id(), b.steer_event());
        g.port(b.id(), b.play_event());
        g.port(b.id(), b.travel_event());
    } else if (!stand.empty()) {
        b.hear(Event{b.play_event(), Params{}.set("clip", stand).set("fade", 0.0)});
    }
    b.dispatch_pending();
    return b.id();
}

bool walks(const Being& b) { return b.find(Key{"blend.go"}) != nullptr; }

std::vector<Event> pace(const Being& b, double speed, double turn) { return pace(b, Vec3d{speed, 0, 0}, turn); }

std::vector<Event> pace(const Being& b, const Vec3d& velocity, double turn) {
    const Params& self = b.element(Being::self_id()).params;
    const double speed = std::hypot(velocity.x, velocity.z);
    // How it goes across its ground, told whenever it changes (its feet set
    // down stay put as it goes).
    std::vector<Event> out;
    if (std::fabs(self.num("go_x") - velocity.x) > 1e-4 || std::fabs(self.num("go_z") - velocity.z) > 1e-4 || std::fabs(self.num("go_turn") - turn) > 1e-4)
        out.push_back(Event{b.travel_event(), Params{}.set(keys::x, velocity.x).set(keys::z, velocity.z).set("turn", turn)});
    const std::string stand = self.get_or<std::string>("stand_clip", "");
    // Standing now: its own standing clip the one it plays.
    bool standing = false;
    for (const Element& e : b.elements())
        if (e.kind == Key{"layer"} && !stand.empty() && e.params.get_or<std::string>("clip", "") == stand && e.params.num("to") > 0.5) standing = true;
    // (Standing and walking change into each other inertialized: at once,
    // the difference let go of - not the two averaged as they cross.)
    if (speed < 0.05 && !stand.empty()) {
        if (standing) return out;
        out.push_back(Event{b.play_event(), Params{}.set("clip", stand).set("fade", 0.25).set("inert", 0.12).set("speed", 1.0)});
        return out;
    }
    // Slower than its stride: the walk eased into standing (shorter steps, as
    // fast); faster: the walk itself quicker - so its feet go as fast as it does.
    const double stride = self.num("stride", 0.88), quick = std::max(1.0, speed / stride);
    out.push_back(Event{b.steer_event(), Params{}.set("blend", std::string("go")).set("x", speed / quick).set("y", 0.0)});
    out.push_back(Event{b.play_event(), Params{}.set("clip", std::string("go")).set("fade", 0.0).set("inert", 0.12).set("speed", quick)});
    return out;
}

std::vector<Event> pace(const Spatial3D& host, const Being& b, const Vec3d& velocity, double turn) {
    // (A yaw takes x toward z: turned back by its spot's.)
    const Element* spot = host.find(spot_of(host, b.id()));
    const double yaw = spot ? spot->params.num(keys::yaw) : 0.0, c = std::cos(yaw), s = std::sin(yaw);
    std::vector<Event> out = pace(b, Vec3d{velocity.x * c + velocity.z * s, 0.0, -velocity.x * s + velocity.z * c}, turn);
    if (!spot) return out;
    // The ground under each foot, from the ground its spot stands on: what
    // stops a walker, met by a ray straight down where the foot is to be
    // (no further up or down than half its leg; nothing met, its spot's own).
    const Vec3d at = position_of(*spot);
    std::vector<std::pair<Key, double>> floors;
    bool changed = false;
    for (const Element& g : b.elements()) {
        if (g.kind != Key{"goal"} || g.params.num("plants") < 0.5) continue;
        const double leg = g.params.num("leg", 0.9), x = g.params.num(keys::x), z = g.params.num(keys::z);
        const Vec3d from{at.x + x * c - z * s, at.y + 0.5 * leg, at.z + x * s + z * c};
        Vec3d hit, normal;
        const double floor = ray(host, from, {0, -1, 0}, leg, hit, normal) ? hit.y - at.y : 0.0;
        floors.emplace_back(Key{"floor_" + g.params.get_or<std::string>("tip", "")}, floor);
        if (std::fabs(floor - g.params.num("floor_to")) > 1e-3) changed = true;
    }
    if (!changed) return out;
    for (Event& e : out)
        if (e.name == b.travel_event()) {
            for (const auto& [k, v] : floors) e.args.set(k, v);
            return out;
        }
    Params travel = Params{}.set(keys::x, b.element(Being::self_id()).params.num("go_x")).set(keys::z, b.element(Being::self_id()).params.num("go_z"));
    travel.set("turn", b.element(Being::self_id()).params.num("go_turn"));
    for (const auto& [k, v] : floors) travel.set(k, v);
    out.insert(out.begin(), Event{b.travel_event(), travel});
    return out;
}

Key lead(StateGraph& g, Spatial3D& host, Temporal& clock, Being& led, const std::string& glb, double height, double speed, std::string* why) {
    // What leads it: the motion alone, a being of its own - fitted as tall
    // (its travel carried as far for the led's size as for its own),
    // standing where the led does, never shown.
    auto& from = g.add<Being>(Key{led.id().str() + "_lead"});
    std::string err;
    if (!from.import_gltf(glb, "", &err)) {
        if (why) *why = glb + ": " + err;
        return {};
    }
    const std::vector<std::string> clips = clips_of(from);
    fit_to(from, height);
    const Element* at = host.find(spot_of(host, led.id()));
    Element& spot = host.portal(spot_of(host, from.id()), at ? position_of(*at) : Vec3d{}, 0.1, 0.1, at ? at->params.num(keys::yaw) : 0.0);
    spot.params.set("frame", 0.0).set(keys::open, false);
    g.set_focus(g.embed(Key{from.id().str() + ".here"}, host.id(), spot.id, from.id(), Key{}, Key{}).name, false);
    // (Always: a skeleton and nothing more, cheap - and whoever it leads is
    // seen through a doorway before its room is entered, standing as it moves.)
    drive(g, clock, from.id(), from.live_event(), false, Keeps::Always);
    if (!clips.empty()) from.hear(Event{from.play_event(), Params{}.set("clip", clips.front()).set("fade", 0.0).set("speed", speed)});
    from.dispatch_pending();
    // Its motion the led's own: by the joints they share, as a clip of its
    // own would move it (followed, not leading it - so a ragdoll's knocks are
    // on top of it, as on anyone's).
    if (same_joints(from, led).empty()) {
        if (why) *why = "it shares no joints with " + glb;
        return {};
    }
    retarget(g, from, led, {}, true);
    led.hear(Event{led.follow_event(), Params{}.set("weight", 1.0).set("fade", 0.0)});
    led.dispatch_pending();
    return from.id();
}

Vec3d in_being(const Spatial3D& host, Key being, const Vec3d& p) {
    // (As its ragdoll sees the place: from where it stands, turned as it is.)
    const Element& o = host.element(spot_of(host, being));
    const double c = std::cos(o.params.num(keys::yaw)), s = std::sin(o.params.num(keys::yaw));
    const Vec3d d{p.x - o.params.num(keys::x), p.y - o.params.num(keys::y), p.z - o.params.num(keys::z)};
    return {c * d.x + s * d.z, d.y, -s * d.x + c * d.z};
}

bool touch(const StateGraph& g, const Spatial3D& host, Key being, const Vec3d& eye, const Vec3d& dir, double reach, Touch& out) {
    const auto* rag = dynamic_cast<const Ragdoll*>(g.find(Key{being.str() + ".rag"}));
    const Element* o = host.find(spot_of(host, being));
    if (!rag || !o) return false;
    // In its own frame: the eye and its way, against each bone's solid.
    const Vec3d e = in_being(host, being, eye);
    const double c = std::cos(o->params.num(keys::yaw)), s = std::sin(o->params.num(keys::yaw));
    const Vec3d d{c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z};
    double best = reach;
    for (Key bone : rag->bones()) {
        const Element& be = rag->element(bone);
        if (be.params.num("rides") > 0.5) continue;
        rigid::Body body = Ragdoll::body(be, bone.str(), 1);
        Ragdoll::pose_in(be, {0, 0, 0}, 0.0, body.x, body.r);
        body.place();
        for (const rigid::Body::Placed& pl : body.world) {
            const double t = rigid::ray_hull(pl, {e.x, e.y, e.z}, {d.x, d.y, d.z}, best);
            if (t > 0 && t < best) best = t, out.bone = bone.str();
        }
    }
    if (out.bone.empty()) return false;
    out.rag = rag->id();
    out.t = best;
    out.at = e + d * best;
    out.dir = d;
    return true;
}

}  // namespace sg
