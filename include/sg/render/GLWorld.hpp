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
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/domains/Spatial.hpp"
#include "sg/domains/Surface.hpp"
#include "sg/gl/Renderer.hpp"
#include "sg/gl/Shaders.hpp"

namespace sg::render {

struct GLQuality {
    int shadow_size = 2048;
    int msaa = 4;
    float bloom_strength = 0.55f;
    float bloom_threshold = 1.05f;
    float exposure = 1.15f;
    int bloom_passes = 3;  // horizontal+vertical pairs
};

class GLWorldView {
public:
    static constexpr std::size_t kMaxLights = 4;    // matches the scene shader
    static constexpr std::size_t kShadowMaps = 2;  // the nearest two cast

    explicit GLWorldView(GLQuality q = {}) : q_(q) {}

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
        const PlacedRoom one{&world, Pose{}};
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

        Spatial3D& world = *rooms.front().room;
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
            const PlacedRoom guest{wp.world, Pose{}};
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

    // The pose of an element in the frame currently being drawn: its place
    // inside its room (following any anchor), then that room's placement.
    Pose placed_pose(const State& st, const Element& e) const {
        return compose_pose(frame_, world_pose(st, e));
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
        scene_ = gl::Program(gl::scene_vs(), gl::scene_fs(), "scene");
        depth_ = gl::Program(gl::depth_vs(), gl::depth_fs(), "depth");
        bright_ = gl::Program(gl::post_vs(), gl::bright_fs(), "bright");
        blur_ = gl::Program(gl::post_vs(), gl::blur_fs(), "blur");
        composite_ = gl::Program(gl::post_vs(), gl::composite_fs(), "composite");
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
        depth_.use();
        for (std::size_t i = 0; i < shadowed; ++i) {
            shadow_[i].bind();
            gl::glClear(gl::GL_DEPTH_BUFFER_BIT);
            depth_.set("uLightViewProj", light_vp[i]);
            for (const PlacedRoom& placed : rooms) {
                if (!placed.room) continue;
                frame_ = placed.pose;
                for (const auto& e : placed.room->elements()) {
                    if (!e.alive) continue;
                    if (e.kind == kinds::mesh || e.kind == kinds::wall) {
                        depth_.set("uModel", box_model(*placed.room, e));
                        cube_.draw();
                    } else if (e.kind == kinds::portal && !is_doorway(e) && has_surface(e)) {
                        depth_.set("uModel", portal_frame_model(*placed.room, e));
                        cube_.draw();
                    }
                }
            }
        }
        gl::glCullFace(gl::GL_BACK);

        // --- the scene -------------------------------------------------------
        target.bind();
        gl::glClearColor(0.012f, 0.014f, 0.022f, 1.0f);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
        gl::glEnable(gl::GL_DEPTH_TEST);
        gl::glDepthFunc(gl::GL_LESS);
        gl::glEnable(gl::GL_MULTISAMPLE);
        gl::glDisable(gl::GL_CULL_FACE);

        scene_.use();
        scene_.set("uViewProj", view_proj);
        scene_.set("uLightViewProj0", light_vp[0]);
        scene_.set("uLightViewProj1", light_vp[1]);
        scene_.set("uLightCount", static_cast<int>(lights.size()));
        for (std::size_t i = 0; i < lights.size(); ++i) {
            const std::string ix = "[" + std::to_string(i) + "]";
            scene_.set(("uLightPos" + ix).c_str(), lights[i].pos);
            scene_.set(("uLightDir" + ix).c_str(), lights[i].dir);
            scene_.set(("uLightColor" + ix).c_str(), lights[i].color);
            scene_.set(("uLightPower" + ix).c_str(), lights[i].power);
            scene_.set(("uCosInner" + ix).c_str(), std::cos(lights[i].inner));
            scene_.set(("uCosOuter" + ix).c_str(), std::cos(lights[i].outer));
        }
        scene_.set("uViewPos", cam.eye);
        scene_.set("uFogColor", gl::Vec3{0.05f, 0.06f, 0.09f});
        scene_.set("uFogDensity", 0.018f);
        scene_.set("uShadowTexel", 1.0f / static_cast<float>(shadow_[0].size()),
                   1.0f / static_cast<float>(shadow_[0].size()));
        scene_.set("uShadowMap0", 1);
        scene_.set("uShadowMap1", 2);
        scene_.set("uTex", 0);
        scene_.set("uScreenUV", 0.0f);
        scene_.set("uViewport", static_cast<float>(target.width()),
                   static_cast<float>(target.height()));
        shadow_[0].bind_depth(1);
        shadow_[1].bind_depth(2);

        for (const PlacedRoom& placed : rooms) {
            if (!placed.room) continue;
            frame_ = placed.pose;
            Spatial3D& room = *placed.room;

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
        frame_ = Pose{};
    }

    // A doorway (a portal bound to another room) has no solid frame in the
    // shadow pass: light should pass between the rooms.
    bool is_doorway(const Element& e) const {
        auto it = worlds_.find(e.id);
        return it != worlds_.end() && it->second.world != nullptr;
    }

    // Boxes sit on the floor: y is the base, not the centre. The pose comes from
    // world_pose, so an anchored element follows its group for free.
    gl::Mat4 box_model(const State& st, const Element& e) const {
        const gl::Vec3 s{static_cast<float>(e.params.num(keys::sx, 1.0)),
                         static_cast<float>(e.params.num(keys::sy, 1.0)),
                         static_cast<float>(e.params.num(keys::sz, 1.0))};
        const Pose w = placed_pose(st, e);
        const gl::Vec3 p{static_cast<float>(w.position.x),
                         static_cast<float>(w.position.y) + s.y * 0.5f,
                         static_cast<float>(w.position.z)};
        return gl::Mat4::translate(p) * gl::Mat4::rotate_y(static_cast<float>(w.yaw)) *
               gl::Mat4::scale(s);
    }

    gl::Mat4 portal_frame_model(const State& st, const Element& e) const {
        const Pose pose = placed_pose(st, e);
        const float w = static_cast<float>(e.params.num(keys::w, 3.0));
        const float h = static_cast<float>(e.params.num(keys::h, 2.0));
        return gl::Mat4::translate(to_vec3(pose.position)) *
               gl::Mat4::rotate_y(static_cast<float>(pose.yaw)) *
               gl::Mat4::scale({0.08f, h + 0.22f, w + 0.22f});
    }

    void draw_solid(const gl::Mat4& model, const gl::Vec3& albedo, float roughness, float surface,
                    float emissive = 0.0f, float highlight = 0.0f) {
        scene_.set("uModel", model);
        scene_.set("uAlbedo", albedo);
        scene_.set("uRoughness", roughness);
        scene_.set("uSurface", surface);
        scene_.set("uEmissive", emissive);
        scene_.set("uHighlight", highlight);
        scene_.set("uTexMix", 0.0f);
        scene_.set("uGlow", 0.0f);
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

        const gl::Mat4 room_frame =
            gl::Mat4::translate({static_cast<float>(frame_.position.x),
                                 static_cast<float>(frame_.position.y),
                                 static_cast<float>(frame_.position.z)}) *
            gl::Mat4::rotate_y(static_cast<float>(frame_.yaw));

        draw_solid(room_frame * gl::Mat4::translate({w / 2, -t / 2, d / 2}) *
                       gl::Mat4::scale({w, t, d}),
                   floor_c, 0.55f, 1.0f);
        draw_solid(room_frame * gl::Mat4::translate({w / 2, h + t / 2, d / 2}) *
                       gl::Mat4::scale({w, t, d}),
                   wall_c * 0.5f, 0.95f, 2.0f);

        if (has_walls(world)) return;  // the state places its own walls

        draw_wall(room_frame * gl::Mat4::translate({-t / 2, h / 2, d / 2}) * gl::Mat4::scale({t, h, d}),
                  wall_c);
        draw_wall(room_frame * gl::Mat4::translate({w + t / 2, h / 2, d / 2}) *
                      gl::Mat4::scale({t, h, d}),
                  wall_c);
        draw_wall(room_frame * gl::Mat4::translate({w / 2, h / 2, -t / 2}) * gl::Mat4::scale({w, h, t}),
                  wall_c);
        draw_wall(room_frame * gl::Mat4::translate({w / 2, h / 2, d + t / 2}) *
                      gl::Mat4::scale({w, h, t}),
                  wall_c);

        // Skirting board: cheap, and it sells the scale.
        const float sh = 0.16f;
        const gl::Vec3 trim{0.20f, 0.18f, 0.17f};
        draw_solid(room_frame * gl::Mat4::translate({w / 2, sh / 2, 0.06f}) *
                       gl::Mat4::scale({w, sh, 0.12f}),
                   trim, 0.7f, 0.0f);
        draw_solid(room_frame * gl::Mat4::translate({w / 2, sh / 2, d - 0.06f}) *
                       gl::Mat4::scale({w, sh, 0.12f}),
                   trim, 0.7f, 0.0f);
        draw_solid(room_frame * gl::Mat4::translate({0.06f, sh / 2, d / 2}) *
                       gl::Mat4::scale({0.12f, sh, d}),
                   trim, 0.7f, 0.0f);
        draw_solid(room_frame * gl::Mat4::translate({w - 0.06f, sh / 2, d / 2}) *
                       gl::Mat4::scale({0.12f, sh, d}),
                   trim, 0.7f, 0.0f);
    }

    void draw_wall(const gl::Mat4& model, const gl::Vec3& color) {
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
        const gl::Vec3 pos = to_vec3(placed_pose(world, e).position);
        const gl::Vec3 color = color_of(e, {1.0f, 0.93f, 0.82f});
        const float room_h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));

