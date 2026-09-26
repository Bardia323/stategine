// Stategine - the OpenGL view of a 3D spatial state.
//
// A view, not a state: it reads elements and draws them. Swapping it for the
// terminal view (or running both) changes nothing about the domain, and any
// 3D state gets this renderer for free as long as it uses the shared parameter
// vocabulary (x/y/z, sx/sy/sz, r/g/b, w/h/yaw).
//
// A mesh bound to a surface wears it as a skin: a texture atlas, one cell per
// face (see skin_uv in Shaders.hpp) - a book's spine and covers.
//
// A portal element shows whatever is bound to it:
//   bind_surface(portal, Surface2D*)                 a 2D state, as a panel
//   bind_world(portal, Spatial3D*, carry, back)      another 3D state, as a
//                                                    window you can walk through
//   bind_feed(portal, Spatial3D*, w, h)              another 3D state, as a
//                                                    picture on a screen
//
// And without being told: a portal with `feed` = 1 that the graph embeds a 3D
// state in, while that embedding is open, shows it as a feed (`feed_w` x
// `feed_h`, 640 x 480 unless it says; `live` = 0 holds the picture). What is
// shown is what the graph declares, and only that.
//
// A feed is that world drawn from its own camera in its own look - every
// pass, its composite too - at w x h, and laid on the panel as a surface is:
// so a `crt` panel shows it through its glass. The look belongs to the world,
// the glass to the screen: whoever shows the world, it looks as it looks.
//
// A world portal is seen from a camera of its own: the viewer's, carried
// across by `carry` - the seam's travel, handed in by the game - so a door and
// a window can open onto the same room by different gluings, and a room can
// even open onto itself. With no carry the guest's own camera is used (a View
// embedding keeps it aimed). A portal with `screen` = 1 is a projection: drawn
// only from the room it stands in (seen through another portal it is not
// there), cut by no plane, and screens showing the same world from the same
// eye share one view.
//
// An open world - `sky` = 1 on the state - has a sky instead of a ceiling and
// walls, and may carry a `terrain` element: ground that goes on for ever,
// sampled from a height function bound with bind_terrain and rebuilt around
// the viewer as they walk. A light with `sun` = 1 is parallel light with an
// orthographic shadow that follows the viewer. How far anything is drawn is
// the state's `far` (120 m unless it says otherwise).
//
// Light goes through a doorway as the viewer does. A room is lit by its own
// lamps and also by those of each world its doorways open onto, carried into
// its frame by the doorway's own travel - and by that world's sky, as a glow
// standing just beyond the opening. Such light reaches only what sees it
// through the opening: the doorway is its aperture, the walls round it keep
// the rest out, and whatever stands in it (a door shut in its frame) keeps
// out as much as it covers. Both sides do it, so a lamp by the door lights
// the terrace beyond, and the terrace's sun falls in across the floor. A
// portal with `light` = 0 lets none through: its room lights it itself.
//
// Frame: for each world portal, render the other room into its own target from
// that room's own camera (a View embedding aims it); then the shadow pass, the
// scene into a multisampled HDR target, resolve, ambient occlusion (when the
// look asks for it: the composite pass's `ao` setting is its strength, `ao.radius`
// its reach in metres), bright pass, blur, composite.
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
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <future>
#include <thread>
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
    // Things of the same shape drawn in one call, each with its own place and
    // material, rather than one call each. Off: one call each (to compare).
    bool instancing = true;
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
        .uniform(passes::scene, "uShadowSoft", 1.0)
        .uniform(passes::scene, "uShadowFloor", 0.0)
        .uniform(passes::scene, "uWind", 0.0)
        .uniform(passes::scene, "uStars", 0.0)
        .uniform(passes::scene, "uClouds", 0.0)
        .uniform(passes::scene, "uCloudColor", 1.0, 0.95, 0.92)
        .uniform(passes::scene, "uCloudShade", 0.55, 0.52, 0.62)
        .uniform(passes::scene, "uSkyTop", 0.20, 0.40, 0.75)
        .uniform(passes::scene, "uSkyHorizon", 0.72, 0.78, 0.84)
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
        .uniform(passes::composite, "uGrain", 0.015)
        .uniform(passes::composite, "uRays", 0.0)
        .uniform(passes::composite, "uRayCut", 1.0)
        .uniform(passes::composite, "uRaySpread", 0.3)
        .uniform(passes::composite, "uRayColor", 1.0, 1.0, 1.0)
        .uniform(passes::composite, "uRayDir", 0.0, 1.0, 0.0)
        .uniform(passes::composite, "uCamFwd", 0.0, 0.0, -1.0)
        .uniform(passes::composite, "uTanHalf", 0.7);
}

class GLWorldView {
public:
    static constexpr std::size_t kMaxLights = 24;   // matches the scene shader
    static constexpr std::size_t kShadowMaps = 8;  // layers of the shadow array; matches the scene shader
    static constexpr std::size_t kOwnShadows = 4;  // a room's own strongest four cast; the rest are for doorways
    static constexpr int kMaxBounds = 8;           // doorways per room; matches the scene shader
    static constexpr float kNear = 0.05f;          // the near plane, metres

    explicit GLWorldView(GLQuality q = {}) : q_(q), standard_(Key{"<standard>"}) {
        standard_look(standard_, q_);
    }

    struct LookStats {
        int programs = 0;       // compiled, shared by every look with the same source
        int late = 0;           // compiled mid-frame, because prepare() never saw them
        double compile_ms = 0;  // time spent compiling
    };
    const LookStats& stats() const { return stats_; }

    // Where the last frame's time went, in milliseconds, when timing is on
    // (it waits for the GPU between stages, so it is for finding stalls, not
    // for playing with).
    struct FrameTimes {
        double portals = 0, shadows = 0, scene = 0, post = 0, scene_cpu = 0;
        double feeds = 0;  // screens showing whole worlds, drawn before the rest
        int portal_views = 0, feed_views = 0;
        int draws = 0, instanced = 0;  // scene draw calls, and how many things were drawn in batches
        int shadow_maps = 0;           // shadow layers drawn again (most frames, none)
        double signature = 0;          // ms spent seeing whether anything that casts has moved
    };
    void set_timing(bool on) { timing_ = on; }
    // The state the viewer is attending to - an interface they sit at, say.
    // Its active look is laid over every room's own (the uniforms it sets win),
    // blended by the look fader like any look: so what the eyes are adjusted
    // to belongs to that state, and switching its look is how they adjust.
    void attend(const State* s) { attend_ = s; }
    const FrameTimes& times() const { return times_; }

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

    // How the viewer's camera crosses a portal into the room it shows: the
    // near room's camera in, the far room's eye out.
    using Carry = std::function<void(const Element& from, Element& to)>;

    // Attach another 3D state: the portal becomes a window into it. It is
    // rendered from the viewer's camera carried across by `carry` - the seam's
    // travel, which the game owns - or, with none, from that state's own
    // camera, which a `View` embedding keeps aimed (see sg::portal_carry).
    // Either way the renderer does no portal maths of its own. `back` is the
    // far side's own portal onto this one, left out of the view (it would fill
    // it); with none, any portal there bound to this room is.
    void bind_world(Key portal_element, const Spatial3D* world, Carry carry = {}, Key back = {}) {
        WorldPortal& wp = worlds_[portal_element];
        wp.world = world;
        wp.carry = std::move(carry);
        wp.back = back;
    }

    // The portal shows nothing any more: it is a plain opening again.
    void unbind_world(Key portal_element) { worlds_.erase(portal_element); }

    // Show another 3D state on a panel as a picture (see the top of this
    // file): drawn from its own camera, in its own look, `w` x `h` pixels.
    // Only drawn while the panel is in the room being drawn and in view.
    // Bound again with another world or size, it follows. Not `live`, it
    // holds the last picture it drew - a paused tape.
    void bind_feed(Key portal_element, const Spatial3D* world, int w, int h, bool live = true) {
        Feed& f = feeds_[portal_element];
        if (f.world != world) f.drawn = false;
        f.world = world;
        f.live = live;
        if (f.w != w || f.h != h || !f.out.valid()) {
            f.w = std::max(1, w), f.h = std::max(1, h);
            // Written as the composite writes the screen, encoded; read back
            // as the panel reads any picture, decoded.
            f.out.create(f.w, f.h, gl::GL_SRGB8_ALPHA8, 0, false);
            f.drawn = false;
        }
        if (!f.view) {
            GLQuality q = q_;
            q.shadow_size = std::min(q_.shadow_size, 1024);
            f.view = std::make_unique<GLWorldView>(q);
        }
        f.view->set_fixed_step(fixed_step_);
    }
    void unbind_feed(Key portal_element) { feeds_.erase(portal_element); }
    bool has_feed(Key portal_element) const { return feeds_.count(portal_element) != 0; }

    // Ground for a `terrain` element: its height at any (x, z) of the state.
    // The renderer samples it on a grid centred on the viewer - fine near
    // them, coarse far off - and resamples as they move, so the ground has no
    // edge however far they walk. Bump the element's `rev` to have it
    // resampled when the function itself changes. It is called from several
    // threads at once, in the background while the game goes on, so it must
    // be safe to call while the game is changing its state.
    void bind_terrain(Key terrain_element, std::function<double(double, double)> height) {
        terrains_[terrain_element].height = std::move(height);
    }

    // Draw a highlight on one element for this frame (a "you can use this" cue).
    void highlight(Key element_id) { highlight_ = element_id; }

    // Draw each of `worlds` once, off screen, and forget it was done: every
    // program, target and texture the game will need is then used once before
    // it matters. Drivers finish a shader, or a framebuffer, the first time it
    // is drawn with, not when it is made - so without this the first step into
    // another world stalls. Fades are left exactly as they were.
    void warm(const std::vector<Spatial3D*>& worlds, int fb_w, int fb_h) {
        const LookFader fader = fader_;
        const Mix post = post_;
        const double time = time_;
        for (Spatial3D* w : worlds)
            if (w) render(*w, fb_w, fb_h);
        gl::glFinish();
        fader_ = fader;
        post_ = post;
        time_ = time;
    }

    // One room, standing on its own.
    void render(const Spatial3D& world, int fb_w, int fb_h) {
        const PlacedRoom one{&world, Pose{}, {}};
        render(std::vector<PlacedRoom>{one}, fb_w, fb_h);
    }

