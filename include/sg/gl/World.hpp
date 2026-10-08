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
// `feed_h`, 640 x 480 unless it says; `live` = 0 holds the picture; `untone`
// = 0 shows it as paint, colour lit by the room, not as light). What is
// shown is what the graph declares, and only that. The portal may belong to
// a state that is not drawn - a deck, whose output is what it plays - and a
// panel in a room shows it by `shows` = that embedding's name: a set cabled
// to the deck.
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
// sampled from a height function - the state's own (Spatial3D::terrain), or
// one bound with bind_terrain - and rebuilt around the viewer as they walk. A
// terrain that says `splat` (sg::terrain::lay) is covered by its layers. A state with `own_time` among its params keeps its own
// time: its shaders move by it (`uTime`), or its declared Temporal line - still
// when it is still. A thing with `unseen` = 1 is in no picture but casts its
// shadow (a walker's own body, seen from inside it). A light with `sun` = 1 is parallel light with an
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
// that room's own camera (a View embedding aims it) - only as much of it as
// its doorway shows on the screen; then the shadow pass, the room's lit air
// (when its look's scene pass says the air scatters, `scatter`: see air_fs),
// the scene into a multisampled HDR target - what stands in a room before its
// walls, and each batch nearest first, so what is hidden is refused by depth
// before it is shaded - resolve, ambient occlusion (when the look asks for
// it: the composite pass's `ao` setting is its strength, `ao.radius` its
// reach in metres), bright pass, blur, composite (the look's grade and curve,
// and the glow thick air spreads: film_glsl, fog_bloom_glsl), and - only when
// the look's composite pass says - its finish (gl::finish_fs): `deband`
// smooths the steps where nothing was drawn, and `smear` (`smear.blur`) lays
// the last frame shown, blurred, under this one. Both are 0 unless said, and
// then there is no such pass at all.
//
// A look's air (scene pass settings): `scatter`, how much of the light
// passing through it a metre of the air scatters towards the eye - 0, as
// every look is unless it says, gathers nothing and costs nothing;
// `scatter.ahead`, how much of that goes on ahead rather than back (-1..1,
// 0.5 unless it says); `scatter.far`, how far out from the eye it is gathered
// (the state's `far`, up to 90 m, unless it says). Each light scatters
// `scatter` times that (its own param, 1 unless it says). The air is
// gathered over cells of the view, slice by slice out from the eye, by every
// lamp and sun that lights the view, shadowed by their own maps, and dimmed
// by the fog the look already has (`uFogDensity`, `uFogStart`); the scene
// reads it at each pixel's distance. It is gathered for the view of the eye
// and the views one doorway on - seen through a doorway, only the air beyond
// it is the far world's - and gathered again only when the view, its lights
// or their maps move.
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
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <future>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sg/core/Cache.hpp"
#include "sg/domains/Light.hpp"
#include "sg/domains/Look.hpp"
#include "sg/domains/Probe.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/domains/Surface.hpp"
#include "sg/gl/Renderer.hpp"
#include "sg/gl/Shaders.hpp"
#include "sg/render/Visibility.hpp"
#include "sg/render/ViewPlan.hpp"
#include "sg/render/Defaults.hpp"

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

using GLQuality = Quality;