        draw_solid(gl::Mat4::translate({pos.x, pos.y + 0.12f, pos.z}) *
                       gl::Mat4::scale({0.62f, 0.22f, 0.62f}),
                   {0.12f, 0.11f, 0.10f}, 0.4f, 0.0f);
        draw_solid(gl::Mat4::translate(pos) * gl::Mat4::scale({0.30f, 0.16f, 0.30f}), color, 0.2f,
                   0.0f, 6.0f);
        const float stem = std::max(0.05f, room_h - pos.y - 0.2f);
        draw_solid(gl::Mat4::translate({pos.x, pos.y + 0.2f + stem * 0.5f, pos.z}) *
                       gl::Mat4::scale({0.035f, stem, 0.035f}),
                   {0.09f, 0.09f, 0.10f}, 0.8f, 0.0f);
    }

    void draw_portal(const State& st, const Element& e, int depth,
                     const gl::RenderTarget& target) {
        // A portal with nothing bound to it is a marker for a plain opening -
        // the gap between wall segments is the doorway, and it needs no
        // geometry of its own.
        if (!has_surface(e) && !is_doorway(e)) return;

        const Pose pose = placed_pose(st, e);
        const gl::Vec3 pos = to_vec3(pose.position);
        const float w = static_cast<float>(e.params.num(keys::w, 3.0));
        const float h = static_cast<float>(e.params.num(keys::h, 2.0));
        const float yaw = static_cast<float>(pose.yaw);
        const bool open = e.params.get_or<bool>(keys::open, false);
        const gl::Vec3 n{std::cos(yaw), 0.0f, std::sin(yaw)};  // the way it faces

        auto world_it = worlds_.find(e.id);
        const bool is_window = world_it != worlds_.end() && world_it->second.world != nullptr;

        const float hi = (open || e.id == highlight_) ? 1.0f : 0.0f;
        if (is_window) {
            // A doorway is cased on four sides, never backed: the opening has to
            // stay clear or there is nothing to see through.
            const gl::Vec3 casing{0.24f, 0.22f, 0.20f};
            const float t = 0.22f, d = 0.34f;
            const gl::Vec3 tangent{-std::sin(yaw), 0.0f, std::cos(yaw)};
            const gl::Mat4 rot = gl::Mat4::rotate_y(yaw);
            draw_solid(gl::Mat4::translate(pos + tangent * (w * 0.5f + t * 0.5f)) * rot *
                           gl::Mat4::scale({d, h + 2 * t, t}),
                       casing, 0.6f, 0.0f, 0.0f, hi);
            draw_solid(gl::Mat4::translate(pos - tangent * (w * 0.5f + t * 0.5f)) * rot *
                           gl::Mat4::scale({d, h + 2 * t, t}),
                       casing, 0.6f, 0.0f, 0.0f, hi);
            draw_solid(gl::Mat4::translate(pos + gl::Vec3{0, h * 0.5f + t * 0.5f, 0}) * rot *
                           gl::Mat4::scale({d, t, w + 2 * t}),
                       casing, 0.6f, 0.0f, 0.0f, hi);
            draw_solid(gl::Mat4::translate(pos - gl::Vec3{0, h * 0.5f + t * 0.5f, 0}) * rot *
                           gl::Mat4::scale({d, t, w + 2 * t}),
                       casing, 0.6f, 0.0f, 0.0f, hi);
        } else {
            // A panel hangs on the wall, so it keeps its backing frame.
            draw_solid(gl::Mat4::translate(pos) * gl::Mat4::rotate_y(yaw) *
                           gl::Mat4::scale({0.12f, h + 0.3f, w + 0.3f}),
                       {0.14f, 0.11f, 0.08f}, 0.6f, 0.0f, 0.0f, hi);
        }

        if (is_window) {
            WorldPortal& wp = world_it->second;
            if (depth > 0 || !wp.target.valid()) {
                // One level deep: a window seen through a window is just glass.
                scene_.set("uModel", gl::Mat4::translate(pos + n * 0.06f) *
                                         gl::Mat4::rotate_y(yaw) * gl::Mat4::scale({1.0f, h, w}));
                scene_.set("uAlbedo", gl::Vec3{0.05f, 0.06f, 0.08f});
                scene_.set("uRoughness", 0.25f);
                scene_.set("uSurface", 0.0f);
                scene_.set("uEmissive", 0.0f);
                scene_.set("uHighlight", 0.0f);
                scene_.set("uTexMix", 0.0f);
                scene_.set("uGlow", 0.0f);
                quad_.draw();
                return;
            }
            // The far room, sampled in screen space: a hole in the wall.
            wp.target.bind_color(0);
            scene_.set("uModel", gl::Mat4::translate(pos + n * 0.06f) * gl::Mat4::rotate_y(yaw) *
                                     gl::Mat4::scale({1.0f, h, w}));
            scene_.set("uAlbedo", gl::Vec3{1, 1, 1});
            scene_.set("uRoughness", 1.0f);
            scene_.set("uSurface", 0.0f);
            scene_.set("uEmissive", 1.0f);  // the far room arrives already lit
            scene_.set("uHighlight", 0.0f);
            scene_.set("uTexMix", 1.0f);
            scene_.set("uGlow", 0.0f);
            scene_.set("uScreenUV", 1.0f);
            scene_.set("uViewport", static_cast<float>(target.width()),
                       static_cast<float>(target.height()));
            quad_.draw();
            scene_.set("uScreenUV", 0.0f);
            scene_.set("uTexMix", 0.0f);
            scene_.set("uEmissive", 0.0f);
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
        scene_.set("uModel", gl::Mat4::translate(pos + n * 0.08f) * gl::Mat4::rotate_y(yaw) *
                                 gl::Mat4::scale({1.0f, h, w}));
        scene_.set("uAlbedo", gl::Vec3{1, 1, 1});
        scene_.set("uRoughness", 0.75f);
        scene_.set("uSurface", 0.0f);
        scene_.set("uEmissive", 0.0f);
        scene_.set("uHighlight", 0.0f);
        scene_.set("uTexMix", 1.0f);
        scene_.set("uGlow", open ? 0.55f : 0.12f);
        quad_.draw();
        scene_.set("uTexMix", 0.0f);
        scene_.set("uGlow", 0.0f);
    }

    void run_bloom() {
        gl::glDisable(gl::GL_DEPTH_TEST);
        bloom_a_.bind();
        gl::glClear(gl::GL_COLOR_BUFFER_BIT);
        bright_.use();
        bright_.set("uScene", 0);
        bright_.set("uThreshold", q_.bloom_threshold);
        resolve_.bind_color(0);
        screen_.draw();

        blur_.use();
        blur_.set("uSource", 0);
        const float tx = 1.0f / static_cast<float>(bloom_a_.width());
        const float ty = 1.0f / static_cast<float>(bloom_a_.height());
        for (int i = 0; i < q_.bloom_passes; ++i) {
            bloom_b_.bind();
            blur_.set("uDirection", tx, 0.0f);
            bloom_a_.bind_color(0);
            screen_.draw();

            bloom_a_.bind();
            blur_.set("uDirection", 0.0f, ty);
            bloom_b_.bind_color(0);
            screen_.draw();
        }
    }

    void composite(int fb_w, int fb_h) {
        gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
        gl::glViewport(0, 0, fb_w, fb_h);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
        composite_.use();
        composite_.set("uScene", 0);
        composite_.set("uBloom", 1);
        composite_.set("uBloomStrength", q_.bloom_strength);
        composite_.set("uExposure", q_.exposure);
        composite_.set("uTexel", 1.0f / static_cast<float>(fb_w),
                       1.0f / static_cast<float>(fb_h));
        resolve_.bind_color(0);
        bloom_a_.bind_color(1);
        screen_.draw();
        gl::glEnable(gl::GL_DEPTH_TEST);
    }

    GLQuality q_;
    bool ready_ = false;
    int target_w_ = 0, target_h_ = 0;

    gl::Program scene_, depth_, bright_, blur_, composite_;
    gl::Mesh cube_, quad_;
    gl::FullscreenTriangle screen_;
    gl::ShadowMap shadow_[kShadowMaps];
    gl::RenderTarget scene_target_, resolve_, bloom_a_, bloom_b_;

    Pose frame_;  // the placement of the room currently being drawn
    std::unordered_map<Key, BoundSurface> surfaces_;
    std::unordered_map<Key, WorldPortal> worlds_;
    Key highlight_;
};

}  // namespace sg::render
