// A being: a skeleton, a body on it, and a spirit that gets it where it means
// to be - clips blended, a joint held, a hand reaching by inverse kinematics -
// each joint at its own stiffness, on its own clock.
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/core/Text.hpp"
#include "sg/domains/Being.hpp"

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

namespace {
double dist(const sg::Vec3d& a, const sg::Vec3d& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

sg::Being& person(sg::StateGraph& g, const char* id, double scale = 1.0) {
    auto& b = g.add<sg::Being>(sg::Key{id}, scale);
    sg::humanoid(b, 1.75);
    return b;
}
}  // namespace

int main() {
    {
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& ann = person(g, "ann");
        sg::drive(g, clock, "ann", ann.live_event());
        g.set_initial("ann");
        const sg::LawReport r = sg::verify(g);
        for (const auto& v : r.violations) std::printf("     %s: %s\n", v.where.c_str(), v.detail.c_str());
        check(r.structure.empty() && r.ok(), "a person: a being of joints and parts, driven by a clock, keeps the laws");
        check(ann.joints().size() == 17 && ann.parts().size() == 17, "a skeleton of 17 joints, a body of 17 parts");
        const double head0 = ann.pose_of("skull").position.y;
        check(head0 > 1.5 && head0 < 1.8, "standing: the head about 1.6 m up (" + std::to_string(head0) + ")");

        sg::Engine e(g);
        e.set_strict(true);
        e.start();
        // A clip played: the legs go, one forward as the other goes back.
        e.fire(sg::Event{ann.play_event(), sg::Params{}.set("clip", std::string("walk")).set("fade", 0.2)});
        double most = 0;
        for (int i = 0; i < 60; ++i) {
            e.tick(1.0 / 60);
            const double l = ann.pose_of("foot_l").position.x, rt = ann.pose_of("foot_r").position.x;
            most = std::max(most, std::fabs(l - rt));
        }
        check(most > 0.25, "walking: the feet pass each other (" + std::to_string(most) + " m apart at most)");
        // Faded across to another: its weight rises, the other's falls, smoothly.
        e.fire(sg::Event{ann.play_event(), sg::Params{}.set("clip", std::string("idle")).set("fade", 0.5)});
        double last_w = -1;
        bool rising = true;
        for (int i = 0; i < 30; ++i) {
            e.tick(1.0 / 60);
            double w = 0;
            for (int k = 0; k < sg::Being::kLayers; ++k) {
                const auto& l = ann.element(sg::Key{"layer" + std::to_string(k)});
                if (l.params.get_or<std::string>("clip", "") == "idle") w = l.params.num("weight");
            }
            rising = rising && w >= last_w;
            last_w = w;
        }
        check(rising && last_w > 0.4 && last_w < 1.0, "blended: idle fades in over its half second (" + std::to_string(last_w) + ")");
        for (int i = 0; i < 120; ++i) e.tick(1.0 / 60);

        // A hand reaching for a point: inverse kinematics, and the arm goes there.
        const sg::Vec3d aim{0.35, 1.45, -0.32};
        e.fire(sg::Event{ann.reach_event(), sg::Params{}.set("goal", std::string("hand_r")).set(sg::keys::x, aim.x).set(sg::keys::y, aim.y).set(sg::keys::z, aim.z)});
        const double before = dist(ann.pose_of("hand_r").position, aim);
        for (int i = 0; i < 120; ++i) e.tick(1.0 / 60);
        const double after = dist(ann.pose_of("hand_r").position, aim);
        check(before > 0.3 && after < 0.03, "reaching: the right hand goes to the point (" + std::to_string(before) + " m off, then " + std::to_string(after) + ")");
        e.fire(sg::Event{ann.release_event(), sg::Params{}.set("goal", std::string("hand_r"))});
        for (int i = 0; i < 90; ++i) e.tick(1.0 / 60);
        check(dist(ann.pose_of("hand_r").position, aim) > 0.2, "let go, it falls back to what it was doing");

        // A joint held where it is told: the head turned, and back when freed.
        e.fire(sg::Event{ann.turn_event(), sg::Params{}.set("joint", std::string("head")).set("yaw", 50.0)});
        for (int i = 0; i < 90; ++i) e.tick(1.0 / 60);
        const double yaw = ann.pose_of("head").yaw;
        check(yaw > 0.7 && yaw < 1.0, "a joint held: the head turned 50 degrees (" + std::to_string(yaw * 180 / 3.14159265) + ")");
        // No time, no change.
        const std::string snap = sg::to_text(ann);
        e.tick(0.0);
        check(sg::to_text(ann) == snap, "no time passing, nothing moves");
    }
    {
        // Its own clock: a creature half the size lives twice as fast.
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& big = person(g, "big", 1.0);
        auto& small = person(g, "small", 0.5);
        sg::drive(g, clock, "big", big.live_event(), false, sg::Keeps::Always);
        sg::drive(g, clock, "small", small.live_event(), false, sg::Keeps::Always);
        g.set_initial("big");
        g.connect("big", "swap", "small");
        sg::Engine e(g);
        e.start();
        for (int i = 0; i < 60; ++i) e.tick(1.0 / 60);
        const double ab = big.element(sg::Being::self_id()).params.num("age"), as = small.element(sg::Being::self_id()).params.num("age");
        check(std::fabs(ab - 1.0) < 1e-6 && std::fabs(as - 2.0) < 1e-6, "its own clock: in a second, the big one lives 1 s, the half-sized one 2 s");
    }
    {
        // Motion read from BVH: its joints, and a clip that moves them.
        const std::string bvh =
            "HIERARCHY\nROOT root\n{\n OFFSET 0 90 0\n CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n"
            " JOINT knee\n {\n  OFFSET 0 -45 0\n  CHANNELS 3 Zrotation Xrotation Yrotation\n  End Site\n  {\n   OFFSET 0 -45 0\n  }\n }\n}\n"
            "MOTION\nFrames: 3\nFrame Time: 0.5\n0 90 0 0 0 0 0 0 0\n0 90 0 0 0 0 30 0 0\n0 90 0 0 0 0 0 0 0\n";
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"bvh"});
        std::string why;
        const bool ok = b.import_bvh(bvh, "bend", &why);
        b.part("leg", "knee", "cylinder", {0.1, 0.45, 0.1}, {0, -0.225, 0});
        sg::drive(g, clock, "bvh", b.live_event());
        g.set_initial("bvh");
        check(ok && b.joints().size() == 2 && std::fabs(b.element(sg::Key{"knee"}).params.num(sg::keys::y) + 0.45) < 1e-9,
              "BVH read: its joints, in metres " + why);
        sg::Engine e(g);
        e.start();
        e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("bend")).set("fade", 0.0)});
        double most = 0;
        for (int i = 0; i < 60; ++i) e.tick(1.0 / 60), most = std::max(most, std::fabs(b.pose_of("knee").pitch));
        check(most > 0.3, "and its motion plays: the knee bends (" + std::to_string(most * 180 / 3.14159265) + " degrees)");
    }
    {
        // Shown in a room: a thing there for each part, riding where it stands.
        sg::StateGraph g;
        auto& room = g.add<sg::State>("room");
        room.add_element("spot", "anchor").params.set(sg::keys::x, 2.0);
        auto& b = person(g, "cara");
        const sg::Key f = sg::show(g, b, "room", "spot");
        const sg::Element* skull = room.find(sg::Key{"spot.skull"});
        check(g.functor(f) && skull && std::fabs(skull->params.num(sg::keys::y) - b.pose_of("skull").position.y) < 1e-9 &&
                  skull->params.get_or<std::string>(sg::keys::parent, "") == "spot",
              "shown in a room: each part a thing there, riding the spot it stands on, carried by a functor");
    }
    {
        // A model made elsewhere, rigged, in glTF: a strip of two joints, its
        // top bound to the upper joint, and an animation bending it.
        std::string bin;
        const auto f32 = [&](float v) { bin.append(reinterpret_cast<const char*>(&v), 4); };
        const auto u16 = [&](uint16_t v) { bin.append(reinterpret_cast<const char*>(&v), 2); };
        for (float v : {-0.1f, 0.f, 0.f, 0.1f, 0.f, 0.f, -0.1f, 2.f, 0.f, 0.1f, 2.f, 0.f}) f32(v);  // 0: positions, 48
        for (int i = 0; i < 4; ++i) bin += char(i < 2 ? 0 : 1), bin += char(0), bin += char(0), bin += char(0);  // 48: joints, 16
        for (int i = 0; i < 4; ++i) f32(1.f), f32(0.f), f32(0.f), f32(0.f);  // 64: weights, 64
        for (uint16_t v : {0, 1, 2, 1, 3, 2}) u16(v);  // 128: indices, 12
        for (float v : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f}) f32(v);  // 140: inverse binds, 128
        for (float v : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -1.f, 0.f, 1.f}) f32(v);
        f32(0.f), f32(1.f);  // 268: times, 8
        const float s = std::sqrt(0.5f);
        for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, s, s}) f32(v);  // 276: turns (x y z w), 32
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string enc;
        for (std::size_t i = 0; i < bin.size(); i += 3) {
            const uint32_t n = (uint32_t(uint8_t(bin[i])) << 16) | (i + 1 < bin.size() ? uint32_t(uint8_t(bin[i + 1])) << 8 : 0) |
                               (i + 2 < bin.size() ? uint32_t(uint8_t(bin[i + 2])) : 0);
            enc += b64[(n >> 18) & 63], enc += b64[(n >> 12) & 63];
            enc += i + 1 < bin.size() ? b64[(n >> 6) & 63] : '=';
            enc += i + 2 < bin.size() ? b64[n & 63] : '=';
        }
        const std::string gltf = std::string(R"({"asset":{"version":"2.0"},
  "nodes":[{"name":"root","children":[1]},{"name":"tip","translation":[0,1,0]},{"name":"strip","mesh":0,"skin":0}],
  "skins":[{"joints":[0,1],"inverseBindMatrices":6}],
  "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3}]}],
  "animations":[{"name":"bend","channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}],"samplers":[{"input":4,"output":5}]}],
  "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":1,"componentType":5121,"count":4,"type":"VEC4"},
    {"bufferView":2,"componentType":5126,"count":4,"type":"VEC4"},{"bufferView":3,"componentType":5123,"count":6,"type":"SCALAR"},
    {"bufferView":5,"componentType":5126,"count":2,"type":"SCALAR"},{"bufferView":6,"componentType":5126,"count":2,"type":"VEC4"},
    {"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"}],
  "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":16},{"buffer":0,"byteOffset":64,"byteLength":64},
    {"buffer":0,"byteOffset":128,"byteLength":12},{"buffer":0,"byteOffset":140,"byteLength":128},{"buffer":0,"byteOffset":268,"byteLength":8},
    {"buffer":0,"byteOffset":276,"byteLength":32}],
  "buffers":[{"byteLength":308,"uri":"data:application/octet-stream;base64,)") + enc + R"("}]})";
        sg::Being::files().read = [&](const std::string& path, std::string& bytes) { return path == "strip.gltf" ? (bytes = gltf, true) : false; };
        sg::Being::files().stamp = [](const std::string&) { return 1LL; };
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"rig"});
        std::string why;
        const bool ok = b.import_gltf("strip.gltf", "", &why);
        sg::drive(g, clock, "rig", b.live_event());
        g.set_initial("rig");
        check(ok && b.joints().size() == 2 && b.find(sg::Key{"skin"}) && b.find(sg::Key{"clip.bend"}), "glTF read: its skeleton, its skinned body, its animation as a clip " + why);
        const auto rest = b.skinned("skin");
        check(rest.size() == 6 * 8 && std::fabs(rest[2 * 8 + 1] - 2.0f) < 1e-5, "at rest, the skin is where it was bound");
        sg::Engine e(g);
        e.start();
        e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("bend")).set("fade", 0.0).set("loop", 0.0)});
        for (int i = 0; i < 90; ++i) e.tick(1.0 / 60);
        const auto bent = b.skinned("skin");
        float top_x = 0;
        for (std::size_t i = 0; i < bent.size(); i += 8)
            if (bent[i + 1] > 0.9f) top_x = std::min(top_x, bent[i]);
        check(top_x < -0.8f, "played, the skin bends with its joint: the top swung over (" + std::to_string(top_x) + ")");
        // And modulated, not invented: the same clip at half speed, held halfway.
        sg::StateGraph g2;
        auto& clock2 = g2.add<sg::Temporal>("clock");
        auto& b2 = g2.add<sg::Being>(sg::Key{"rig2"});
        b2.import_gltf("strip.gltf");
        sg::drive(g2, clock2, "rig2", b2.live_event());
        g2.set_initial("rig2");
        sg::Engine e2(g2);
        e2.start();
        e2.fire(sg::Event{b2.play_event(), sg::Params{}.set("clip", std::string("bend")).set("fade", 0.0).set("loop", 0.0).set("speed", 0.5)});
        for (int i = 0; i < 60; ++i) e2.tick(1.0 / 60);
        const double half = b2.pose_of("tip").pitch;
        check(half > 0.5 && half < 1.1, "and played at half speed, a second in it is half bent (" + std::to_string(half * 180 / 3.14159265) + " degrees)");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
