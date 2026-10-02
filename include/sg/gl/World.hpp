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
// sampled from a height function bound with bind_terrain and rebuilt around
// the viewer as they walk. A state with `own_time` among its params keeps its own
// time: its shaders move by it (`uTime`), or its declared Temporal line - still
// when it is still. A light with `sun` = 1 is parallel light with an
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

#include "sg/domains/Light.hpp"
#include "sg/domains/Look.hpp"
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

    // Supply a transient interpolation interval. No interval accumulates into
    // shader time. Zero keeps fades still; world time is declared by Temporal.
    void set_fixed_step(double seconds) { fixed_step_ = seconds; }
    void set_frame_delta(double seconds) { fixed_step_ = seconds; }

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

    // A lamp that takes its colour and its strength from a picture: the light
    // `light` (an element's id, in whatever room is drawn) glows as `from`
    // looks - eased a quarter of the way there each frame, up to `most` - and
    // not at all while it is not `on`. How a screen lights the room it is in
    // is how it is drawn: the renderer's to work out, frame by frame, never
    // written into the world.
    void spill(Key light, const Surface2D* from, double most, bool on = true);

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

    // Show another 3D state on a panel as a picture (see the top of this
    // file): drawn from its own camera, in its own look, `w` x `h` pixels.
    // Only drawn while the panel is in the room being drawn and in view.
    // Bound again with another world or size, it follows. Not `live`, it
    // holds the last picture it drew - a paused tape.
    void bind_feed(Key portal_element, const Spatial3D* world, int w, int h, bool live = true);
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
    void warm(const std::vector<Spatial3D*>& worlds, int fb_w, int fb_h);

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
    };

    using Light = DrawLight;

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
        // `own_look`: the far side drawn whole, in its own look, from the
        // carried eye - as it will be seen once through (its picture, and
        // the view that draws it).
        std::unique_ptr<GLWorldView> own;
        gl::RenderTarget own_out;
        bool own_drawn = false;
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
    bool in_view(const Spatial3D& world, const Element& e, const Camera& cam) const;

    // The plane of `portal` (in `host`) as seen on the far side, facing away
    // from it: the far side is drawn only beyond it. The turn and shift are
    // read off the two cameras - the guest's was carried from the host's by
    // the portal's own functor, so the pair of them is that functor.
    HalfSpace far_side(const Spatial3D& host, const Element& portal, const Element& gc) const;

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

    void ensure_targets(int w, int h);

    // Each spilling lamp, a frame further towards what its picture shows.
    struct Spill {
        const Surface2D* from = nullptr;
        double most = 0.0;
        bool on = true;
        bool begun = false;
        double r = 0, g = 0, b = 0, intensity = 0;
    };
    void ease_spills(const std::vector<PlacedRoom>& rooms);

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
    // the scene shader (around_at): a thing through a doorway is lit as one.
    void doors_to_program(const PlacedRoom& placed);

    // How much of a doorway's opening (half `half_w` across, `half_h` high)
    // the things of its room standing in it cover, 0 to 1: each box near the
    // opening's plane, seen square on to it - the most any one of them does
    // (a door's panels lie on its slab: covering the same, not more). A door
    // shut in its frame covers it all; swung open it is edge on, and covers
    // a sliver. `shut`: something lying flat in the opening fills it.
    float covered(const Spatial3D& room, const Element& portal, const Pose& door, float half_w, float half_h, bool& shut) const;

    // Every lamp that lights these rooms, strongest first: the first four of
    // their own get shadow maps (`shadowed` of them), the rest light without
    // casting. What comes in through their doorways goes after the shadowed,
    // before their fainter own.
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
        Pose pose;
        RoomMatrix box;
        std::array<float, gl::Mesh::kInstanceFloats> record{};  // as a batch carries it
    };
    static uint64_t chain_stamp(const State& st, const Element& e);
    Placed& placed_of(const State& st, const Element& e) const;
    Pose pose_of(const State& st, const Element& e) const;
    const RoomMatrix& box_matrix(const State& st, const Element& e) const;
    mutable std::unordered_map<const Element*, Placed> placed_;
    mutable std::unordered_map<const Element*, std::pair<uint64_t, const gl::Mesh*>> shape_memo_;  // each thing's mesh, by its stamp

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

    void draw_terrain(const Element& e);

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
    };
    std::unordered_map<const Spatial3D*, DrawPlan> draw_plans_;
    std::vector<std::size_t> plan_draws(const Spatial3D& room, const Frustum& view);
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
    };
    // A thing is drawn with the others of its shape unless it wears a skin (a
    // surface bound to it) or is being pointed at.
    bool instanceable(const Element& e) const;
    void batch_crate(const State& st, const Element& e);
    Batch& batch_for(const gl::Mesh& mesh);
    // One more of `mesh` to draw, at `local` in the room being drawn.
    void batch(const gl::Mesh& mesh, const gl::Mat4& local, const gl::Vec3& albedo, float roughness, float surface,
               float emissive, float highlight, float mirror);
    // Everything batched, drawn: a call for each shape, in the room's frame.
    void flush_batches(const gl::Program& p, bool scene);

    static bool is_sprite(const Element& e);
    // One of its state's pictures, made current on unit 0 - false if it keeps
    // none by that name.
    bool bind_picture(const State& st, const std::string& name);
    // A sprite: its picture on a flat card at its place, turned to the eye -
    // round about the upright, or (`face`) wholly, to lie square to the view.
    void draw_sprite(const State& st, const Element& e);

    void draw_crate(const State& st, const Element& e);

    // A lamp hangs from the ceiling in a housing, unless `fixture` is 0: then
    // it is only light, for a lamp whose body is modelled elsewhere.
    void draw_lamp(const Spatial3D& world, const Element& e);

    void draw_portal(const State& st, const Element& e, int depth, const gl::RenderTarget& target,
                     bool frame_only = false);

    // Occlusion, at full resolution: the depth resolved, the occlusion
    // found and blurred along surfaces, and laid over the scene into `lit_`.
    void run_ao(float strength, float radius);

    void run_bloom();

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
    double dt_ = 0.0;
    // The time its shaders move by (`uTime`: water, clouds, stars, grain):
    // declared own_time or the world's Temporal line, so time that stands still
    // stands still in every pass too.
    double world_time_ = 0.0;
    double fixed_step_ = 0.0;

    gl::Mesh cube_, quad_, cylinder_, sphere_;
    mutable std::unordered_map<std::string, gl::Mesh> shaped_;  // bevelled and tapered, by size
    mutable std::unordered_map<const std::vector<float>*, gl::Mesh> model_meshes_;  // each model a state keeps, as a mesh
    // Each picture a state keeps, as a texture - painted again when the
    // picture is.
    struct PictureTexture {
        gl::Texture texture;
        uint64_t revision = ~uint64_t{0};
    };
    std::unordered_map<const Spatial3D::Picture*, PictureTexture> picture_textures_;
    gl::Vec3 cam_forward_{0, 0, -1}, cam_up_{0, 1, 0};  // and which way it looks
    gl::FullscreenTriangle screen_;
    // Shadow maps, a set for each world drawn, and what each was drawn of.
    std::vector<Batch> batches_;  // kept from frame to frame, emptied as drawn
    gl::InstanceBuffer instances_;

    struct ShadowSet {
        gl::ShadowArray array;  // a layer for each light that casts, made as wanted
        uint64_t sig[kShadowMaps] = {};
    };
    std::map<std::pair<const void*, const void*>, std::unique_ptr<ShadowSet>> shadow_sets_;
    ShadowSet& shadows_for(const void* world, const void* view);
    static uint64_t mix_bits(uint64_t h, float f);
    // Everything that casts a shadow, where it is now: its matrix, bit for
    // bit. Equal from one frame to the next, the maps from last frame stand.
    uint64_t caster_signature(const std::vector<PlacedRoom>& rooms);
    // Which mesh a box is drawn with (its shape, rounding and taper).
    gl::RenderTarget scene_target_, resolve_, bloom_a_, bloom_b_;
    // Where the composite writes: the screen, or a feed's picture.
    const gl::RenderTarget* output_ = nullptr;
    const Element* eye_override_ = nullptr;  // drawn from this eye, not the world's camera (a doorway's own look)
    struct Feed {
        const Spatial3D* world = nullptr;
        const Element* eye = nullptr;  // seen from this (a camera's lens), not the world's camera
        int w = 0, h = 0;
        bool live = true, drawn = false;
        bool from_graph = false, seen = false;
        gl::RenderTarget out[2];
        int front = 0;  // the one shown; the other is drawn into
        const gl::RenderTarget& shown() const { return out[front]; }
        std::unique_ptr<GLWorldView> view;
    };
    std::unordered_map<Key, Feed> feeds_;
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
    const gl::RenderTarget* scene_src_ = &resolve_;
    std::unique_ptr<gl::Program> ao_prog_, ao_blur_prog_, ao_apply_prog_;
    struct ViewParams {
        float fov = 1.2f, aspect = 1.0f, znear = 0.05f, zfar = 120.0f;
    } view_;

    Pose frame_;              // the placement of the room currently being drawn
    gl::Mat4 frame_matrix_;   // the same thing, ready to multiply
    std::unordered_map<Key, BoundSurface> surfaces_;
    std::unordered_map<Key, Spill> spills_;
    const Spatial3D* rays_room_ = nullptr;
    Vec3d rays_at_{}, rays_dir_{}, rays_fwd_{};
    double rays_strength_ = 0, rays_tan_ = 0.7;
    Rgb rays_colour_{1, 1, 1};
    bool rays_on_ = false;
    std::unordered_map<Key, WorldPortal> worlds_;
    std::unordered_map<Key, TerrainMesh> terrains_;
    gl::Vec3 cam_eye_;  // the camera of the view being drawn
    Key highlight_;
    bool timing_ = false;
    const State* attend_ = nullptr;
    FrameTimes times_;
};

}  // namespace sg::render
