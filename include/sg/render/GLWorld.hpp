// Stategine - the OpenGL view of a 3D spatial state.
//
// A view, not a state: it reads elements and draws them. Swapping it for the
// terminal view (or running both) changes nothing about the domain, and any
// 3D state gets this renderer for free as long as it uses the shared parameter
// vocabulary (x/y/z, sx/sy/sz, r/g/b, w/h/yaw).
//
// Frame: shadow depth from the lamp -> scene into a multisampled HDR target ->
// resolve -> bright pass -> separable blur -> ACES composite with bloom.
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
    explicit GLWorldView(GLQuality q = {}) : q_(q) {}

    // Attach a 2D state to a portal element: its raster becomes the texture.
    void bind_surface(Key portal_element, Surface2D* surface) {
        surfaces_[portal_element] = Bound{surface, gl::Texture{}, 0};
    }

    // Draw a highlight on one element for this frame (a "you can use this" cue).
    void highlight(Key element_id) { highlight_ = element_id; }

    // The whole frame. `fb_w/fb_h` are the framebuffer's pixel dimensions.
    void render(Spatial3D& world, int fb_w, int fb_h) {
        if (fb_w <= 0 || fb_h <= 0) return;
        ensure_resources();
        ensure_targets(fb_w, fb_h);

        const Element& cam = world.camera();
        const gl::Vec3 eye = to_vec3(position_of(cam));
        const gl::Vec3 fwd = to_vec3(forward_of(cam));
        const float fov = static_cast<float>(cam.params.num(keys::fov, 70.0)) * 3.14159265f /
                          180.0f;
        const float aspect = static_cast<float>(fb_w) / static_cast<float>(fb_h);
        const gl::Mat4 view_proj =
            gl::Mat4::perspective(fov, aspect, 0.05f, 120.0f) * gl::Mat4::look_at(eye, eye + fwd,
                                                                                 {0, 1, 0});

        // --- the lamp, as a spot light ---------------------------------------
        Light light = read_light(world);
        const gl::Mat4 light_vp =
            gl::Mat4::perspective(light.outer * 2.05f, 1.0f, 0.35f, 60.0f) *
            gl::Mat4::look_at(light.pos, light.pos + light.dir, {0, 0, 1});

        // --- pass 1: shadow depth ---------------------------------------------
        shadow_.bind();
        gl::glClear(gl::GL_DEPTH_BUFFER_BIT);
        gl::glEnable(gl::GL_DEPTH_TEST);
        gl::glEnable(gl::GL_CULL_FACE);
        gl::glCullFace(gl::GL_FRONT);  // front-face culling hides most acne
        depth_.use();
        depth_.set("uLightViewProj", light_vp);
        for (const auto& e : world.elements()) {
            if (!e.alive) continue;
            if (e.kind == kinds::mesh) {
                depth_.set("uModel", box_model(e));
                cube_.draw();
            } else if (e.kind == kinds::portal) {
                depth_.set("uModel", portal_frame_model(e));
                cube_.draw();
            }
        }
        gl::glCullFace(gl::GL_BACK);

        // --- pass 2: the scene, into HDR --------------------------------------
        scene_target_.bind();
        gl::glClearColor(0.012f, 0.014f, 0.022f, 1.0f);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
        gl::glEnable(gl::GL_DEPTH_TEST);
        gl::glDepthFunc(gl::GL_LESS);
        gl::glEnable(gl::GL_MULTISAMPLE);
        gl::glDisable(gl::GL_CULL_FACE);

        scene_.use();
        scene_.set("uViewProj", view_proj);
        scene_.set("uLightViewProj", light_vp);
        scene_.set("uLightPos", light.pos);
        scene_.set("uLightDir", light.dir);
        scene_.set("uLightColor", light.color);
        scene_.set("uLightPower", light.power);
        scene_.set("uCosInner", std::cos(light.inner));
        scene_.set("uCosOuter", std::cos(light.outer));
        scene_.set("uViewPos", eye);
        scene_.set("uFogColor", gl::Vec3{0.05f, 0.06f, 0.09f});
        scene_.set("uFogDensity", 0.018f);
        scene_.set("uShadowTexel", 1.0f / static_cast<float>(shadow_.size()),
                   1.0f / static_cast<float>(shadow_.size()));
        scene_.set("uShadowMap", 1);
        scene_.set("uTex", 0);
        shadow_.bind_depth(1);

        draw_room(world);
        for (const auto& e : world.elements()) {
            if (!e.alive) continue;
            if (e.kind == kinds::mesh) {
                draw_crate(e);
            } else if (e.kind == kinds::light) {
                draw_lamp(world, e);
            }
        }
        for (const auto& e : world.elements())
            if (e.kind == kinds::portal && e.alive) draw_portal(e);

        // --- post -------------------------------------------------------------
        scene_target_.blit_to(resolve_);
        run_bloom();
        composite(fb_w, fb_h);
        highlight_ = Key{};
    }

