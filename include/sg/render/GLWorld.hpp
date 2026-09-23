// Stategine - the OpenGL view of a 3D spatial state.
//
// A view, not a state: it reads elements and draws them. Swapping it for the
// terminal view (or running both) changes nothing about the domain, and any
// 3D state gets this renderer for free as long as it uses the shared parameter
// vocabulary (x/y/z, sx/sy/sz, r/g/b, w/h/yaw).
//
// A portal element shows whatever is bound to it:
//   bind_surface(portal, Surface2D*)                 a 2D state, as a panel
//   bind_world(portal, Spatial3D*)                   another 3D state, as a
//                                                    window you can walk through
//
// Frame: for each world portal, render the other room into its own target from
// that room's own camera (a View embedding aims it); then the shadow pass, the
// scene into a multisampled HDR target, resolve, bright pass, blur, composite.
//
// How each pass looks is itself a state: a LookState worn by the room (see
// domains/Look.hpp). Each room is drawn with its own look, so fog and shaders
// can differ on either side of a doorway; the post passes follow the room the
// viewer stands in. When a room's look changes, or the viewer changes rooms,
// numbers fade over the incoming look's `fade` seconds and the composite pass
// crossfades between shaders. `prepare(graph)` compiles every look the graph
// can reach before the first frame, so none of that compiles mid-game.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/domains/Look.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/domains/Surface.hpp"
#include "sg/gl/Renderer.hpp"
#include "sg/gl/Shaders.hpp"

namespace sg::render {

// A model matrix in a room's own coordinates, before that room is placed.
//
// The distinction is load-bearing. Where a room sits depends on which room the
// viewer is standing in, so the placement belongs in the geometry (uModel) and
// nowhere near surface detail (uTexModel) - conflate them and every texture in
// the building slides when you walk through a door. Giving "not yet placed"
// its own type means the placement is applied exactly once, in one function,
// and handing an already-placed matrix to a room-local parameter will not
// compile.
struct RoomMatrix {
    gl::Mat4 m;
};

inline RoomMatrix room_local(const gl::Mat4& m) { return RoomMatrix{m}; }

struct GLQuality {
    int shadow_size = 2048;
    int msaa = 4;
    float bloom_strength = 0.55f;
    float bloom_threshold = 1.05f;
    float exposure = 1.15f;
    int bloom_passes = 3;  // horizontal+vertical pairs
};

// The standard look: every value the built-in shaders read, and the fallback
// for anything a look leaves unset. A look therefore only states how it
// differs, and two looks always have a value to fade between.
inline void standard_look(LookState& l, const GLQuality& q = {}) {
    l.uniform(passes::scene, "uFogColor", 0.05, 0.06, 0.09)
        .uniform(passes::scene, "uFogDensity", 0.018)
        .uniform(passes::scene, "uSky", 0.10, 0.13, 0.20)
        .uniform(passes::scene, "uGround", 0.14, 0.10, 0.07)
        .uniform(passes::scene, "uAmbient", 0.55)
        .setting(passes::scene, "clear.x", 0.012)
        .setting(passes::scene, "clear.y", 0.014)
        .setting(passes::scene, "clear.z", 0.022)
        .uniform(passes::bright, "uThreshold", q.bloom_threshold)
        .setting(passes::blur, "passes", q.bloom_passes)
        .uniform(passes::composite, "uBloomStrength", q.bloom_strength)
        .uniform(passes::composite, "uExposure", q.exposure)
        .uniform(passes::composite, "uTint", 1.0, 1.0, 1.0)
        .uniform(passes::composite, "uSaturation", 1.0)
        .uniform(passes::composite, "uVignette", 0.55)
        .uniform(passes::composite, "uGrain", 0.015);
}

class GLWorldView {
public:
    static constexpr std::size_t kMaxLights = 4;    // matches the scene shader
    static constexpr std::size_t kShadowMaps = 2;  // the nearest two cast
    static constexpr int kMaxBounds = 8;           // doorways per room; matches the scene shader

    explicit GLWorldView(GLQuality q = {}) : q_(q), standard_(Key{"<standard>"}) {
        standard_look(standard_, q_);
    }

    struct LookStats {
        int programs = 0;       // compiled, shared by every look with the same source
        int late = 0;           // compiled mid-frame, because prepare() never saw them
        double compile_ms = 0;  // time spent compiling
    };
    const LookStats& stats() const { return stats_; }

    // Advance fades by a fixed step per frame instead of real time: headless
    // renders then show the same moment of a fade on every machine. 0 = real time.
    void set_fixed_step(double seconds) { fixed_step_ = seconds; }

    // Compile every look the graph can show - those worn by the states
    // reachable from its initial one - and check each against what this
    // renderer feeds it. Call once there is a GL context, before the first
    // frame. What comes back is what is wrong, as counterexamples; a look
    // whose shader fails is shown with the built-in one instead.
    std::vector<std::string> prepare(const StateGraph& g) {
        graph_ = &g;
        ensure_resources();
        std::vector<std::string> out = look_defects(g);
        std::set<Key> reach = g.reachable();
        if (reach.empty())
            for (Key id : g.ids()) reach.insert(id);
        preparing_ = true;
        for (Key id : reach)
            if (const auto* look = dynamic_cast<const LookState*>(g.find(id)))
                check_look(*look, out);
        preparing_ = false;
        return out;
    }

    // Attach a 2D state to a portal element: its raster becomes the texture.
    void bind_surface(Key portal_element, Surface2D* surface) {
        surfaces_[portal_element].surface = surface;
    }

    // Attach another 3D state: the portal becomes a window into it, rendered
    // from that state's own camera. A `View` embedding is what keeps that
    // camera aimed - see sg::portal_carry - so the renderer does no portal
    // maths of its own.
    void bind_world(Key portal_element, Spatial3D* world) { worlds_[portal_element].world = world; }

    // Draw a highlight on one element for this frame (a "you can use this" cue).
    void highlight(Key element_id) { highlight_ = element_id; }

