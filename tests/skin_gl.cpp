// Stategine - a skin posed on the card, against a real GL context
// (Being::bound, Being::pose_skin, Spatial3D::skin_model / pose_model,
// gl::Mesh::create_bound, the vertex stage's posed_by_joints).
//
// A strip of two joints, bent by its clip and shown in a room (show_skins):
// drawn posed by its joints on the card, it is the picture of its corners
// posed on the processor (Spatial3D::posed) drawn as a model - and bent, not
// as it was bound. Its shadow is posed too.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "sg/core/Engine.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Being.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/sg.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}

// A strip 0.2 m wide and 2 m tall: its foot bound to the root, its top to
// the tip a metre up, and a clip turning the tip a quarter about z.
std::string strip_gltf() {
    std::string bin;
    const auto f32 = [&](float v) { bin.append(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](uint16_t v) { bin.append(reinterpret_cast<const char*>(&v), 2); };
    for (float v : {-0.1f, 0.f, 0.f, 0.1f, 0.f, 0.f, -0.1f, 2.f, 0.f, 0.1f, 2.f, 0.f}) f32(v);
    for (int i = 0; i < 4; ++i) bin += char(i < 2 ? 0 : 1), bin += char(0), bin += char(0), bin += char(0);
    for (int i = 0; i < 4; ++i) f32(1.f), f32(0.f), f32(0.f), f32(0.f);
    for (uint16_t v : {0, 1, 2, 1, 3, 2}) u16(v);
    for (float v : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f}) f32(v);
    for (float v : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, -1.f, 0.f, 1.f}) f32(v);
    f32(0.f), f32(1.f);
    const float s = std::sqrt(0.5f);
    for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, s, s}) f32(v);
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    for (std::size_t i = 0; i < bin.size(); i += 3) {
        const uint32_t n = (uint32_t(uint8_t(bin[i])) << 16) | (i + 1 < bin.size() ? uint32_t(uint8_t(bin[i + 1])) << 8 : 0) |
                           (i + 2 < bin.size() ? uint32_t(uint8_t(bin[i + 2])) : 0);
        enc += b64[(n >> 18) & 63], enc += b64[(n >> 12) & 63];
        enc += i + 1 < bin.size() ? b64[(n >> 6) & 63] : '=';
        enc += i + 2 < bin.size() ? b64[n & 63] : '=';
    }
    return std::string(R"({"asset":{"version":"2.0"},
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
  "buffers":[{"byteLength":308,"uri":"data:application/octet-stream;base64,)") +
           enc + R"("}]})";
}
}  // namespace

int main() {
    const int W = 480, H = 360;
    sg::gl::Window window(W, H, "skin");
    const std::string gltf = strip_gltf();
    sg::Being::files().read = [&](const std::string& path, std::string& bytes) { return path == "strip.gltf" ? (bytes = gltf, true) : false; };
    sg::Being::files().stamp = [](const std::string&) { return 1LL; };

    sg::StateGraph g;
    auto& clock = g.add<sg::Temporal>("clock");
    auto& b = g.add<sg::Being>(sg::Key{"rig"});
    std::string why;
    check(b.import_gltf("strip.gltf", "", &why), "the strip is read " + why);
    sg::drive(g, clock, "rig", b.live_event(), false, sg::Keeps::Always);
    auto& room = g.add<sg::Spatial3D>("room");
    room.params().set("room_w", 6.0).set("room_d", 6.0).set("room_h", 4.0);
    g.set_initial("room");
    // (Reached from the room, so the engine steps them.)
    g.connect("room", sg::Key{"room.rig"}, "rig");
    g.connect("room", sg::Key{"room.clock"}, "clock");
    sg::Element& spot = room.fixture(sg::Key{"spot"}, 3.0, 0.0, 3.0);
    spot.params.set(sg::keys::sx, 0.01).set(sg::keys::sy, 0.01).set(sg::keys::sz, 0.01).set("unseen", 1.0);
    sg::Element& lamp = room.light(sg::Key{"lamp"}, {3.0, 3.5, 4.5});
    lamp.params.set(sg::keys::intensity, 1.2).set("dx", 0.0).set("dy", -0.6).set("dz", -0.8);
    sg::Element& eye = room.camera();
    eye.params.set(sg::keys::x, 3.0).set(sg::keys::y, 1.2).set(sg::keys::z, 5.6).set(sg::keys::yaw, -1.5707963).set(sg::keys::pitch, 0.0);

    sg::Engine e(g);
    e.start();
    sg::render::GLWorldView view;
    view.prepare(g);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    const auto picture = [&] {
        for (int i = 0; i < 12; ++i) {
            view.render(room, W, H);
            window.swap();
        }
        sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
        return px;
    };
    // At rest, as it was bound; then bent by its clip.
    sg::show_skins(room, b, sg::Key{"spot"});
    const sg::Key model{"rig.skin"};
    sg::Element* shown = room.find(sg::Key{"spot.skin"});
    check(shown && shown->params.num("skinned") > 0.5 && room.skin_bound(model) && room.skin_pose(model), "shown in the room: a skin its joints move");
    if (!shown) return 1;
    const std::vector<unsigned char> unbent = picture();
    b.hear(sg::Event{b.play_event(), sg::Params{}.set("clip", std::string("bend")).set("fade", 0.0).set("loop", 0.0)});
    b.dispatch_pending();
    for (int i = 0; i < 90; ++i) e.tick(1.0 / 60);
    sg::show_skins(room, b, sg::Key{"spot"});
    const std::vector<unsigned char> on_card = picture();
    if (const char* at = std::getenv("SG_SKIN_SHOT"))
        if (FILE* f = std::fopen(at, "wb")) {
            std::fprintf(f, "P6 %d %d 255\n", W, H);
            for (int j = H - 1; j >= 0; --j) std::fwrite(&on_card[static_cast<std::size_t>(j) * W * 3], 1, static_cast<std::size_t>(W) * 3, f);
            std::fclose(f);
        }
    // The same corners posed on the processor, drawn as a plain model.
    room.model(sg::Key{"rig.skin.posed"}, room.posed(model));
    shown->params.set("skinned", 0.0).set("model", std::string("rig.skin.posed"));
    const std::vector<unsigned char> on_processor = picture();
    shown->params.set("unseen", 1.0);
    const std::vector<unsigned char> without = picture();
    shown->params.set("unseen", 0.0);

    int most = 0, apart = 0, bent = 0, seen = 0;
    for (std::size_t i = 0; i < px.size(); ++i) {
        const int d = std::abs(int(on_card[i]) - int(on_processor[i]));
        most = std::max(most, d), apart += d > 8;
        bent += std::abs(int(on_card[i]) - int(unbent[i])) > 24;
        seen += std::abs(int(on_card[i]) - int(without[i])) > 24;
    }
    check(seen > 300, "the strip is seen (" + std::to_string(seen) + " levels it changes)");
    check(most <= 24 && apart < 40, "posed on the card, it is the picture of it posed on the processor (" + std::to_string(apart) +
                                         " levels apart by more than 8, " + std::to_string(most) + " at most)");
    check(bent > 300, "and bent by its joints, not as it was bound (" + std::to_string(bent) + " levels changed)");
    return ok ? 0 : 1;
}