private:
    struct Light {
        gl::Vec3 pos{0, 3, 0};
        gl::Vec3 dir{0, -1, 0};
        gl::Vec3 color{1.0f, 0.93f, 0.82f};
        float power = 26.0f;
        float inner = 0.55f;  // radians
        float outer = 1.15f;
    };

    struct Bound {
        Surface2D* surface = nullptr;
        gl::Texture texture;
        uint64_t revision = 0;
    };

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
        shadow_.create(q_.shadow_size);
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

    Light read_light(Spatial3D& world) {
        Light l;
        for (const auto& e : world.elements()) {
            if (e.kind != kinds::light || !e.alive) continue;
            l.pos = to_vec3(position_of(e));
            l.color = color_of(e, l.color);
            l.power = static_cast<float>(e.params.num(keys::intensity, 1.0)) * 26.0f;
            l.dir = gl::normalize({static_cast<float>(e.params.num(Key{"dx"}, 0.0)),
                                   static_cast<float>(e.params.num(Key{"dy"}, -1.0)),
                                   static_cast<float>(e.params.num(Key{"dz"}, 0.0))});
            l.inner = static_cast<float>(e.params.num(Key{"inner"}, 0.55));
            l.outer = static_cast<float>(e.params.num(Key{"outer"}, 1.15));
            break;
        }
        return l;
    }

    gl::Mat4 box_model(const Element& e) const {
        const gl::Vec3 s{static_cast<float>(e.params.num(keys::sx, 1.0)),
                         static_cast<float>(e.params.num(keys::sy, 1.0)),
                         static_cast<float>(e.params.num(keys::sz, 1.0))};
        const gl::Vec3 p{static_cast<float>(e.params.num(keys::x)),
                         static_cast<float>(e.params.num(keys::y)) + s.y * 0.5f,
                         static_cast<float>(e.params.num(keys::z))};
        return gl::Mat4::translate(p) *
               gl::Mat4::rotate_y(static_cast<float>(e.params.num(keys::yaw))) *
               gl::Mat4::scale(s);
    }

    gl::Mat4 portal_frame_model(const Element& e) const {
        const gl::Vec3 p = to_vec3(position_of(e));
        const float w = static_cast<float>(e.params.num(keys::w, 3.0));
        const float h = static_cast<float>(e.params.num(keys::h, 2.0));
        return gl::Mat4::translate(p) *
               gl::Mat4::rotate_y(static_cast<float>(e.params.num(keys::yaw))) *
               gl::Mat4::scale({w + 0.22f, h + 0.22f, 0.08f});
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

    void draw_room(Spatial3D& world) {
        const float w = static_cast<float>(world.params().num(Key{"room_w"}, 14.0));
        const float d = static_cast<float>(world.params().num(Key{"room_d"}, 12.0));
        const float h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));
        const float t = 0.25f;

        draw_solid(gl::Mat4::translate({w / 2, -t / 2, d / 2}) * gl::Mat4::scale({w, t, d}),
                   {0.42f, 0.39f, 0.36f}, 0.55f, 1.0f);
        draw_solid(gl::Mat4::translate({w / 2, h + t / 2, d / 2}) * gl::Mat4::scale({w, t, d}),
                   {0.26f, 0.26f, 0.29f}, 0.95f, 2.0f);
        draw_solid(gl::Mat4::translate({-t / 2, h / 2, d / 2}) * gl::Mat4::scale({t, h, d}),
                   {0.52f, 0.50f, 0.48f}, 0.9f, 2.0f);
        draw_solid(gl::Mat4::translate({w + t / 2, h / 2, d / 2}) * gl::Mat4::scale({t, h, d}),
                   {0.52f, 0.50f, 0.48f}, 0.9f, 2.0f);
        draw_solid(gl::Mat4::translate({w / 2, h / 2, -t / 2}) * gl::Mat4::scale({w, h, t}),
                   {0.50f, 0.48f, 0.47f}, 0.9f, 2.0f);
        draw_solid(gl::Mat4::translate({w / 2, h / 2, d + t / 2}) * gl::Mat4::scale({w, h, t}),
                   {0.50f, 0.48f, 0.47f}, 0.9f, 2.0f);

        // A skirting board around the floor: cheap, and it sells the scale.
        const float sh = 0.16f;
        const gl::Vec3 trim{0.20f, 0.18f, 0.17f};
        draw_solid(gl::Mat4::translate({w / 2, sh / 2, 0.06f}) * gl::Mat4::scale({w, sh, 0.12f}),
                   trim, 0.7f, 0.0f);
        draw_solid(gl::Mat4::translate({w / 2, sh / 2, d - 0.06f}) *
                       gl::Mat4::scale({w, sh, 0.12f}),
                   trim, 0.7f, 0.0f);
        draw_solid(gl::Mat4::translate({0.06f, sh / 2, d / 2}) * gl::Mat4::scale({0.12f, sh, d}),
                   trim, 0.7f, 0.0f);
        draw_solid(gl::Mat4::translate({w - 0.06f, sh / 2, d / 2}) *
                       gl::Mat4::scale({0.12f, sh, d}),
                   trim, 0.7f, 0.0f);
    }

    void draw_crate(const Element& e) {
        draw_solid(box_model(e), color_of(e, {0.8f, 0.5f, 0.25f}),
                   static_cast<float>(e.params.num(Key{"roughness"}, 0.6)), 3.0f, 0.0f,
                   e.id == highlight_ ? 1.0f : 0.0f);
    }

    void draw_lamp(Spatial3D& world, const Element& e) {
        const gl::Vec3 pos = to_vec3(position_of(e));
        const gl::Vec3 color = color_of(e, {1.0f, 0.93f, 0.82f});
        const float room_h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));

        // Shade, bulb, and the flex up to the ceiling.
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

    void draw_portal(const Element& e) {
        auto it = surfaces_.find(e.id);
        if (it == surfaces_.end() || !it->second.surface) return;
        Bound& bound = it->second;
        Surface2D& surf = *bound.surface;

        const gl::Vec3 pos = to_vec3(position_of(e));
        const float w = static_cast<float>(e.params.num(keys::w, 3.0));
        const float h = static_cast<float>(e.params.num(keys::h, 2.0));
        const float yaw = static_cast<float>(e.params.num(keys::yaw));
        const bool open = e.params.get_or<bool>(keys::open, false);

        // Frame first, so the map reads as an object hanging on the wall.
        draw_solid(portal_frame_model(e), {0.14f, 0.11f, 0.08f}, 0.65f, 0.0f, 0.0f,
                   (open || e.id == highlight_) ? 1.0f : 0.0f);

        // The surface only re-uploads when its raster actually changed.
        const auto& pixels = surf.raster();
        if (!bound.texture.valid()) bound.texture.create(surf.px_w(), surf.px_h());
        if (bound.revision != surf.revision()) {
            bound.texture.upload(pixels);
            bound.revision = surf.revision();
        }
        bound.texture.bind(0);

        const gl::Vec3 n{std::sin(yaw), 0.0f, std::cos(yaw)};
        scene_.set("uModel", gl::Mat4::translate(pos + n * 0.055f) * gl::Mat4::rotate_y(yaw) *
                                 gl::Mat4::scale({w, h, 1.0f}));
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
    gl::ShadowMap shadow_;
    gl::RenderTarget scene_target_, resolve_, bloom_a_, bloom_b_;

    std::unordered_map<Key, Bound> surfaces_;
    Key highlight_;
};

}  // namespace sg::render