    // One room, standing on its own.
    void render(Spatial3D& world, int fb_w, int fb_h) {
        const PlacedRoom one{&world, Pose{}, {}};
        render(std::vector<PlacedRoom>{one}, fb_w, fb_h);
    }

    // A neighbourhood of rooms, each with the pose it has when seen from the
    // first one - which is the room the viewer is standing in. Nothing here
    // treats that room as special beyond being the one asked from: hand it a
    // different root and the same geometry is drawn from the other side.
    void render(const std::vector<PlacedRoom>& rooms, int fb_w, int fb_h) {
        if (fb_w <= 0 || fb_h <= 0 || rooms.empty() || !rooms.front().room) return;
        ensure_resources();
        ensure_targets(fb_w, fb_h);
        advance_clock();

        Spatial3D& world = *rooms.front().room;
        // The post passes belong to the viewer, so they wear the look of the
        // room the viewer is in, and fade when that room changes.
        post_ = mix(view_key(), look_of(world));
        const Camera eye_cam = camera_of(world);
        const float aspect = static_cast<float>(fb_w) / static_cast<float>(fb_h);

        // --- portal views, one pass per window --------------------------------
        // Rendered first, at framebuffer resolution, because the portal quad
        // samples them in screen space. The guest's camera was already carried
        // through the doorway by the embedding's functor.
        for (const auto& e : world.elements()) {
            if (e.kind != kinds::portal || !e.alive) continue;
            auto it = worlds_.find(e.id);
            if (it == worlds_.end() || !it->second.world) continue;
            WorldPortal& wp = it->second;
            if (!wp.target.valid() || wp.width != fb_w || wp.height != fb_h) {
                wp.target.create(fb_w, fb_h, gl::GL_RGBA16F, 0, true);
                wp.width = fb_w;
                wp.height = fb_h;
            }
            // The virtual camera stands behind the far room's wall - that is
            // what a portal is - so the near plane is pushed out to the far
            // doorway. Everything between, the wall included, is clipped away.
            const Camera guest_cam = camera_of(*wp.world);
            const Element* back = back_portal(*wp.world, world);
            float znear = 0.05f;
            if (back) {
                const float along = gl::dot(to_vec3(position_of(*back)) - guest_cam.eye,
                                            gl::normalize(guest_cam.forward));
                znear = std::max(0.05f, along + 0.02f);
            }
            const PlacedRoom guest{wp.world, Pose{}, {}};
            draw_world(std::vector<PlacedRoom>{guest}, guest_cam, aspect, wp.target,
                       /*depth=*/1, znear, back ? back->id : Key{});
        }

        // --- the room the viewer is actually standing in ------------------------
        draw_world(rooms, eye_cam, aspect, scene_target_, /*depth=*/0, 0.05f);

        scene_target_.blit_to(resolve_);
        run_bloom();
        composite(fb_w, fb_h);
        highlight_ = Key{};
    }

private:
    struct Camera {
        gl::Vec3 eye;
        gl::Vec3 forward{0, 0, -1};
        float fov = 1.2f;
    };

    struct Light {
        gl::Vec3 pos{0, 3, 0};
        gl::Vec3 dir{0, -1, 0};
        gl::Vec3 color{1.0f, 0.93f, 0.82f};
        float power = 26.0f;
        float inner = 0.55f;  // radians
        float outer = 1.15f;
    };

    struct BoundSurface {
        Surface2D* surface = nullptr;
        gl::Texture texture;
        uint64_t revision = 0;
    };

    struct WorldPortal {
        Spatial3D* world = nullptr;
        gl::RenderTarget target;
        int width = 0, height = 0;
    };

    void set_frame(const Pose& p) {
        frame_ = p;
        frame_matrix_ = gl::Mat4::translate({static_cast<float>(p.position.x),
                                             static_cast<float>(p.position.y),
                                             static_cast<float>(p.position.z)}) *
                        gl::Mat4::rotate_y(static_cast<float>(p.yaw));
    }

    bool has_surface(const Element& e) const {
        auto it = surfaces_.find(e.id);
        return it != surfaces_.end() && it->second.surface != nullptr;
    }

    static Camera camera_of(Spatial3D& world) {
        const Element& cam = world.camera();
        Camera c;
        c.eye = to_vec3(position_of(cam));
        c.forward = to_vec3(forward_of(cam));
        c.fov = static_cast<float>(cam.params.num(keys::fov, 70.0)) * 3.14159265f / 180.0f;
        return c;
    }

    static gl::Vec3 to_vec3(const Vec3d& v) {
        return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
    }

    static gl::Vec3 color_of(const Element& e, gl::Vec3 fallback) {
        return {static_cast<float>(e.params.num(keys::r, fallback.x)),
                static_cast<float>(e.params.num(keys::g, fallback.y)),
                static_cast<float>(e.params.num(keys::b, fallback.z))};
    }

    void ensure_resources() {
        if (ready_) return;
        builtin_ = {
            {passes::shadow, {gl::depth_vs(), gl::depth_fs()}},
            {passes::scene, {gl::scene_vs(), gl::scene_fs()}},
            {passes::bright, {gl::post_vs(), gl::bright_fs()}},
            {passes::blur, {gl::post_vs(), gl::blur_fs()}},
            {passes::composite, {gl::post_vs(), gl::composite_fs()}},
        };
        const bool was = preparing_;
        preparing_ = true;  // the built-ins are never late
        for (Key p : passes::all()) program_for(standard_, p);
        preparing_ = was;
        cube_.create(gl::cube_vertices());
        quad_.create(gl::quad_vertices());
        screen_.create();
        for (auto& sm : shadow_) sm.create(q_.shadow_size);
        ready_ = true;
    }

    void ensure_targets(int w, int h) {
        if (w == target_w_ && h == target_h_) return;
        target_w_ = w;
        target_h_ = h;
        scene_target_.create(w, h, gl::GL_RGBA16F, q_.msaa, true);
        resolve_.create(w, h, gl::GL_RGBA16F, 0, false);
        const int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
        bloom_a_.create(bw, bh, gl::GL_RGBA16F, 0, false);
        bloom_b_.create(bw, bh, gl::GL_RGBA16F, 0, false);
    }

