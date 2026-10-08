// A being: a skeleton, a body on it, and a spirit that gets it where it means
// to be - clips blended, a joint held, a hand reaching by inverse kinematics -
// each joint at its own stiffness, on its own clock.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/core/Text.hpp"
#include "sg/domains/Being.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/spatial/Math.hpp"

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    failures += ok ? 0 : 1;
}

namespace {

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
        const double before = sg::distance(ann.pose_of("hand_r").position, aim);
        for (int i = 0; i < 120; ++i) e.tick(1.0 / 60);
        const double after = sg::distance(ann.pose_of("hand_r").position, aim);
        check(before > 0.3 && after < 0.03, "reaching: the right hand goes to the point (" + std::to_string(before) + " m off, then " + std::to_string(after) + ")");
        e.fire(sg::Event{ann.release_event(), sg::Params{}.set("goal", std::string("hand_r"))});
        for (int i = 0; i < 90; ++i) e.tick(1.0 / 60);
        check(sg::distance(ann.pose_of("hand_r").position, aim) > 0.2, "let go, it falls back to what it was doing");

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
    {
        // The same strip, held by what is no joint (an `Armature` turned a
        // quarter about x and scaled to centimetres, as a Mixamo export is):
        // read in, it stands, bends and is skinned exactly as the plain one.
        // (`unit`: what a metre is in the plain one's mesh, its binds sizing it back.)
        const auto strip = [](bool held, float unit = 1.f) {
            std::string bin;
            const auto f32 = [&](float v) { bin.append(reinterpret_cast<const char*>(&v), 4); };
            const auto u16 = [&](uint16_t v) { bin.append(reinterpret_cast<const char*>(&v), 2); };
            for (float v : {-0.1f, 0.f, 0.f, 0.1f, 0.f, 0.f, -0.1f, 2.f, 0.f, 0.1f, 2.f, 0.f}) f32(v * unit);
            for (int i = 0; i < 4; ++i) bin += char(i < 2 ? 0 : 1), bin += char(0), bin += char(0), bin += char(0);
            for (int i = 0; i < 4; ++i) f32(1.f), f32(0.f), f32(0.f), f32(0.f);
            for (uint16_t v : {0, 1, 2, 1, 3, 2}) u16(v);
            const float k = 1.f / unit;
            if (held) {
                // Inverse binds: the holder undone (scaled up, turned back), then the joint.
                for (float v : {100.f, 0.f, 0.f, 0.f, 0.f, 0.f, -100.f, 0.f, 0.f, 100.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f}) f32(v);
                for (float v : {100.f, 0.f, 0.f, 0.f, 0.f, 0.f, -100.f, 0.f, 0.f, 100.f, 0.f, 0.f, 0.f, 0.f, 100.f, 1.f}) f32(v);
            } else {
                for (float v : {k, 0.f, 0.f, 0.f, 0.f, k, 0.f, 0.f, 0.f, 0.f, k, 0.f, 0.f, 0.f, 0.f, 1.f}) f32(v);
                for (float v : {k, 0.f, 0.f, 0.f, 0.f, k, 0.f, 0.f, 0.f, 0.f, k, 0.f, 0.f, -1.f, 0.f, 1.f}) f32(v);
            }
            f32(0.f), f32(1.f);
            const float h = std::sqrt(0.5f);
            // The bend: about the room's z - in the held tip's own frame, its y.
            if (held) for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, h, 0.f, h}) f32(v);
            else for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, h, h}) f32(v);
            static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string enc;
            for (std::size_t i = 0; i < bin.size(); i += 3) {
                const uint32_t n = (uint32_t(uint8_t(bin[i])) << 16) | (i + 1 < bin.size() ? uint32_t(uint8_t(bin[i + 1])) << 8 : 0) |
                                   (i + 2 < bin.size() ? uint32_t(uint8_t(bin[i + 2])) : 0);
                enc += b64[(n >> 18) & 63], enc += b64[(n >> 12) & 63];
                enc += i + 1 < bin.size() ? b64[(n >> 6) & 63] : '=';
                enc += i + 2 < bin.size() ? b64[n & 63] : '=';
            }
            const std::string nodes = held ? R"([{"name":"Armature","rotation":[0.70710678,0,0,0.70710678],"scale":[0.01,0.01,0.01],"children":[1]},
                {"name":"root","children":[2]},{"name":"tip","translation":[0,0,-100]},{"name":"strip","mesh":0,"skin":0}])"
                                           : R"([{"name":"root","children":[1]},{"name":"tip","translation":[0,1,0]},{"name":"strip","mesh":0,"skin":0}])";
            const std::string joints = held ? "[1,2]" : "[0,1]", tip = held ? "2" : "1";
            return std::string(R"({"asset":{"version":"2.0"},"nodes":)") + nodes + R"(,
  "skins":[{"joints":)" + joints + R"(,"inverseBindMatrices":6}],
  "meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3}]}],
  "animations":[{"name":"bend","channels":[{"sampler":0,"target":{"node":)" + tip + R"(,"path":"rotation"}}],"samplers":[{"input":4,"output":5}]}],
  "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":1,"componentType":5121,"count":4,"type":"VEC4"},
    {"bufferView":2,"componentType":5126,"count":4,"type":"VEC4"},{"bufferView":3,"componentType":5123,"count":6,"type":"SCALAR"},
    {"bufferView":5,"componentType":5126,"count":2,"type":"SCALAR"},{"bufferView":6,"componentType":5126,"count":2,"type":"VEC4"},
    {"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"}],
  "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":16},{"buffer":0,"byteOffset":64,"byteLength":64},
    {"buffer":0,"byteOffset":128,"byteLength":12},{"buffer":0,"byteOffset":140,"byteLength":128},{"buffer":0,"byteOffset":268,"byteLength":8},
    {"buffer":0,"byteOffset":276,"byteLength":32}],
  "buffers":[{"byteLength":308,"uri":"data:application/octet-stream;base64,)" + enc + R"("}]})";
        };
        const std::string plain = strip(false), held = strip(true), small = strip(false, 0.01f);
        sg::Being::files().read = [&](const std::string& path, std::string& bytes) {
            if (path == "plain.gltf") return bytes = plain, true;
            if (path == "held.gltf") return bytes = held, true;
            if (path == "small.gltf") return bytes = small, true;
            return false;
        };
        sg::Being::files().stamp = [](const std::string&) { return 1LL; };
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& a = g.add<sg::Being>(sg::Key{"plain"});
        auto& b = g.add<sg::Being>(sg::Key{"held"});
        std::string why;
        const bool ok = a.import_gltf("plain.gltf", "", &why) && b.import_gltf("held.gltf", "", &why);
        check(ok, "a skeleton held by what is no joint is read " + why);
        check(sg::distance(a.pose_of("tip").position, b.pose_of("tip").position) < 1e-6 && std::fabs(b.pose_of("tip").position.y - 1.0) < 1e-6,
              "it stands as the plain one does: upright, at its size in metres (the tip " + std::to_string(b.pose_of("tip").position.y) + " m up)");
        const auto rest_a = a.skinned("skin"), rest_b = b.skinned("skin");
        double most = 0;
        for (std::size_t i = 0; i < rest_a.size() && i < rest_b.size(); ++i) most = std::max(most, double(std::fabs(rest_a[i] - rest_b[i])));
        check(!rest_b.empty() && most < 1e-4, "and its skin is where the plain one's is (" + std::to_string(most) + " apart)");
        sg::drive(g, clock, "plain", a.live_event(), false, sg::Keeps::Always);
        sg::drive(g, clock, "held", b.live_event(), false, sg::Keeps::Always);
        g.set_initial("plain");
        g.connect("plain", sg::Key{"plain.go"}, "held");
        sg::Engine e(g);
        e.start();
        for (sg::Being* x : {&a, &b}) {
            x->hear(sg::Event{x->play_event(), sg::Params{}.set("clip", std::string("bend")).set("fade", 0.0).set("loop", 0.0)});
            x->dispatch_pending();
        }
        for (int i = 0; i < 90; ++i) e.tick(1.0 / 60);
        const auto bent_a = a.skinned("skin"), bent_b = b.skinned("skin");
        most = 0;
        for (std::size_t i = 0; i < bent_a.size() && i < bent_b.size(); ++i) most = std::max(most, double(std::fabs(bent_a[i] - bent_b[i])));
        check(most < 1e-3, "and it bends as the plain one bends (" + std::to_string(most) + " apart)");
        // Shown in a room (`show_skins`), the thing a skin is drawn as holds
        // all of it - its box is what the renderer culls it by and a hand
        // finds it in - however it is posed, whatever units its mesh was
        // made in. (Fitted in its bind pose's box, a mesh made in other
        // units than the being's was a speck at its feet, and vanished from
        // a picture that did not show them; a bend out of it, likewise.)
        const auto holds = [](const sg::Being& being) {
            sg::Spatial3D room{sg::Key{"room"}};
            sg::show_skins(room, being, sg::Key{"spot"});
            const sg::Element* e = room.find(sg::Key{"spot.skin"});
            const std::vector<float>* drawn = room.model(sg::Key{being.id().str() + ".skin"});
            const std::vector<float> tris = being.skinned("skin");
            if (!e || !drawn || drawn->size() != tris.size() || tris.empty()) return 1e9;
            const double x = e->params.num(sg::keys::x), y = e->params.num(sg::keys::y), z = e->params.num(sg::keys::z);
            const double sx = e->params.num(sg::keys::sx), sy = e->params.num(sg::keys::sy), sz = e->params.num(sg::keys::sz);
            double out = 0;  // how far the furthest corner is outside its box
            for (std::size_t i = 0; i < tris.size(); i += 8) {
                out = std::max({out, std::fabs(tris[i] - x) - sx / 2, y - tris[i + 1], tris[i + 1] - (y + sy), std::fabs(tris[i + 2] - z) - sz / 2});
                // and drawn at that size, there, it is where the skin is
                out = std::max({out, std::fabs(x + (*drawn)[i] * sx - tris[i]), std::fabs(y + ((*drawn)[i + 1] + 0.5) * sy - tris[i + 1]),
                                std::fabs(z + (*drawn)[i + 2] * sz - tris[i + 2])});
            }
            return out;
        };
        sg::Being c{sg::Key{"small"}};
        check(c.import_gltf("small.gltf", "", &why), "a skin made in hundredths of a metre is read " + why);
        const double small_out = holds(c), bent_out = holds(a);
        check(small_out < 1e-4, "shown in a room, a skin made in other units is in the box it is drawn in (" + std::to_string(small_out) +
                                    " m out)");
        check(bent_out < 1e-4, "and a skin bent far from its bind pose is too (" + std::to_string(bent_out) + " m out)");
    }
    {
        // Fitted to a reference: a body made in centimetres, facing +z and
        // standing half a metre up, put where the engine's humanoid stands -
        // as tall, facing the same way, on the same floor - walking as before.
        auto ref = std::make_unique<sg::Being>(sg::Key{"ref"});
        sg::humanoid(*ref, 1.7);
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"giant"});
        sg::humanoid(b, 175.0);
        b.face(3.14159265358979 / 2);
        b.lift(50.0);
        const sg::Vec3d f0 = b.facing();
        check(std::fabs(f0.z - 1.0) < 1e-3 && std::fabs(b.height() - ref->height() * 175.0 / 1.7) < 1.0,
              "made otherwise: " + std::to_string(b.height()) + " tall, facing (" + std::to_string(f0.x) + ", " + std::to_string(f0.z) + ")");
        sg::fit(b, *ref);
        const sg::Vec3d f = b.facing();
        check(std::fabs(b.height() - ref->height()) < 1e-3 && std::fabs(f.x - 1.0) < 1e-3 && std::fabs(b.extent_low() - ref->extent_low()) < 1e-3,
              "fitted to the reference: as tall (" + std::to_string(b.height()) + " m), facing x, on its floor (" + std::to_string(b.extent_low()) + ")");
        sg::drive(g, clock, "giant", b.live_event());
        g.set_initial("giant");
        sg::Engine e(g);
        e.start();
        e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("walk")).set("fade", 0.0)});
        double most = 0;
        for (int i = 0; i < 60; ++i) {
            e.tick(1.0 / 60);
            const auto l = b.pose_of("foot_l").position, r = b.pose_of("foot_r").position;
            most = std::max(most, std::fabs(l.x - r.x));
        }
        check(most > 0.2 && most < 1.0, "and it walks as the reference would, its feet passing along x (" + std::to_string(most) + " m apart at most)");
    }
    {
        // A clip that carries the root (as Mixamo's hips are carried): the
        // floor is found where the clip stands it, not where it was bound.
        auto ref = std::make_unique<sg::Being>(sg::Key{"ref"});
        sg::humanoid(*ref, 1.7);
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"lifted"});
        sg::humanoid(b, 1.7);
        const double hips = b.element("hips").params.num(sg::keys::y);
        b.clip("stand", "0 hips p 0 " + std::to_string(hips + 0.3) + " 0\n1 hips p 0 " + std::to_string(hips + 0.3) + " 0\n");
        sg::fit(b, *ref);
        sg::drive(g, clock, "lifted", b.live_event());
        g.set_initial("lifted");
        sg::Engine e(g);
        e.start();
        e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("stand")).set("fade", 0.0)});
        for (int i = 0; i < 30; ++i) e.tick(1.0 / 60);
        check(std::fabs(b.extent_low() - ref->extent_low()) < 0.01, "a clip that carries its root: fitted, it stands on the floor as the clip stands it (" + std::to_string(b.extent_low()) + ")");
    }
    {
        // One dance, two bodies: a functor carries each joint's turn away
        // from its bind pose onto the same joint of another - smaller, and
        // with its arm's bone axes turned another way, so a turn copied as
        // it is would point its hand elsewhere.
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& a = g.add<sg::Being>(sg::Key{"dancer"});
        a.joint("hips", "", {0, 1.0, 0});
        a.joint("mixamorig:Arm", "hips", {0, 0.5, 0});
        a.joint("mixamorig:Hand", "mixamorig:Arm", {0, -0.5, 0});
        a.clip("reach", "0 mixamorig:Arm 30 80 0\n1 mixamorig:Arm 30 80 0\n0 hips p 0.2 0.9 0\n1 hips p 0.2 0.9 0");
        auto& b = g.add<sg::Being>(sg::Key{"kid"});
        b.joint("hips", "", {0, 0.6, 0});
        // Its arm at rest turned a quarter about x, its hand's offset turned
        // back so the hand hangs where the dancer's does, at its size.
        const sg::spatial::M3 roll = sg::spatial::from_euler(0, 0, 3.14159265358979 / 2);
        const sg::spatial::V3 hand = sg::spatial::transpose(roll) * sg::spatial::V3{0, -0.3, 0};
        b.joint("Arm", "hips", {0, 0.3, 0}, {0, 0, 90});
        b.joint("Hand", "Arm", {hand.x, hand.y, hand.z});
        const auto pairs = sg::same_joints(a, b);
        check(pairs.size() == 3, "the same skeleton found under another rig's names (" + std::to_string(pairs.size()) + " joints)");
        const sg::Key f = sg::retarget(g, a, b);
        sg::drive(g, clock, "dancer", a.live_event());
        sg::drive(g, clock, "kid", b.live_event(), false, sg::Keeps::Always);
        g.set_initial("dancer");
        a.hear(sg::Event{a.play_event(), sg::Params{}.set("clip", std::string("reach")).set("fade", 0.0)});
        a.dispatch_pending();
        b.hear(sg::Event{b.lead_event(), sg::Params{}.set("weight", 1.0).set("fade", 0.0)});
        b.dispatch_pending();
        const sg::LawReport r = sg::verify(g);
        for (const auto& v : r.violations) std::printf("     %s: %s\n", v.where.c_str(), v.detail.c_str());
        check(r.structure.empty() && r.ok() && g.functor(f), "retargeted: two beings and the functor between them keep the laws");
        sg::Engine e(g);
        e.set_strict(true);
        e.start();
        for (int i = 0; i < 60; ++i) e.tick(1.0 / 60);
        const auto dir = [](const sg::Being& s, const char* arm, const char* hd) {
            const sg::Vec3d d = s.pose_of(sg::Key{hd}).position - s.pose_of(sg::Key{arm}).position;
            const double l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            return d * (1.0 / l);
        };
        const sg::Vec3d da = dir(a, "mixamorig:Arm", "mixamorig:Hand"), db = dir(b, "Arm", "Hand");
        const double off = std::acos(std::min(1.0, da.x * db.x + da.y * db.y + da.z * db.z)) * 180 / 3.14159265;
        check(da.y > -0.5 && off < 1.0, "the kid's hand points where the dancer's does, though its arm's axes are turned (" + std::to_string(off) + " degrees off)");
        const double ha = a.pose_of("hips").position.y, hb = b.pose_of("hips").position.y;
        check(std::fabs(hb / ha - 0.6) < 0.01 && std::fabs(b.pose_of("hips").position.x - 0.12) < 0.01,
              "and its hips travel as far for its size (" + std::to_string(hb) + " m against " + std::to_string(ha) + ")");
        // Let go: it gives itself back to its own clips (none: its rest).
        b.hear(sg::Event{b.lead_event(), sg::Params{}.set("weight", 0.0).set("fade", 0.1)});
        b.dispatch_pending();
        for (int i = 0; i < 90; ++i) e.tick(1.0 / 60);
        const sg::Vec3d rest = dir(b, "Arm", "Hand");
        check(rest.y < -0.99 && std::fabs(b.pose_of("hips").position.y - 0.6) < 1e-3, "let go, it stands as it was made again");
    }
    {
        // A blend: two clips played together by how near a point is to each,
        // in step, steered and eased there.
        sg::StateGraph g;
        auto& clock = g.add<sg::Temporal>("clock");
        auto& b = g.add<sg::Being>(sg::Key{"mover"});
        b.joint("hips", "", {0, 1.0, 0});
        b.joint("arm", "hips", {0, 0.5, 0}, {}, 200);
        b.clip("slow", "0 arm 0 20 0\n1 arm 0 20 0");
        b.clip("fast", "0 arm 0 80 0\n0.5 arm 0 80 0");
        b.blend("go", "slow 0\nfast 1");
        sg::drive(g, clock, "mover", b.live_event());
        g.set_initial("mover");
        const sg::LawReport r = sg::verify(g);
        for (const auto& v : r.violations) std::printf("     %s: %s\n", v.where.c_str(), v.detail.c_str());
        check(r.structure.empty() && r.ok(), "a being with a blend keeps the laws");
        sg::Engine e(g);
        e.set_strict(true);
        e.start();
        e.fire(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("go")).set("fade", 0.0)});
        for (int i = 0; i < 30; ++i) e.tick(1.0 / 60);
        const double at0 = b.pose_of("arm").pitch * 180 / 3.14159265;
        e.fire(sg::Event{b.steer_event(), sg::Params{}.set("blend", std::string("go")).set(sg::keys::x, 0.5)});
        for (int i = 0; i < 120; ++i) e.tick(1.0 / 60);
        const double half = b.pose_of("arm").pitch * 180 / 3.14159265;
        e.fire(sg::Event{b.steer_event(), sg::Params{}.set("blend", std::string("go")).set(sg::keys::x, 1.0)});
        for (int i = 0; i < 120; ++i) e.tick(1.0 / 60);
        const double all = b.pose_of("arm").pitch * 180 / 3.14159265;
        check(std::fabs(at0 - 20) < 1 && std::fabs(half - 50) < 2 && std::fabs(all - 80) < 1,
              "steered along it, the pose goes from one clip through the mean to the other (" + std::to_string(at0) + ", " + std::to_string(half) + ", " +
                  std::to_string(all) + " degrees)");
        const double ph = b.element("layer0").params.num("phase");
        check(ph >= 0 && ph < 1, "its clips go round together, on one phase (" + std::to_string(ph) + ")");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