class GLWorldView {
public:
    static constexpr std::size_t kMaxLights = 24;   // matches the scene shader
    static constexpr std::size_t kShadowMaps = 24;  // layers of the shadow array; matches the scene shader
    static constexpr std::size_t kShadowLights = 10;  // lights with maps, and suns' close-up maps: a lamp with no cone counts once for its six layers
    static constexpr std::size_t kOwnShadows = 6;  // a room's own strongest six cast; the rest are for doorways and a sun's close-up map
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
        int shadow_casters = 0;        // and the casters they were drawn with, summed
        double signature = 0;          // ms spent seeing whether anything that casts has moved
        double lights_ms = 0, layers_ms = 0;
        double part[6] = {};
        // Work a frame did that a still world needs only once: lights read,
        // casters listed, visibility indices built, a look's uniforms found by
        // name, questions put to the graph. Each is kept until what it was
        // made from changes - cheap by construction, for any room.
        int lights_read = 0, casters_listed = 0, indices_built = 0, uniforms_resolved = 0, graph_queries = 0;
        int air_built = 0;  // views' air gathered again (air_fs): none while nothing it is made from moves  // (where a drawing's time goes, on the CPU: before the scene, its setup, rooms' setup, things, batches, doorways)  // reading the lights; going through the casters for each map
    };
    void set_timing(bool on) { timing_ = on; }
    // The state the viewer is attending to - an interface they sit at, say.
    // Its active look is laid over every room's own (the uniforms it sets win),
    // blended by the look fader like any look: so what the eyes are adjusted
    // to belongs to that state, and switching its look is how they adjust.
    void attend(const State* s) { attend_ = s; }
    const FrameTimes& times() const { return times_; }

    // Supply a transient interpolation interval. No interval accumulates into
    // shader time. Zero keeps fades still; world time is declared by Temporal.
    void set_fixed_step(double seconds) { fixed_step_ = seconds; }
    void set_frame_delta(double seconds) { fixed_step_ = seconds; }

    // The eye's exposure. A look whose scene pass says `exposure.auto` = 1
    // has the eye adjust to what it sees: the scene's light, measured each
    // frame (the log of its brightness, the middle of the view counting
    // most), is brought to `exposure.key` (0.18, mid-grey), opened or closed
    // by no more than `exposure.min` .. `exposure.max` stops (-8 .. 8), at
    // `exposure.rate` stops a second (1.5) of the interval handed in - the
    // look's own `uExposure` laid on top, as a bias. A look that says none
    // of it is drawn exactly as before. It is how the eye is, not how the
    // world is: nothing of it is written into a state or its time. Only the
    // eye's view adjusts; a screen's picture keeps its look's own exposure,
    // and a room seen through a doorway is seen with the eye's.
    //
    // Settled: the next frame takes the measured exposure at once, not eased
    // there - as the first frame the eye adjusts at all does, and as a look
    // first seen is shown as it is. A shot's first frame asks for it, so a
    // shot is the same picture however it was come to.
    void settle_exposure() { exposure_.settle = true; }
    // How many stops the eye is opened by now (0 when no look asks it to adjust).
    double exposure_stops() const { return exposure_.weight > 0.0f ? exposure_.ev : 0.0; }

    // Compile every look the graph can show - those worn by the states
    // reachable from its initial one - and check each against what this
    // renderer feeds it. Call once there is a GL context, before the first
    // frame. What comes back is what is wrong, as counterexamples; a look
    // whose shader fails is shown with the built-in one instead.
    std::vector<std::string> prepare(const StateGraph& g);

    // Attach a 2D state to a portal element: its raster becomes the texture.
    void bind_surface(Key portal_element, Surface2D* surface) {
        surfaces_[portal_element].surface = surface;
    }

    // Shafts of daylight in `room`: toward `at` (a point in the room, where
    // the light comes in), as strong as `strength`, of `colour` - aimed from
    // whatever camera the room is drawn with. Strength 0, or another room
    // drawn first, and there are none.
    void rays(const Spatial3D* room, Vec3d at, double strength, Rgb colour) {
        rays_room_ = room;
        rays_at_ = at;
        rays_strength_ = strength;
        rays_colour_ = colour;
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
    void bind_world(Key portal_element, const Spatial3D* world, Carry carry = {}, Key back = {});

    // The portal shows nothing any more: it is a plain opening again.
    void unbind_world(Key portal_element) { worlds_.erase(portal_element); }

    // Every seam of the prepared graph is bound so, both ways, by itself: its
    // doorways show the worlds they are glued to, from the eye carried by the
    // seam's own travel - made again whenever the graph changes. A seam needs
    // no binding by hand; bind_world is for what is not a seam (a projection).
    void bind_seams();
    // A ball onto a world of another scale (seam_scale), and not saying
    // `window` 0: drawn as a window onto that world, its view on the ball.
    bool ball_window(const Element& e) const;

    // Show another 3D state on a panel as a picture (see the top of this
    // file): drawn from its own camera, in its own look, `w` x `h` pixels.
    // Only drawn while the panel is in the room being drawn and in view.
    // Bound again with another world or size, it follows. Not `live`, it
    // holds the last picture it drew - a paused tape.
    void bind_feed(Key portal_element, const Spatial3D* world, int w, int h, bool live = true);
    void unbind_feed(Key portal_element);
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
    //
    // Every texture bound is packed first (sg/render/Pack.hpp), on as many
    // cores as there are and kept on disk, where the quality says and the
    // card takes it: what things wear goes to the card a quarter the size,
    // whole, before the first frame.
    void warm(const std::vector<Spatial3D*>& worlds, int fb_w, int fb_h);

    // The light probes of `room` baked (sg/domains/Probe.hpp): for each probe
    // and each of the room's lamps alone - lit at unit strength, in white,
    // nothing else glowing, no light from all round, no air - the room seen
    // every way from the probe's middle (a cube, `size` pixels a face) and
    // taken to spherical harmonics; then, `bounces` 2, again, the room lit
    // by that lamp and by what the probes saw of it, for the light that
    // comes back twice. Only the room's own lamps (not a sun, not one that
    // stands in for bounce). The renderer's work, handed back as data for
    // an edit to write (sg::bake_into): nothing of the world is changed
    // here. Needs prepare() first.
    std::vector<ProbeBake> bake_probes(const Spatial3D& room, int size = 32, int bounces = 2);

    // The same sets, with the lamps taken out of what is baked. What each
    // probe sees every way - where each surface is, which way it faces, what
    // of the light it scatters - is drawn once for the room's shell and
    // kept, lamps or none; a lamp's set is then its light on those surfaces
    // (shadowed by a cube of distances drawn from the lamp), sent back to
    // the probe - and, `bounces` 2, the probes' light on them again. A lamp
    // moved, turned or new costs its own set, and no drawing of the room
    // from the probes; one that did not move costs nothing. Needs prepare().
    // `changed`, if asked, says whether anything was worked out again.
    std::vector<ProbeBake> relight_probes(const Spatial3D& room, int size = 32, int bounces = 2, bool* changed = nullptr);

    // Light from all round, by default. A room that declares no probes (and
    // says how big it is, closed round, and not `gi` 0) has boxes of its own
    // (grid_of), relit before each frame as relight_probes relights - its
    // lamps placed are its light come back off it, with nothing declared;
    // a room's declared probes that hold no bake are relit so too. Nothing
    // of the world is written: what is shown is the renderer's, as its
    // shadows are. A box: in the room's frame, its middle, half its size,
    // how far it fades beyond each side, its turn.
    struct ProbeBox {
        Key id;
        Vec3d mid, half, soft_lo, soft_hi;
        double yaw = 0;
    };
    static std::vector<ProbeBox> grid_of(const Spatial3D& room);

    // One room, standing on its own.
    void render(const Spatial3D& world, int fb_w, int fb_h);

    // A neighbourhood of rooms, each with the pose it has when seen from the
    // first one - which is the room the viewer is standing in. Nothing here
    // treats that room as special beyond being the one asked from: hand it a
    // different root and the same geometry is drawn from the other side.
    void render(const std::vector<PlacedRoom>& rooms, int fb_w, int fb_h);

private:
    struct Camera {
        gl::Vec3 eye;
        gl::Vec3 forward{0, 0, -1};
        gl::Vec3 up{0, 1, 0};
        float fov = 1.2f;
        // Seen square on, with no depth to it: half the height of the view,
        // in metres (the camera's `ortho`); 0 sees in perspective.
        float ortho = 0.0f;
    };
    // How a camera's view is projected onto the screen: in perspective, or
    // square on.
    static gl::Mat4 projection_of(const Camera& cam, float aspect, float znear, float zfar);

    using Light = DrawLight;

    struct BoundSurface {
        Surface2D* surface = nullptr;
        gl::Texture texture;
        uint64_t revision = 0;
        // The frame it was last asked whether it changed: once a frame, however
        // many things wear it, in however many copies of a room.
        uint64_t asked = 0;
        // The picture packed for the card (warm), and which of the surface's
        // pictures it is: shown packed while the surface still shows that one.
        std::shared_ptr<const render::Packed> packed;
        uint64_t packed_revision = 0;
    };

    // Where a doorway is on the view, in -1..1 each way, and what of it a
    // view through another leaves open.
    struct Rect {
        float x0, y0, x1, y1;
        bool empty() const { return x1 <= x0 || y1 <= y0; }
        Rect cut(const Rect& o) const { return {std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)}; }
    };
    // Where a ball onto a world (ball_window) is on the screen, seen by `cam`.
    Rect ball_rect(const Spatial3D& world, const Element& e, const Camera& cam, float aspect) const;

    struct WorldPortal {
        const Spatial3D* world = nullptr;
        Carry carry;
        Key back;
        WorldPortal* shared = nullptr;  // this frame, showing another screen's view
        uint64_t drawn = 0;       // the frame its view was last drawn
        const gl::RenderTarget* shown = nullptr;  // that frame's picture of it (a view of the frame's own)
        int width = 0, height = 0;
        // Where on the screen that picture was drawn, and how much of its
        // target it fills (a ball onto a world is drawn only where it is).
        Rect seen{-1, -1, 1, 1};
        float fx = 1.0f, fy = 1.0f;
        // A glass's reflection: the room seen every way from its middle,
        // a face of it drawn again each frame in turn (capture_glass).
        gl::CubeMap env;
        gl::RenderTarget env_face;
        int env_next = 0, env_wait = 0;
        Vec3d env_at;  // where the glass was when last drawn
        // `own_look`: the far side drawn whole, in its own look, from the
        // carried eye - as it will be seen once through (its picture, and
        // the view that draws it).
        std::unique_ptr<GLWorldView> own;
        gl::RenderTarget own_out;
        bool own_drawn = false;
    };
    // A ball window's reflection of `world` round it, kept up to date.
    void capture_glass(const Spatial3D& world, const Element& e, WorldPortal& wp);

    struct TerrainMesh {
        std::function<double(double, double)> height;
        gl::Mesh mesh;
        double cx = 1e300, cz = 1e300, rev = -1;
        // A resample under way in the background, and where it is centred.
        std::future<std::vector<float>> job;
        double job_cx = 0, job_cz = 0, job_rev = -1;
    };

    // Is any of the portal in front of the camera, inside what it sees (at
    // `aspect`, width to height), and near enough to draw?
    bool in_view(const Spatial3D& world, const Element& e, const Camera& cam, float aspect = 0.0f) const;

    // The plane of `portal` (in `host`) as seen on the far side, facing away
    // from it: the far side is drawn only beyond it. The turn and shift are
    // read off the two cameras - the guest's was carried from the host's by
    // the portal's own functor, so the pair of them is that functor.
    HalfSpace far_side(const Spatial3D& host, const Element& portal, const Element& gc) const;
    // What is seen of a world, wherever it is seen from: it, and the worlds
    // round it or in it (sg::nests), where they are.
    std::vector<PlacedRoom> seen(const Spatial3D& world) const;
    Rect screen_rect(const Spatial3D& world, const Element& e, const Camera& cam, float aspect) const;
    // The views through the doorways of `world`, seen from `eye` through the
    // doorways `path` names - as deep into it as the world says
    // (`views_deep`), counted from where it was come into (`from`), and only
    // those the view before leaves open (`seen`) - never back through the
    // doorway it was come in by (`back`): planned, for draw_views to draw
    // this frame's best of.
    void view_through(const Spatial3D& world, const Element& eye, float aspect, int depth, const std::string& path, Rect seen, int from, Key back);
    // Of every view planned this frame, the biggest on the screen - as many
    // as the room in view says (`views_most`) - drawn before whatever shows it.
    void draw_views(const Spatial3D& world, float aspect);
    // A doorway whose door is shut stops the view through it: its portal says
    // so (`closed`, carried there from whatever hangs in it) and the eye is
    // not in the doorway's own thickness - walking through a shut leaf, what
    // is beyond is what is seen.
    static bool shut_to(const Element& portal, const Vec3d& eye);
    // How far either side of its plane a doorway is a wall's thickness.
    static double slab_of(const Element& portal);
    // A view through the doorway `portal`, seen on its far side through
    // `back` from `cam`: everything it shows lies past the doorway, in the
    // pyramid from the eye through it. So its near plane can stand just short
    // of the doorway (`znear` - the depth buffer spent on the room beyond, not
    // on the air before it), and what is drawn of it is culled by that
    // pyramid's sides on the CPU too (`sides`). With the eye in the
    // doorway's thickness, or through a screen or a ball, neither: kNear and
    // no sides.
    struct Through {
        float znear = kNear;
        std::vector<spatial::HalfSpace> sides;
    };
    Through through(const Element& portal, const Spatial3D& guest, const Element& back, const Camera& cam) const;
    // While a view through a doorway is drawn: the planes besides the
    // picture's own that what it draws is culled by (its pyramid's sides,
    // the doorway's cut) - the same planes the GPU clips by, used on the CPU.
    std::vector<spatial::HalfSpace> cull_;

    void set_frame(const Pose& p);

    bool has_surface(const Element& e) const;
    // The portal whose feed a panel shows: its own, or - `shows` = an
    // embedding's name - that embedding's, wherever it is hosted.
    Key signal_of(const Element& e) const;
    // The panel of `room` that shows the feed of portal `id`, if one does.
    const Element* shown_in(const State& room, Key id) const;

    static Camera camera_of(const Spatial3D& world) { return camera_of(world.camera()); }
    // Where this view sees `world` from: the eye it was handed (a camera's
    // lens, a doorway's carried eye), or the world's own camera.
    const Element& eye_of(const Spatial3D& world) const { return eye_override_ ? *eye_override_ : world.camera(); }

    static Camera camera_of(const Element& cam);

    static gl::Vec3 to_vec3(const Vec3d& v);

    static gl::Vec3 color_of(const Element& e, gl::Vec3 fallback);

    void ensure_resources();

    // The targets a picture of this size is drawn into. A view that draws a
    // feed keeps the targets of each size the picture has been made at
    // (`keep_sizes`): going from one size to another is taking the set that
    // is already made - nothing is allocated mid-frame - while any other
    // view makes them again, as the window it draws is sized.
    void ensure_targets(int w, int h);
    struct Targets;
    void swap_targets(Targets& t);
    void make_targets(int w, int h);
    void keep_sizes(const std::vector<std::pair<int, int>>& sizes);
    void release_targets();

    // The shafts' aim, from the camera of the room drawn first - if it is the
    // room they are in.
    void aim_rays(const Spatial3D& first);

    // One room's own lamps, placed as the room is.
    std::vector<Light> own_lights(const Spatial3D& room, const Pose& pose) const;

    // What comes in through the doorways of `placed`: the lamps of each world
    // a doorway opens onto, and a glow of its sky, carried into this room by
    // the doorway's own travel and let through its opening only. The travel
    // is read as a turn and a shift, as far_side() reads it: a probe standing
    // in the doorway, carried across.
    std::vector<Light> through_doorways(const PlacedRoom& placed);

    // The doorways of a room, and the light from all round beyond each, for
    // the scene shader (around_at): a thing through a doorway is lit as one -
    // with what the far room's probe nearest the doorway holds, if it has any.
    void doors_to_program(const PlacedRoom& placed);

    // A room's light probes on the scene program (up to kMaxProbes): each
    // box placed as the room is, and what its lamps give it now (or, baking
    // its second bounce, what the first saw: probe_override_).
    static constexpr int kMaxProbes = 8;  // matches the scene shader
    void probes_to_program(const PlacedRoom& placed);
    // A probe's sets, read from its params once while they stand.
    struct ProbeSets {
        uint64_t stamp = ~uint64_t{0};
        std::map<std::string, Sh9> sets;
    };
    mutable std::unordered_map<const Element*, ProbeSets> probe_sets_;
    const std::map<std::string, Sh9>& sets_of(const Element& probe) const;
    // Baking (bake_probes): this view draws its room lit by the lamp `solo_`
    // alone, at 1 in white, nothing glowing, no light from all round, none
    // through its doorways - the probes, if it says, lit as the first
    // bounce saw them.
    bool baking_ = false;
    Key solo_;
    const std::vector<Sh9>* probe_override_ = nullptr;
    std::unique_ptr<GLWorldView> baker_;  // the view that bakes, kept
    std::unique_ptr<GLWorldView> lamp_seer_, sun_seer_;  // the view that draws lamps' cubes of distances (relight_probes), kept
    // The room seen every way from `at`, lit as baking says, as harmonics.
    Sh9 capture(const Spatial3D& room, const Vec3d& at, int size);

    // Relighting (relight_probes). What the scene shader writes instead of
    // light (uSurfaceOnly): 1 where, 2 facing, 3 scattering, 4 distance.
    int surface_only_ = 0;
    // The lamp a view is drawn from (its cube of distances): its own
    // fitting is not drawn.
    Key own_lamp_;
    // One view of a room, `fov` high and wide, `size` square, as the shader
    // writes it in `mode`, nothing nearer than `znear` - into `into` at
    // (`x`, `y`), on the card: what a relight is drawn from.
    void see_into(const Spatial3D& room, const Vec3d& at, const Vec3d& forward, const Vec3d& up, float fov, int size, int mode,
                  float znear, const gl::RenderTarget& into, int x, int y);
    // A target that lets go of what it holds with whatever holds it.
    struct OwnedTarget {
        gl::RenderTarget t;
        OwnedTarget() = default;
        OwnedTarget(const OwnedTarget&) = delete;
        OwnedTarget& operator=(const OwnedTarget&) = delete;
        ~OwnedTarget() { t.destroy(); }
    };
    // And a buffer on the card the same way (what is read back through it).
    struct OwnedBuffer {
        gl::GLuint id = 0;
        OwnedBuffer() = default;
        OwnedBuffer(const OwnedBuffer&) = delete;
        OwnedBuffer& operator=(const OwnedBuffer&) = delete;
        ~OwnedBuffer();
    };
    // A lamp's light on a room's surroundings (per bounce, per box), kept
    // while it stands where it was and the shell is the same (`place`).
    struct LampLight {
        bool let_in = false;  // let in at an opening: its set is as it is, not at 1 in white
        Digest place, shell;
        std::vector<Sh9> sets;  // per box: its light come back off the room, all its bounces
        OwnedBuffer pbo;        // what is read back through, a frame on
        bool pending = false;   // worked out, not yet read back
        uint64_t done_at = 0;   // the relight it was last worked out at
        // How far it sees every way (a lamp's six faces in a row; a sun's one
        // view), drawn a face a frame for `drawing`.
        Digest drawing;
        int faces = 0;
        OwnedTarget far;
    };
    // All a room's relighting keeps (relight_room): what moves in it and
    // what stands still, its shell (a digest of all that stands still), what
    // each box sees of it, each lamp's light on that - and what is shown:
    // each box's sets, by lamp.
    struct RoomLight {
        uint64_t structure = ~uint64_t{0};
        std::vector<uint64_t> stamps;   // each thing's params, as last seen
        std::vector<uint32_t> still;    // for how many relights it has not moved
        std::unordered_map<Key, std::size_t> index;
        std::unordered_set<Key> hidden;  // what moves, and what hangs from it: not seen by the probes
        bool restless = false;           // which things move is not yet what `hidden` says
        uint32_t quiet = 0;              // relights since anything started moving or came to rest
        Digest shell;
        bool shell_known = false;
        int size = 0, bounces = 0;
        std::vector<ProbeBox> boxes;
        // What each box sees, on the card, a box a row of six faces: where
        // each surface is, which way it faces, what of the light it
        // scatters; that lit by one light (`light`), and in harmonics (`sh`).
        OwnedTarget where, facing, scatter, light, sh, sh2;  // (sh, sh2: one bounce's harmonics, the next's)
        uint64_t calls = 0;  // relights so far
        std::vector<Light> let_in_lights;  // what its openings let in, as last looked at
        uint64_t let_in_seen = 0;          // the relight that was at
        std::vector<Digest> drawn;  // per box and face: the shell that was drawn for
        std::map<std::string, LampLight> lamps;
        std::vector<std::map<std::string, Sh9>> sets;  // per box, per lamp of the room's own (at 1, in white)
        std::vector<Sh9> let_in;                       // per box, all that its openings let in, as it is
        uint64_t revision = 0;                         // moves whenever the sets do
    };
    std::unordered_map<const Spatial3D*, RoomLight> room_light_;
    std::unique_ptr<gl::Program> relight_prog_, relight_sh_prog_;
    // A light's own uniforms as light `i` of a program that lights
    // (lights_glsl): all but its shadow maps'.
    static void lamp_to(const gl::Program& p, std::size_t i, const Light& l);
    // What the views that relight do not draw (relight_room: what moves).
    const std::unordered_set<Key>* hidden_ = nullptr;
    // A room relit: its boxes' surroundings drawn for the shell as it is,
    // and each lamp's light on them worked out where it moved - all of it
    // now (`all_now`, or the first time), else a frame's share: one box
    // drawn, or one lamp worked out.
    RoomLight& relight_room(const Spatial3D& room, const std::vector<ProbeBox>& boxes, int size, int bounces, bool all_now);
    // Before a frame is drawn, each room seen relit (light_rooms), and what
    // it holds for the scene shader (live_light: the view on the screen's).
    void light_rooms(const std::vector<PlacedRoom>& rooms);
    const RoomLight* live_light(const Spatial3D& room) const;
    // The boxes of a room's declared probes, as relighting takes them.
    std::vector<ProbeBox> declared_boxes(const Spatial3D& room) const;

    // How much of a doorway's opening (half `half_w` across, `half_h` high)
    // the things of its room standing in it cover, 0 to 1: each box near the
    // opening's plane, seen square on to it - the most any one of them does
    // (a door's panels lie on its slab: covering the same, not more). A door
    // shut in its frame covers it all; swung open it is edge on, and covers
    // a sliver. `shut`: something lying flat in the opening fills it.
    float covered(const Spatial3D& room, const Element& portal, const Pose& door, float half_w, float half_h, bool& shut) const;

    // Every lamp that lights these rooms, strongest first: the first six of
    // their own get shadow maps (`shadowed` of them; one each, or six - a
    // face of a cube each - for a lamp with no cone, while the array holds
    // them), the rest light without casting. What comes in through their
    // doorways goes after the shadowed, before their fainter own.
    std::vector<Light> read_lights(const std::vector<PlacedRoom>& rooms, std::size_t& shadowed);

    // The doorway in `guest` that leads back to `host` - the one being looked
    // through. Its plane is where the portal view has to start, or the wall it
    // is set into hides everything; and it must not be drawn in that view, or
    // seen from the virtual camera it fills the whole frame.
    static bool is_screen(const Element& e) { return e.params.num(Key{"screen"}, 0.0) > 0.5; }

    static bool same_eye(const Element& a, const Element& b);

    const Element* back_portal(const Spatial3D& guest, const Spatial3D& host) const;

    // Shadow pass plus scene pass for one room, into one target. `depth` is the
    // portal recursion level: a room seen through a window does not itself open
    // further windows.

    void draw_world(const std::vector<PlacedRoom>& rooms, const Camera& cam, float aspect,
                    gl::RenderTarget& target, int depth, float znear, Key skip_portal = Key{},
                    const std::vector<HalfSpace>& clips = {});

    // Where each element is, and each box's matrix: the shadow passes, the
    // scene and any view through a portal all ask, every frame. Kept from
    // frame to frame, and worked out again only when the element's
    // parameters, or those of what it hangs off, have changed - each change
    // is a new stamp, so their stamps together say whether it could have.
    struct Placed {
        uint64_t stamp = 0;
        bool posed = false, boxed = false, recorded = false, hashed = false;
        uint64_t where = 0;  // its box and its shape, as a shadow map sees it
        // The sphere that holds it in the world, kept while its room stands where it did.
        uint64_t held_in = 0;
        gl::Vec3 held_at;
        float held_r = 0;
        Pose pose;
        RoomMatrix box;
        std::array<float, gl::Mesh::kInstanceFloats> record{};  // as a batch carries it
        // A lamp's light, read with its room placed as `lit_at` says.
        bool lit = false;
        Pose lit_at;
        DrawLight light;
    };
    static uint64_t chain_stamp(const State& st, const Element& e);
    Placed& placed_of(const State& st, const Element& e) const;
    Pose pose_of(const State& st, const Element& e) const;
    const RoomMatrix& box_matrix(const State& st, const Element& e) const;
    // What a thing hangs off, as last found: the thing and each anchor up
    // from it, with the stamp each had, in the room's structure as it was.
    // While the structure and every one of those stamps hold, the anchors
    // are the same anchors (no name in the chain was changed) and their
    // stamps together, chain_stamp's, are the same - so asking again is a
    // comparison of stamps, not a search by name.
    struct Chain {
        uint64_t structure = ~uint64_t{0};
        uint64_t stamp = 0;
        std::size_t links = 0;
        std::array<const Element*, 9> link{};
        std::array<uint64_t, 9> seen{};
    };
    // Each thing's mesh, by its stamp - and, drawn as a model, by the
    // model's making (made again, the thing's params need not change), the
    // model's name read once for the stamp.
    struct ShapeMemo {
        uint64_t stamp = 0, made = 0;
        const gl::Mesh* mesh = nullptr;
        bool modelled = false;
        Key model;
        // What the mesh was found from (find_shape reads these and nothing
        // else of the thing): a thing whose stamp moved but not these - one
        // carried, one recoloured - has the same mesh.
        std::string shape_name, model_name;
        double taper = 1.0, bevel = 0.0, sx = 1.0, sy = 1.0, sz = 1.0;
    };
    struct PlacedSlot {
        Placed placed;
        Chain chain;
        ShapeMemo shape;
    };
    uint64_t chain_stamp(const State& st, const Element& e, Chain& memo) const;
    Placed& placed_in(PlacedSlot& slot, const State& st, const Element& e) const;
    // Each thing's slot, by where the thing is: an open table of addresses
    // to slots that never move once made (asked thousands of times a frame).
    PlacedSlot& slot_of(const Element& e) const;
    mutable std::vector<std::pair<const Element*, uint32_t>> placed_index_;
    mutable std::deque<PlacedSlot> placed_;

    static Key terrain_kind() {
        static const Key k{"terrain"};
        return k;
    }

    // The sky: a sphere round the viewer, inside the far plane, unlit.
    void draw_sky(const Camera& cam, float zfar);

    // The ground round (cx, cz): `n` cells across, `step` metres apart at the
    // middle, widening to `reach`. Sampled on every core at once.
    static std::vector<float> sample_ground(const std::function<double(double, double)>& height, int n, double reach,
                                            double step, double cx, double cz);

    // Keep the ground centred on the viewer. When they have moved a grid step
    // the ground is resampled in the background, and the ground already there
    // is drawn until it is ready - it is only a step off centre, and the frame
    // never waits for it. Only with no ground yet, or after the element asks
    // (`rev`, as when a desert shifts its origin), does the frame wait.
    void ensure_terrain(const Element& e, const Camera& cam);

    void draw_terrain(const State& st, const Element& e);
    // A land's ground or water says what its picture is (`splat`): bound,
    // and the uniforms that read it set (sg::terrain); false if it says none.
    bool bind_land(const State& st, const Element& e);
    void unbind_land();

    // A doorway (a portal bound to another room) has no solid frame in the
    // shadow pass: light should pass between the rooms.
    bool is_doorway(const State& host, const Element& e) const;

    // A mesh is a box unless it says otherwise: `shape` = "cylinder" (standing
    // on y; sx and sz are its diameters) or "sphere". All three share the same
    // unit size, so the same model matrix places any of them.
    //
    // A box may have `bevel`: its edges rounded to that many metres, as made
    // things' edges are, so they catch the light. A box or a cylinder may
    // have `taper`: the top that fraction of the bottom's width (a lamp's
    // shade, the back of a tube). Those are made once for each size and kept.
    const gl::Mesh& shape_of(const State& st, const Element& e) const;
    const gl::Mesh& find_shape(const State& st, const Element& e) const;

    // Boxes sit on the floor: y is the base, not the centre. The pose comes from
    // world_pose, so an anchored element follows its group for free. A mesh may
    // be tipped (pitch) and turned about its own length (roll) as a panel is,
    // pivoting on its centre; untilted, it stands on its base as always.
    RoomMatrix box_model(const State& st, const Element& e) const;

    // A panel hangs upright unless it says otherwise: `pitch` tips its face up
    // (a sheet lying on a desk faces the ceiling at pitch = pi/2) and `roll`
    // turns it in its own plane (a photo pinned crooked). Doorways ignore both;
    // they are hinged on the vertical, like the walk through them.
    static gl::Mat4 panel_turn(const Pose& pose, const Element& e);

    // The way a panel's face points: its heading, raised by its pitch.
    static gl::Vec3 panel_normal(const Pose& pose, const Element& e);

    // A framed panel (the default) is mounted on a board; `frame = 0` makes it
    // a bare sheet - paper, a print - `thick` metres thick, in its own r/g/b.
    static bool framed(const Element& e) { return e.params.num(Key{"frame"}, 1.0) > 0.5; }
    static float sheet_thickness(const Element& e);

    RoomMatrix portal_frame_model(const State& st, const Element& e) const;

    // `local` is in the room's own coordinates. The room's placement is applied
    // here, once, and the unplaced matrix goes to the shader as well so that
    // procedural surfaces stay put when the viewer changes rooms.
    void draw_solid(const RoomMatrix& local, const gl::Vec3& albedo, float roughness,
                    float surface, float emissive = 0.0f, float highlight = 0.0f);

    static bool has_walls(const State& st);

    // A state that places its own wall elements gets only a floor and a
    // ceiling from its room_* parameters; one that does not gets the whole
    // implicit box, which is all a single-room scene needs.
    // What the camera can see: the six planes of its view, each as ax + by +
    // cz + d >= 0 inside (read off the view-projection's rows).
    using Frustum = spatial::ConvexVolume;
    static Frustum frustum_of(const gl::Mat4& vp);
    // Whether anything of a box - the unit cube `local` places, in this
    // room's frame - can be in view. A ball round it, a little generous, is
    // tested: what is wholly outside the view is not drawn, which is most of
    // a room when you lean into a screen.
    struct DrawBound {
        const Element* element=nullptr;
        uint64_t stamp=0;
        spatial::Aabb bounds;
        gl::Vec3 centre;
        float radius=0;
    };
    struct DrawPlan {
        Visibility visibility;
        std::vector<std::size_t> unbounded, portals;
        std::vector<DrawBound> bounds;
        gl::Mat4 frame;
        bool framed=false;
        std::size_t indexed=0;  // how many things its index holds: made again only when one came, went, or first moved
        // What has moved: culled each on its own, out of the index - so
        // snow falling, a thing carried, does not make the index again.
        std::vector<char> mover;
        std::vector<std::size_t> movers;
        // Each thing's slot, by its place in the room, while it is the thing
        // there (bounds[i].element): no search by address for each, each view.
        std::vector<PlacedSlot*> slots;
    };
    std::unordered_map<const Spatial3D*, DrawPlan> draw_plans_;
    // What of a room the view sees. A copy of a space that wraps (`shift`,
    // how far it is from the room as just planned) is that room moved: its
    // things are those, seen by the view moved back - nothing planned again.
    std::vector<std::size_t> plan_draws(const Spatial3D& room, const Frustum& view, const gl::Vec3* shift = nullptr);
    DrawBound query_bounds(const RoomMatrix& local) const;
    bool declared_world(const State& host, const Element& portal, const Spatial3D& guest) const;
    bool declared_feed(Key portal, const Spatial3D& guest) const;
    bool declared_surface(const Element& portal, const Surface2D& surface) const;
    struct SurfaceAccess {
        const StateGraph* graph=nullptr;
        const Surface2D* surface=nullptr;
        uint64_t revision=0, stamp=0;
        bool allowed=false;
    };
    mutable std::unordered_map<const Element*, SurfaceAccess> surface_access_;
    // The same for a world shown in a portal, and a portal's signal: asked
    // many times a frame, answered once while the graph and the portal stand.
    struct WorldAccess {
        const StateGraph* graph = nullptr;
        const State* host = nullptr;
        const Spatial3D* guest = nullptr;
        uint64_t revision = 0, stamp = 0;
        bool allowed = false;
    };
    mutable std::unordered_map<const Element*, WorldAccess> world_access_;
    struct SignalOf {
        const StateGraph* graph = nullptr;
        uint64_t revision = 0, stamp = 0;
        Key signal;
    };
    mutable std::unordered_map<const Element*, SignalOf> signal_memo_;

    // The one place a room's placement is applied.
    void set_model(const RoomMatrix& local);

    void draw_room(const Spatial3D& world);

    void draw_wall(const RoomMatrix& model, const gl::Vec3& color) {
        draw_solid(model, color, 0.9f, 2.0f);
    }

    // A wall element: level geometry placed by hand (or by an anchor), rather
    // than the implicit shell a plain room gets.
    void draw_wall_element(const State& st, const Element& e);

    // `surface` picks the material: 3 (the default) crate planks, 4 wood,
    // 5 brushed metal, 6 moulded plastic, 7 fabric, 0 plain.
    // --- batches: things of one shape, drawn in one call --------------------------------
    struct Batch {
        const gl::Mesh* mesh;
        std::vector<float> data;  // gl::Mesh::kInstanceFloats a thing
        BoundSurface* skin = nullptr;  // a texture all of them wear, each in its own frame
    };
    // A mesh wearing a texture in its own frame goes with the others of its
    // shape wearing the same one: its frame is its own matrix.
    bool batch_skinned(const State& st, const Element& e);
    // Its matrix and material, as an instance of its batch (kept until it moves).
    void append_record(const State& st, const Element& e, Batch& b);
    void upload_skin(BoundSurface& bound);
    // The surface's picture on the card as it is now: packed, while it is the
    // picture that was packed; its pixels, once it has changed.
    void refresh(BoundSurface& bound);
    // Every texture bound, packed (warm).
    void pack_skins();
    // A thing is drawn with the others of its shape unless it wears a skin (a
    // surface bound to it) or is being pointed at.
    bool instanceable(const Element& e) const;
    // What a mesh wears and in whose frame: itself, or a thing it hangs from
    // (its `parent`s) that a texture is embedded in - all its parts as one.
    const Element* skin_holder(const State& st, const Element& e) const;
    struct SkinFrame {
        uint64_t frame = ~uint64_t{0};
        gl::Mat4 to_unit;
        gl::Vec3 size{1, 1, 1};
    };
    mutable std::unordered_map<const Element*, SkinFrame> skin_frames_;
    // The room's frame to the unit box of the thing that wears a texture: a
    // mesh's own box, or the box round all of a thing's parts, in its turn.
    const SkinFrame& skin_frame(const State& st, const Element& holder) const;
    void batch_crate(const State& st, const Element& e);
    Batch& batch_for(const gl::Mesh& mesh, BoundSurface* skin = nullptr);
    // One more of `mesh` to draw, at `local` in the room being drawn.
    void batch(const gl::Mesh& mesh, const gl::Mat4& local, const gl::Vec3& albedo, float roughness, float surface,
               float emissive, float highlight, float mirror);
    // Everything batched, drawn: a call for each shape, in the room's frame.
    void flush_batches(const gl::Program& p, bool scene);
    // A batch's things in the order the eye meets them, the nearest first:
    // what stands behind is then refused by depth before it is shaded.
    void nearest_first(std::vector<float>& data);
    std::vector<std::pair<float, uint32_t>> order_;
    std::vector<float> sorted_;

    static bool is_sprite(const Element& e);
    // One of its state's pictures, made current on unit 0 - false if it keeps
    // none by that name.
    bool bind_picture(const State& st, const std::string& name, bool data = false);
    // A sprite: its picture on a flat card at its place, turned to the eye -
    // round about the upright, or (`face`) wholly, to lie square to the view.
    void draw_sprite(const State& st, const Element& e);

    void draw_crate(const State& st, const Element& e);

    // What stands half through a doorway is half in each room, and seen
    // whole from either: drawing a room, the things of the room beyond each
    // seam that admits things (Channel::Objects) whose bounds cross the
    // doorway are drawn too, carried by the seam's own transform and cut to
    // this room's side of it - as a door's leaf hangs in both (`straddle`).
    // Read from the room beyond, through the declared seam; nothing copied.
    void draw_straddlers(const PlacedRoom& placed, const Spatial3D& room);

    // A lamp hangs from the ceiling in a housing, unless `fixture` is 0: then
    // it is only light, for a lamp whose body is modelled elsewhere.
    void draw_lamp(const Spatial3D& world, const Element& e);

    void draw_portal(const State& st, const Element& e, int depth, const gl::RenderTarget& target,
                     bool frame_only = false);

    // Occlusion, at full resolution: the depth resolved, the occlusion
    // found and blurred along surfaces, and laid over the scene into `lit_`.
    void run_ao(float strength, float radius);

    void run_bloom();

    // The eye's adjustment (settle_exposure): measured from the scene's
    // light into a small picture whose mipmaps average it, read back a frame
    // late (or at once, settling), and eased to by the interval handed in.
    struct Exposure {
        gl::RenderTarget meter;
        gl::GLuint pbo[2] = {0, 0};
        bool asked[2] = {};
        int next = 0;
        double ev = 0.0, target = 0.0;  // stops opened by now, and as the last measure says
        bool known = false, settle = false;
        float weight = 0.0f;  // how much the look on screen says auto (it fades as looks do)
    };
    static constexpr int kMeterW = 128, kMeterH = 64, kMeterLevels = 8;  // 128 x 64 down to 1 x 1
    Exposure exposure_;
    std::unique_ptr<gl::Program> meter_prog_;
    // At the start of the eye's frame: what was measured last frame, eased to.
    void adapt_exposure();
    // After the scene is drawn: measured, to be read the next frame - or at
    // once, when settling.
    void meter_exposure();
    // Where a measure (the meter's last level: the weighted log, the weight) puts the eye.
    double exposure_target(const float rg[2]) const;
    // What the eye's adjustment multiplies a look's `uExposure` by in this
    // view: its own, the eye's (a doorway drawn in its own look), or none (a screen).
    float exposure_gain() const;

    // The wide glow (bloom_down_fs): the tight bloom taken down the chain and
    // back up, each level adding its own, then mixed into the tight bloom by
    // `wide` - how much of the glow spreads far rather than near.
    void run_wide_bloom(float wide);

    // The last pass, and the one a change of look is most visible in. Every
    // program in the blend on screen runs, and they are averaged by weight, so
    // a change of shader is as continuous as a change of number: it dissolves,
    // and a dissolve turned back half way dissolves back.
    void composite(int fb_w, int fb_h);

    // --- looks ------------------------------------------------------------------
    // The fading itself is GL-free and lives with the looks (LookFader); what
    // is here is only what a GL renderer does with it.
    using Mix = LookMix;

    static Key view_key() { return Key{"<view>"}; }

    // Real time by default; a fixed step makes headless frames reproducible.
    void advance_fades();

    const LookState& look_of(const State& s) const { return fader_.look_of(graph_, s); }
    Mix mix(Key who, const LookState& target) { return fader_.mix(who, target); }
    // Worlds shown last frame through a doorway in their own look, and the
    // world the viewer was in: come into one of them, its look is already on
    // the screen, and is taken at once.
    std::vector<const Spatial3D*> own_shown_, own_shown_now_;
    const Spatial3D* last_world_ = nullptr;

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
    void apply_attended(const gl::Program& p, Key pass);

    void apply_uniforms(const gl::Program& p, const Mix& m, Key pass);

    // The program for one pass of one look. Programs are shared by source, so
    // looks that only change numbers all use the built-in ones; a look's slot
    // remembers its source, so a shader edited while running is picked up.
    const gl::Program* program_for(const LookState& look, Key pass);

    const std::string& source(const LookState& look, Key pass, Key which) const;

    const gl::Program* shared_program(const std::string& vs, const std::string& fs,
                                      const std::string& tag, std::string& error);

    // Compile a look's passes and hold them to what this renderer feeds them.
    void check_look(const LookState& look, std::vector<std::string>& out);

    static const char* clip_uniform(int i);

    // Shadow uniform names (where each map sees from, its bias), built once.
    static const char* shadow_uniform(std::size_t i, int field);

    // Light uniform names, built once: they are asked for every frame.
    static const char* light_uniform(std::size_t i, int field);

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
    // Drawn as the far side of an own-look doorway: what is cut away (the
    // near side of that doorway's plane), and the doorway left out.
    std::vector<HalfSpace> own_clips_;
    Key own_skip_;
    // What bind_seams bound, and at which revision of the graph.
    std::vector<Key> seam_bound_;
    uint64_t seams_at_ = ~uint64_t{0};
    LookState standard_;
    std::unordered_map<Key, std::pair<std::string, std::string>> builtin_;
    std::unordered_map<std::string, std::unique_ptr<gl::Program>> programs_;
    // Each program's sources, and its twin for pictures cut out of their
    // cards (SG_CUTOUT): the same program, shading after the depth test.
    std::unordered_map<const gl::Program*, std::pair<std::string, std::string>> sources_of_;
    std::unordered_map<const gl::Program*, const gl::Program*> cutout_;
    const gl::Program* cutout_of(const gl::Program& p);
    std::unordered_map<Key, std::unordered_map<Key, PassProgram>> resolved_;
    LookFader fader_{standard_};
    Mix post_{{{&standard_, 1.0f}}};
    std::vector<std::pair<const gl::Program*, float>> programs_in_mix_;
    const gl::Program* scene_ = nullptr;  // the scene program currently bound
    std::vector<Key> uniform_keys_;
    std::vector<VectorUniform> vectors_;
    LookStats stats_;
    bool preparing_ = false;
    double dt_ = 0.0;
    // The time its shaders move by (`uTime`: water, clouds, stars, grain):
    // declared own_time or the world's Temporal line, so time that stands still
    // stands still in every pass too.
    double world_time_ = 0.0;
    double fixed_step_ = 0.0;

    gl::Mesh cube_, quad_, cylinder_, sphere_;
    mutable std::unordered_map<std::string, gl::Mesh> shaped_;  // bevelled and tapered, by size
    // Each model a state keeps, as a mesh: by the state and the model's
    // name, made again when the model is (by its making, `model_revision`).
    struct ModelMesh {
        uint64_t revision = 0;
        gl::Mesh mesh;
    };
    mutable std::map<std::pair<const State*, std::string>, ModelMesh> model_meshes_;
    // Each picture a state keeps, as a texture - painted again when the
    // picture is.
    struct PictureTexture {
        gl::Texture texture;
        uint64_t revision = ~uint64_t{0};
    };
    // A picture as pixel art (sampled nearest, in colour), and as data (a
    // land's shares or depths: smooth, linear) - two textures of one picture.
    std::unordered_map<const Spatial3D::Picture*, PictureTexture> picture_textures_, data_textures_;
    gl::Vec3 cam_forward_{0, 0, -1}, cam_up_{0, 1, 0};  // and which way it looks
    gl::FullscreenTriangle screen_;
    // Shadow maps, a set for each world drawn, and what each was drawn of.
    std::vector<Batch> batches_;  // kept from frame to frame, emptied as drawn
    // Which batch is whose (its mesh and its skin), by place in batches_.
    std::map<std::pair<const void*, const void*>, std::size_t> batch_index_;
    bool batch_frames_ = false;   // a doorway's frame goes with the others of its shape (a copy of a space that wraps)
    gl::InstanceBuffer instances_;
    std::vector<float> instance_stream_;  // every batch of a flush, one after another: sent at once

    struct ShadowSet {
        gl::ShadowArray array;  // a layer for each light that casts, made as wanted
        // What stands still, kept apart (a layer for each light that has
        // something moving in it): a map is this laid under what moves, so
        // a thing that moves every frame costs its own few draws and a copy,
        // not the whole room's casters drawn again.
        gl::ShadowArray still;
        uint64_t sig[kShadowMaps] = {};  // what each map holds (0: never drawn)
        gl::Mat4 vp[kShadowMaps];  // the box each map was drawn for
        uint64_t ident[kShadowMaps] = {};  // which light, as far as what a map holds goes
        uint64_t still_at[kShadowMaps] = {};  // the flip clock when what stands still was laid (0: not laid)
        uint64_t layout[kShadowMaps] = {};  // and the rooms' casters it was laid from
        uint64_t movers[kShadowMaps] = {};  // what moves, as drawn over it (0: nothing)
        bool base[kShadowMaps] = {};  // `still` holds what stands still, for this map
        int mrect[kShadowMaps][4] = {};  // the pixels of the map what moves was drawn on
        uint32_t waits[kShadowMaps] = {};  // frames each has stood out of date
        uint64_t used = 0;  // the frame a view last asked for it
        void forget();  // every map to be laid again
    };
    // A view's air, lit by its lamps (air_fs): each slice's own light (laid
    // eight to a row), the slices added up from the eye (what the scene
    // reads), and what they were gathered from - gathered again only when
    // that moves: a still view of a still world gathers them once.
    struct Air {
        gl::RenderTarget local;
        gl::LayerArray light;
        uint64_t of = 0;
        uint64_t used = 0;  // the frame a view last asked for it
        float near = 0.3f, far = 60.0f;
        // How many gatherings of what it is made from now are in it, averaged,
        // and how many there have been in all (each samples other points).
        int gathered = 0;
        uint32_t spin = 0;
    };
    static constexpr int kAirGatherings = 16;  // averaged, while nothing it is made from moves
    static constexpr int kAirTile = 16;     // pixels of the view to a cell, each way (fewer in a small view: 120 cells across)
    static constexpr int kAirSlices = 64;   // slices out from the eye, each sampled once at a point jittered within it
    static constexpr std::size_t kAirs = 6;  // views' airs kept at once
    std::map<std::pair<const void*, std::string>, std::unique_ptr<Air>> airs_;
    std::unique_ptr<gl::Program> air_prog_, air_sum_prog_;
    Air& air_for(const Spatial3D* world);

    // How many maps the views seen through doorways may still lay this
    // frame, between them: past it, a map stays as it was last laid (with the
    // box it was laid for) and waits its turn; a view whose maps were never
    // laid is lit without them until they are (`kFirstShadowMaps` more, for
    // those, that a view that has just come into sight has a few at once) - a
    // few frames, never a stall. The eye's own view lays every map it must,
    // the frame it must.
    static constexpr int kNestedShadowMaps = 6;
    static constexpr int kFirstShadowMaps = 2;
    static constexpr uint32_t kShadowWaits = 3;  // frames a map may stand out of date, in a view through a doorway
    int shadow_budget_ = kNestedShadowMaps;
    std::map<std::pair<const void*, const void*>, std::unique_ptr<ShadowSet>> shadow_sets_;
    ShadowSet& shadows_for(const void* world, const void* view);
    static void copy_depth(const gl::ShadowArray& from, const gl::ShadowArray& to, int layer, const int* rect = nullptr);
    static uint64_t mix_bits(uint64_t h, float f);
    // Everything that casts a shadow in a room, and where it is: its matrix,
    // bit for bit, and the sphere that holds it in the world being drawn.
    // Each is read again only when its own parameters (or those of what it
    // hangs off) have a new stamp - never the room's data as a whole, never
    // the camera: a creature moving, or a viewer walking, re-reads what
    // moved and nothing else. A thing that has changed lately is a mover; the
    // rest stand still, and a shadow map keeps them apart (ShadowSet::still).
    struct Caster {
        std::size_t room = 0;  // which of the rooms being drawn
        const Element* element = nullptr;
        gl::Vec3 centre;
        float radius = 0;
        float lx = 0, lz = 0;  // where it stands in its room, on the ground
        bool bounded = false;  // terrain and the like are held by nothing: they are in every map
        uint64_t where = 0;
        uint64_t stamp = 0;  // what it was last read at
        uint64_t moved = 0;  // the frame it last changed (0: not since it was listed)
        uint32_t hold = 0;   // frames it stays a mover once it stops (longer each time it starts again)
        bool chained = false;  // it hangs off another (its stamp is the chain's)
        bool alive = false, on = false;  // on: it casts (a shape, not a sprite, not shaded off)
        bool mover = false;
        PlacedSlot* slot = nullptr;  // its slot, found once with it
    };
    // A thing at rest that has changed, or come to rest: where it was, or is,
    // for the maps that laid it (`at` is the flip clock then).
    struct Flip {
        uint64_t at = 0;
        gl::Vec3 centre;
        float radius = 0;
        bool all = false;  // the list was made again
    };
    struct RoomCasters {
        uint64_t structure = ~uint64_t{0};
        uint64_t refreshed = ~uint64_t{0};  // the frame it was last read
        uint64_t used = 0;
        std::size_t room = 0;
        const State* state = nullptr;
        std::vector<Caster> list;        // meshes and walls, in element order
        std::vector<uint32_t> movers;    // of them, the ones that have changed lately
        uint64_t movers_key = 0;         // where they are
        std::vector<Caster> extras;      // terrain and panels: asked again each frame
        uint64_t extras_key = 0;
        std::vector<Flip> flips;         // newest last
        uint64_t trimmed = 0;            // flips up to here were let go
        uint64_t flip_count = 0;
    };
    static constexpr uint32_t kMoverFrames = 60;
    // How much of a doorway what stands in it covers, as last asked (covered).
    struct OccMemo {
        uint64_t key = 0;
        float value = 0;
        bool shut = false, known = false;
    };
    mutable std::unordered_map<const Element*, OccMemo> occlusion_;
    std::unordered_map<uint64_t, std::unique_ptr<RoomCasters>> room_casters_;
    uint64_t flip_clock_ = 1;  // (never 0: 0 is "not laid")
    RoomCasters& casters_in(const PlacedRoom& placed, std::size_t r);
    void refresh_casters(RoomCasters& rc, const PlacedRoom& placed);
    void flipped(RoomCasters& rc, gl::Vec3 centre, float radius, bool all);
    // What each state holds of the kinds the renderer reads on their own
    // (lamps, doorways, terrain), found again only when its structure moves.
    struct KindIndex {
        uint64_t structure = ~uint64_t{0};
        std::vector<const Element*> lights, portals, terrains, probes;
    };
    mutable std::unordered_map<const State*, KindIndex> kind_index_;
    const KindIndex& index_of(const State& s) const;
    // The lights and the casters of rooms are functions of their data (and
    // of the worlds their doorways open onto): kept by it, so the views of
    // one world in a frame - and every frame nothing changed in - read them
    // once.
    struct LightsMemo {
        std::vector<Light> lights;
        std::size_t shadowed = 0;
    };
    std::unordered_map<uint64_t, LightsMemo> lights_memo_;
    mutable std::unordered_map<const State*, std::pair<uint64_t, uint64_t>> stamps_;  // a state's data version, this frame
    uint64_t stamp_of(const State& s) const;
    // What the lights of these rooms are a function of: their lamps, their
    // doorways, and what moves across a doorway - not the room's other data.
    uint64_t lights_key(const std::vector<PlacedRoom>& rooms, const std::vector<RoomCasters*>& casters) const;
    uint64_t worlds_stamp() const;
    mutable std::pair<uint64_t, uint64_t> worlds_memo_{~uint64_t{0}, 0};  // that, and the frame it was worked out in
    // A look's uniforms as a program last took them - where each is, what it
    // was set to - kept by the look's data and its mix: set again straight
    // from this, no name looked up, while neither changes.
    struct UniformSet {
        gl::GLint at;
        int n;
        float v[3];
    };
    std::unordered_map<uint64_t, std::vector<UniformSet>> uniform_memo_;
    // A doorway's frame and casing, as drawn: kept while it stands where it did.
    struct Body {
        uint64_t stamp = ~uint64_t{0};
        bool window = false;
        std::vector<DrawInstance> parts;
    };
    std::unordered_map<const Element*, Body> bodies_;
    // Whether a caster can lie across the rays of a map: inside the volume the
    // light sees (a thing outside it shades nothing in it), and, for light let
    // in through a doorway, on this side of the opening.
    static bool shades(const Caster& c, const Frustum& sees, const Light& light);
    // Which mesh a box is drawn with (its shape, rounding and taper).
    gl::RenderTarget scene_target_, resolve_, bloom_a_, bloom_b_;
    // Where the composite writes: the screen, or a feed's picture.
    const gl::RenderTarget* output_ = nullptr;
    const Element* eye_override_ = nullptr;  // drawn from this eye, not the world's camera (a doorway's own look)
    // Seen through a window or doorway by another view: its film is the
    // viewer's, laid over the whole picture - its own grain would be a second
    // layer, and a still one wherever the world beyond keeps its time still.
    bool film_of_viewer_ = false;
    struct Feed {
        const Spatial3D* world = nullptr;
        const Element* eye = nullptr;  // seen from this (a camera's lens), not the world's camera
        int w = 0, h = 0;      // as the screen asks for it
        int rw = 0, rh = 0;    // as it is drawn: a fraction of that, while the screen is small
        int div = 1;           // that fraction's denominator (1, 2, 4 or 8)
        int slack = 0;         // frames a smaller picture would have done
        bool live = true, drawn = false;
        bool from_graph = false, seen = false;
        // Its picture, made at each size it may be drawn at (by `div`: 1, 2, 4,
        // 8) and kept, so that a screen coming near or leaving is only a
        // change of which is drawn into - never made again mid-frame.
        gl::RenderTarget outs[4][2];
        static int slot_of(int div) { return div >= 8 ? 3 : div >= 4 ? 2 : div >= 2 ? 1 : 0; }
        gl::RenderTarget* out() { return outs[slot_of(div)]; }
        const gl::RenderTarget* out() const { return outs[slot_of(div)]; }
        int front = 0;  // the one shown; the other is drawn into
        uint64_t drawn_of = 0;  // what its picture was drawn from (feed_key): drawn again only when that moves
        const gl::RenderTarget& shown() const { return out()[front]; }
        std::unique_ptr<GLWorldView> view;
    };
    std::unordered_map<Key, Feed> feeds_;
    // A feed let go: its pictures and its view's targets are given back.
    static void release_feed(Feed& f);
    // The denominator of the fraction of a `declared` pixels tall picture that
    // is enough for a screen `shown` pixels tall: a pixel and six tenths of the
    // picture to each of the screen's, so it is never seen coarser than it is
    // made, and never smaller than a few dozen.
    static int feed_detail(float shown, int declared);
    // Everything a feed's picture is made from: its world's data and look,
    // its time there, the eye it is seen from, its size, and the worlds its
    // own doorways open onto. A still world on a wall is drawn once.
    uint64_t feed_key(const Feed& f) const;
    // Draw a feed's picture through its own view, unless nothing it is made
    // from moved since it was last drawn (feed_key). Whether it was drawn.
    bool draw_feed(Feed& f);
    // Everything the scene pass of a picture is drawn from: what it draws
    // (things, walls, lamps, doorways, their anchors, the ground, the eye),
    // its world's params, the look's scene and shadow passes, its time, its
    // size, the pictures shown in it, and the worlds its doorways open onto.
    // A feed whose scene is as it was is only developed again (post): light
    // falling on a painting changes how it is seen, not what it shows.
    uint64_t scene_key(const std::vector<PlacedRoom>& rooms, int w, int h) const;
    uint64_t scene_drawn_ = 0;
    // The view on the screen, for a view it draws a feed or a far room with:
    // the feeds are that one's, and every view shows the same pictures.
    GLWorldView* root_ = nullptr;
    const std::unordered_map<Key, Feed>& shared_feeds() const { return root_ ? root_->feeds_ : feeds_; }
    static constexpr int kBloomLevels = 5;
    gl::RenderTarget bloom_chain_[kBloomLevels];
    int bloom_levels_ = 0;
    std::unique_ptr<gl::Program> bloom_down_, bloom_up_;
    // Ambient occlusion: the resolved depth, the occlusion and its blur, and
    // the scene with it laid on. `scene_src_` is what the post chain reads.
    gl::RenderTarget depth_, ao_a_, ao_b_, lit_;
    bool fog_depth_ = false;  // this frame's composite reads depth_ (uFogBloom)
    // What a look lays over its composited picture (gl::finish_fs): `deband`
    // where nothing was drawn, `smear` of the last frame shown. Both off
    // unless the look says, and then the composite writes the screen as it
    // always did, with no pass and no picture more.
    struct Finish {
        float deband = 0.0f;  // 0..1
        float keep = 0.0f;    // how much of the last frame stays this frame (0: none)
        float blur = 1.0f;    // the last frame's blur, its taps apart in pixels
        bool smear = false;   // the last frame is kept: the view on the screen, smearing
        bool on() const { return deband > 0.0f || smear; }
    };
    Finish finish_of();
    // The composited picture (post_frame_) finished into the output: debanded,
    // and laid over the last frame shown, which it then is.
    void finish(int fb_w, int fb_h, const Finish& f);
    // Its programs, built once; null (and why, in `error`) if they do not build.
    const gl::Program* finish_program(std::string* error = nullptr);
    // Whether a seam joining two worlds lets the picture through it (its
    // doorways' `admits`, absent: all): a step through one goes on smearing.
    bool view_carried(Key from, Key to) const;
    gl::RenderTarget post_frame_;
    std::unique_ptr<gl::Program> finish_prog_, present_prog_;
    bool finish_tried_ = false;
    std::string finish_error_;
    // The last frame shown - presentation history, as a TAA's is, never a
    // state's - two pictures, one read while the other is drawn; and what
    // it was drawn of (the world, and whether its look cuts), so a cut or
    // a world reached by no seam that lets the view through lets it go.
    gl::RenderTarget smear_hist_[2];
    int smear_front_ = 0;
    bool smear_valid_ = false;
    Key smear_world_, smear_look_;
    bool smear_look_cuts_ = false;
    const gl::RenderTarget* scene_src_ = &resolve_;
    std::unique_ptr<gl::Program> ao_prog_, ao_blur_prog_, ao_apply_prog_;
    struct ViewParams {
        float fov = 1.2f, aspect = 1.0f, znear = 0.05f, zfar = 120.0f;
    } view_;

    Pose frame_;              // the placement of the room currently being drawn
    gl::Mat4 frame_matrix_;   // the same thing, ready to multiply
    std::unordered_map<Key, BoundSurface> surfaces_;
    const Spatial3D* rays_room_ = nullptr;
    Vec3d rays_at_{}, rays_dir_{}, rays_fwd_{};
    double rays_strength_ = 0, rays_tan_ = 0.7;
    Rgb rays_colour_{1, 1, 1};
    bool rays_on_ = false;
    std::unordered_map<Key, WorldPortal> worlds_;
    // Views through doorways seen through doorways, by the way the eye came.
    struct Nested {
        gl::RenderTarget target;
        uint64_t frame = 0;
        // Where on the screen the view was drawn (its doorway's rect, in the
        // eye's frame, to the pixel), in the corner of `target` it filled.
        Rect rect{-1, -1, 1, 1};
        float fx = 1, fy = 1;
        std::string owner;  // the view it last drew (its path): kept by it from frame to frame
    };
    // The least of the screen (of 4, the whole) a view through a doorway is drawn for.
    static constexpr float kLeastView = 1e-5f;
    // The part of the screen the drawing now going on is (the eye's own frame,
    // -1..1; all of it but for a view drawn where its doorway is), and its
    // viewport in pixels.
    Rect sub_{-1, -1, 1, 1};
    float vp_w_ = 1, vp_h_ = 1;
    // Of a doorway's view drawn whole, the part of it that is seen (its
    // doorway's rect on the screen, with a margin): only that is drawn.
    Rect cut_{-1, -1, 1, 1};
    // Draw only into `r` (-1..1 each way) of a `w` x `h` picture: true if
    // that is less than all of it, and the scissor is on.
    static bool scissor_to(const Rect& r, int w, int h);
    // The air of the world a view is seen from, while it is drawn: the way
    // to what it shows goes through that air up to the doorway (uHostFog).
    struct HostAir {
        bool on = false;
        float density = 0, start = 0, full = 0;
        gl::Vec3 color{0, 0, 0};
    };
    HostAir host_air_;
    HostAir air_of(const Spatial3D& world) const;
    // A picture of the screen's part `r`, drawn into the corner (fx, fy) of
    // its target, sampled where it is seen from the drawing now going on.
    void sample_screen(const Rect& r, float fx, float fy) const;
    // A view lives only within its frame: the views drawn share a set of
    // pictures, handed out by rank, each as big as the part of the screen it
    // was given - as many as the views the frame has (to kViewPoolMost), so
    // which are drawn is the views' own rule (how deep, how far the air
    // lets one see, how small), never which won a place this frame.
    std::vector<Nested> pool_;
    std::unordered_map<std::string, std::size_t> slot_;  // this frame's view, by the way the eye came
    static constexpr std::size_t kViewPool = 12, kViewPoolMost = 128;
    void fit(Nested& n, int w, int h, bool anew = false);
    // And the pictures of the doorways of the room the eye is in: as many as
    // are seen at once (made with the screen's targets, and more the first
    // frame more are in sight), not one for every doorway the world has.
    struct RootView {
        gl::RenderTarget ms, target;
    };
    std::vector<RootView> root_pool_;
    static constexpr std::size_t kRootViews = 8, kRootViewsMost = 32;
    void make_root_view(RootView& v, int w, int h);
    // Everything ensure_targets makes for one size of picture.
    struct Targets {
        int w = 0, h = 0;
        gl::RenderTarget scene, resolve, depth, lit, ao_a, ao_b, bloom_a, bloom_b, chain[kBloomLevels];
        gl::RenderTarget post;  // the composited picture, when a look finishes it (finish)
        int levels = 0;
        std::vector<RootView> roots;
        std::vector<Nested> nested;
    };
    // The sets of the other sizes this view has been drawn at (see keep_sizes),
    // and whether it keeps them: a view that draws a feed does.
    std::vector<Targets> parked_;
    bool keeps_sizes_ = false;
    std::vector<std::pair<int, int>> sizes_;  // the sizes a feed's picture may be drawn at
    bool sizes_kept_ = true;                  // and that their sets are made
    // A view through a doorway lives in a slot of the pool; the slot it had
    // the frame before is its again (by path), so it is not handed to another
    // and regrown.
    std::unordered_map<std::string, std::size_t> bound_;
    struct ViewJob {
        std::string key;
        const Spatial3D* host;
        const Element* portal;
        Element from, there;
        const WorldPortal* wp;
        int depth;
        float area;
        Rect seen;  // its doorway's rect on the screen, cut by the views it is seen through
    };
    std::vector<ViewJob> jobs_;  // this frame's views through doorways seen through doorways
    std::string path_;          // the way the eye came, while a view through doorways is drawn
    uint64_t frame_count_ = 0;
    std::unordered_map<Key, TerrainMesh> terrains_;
    gl::Vec3 cam_eye_;  // the camera of the view being drawn
    // Drawing a world seen through a doorway (not the viewer's own), and how
    // many planes cut this room's geometry: what hangs in a doorway is drawn
    // whole there, for the viewer's side to blend into (draw_crate).
    bool guest_pass_ = false;
    int clip_count_ = 0;
    // How many pixels a thing one metre across takes at a metre's distance,
    // on the picture being drawn; and the least a thing may take (its
    // radius, in pixels) to be drawn at all: smaller, it is a speck nobody
    // could tell, and costs a draw. A room's `lod_px` sets it; 0 draws all.
    float lod_px_ = 0.0f, lod_least_ = 0.0f;
    Key highlight_;
    bool timing_ = false;
    const State* attend_ = nullptr;
    mutable FrameTimes times_;  // (counted from const questions too)
    // Per room: which of its things move, and so may stand half through a
    // doorway (draw_straddlers). Made again only when what says so changed -
    // the room's structure, or what a thing says of it (`says`, looked at
    // again only when its params' stamp moved) - asked once a frame
    // (`checked`); usually empty.
    struct Straddlers {
        struct Ball {
            std::size_t index;
            Vec3d centre;
            double radius;
            bool shown;
        };
        uint64_t structure = ~uint64_t{0};
        uint64_t checked = ~uint64_t{0};
        std::vector<uint64_t> stamps, says;
        std::vector<char> moved;                      // whose params moved this frame
        std::vector<Ball> balls;                      // each that moves, where it is
        std::vector<std::vector<std::size_t>> chains;  // it and what it hangs from
    };
    std::unordered_map<const Spatial3D*, Straddlers> straddlers_;
};

}  // namespace sg::render