    // Every lamp in the state, nearest to the viewer first: that one gets the
    // shadow map, the rest light without casting.
    std::vector<Light> read_lights(const std::vector<PlacedRoom>& rooms, const Camera& cam) {
        std::vector<Light> out;
        for (const PlacedRoom& placed : rooms) {
            if (!placed.room) continue;
            for (const auto& e : placed.room->elements()) {
            if (e.kind != kinds::light || !e.alive) continue;
            Light l;
            l.pos = to_vec3(compose_pose(placed.pose, world_pose(*placed.room, e)).position);
            l.color = color_of(e, l.color);
            l.power = static_cast<float>(e.params.num(keys::intensity, 1.0)) * 26.0f;
            l.dir = gl::normalize({static_cast<float>(e.params.num(Key{"dx"}, 0.0)),
                                   static_cast<float>(e.params.num(Key{"dy"}, -1.0)),
                                   static_cast<float>(e.params.num(Key{"dz"}, 0.0))});
            l.inner = static_cast<float>(e.params.num(Key{"inner"}, 0.55));
            l.outer = static_cast<float>(e.params.num(Key{"outer"}, 1.15));
            out.push_back(l);
            }
        }
        std::sort(out.begin(), out.end(), [&cam](const Light& a, const Light& b) {
            const gl::Vec3 da = a.pos - cam.eye, db = b.pos - cam.eye;
            return gl::dot(da, da) < gl::dot(db, db);
        });
        if (out.size() > kMaxLights) out.resize(kMaxLights);
        if (out.empty()) out.push_back(Light{});
        return out;
    }

    // The doorway in `guest` that leads back to `host` - the one being looked
    // through. Its plane is where the portal view has to start, or the wall it
    // is set into hides everything; and it must not be drawn in that view, or
    // seen from the virtual camera it fills the whole frame.
    const Element* back_portal(Spatial3D& guest, Spatial3D& host) const {
        for (const auto& e : guest.elements()) {
            if (e.kind != kinds::portal || !e.alive) continue;
            auto it = worlds_.find(e.id);
            if (it != worlds_.end() && it->second.world == &host) return &e;
        }
        return nullptr;
    }

    // Shadow pass plus scene pass for one room, into one target. `depth` is the
    // portal recursion level: a room seen through a window does not itself open
    // further windows.

    void draw_world(const std::vector<PlacedRoom>& rooms, const Camera& cam, float aspect,
                    gl::RenderTarget& target, int depth, float znear, Key skip_portal = Key{}) {
        const std::vector<Light> lights = read_lights(rooms, cam);
        // The two nearest lamps get a shadow map each: standing in one room
        // must not flatten the other one.
        const std::size_t shadowed = std::min<std::size_t>(lights.size(), kShadowMaps);
        gl::Mat4 light_vp[kShadowMaps];
        for (std::size_t i = 0; i < kShadowMaps; ++i) {
            const Light& l = lights[std::min(i, lights.size() - 1)];
            light_vp[i] = gl::Mat4::perspective(l.outer * 2.05f, 1.0f, 0.35f, 60.0f) *
                          gl::Mat4::look_at(l.pos, l.pos + l.dir, {0, 0, 1});
        }
        const gl::Mat4 view_proj =
            gl::Mat4::perspective(cam.fov, aspect, znear, 120.0f) *
            gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, {0, 1, 0});

        // --- shadow depth, one pass per shadowed lamp ------------------------
        gl::glEnable(gl::GL_DEPTH_TEST);
        gl::glEnable(gl::GL_CULL_FACE);
        gl::glCullFace(gl::GL_FRONT);  // front-face culling hides most acne
        const gl::Program& caster = *program_for(post_.shown(), passes::shadow);
        caster.use();
        apply_uniforms(caster, post_, passes::shadow);
        for (std::size_t i = 0; i < shadowed; ++i) {
            shadow_[i].bind();
            gl::glClear(gl::GL_DEPTH_BUFFER_BIT);
            caster.set("uLightViewProj", light_vp[i]);
            for (const PlacedRoom& placed : rooms) {
                if (!placed.room) continue;
                set_frame(placed.pose);
                for (const auto& e : placed.room->elements()) {
                    if (!e.alive) continue;
                    if (e.kind == kinds::mesh || e.kind == kinds::wall) {
                        caster.set("uModel", frame_matrix_ * box_model(*placed.room, e).m);
                        cube_.draw();
                    } else if (e.kind == kinds::portal && !is_doorway(e) && has_surface(e)) {
                        caster.set("uModel",
                                   frame_matrix_ * portal_frame_model(*placed.room, e).m);
                        cube_.draw();
                    }
                }
            }
        }
        gl::glCullFace(gl::GL_BACK);

        // --- the scene -------------------------------------------------------
        // The first room is the one this view is taken from; its look clears.
        const Mix first = mix(rooms.front().room->id(), look_of(*rooms.front().room));
        target.bind();
        gl::glClearColor(static_cast<float>(setting(first, passes::scene, "clear.x", 0.012)),
                         static_cast<float>(setting(first, passes::scene, "clear.y", 0.014)),
                         static_cast<float>(setting(first, passes::scene, "clear.z", 0.022)),
                         1.0f);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
        gl::glEnable(gl::GL_DEPTH_TEST);
        gl::glDepthFunc(gl::GL_LESS);
        gl::glEnable(gl::GL_MULTISAMPLE);
        gl::glDisable(gl::GL_CULL_FACE);
        for (int i = 0; i < kMaxBounds; ++i)
            gl::glEnable(gl::GL_CLIP_DISTANCE0 + static_cast<gl::GLenum>(i));
        shadow_[0].bind_depth(1);
        shadow_[1].bind_depth(2);