    // A neighbourhood of rooms, each with the pose it has when seen from the
    // first one - which is the room the viewer is standing in. Nothing here
    // treats that room as special beyond being the one asked from: hand it a
    // different root and the same geometry is drawn from the other side.
    void render(const std::vector<PlacedRoom>& rooms, int fb_w, int fb_h) {
        if (fb_w <= 0 || fb_h <= 0 || rooms.empty() || !rooms.front().room) return;
        // Feeds the graph declares: an open embedding of a 3D state in a
        // `feed` portal. Those it no longer declares go.
        if (graph_) {
            for (auto& [id, f] : feeds_) f.seen = false;
            for (const Embedding& em : graph_->embeddings()) {
                if (!em.open) continue;
                auto* guest = dynamic_cast<const Spatial3D*>(graph_->find(em.guest));
                const State* host = graph_->find(em.host);
                const Element* panel = host ? host->find(em.portal) : nullptr;
                if (!guest || !panel || panel->params.num(Key{"feed"}, 0.0) < 0.5) continue;
                bind_feed(em.portal, guest, static_cast<int>(panel->params.num(Key{"feed_w"}, 640.0)),
                          static_cast<int>(panel->params.num(Key{"feed_h"}, 480.0)),
                          panel->params.num(Key{"live"}, 1.0) > 0.5);
                Feed& f = feeds_[em.portal];
                f.seen = true;
                f.from_graph = true;
            }
            for (auto it = feeds_.begin(); it != feeds_.end();)
                it = it->second.from_graph && !it->second.seen ? feeds_.erase(it) : std::next(it);
        }
        // Feeds first, each whole, into its own picture: a screen showing a
        // world shows it as it is this frame.
        const auto feeds_from = std::chrono::steady_clock::now();
        int feed_views = 0;
        for (auto& [id, f] : feeds_) {
            if (!f.world || !f.view || (!f.live && f.drawn)) continue;
            bool here = false;
            for (const PlacedRoom& placed : rooms)
                if (placed.room)
                    if (const Element* e = placed.room->find(id); e && e->alive && in_view(*placed.room, *e, camera_of(*rooms.front().room)))
                        here = true;
            if (!here) continue;
            f.drawn = true;
            f.view->graph_ = graph_;  // the looks it is shown in are in the same graph
            f.view->output_ = &f.out;
            f.view->render(*f.world, f.w, f.h);
            f.view->output_ = nullptr;
            ++feed_views;
        }
        const double feeds_ms =
            timing_ ? (gl::glFinish(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - feeds_from).count()) : 0.0;
        ensure_resources();
        ensure_targets(fb_w, fb_h);
        advance_clock();

        const Spatial3D& world = *rooms.front().room;
        // The post passes belong to the viewer, so they wear the look of the
        // room the viewer is in, and fade when that room changes.
        post_ = mix(view_key(), look_of(world));
        const Camera eye_cam = camera_of(world);
        const float aspect = static_cast<float>(fb_w) / static_cast<float>(fb_h);

        // --- portal views, one pass per window --------------------------------
        // Rendered first, at framebuffer resolution, because the portal quad
        // samples them in screen space. The guest's camera was already carried
        // through the doorway by the embedding's functor.
        // Every portal's targets, whichever world they are in, made now: the
        // first frame through a doorway must not stop to allocate.
        for (auto& [id, wp] : worlds_) {
            if (!wp.world || (wp.target.valid() && wp.width == fb_w && wp.height == fb_h)) continue;
            wp.ms.create(fb_w, fb_h, gl::GL_RGBA16F, msaa_, true);
            wp.target.create(fb_w, fb_h, gl::GL_RGBA16F, 0, false);
            wp.width = fb_w;
            wp.height = fb_h;
            // Touched once now: drivers allocate on first use, and that is
            // otherwise the first frame through the doorway.
            for (gl::RenderTarget* t : {&wp.ms, &wp.target}) {
                t->bind();
                gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
            }
            wp.ms.blit_to(wp.target);
        }
        times_ = FrameTimes{};
        times_.feeds = feeds_ms;
        times_.feed_views = feed_views;
        const auto mark = [this] {
            if (timing_) gl::glFinish();
            return std::chrono::steady_clock::now();
        };
        const auto since = [](std::chrono::steady_clock::time_point a) {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
        };
        auto t0 = mark();
        // Screens already drawn this frame: the same world from the same eye
        // is one view, whichever screen shows it.
        std::vector<std::pair<WorldPortal*, Element>> screens;
        for (const auto& e : world.elements()) {
            if (e.kind != kinds::portal || !e.alive) continue;
            auto it = worlds_.find(e.id);
            if (it == worlds_.end() || !it->second.world) continue;
            WorldPortal& wp = it->second;
            wp.shared = nullptr;
            if (!in_view(world, e, eye_cam)) continue;
            // The virtual camera stands behind the far side's doorway - that is
            // what a portal is. What lies between it and the doorway is cut
            // away by a plane: this portal's own plane, carried to the far
            // side by the same turn and shift that carried the camera. So a
            // window beside a door shows the far side beside it, and nothing
            // standing behind the far doorway gets in the way.
            Element eye = wp.world->camera();
            if (wp.carry) wp.carry(world.camera(), eye);
            const bool screen = is_screen(e);
            if (screen) {
                for (const auto& [drawn, at] : screens)
                    if (drawn->world == wp.world && same_eye(at, eye)) wp.shared = drawn;
                if (wp.shared) continue;
                screens.emplace_back(&wp, eye);
            }
            const Camera guest_cam = camera_of(eye);
            const Element* back = !wp.back.empty() ? wp.world->find(wp.back) : back_portal(*wp.world, world);
            const PlacedRoom guest{wp.world, Pose{}, {}};
            std::vector<HalfSpace> clips;
            if (!screen) clips.push_back(far_side(world, e, eye));
            draw_world(std::vector<PlacedRoom>{guest}, guest_cam, aspect, wp.ms,
                       /*depth=*/1, kNear, back ? back->id.key() : Key{}, clips);
            wp.ms.blit_to(wp.target);
            ++times_.portal_views;
        }
        times_.portals = since(t0);
        t0 = mark();

        // --- the room the viewer is actually standing in ------------------------
        draw_world(rooms, eye_cam, aspect, scene_target_, /*depth=*/0, kNear);
        times_.scene_cpu = since(t0);
        if (timing_) gl::glFinish();
        times_.scene = since(t0);
        t0 = mark();

        scene_target_.blit_to(resolve_);
        scene_src_ = &resolve_;
        const double ao = setting(post_, passes::composite, "ao", 0.0);
        if (ao > 0.0) {
            view_ = ViewParams{eye_cam.fov, aspect, kNear,
                               static_cast<float>(world.params().num(Key{"far"}, 120.0))};
            run_ao(static_cast<float>(ao),
                   static_cast<float>(setting(post_, passes::composite, "ao.radius", 0.45)));
            scene_src_ = &lit_;
        }
        run_bloom();
        composite(fb_w, fb_h);
        if (timing_) gl::glFinish();
        times_.post = since(t0);
        highlight_ = Key{};
    }

private:
    struct Camera {
        gl::Vec3 eye;
        gl::Vec3 forward{0, 0, -1};
        gl::Vec3 up{0, 1, 0};
        float fov = 1.2f;
    };

    struct Light {
        gl::Vec3 pos{0, 3, 0};
        gl::Vec3 dir{0, -1, 0};
        gl::Vec3 color{1.0f, 0.93f, 0.82f};
        float power = 26.0f;
        float inner = 0.55f;  // radians
        float outer = 1.15f;
        bool sun = false;      // parallel, from `dir`; its shadow is a box round the viewer
        float extent = 40.0f;  // a sun's shadow reaches this far either side of the viewer
        float floor = -1.0f;   // light left in its own full shadow; < 0: the look's uShadowFloor
        bool indirect = false; // stands in for bounced light: no highlight, and occlusion darkens it
        float falloff = 0.0f;  // 0: the soft falloff; 1: the inverse square, as real light
        // Light from beyond a doorway comes in only through its opening: the
        // opening's middle, which way across it is, and half its width and
        // height. Such a light casts no shadow of its own.
        bool gated = false;
        gl::Vec3 gate_at{0, 0, 0}, gate_across{1, 0, 0}, gate_in{0, 0, 1};
        float gate_w = 0.0f, gate_h = 0.0f;
        float open = 1.0f;  // how much of the opening is clear, for a gated light with no shadow map
    };

    struct BoundSurface {
        Surface2D* surface = nullptr;
        gl::Texture texture;
        uint64_t revision = 0;
    };

    struct WorldPortal {
        const Spatial3D* world = nullptr;
        Carry carry;
        Key back;
        WorldPortal* shared = nullptr;  // this frame, showing another screen's view
        gl::RenderTarget ms;      // drawn into, multisampled
        gl::RenderTarget target;  // resolved, and sampled by the portal's quad
        int width = 0, height = 0;
    };

    struct TerrainMesh {
        std::function<double(double, double)> height;
        gl::Mesh mesh;
        double cx = 1e300, cz = 1e300, rev = -1;
        // A resample under way in the background, and where it is centred.
        std::future<std::vector<float>> job;
        double job_cx = 0, job_cz = 0, job_rev = -1;
    };

    // Is any of the portal in front of the camera, and near enough to draw?
    bool in_view(const Spatial3D& world, const Element& e, const Camera& cam) const {
        const Pose p = pose_of(world, e);
        const float w = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f + 0.3f;
        const float h = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f + 0.3f;
        const gl::Vec3 c = to_vec3(p.position), side = to_vec3(across(p.yaw));
        const float far = static_cast<float>(world.params().num(Key{"far"}, 120.0));
        const gl::Vec3 f = gl::normalize(cam.forward);
        for (float a : {-1.0f, 1.0f})
            for (float b : {-1.0f, 1.0f}) {
                const gl::Vec3 corner = c + side * (a * w) + gl::Vec3{0, b * h, 0};
                const float along = gl::dot(corner - cam.eye, f);
                if (along > 0.0f && along < far) return true;
            }
        return false;
    }

    // The plane of `portal` (in `host`) as seen on the far side, facing away
    // from it: the far side is drawn only beyond it. The turn and shift are
    // read off the two cameras - the guest's was carried from the host's by
    // the portal's own functor, so the pair of them is that functor.
    static HalfSpace far_side(const Spatial3D& host, const Element& portal, const Element& gc) {
        const Pose p = world_pose(host, portal);
        const Vec3d n = heading(p.yaw);
        const double inset = portal.params.num(Key{"inset"}, 0.06);
        const Vec3d at{p.position.x + n.x * inset, p.position.y, p.position.z + n.z * inset};
        const Element& hc = host.camera();
        const double turn = gc.params.num(keys::yaw) - hc.params.num(keys::yaw);
        const Vec3d he = position_of(hc), ge = position_of(gc);
        const Vec3d off = rotate_xz({at.x - he.x, at.y - he.y, at.z - he.z}, turn);
        const Vec3d q{ge.x + off.x, ge.y + off.y, ge.z + off.z};
        const Vec3d m = rotate_xz(n, turn);
        return HalfSpace{{-m.x, -m.y, -m.z}, m.x * q.x + m.y * q.y + m.z * q.z};
    }

    void set_frame(const Pose& p) {
        frame_ = p;
        frame_matrix_ = gl::Mat4::translate({static_cast<float>(p.position.x),
                                             static_cast<float>(p.position.y),
                                             static_cast<float>(p.position.z)}) *
                        gl::Mat4::rotate_y(static_cast<float>(p.yaw));
    }

    bool has_surface(const Element& e) const {
        auto it = surfaces_.find(e.id);
        if (it != surfaces_.end() && it->second.surface != nullptr) return true;
        auto f = feeds_.find(e.id);
        return f != feeds_.end() && f->second.world != nullptr;
    }

    static Camera camera_of(const Spatial3D& world) { return camera_of(world.camera()); }

    static Camera camera_of(const Element& cam) {
        Camera c;
        c.eye = to_vec3(position_of(cam));
        c.forward = to_vec3(forward_of(cam));
        c.fov = static_cast<float>(cam.params.num(keys::fov, 70.0)) * 3.14159265f / 180.0f;
        // `roll`: the head tipped about the line of sight (radians).
        if (const float roll = static_cast<float>(cam.params.num(keys::roll, 0.0)); roll != 0.0f) {
            const gl::Vec3 side = gl::normalize(gl::cross(c.forward, gl::Vec3{0, 1, 0}));
            const gl::Vec3 up = gl::cross(side, c.forward);
            c.up = up * std::cos(roll) + side * std::sin(roll);
        }
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
        cylinder_.create(gl::cylinder_vertices());
        sphere_.create(gl::sphere_vertices());
        quad_.create(gl::quad_vertices());
        screen_.create();
        ready_ = true;
    }

    void ensure_targets(int w, int h) {
        if (w == target_w_ && h == target_h_) return;
        target_w_ = w;
        target_h_ = h;
        // As many samples as asked for, if the driver has them.
        gl::GLint most = 0;
        gl::glGetIntegerv(gl::GL_MAX_SAMPLES, &most);
        msaa_ = most > 0 ? std::min(q_.msaa, static_cast<int>(most)) : q_.msaa;
        scene_target_.create(w, h, gl::GL_RGBA16F, msaa_, true);
        resolve_.create(w, h, gl::GL_RGBA16F, 0, false);
        depth_.create(w, h, gl::GL_RGBA16F, 0, true, /*depth_texture=*/true);
        lit_.create(w, h, gl::GL_RGBA16F, 0, false);
        // Full resolution: at half, the occlusion's edges stair-step over the
        // antialiased picture.
        ao_a_.create(w, h, gl::GL_RGBA16F, 0, false);
        ao_b_.create(w, h, gl::GL_RGBA16F, 0, false);
        const int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
        bloom_a_.create(bw, bh, gl::GL_RGBA16F, 0, false);
        bloom_b_.create(bw, bh, gl::GL_RGBA16F, 0, false);
        bloom_levels_ = 0;
        for (int lw = bw / 2, lh = bh / 2; bloom_levels_ < kBloomLevels && lw >= 8 && lh >= 8; lw /= 2, lh /= 2)
            bloom_chain_[bloom_levels_++].create(lw, lh, gl::GL_RGBA16F, 0, false);
    }

