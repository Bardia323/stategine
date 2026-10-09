#include "sg/web/WebGPU.hpp"
#include "sg/domains/Texture.hpp"
#include "sg/render/Defaults.hpp"
#include "sg/render/Geometry.hpp"
#include "sg/render/Pack.hpp"
#include <cctype>
#include <cstring>
#include <emscripten/val.h>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
namespace sg::web {
using emscripten::val;
using spatial::projection::Mat4;
using spatial::projection::Vec3;
namespace {
template <class T> val copy(const std::vector<T> &data, const char *type) {
    return val::global(type).new_(val(emscripten::typed_memory_view(data.size(), data.data())));
}
Vec3 vec(Vec3d v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }
Mat4 web_depth(Mat4 m) {
    for (int c = 0; c < 4; ++c)
        m.m[c * 4 + 2] = (m.m[c * 4 + 2] + m.m[c * 4 + 3]) * 0.5f;
    return m;
}
void matrix(std::vector<float> &f, std::size_t at, const Mat4 &m) { std::copy(m.m, m.m + 16, f.begin() + at); }
} // namespace
struct WebGPUView::Impl {
    val handle;
    LookState standard{Key{"<standard>"}};
    LookFader fader{standard};
    std::map<Key, Surface2D *> surfaces;
    val shaders = val::array();
    bool configured = false, refused = false;
    unsigned resource_generation = 0;
    std::map<Key, std::vector<Key>> uniform_keys;
    std::map<Key, const LookState *> borrowed_looks;
    std::set<std::string> prepared_shaders;
    std::set<std::string> active_feeds, finished_feeds;
    // What the browser holds: each mesh by its key, each picture by its key
    // and revision - as of the last frame it drew, which kept exactly what
    // that frame used (and let the rest go). What this frame uses, the first
    // use of each carrying its data only when the browser does not hold it.
    std::set<std::string> held_meshes, used_meshes;
    std::map<std::string, std::string> held_pictures, used_pictures;
    unsigned held_generation = 0;
    // The shapes every frame draws, made once.
    const std::vector<float> cube = render::cube_vertices(), quad = render::quad_vertices(),
                             sphere = render::sphere_vertices();
    std::map<std::string, std::vector<float>> shapes;
    const std::vector<float> &shape(const std::string &key, const std::function<std::vector<float>()> &make) {
        auto it = shapes.find(key);
        if (it == shapes.end()) it = shapes.emplace(key, make()).first;
        return it->second;
    }
    // A mesh by what it is (its key), its corners sent, indexed, only when
    // the browser does not hold it.
    val geometry(const std::string &key, const std::vector<float> &mesh) {
        auto v = val::object();
        v.set("key", key);
        if (used_meshes.insert(key).second && !held_meshes.count(key)) {
            const render::Indexed ix = render::indexed(mesh);
            v.set("vertices", copy(ix.corners, "Float32Array"));
            if (ix.corners.size() / 8 <= 65536) {
                const std::vector<std::uint16_t> small(ix.index.begin(), ix.index.end());
                v.set("indices", copy(small, "Uint16Array"));
            } else {
                v.set("indices", copy(ix.index, "Uint32Array"));
            }
        }
        return v;
    }
    // A picture's bytes, sent only when the browser does not hold this
    // revision of it: packed (BC7, every mip) when the browser takes that
    // and the packed picture is kept by what names it, else its pixels.
    bool send_picture(const std::string &key, const std::string &revision) {
        if (!used_pictures.emplace(key, revision).second) return false;
        const auto held = held_pictures.find(key);
        return held == held_pictures.end() || held->second != revision;
    }
    MissingShader policy = MissingShader::Refuse;
    explicit Impl(const std::string &selector) : handle(val::global("sgGPU").call<val>("create", selector)) {
        render::standard_look(standard);
    }
    void configure() {
        if (resource_generation != handle.call<unsigned>("generation")) {
            resource_generation = handle.call<unsigned>("generation");
            configured = false;
        }
        if (!configured && !refused && handle.call<bool>("initialized")) {
            handle.call<val>("prepare", shaders, policy == MissingShader::BuiltinFallback);
            configured = true;
        }
    }
    std::string shader(const LookState *look, Key pass, const std::string &suffix = {}) const {
        if (!look)
            return {};
        const auto *p = look->find(pass);
        if (!p)
            return {};
        const auto source = p->params.get_or<std::string>("wgsl", {});
        if (source.empty()) {
            if (policy == MissingShader::Refuse &&
                (!p->params.get_or<std::string>("fs", {}).empty() || !p->params.get_or<std::string>("vs", {}).empty()))
                throw std::runtime_error("web: missing custom WGSL shader; prepare() required");
            return {};
        }
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : source)
            h = (h ^ c) * 1099511628211ULL;
        const auto key = look->id().str() + ":" + pass.str() + ":" + std::to_string(h) + suffix;
        if (!prepared_shaders.count(key))
            throw std::runtime_error("web: changed custom shader requires prepare(): " + key);
        return key;
    }
    val texture(Surface2D &surface) {
        auto t = val::object();
        surface.catch_up();
        const auto key = surface.id().str(), revision = std::to_string(surface.revision());
        t.set("key", key);
        t.set("w", surface.px_w());
        t.set("h", surface.px_h());
        t.set("srgb", surface.srgb());
        t.set("revision", revision);
        if (!send_picture(key, revision)) return t;
        Digest made;
        render::Packed packed;
        if (handle.call<bool>("packs") && dynamic_cast<const Texture *>(&surface) &&
            render::packable(surface.px_w(), surface.px_h()) && surface.pixels_digest(made) &&
            render::packed_kept(surface.px_w(), surface.px_h(), surface.srgb(), made, packed)) {
            auto levels = val::array();
            for (std::size_t i = 0; i < packed.levels.size(); ++i) {
                const auto &l = packed.levels[i];
                auto level = val::object();
                level.set("w", l.w);
                level.set("h", l.h);
                level.set("at", static_cast<double>(l.at));
                level.set("size", static_cast<double>(l.size));
                levels.set(i, level);
            }
            t.set("levels", levels);
            t.set("bytes", val::global("Uint8Array")
                               .new_(val(emscripten::typed_memory_view(packed.bytes.size(),
                                                                       reinterpret_cast<const unsigned char *>(packed.bytes.data())))));
            return t;
        }
        t.set("bytes", copy(surface.raster(), "Uint8Array"));
        return t;
    }
    val room(const StateGraph &g, const State &root, int w, int h, const render::ViewCamera *eye,
             std::vector<HalfSpace> clips, unsigned depth, val &all, const std::string &route, Key entered = {}) {
        const auto derived = render::view_plan(g, root);
        val out = val::object();
        out.set("key", root.id().str() + ":" + route);
        out.set("width", w);
        out.set("height", h);
        std::vector<float> frame(1048, 0);
        const LookState *look = nullptr;
        render::ViewCamera camera;
        const bool flat = derived.rooms.empty();
        if (!flat) {
            camera = eye ? *eye : derived.rooms[0].camera;
            look = derived.rooms[0].look;
        }
        const auto &target = look ? *look : standard;
        if (look)
            borrowed_looks[look->id()] = look;
        const auto &mix = fader.mix(root.id(), target);
        const auto value = [&](Key pass, const char *name, double fallback) {
            return static_cast<float>(fader.value(mix, pass, Key{name}, fallback));
        };
        Mat4 vp;
        if (flat)
            vp = web_depth(Mat4::ortho(-1, 1, -1, 1, -1, 1));
        else
            vp = web_depth(Mat4::perspective(static_cast<float>(camera.fov * 3.141592653589793 / 180),
                                             static_cast<float>(w) / h, static_cast<float>(camera.near_plane),
                                             static_cast<float>(camera.far_plane))) *
                 Mat4::look_at(vec(camera.eye), vec(camera.eye) + vec(camera.forward), vec(camera.up));
        matrix(frame, 0, vp);
        frame[16] = static_cast<float>(camera.eye.x);
        frame[17] = static_cast<float>(camera.eye.y);
        frame[18] = static_cast<float>(camera.eye.z);
        frame[20] = static_cast<float>(derived.time);
        frame[21] = static_cast<float>(std::min<std::size_t>(clips.size(), 8));
        frame[23] = flat ? 1 : 0;
        frame[24] = value(passes::scene, "uFogColor.x", .05);
        frame[25] = value(passes::scene, "uFogColor.y", .06);
        frame[26] = value(passes::scene, "uFogColor.z", .09);
        frame[27] = value(passes::scene, "uAmbient", .55);
        frame[28] = flat ? 0 : value(passes::scene, "uFogDensity", .018);
        frame[31] = value(passes::scene, "uShadowFloor", 0);
        frame[32] = value(passes::bright, "uThreshold", 1.05);
        frame[33] = value(passes::composite, "uBloomStrength", .55);
        frame[34] = value(passes::composite, "uExposure", 1.15);
        frame[35] = value(passes::composite, "uSaturation", 1);
        for (int i = 0; i < 3; ++i)
            frame[36 + i] = value(passes::composite, i == 0 ? "uTint.x" : i == 1 ? "uTint.y" : "uTint.z", 1);
        frame[39] = value(passes::composite, "ao", 0);
        frame[40] = value(passes::composite, "uVignette", .55);
        frame[41] = value(passes::composite, "uGrain", .015);
        frame[43] = value(passes::composite, "ao.radius", .3);
        frame[44] = value(passes::scene, "uDim", 0);
        for (std::size_t i = 0; i < clips.size() && i < 8; ++i) {
            frame[56 + i * 4] = static_cast<float>(clips[i].normal.x);
            frame[57 + i * 4] = static_cast<float>(clips[i].normal.y);
            frame[58 + i * 4] = static_cast<float>(clips[i].normal.z);
            frame[59 + i * 4] = static_cast<float>(clips[i].offset);
        }
        const auto vector = [&](std::size_t at, Key pass, const char *name, Vec3d fallback) {
            for (int c = 0; c < 3; ++c) {
                const auto key = std::string(name) + (c == 0 ? ".x" : c == 1 ? ".y" : ".z");
                frame[at + c] = value(pass, key.c_str(), c == 0 ? fallback.x : c == 1 ? fallback.y : fallback.z);
            }
        };
        vector(392, passes::scene, "uSky", {.10, .13, .20});
        frame[395] = value(passes::scene, "uShadowSoft", 1);
        vector(396, passes::scene, "uGround", {.14, .10, .07});
        frame[399] = value(passes::scene, "uWind", 0);
        vector(400, passes::scene, "uSkyTop", {.2, .4, .75});
        frame[403] = value(passes::scene, "uStars", 0);
        vector(404, passes::scene, "uSkyHorizon", {.72, .78, .84});
        frame[407] = value(passes::scene, "uClouds", 0);
        vector(408, passes::scene, "uCloudColor", {1, .95, .92});
        frame[411] = static_cast<float>(root.params().num("sky"));
        vector(412, passes::scene, "uCloudShade", {.55, .52, .62});
        frame[424] = static_cast<float>(camera.forward.x);
        frame[425] = static_cast<float>(camera.forward.y);
        frame[426] = static_cast<float>(camera.forward.z);
        frame[427] = static_cast<float>(std::tan(camera.fov * 3.141592653589793 / 360));
        vector(428, passes::composite, "uRayDir", {0, 1, 0});
        frame[431] = value(passes::composite, "uRays", 0);
        vector(432, passes::composite, "uRayColor", {1, 1, 1});
        frame[435] = value(passes::composite, "uRaySpread", .3);
        frame[436] = static_cast<float>(camera.near_plane);
        frame[437] = static_cast<float>(camera.far_plane);
        frame[438] = static_cast<float>(w) / h;
        frame[439] = value(passes::composite, "uRayCut", 1);
        frame[440] = value(passes::composite, "ao.radius", .3);
        frame[441] = value(passes::blur, "wide", .5);
        std::vector<render::DrawLight> lights;
        if (!flat)
            for (const auto *e : derived.rooms[0].lights) {
                auto l = render::light_of(root, *e);
                if (l.power > 0)
                    lights.push_back(l);
            }
        std::sort(lights.begin(), lights.end(), [](const auto &a, const auto &b) {
            if (a.indirect != b.indirect)
                return b.indirect;
            if (a.sun != b.sun)
                return a.sun;
            if (a.power != b.power)
                return a.power > b.power;
            if (a.pos.x != b.pos.x)
                return a.pos.x < b.pos.x;
            if (a.pos.z != b.pos.z)
                return a.pos.z < b.pos.z;
            return a.pos.y < b.pos.y;
        });
        const auto own = std::min<std::size_t>(lights.size(), 4);
        auto incoming = flat ? std::vector<render::DrawLight>{} : render::boundary_lights(g, derived.rooms[0], fader);
        const unsigned shadowed = static_cast<unsigned>(std::min<std::size_t>(own + incoming.size(), 8));
        for (std::size_t i = shadowed - own; i < incoming.size(); ++i)
            incoming[i].power *= incoming[i].open;
        lights.insert(lights.begin() + own, incoming.begin(), incoming.end());
        if (lights.size() > 24)
            lights.resize(24);
        frame[22] = static_cast<float>(lights.size());
        frame[442] = static_cast<float>(shadowed);
        for (std::size_t i = 0; i < lights.size(); ++i) {
            const auto &l = lights[i];
            frame[88 + i * 12] = l.pos.x;
            frame[89 + i * 12] = l.pos.y;
            frame[90 + i * 12] = l.pos.z;
            frame[91 + i * 12] = l.sun ? 1 : 0;
            frame[92 + i * 12] = l.color.x;
            frame[93 + i * 12] = l.color.y;
            frame[94 + i * 12] = l.color.z;
            frame[95 + i * 12] = l.power;
            frame[96 + i * 12] = l.dir.x;
            frame[97 + i * 12] = l.dir.y;
            frame[98 + i * 12] = l.dir.z;
            frame[99 + i * 12] = std::cos(l.outer);
            frame[488 + i * 4] = std::cos(l.inner);
            frame[489 + i * 4] = l.floor;
            frame[490 + i * 4] = l.falloff;
            frame[491 + i * 4] = l.indirect ? 1 : 0;
            if (l.sun && !l.gated && frame[420] == 0 && frame[421] == 0 && frame[422] == 0) {
                frame[416] = -l.dir.x;
                frame[417] = -l.dir.y;
                frame[418] = -l.dir.z;
                frame[420] = l.color.x;
                frame[421] = l.color.y;
                frame[422] = l.color.z;
            }
            frame[584 + i * 4] = l.gate_at.x;
            frame[585 + i * 4] = l.gate_at.y;
            frame[586 + i * 4] = l.gate_at.z;
            frame[587 + i * 4] = l.gate_w;
            frame[680 + i * 4] = l.gate_across.x;
            frame[681 + i * 4] = l.gate_across.z;
            frame[682 + i * 4] = l.gate_h;
            frame[683 + i * 4] = l.gated ? 1 : 0;
            if (i < shadowed) {
                frame[936 + i * 4] = l.gated ? l.gate_in.x : 0;
                frame[937 + i * 4] = l.gated ? l.gate_in.y : 0;
                frame[938 + i * 4] = l.gated ? l.gate_in.z : 0;
                frame[939 + i * 4] = l.gated ? .08f - spatial::projection::dot(l.gate_in, l.gate_at) : 1;
                float bias = 1;
                const auto m = web_depth(render::shadow_projection(l, camera, 1024, bias));
                matrix(frame, 776 + i * 16, m);
                frame[904 + i * 4] = bias;
            }
        }
        if (shadowed)
            std::copy(frame.begin() + 776, frame.begin() + 792, frame.begin() + 376);

        if (!flat) {
            const auto doors = render::boundary_ambient(g, derived.rooms[0], fader);
            frame[444] = static_cast<float>(doors.size());
            for (std::size_t i = 0; i < doors.size(); ++i) {
                const auto &d = doors[i];
                const std::size_t at = 968 + i * 20;
                frame[at] = d.at.x;
                frame[at + 1] = d.at.y;
                frame[at + 2] = d.at.z;
                frame[at + 3] = d.half_width;
                frame[at + 4] = d.across.x;
                frame[at + 5] = d.across.z;
                frame[at + 6] = d.half_height;
                frame[at + 8] = d.inward.x;
                frame[at + 9] = d.inward.z;
                frame[at + 12] = d.sky.x;
                frame[at + 13] = d.sky.y;
                frame[at + 14] = d.sky.z;
                frame[at + 16] = d.ground.x;
                frame[at + 17] = d.ground.y;
                frame[at + 18] = d.ground.z;
            }
        }
        out.set("frame", copy(frame, "Float32Array"));
        auto uniforms = val::object();
        for (const auto &keys : uniform_keys) {
            std::vector<float> values(1024, 0);
            for (std::size_t i = 0; i < keys.second.size(); ++i)
                values[i] = keys.second[i] == Key{"uTime"} ? static_cast<float>(derived.time)
                                                           : value(keys.first, keys.second[i].str().c_str(), 0);
            uniforms.set(keys.first.str(), copy(values, "Float32Array"));
        }
        out.set("uniforms", uniforms);
        auto clear = val::array();
        clear.set(0, value(passes::scene, "clear.x", .012));
        clear.set(1, value(passes::scene, "clear.y", .014));
        clear.set(2, value(passes::scene, "clear.z", .022));
        clear.set(3, 1);
        out.set("clear", clear);
        for (Key p : passes::all())
            out.set(p.str() + "Shader", shader(&mix.shown(), p));
        out.set("feedShader", shader(&mix.shown(), passes::composite, ":feed"));
        out.set("blurPasses", static_cast<int>(value(passes::blur, "passes", 3)));
        val composites = val::array();
        std::vector<std::pair<std::string, double>> programs;
        for (const auto &part : mix.parts) {
            const auto name = shader(part.look, passes::composite);
            auto it = std::find_if(programs.begin(), programs.end(), [&](const auto &p) { return p.first == name; });
            if (it == programs.end())
                programs.emplace_back(name, part.weight);
            else
                it->second += part.weight;
        }
        for (unsigned i = 0; i < programs.size(); ++i) {
            auto p = val::object();
            p.set("shader", programs[i].first);
            p.set("feed", programs[i].first.empty() ? "" : programs[i].first + ":feed");
            p.set("weight", programs[i].second);
            composites.set(i, p);
        }
        out.set("composites", composites);
        val batches = val::array();
        unsigned number = 0;
        const auto append = [&](const std::string &mesh_key, const std::vector<float> &mesh, const Mat4 &m, Rgb colour, double roughness,
                                double surface, double emissive, val picture, const std::string &feed, bool feedback,
                                double glass, const Element *source = nullptr, bool sample_hdr = false) {
            std::vector<float> data(m.m, m.m + 16);
            data.insert(data.end(),
                        {static_cast<float>(colour.r), static_cast<float>(colour.g), static_cast<float>(colour.b),
                         static_cast<float>(roughness), static_cast<float>(surface), static_cast<float>(emissive),
                         source && glass == 3 ? static_cast<float>(source->params.num("undim")) : 0,
                         source ? static_cast<float>(source->params.num("mirror")) : 0});
            data.insert(data.end(),
                        {static_cast<float>(glass), source ? static_cast<float>(source->params.num("flat")) : 0,
                         glass == 3 ? (sample_hdr ? 0 : 1)
                         : source   ? static_cast<float>(source->params.num("untone", feed.empty() ? 0 : 1))
                                    : 0,
                         source ? static_cast<float>(source->params.num("halo")) : 0,
                         source && glass == 6
                             ? static_cast<float>(std::fmod(std::max(0.0, std::floor(source->params.num("frame"))),
                                                            std::max(1.0, source->params.num("frames", 1))) /
                                                  std::max(1.0, source->params.num("frames", 1)))
                         : source && glass == 5 ? static_cast<float>(source->params.num("tile", 1))
                                                : 0,
                         0, glass == 6 ? static_cast<float>(1 / std::max(1.0, source->params.num("frames", 1))) : 0,
                         glass == 6 ? 1.0f : 0});
            auto batch = val::object();
            batch.set("mesh", geometry(mesh_key, mesh));
            batch.set("instances", copy(data, "Float32Array"));
            batch.set("picture", picture);
            batch.set("target", feed);
            batch.set("feedback", feedback);
            batch.set("sampleHdr", sample_hdr);
            batch.set("casts", (glass < .5 || glass == 4 || glass == 5) && surface != 9 &&
                                   (!source || source->params.num("cast", 1) > .5));
            batches.set(number++, batch);
        };
        if (derived.surface) {
            // raster() only memoizes pixels; the surface's semantic data is read.
            auto &surface = const_cast<Surface2D &>(*derived.surface);
            const auto m = Mat4::rotate_y(1.5707963267948966f) * Mat4::scale({1, 2, 2});
            append("quad", quad, m, {1, 1, 1}, 1, 0, 0, texture(surface), {}, false, 1);
        }
        const auto &instances = flat ? derived.sprites : derived.rooms[0].instances;
        if (!derived.surface)
            for (const auto &instance : instances) {
                const auto *e = instance.element;
                std::string mesh_key = "cube";
                const std::vector<float> *mesh = &cube;
                Mat4 m = instance.model;
                if (flat) {
                    m = Mat4::translate(vec(instance.pose.position)) *
                        Mat4::scale({static_cast<float>(instance.size.x), static_cast<float>(instance.size.y), .01f});
                } else if (e) {
                    const auto shape = e->params.get_or<std::string>("shape", {});
                    if (shape == "sphere") {
                        mesh_key = "sphere", mesh = &sphere;
                    } else if (shape == "cylinder") {
                        const float taper = static_cast<float>(e->params.num("taper", 1));
                        mesh_key = "cylinder:" + std::to_string(taper);
                        mesh = &this->shape(mesh_key, [&] { return render::cylinder_vertices(28, taper); });
                    } else if (shape == "model") {
                        const Key name{e->params.get_or<std::string>("model", {})};
                        const auto &space = dynamic_cast<const Spatial3D &>(root);
                        if (const auto *model = space.model(name)) {
                            // (By what it is: the model, as made this time.)
                            mesh_key = "model:" + root.id().str() + ":" + name.str() + ":" + std::to_string(space.model_revision(name));
                            mesh = model;
                        }
                    } else if (e->params.num("bevel") > 0) {
                        const float sx = static_cast<float>(instance.size.x), sy = static_cast<float>(instance.size.y),
                                    sz = static_cast<float>(instance.size.z), bevel = static_cast<float>(e->params.num("bevel")),
                                    taper = static_cast<float>(e->params.num("taper", 1));
                        mesh_key = "rounded:" + std::to_string(sx) + "," + std::to_string(sy) + "," + std::to_string(sz) + "," +
                                   std::to_string(bevel) + "," + std::to_string(taper);
                        mesh = &this->shape(mesh_key, [&] { return render::rounded_box_vertices(sx, sy, sz, bevel, taper); });
                    }
                }
                val skin = val::null();
                double mode = 0;
                if (!flat && e) {
                    const auto &space = dynamic_cast<const Spatial3D &>(root);
                    const bool sprite = e->params.is("shape", "sprite");
                    const auto name = e->params.get_or<std::string>(sprite ? Key{"picture"} : Key{"skin"}, {});
                    if (const auto *pic = space.picture(Key{name}); pic && pic->w > 0 && pic->h > 0) {
                        skin = val::object();
                        skin.set("key", root.id().str() + ":" + name);
                        skin.set("w", pic->w);
                        skin.set("h", pic->h);
                        skin.set("srgb", true);
                        skin.set("pixel", true);
                        skin.set("revision", std::to_string(pic->revision));
                        if (send_picture(root.id().str() + ":" + name, std::to_string(pic->revision)))
                            skin.set("bytes", copy(pic->rgba, "Uint8Array"));
                        mode = sprite ? 6 : 5;
                    }
                    if (sprite) {
                        if (skin.isNull())
                            continue;
                        mesh_key = "quad", mesh = &quad;
                        m = render::sprite_transform(root, *e, camera);
                    }
                }
                if (e && skin.isNull()) {
                    const auto it = surfaces.find(e->id);
                    if (it != surfaces.end() && it->second && render::declared_surface(g, *e, *it->second)) {
                        skin = texture(*it->second);
                        mode = 4;
                    }
                }
                append(mesh_key, *mesh, m, instance.colour, instance.roughness, instance.surface,
                       instance.emissive + (mode == 6 ? e->params.num("glow") : 0), skin, {}, false, mode, e);
            }
        if (!flat) {
            const auto &room = derived.rooms[0];
            if (root.params().num("sky") > .5)
                append("sphere", sphere,
                       Mat4::translate(vec(camera.eye)) * Mat4::scale({static_cast<float>(camera.far_plane * 1.8),
                                                                       static_cast<float>(camera.far_plane * 1.8),
                                                                       static_cast<float>(camera.far_plane * 1.8)}),
                       {1, 1, 1}, 1, 9, 0, val::null(), {}, false, 0);
            else
                for (const auto &d : render::enclosure(*room.room))
                    append("cube", cube, d.model, d.colour, d.roughness, d.surface, 0, val::null(), {}, false, 0);
            for (const auto &portal : room.portals) {
                const auto &e = *portal.portal;
                const auto pose = world_pose(root, e);
                const bool window = !portal.feed && dynamic_cast<const Spatial3D *>(portal.guest);
                const auto panel = render::portal_face(root, e, window);
                for (const auto &body : render::portal_body(root, e, window))
                    append("cube", cube, body.model, body.colour, body.roughness, 0, 0, val::null(), {}, false, 0);
                if (portal.seam && portal.seam->name == entered)
                    continue;
                val image = val::null();
                std::string feed;
                bool feedback = false;
                if (const auto *s = dynamic_cast<const Surface2D *>(portal.guest)) {
                    const auto binding = surfaces.find(e.id);
                    if (binding != surfaces.end() && binding->second == s && render::declared_surface(g, e, *s))
                        image = texture(*binding->second);
                }
                if (const auto *guest = dynamic_cast<const Spatial3D *>(portal.guest);
                    guest && (portal.feed || depth < 2)) {
                    auto farCamera = render::view_camera(portal.feed_eye ? *portal.feed_eye : guest->camera());
                    std::vector<HalfSpace> far;
                    if (!portal.feed) {
                        Element hostEye = dynamic_cast<const Spatial3D &>(root).camera(), carried = guest->camera();
                        hostEye.params.set(keys::x, camera.eye.x)
                            .set(keys::y, camera.eye.y)
                            .set(keys::z, camera.eye.z)
                            .set(keys::yaw, std::atan2(camera.forward.z, camera.forward.x))
                            .set(keys::pitch, std::asin(std::clamp(camera.forward.y, -1.0, 1.0)));
                        const auto side = spatial::projection::normalize(
                            spatial::projection::cross(vec(camera.forward), Vec3{0, 1, 0}));
                        const auto up = spatial::projection::cross(side, vec(camera.forward));
                        hostEye.params.set(keys::roll, std::atan2(spatial::projection::dot(vec(camera.up), side),
                                                                  spatial::projection::dot(vec(camera.up), up)));
                        const Key functor =
                            portal.seam ? (portal.seam->a == root.id() ? portal.seam->a_to_b : portal.seam->b_to_a)
                                        : portal.embedding->in;
                        if (const auto *f = g.functor(functor))
                            if (const auto *transport = f->transport_of(hostEye.id))
                                (*transport)(hostEye, carried);
                        farCamera = render::view_camera(carried);
                        far.push_back(render::portal_clip(root, e, hostEye, carried));
                    }
                    const auto *dimensions = portal.feed_eye ? portal.feed_eye : &e;
                    const int gw = portal.feed ? static_cast<int>(dimensions->params.num("feed_w", 640)) : w,
                              gh = portal.feed ? static_cast<int>(dimensions->params.num("feed_h", 480)) : h;
                    const auto path =
                        portal.feed ? "feed/" + portal.embedding->name.str()
                                    : route + ":" + root.id().str() + "." + e.id.str() + ":" +
                                          (portal.embedding ? portal.embedding->name.str() : portal.seam->name.str());
                    feed = guest->id().str() + ":" + path;
                    if (portal.feed && active_feeds.count(feed)) {
                        feedback = true;
                    } else if (!portal.feed || !finished_feeds.count(feed)) {
                        if (portal.feed)
                            active_feeds.insert(feed);
                        auto child = this->room(g, *guest, gw, gh, &farCamera, std::move(far),
                                                portal.feed ? depth : depth + 1, all, path,
                                                portal.feed   ? Key{}
                                                : portal.seam ? portal.seam->name
                                                              : Key{});
                        child.set("raw", !portal.feed && e.params.num("own_look") < .5);
                        all.set(all["length"].as<unsigned>(), child);
                        if (portal.feed) {
                            active_feeds.erase(feed);
                            finished_feeds.insert(feed);
                        }
                    }
                }
                if (!opens_from(e, camera.eye)) continue;
                if (!image.isNull() || !feed.empty())
                    append("quad", quad, panel, {1, 1, 1}, e.params.num("roughness", .6), 0,
                           e.params.num("glow", 0), image, feed, feedback,
                           e.params.num("crt") > .5 ? 2
                           : portal.feed            ? 1
                                                    : 3,
                           &e, !portal.feed && e.params.num("own_look") < .5);
            }
        }
        out.set("batches", batches);
        return out;
    }
};
WebGPUView::WebGPUView(const std::string &canvas) : impl_(std::make_unique<Impl>(canvas)) {}
WebGPUView::~WebGPUView() { impl_->handle.call<void>("destroy"); }
bool WebGPUView::ready() const {
    impl_->configure();
    return !impl_->refused && impl_->configured && impl_->handle.call<bool>("ready");
}
std::vector<std::string> WebGPUView::prepare(const StateGraph &g, MissingShader policy) {
    impl_->policy = policy;
    impl_->shaders = val::array();
    impl_->configured = false;
    impl_->refused = false;
    impl_->prepared_shaders.clear();
    impl_->uniform_keys.clear();
    auto out = look_defects(g);
    unsigned i = 0;
    for (Key pass : passes::all()) {
        std::set<Key> keys;
        keys.insert("uTime");
        for (const auto &kv : impl_->standard.element(pass).params)
            if (is_uniform_key(kv.first))
                keys.insert(kv.first);
        for (Key id : g.reachable())
            if (const auto *l = dynamic_cast<const LookState *>(g.find(id)))
                if (const auto *p = l->find(pass))
                    for (const auto &kv : p->params)
                        if (is_uniform_key(kv.first))
                            keys.insert(kv.first);
        if (keys.size() > 1024)
            throw std::length_error("web: custom Look uniform capacity exceeded");
        impl_->uniform_keys[pass] = {keys.begin(), keys.end()};
    }
    for (Key id : g.reachable())
        if (const auto *look = dynamic_cast<const LookState *>(g.find(id)))
            for (Key pass : passes::all())
                if (const auto *p = look->find(pass)) {
                    const auto source = p->params.get_or<std::string>("wgsl", {});
                    if (source.empty() && (!p->params.get_or<std::string>("fs", {}).empty() ||
                                           !p->params.get_or<std::string>("vs", {}).empty())) {
                        out.push_back("look " + id.str() + "/" + pass.str() + ": missing WGSL implementation");
                        if (policy == MissingShader::Refuse)
                            impl_->refused = true;
                    }
                    if (!source.empty())
                        for (const bool feed : {false, true}) {
                            if (feed && pass != passes::composite)
                                continue;
                            auto s = val::object();
                            std::uint64_t digest = 1469598103934665603ULL;
                            for (unsigned char c : source)
                                digest = (digest ^ c) * 1099511628211ULL;
                            const auto key = look->id().str() + ":" + pass.str() + ":" + std::to_string(digest) +
                                             (feed ? ":feed" : "");
                            impl_->prepared_shaders.insert(key);
                            s.set("key", key);
                            s.set("name", id.str() + "/" + pass.str());
                            s.set("pass", pass.str());
                            std::string prefix = "struct SgLookUniforms { values:array<vec4f,256> };\n@group(0) "
                                                 "@binding(6) var<uniform> sg_look:SgLookUniforms;\n";
                            unsigned at = 0;
                            std::set<std::string> functions;
                            for (Key k : impl_->uniform_keys[pass]) {
                                auto name = "sg_" + k.str();
                                for (char &c : name)
                                    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                                        c = '_';
                                if (!functions.insert(name).second)
                                    throw std::invalid_argument("web: colliding custom uniform names");
                                prefix += "fn " + name + "()->f32 { return sg_look.values[" + std::to_string(at / 4) +
                                          "][" + std::to_string(at % 4) + "]; }\n";
                                ++at;
                            }
                            s.set("code", source + "\n" + prefix);
                            s.set("format",
                                  feed ? "rgba8unorm"
                                  : pass == passes::composite
                                      ? val::global("navigator")["gpu"].call<std::string>("getPreferredCanvasFormat")
                                      : "rgba16float");
                            impl_->shaders.set(i++, s);
                        }
                }
    impl_->configure();
    return out;
}
std::vector<std::string> WebGPUView::diagnostics() const {
    auto data = impl_->handle.call<val>("diagnostics");
    std::vector<std::string> out;
    for (unsigned i = 0; i < data["length"].as<unsigned>(); ++i)
        out.push_back(data[i].as<std::string>());
    return out;
}
void WebGPUView::render(const StateGraph &g, const State &root, int w, int h, double dt) {
    if (w <= 0 || h <= 0)
        return;
    if (!std::isfinite(dt) || dt < 0)
        throw std::invalid_argument("web: invalid transient frame interval");
    if (!ready())
        return;
    for (const auto &borrowed : impl_->borrowed_looks)
        if (g.find(borrowed.first) != borrowed.second) {
            impl_->fader = LookFader(impl_->standard);
            impl_->borrowed_looks.clear();
            break;
        }
    impl_->fader.advance(dt);
    impl_->active_feeds.clear();
    impl_->finished_feeds.clear();
    // (A device made again holds nothing.)
    if (impl_->held_generation != impl_->resource_generation) {
        impl_->held_meshes.clear(), impl_->held_pictures.clear();
        impl_->held_generation = impl_->resource_generation;
    }
    impl_->used_meshes.clear(), impl_->used_pictures.clear();
    auto rooms = val::array();
    rooms.set(0, val::null());
    auto first = impl_->room(g, root, w, h, nullptr, {}, 0, rooms, root.id().str());
    rooms.set(0, first);
    // Drawn, the browser holds what this frame used - and only that (what
    // a frame does not use it lets go); not drawn, what it holds is as it was.
    if (!impl_->handle.call<bool>("render", rooms, w, h)) return;
    impl_->held_meshes.swap(impl_->used_meshes);
    impl_->held_pictures.swap(impl_->used_pictures);
}
void WebGPUView::bind_surface(Key p, Surface2D *s) { impl_->surfaces[p] = s; }
void WebGPUView::recreate() {
    impl_->configured = false;
    impl_->handle.call<void>("recreate");
}
} // namespace sg::web
