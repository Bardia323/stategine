#include "sg/domains/Cast.hpp"
#include "sg/domains/Mover.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "sg/core/Temporal.hpp"
#include "sg/domains/Being.hpp"
#include "sg/domains/Ragdoll.hpp"
#include "sg/domains/Walk.hpp"
#include "sg/spatial/Math.hpp"

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
        g.remove(g.removal(name));  // (nothing of it left behind)
        return {};
    }
    // (Its first clip: what it does standing.)
    const std::vector<std::string> own = clips_of(b);
    const std::string stand = own.empty() ? std::string() : own.front();
    const std::string stride = how.moved ? std::string() : read_walk(b, how.walk, why);
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
    if (how.plant && !how.moved)
        for (Key ankle : b.ankles()) b.plant(ankle.str());
    // Moved by MotionBricks: its mover on the same clock, its body and spot carried from it.
    if (how.moved) {
        auto& mv = g.add<Mover>(mover_of(b.id()), how.at.x, how.at.z, how.yaw);
        drive(g, clock, mv.id(), mv.live_event(), false, how.always ? Keeps::Always : Keeps::WhileActive);
        g.port(mv.id(), mv.go_event());
        g.port(mv.id(), mv.stand_event());
        move_body(g, mv, b);
        move_spot(g, mv, host, spot.id);
        // (Held in its spot beside its body: reached from the place, as the body is.)
        host.element(spot.id).params.set(shared_key(), 1.0);
        g.set_focus(g.embed(Key{name.str() + ".moves"}, host.id(), spot.id, mv.id(), Key{}, Key{}).name, false);
    }
    // How fast its walk goes as made: as said, else as its own feet say it.
    // (How fast: as told; else as its walk says it was made - captured motion
    // knows; else as its feet seem to go, a guess: on MHR's five-jointed feet
    // it came out a quarter of the truth, and the feet slid.)
    const Element* walk_clip = stride.empty() ? nullptr : b.find(Key{"clip." + stride});
    const double made = walk_clip ? walk_clip->params.num("pace", 0.0) : 0.0;
    const double pace_of = how.stride > 0 || stride.empty() ? how.stride : made > 0 ? made : b.ground_speed(stride);
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
    // Going back: its walk played backwards - its feet going the way the
    // ground does under them.
    const double stride = self.num("stride", 0.88), quick = std::max(1.0, speed / stride), way = velocity.x < 0 ? -1.0 : 1.0;
    out.push_back(Event{b.steer_event(), Params{}.set("blend", std::string("go")).set("x", speed / quick).set("y", 0.0)});
    out.push_back(Event{b.play_event(), Params{}.set("clip", std::string("go")).set("fade", 0.0).set("inert", 0.12).set("speed", way * quick)});
    return out;
}