    // One room's own lamps, placed as the room is.
    std::vector<Light> own_lights(const Spatial3D& room, const Pose& pose) const {
        std::vector<Light> out;
        for (const auto& e : room.elements()) {
            if (e.kind != kinds::light || !e.alive) continue;
            Light l;
            l.pos = to_vec3(compose_pose(pose, pose_of(room, e)).position);
            l.color = color_of(e, l.color);
            l.power = static_cast<float>(e.params.num(keys::intensity, 1.0)) * 26.0f;
            l.dir = gl::normalize(to_vec3(rotate_xz(
                {e.params.num(Key{"dx"}, 0.0), e.params.num(Key{"dy"}, -1.0), e.params.num(Key{"dz"}, 0.0)}, pose.yaw)));
            l.inner = static_cast<float>(e.params.num(Key{"inner"}, 0.55));
            l.outer = static_cast<float>(e.params.num(Key{"outer"}, 1.15));
            l.sun = e.params.num(Key{"sun"}, 0.0) > 0.5;
            l.extent = static_cast<float>(e.params.num(Key{"extent"}, 40.0));
            l.floor = static_cast<float>(e.params.num(Key{"shadow_floor"}, -1.0));
            l.indirect = e.params.num(Key{"indirect"}, 0.0) > 0.5;
            l.falloff = static_cast<float>(std::clamp(e.params.num(Key{"falloff"}, 0.0), 0.0, 1.0));
            if (l.power <= 0.0f) continue;  // switched off
            out.push_back(l);
        }
        return out;
    }