        // Everything a scene shader is fed that is not the look's. Set again
        // whenever a room's look brings a different program.
        const auto frame_uniforms = [&](const gl::Program& p) {
            p.set("uViewProj", view_proj);
            p.set("uLightViewProj0", light_vp[0]);
            p.set("uLightViewProj1", light_vp[1]);
            p.set("uLightCount", static_cast<int>(lights.size()));
            for (std::size_t i = 0; i < lights.size(); ++i) {
                p.set(light_uniform(i, 0), lights[i].pos);
                p.set(light_uniform(i, 1), lights[i].dir);
                p.set(light_uniform(i, 2), lights[i].color);
                p.set(light_uniform(i, 3), lights[i].power);
                p.set(light_uniform(i, 4), std::cos(lights[i].inner));
                p.set(light_uniform(i, 5), std::cos(lights[i].outer));
            }
            p.set("uViewPos", cam.eye);
            p.set("uTime", static_cast<float>(time_));
            p.set("uShadowTexel", 1.0f / static_cast<float>(shadow_[0].size()),
                  1.0f / static_cast<float>(shadow_[0].size()));
            p.set("uShadowMap0", 1);
            p.set("uShadowMap1", 2);
            p.set("uTex", 0);
            p.set("uScreenUV", 0.0f);
            p.set("uViewport", static_cast<float>(target.width()),
                  static_cast<float>(target.height()));
        };

        scene_ = nullptr;
        for (const PlacedRoom& placed : rooms) {
            if (!placed.room) continue;
            set_frame(placed.pose);
            Spatial3D& room = *placed.room;

            // Each room in its own look: the annex seen through the doorway
            // keeps its own fog, whichever side you stand on.
            const Mix look = mix(room.id(), look_of(room));
            const gl::Program* program = program_for(look.shown(), passes::scene);
            if (program != scene_) {
                scene_ = program;
                scene_->use();
                frame_uniforms(*scene_);
            }
            apply_uniforms(*scene_, look, passes::scene);

            // This room's side of each doorway, from the portals as they are now.
            int bounds = 0;
            for (Key d : placed.doorways) {
                const Element* portal = room.find(d);
                if (!portal || bounds >= kMaxBounds) continue;
                const HalfSpace h = room_side(room, *portal, placed.pose);
                scene_->set(clip_uniform(bounds++), static_cast<float>(h.normal.x),
                            static_cast<float>(h.normal.y), static_cast<float>(h.normal.z),
                            static_cast<float>(h.offset));
            }
            scene_->set("uClipCount", bounds);

            draw_room(room);
            for (const auto& e : room.elements()) {
                if (!e.alive) continue;
                if (e.kind == kinds::mesh) {
                    draw_crate(room, e);
                } else if (e.kind == kinds::wall) {
                    draw_wall_element(room, e);
                } else if (e.kind == kinds::light) {
                    draw_lamp(room, e);
                }
            }
            for (const auto& e : room.elements()) {
                if (e.kind != kinds::portal || !e.alive) continue;
                if (!skip_portal.empty() && e.id == skip_portal) continue;  // looked through
                draw_portal(room, e, depth, target);
            }
        }
        for (int i = 0; i < kMaxBounds; ++i)
            gl::glDisable(gl::GL_CLIP_DISTANCE0 + static_cast<gl::GLenum>(i));
        set_frame(Pose{});
    }

    // A doorway (a portal bound to another room) has no solid frame in the
    // shadow pass: light should pass between the rooms.
    bool is_doorway(const Element& e) const {
        auto it = worlds_.find(e.id);
        return it != worlds_.end() && it->second.world != nullptr;
    }

    // Boxes sit on the floor: y is the base, not the centre. The pose comes from
    // world_pose, so an anchored element follows its group for free.
    RoomMatrix box_model(const State& st, const Element& e) const {
        const gl::Vec3 s{static_cast<float>(e.params.num(keys::sx, 1.0)),
                         static_cast<float>(e.params.num(keys::sy, 1.0)),
                         static_cast<float>(e.params.num(keys::sz, 1.0))};
        const Pose w = world_pose(st, e);
        const gl::Vec3 p{static_cast<float>(w.position.x),
                         static_cast<float>(w.position.y) + s.y * 0.5f,
                         static_cast<float>(w.position.z)};
        return room_local(gl::Mat4::translate(p) * gl::Mat4::rotate_y(static_cast<float>(w.yaw)) *
                          gl::Mat4::scale(s));
    }

    RoomMatrix portal_frame_model(const State& st, const Element& e) const {
        const Pose pose = world_pose(st, e);
        const float w = static_cast<float>(e.params.num(keys::w, 3.0));
        const float h = static_cast<float>(e.params.num(keys::h, 2.0));
        return room_local(gl::Mat4::translate(to_vec3(pose.position)) *
                          gl::Mat4::rotate_y(static_cast<float>(pose.yaw)) *
                          gl::Mat4::scale({0.08f, h + 0.22f, w + 0.22f}));
    }

    // `local` is in the room's own coordinates. The room's placement is applied
    // here, once, and the unplaced matrix goes to the shader as well so that
    // procedural surfaces stay put when the viewer changes rooms.
    void draw_solid(const RoomMatrix& local, const gl::Vec3& albedo, float roughness,
                    float surface, float emissive = 0.0f, float highlight = 0.0f) {
        set_model(local);
        scene_->set("uAlbedo", albedo);
        scene_->set("uRoughness", roughness);
        scene_->set("uSurface", surface);
        scene_->set("uEmissive", emissive);
        scene_->set("uHighlight", highlight);
        scene_->set("uTexMix", 0.0f);
        scene_->set("uGlow", 0.0f);
        cube_.draw();
    }

    static bool has_walls(const State& st) {
        for (const auto& e : st.elements())
            if (e.kind == kinds::wall && e.alive) return true;
        return false;
    }

    // A state that places its own wall elements gets only a floor and a
    // ceiling from its room_* parameters; one that does not gets the whole
    // implicit box, which is all a single-room scene needs.
    // The one place a room's placement is applied.
    void set_model(const RoomMatrix& local) {
        scene_->set("uModel", frame_matrix_ * local.m);
        scene_->set("uTexModel", local.m);
    }

