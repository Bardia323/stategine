// Discardable answers to drawing queries. The graph owns every named thing.
#pragma once
#include "sg/domains/Spatial.hpp"
#include "sg/domains/Surface.hpp"
#include "sg/domains/Look.hpp"
#include "sg/domains/Light.hpp"
#include "sg/spatial/Geometry.hpp"
#include "sg/spatial/Projection.hpp"

namespace sg::render {
struct ViewCamera {
    Vec3d eye, forward{0,0,-1}, up{0,1,0};
    double fov = 70, near_plane = 0.05, far_plane = 120;
};
struct DrawLists {
    std::vector<std::size_t> solids, portals, unbounded;
};
struct DrawInstance {
    const Element* element = nullptr;
    Pose pose;
    Vec3d size{1,1,1};
    Rgb colour{0.55,0.5,0.45};
    double pitch = 0, roll = 0, roughness = 0.6, surface = 0, emissive = 0;
    spatial::projection::Mat4 model;
};
struct PortalDraw {
    const State* host = nullptr;
    const Element* portal = nullptr;
    const State* guest = nullptr;
    const Embedding* embedding = nullptr;
    const Seam* seam = nullptr;
    bool feed = false;
    const Element* feed_eye = nullptr;
};
struct DrawLight {
        spatial::projection::Vec3 pos{0, 3, 0};
        spatial::projection::Vec3 dir{0, -1, 0};
        spatial::projection::Vec3 color{1.0f, 0.93f, 0.82f};
        float power = 26.0f;
        float inner = 0.55f;  // radians
        float outer = 1.15f;
        bool sun = false;      // parallel, from `dir`; its shadow is a box round the viewer
        float extent = 40.0f;  // a sun's shadow reaches this far either side of the viewer
        // ...or of `focus`, if it is pinned: light let in at a doorway needs its
        // shadows where it falls, within a room's reach of the opening, not over
        // the whole of the world it comes from - a box the size of a desert
        // would give each texel the width of a hand.
        bool pinned = false;
        spatial::projection::Vec3 focus{0, 0, 0};
        float floor = -1.0f;   // light left in its own full shadow; < 0: the look's uShadowFloor
        bool indirect = false; // stands in for bounced light: no highlight, and occlusion darkens it
        float falloff = 0.0f;  // 0: the soft falloff; 1: the inverse square, as real light
        // How much of it the air scatters, times what the look's air does
        // (its scene pass's `scatter`): the light's `scatter`, 1 unless it
        // says - a lamp that glows in the air more, or less, than it lights.
        float scatter = 1.0f;
        // A projector's beam: not a round cone but a rectangle, `frame_w` by
        // `frame_h` the tangents of its half-angles across and up (0: round,
        // as `inner` and `outer` say), its edges softened over `frame_soft`
        // of the way in - what it throws a picture, edge to edge, and nothing
        // past it, in the air as on what it lands on.
        float frame_w = 0.0f, frame_h = 0.0f, frame_soft = 0.08f;
        // Light from beyond a doorway comes in only through its opening: the
        // opening's middle, which way across it is, and half its width and
        // height. Such a light casts no shadow of its own.
        bool gated = false;
        spatial::projection::Vec3 gate_at{0, 0, 0}, gate_across{1, 0, 0}, gate_in{0, 0, 1};
        float gate_w = 0.0f, gate_h = 0.0f;
        uint64_t gate = 0;  // which doorway it comes in by (its name), wherever that doorway is now
        float open = 1.0f;  // how much of the opening is clear, for a gated light with no shadow map
        // Let through only onto what hangs in the doorway (a door's leaf): a
        // bounce of the far room's, or any of its light while the door is shut.
        // What is half in that room is lit by its light, whatever else is not.
        bool hung_only = false;
    };


DrawLight light_of(const State& room,const Element& element,const Pose& placement = {});
spatial::projection::Mat4 shadow_projection(const DrawLight& light,const ViewCamera& camera,int size,float& bias);

float portal_occlusion(const Spatial3D& room,const Element& portal,const Pose& door,float half_w,float half_h,bool& shut);
struct RoomDraw {
    const Spatial3D* room = nullptr;
    Pose placement;
    ViewCamera camera;
    double time = 0;
    const LookState* look = nullptr;
    std::vector<HalfSpace> clips;
    std::vector<DrawInstance> instances;
    std::vector<const Element*> lights;
    std::vector<PortalDraw> portals;
};
std::vector<DrawLight> boundary_lights(const StateGraph& graph,const RoomDraw& room,const LookFader& look);
struct DoorLight {
    spatial::projection::Vec3 at, across, inward, sky, ground;
    float half_width = 0, half_height = 0;
};
std::vector<DoorLight> boundary_ambient(const StateGraph& graph,const RoomDraw& room,const LookFader& look);
struct ViewPlan {
    std::vector<RoomDraw> rooms;
    std::vector<DrawInstance> sprites;
    const Surface2D* surface = nullptr;
    double time = 0;
};

ViewCamera view_camera(const Element& camera);
HalfSpace portal_clip(const State& host,const Element& portal,const Element& host_eye,const Element& guest_eye);
DrawLists draw_lists(const Spatial3D& room);
spatial::projection::Mat4 box_transform(const State& room,const Element& element);
std::vector<DrawInstance> enclosure(const Spatial3D& room);
std::vector<DrawInstance> portal_body(const State& host,const Element& portal,bool window);
spatial::projection::Mat4 portal_face(const State& host,const Element& portal,bool window);
spatial::projection::Mat4 sprite_transform(const State& host,const Element& sprite,const ViewCamera& camera);
Key signal_of(const StateGraph& graph, const Element& panel);
bool declared_world(const StateGraph& graph, const State& host, const Element& portal, const State& guest);
bool declared_feed(const StateGraph& graph, Key portal, const State& guest);
bool declared_surface(const StateGraph& graph, const Element& panel, const Surface2D& surface);
// own_time is declared data; otherwise read a declared drive's Temporal line.
// A state with no declared time is still. No renderer manufactures a clock.
double semantic_time(const StateGraph* graph, const State& state);
ViewPlan view_plan(const StateGraph& graph, const State& root,
                   const std::vector<PlacedRoom>& rooms = {});
} // namespace sg::render