std::vector<Event> pace(const Spatial3D& host, const Being& b, const Vec3d& velocity, double turn) {
    // (A yaw takes x toward z: turned back by its spot's.)
    const Element* spot = host.find(spot_of(host, b.id()));
    const double yaw = spot ? spot->params.num(keys::yaw) : 0.0, c = std::cos(yaw), s = std::sin(yaw);
    std::vector<Event> out = pace(b, Vec3d{velocity.x * c + velocity.z * s, 0.0, -velocity.x * s + velocity.z * c}, turn);
    if (!spot) return out;
    // The ground under each foot, from the ground its spot stands on: what
    // stops a walker, met by a ray straight down where the foot is to be (no
    // further up or down than half its leg; nothing met, its spot's own) -
    // how high it is, the way it faces (in the being's own frame) and what it
    // is (its `ground`, else its name). A foot set down where it was last
    // asked is not asked again; a foot lifted is asked where it will come
    // down (where its travel takes it a fifth of a second on), so it is
    // lifted onto a step before it gets there, not into it. And round the
    // being, how the ground slopes: ahead and to its side, from four rays a
    // hand and a half off its middle.
    const Vec3d at = position_of(*spot);
    const Params& self = b.element(Being::self_id()).params;
    const double go_x = self.num("go_x"), go_z = self.num("go_z");
    Params told;
    bool changed = false;
    // Every ray it asks, at once (sg::rays): what stops them gathered once.
    std::vector<RayAsk> asks;
    const auto down_at = [&](double x, double z, double reach) {
        asks.push_back({{at.x + x * c - z * s, at.y + 0.5 * reach, at.z + x * s + z * c}, {0, -1, 0}, reach});
    };
    struct Foot {
        const Element* goal;
        std::string tip;
        double x, z;
        bool down;
    };
    std::vector<Foot> feet;
    for (const Element& g : b.elements()) {
        if (g.kind != Key{"goal"} || g.params.num("plants") < 0.5) continue;
        const std::string tip = g.params.get_or<std::string>("tip", "");
        const bool down = g.params.num("locked") > 0.5;
        double x = g.params.num(keys::x), z = g.params.num(keys::z);
        if (down && g.params.num("asked") > 0.5 && std::hypot(x - g.params.num("asked_x"), z - g.params.num("asked_z")) < 0.03) continue;
        if (!down) x += go_x * 0.2, z += go_z * 0.2;
        feet.push_back({&g, tip, x, z, down});
        down_at(x, z, g.params.num("leg", 0.9));
    }
    const double off[4][2] = {{0.3, 0}, {-0.3, 0}, {0, 0.3}, {0, -0.3}};
    for (const auto& o : off) down_at(o[0], o[1], 1.0);
    const std::vector<RayHit> met = rays(host, asks);
    for (std::size_t f = 0; f < feet.size(); ++f) {
        const Foot& foot = feet[f];
        const Element& g = *foot.goal;
        const RayHit& r = met[f];
        const Vec3d normal = r.hit ? r.normal : Vec3d{0, 1, 0};
        const double floor = r.hit ? r.at.y - at.y : 0.0;
        // (The way it faces, turned into the being's frame as its travel is.)
        const double nx = normal.x * c + normal.z * s, nz = -normal.x * s + normal.z * c;
        std::string on;
        if (r.hit && !r.what.empty())
            if (const Element* e = host.find(r.what)) on = e->params.get_or<std::string>("ground", r.what.str());
        const std::string& tip = foot.tip;
        told.set(Key{"floor_" + tip}, floor).set(Key{"tilt_" + tip + "_x"}, nx).set(Key{"tilt_" + tip + "_z"}, nz).set(Key{"on_" + tip}, on);
        told.set(Key{"asked_" + tip + "_x"}, foot.x).set(Key{"asked_" + tip + "_z"}, foot.z);
        if (std::fabs(floor - g.params.num("floor_to")) > 1e-3 || std::fabs(nx - g.params.num("tilt_to_x")) > 1e-3 ||
            std::fabs(nz - g.params.num("tilt_to_z")) > 1e-3 || !foot.down)
            changed = true;
    }
    {
        bool all = true;
        double h[4];
        for (int k = 0; k < 4; ++k) {
            const RayHit& r = met[feet.size() + static_cast<std::size_t>(k)];
            all = all && r.hit;
            h[k] = r.at.y;
        }
        const double ahead = all ? std::atan2(h[0] - h[1], 0.6) : 0.0, side = all ? std::atan2(h[2] - h[3], 0.6) : 0.0;
        told.set("slope_ahead", ahead).set("slope_side", side);
        if (std::fabs(ahead - self.num("slope_ahead_to")) > 1e-3 || std::fabs(side - self.num("slope_side_to")) > 1e-3) changed = true;
    }
    if (!changed) return out;
    for (Event& e : out)
        if (e.name == b.travel_event()) {
            for (const auto& kv : told) e.args.set(kv.first, kv.second);
            return out;
        }
    Params travel = Params{}.set(keys::x, go_x).set(keys::z, go_z).set("turn", self.num("go_turn"));
    for (const auto& kv : told) travel.set(kv.first, kv.second);
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

namespace {

// A joint of `b` by its own name (a model's joints are named with its prefix).
const Element* joint_named(const Being& b, const std::string& name) {
    for (Key j : b.joints()) {
        const std::string& k = j.str();
        const std::size_t cut = k.find_last_of(":|");
        if ((cut == std::string::npos ? k : k.substr(cut + 1)) == name) return &b.element(j);
    }
    return nullptr;
}

}  // namespace

std::vector<Event> brace(const Spatial3D& host, const Being& b, const std::string& goal, const std::string& shoulder, const std::string& wrist,
                         const Vec3d& toward, double palm, double* dist) {
    const Element* spot = host.find(spot_of(host, b.id()));
    const Element* s = joint_named(b, shoulder);
    const Element* w = joint_named(b, wrist);
    if (!spot || !s || !w) return {};
    const Element* e = b.find(Key{w->params.get_or<std::string>("parent", "")});
    const Pose there = world_pose(host, *spot);
    const Vec3d S = place_in(there, b.pose_of(s->id).position), W = place_in(there, b.pose_of(w->id).position);
    const Vec3d E = e ? place_in(there, b.pose_of(e->id).position) : (S + W) * 0.5;
    const double arm = length(E - S) + length(W - E);
    if (arm <= 1e-6) return {};
    // (Where to look for a surface: the way said, or a fan from the side the
    // arm is on round to the front, each a little down.)
    std::vector<Vec3d> ways;
    if (length(toward) > 1e-9) ways.push_back(unit(toward));
    else {
        const Vec3d ahead = unit(turn(there, Vec3d{1, 0, 0}));
        const Vec3d side = unit(S - place_in(there, Vec3d{0, S.y - there.position.y, 0}));
        const Vec3d out = unit(Vec3d{side.x, 0, side.z});
        for (int k = 0; k <= 6; ++k) {
            const double a = k / 6.0;
            ways.push_back(unit(unit(out * (1 - a) + ahead * a) + Vec3d{0, -0.3, 0}));
        }
    }
    // (Of what the rays meet, where the arm rests easiest: some seven tenths
    // of its reach, not bunched up against the shoulder nor at full stretch.)
    double best = 1e30, near = 1e30;
    Vec3d at, normal;
    for (const Vec3d& d : ways) {
        Vec3d hit, n;
        double t = 0;
        if (!ray(host, S, d, arm * 0.92, hit, n, &t)) continue;
        near = std::min(near, t);
        if (const double ease = std::abs(t - arm * 0.7); ease < best) best = ease, at = hit, normal = n;
    }
    if (best > 1e29) return {};
    if (dist) *dist = near;
    // (The wrist off the surface by the hand's thickness; the elbow down, and
    // out from the surface, as an arm leant on a wall bends.)
    const Vec3d to = at + normal * palm;
    const Vec3d pole = (S + to) * 0.5 + Vec3d{0, -0.45, 0} + normal * 0.2;
    const Vec3d t = local_of(there, to), p = local_of(there, pole);
    std::vector<Event> out{Event{b.reach_event(), Params{}.set("goal", goal).set(keys::x, t.x).set(keys::y, t.y).set(keys::z, t.z)
                                                      .set("pole_x", p.x).set("pole_y", p.y).set("pole_z", p.z).set("fade", 0.35).set("soften", 0.96)}};
    // (Flat on it: its fingers up the surface, leaning the way the body
    // faces, its back out.)
    if (b.find(Key{"goal." + goal + "_palm"})) {
        const Vec3d ahead = turn(there, Vec3d{1, 0, 0});
        Vec3d along = Vec3d{0, 1, 0} + ahead * 0.5;
        along = unit(along - normal * dot(along, normal));
        const Vec3d f = local_of(there, to + along * 0.15), u = local_of(there, to + normal);
        out.push_back(Event{b.look_event(), Params{}.set("goal", goal + "_palm").set(keys::x, f.x).set(keys::y, f.y).set(keys::z, f.z)
                                                .set("pole_x", u.x).set("pole_y", u.y).set("pole_z", u.z).set("fade", 0.35)});
    }
    return out;
}

Element& palm_goal(Being& b, const std::string& name, const std::string& wrist, const std::string& middle, const std::string& index,
                   const std::string& little, const std::string& thumb) {
    const Element* w = joint_named(b, wrist);
    Element& g = b.goal(name, w ? w->id.str() : wrist, 0);
    const Element *m = joint_named(b, middle), *i = joint_named(b, index), *l = joint_named(b, little), *t = joint_named(b, thumb);
    if (!w || !m || !i || !l || !t) return g;
    const Vec3d W = b.pose_of(w->id).position;
    const Vec3d fingers = unit(b.pose_of(m->id).position - W), across = b.pose_of(i->id).position - b.pose_of(l->id).position;
    Vec3d back = unit(cross(across, fingers));
    if (dot(back, b.pose_of(t->id).position - W) > 0) back = back * -1.0;
    // (Now, as it stands, back to as it was bound: by the turn it has from its binding.)
    using namespace spatial;
    const M3 now = from_quat(w->params.num("pqw", 1.0), w->params.num("pqx"), w->params.num("pqy"), w->params.num("pqz"));
    const M3 bound = from_quat(w->params.num("bqw", 1.0), w->params.num("bqx"), w->params.num("bqy"), w->params.num("bqz"));
    const M3 unturn = bound * transpose(now);
    const V3 f = unturn * V3{fingers.x, fingers.y, fingers.z}, u = unturn * V3{back.x, back.y, back.z};
    g.params.set("fwd_x", f.x).set("fwd_y", f.y).set("fwd_z", f.z).set("up_x", u.x).set("up_y", u.y).set("up_z", u.z);
    return g;
}



std::optional<Event> lean(const Spatial3D& host, const Being& b, const std::string& goal, const std::string& neck, const std::string& head, const Vec3d& look,
                          double radius, double above, double near) {
    const Element* spot = host.find(spot_of(host, b.id()));
    const Element* n = joint_named(b, neck);
    const Element* h = joint_named(b, head);
    if (!spot || !n || !h) return std::nullopt;
    const Pose there = world_pose(host, *spot);
    const Vec3d N = place_in(there, b.pose_of(n->id).position), H = place_in(there, b.pose_of(h->id).position);
    // (The skull as it would stand upright on the neck: what the wall is
    // measured from, so a head already leant does not lean on.)
    const double reach = length(H - N) + above;
    const Vec3d C = N + Vec3d{0, reach, 0};
    double d = 1e30;
    Vec3d wall;
    for (int k = 0; k < 16; ++k) {
        const double a = k * 3.14159265358979 / 8.0;
        Vec3d hit, nn;
        double t = 0;
        if (ray(host, C, Vec3d{std::cos(a), 0, std::sin(a)}, near, hit, nn, &t) && t < d) d = t, wall = unit(Vec3d{nn.x, 0, nn.z});
    }
    Vec3d up{0, 1, 0};
    if (d < 1e29) {
        // (Tipped about the neck as far as brings the skull's side to the
        // wall: sin of it the gap over the neck's reach - a third of a right
        // angle at most.)
        const double tip = std::asin(std::clamp((d - radius) / std::max(reach, 1e-6), -0.5, 0.5));
        up = unit(Vec3d{0, std::cos(tip), 0} - wall * std::sin(tip));
    }
    const Vec3d l = local_of(there, look), p = local_of(there, C + up);
    return Event{b.look_event(), Params{}.set("goal", goal).set(keys::x, l.x).set(keys::y, l.y).set(keys::z, l.z)
                                     .set("pole_x", p.x).set("pole_y", p.y).set("pole_z", p.z).set("fade", 0.35)};
}

}  // namespace sg