    void draw_room(Spatial3D& world) {
        const float w = static_cast<float>(world.params().num(Key{"room_w"}, 14.0));
        const float d = static_cast<float>(world.params().num(Key{"room_d"}, 12.0));
        const float h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));
        const float t = 0.25f;
        const gl::Vec3 floor_c{static_cast<float>(world.params().num(Key{"floor_r"}, 0.42)),
                               static_cast<float>(world.params().num(Key{"floor_g"}, 0.39)),
                               static_cast<float>(world.params().num(Key{"floor_b"}, 0.36))};
        const gl::Vec3 wall_c{static_cast<float>(world.params().num(Key{"wall_r"}, 0.52)),
                              static_cast<float>(world.params().num(Key{"wall_g"}, 0.50)),
                              static_cast<float>(world.params().num(Key{"wall_b"}, 0.48))};

        draw_solid(room_local(gl::Mat4::translate({w / 2, -t / 2, d / 2}) *
                       gl::Mat4::scale({w, t, d})),
                   floor_c, 0.55f, 1.0f);
        draw_solid(room_local(gl::Mat4::translate({w / 2, h + t / 2, d / 2}) *
                       gl::Mat4::scale({w, t, d})),
                   wall_c * 0.5f, 0.95f, 2.0f);

        if (has_walls(world)) return;  // the state places its own walls

        draw_wall(room_local(gl::Mat4::translate({-t / 2, h / 2, d / 2}) * gl::Mat4::scale({t, h, d})),
                  wall_c);
        draw_wall(room_local(gl::Mat4::translate({w + t / 2, h / 2, d / 2}) *
                      gl::Mat4::scale({t, h, d})),
                  wall_c);
        draw_wall(room_local(gl::Mat4::translate({w / 2, h / 2, -t / 2}) * gl::Mat4::scale({w, h, t})),
                  wall_c);
        draw_wall(room_local(gl::Mat4::translate({w / 2, h / 2, d + t / 2}) *
                      gl::Mat4::scale({w, h, t})),
                  wall_c);

        // Skirting board: cheap, and it sells the scale.
        const float sh = 0.16f;
        const gl::Vec3 trim{0.20f, 0.18f, 0.17f};
        draw_solid(room_local(gl::Mat4::translate({w / 2, sh / 2, 0.06f}) *
                       gl::Mat4::scale({w, sh, 0.12f})),
                   trim, 0.7f, 0.0f);
        draw_solid(room_local(gl::Mat4::translate({w / 2, sh / 2, d - 0.06f}) *
                       gl::Mat4::scale({w, sh, 0.12f})),
                   trim, 0.7f, 0.0f);
        draw_solid(room_local(gl::Mat4::translate({0.06f, sh / 2, d / 2}) *
                       gl::Mat4::scale({0.12f, sh, d})),
                   trim, 0.7f, 0.0f);
        draw_solid(room_local(gl::Mat4::translate({w - 0.06f, sh / 2, d / 2}) *
                       gl::Mat4::scale({0.12f, sh, d})),
                   trim, 0.7f, 0.0f);
    }

    void draw_wall(const RoomMatrix& model, const gl::Vec3& color) {
        draw_solid(model, color, 0.9f, 2.0f);
    }

    // A wall element: level geometry placed by hand (or by an anchor), rather
    // than the implicit shell a plain room gets.
    void draw_wall_element(const State& st, const Element& e) {
        draw_wall(box_model(st, e), color_of(e, {0.52f, 0.50f, 0.48f}));
    }

    void draw_crate(const State& st, const Element& e) {
        draw_solid(box_model(st, e), color_of(e, {0.8f, 0.5f, 0.25f}),
                   static_cast<float>(e.params.num(Key{"roughness"}, 0.6)), 3.0f, 0.0f,
                   e.id == highlight_ ? 1.0f : 0.0f);
    }

    void draw_lamp(Spatial3D& world, const Element& e) {
        const gl::Vec3 pos = to_vec3(world_pose(world, e).position);
        const gl::Vec3 color = color_of(e, {1.0f, 0.93f, 0.82f});
        const float room_h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));

        draw_solid(room_local(gl::Mat4::translate({pos.x, pos.y + 0.12f, pos.z}) *
                       gl::Mat4::scale({0.62f, 0.22f, 0.62f})),
                   {0.12f, 0.11f, 0.10f}, 0.4f, 0.0f);
        draw_solid(room_local(gl::Mat4::translate(pos) * gl::Mat4::scale({0.30f, 0.16f, 0.30f})), color, 0.2f,
                   0.0f, 6.0f);
        const float stem = std::max(0.05f, room_h - pos.y - 0.2f);
        draw_solid(room_local(gl::Mat4::translate({pos.x, pos.y + 0.2f + stem * 0.5f, pos.z}) *
                       gl::Mat4::scale({0.035f, stem, 0.035f})),
                   {0.09f, 0.09f, 0.10f}, 0.8f, 0.0f);
    }

    void draw_portal(const State& st, const Element& e, int depth,
                     const gl::RenderTarget& target) {
        // A portal with nothing bound to it is a marker for a plain opening -
        // the gap between wall segments is the doorway, and it needs no
        // geometry of its own.
        if (!has_surface(e) && !is_doorway(e)) return;

        const Pose pose = world_pose(st, e);
        const gl::Vec3 pos = to_vec3(pose.position);
        const float w = static_cast<float>(e.params.num(keys::w, 3.0));
        const float h = static_cast<float>(e.params.num(keys::h, 2.0));
        const float yaw = static_cast<float>(pose.yaw);
        const bool open = e.params.get_or<bool>(keys::open, false);
        const gl::Vec3 n = to_vec3(heading(yaw));  // the domain decides what yaw means

        auto world_it = worlds_.find(e.id);
        const bool is_window = world_it != worlds_.end() && world_it->second.world != nullptr;

        const float hi = (open || e.id == highlight_) ? 1.0f : 0.0f;
        if (is_window) {
            // A doorway is cased on four sides, never backed: the opening has to
            // stay clear or there is nothing to see through.
            const gl::Vec3 casing{0.24f, 0.22f, 0.20f};
            const float t = 0.22f, d = 0.34f;
            const gl::Vec3 tangent = to_vec3(across(yaw));
            const gl::Mat4 rot = gl::Mat4::rotate_y(yaw);
            draw_solid(room_local(gl::Mat4::translate(pos + tangent * (w * 0.5f + t * 0.5f)) * rot *
                           gl::Mat4::scale({d, h + 2 * t, t})),
                       casing, 0.6f, 0.0f, 0.0f, hi);
            draw_solid(room_local(gl::Mat4::translate(pos - tangent * (w * 0.5f + t * 0.5f)) * rot *
                           gl::Mat4::scale({d, h + 2 * t, t})),
                       casing, 0.6f, 0.0f, 0.0f, hi);
            draw_solid(room_local(gl::Mat4::translate(pos + gl::Vec3{0, h * 0.5f + t * 0.5f, 0}) * rot *
                           gl::Mat4::scale({d, t, w + 2 * t})),
                       casing, 0.6f, 0.0f, 0.0f, hi);
            draw_solid(room_local(gl::Mat4::translate(pos - gl::Vec3{0, h * 0.5f + t * 0.5f, 0}) * rot *
                           gl::Mat4::scale({d, t, w + 2 * t})),
                       casing, 0.6f, 0.0f, 0.0f, hi);
        } else {
            // A panel hangs on the wall, so it keeps its backing frame.
            draw_solid(room_local(gl::Mat4::translate(pos) * gl::Mat4::rotate_y(yaw) *
                           gl::Mat4::scale({0.12f, h + 0.3f, w + 0.3f})),
                       {0.14f, 0.11f, 0.08f}, 0.6f, 0.0f, 0.0f, hi);
        }

        if (is_window) {
            WorldPortal& wp = world_it->second;
            if (depth > 0 || !wp.target.valid()) {
                // One level deep: a window seen through a window is just glass.
                set_model(room_local(gl::Mat4::translate(pos + n * 0.06f) * gl::Mat4::rotate_y(yaw) *
                          gl::Mat4::scale({1.0f, h, w})));
                scene_->set("uAlbedo", gl::Vec3{0.05f, 0.06f, 0.08f});
                scene_->set("uRoughness", 0.25f);
                scene_->set("uSurface", 0.0f);
                scene_->set("uEmissive", 0.0f);
                scene_->set("uHighlight", 0.0f);
                scene_->set("uTexMix", 0.0f);
                scene_->set("uGlow", 0.0f);
                quad_.draw();
                return;
            }
            // The far room, sampled in screen space: a hole in the wall.
            wp.target.bind_color(0);
            set_model(room_local(gl::Mat4::translate(pos + n * 0.06f) * gl::Mat4::rotate_y(yaw) *
                      gl::Mat4::scale({1.0f, h, w})));
            scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
            scene_->set("uRoughness", 1.0f);
            scene_->set("uSurface", 0.0f);
            scene_->set("uEmissive", 1.0f);  // the far room arrives already lit
            scene_->set("uHighlight", 0.0f);
            scene_->set("uTexMix", 1.0f);
            scene_->set("uGlow", 0.0f);
            scene_->set("uScreenUV", 1.0f);
            scene_->set("uViewport", static_cast<float>(target.width()),
                       static_cast<float>(target.height()));
            quad_.draw();
            scene_->set("uScreenUV", 0.0f);
            scene_->set("uTexMix", 0.0f);
            scene_->set("uEmissive", 0.0f);
            return;
        }

        auto it = surfaces_.find(e.id);
        if (it == surfaces_.end() || !it->second.surface) return;  // a plain opening
        BoundSurface& bound = it->second;
        Surface2D& surf = *bound.surface;

        const auto& pixels = surf.raster();
        if (!bound.texture.valid()) bound.texture.create(surf.px_w(), surf.px_h());
        if (bound.revision != surf.revision()) {
            bound.texture.upload(pixels);
            bound.revision = surf.revision();
        }
        bound.texture.bind(0);

        // Clear of the frame slab (half-thickness 0.06), or the panel sinks into it.
        set_model(room_local(gl::Mat4::translate(pos + n * 0.08f) * gl::Mat4::rotate_y(yaw) *
                  gl::Mat4::scale({1.0f, h, w})));
        scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
        scene_->set("uRoughness", 0.75f);
        scene_->set("uSurface", 0.0f);
        scene_->set("uEmissive", 0.0f);
        scene_->set("uHighlight", 0.0f);
        scene_->set("uTexMix", 1.0f);
        scene_->set("uGlow", open ? 0.55f : 0.12f);
        quad_.draw();
        scene_->set("uTexMix", 0.0f);
        scene_->set("uGlow", 0.0f);
    }

    void run_bloom() {
        gl::glDisable(gl::GL_DEPTH_TEST);
        bloom_a_.bind();
        gl::glClear(gl::GL_COLOR_BUFFER_BIT);
        const gl::Program& bright = *program_for(post_.shown(), passes::bright);
        bright.use();
        apply_uniforms(bright, post_, passes::bright);
        bright.set("uScene", 0);
        resolve_.bind_color(0);
        screen_.draw();

        const gl::Program& blur = *program_for(post_.shown(), passes::blur);
        blur.use();
        apply_uniforms(blur, post_, passes::blur);
        blur.set("uSource", 0);
        const float tx = 1.0f / static_cast<float>(bloom_a_.width());
        const float ty = 1.0f / static_cast<float>(bloom_a_.height());
        const long rounds = std::lround(setting(post_, passes::blur, "passes", q_.bloom_passes));
        for (long i = 0; i < rounds; ++i) {
            bloom_b_.bind();
            blur.set("uDirection", tx, 0.0f);
            bloom_a_.bind_color(0);
            screen_.draw();

            bloom_a_.bind();
            blur.set("uDirection", 0.0f, ty);
            bloom_b_.bind_color(0);
            screen_.draw();
        }
    }

    // The last pass, and the one a change of look is most visible in. Every
    // program in the blend on screen runs, and they are averaged by weight, so
    // a change of shader is as continuous as a change of number: it dissolves,
    // and a dissolve turned back half way dissolves back.
    void composite(int fb_w, int fb_h) {
        gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
        gl::glViewport(0, 0, fb_w, fb_h);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
        resolve_.bind_color(0);
        bloom_a_.bind_color(1);

        const auto draw = [&](const gl::Program& p) {
            p.use();
            apply_uniforms(p, post_, passes::composite);
            p.set("uScene", 0);
            p.set("uBloom", 1);
            p.set("uTime", static_cast<float>(time_));
            p.set("uTexel", 1.0f / static_cast<float>(fb_w), 1.0f / static_cast<float>(fb_h));
            screen_.draw();
        };
        // Looks that share a program share its draw.
        programs_in_mix_.clear();
        for (const Mix::Part& part : post_.parts) {
            const gl::Program* p = program_for(*part.look, passes::composite);
            auto it = std::find_if(programs_in_mix_.begin(), programs_in_mix_.end(),
                                   [p](const auto& e) { return e.first == p; });
            if (it == programs_in_mix_.end()) {
                programs_in_mix_.emplace_back(p, part.weight);
            } else {
                it->second += part.weight;
            }
        }
        // A running weighted mean: the k-th program is blended over the ones
        // before it with alpha = its weight / the weight drawn so far.
        float drawn = 0.0f;
        for (const auto& e : programs_in_mix_) {
            drawn += e.second;
            if (drawn == e.second) {
                draw(*e.first);
                continue;
            }
            gl::glEnable(gl::GL_BLEND);
            gl::glBlendColor(0.0f, 0.0f, 0.0f, e.second / drawn);
            gl::glBlendFunc(gl::GL_CONSTANT_ALPHA, gl::GL_ONE_MINUS_CONSTANT_ALPHA);
            draw(*e.first);
            gl::glDisable(gl::GL_BLEND);
        }
        gl::glEnable(gl::GL_DEPTH_TEST);
    }

    // --- looks ------------------------------------------------------------------
    // The fading itself is GL-free and lives with the looks (LookFader); what
    // is here is only what a GL renderer does with it.
    using Mix = LookMix;

    static Key view_key() { return Key{"<view>"}; }

    // Real time by default; a fixed step makes headless frames reproducible.
    void advance_clock() {
        const auto now = std::chrono::steady_clock::now();
        if (fixed_step_ > 0.0) {
            dt_ = fixed_step_;
        } else {
            dt_ = clock_started_
                      ? std::min(0.25, std::chrono::duration<double>(now - last_frame_).count())
                      : 0.0;
        }
        clock_started_ = true;
        last_frame_ = now;
        time_ += dt_;
        fader_.advance(dt_);
    }

    const LookState& look_of(const State& s) const { return fader_.look_of(graph_, s); }
    Mix mix(Key who, const LookState& target) { return fader_.mix(who, target); }

    double value(const LookState& l, Key pass, Key k, double fallback) const {
        return fader_.value(l, pass, k, fallback);
    }

    double setting(const Mix& m, Key pass, const char* name, double fallback) const {
        return fader_.value(m, pass, Key{name}, fallback);
    }

    // Every uniform the standard look and the looks in the blend give this
    // pass, weighted, and set. `name.x/.y/.z` components become a vector.
    void apply_uniforms(const gl::Program& p, const Mix& m, Key pass) {
        uniform_keys_.clear();
        const auto collect = [&](const LookState& l) {
            const Element* e = l.find(pass);
            if (!e) return;
            for (const auto& kv : e->params)
                if (is_uniform_key(kv.first) &&
                    std::find(uniform_keys_.begin(), uniform_keys_.end(), kv.first) ==
                        uniform_keys_.end())
                    uniform_keys_.push_back(kv.first);
        };
        collect(standard_);
        for (const Mix::Part& part : m.parts) collect(*part.look);
        vectors_.clear();
        for (Key k : uniform_keys_) {
            const std::string& name = k.str();
            const float v = static_cast<float>(fader_.value(m, pass, k, 0.0));
            const std::size_t dot = name.find('.');
            if (dot == std::string::npos) {
                p.set(name.c_str(), v);
                continue;
            }
            const std::string base = name.substr(0, dot);
            const char c = dot + 1 < name.size() ? name[dot + 1] : 'x';
            const int i = c == 'y' ? 1 : (c == 'z' ? 2 : 0);
            auto it = std::find_if(vectors_.begin(), vectors_.end(),
                                   [&](const VectorUniform& u) { return u.name == base; });
            if (it == vectors_.end()) {
                vectors_.push_back(VectorUniform{base, {0, 0, 0}, 0});
                it = vectors_.end() - 1;
            }
            it->v[i] = v;
            it->n = std::max(it->n, i + 1);
        }
        for (const VectorUniform& u : vectors_) {
            if (u.n == 3) {
                p.set(u.name.c_str(), gl::Vec3{u.v[0], u.v[1], u.v[2]});
            } else if (u.n == 2) {
                p.set(u.name.c_str(), u.v[0], u.v[1]);
            } else {
                p.set(u.name.c_str(), u.v[0]);
            }
        }
    }

    // The program for one pass of one look. Programs are shared by source, so
    // looks that only change numbers all use the built-in ones; a look's slot
    // remembers its source, so a shader edited while running is picked up.
    const gl::Program* program_for(const LookState& look, Key pass) {
        const std::string& vs = source(look, pass, look_keys::vs);
        const std::string& fs = source(look, pass, look_keys::fs);
        PassProgram& slot = resolved_[look.id()][pass];
        if (slot.program && slot.vs == vs && slot.fs == fs) return slot.program;
        slot.vs = vs;
        slot.fs = fs;
        slot.error.clear();
        slot.program = shared_program(vs, fs, look.id().str() + "." + pass.str(), slot.error);
        // A shader that will not build is reported by prepare(); the pass
        // falls back to the built-in rather than drawing nothing.
        if (!slot.program && &look != &standard_) slot.program = program_for(standard_, pass);
        return slot.program;
    }

    const std::string& source(const LookState& look, Key pass, Key which) const {
        if (const Element* e = look.find(pass))
            if (e->params.has(which))
                if (const auto* s = std::get_if<std::string>(&e->params.get(which)))
                    if (!s->empty()) return *s;
        const auto& b = builtin_.at(pass);
        return which == look_keys::vs ? b.first : b.second;
    }

    const gl::Program* shared_program(const std::string& vs, const std::string& fs,
                                      const std::string& tag, std::string& error) {
        std::string key;
        key.reserve(vs.size() + fs.size() + 1);
        key += vs;
        key += '\0';
        key += fs;
        auto it = programs_.find(key);
        if (it != programs_.end()) return it->second.get();
        const auto t0 = std::chrono::steady_clock::now();
        std::unique_ptr<gl::Program> p;
        try {
            p = std::make_unique<gl::Program>(vs.c_str(), fs.c_str(), tag.c_str());
        } catch (const std::exception& e) {
            error = e.what();
        }
        stats_.compile_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                .count();
        if (!p) return nullptr;
        ++stats_.programs;
        if (!preparing_) ++stats_.late;
        return programs_.emplace(std::move(key), std::move(p)).first->second.get();
    }

    // Compile a look's passes and hold them to what this renderer feeds them.
    void check_look(const LookState& look, std::vector<std::string>& out) {
        static const std::unordered_map<Key, std::vector<const char*>> required{
            {passes::shadow, {"uModel", "uLightViewProj"}},
            // Without the clip planes two glued rooms both draw their shared wall.
            {passes::scene, {"uModel", "uViewProj", "uClipCount"}},
            {passes::bright, {"uScene"}},
            {passes::blur, {"uSource", "uDirection"}},
            {passes::composite, {"uScene"}},
        };
        const std::string tag = "look " + look.id().str() + ": ";
        for (Key pass : passes::all()) {
            const gl::Program* p = program_for(look, pass);
            const PassProgram& slot = resolved_[look.id()][pass];
            if (!slot.error.empty()) {
                const std::string& e = slot.error;
                out.push_back(tag + pass.str() + " shader does not build, so the built-in is used - " +
                              e.substr(0, e.find('\n')));
                continue;
            }
            const Element* el = look.find(pass);
            const bool custom = el && (el->params.has(look_keys::vs) || el->params.has(look_keys::fs));
            if (custom)
                for (const char* name : required.at(pass))
                    if (!p->has(name))
                        out.push_back(tag + "its " + pass.str() + " shader has no " + name +
                                      ", which the renderer sets on every draw");
            if (!el) continue;
            std::vector<std::string> named;  // a vector's three components are one name
            for (const auto& kv : el->params) {
                if (!is_uniform_key(kv.first)) continue;
                const std::string& name = kv.first.str();
                const std::string base = name.substr(0, name.find('.'));
                if (std::find(named.begin(), named.end(), base) != named.end()) continue;
                named.push_back(base);
                if (!p->has(base.c_str()))
                    out.push_back(tag + pass.str() + " sets " + base +
                                  ", which its shader does not have (misspelt, or declared "
                                  "and never used)");
            }
        }
    }

    static const char* clip_uniform(int i) {
        static const auto names = [] {
            std::array<std::string, kMaxBounds> n;
            for (int b = 0; b < kMaxBounds; ++b)
                n[static_cast<std::size_t>(b)] = "uClip[" + std::to_string(b) + "]";
            return n;
        }();
        return names[static_cast<std::size_t>(i)].c_str();
    }

    // Light uniform names, built once: they are asked for every frame.
    static const char* light_uniform(std::size_t i, int field) {
        static const auto names = [] {
            static const char* fields[] = {"uLightPos",   "uLightDir", "uLightColor",
                                           "uLightPower", "uCosInner", "uCosOuter"};
            std::array<std::array<std::string, 6>, kMaxLights> n;
            for (std::size_t l = 0; l < kMaxLights; ++l)
                for (int f = 0; f < 6; ++f)
                    n[l][static_cast<std::size_t>(f)] =
                        std::string(fields[f]) + "[" + std::to_string(l) + "]";
            return n;
        }();
        return names[i][static_cast<std::size_t>(field)].c_str();
    }

    struct PassProgram {
        std::string vs, fs;
        const gl::Program* program = nullptr;
        std::string error;
    };

    struct VectorUniform {
        std::string name;
        float v[3];
        int n;
    };

    GLQuality q_;
    bool ready_ = false;
    int target_w_ = 0, target_h_ = 0;

    // Looks, and the programs they compile to.
    const StateGraph* graph_ = nullptr;
    LookState standard_;
    std::unordered_map<Key, std::pair<std::string, std::string>> builtin_;
    std::unordered_map<std::string, std::unique_ptr<gl::Program>> programs_;
    std::unordered_map<Key, std::unordered_map<Key, PassProgram>> resolved_;
    LookFader fader_{standard_};
    Mix post_{{{&standard_, 1.0f}}};
    std::vector<std::pair<const gl::Program*, float>> programs_in_mix_;
    const gl::Program* scene_ = nullptr;  // the scene program currently bound
    std::vector<Key> uniform_keys_;
    std::vector<VectorUniform> vectors_;
    LookStats stats_;
    bool preparing_ = false;
    std::chrono::steady_clock::time_point last_frame_{};
    bool clock_started_ = false;
    double dt_ = 0.0, time_ = 0.0;
    double fixed_step_ = 0.0;

    gl::Mesh cube_, quad_;
    gl::FullscreenTriangle screen_;
    gl::ShadowMap shadow_[kShadowMaps];
    gl::RenderTarget scene_target_, resolve_, bloom_a_, bloom_b_;

    Pose frame_;              // the placement of the room currently being drawn
    gl::Mat4 frame_matrix_;   // the same thing, ready to multiply
    std::unordered_map<Key, BoundSurface> surfaces_;
    std::unordered_map<Key, WorldPortal> worlds_;
    Key highlight_;
};

}  // namespace sg::render