    // What comes in through the doorways of `placed`: the lamps of each world
    // a doorway opens onto, and a glow of its sky, carried into this room by
    // the doorway's own travel and let through its opening only. The travel
    // is read as a turn and a shift, as far_side() reads it: a probe standing
    // in the doorway, carried across.
    std::vector<Light> through_doorways(const PlacedRoom& placed) {
        std::vector<Light> out;
        if (!placed.room) return out;
        const Spatial3D& room = *placed.room;
        for (const auto& e : room.elements()) {
            if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.num(Key{"light"}, 1.0) < 0.5) continue;
            const auto it = worlds_.find(e.id);
            if (it == worlds_.end() || !it->second.world || !it->second.carry) continue;
            const Spatial3D& far = *it->second.world;
            const Pose door = pose_of(room, e);
            const float half_w = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f;
            const float half_h = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f;
            if (half_w <= 0.0f || half_h <= 0.0f) continue;
            Element probe = room.camera(), there = far.camera();
            probe.params.set(keys::x, door.position.x).set(keys::y, door.position.y).set(keys::z, door.position.z)
                .set(keys::yaw, 0.0).set(keys::pitch, 0.0);
            it->second.carry(probe, there);
            const double turn = there.params.num(keys::yaw);
            const Vec3d from = position_of(there);
            // A point of the far world, where this room has it.
            const auto here = [&](const gl::Vec3& q) {
                const Vec3d local = rotate_xz({q.x - from.x, q.y - from.y, q.z - from.z}, -turn);
                const Pose p{{door.position.x + local.x, door.position.y + local.y, door.position.z + local.z}, 0.0};
                return to_vec3(compose_pose(placed.pose, p).position);
            };
            const auto turned = [&](const gl::Vec3& d) {
                return to_vec3(rotate_xz({d.x, d.y, d.z}, placed.pose.yaw - turn));
            };
            const gl::Vec3 at = to_vec3(compose_pose(placed.pose, door).position);
            const gl::Vec3 across_door = to_vec3(across(door.yaw + placed.pose.yaw));
            const gl::Vec3 into = to_vec3(heading(door.yaw + placed.pose.yaw));  // a portal faces into its own room
            // A door shut in the opening lets nothing through. Ajar, what
            // comes through throws the leaf's shadow (its own shadow map);
            // `open` is only for a light past the maps there are.
            bool shut = false;
            const float open = 1.0f - covered(room, e, door, half_w, half_h, shut);
            if (shut) continue;
            const auto gate = [&](Light& l) {
                l.gated = true;
                l.gate_at = at, l.gate_across = across_door, l.gate_in = into, l.gate_w = half_w, l.gate_h = half_h;
                // In the shadow of what stands in the way, none of it gets through.
                l.open = open, l.floor = 0.0f;
            };
            // Its lamps, the strongest few. A bounce standing in for light
            // from all round belongs to its own room, and stays there.
            std::vector<Light> lamps = own_lights(far, Pose{});
            lamps.erase(std::remove_if(lamps.begin(), lamps.end(), [](const Light& l) { return l.indirect; }), lamps.end());
            std::sort(lamps.begin(), lamps.end(), [](const Light& a, const Light& b) {
                if (a.sun != b.sun) return a.sun;
                return a.power > b.power;
            });
            if (lamps.size() > 3) lamps.resize(3);
            for (Light l : lamps) {
                l.pos = here(l.pos);
                l.dir = gl::normalize(turned(l.dir));
                gate(l);
                out.push_back(l);
            }
            // Its sky, or whatever light fills it from all round, seen
            // through the opening: a soft lamp just beyond it, as wide as it.
            const LookState& look = look_of(far);
            const double amb = value(look, passes::scene, Key{"uAmbient"}, 0.55);
            const gl::Vec3 sky{static_cast<float>(value(look, passes::scene, Key{"uSky.x"}, 0.10) * amb),
                               static_cast<float>(value(look, passes::scene, Key{"uSky.y"}, 0.13) * amb),
                               static_cast<float>(value(look, passes::scene, Key{"uSky.z"}, 0.20) * amb)};
            const float bright = std::max({sky.x, sky.y, sky.z});
            if (bright > 1e-4f) {
                // A portal faces into its own room; beyond is behind it.
                const gl::Vec3 beyond = to_vec3(heading(door.yaw + placed.pose.yaw)) * -1.0f;
                Light glow;
                glow.pos = at + beyond * 0.35f;
                glow.dir = beyond * -1.0f;
                glow.color = sky * (1.0f / bright);
                glow.power = bright * half_w * half_h * 24.0f;
                glow.inner = 0.9f;
                glow.outer = 1.55f;
                glow.falloff = 1.0f;
                glow.indirect = true;
                gate(glow);
                out.push_back(glow);
            }
        }
        if (out.size() > 8) out.resize(8);
        return out;
    }

    // The doorways of a room, and the light from all round beyond each, for
    // the scene shader (around_at): a thing through a doorway is lit as one.
    void doors_to_program(const PlacedRoom& placed) {
        static const auto name = [](const char* base, int i) {
            static std::array<std::array<std::string, 4>, 5> names = [] {
                std::array<std::array<std::string, 4>, 5> n;
                const char* bases[] = {"uDoorAt", "uDoorAxis", "uDoorIn", "uDoorSky", "uDoorGround"};
                for (int b = 0; b < 5; ++b)
                    for (int k = 0; k < 4; ++k) n[static_cast<std::size_t>(b)][static_cast<std::size_t>(k)] = std::string(bases[b]) + "[" + std::to_string(k) + "]";
                return n;
            }();
            const std::string s = base;
            const int b = s == "uDoorAt" ? 0 : s == "uDoorAxis" ? 1 : s == "uDoorIn" ? 2 : s == "uDoorSky" ? 3 : 4;
            return names[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)].c_str();
        };
        int count = 0;
        if (placed.room)
            for (const auto& e : placed.room->elements()) {
                if (count >= 4) break;
                if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.num(Key{"light"}, 1.0) < 0.5) continue;
                const auto it = worlds_.find(e.id);
                if (it == worlds_.end() || !it->second.world) continue;
                const Pose door = pose_of(*placed.room, e);
                // A door shut in it: nothing of the other side here.
                bool shut = false;
                covered(*placed.room, e, door, static_cast<float>(e.params.num(keys::w, 3.0) * 0.5), static_cast<float>(e.params.num(keys::h, 2.0) * 0.5), shut);
                if (shut) continue;
                const gl::Vec3 at = to_vec3(compose_pose(placed.pose, door).position);
                const Vec3d a = across(door.yaw + placed.pose.yaw), in = heading(door.yaw + placed.pose.yaw);
                const LookState& look = look_of(*it->second.world);
                const double amb = value(look, passes::scene, Key{"uAmbient"}, 0.55);
                const auto v3 = [&](const char* k, double fx, double fy, double fz) {
                    return gl::Vec3{static_cast<float>(value(look, passes::scene, Key{std::string(k) + ".x"}, fx) * amb),
                                    static_cast<float>(value(look, passes::scene, Key{std::string(k) + ".y"}, fy) * amb),
                                    static_cast<float>(value(look, passes::scene, Key{std::string(k) + ".z"}, fz) * amb)};
                };
                scene_->set(name("uDoorAt", count), at.x, at.y, at.z, static_cast<float>(e.params.num(keys::w, 3.0) * 0.5));
                scene_->set(name("uDoorAxis", count), static_cast<float>(a.x), static_cast<float>(a.z),
                            static_cast<float>(e.params.num(keys::h, 2.0) * 0.5), 0.0f);
                scene_->set(name("uDoorIn", count), static_cast<float>(in.x), static_cast<float>(in.z), 0.0f, 0.0f);
                scene_->set(name("uDoorSky", count), v3("uSky", 0.10, 0.13, 0.20));
                scene_->set(name("uDoorGround", count), v3("uGround", 0.14, 0.10, 0.07));
                ++count;
            }
        scene_->set("uDoorCount", count);
    }

    // How much of a doorway's opening (half `half_w` across, `half_h` high)
    // the things of its room standing in it cover, 0 to 1: each box near the
    // opening's plane, seen square on to it - the most any one of them does
    // (a door's panels lie on its slab: covering the same, not more). A door
    // shut in its frame covers it all; swung open it is edge on, and covers
    // a sliver. `shut`: something lying flat in the opening fills it.
    float covered(const Spatial3D& room, const Element& portal, const Pose& door, float half_w, float half_h, bool& shut) const {
        const Vec3d a = across(door.yaw), n = heading(door.yaw);
        float most = 0.0f;
        shut = false;
        for (const auto& e : room.elements()) {
            if (!e.alive || (e.kind != kinds::mesh && e.kind != kinds::wall) || e.id == portal.id) continue;
            if (e.params.num(Key{"cast"}, 1.0) < 0.5) continue;
            const Vec3d c = pose_of(room, e).position;  // in the room, not off its anchor
            const double dx = c.x - door.position.x, dz = c.z - door.position.z;
            if (dx * dx + dz * dz > (half_w + 1.5) * (half_w + 1.5)) continue;
            const gl::Mat4& m = box_matrix(room, e).m;
            float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f, d0 = 1e9f, d1 = -1e9f;
            for (int k = 0; k < 8; ++k) {
                const gl::Vec3 q = m.transform_point({k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, k & 4 ? 0.5f : -0.5f});
                const double rx = q.x - door.position.x, ry = q.y - door.position.y, rz = q.z - door.position.z;
                const float u = static_cast<float>(rx * a.x + rz * a.z), v = static_cast<float>(ry);
                const float d = static_cast<float>(rx * n.x + rz * n.z);
                u0 = std::min(u0, u), u1 = std::max(u1, u), v0 = std::min(v0, v), v1 = std::max(v1, v);
                d0 = std::min(d0, d), d1 = std::max(d1, d);
            }
            // Only what is in the opening, not a thing across the room.
            if (d1 < -0.25f || d0 > 0.25f) continue;
            const float w = std::max(0.0f, std::min(u1, half_w) - std::max(u0, -half_w));
            const float h = std::max(0.0f, std::min(v1, half_h) - std::max(v0, -half_h));
            const float part = w * h / (4.0f * half_w * half_h);
            most = std::max(most, part);
            if (part > 0.95f && d1 - d0 < 0.1f) shut = true;
        }
        return std::clamp(most, 0.0f, 1.0f);
    }

    // Every lamp that lights these rooms, strongest first: the first four of
    // their own get shadow maps (`shadowed` of them), the rest light without
    // casting. What comes in through their doorways goes after the shadowed,
    // before their fainter own.
    std::vector<Light> read_lights(const std::vector<PlacedRoom>& rooms, std::size_t& shadowed) {
        std::vector<Light> out;
        for (const PlacedRoom& placed : rooms)
            if (placed.room)
                for (const Light& l : own_lights(*placed.room, placed.pose)) out.push_back(l);
        // A sun first - it lights everything, so it has the first shadow -
        // then lamps, brightest first. Not nearest: which lamps cast shadows
        // must not change as the viewer walks about, or shadows pop in and out.
        // Equal lamps are ordered by where they hang, never by the viewer:
        // a tie broken by distance hands the shadow maps from lamp to lamp as
        // the viewer walks, and shadows vanish a step further off.
        // Bounce light stands in for light from all round and casts no
        // shadow worth a map: it comes after every real lamp.
        std::sort(out.begin(), out.end(), [](const Light& a, const Light& b) {
            if (a.indirect != b.indirect) return b.indirect;
            if (a.sun != b.sun) return a.sun;
            if (a.power != b.power) return a.power > b.power;
            if (a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
            if (a.pos.z != b.pos.z) return a.pos.z < b.pos.z;
            return a.pos.y < b.pos.y;
        });
        const std::size_t own = std::min(out.size(), kOwnShadows);
        std::vector<Light> in;
        for (const PlacedRoom& placed : rooms)
            for (const Light& l : through_doorways(placed)) in.push_back(l);
        // What comes through a doorway gets a shadow map of its own, while
        // there are maps: then what stands in the opening - a door ajar, the
        // frame - throws its own shadow. Past that, it is let in by how much
        // of the opening is clear.
        shadowed = std::min(own + in.size(), kShadowMaps);
        for (std::size_t k = shadowed - own; k < in.size(); ++k) in[k].power *= in[k].open;
        out.insert(out.begin() + static_cast<std::ptrdiff_t>(own), in.begin(), in.end());
        shadowed = std::min(shadowed, out.size());
        if (out.size() > kMaxLights) out.resize(kMaxLights);
        if (out.empty()) {
            Light none;
            none.power = 0.0f;  // everything off: ambient only, and the shadow map unused
            out.push_back(none);
        }
        return out;
    }

    // The doorway in `guest` that leads back to `host` - the one being looked
    // through. Its plane is where the portal view has to start, or the wall it
    // is set into hides everything; and it must not be drawn in that view, or
    // seen from the virtual camera it fills the whole frame.
    static bool is_screen(const Element& e) { return e.params.num(Key{"screen"}, 0.0) > 0.5; }

    static bool same_eye(const Element& a, const Element& b) {
        for (Key k : {keys::x, keys::y, keys::z, keys::yaw, keys::pitch, keys::fov})
            if (a.params.num(k, 0.0) != b.params.num(k, 0.0)) return false;
        return true;
    }

    const Element* back_portal(const Spatial3D& guest, const Spatial3D& host) const {
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
                    gl::RenderTarget& target, int depth, float znear, Key skip_portal = Key{},
                    const std::vector<HalfSpace>& clips = {}) {
        std::size_t shadowed = 0;
        const std::vector<Light> lights = read_lights(rooms, shadowed);
        cam_eye_ = cam.eye;
        const float zfar = static_cast<float>(rooms.front().room->params().num(Key{"far"}, 120.0));
        for (const PlacedRoom& placed : rooms)
            if (placed.room)
                for (const auto& e : placed.room->elements())
                    if (e.alive && e.kind == terrain_kind()) ensure_terrain(e, cam);
        // The strongest lamps get a shadow map each.
        gl::Mat4 light_vp[kShadowMaps];
        float bias[kShadowMaps] = {1.0f, 1.0f, 1.0f, 1.0f};
        for (std::size_t i = 0; i < kShadowMaps; ++i) {
            const Light& l = lights[std::min(i, lights.size() - 1)];
            if (l.sun) {
                // A box of shadow round the viewer, a little ahead of them,
                // moved in whole texels so the edges do not crawl. The sun's
                // way is taken in steps of about a fifth of a degree: it
                // creeps across the sky, and its map is drawn again when it
                // has moved that far, not every frame of the day.
                gl::Vec3 d = gl::normalize(l.dir);
                d = gl::normalize({std::round(d.x * 300.0f) / 300.0f, std::round(d.y * 300.0f) / 300.0f,
                                   std::round(d.z * 300.0f) / 300.0f});
                const float e = l.extent, reach = e * 4.0f;
                const float texel = 2.0f * e / static_cast<float>(q_.shadow_size);
                gl::Vec3 c = cam.eye + gl::normalize({cam.forward.x, 0.0f, cam.forward.z}) * (e * 0.4f);
                c = {std::floor(c.x / texel) * texel, std::floor(c.y / texel) * texel,
                     std::floor(c.z / texel) * texel};
                const gl::Vec3 up = std::fabs(d.y) > 0.99f ? gl::Vec3{0, 0, 1} : gl::Vec3{0, 1, 0};
                light_vp[i] = gl::Mat4::ortho(-e, e, -e, e, 1.0f, reach * 2.0f) *
                              gl::Mat4::look_at(c - d * reach, c, up);
                bias[i] = 45.0f / (reach * 2.0f);
            } else {
                // A wide lamp spreads its map thin, so each texel covers more
                // of a wall at a slant: its bias grows with the width, or the
                // walls stripe with acne.
                const float fov = std::min(l.outer * 2.05f, 2.7f);
                light_vp[i] = gl::Mat4::perspective(fov, 1.0f, 0.1f, 40.0f) *
                              gl::Mat4::look_at(l.pos, l.pos + l.dir, std::fabs(l.dir.y) > 0.99f ? gl::Vec3{0, 0, 1} : gl::Vec3{0, 1, 0});
                bias[i] = std::max(1.0f, std::tan(fov * 0.5f) / std::tan(0.6f)) * 1.5f;
            }
        }
        const gl::Mat4 view_proj =
            gl::Mat4::perspective(cam.fov, aspect, znear, zfar) *
            gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, cam.up);

        if (timing_) gl::glFinish();
        const auto shadow_start = std::chrono::steady_clock::now();
        // --- shadow depth, one pass per shadowed lamp ------------------------
        // Each world seen (the room, and whatever a portal shows) keeps its own
        // maps, and a map is drawn again only when its lamp has moved or
        // turned, or anything that casts has: most frames, nothing has, and
        // the shadows cost nothing.
        // Each view keeps its own maps: two windows onto one world see it
        // lit differently (each lets in its own room's light), and would
        // otherwise draw over each other's maps every frame.
        ShadowSet& maps = shadows_for(rooms.front().room, &target);
        if (maps.array.ensure(q_.shadow_size, static_cast<int>(std::max<std::size_t>(shadowed, 1))))
            for (uint64_t& s : maps.sig) s = 0;
        const auto sig_from = std::chrono::steady_clock::now();
        const uint64_t casters = caster_signature(rooms);
        times_.signature += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sig_from).count();
        gl::glEnable(gl::GL_DEPTH_TEST);
        gl::glEnable(gl::GL_CULL_FACE);
        gl::glCullFace(gl::GL_FRONT);  // front-face culling hides most acne
        const gl::Program& caster = *program_for(post_.shown(), passes::shadow);
        bool caster_ready = false;
        for (std::size_t i = 0; i < shadowed; ++i) {
            uint64_t sig = casters ^ (0x9E3779B97F4A7C15ULL * (i + 1));
            for (float f : light_vp[i].m) sig = mix_bits(sig, f);
            if (maps.sig[i] == sig) continue;
            maps.sig[i] = sig;
            ++times_.shadow_maps;
            if (!caster_ready) {
                caster.use();
                apply_uniforms(caster, post_, passes::shadow);
                caster.set("uInstanced", 0);
                caster_ready = true;
            }
            maps.array.bind_layer(static_cast<int>(i));
            gl::glClear(gl::GL_DEPTH_BUFFER_BIT);
            caster.set("uLightViewProj", light_vp[i]);
            // Light from beyond a doorway is kept out only by what stands on
            // this side of it - the room behind the opening's plane is the
            // other world's, and the light comes from there. (A hand's
            // breadth of slack keeps a leaf shut in the plane casting.)
            const Light& li = lights[std::min(i, lights.size() - 1)];
            if (li.gated) {
                gl::glEnable(gl::GL_CLIP_DISTANCE0);
                caster.set("uCasterSide", li.gate_in.x, li.gate_in.y, li.gate_in.z, 0.08f - gl::dot(li.gate_in, li.gate_at));
            } else {
                gl::glDisable(gl::GL_CLIP_DISTANCE0);
                caster.set("uCasterSide", 0.0f, 0.0f, 0.0f, 1.0f);
            }
            for (const PlacedRoom& placed : rooms) {
                if (!placed.room) continue;
                set_frame(placed.pose);
                for (const auto& e : placed.room->elements()) {
                    if (!e.alive) continue;
                    if (e.kind == kinds::mesh || e.kind == kinds::wall) {
                        // A lamp's own shade does not shadow its lamp.
                        if (e.params.num(Key{"cast"}, 1.0) < 0.5) continue;
                        if (q_.instancing) {
                            batch(shape_of(e), box_matrix(*placed.room, e).m, {}, 0, 0, 0, 0, 0);
                            continue;
                        }
                        caster.set("uModel", frame_matrix_ * box_matrix(*placed.room, e).m);
                        shape_of(e).draw();
                    } else if (e.kind == terrain_kind()) {
                        auto t = terrains_.find(e.id);
                        if (t == terrains_.end() || !t->second.mesh.valid()) continue;
                        caster.set("uModel", frame_matrix_);
                        t->second.mesh.draw();
                    } else if (e.kind == kinds::portal && !is_doorway(e) && has_surface(e)) {
                        caster.set("uModel",
                                   frame_matrix_ * portal_frame_model(*placed.room, e).m);
                        cube_.draw();
                    }
                }
                flush_batches(caster, false);
            }
        }
        gl::glDisable(gl::GL_CLIP_DISTANCE0);
        gl::glCullFace(gl::GL_BACK);
        if (timing_) {
            gl::glFinish();
            times_.shadows += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shadow_start).count();
        }

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
        maps.array.bind_depth(1);

        // Everything a scene shader is fed that is not the look's. Set again
        // whenever a room's look brings a different program.
        const auto frame_uniforms = [&](const gl::Program& p) {
            p.set("uViewProj", view_proj);
            p.set("uInstanced", 0);
            p.set("uDim", 0.0f);
            for (std::size_t i = 0; i < kShadowMaps; ++i) {
                p.set(shadow_uniform(i, 0), light_vp[i]);
                p.set(shadow_uniform(i, 1), bias[i]);
            }
            p.set("uLightCount", static_cast<int>(lights.size()));
            p.set("uShadowCount", static_cast<int>(shadowed));
            gl::Vec3 sun_dir{0, 1, 0}, sun_color{0, 0, 0};
            for (std::size_t i = 0; i < lights.size(); ++i) {
                p.set(light_uniform(i, 0), lights[i].pos);
                p.set(light_uniform(i, 1), lights[i].dir);
                p.set(light_uniform(i, 2), lights[i].color);
                p.set(light_uniform(i, 3), lights[i].power);
                p.set(light_uniform(i, 4), std::cos(lights[i].inner));
                p.set(light_uniform(i, 5), std::cos(lights[i].outer));
                p.set(light_uniform(i, 6), lights[i].sun ? 1.0f : 0.0f);
                p.set(light_uniform(i, 7), lights[i].floor);
                p.set(light_uniform(i, 8), lights[i].indirect ? 1.0f : 0.0f);
                p.set(light_uniform(i, 9), lights[i].falloff);
                const Light& l = lights[i];
                p.set(light_uniform(i, 10), l.gate_at.x, l.gate_at.y, l.gate_at.z, l.gate_w);
                p.set(light_uniform(i, 11), l.gate_across.x, l.gate_across.z, l.gate_h, l.gated ? 1.0f : 0.0f);
                // The sky's sun is this room's own, not one seen through a door.
                if (lights[i].sun && !lights[i].gated && sun_color.x == 0.0f && sun_color.y == 0.0f && sun_color.z == 0.0f) {
                    sun_dir = gl::normalize(lights[i].dir) * -1.0f;
                    sun_color = lights[i].color;
                }
            }
            p.set("uSunDir", sun_dir);
            p.set("uSunColor", sun_color);
            p.set("uViewPos", cam.eye);
            p.set("uTime", static_cast<float>(time_));
            p.set("uShadowTexel", 1.0f / static_cast<float>(maps.array.size()),
                  1.0f / static_cast<float>(maps.array.size()));
            p.set("uShadowMaps", 1);
            p.set("uTex", 0);
            p.set("uCRT", 0.0f);
            p.set("uScreenUV", 0.0f);
            p.set("uViewport", static_cast<float>(target.width()),
                  static_cast<float>(target.height()));
        };

        scene_ = nullptr;
        const Frustum view = frustum_of(view_proj);
        for (const PlacedRoom& placed : rooms) {
            if (!placed.room) continue;
            set_frame(placed.pose);
            const Spatial3D& room = *placed.room;

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
            apply_attended(*scene_, passes::scene);

            // This room's side of each doorway, from the portals as they are now,
            // and whatever the caller cuts away besides.
            int bounds = 0;
            for (const HalfSpace& h : clips) {
                if (bounds >= kMaxBounds) break;
                scene_->set(clip_uniform(bounds++), static_cast<float>(h.normal.x),
                            static_cast<float>(h.normal.y), static_cast<float>(h.normal.z),
                            static_cast<float>(h.offset));
            }
            for (Key d : placed.doorways) {
                const Element* portal = room.find(d);
                if (!portal || bounds >= kMaxBounds) continue;
                const HalfSpace h = room_side(room, *portal, placed.pose);
                scene_->set(clip_uniform(bounds++), static_cast<float>(h.normal.x),
                            static_cast<float>(h.normal.y), static_cast<float>(h.normal.z),
                            static_cast<float>(h.offset));
            }
            scene_->set("uClipCount", bounds);
            doors_to_program(placed);

            if (room.params().num(Key{"sky"}, 0.0) > 0.5) {
                // The sky is at no distance a plane can cut: it is the room's
                // ceiling, whatever bounds its ground.
                scene_->set("uClipCount", 0);
                draw_sky(cam, zfar);
                scene_->set("uClipCount", bounds);
            } else {
                draw_room(room);
            }
            for (const auto& e : room.elements()) {
                if (!e.alive) continue;
                if (e.kind == terrain_kind()) {
                    draw_terrain(e);
                } else if (e.kind == kinds::mesh) {
                    if (!sees(view, box_matrix(room, e))) continue;
                    if (instanceable(e)) batch_crate(room, e);
                    else draw_crate(room, e);
                } else if (e.kind == kinds::wall) {
                    if (!sees(view, box_matrix(room, e))) continue;
                    if (q_.instancing && e.id != highlight_)
                        batch(cube_, box_matrix(room, e).m, color_of(e, {0.52f, 0.50f, 0.48f}), 0.9f,
                              static_cast<float>(e.params.num(Key{"surface"}, 2.0)), 0, 0, 0);
                    else
                        draw_wall_element(room, e);
                } else if (e.kind == kinds::light) {
                    draw_lamp(room, e);
                }
            }
            flush_batches(*scene_, true);
            for (const auto& e : room.elements()) {
                if (e.kind != kinds::portal || !e.alive) continue;
                // The doorway being looked through keeps its frame; only its
                // view is left out - seen from its own far side it would fill
                // the whole picture.
                draw_portal(room, e, depth, target, !skip_portal.empty() && e.id == skip_portal);
            }
        }
        for (int i = 0; i < kMaxBounds; ++i)
            gl::glDisable(gl::GL_CLIP_DISTANCE0 + static_cast<gl::GLenum>(i));
        set_frame(Pose{});
    }

    // Where each element is, and each box's matrix: the shadow passes, the
    // scene and any view through a portal all ask, every frame. Kept from
    // frame to frame, and worked out again only when the element's
    // parameters, or those of what it hangs off, have changed - each change
    // is a new stamp, so their stamps together say whether it could have.
    struct Placed {
        uint64_t stamp = 0;
        bool posed = false, boxed = false, recorded = false, hashed = false;
        uint64_t where = 0;  // its box and its shape, as a shadow map sees it
        Pose pose;
        RoomMatrix box;
        std::array<float, gl::Mesh::kInstanceFloats> record{};  // as a batch carries it
    };
    static uint64_t chain_stamp(const State& st, const Element& e) {
        uint64_t h = e.params.stamp();
        const Element* cur = &e;
        for (int i = 0; i < 8; ++i) {
            if (!cur->params.has(keys::parent)) break;
            const std::string* parent_id = std::get_if<std::string>(&cur->params.get(keys::parent));
            if (!parent_id || parent_id->empty()) break;
            const Element* parent = st.find(Key{*parent_id});
            if (!parent) break;
            h = (h * 1099511628211ULL) ^ parent->params.stamp();
            cur = parent;
        }
        return h;
    }
    Placed& placed_of(const State& st, const Element& e) const {
        Placed& p = placed_[&e];
        const uint64_t stamp = chain_stamp(st, e);
        if (p.stamp != stamp) p = Placed{stamp};
        return p;
    }
    Pose pose_of(const State& st, const Element& e) const {
        Placed& p = placed_of(st, e);
        if (!p.posed) p.pose = world_pose(st, e), p.posed = true;
        return p.pose;
    }
    const RoomMatrix& box_matrix(const State& st, const Element& e) const {
        Placed& p = placed_of(st, e);
        if (!p.boxed) p.box = box_model(st, e), p.boxed = true;
        return p.box;
    }
    mutable std::unordered_map<const Element*, Placed> placed_;
    mutable std::unordered_map<const Element*, std::pair<uint64_t, const gl::Mesh*>> shape_memo_;  // each thing's mesh, by its stamp

    static Key terrain_kind() {
        static const Key k{"terrain"};
        return k;
    }

    // The sky: a sphere round the viewer, inside the far plane, unlit.
    void draw_sky(const Camera& cam, float zfar) {
        const gl::Mat4 m = gl::Mat4::translate(cam.eye) * gl::Mat4::scale(gl::Vec3{1, 1, 1} * (zfar * 1.8f));
        scene_->set("uModel", m);
        scene_->set("uTexModel", m);
        scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
        scene_->set("uRoughness", 1.0f);
        scene_->set("uSurface", 9.0f);
        scene_->set("uEmissive", 0.0f);
        scene_->set("uHighlight", 0.0f);
        scene_->set("uTexMix", 0.0f);
        scene_->set("uGlow", 0.0f);
        sphere_.draw();
    }

    // The ground round (cx, cz): `n` cells across, `step` metres apart at the
    // middle, widening to `reach`. Sampled on every core at once.
    static std::vector<float> sample_ground(const std::function<double(double, double)>& height, int n, double reach,
                                            double step, double cx, double cz) {
        const int half = n / 2;
        const double grow = std::max(0.0, (reach - step * half) / (static_cast<double>(half) * half));
        std::vector<double> off(static_cast<std::size_t>(n + 1));
        for (int i = 0; i <= n; ++i) {
            const double k = i - half, a = std::fabs(k);
            off[static_cast<std::size_t>(i)] = (k < 0 ? -1.0 : 1.0) * (step * a + grow * a * a);
        }
        const auto at = [n](int i, int j) { return static_cast<std::size_t>(j) * static_cast<std::size_t>(n + 1) + static_cast<std::size_t>(i); };
        std::vector<double> hgt(static_cast<std::size_t>((n + 1) * (n + 1)));
        const int workers = static_cast<int>(std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
        std::vector<std::thread> pool;
        for (int w = 0; w < workers; ++w)
            pool.emplace_back([&, w] {
                for (int j = w; j <= n; j += workers)
                    for (int i = 0; i <= n; ++i)
                        hgt[at(i, j)] = height(cx + off[static_cast<std::size_t>(i)], cz + off[static_cast<std::size_t>(j)]);
            });
        for (std::thread& th : pool) th.join();
        std::vector<gl::Vec3> nrm(hgt.size());
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i) {
                const int i0 = std::max(0, i - 1), i1 = std::min(n, i + 1);
                const int j0 = std::max(0, j - 1), j1 = std::min(n, j + 1);
                const double dx = (hgt[at(i1, j)] - hgt[at(i0, j)]) / (off[static_cast<std::size_t>(i1)] - off[static_cast<std::size_t>(i0)]);
                const double dz = (hgt[at(i, j1)] - hgt[at(i, j0)]) / (off[static_cast<std::size_t>(j1)] - off[static_cast<std::size_t>(j0)]);
                nrm[at(i, j)] = gl::normalize({static_cast<float>(-dx), 1.0f, static_cast<float>(-dz)});
            }
        std::vector<float> v;
        v.reserve(static_cast<std::size_t>(n) * n * 6 * 8);
        const auto put = [&](int i, int j) {
            const std::size_t k = at(i, j);
            const float x = static_cast<float>(cx + off[static_cast<std::size_t>(i)]);
            const float z = static_cast<float>(cz + off[static_cast<std::size_t>(j)]);
            v.insert(v.end(), {x, static_cast<float>(hgt[k]), z, nrm[k].x, nrm[k].y, nrm[k].z, x * 0.1f, z * 0.1f});
        };
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                put(i, j);
                put(i, j + 1);
                put(i + 1, j + 1);
                put(i, j);
                put(i + 1, j + 1);
                put(i + 1, j);
            }
        return v;
    }

    // Keep the ground centred on the viewer. When they have moved a grid step
    // the ground is resampled in the background, and the ground already there
    // is drawn until it is ready - it is only a step off centre, and the frame
    // never waits for it. Only with no ground yet, or after the element asks
    // (`rev`, as when a desert shifts its origin), does the frame wait.
    void ensure_terrain(const Element& e, const Camera& cam) {
        auto it = terrains_.find(e.id);
        if (it == terrains_.end() || !it->second.height) return;
        TerrainMesh& t = it->second;
        const int n = std::max(16, std::min(400, static_cast<int>(e.params.num(Key{"cells"}, 160.0)))) / 2 * 2;
        const double reach = e.params.num(Key{"reach"}, 320.0);
        const double step = e.params.num(Key{"step"}, 0.5);
        const double snap = e.params.num(Key{"snap"}, 2.0);
        const double rev = e.params.num(Key{"rev"}, 0.0);
        const double cx = std::floor(cam.eye.x / snap) * snap, cz = std::floor(cam.eye.z / snap) * snap;

        // A finished resample goes in, if it is still the ground wanted.
        if (t.job.valid() && t.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            std::vector<float> v = t.job.get();
            if (t.job_rev == rev) {
                t.mesh.update(v);
                t.cx = t.job_cx;
                t.cz = t.job_cz;
                t.rev = t.job_rev;
            }
        }
        if (t.mesh.valid() && cx == t.cx && cz == t.cz && rev == t.rev) return;
        if (!t.mesh.valid() || rev != t.rev) {
            // Nothing sensible to draw meanwhile: wait for it.
            if (t.job.valid()) t.job.wait();
            t.mesh.update(sample_ground(t.height, n, reach, step, cx, cz));
            t.cx = cx;
            t.cz = cz;
            t.rev = rev;
            return;
        }
        if (t.job.valid()) return;  // one at a time; the next frame asks again
        t.job_cx = cx;
        t.job_cz = cz;
        t.job_rev = rev;
        t.job = std::async(std::launch::async, [height = t.height, n, reach, step, cx, cz] {
            return sample_ground(height, n, reach, step, cx, cz);
        });
    }

    void draw_terrain(const Element& e) {
        auto it = terrains_.find(e.id);
        if (it == terrains_.end() || !it->second.mesh.valid()) return;
        set_model(room_local(gl::Mat4::identity()));
        scene_->set("uAlbedo", color_of(e, {0.80f, 0.62f, 0.42f}));
        scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.85)));
        scene_->set("uSurface", static_cast<float>(e.params.num(Key{"surface"}, 8.0)));
        scene_->set("uEmissive", 0.0f);
        scene_->set("uHighlight", 0.0f);
        scene_->set("uTexMix", 0.0f);
        scene_->set("uGlow", 0.0f);
        it->second.mesh.draw();
    }

    // A doorway (a portal bound to another room) has no solid frame in the
    // shadow pass: light should pass between the rooms.
    bool is_doorway(const Element& e) const {
        auto it = worlds_.find(e.id);
        return it != worlds_.end() && it->second.world != nullptr;
    }

    // A mesh is a box unless it says otherwise: `shape` = "cylinder" (standing
    // on y; sx and sz are its diameters) or "sphere". All three share the same
    // unit size, so the same model matrix places any of them.
    //
    // A box may have `bevel`: its edges rounded to that many metres, as made
    // things' edges are, so they catch the light. A box or a cylinder may
    // have `taper`: the top that fraction of the bottom's width (a lamp's
    // shade, the back of a tube). Those are made once for each size and kept.
    const gl::Mesh& shape_of(const Element& e) const {
        if (e.kind != kinds::mesh) return cube_;
        // Asked every frame, in every pass, of every thing: remembered until
        // the thing's parameters change.
        auto& memo = shape_memo_[&e];
        if (memo.first == e.params.stamp() && memo.second) return *memo.second;
        const gl::Mesh& m = find_shape(e);
        memo = {e.params.stamp(), &m};
        return m;
    }
    const gl::Mesh& find_shape(const Element& e) const {
        static const Key shape{"shape"}, bevel{"bevel"}, taper{"taper"};
        const std::string s = e.params.get_or<std::string>(shape, "");
        const double tp = e.params.num(taper, 1.0);
        if (s == "sphere") return sphere_;
        if (s == "cylinder") {
            if (tp == 1.0) return cylinder_;
            const std::string key = "c" + std::to_string(std::lround(tp * 1000));
            gl::Mesh& m = shaped_[key];
            if (!m.valid()) m.create(gl::cylinder_vertices(28, static_cast<float>(tp)));
            return m;
        }
        const double r = e.params.num(bevel, 0.0);
        if (r <= 0.0 && tp == 1.0) return cube_;
        // Sizes to the millimetre: near enough the same box is the same mesh.
        const auto mm = [](double v) { return std::to_string(std::lround(v * 1000)); };
        const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
        const std::string key = "b" + mm(sx) + "," + mm(sy) + "," + mm(sz) + "," + mm(r) + "," + mm(tp);
        gl::Mesh& m = shaped_[key];
        if (!m.valid())
            m.create(gl::rounded_box_vertices(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz),
                                              static_cast<float>(r), static_cast<float>(tp)));
        return m;
    }

    // Boxes sit on the floor: y is the base, not the centre. The pose comes from
    // world_pose, so an anchored element follows its group for free. A mesh may
    // be tipped (pitch) and turned about its own length (roll) as a panel is,
    // pivoting on its centre; untilted, it stands on its base as always.
    RoomMatrix box_model(const State& st, const Element& e) const {
        const gl::Vec3 s{static_cast<float>(e.params.num(keys::sx, 1.0)),
                         static_cast<float>(e.params.num(keys::sy, 1.0)),
                         static_cast<float>(e.params.num(keys::sz, 1.0))};
        const Pose w = pose_of(st, e);
        const gl::Vec3 p{static_cast<float>(w.position.x),
                         static_cast<float>(w.position.y) + s.y * 0.5f,
                         static_cast<float>(w.position.z)};
        const float pitch = static_cast<float>(e.params.num(keys::pitch));
        const float roll = static_cast<float>(e.params.num(keys::roll));
        gl::Mat4 turn = gl::Mat4::rotate_y(static_cast<float>(w.yaw));
        if (pitch != 0.0f || roll != 0.0f) turn = turn * gl::Mat4::rotate_z(pitch) * gl::Mat4::rotate_x(roll);
        return room_local(gl::Mat4::translate(p) * turn * gl::Mat4::scale(s));
    }

    // A panel hangs upright unless it says otherwise: `pitch` tips its face up
    // (a sheet lying on a desk faces the ceiling at pitch = pi/2) and `roll`
    // turns it in its own plane (a photo pinned crooked). Doorways ignore both;
    // they are hinged on the vertical, like the walk through them.
    static gl::Mat4 panel_turn(const Pose& pose, const Element& e) {
        return gl::Mat4::rotate_y(static_cast<float>(pose.yaw)) *
               gl::Mat4::rotate_z(static_cast<float>(e.params.num(keys::pitch))) *
               gl::Mat4::rotate_x(static_cast<float>(e.params.num(keys::roll)));
    }

    // The way a panel's face points: its heading, raised by its pitch.
    static gl::Vec3 panel_normal(const Pose& pose, const Element& e) {
        const double p = e.params.num(keys::pitch);
        const Vec3d f = heading(pose.yaw);
        return to_vec3({f.x * std::cos(p), std::sin(p), f.z * std::cos(p)});
    }

    // A framed panel (the default) is mounted on a board; `frame = 0` makes it
    // a bare sheet - paper, a print - `thick` metres thick, in its own r/g/b.
    static bool framed(const Element& e) { return e.params.num(Key{"frame"}, 1.0) > 0.5; }
    static float sheet_thickness(const Element& e) {
        return static_cast<float>(e.params.num(Key{"thick"}, 0.004));
    }

    RoomMatrix portal_frame_model(const State& st, const Element& e) const {
        const Pose pose = pose_of(st, e);
        const float w = static_cast<float>(e.params.num(keys::w, 3.0));
        const float h = static_cast<float>(e.params.num(keys::h, 2.0));
        const float border = static_cast<float>(e.params.num(Key{"border"}, 0.15));
        const gl::Vec3 size = framed(e) ? gl::Vec3{0.08f, h + 2 * border - 0.08f, w + 2 * border - 0.08f}
                                        : gl::Vec3{sheet_thickness(e), h, w};
        return room_local(gl::Mat4::translate(to_vec3(pose.position)) * panel_turn(pose, e) *
                          gl::Mat4::scale(size));
    }

    // `local` is in the room's own coordinates. The room's placement is applied
    // here, once, and the unplaced matrix goes to the shader as well so that
    // procedural surfaces stay put when the viewer changes rooms.
    void draw_solid(const RoomMatrix& local, const gl::Vec3& albedo, float roughness,
                    float surface, float emissive = 0.0f, float highlight = 0.0f) {
        ++times_.draws;
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
    // What the camera can see: the six planes of its view, each as ax + by +
    // cz + d >= 0 inside (read off the view-projection's rows).
    struct Frustum {
        float plane[6][4];
    };
    static Frustum frustum_of(const gl::Mat4& vp) {
        Frustum f{};
        const auto row = [&](int i, int k) { return vp.m[k * 4 + i]; };
        for (int p = 0; p < 6; ++p) {
            const int axis = p / 2;
            const float sign = p % 2 ? -1.0f : 1.0f;
            float len = 0;
            for (int k = 0; k < 4; ++k) {
                f.plane[p][k] = row(3, k) + sign * row(axis, k);
                if (k < 3) len += f.plane[p][k] * f.plane[p][k];
            }
            len = std::sqrt(len);
            if (len > 0)
                for (int k = 0; k < 4; ++k) f.plane[p][k] /= len;
        }
        return f;
    }
    // Whether anything of a box - the unit cube `local` places, in this
    // room's frame - can be in view. A ball round it, a little generous, is
    // tested: what is wholly outside the view is not drawn, which is most of
    // a room when you lean into a screen.
    bool sees(const Frustum& f, const RoomMatrix& local) const {
        const gl::Mat4 w = frame_matrix_ * local.m;
        const float cx = w.m[12], cy = w.m[13], cz = w.m[14];
        float r2 = 0;
        for (int c = 0; c < 3; ++c) r2 += w.m[c * 4] * w.m[c * 4] + w.m[c * 4 + 1] * w.m[c * 4 + 1] + w.m[c * 4 + 2] * w.m[c * 4 + 2];
        const float r = 0.5f * std::sqrt(r2) * 1.5f + 0.05f;
        for (const auto& p : f.plane)
            if (p[0] * cx + p[1] * cy + p[2] * cz + p[3] < -r) return false;
        return true;
    }

    // The one place a room's placement is applied.
    void set_model(const RoomMatrix& local) {
        scene_->set("uModel", frame_matrix_ * local.m);
        scene_->set("uTexModel", local.m);
    }

    void draw_room(const Spatial3D& world) {
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

        // `floor_surface` and `ceiling_surface` pick their materials (tiles
        // and plaster if not said); `ceiling_r/g/b` its colour.
        const float floor_s = static_cast<float>(world.params().num(Key{"floor_surface"}, 1.0));
        const float ceil_s = static_cast<float>(world.params().num(Key{"ceiling_surface"}, 2.0));
        const gl::Vec3 ceil_c = world.params().has(Key{"ceiling_r"})
                                    ? gl::Vec3{static_cast<float>(world.params().num(Key{"ceiling_r"})),
                                               static_cast<float>(world.params().num(Key{"ceiling_g"})),
                                               static_cast<float>(world.params().num(Key{"ceiling_b"}))}
                                    : wall_c * 0.5f;
        draw_solid(room_local(gl::Mat4::translate({w / 2, -t / 2, d / 2}) *
                       gl::Mat4::scale({w, t, d})),
                   floor_c, 0.55f, floor_s);
        draw_solid(room_local(gl::Mat4::translate({w / 2, h + t / 2, d / 2}) *
                       gl::Mat4::scale({w, t, d})),
                   ceil_c, 0.95f, ceil_s);

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
        // `surface` picks a wall's material; plaster if it does not say.
        draw_solid(box_matrix(st, e), color_of(e, {0.52f, 0.50f, 0.48f}), 0.9f, static_cast<float>(e.params.num(Key{"surface"}, 2.0)));
    }

    // `surface` picks the material: 3 (the default) crate planks, 4 wood,
    // 5 brushed metal, 6 moulded plastic, 7 fabric, 0 plain.
    // --- batches: things of one shape, drawn in one call --------------------------------
    struct Batch {
        const gl::Mesh* mesh;
        std::vector<float> data;  // gl::Mesh::kInstanceFloats a thing
    };
    // A thing is drawn with the others of its shape unless it wears a skin (a
    // surface bound to it) or is being pointed at.
    bool instanceable(const Element& e) const {
        if (!q_.instancing || e.id == highlight_) return false;
        const auto skin = surfaces_.find(e.id);
        return skin == surfaces_.end() || !skin->second.surface;
    }
    void batch_crate(const State& st, const Element& e) {
        Placed& p = placed_of(st, e);
        if (!p.recorded) {
            const gl::Mat4& m = box_matrix(st, e).m;
            const gl::Vec3 c = color_of(e, {0.8f, 0.5f, 0.25f});
            std::copy(m.m, m.m + 16, p.record.begin());
            const float mat[8] = {c.x, c.y, c.z, static_cast<float>(e.params.num(Key{"roughness"}, 0.6)),
                                  static_cast<float>(e.params.num(Key{"surface"}, 3.0)),
                                  static_cast<float>(e.params.num(Key{"emissive"}, 0.0)), 0.0f,
                                  static_cast<float>(e.params.num(Key{"mirror"}, 0.0))};
            std::copy(mat, mat + 8, p.record.begin() + 16);
            p.recorded = true;
        }
        Batch& b = batch_for(shape_of(e));
        b.data.insert(b.data.end(), p.record.begin(), p.record.end());
    }
    Batch& batch_for(const gl::Mesh& mesh) {
        for (Batch& x : batches_)
            if (x.mesh == &mesh) return x;
        return batches_.emplace_back(Batch{&mesh, {}});
    }
    // One more of `mesh` to draw, at `local` in the room being drawn.
    void batch(const gl::Mesh& mesh, const gl::Mat4& local, const gl::Vec3& albedo, float roughness, float surface,
               float emissive, float highlight, float mirror) {
        Batch& b = batch_for(mesh);
        b.data.insert(b.data.end(), local.m, local.m + 16);
        b.data.insert(b.data.end(), {albedo.x, albedo.y, albedo.z, roughness, surface, emissive, highlight, mirror});
    }
    // Everything batched, drawn: a call for each shape, in the room's frame.
    void flush_batches(const gl::Program& p, bool scene) {
        bool any = false;
        for (Batch& b : batches_) {
            if (b.data.empty()) continue;
            if (!any) {
                p.set("uInstanced", 1);
                p.set("uFrame", frame_matrix_);
                if (scene) {
                    p.set("uTexMix", 0.0f);
                    p.set("uGlow", 0.0f);
                    p.set("uSkin", 0.0f);
                    p.set("uScreenUV", 0.0f);
                    p.set("uCRT", 0.0f);
                }
                any = true;
            }
            const auto n = static_cast<gl::GLsizei>(b.data.size() / gl::Mesh::kInstanceFloats);
            b.mesh->draw_instanced(instances_.upload(b.data), n);
            if (scene) ++times_.draws, times_.instanced += n;
            b.data.clear();
        }
        if (any) p.set("uInstanced", 0);
    }

    void draw_crate(const State& st, const Element& e) {
        ++times_.draws;
        set_model(box_matrix(st, e));
        scene_->set("uAlbedo", color_of(e, {0.8f, 0.5f, 0.25f}));
        scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.6)));
        scene_->set("uSurface", static_cast<float>(e.params.num(Key{"surface"}, 3.0)));
        scene_->set("uEmissive", static_cast<float>(e.params.num(Key{"emissive"}, 0.0)));
        scene_->set("uHighlight", e.id == highlight_ ? 1.0f : 0.0f);
        scene_->set("uGlow", 0.0f);
        // `mirror`: how much of the real sky it reflects, rather than the
        // light from all round - glossy stone, still water, under a sky.
        const float mirror = static_cast<float>(e.params.num(Key{"mirror"}, 0.0));
        scene_->set("uMirror", mirror);
        // A surface bound to a mesh is its skin: an atlas, a cell a face.
        auto skin = surfaces_.find(e.id);
        if (skin != surfaces_.end() && skin->second.surface) {
            BoundSurface& bound = skin->second;
            Surface2D& surf = *bound.surface;
            const auto& pixels = surf.raster();
            if (!bound.texture.valid() || bound.texture.width() != surf.px_w() || bound.texture.height() != surf.px_h()) {
                bound.texture.create(surf.px_w(), surf.px_h(), /*mipmaps=*/true, surf.srgb());
                bound.revision = ~uint64_t{0};
            }
            if (bound.revision != surf.revision()) {
                bound.texture.upload(pixels);
                bound.revision = surf.revision();
            }
            bound.texture.bind(0);
            scene_->set("uTexMix", 1.0f);
            scene_->set("uSkin", 1.0f);
            scene_->set("uScreenUV", 0.0f);
            scene_->set("uCRT", 0.0f);
            shape_of(e).draw();
            scene_->set("uSkin", 0.0f);
            scene_->set("uTexMix", 0.0f);
            if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
            return;
        }
        scene_->set("uTexMix", 0.0f);
        shape_of(e).draw();
        if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
    }

    // A lamp hangs from the ceiling in a housing, unless `fixture` is 0: then
    // it is only light, for a lamp whose body is modelled elsewhere.
    void draw_lamp(const Spatial3D& world, const Element& e) {
        if (e.params.num(Key{"fixture"}, 1.0) < 0.5) return;
        const gl::Vec3 pos = to_vec3(pose_of(world, e).position);
        const gl::Vec3 color = color_of(e, {1.0f, 0.93f, 0.82f});
        const float room_h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));

        draw_solid(room_local(gl::Mat4::translate({pos.x, pos.y + 0.12f, pos.z}) *
                       gl::Mat4::scale({0.62f, 0.22f, 0.62f})),
                   {0.12f, 0.11f, 0.10f}, 0.4f, 0.0f);
        // The shade glows while the lamp is on, and is only glass when it is off.
        const float lit = static_cast<float>(std::min(1.0, e.params.num(keys::intensity, 1.0) * 2.0));
        draw_solid(room_local(gl::Mat4::translate(pos) * gl::Mat4::scale({0.30f, 0.16f, 0.30f})), color, 0.2f,
                   0.0f, 6.0f * lit + 0.02f);
        const float stem = std::max(0.05f, room_h - pos.y - 0.2f);
        draw_solid(room_local(gl::Mat4::translate({pos.x, pos.y + 0.2f + stem * 0.5f, pos.z}) *
                       gl::Mat4::scale({0.035f, stem, 0.035f})),
                   {0.09f, 0.09f, 0.10f}, 0.8f, 0.0f);
    }

    void draw_portal(const State& st, const Element& e, int depth, const gl::RenderTarget& target,
                     bool frame_only = false) {
        // A portal with nothing bound to it is a marker for a plain opening -
        // the gap between wall segments is the doorway, and it needs no
        // geometry of its own.
        if (!has_surface(e) && !is_doorway(e)) return;

        const Pose pose = pose_of(st, e);
        const gl::Vec3 pos = to_vec3(pose.position);
        const float w = static_cast<float>(e.params.num(keys::w, 3.0));
        const float h = static_cast<float>(e.params.num(keys::h, 2.0));
        const float yaw = static_cast<float>(pose.yaw);
        const bool open = e.params.get_or<bool>(keys::open, false);
        const gl::Vec3 n = to_vec3(heading(yaw));  // the domain decides what yaw means

        auto world_it = worlds_.find(e.id);
        const bool is_window = world_it != worlds_.end() && world_it->second.world != nullptr;

        // An open panel's frame lights up - unless the panel states its own
        // glow, as a blackboard does: then only when pointed at.
        const bool says_glow = e.params.has(Key{"glow"});
        const float hi = ((open && !says_glow) || e.id == highlight_) ? 1.0f : 0.0f;
        if (is_window) {
            // A doorway is cased on four sides, never backed: the opening has to
            // stay clear or there is nothing to see through. `casing` is how
            // wide the frame is, `depth` how deep, r/g/b its colour.
            const gl::Vec3 casing = color_of(e, {0.24f, 0.22f, 0.20f});
            const float t = static_cast<float>(e.params.num(Key{"casing"}, 0.22));
            const float d = static_cast<float>(e.params.num(Key{"depth"}, 0.34));
            const gl::Vec3 tangent = to_vec3(across(yaw));
            const gl::Mat4 rot = gl::Mat4::rotate_y(yaw);
            if (t > 0.0f) {
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
            }
        } else if (framed(e)) {
            // A panel hangs on the wall, so it keeps its backing frame.
            // `border` is how far the board shows round the panel.
            const float border = static_cast<float>(e.params.num(Key{"border"}, 0.15));
            draw_solid(room_local(gl::Mat4::translate(pos) * panel_turn(pose, e) *
                           gl::Mat4::scale({0.12f, h + 2 * border, w + 2 * border})),
                       {0.14f, 0.11f, 0.08f}, 0.6f, 0.0f, 0.0f, hi);
        } else {
            // A bare sheet: its own body, so it has an edge and a back.
            draw_solid(room_local(gl::Mat4::translate(pos) * panel_turn(pose, e) *
                           gl::Mat4::scale({sheet_thickness(e), h, w})),
                       color_of(e, {0.92f, 0.90f, 0.86f}), 0.85f, 0.0f, 0.0f, hi);
        }

        if (is_window) {
            if (frame_only) return;
            // Seen through another portal, a screen is not there at all: what
            // it projects is already what lies beyond it.
            if (depth > 0 && is_screen(e)) return;
            WorldPortal& wp = world_it->second.shared ? *world_it->second.shared : world_it->second;
            // `inset` is how far in front of the portal's plane the view is
            // drawn; a doorway walked through wants it on the plane (0).
            const float inset = static_cast<float>(e.params.num(Key{"inset"}, 0.06));
            // Where the viewer is, in front of the doorway (> 0) or behind it.
            const Vec3d eye_here = rotate_xz({cam_eye_.x - frame_.position.x, cam_eye_.y - frame_.position.y,
                                               cam_eye_.z - frame_.position.z},
                                              -frame_.yaw);
            const float side = gl::dot(to_vec3(eye_here) - (pos + n * inset), n);
            // A `oneway` doorway, seen from behind, is only its frame: what is
            // behind it is what you see through it.
            if (side < 0.0f && e.params.num(Key{"oneway"}, 0.0) > 0.5) return;
            if (depth > 0 || !wp.target.valid()) {
                // One level deep: a window seen through a window is just glass.
                set_model(room_local(gl::Mat4::translate(pos + n * inset) * gl::Mat4::rotate_y(yaw) *
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
            set_model(room_local(gl::Mat4::translate(pos + n * inset) * gl::Mat4::rotate_y(yaw) *
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
            // Stepping through, the eye comes nearer the doorway than the near
            // plane and the quad is cut away. For those last few centimetres
            // (`tunnel` > 0) the same view is drawn again just past the near
            // plane, behind the doorway, on the opening's own outline as seen
            // from the eye - so it covers exactly what the opening covers, no
            // more, and nothing about the doorway changes size as you close in.
            if (e.params.num(Key{"tunnel"}, 0.0) > 0.0 && side >= 0.0f && side < kNear * 2.0f) {
                const float back = kNear * 2.0f - side;
                const float k = (std::max(side, 0.002f) + back) / std::max(side, 0.002f);
                const gl::Vec3 plane = pos + n * inset, eye = to_vec3(eye_here);
                const gl::Vec3 centre = eye + (plane - eye) * k;
                set_model(room_local(gl::Mat4::translate(centre) * gl::Mat4::rotate_y(yaw) *
                          gl::Mat4::scale({1.0f, h * k, w * k})));
                quad_.draw();
            }
            scene_->set("uScreenUV", 0.0f);
            scene_->set("uTexMix", 0.0f);
            scene_->set("uEmissive", 0.0f);
            return;
        }

        // The picture: a world's feed, or a 2D state's pixels.
        int tex_w = 0, tex_h = 0;
        if (auto f = feeds_.find(e.id); f != feeds_.end() && f->second.world && f->second.out.valid()) {
            f->second.out.bind_color(0);
            tex_w = f->second.w, tex_h = f->second.h;
            scene_->set("uTexFlip", 1.0f);
            scene_->set("uUntone", 1.0f);
        } else {
            auto it = surfaces_.find(e.id);
            if (it == surfaces_.end() || !it->second.surface) return;  // a plain opening
            BoundSurface& bound = it->second;
            Surface2D& surf = *bound.surface;
            const auto& pixels = surf.raster();
            // A surface that has changed size gets a texture its new size.
            if (!bound.texture.valid() || bound.texture.width() != surf.px_w() || bound.texture.height() != surf.px_h()) {
                bound.texture.create(surf.px_w(), surf.px_h(), /*mipmaps=*/true, surf.srgb());
                bound.revision = ~uint64_t{0};
            }
            if (bound.revision != surf.revision()) {
                bound.texture.upload(pixels);
                bound.revision = surf.revision();
            }
            bound.texture.bind(0);
            tex_w = surf.px_w(), tex_h = surf.px_h();
        }

        // Clear of the frame slab (half-thickness 0.06), or the panel sinks into
        // it; a bare sheet's face sits just off its own body.
        const gl::Vec3 face = panel_normal(pose, e);
        const float lift = framed(e) ? 0.08f : sheet_thickness(e) * 0.5f + 0.0015f;
        set_model(room_local(gl::Mat4::translate(pos + face * lift) * panel_turn(pose, e) *
                  gl::Mat4::scale({1.0f, h, w})));
        // `glow` is how much the panel lights itself - a screen more than a
        // map, paper not at all; an open panel that does not say glows at
        // least as a map does. A stated glow is kept: a blackboard or a
        // photo, open or not, is lit only by the room.
        const float glow = static_cast<float>(e.params.num(Key{"glow"}, 0.12));
        scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
        scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.75)));
        scene_->set("uSurface", 0.0f);
        scene_->set("uEmissive", 0.0f);
        // A bare sheet has no frame to light up, so its face takes the
        // highlight - only when pointed at; an open screen is lit by its glow.
        scene_->set("uHighlight", !framed(e) && e.id == highlight_ ? 1.0f : 0.0f);
        scene_->set("uTexMix", 1.0f);
        scene_->set("uGlow", open && !says_glow ? std::max(glow, 0.55f) : glow);
        // `crt` makes the panel a screen: the glass is drawn per pixel.
        scene_->set("uCRT", static_cast<float>(e.params.num(Key{"crt"}, 0.0)));
        // `halo`: how much the tube's phosphor glows into the glass round it.
        scene_->set("uHalo", static_cast<float>(e.params.num(Key{"halo"}, 0.0)));
        // `flat`: how flat the tube is seen (crt_shape) - 1 face up to it.
        scene_->set("uFlat", static_cast<float>(e.params.num(Key{"flat"}, 0.0)));
        scene_->set("uTexSize", static_cast<float>(tex_w), static_cast<float>(tex_h));
        quad_.draw();
        scene_->set("uTexFlip", 0.0f);
        scene_->set("uUntone", 0.0f);
        scene_->set("uTexMix", 0.0f);
        scene_->set("uGlow", 0.0f);
        scene_->set("uCRT", 0.0f);
        scene_->set("uHalo", 0.0f);
        scene_->set("uFlat", 0.0f);
    }

    // Occlusion, at full resolution: the depth resolved, the occlusion
    // found and blurred along surfaces, and laid over the scene into `lit_`.
    void run_ao(float strength, float radius) {
        if (!ao_prog_) {
            ao_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_fs(), "ao");
            ao_blur_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_blur_fs(), "ao blur");
            ao_apply_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_apply_fs(), "ao apply");
        }
        gl::glDisable(gl::GL_DEPTH_TEST);
        scene_target_.blit_depth_to(depth_);
        const float tan_half = std::tan(view_.fov * 0.5f);

        ao_a_.bind();
        ao_prog_->use();
        ao_prog_->set("uDepth", 0);
        depth_.bind_depth(0);
        ao_prog_->set("uTexel", 1.0f / static_cast<float>(ao_a_.width()), 1.0f / static_cast<float>(ao_a_.height()));
        ao_prog_->set("uNear", view_.znear);
        ao_prog_->set("uFar", view_.zfar);
        ao_prog_->set("uTanHalf", tan_half);
        ao_prog_->set("uAspect", view_.aspect);
        ao_prog_->set("uRadius", radius);
        screen_.draw();

        ao_b_.bind();
        ao_blur_prog_->use();
        ao_blur_prog_->set("uAO", 0);
        ao_blur_prog_->set("uDepth", 1);
        ao_a_.bind_color(0);
        depth_.bind_depth(1);
        ao_blur_prog_->set("uTexel", 1.0f / static_cast<float>(ao_b_.width()), 1.0f / static_cast<float>(ao_b_.height()));
        ao_blur_prog_->set("uNear", view_.znear);
        ao_blur_prog_->set("uFar", view_.zfar);
        screen_.draw();

        lit_.bind();
        ao_apply_prog_->use();
        ao_apply_prog_->set("uScene", 0);
        ao_apply_prog_->set("uAO", 1);
        ao_apply_prog_->set("uStrength", strength);
        ao_apply_prog_->set("uDepth", 2);
        ao_apply_prog_->set("uNear", view_.znear);
        ao_apply_prog_->set("uFar", view_.zfar);
        ao_apply_prog_->set("uTexel", 1.0f / static_cast<float>(lit_.width()), 1.0f / static_cast<float>(lit_.height()));
        resolve_.bind_color(0);
        ao_b_.bind_color(1);
        depth_.bind_depth(2);
        screen_.draw();
        gl::glActiveTexture(gl::GL_TEXTURE0);
    }

    void run_bloom() {
        gl::glDisable(gl::GL_DEPTH_TEST);
        bloom_a_.bind();
        gl::glClear(gl::GL_COLOR_BUFFER_BIT);
        const gl::Program& bright = *program_for(post_.shown(), passes::bright);
        bright.use();
        apply_uniforms(bright, post_, passes::bright);
        bright.set("uScene", 0);
        scene_src_->bind_color(0);
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
        run_wide_bloom(static_cast<float>(setting(post_, passes::blur, "wide", 0.5)));
    }

    // The wide glow (bloom_down_fs): the tight bloom taken down the chain and
    // back up, each level adding its own, then mixed into the tight bloom by
    // `wide` - how much of the glow spreads far rather than near.
    void run_wide_bloom(float wide) {
        if (wide <= 0.0f || bloom_levels_ == 0) return;
        if (!bloom_down_) {
            bloom_down_ = std::make_unique<gl::Program>(gl::post_vs(), gl::bloom_down_fs(), "bloom down");
            bloom_up_ = std::make_unique<gl::Program>(gl::post_vs(), gl::bloom_up_fs(), "bloom up");
        }
        const auto texel = [](const gl::RenderTarget& t) {
            return std::pair{1.0f / static_cast<float>(t.width()), 1.0f / static_cast<float>(t.height())};
        };
        bloom_down_->use();
        bloom_down_->set("uSource", 0);
        const gl::RenderTarget* from = &bloom_a_;
        for (int i = 0; i < bloom_levels_; ++i) {
            bloom_chain_[i].bind();
            from->bind_color(0);
            const auto [tx, ty] = texel(*from);
            bloom_down_->set("uTexel", tx, ty);
            bloom_down_->set("uFirst", i == 0 ? 1.0f : 0.0f);
            screen_.draw();
            from = &bloom_chain_[i];
        }
        bloom_up_->use();
        bloom_up_->set("uSource", 0);
        bloom_up_->set("uWeight", 1.0f);
        gl::glEnable(gl::GL_BLEND);
        gl::glBlendFunc(gl::GL_ONE, gl::GL_ONE);
        for (int i = bloom_levels_ - 1; i > 0; --i) {
            bloom_chain_[i - 1].bind();
            bloom_chain_[i].bind_color(0);
            const auto [tx, ty] = texel(bloom_chain_[i]);
            bloom_up_->set("uTexel", tx, ty);
            screen_.draw();
        }
        // Every level has added the whole of the light once: their sum,
        // divided among them, is as bright as the tight bloom it came from.
        bloom_a_.bind();
        bloom_chain_[0].bind_color(0);
        const auto [tx, ty] = texel(bloom_chain_[0]);
        bloom_up_->set("uTexel", tx, ty);
        bloom_up_->set("uWeight", 1.0f / static_cast<float>(bloom_levels_));
        gl::glBlendColor(0.0f, 0.0f, 0.0f, std::min(wide, 1.0f));
        gl::glBlendFunc(gl::GL_CONSTANT_ALPHA, gl::GL_ONE_MINUS_CONSTANT_ALPHA);
        screen_.draw();
        gl::glDisable(gl::GL_BLEND);
    }

    // The last pass, and the one a change of look is most visible in. Every
    // program in the blend on screen runs, and they are averaged by weight, so
    // a change of shader is as continuous as a change of number: it dissolves,
    // and a dissolve turned back half way dissolves back.
    void composite(int fb_w, int fb_h) {
        if (output_) {
            output_->bind();
        } else {
            gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
            gl::glViewport(0, 0, fb_w, fb_h);
        }
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
        scene_src_->bind_color(0);
        bloom_a_.bind_color(1);

        const auto draw = [&](const gl::Program& p) {
            p.use();
            apply_uniforms(p, post_, passes::composite);
            apply_attended(p, passes::composite);
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
    // The attended state's look, over whatever was set: each scalar uniform
    // its looks name, as the blend on screen has it.
    void apply_attended(const gl::Program& p, Key pass) {
        if (!attend_) return;
        const Mix& am = mix(Key{"attend:" + attend_->id().str()}, look_of(*attend_));
        std::vector<Key> keys;
        for (const Mix::Part& part : am.parts)
            if (const Element* e = part.look->find(pass))
                for (const auto& kv : e->params)
                    if (is_uniform_key(kv.first) && kv.first.str().find('.') == std::string::npos &&
                        std::find(keys.begin(), keys.end(), kv.first) == keys.end())
                        keys.push_back(kv.first);
        for (Key k : keys) p.set(k.str().c_str(), static_cast<float>(fader_.value(am, pass, k, 0.0)));
    }

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

    // Shadow uniform names (where each map sees from, its bias), built once.
    static const char* shadow_uniform(std::size_t i, int field) {
        static const auto names = [] {
            std::array<std::array<std::string, 2>, kShadowMaps> n;
            for (std::size_t l = 0; l < kShadowMaps; ++l) {
                n[l][0] = "uShadowVP[" + std::to_string(l) + "]";
                n[l][1] = "uShadowBias[" + std::to_string(l) + "]";
            }
            return n;
        }();
        return names[i][static_cast<std::size_t>(field)].c_str();
    }

    // Light uniform names, built once: they are asked for every frame.
    static const char* light_uniform(std::size_t i, int field) {
        static const auto names = [] {
            static const char* fields[] = {"uLightPos", "uLightDir", "uLightColor", "uLightPower",
                                           "uCosInner", "uCosOuter", "uLightSun", "uLightFloor",
                                           "uLightIndirect", "uLightFalloff", "uLightGate", "uLightGateAxis"};
            std::array<std::array<std::string, 12>, kMaxLights> n;
            for (std::size_t l = 0; l < kMaxLights; ++l)
                for (int f = 0; f < 12; ++f)
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
    int msaa_ = 0;  // what the driver gave of q_.msaa
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

    gl::Mesh cube_, quad_, cylinder_, sphere_;
    mutable std::unordered_map<std::string, gl::Mesh> shaped_;  // bevelled and tapered, by size
    gl::FullscreenTriangle screen_;
    // Shadow maps, a set for each world drawn, and what each was drawn of.
    std::vector<Batch> batches_;  // kept from frame to frame, emptied as drawn
    gl::InstanceBuffer instances_;

    struct ShadowSet {
        gl::ShadowArray array;  // a layer for each light that casts, made as wanted
        uint64_t sig[kShadowMaps] = {};
    };
    std::map<std::pair<const void*, const void*>, std::unique_ptr<ShadowSet>> shadow_sets_;
    ShadowSet& shadows_for(const void* world, const void* view) {
        auto& set = shadow_sets_[{world, view}];
        if (!set) set = std::make_unique<ShadowSet>();
        return *set;
    }
    static uint64_t mix_bits(uint64_t h, float f) {
        uint32_t b = 0;
        std::memcpy(&b, &f, sizeof b);
        return (h ^ b) * 1099511628211ULL;
    }
    // Everything that casts a shadow, where it is now: its matrix, bit for
    // bit. Equal from one frame to the next, the maps from last frame stand.
    uint64_t caster_signature(const std::vector<PlacedRoom>& rooms) {
        uint64_t h = 1469598103934665603ULL;
        for (const PlacedRoom& placed : rooms) {
            if (!placed.room) continue;
            h = mix_bits(h, static_cast<float>(placed.pose.position.x));
            h = mix_bits(h, static_cast<float>(placed.pose.position.z));
            h = mix_bits(h, static_cast<float>(placed.pose.yaw));
            for (const auto& e : placed.room->elements()) {
                if (!e.alive) continue;
                if (e.kind == kinds::mesh || e.kind == kinds::wall) {
                    // Where it is and what shape: worked out once each time
                    // its parameters (or its anchor's) change - a change of
                    // colour or glow is no change to a shadow.
                    Placed& p = placed_of(*placed.room, e);
                    if (!p.hashed) {
                        // To the tenth of a millimetre: a cord settling by less
                        // than that draws no shadow again.
                        uint64_t w = 1469598103934665603ULL;
                        for (float f : box_matrix(*placed.room, e).m.m) w = mix_bits(w, std::round(f * 1e4f));
                        w = (w ^ reinterpret_cast<std::uintptr_t>(&shape_of(e))) * 1099511628211ULL;
                        p.where = w, p.hashed = true;
                    }
                    h = (h ^ reinterpret_cast<std::uintptr_t>(&e)) * 1099511628211ULL;
                    h = (h ^ p.where) * 1099511628211ULL;
                } else if (e.kind == terrain_kind()) {
                    auto t = terrains_.find(e.id);
                    if (t == terrains_.end()) continue;
                    h = mix_bits(h, static_cast<float>(t->second.cx));
                    h = mix_bits(h, static_cast<float>(t->second.cz));
                    h = mix_bits(h, static_cast<float>(t->second.rev));
                } else if (e.kind == kinds::portal && !is_doorway(e) && has_surface(e)) {
                    for (float f : portal_frame_model(*placed.room, e).m.m) h = mix_bits(h, f);
                }
            }
        }
        return h;
    }
    // Which mesh a box is drawn with (its shape, rounding and taper).
    gl::RenderTarget scene_target_, resolve_, bloom_a_, bloom_b_;
    // Where the composite writes: the screen, or a feed's picture.
    const gl::RenderTarget* output_ = nullptr;
    struct Feed {
        const Spatial3D* world = nullptr;
        int w = 0, h = 0;
        bool live = true, drawn = false;
        bool from_graph = false, seen = false;
        gl::RenderTarget out;
        std::unique_ptr<GLWorldView> view;
    };
    std::unordered_map<Key, Feed> feeds_;
    static constexpr int kBloomLevels = 5;
    gl::RenderTarget bloom_chain_[kBloomLevels];
    int bloom_levels_ = 0;
    std::unique_ptr<gl::Program> bloom_down_, bloom_up_;
    // Ambient occlusion: the resolved depth, the occlusion and its blur, and
    // the scene with it laid on. `scene_src_` is what the post chain reads.
    gl::RenderTarget depth_, ao_a_, ao_b_, lit_;
    const gl::RenderTarget* scene_src_ = &resolve_;
    std::unique_ptr<gl::Program> ao_prog_, ao_blur_prog_, ao_apply_prog_;
    struct ViewParams {
        float fov = 1.2f, aspect = 1.0f, znear = 0.05f, zfar = 120.0f;
    } view_;

    Pose frame_;              // the placement of the room currently being drawn
    gl::Mat4 frame_matrix_;   // the same thing, ready to multiply
    std::unordered_map<Key, BoundSurface> surfaces_;
    std::unordered_map<Key, WorldPortal> worlds_;
    std::unordered_map<Key, TerrainMesh> terrains_;
    gl::Vec3 cam_eye_;  // the camera of the view being drawn
    Key highlight_;
    bool timing_ = false;
    const State* attend_ = nullptr;
    FrameTimes times_;
};

}  // namespace sg::render
