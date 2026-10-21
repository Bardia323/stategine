// Stategine - light traced along rays through the world, for the eye's view
// (docs/trace.md). Machinery of the view's, as its shadow maps are: it reads
// the world and writes nothing of it; what it makes is pictures the scene
// reads. Nothing here is made, or costs anything, until a look asks.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Bvh.hpp"
#include "sg/gl/World.hpp"

namespace sg::render::trace {

// What the eye's look asks to be traced (its scene pass's settings: docs/trace.md).
struct Asked {
    bool sun = false, mirror = false;
    bool lamps = false;    // the nearest lamps' shadows, to their bulbs (`trace.lamps`)
    bool now = false;      // traced before the scene, for this frame's (`trace.now`): a pass of surfaces more
    int step = 2;          // pixels each way to one traced of what is mirrored (`trace.scale`)
    float rough = 0.22f;   // the roughest a surface may be and still be mirrored traced (`trace.mirror`)
    float far = 1000.0f;   // how far a ray goes (`trace.far`)
    float reach = 150.0f;  // how far a ray of light come back meets things (`trace.reach`): past it, only the ground
    float sky = 1.0f;      // the sky's light, as many times (`trace.sky`)
    float brightest = 4.0f;  // what a ray brings back at most (`trace.brightest`)
    float hold = 24.0f;    // frames a history holds at most (`trace.hold`)
    // The light from all round, traced (`trace.gi`): one bounce of what each
    // pixel's surface faces, in place of the probes' for its diffuse.
    bool gi = false;
    int gi_step = 2;          // pixels each way to one traced (`trace.gi.scale`)
    int gi_rays = 1;          // rays a pixel traced (`trace.gi.rays`)
    int gi_every = 4;         // and every how many frames (`trace.gi.rays` below one: 2 or 4)
    float gi_reach = 12.0f;   // how far they meet things (`trace.gi.reach`), metres
    float gi_hold = 32.0f;    // frames its history holds at most (`trace.gi.hold`)
    float gi_bounce = 1.0f;   // how much of the probes' light what a ray meets has besides (`trace.gi.bounce`)
    bool any() const { return sun || mirror || lamps || gi; }
    bool operator==(const Asked& o) const;
};

// A texel buffer of floats, four a texel, on the card.
struct Texels {
    gl::GLuint buffer = 0, texture = 0;
    std::size_t floats = 0;  // what the card holds room for
    void put(const std::vector<float>& data, std::size_t from = 0, std::size_t count = ~std::size_t{0});
    void bind(int unit) const;
    ~Texels();
};

class Tracer {
public:
    explicit Tracer(GLWorldView& view) : v_(view) {}

    Asked asked;
    // Before the eye's scene: what it is traced for (and, `trace.now`, the
    // surfaces and the rays); after it, the rays, from the surfaces the scene
    // drew beside it.
    void frame(const std::vector<PlacedRoom>& rooms, const GLWorldView::Camera& cam, float aspect);
    void after(const std::vector<PlacedRoom>& rooms);
    void to_scene(const gl::Program& p) const;
    // Pictures the scene may read now: this frame's, or the last's.
    bool fresh() const { return made_ && made_at_ + 1 >= v_.frame_count_; }
    bool sun_ready() const { return fresh() && asked.sun && seen_.sun && sun_out_; }
    // The most lamps whose shadows are traced: one a channel of their picture.
    static constexpr int kShadowLamps = 3;
    void remember(const std::vector<GLWorldView::Light>& lights, const gl::Vec3& sun_dir, const gl::Vec3& sun_color,
                  const GLWorldView::Atmosphere& a, const Spatial3D* world);

private:
    GLWorldView& v_;
    bool made_ = false;  // this frame's pictures made, for the scene to read
    uint64_t made_at_ = 0;  // the view's frame they were made in
    Asked sized_for_;    // what the pictures were made the size of

    // --- the world, as rays meet it (TraceWorld.cpp) ---------------------------
    // A shape's triangles and their tree, laid in the shared pictures; a skin's
    // made again (its tree's boxes fitted round them) as its joints move.
    struct Shape {
        uint32_t node_base = 0, nodes = 0, tri_base = 0, tris = 0;
        Box bounds;
        uint64_t made = 0;       // which making of its corners (a skin's: its pose's)
        uint64_t used = 0;       // the frame last traced
        Key skin;                // a skin's model (its corners posed each time its joints move)
        const Spatial3D* space = nullptr;
        std::vector<uint32_t> order;  // its triangles in the tree's order
    };
    std::unordered_map<std::string, Shape> shapes_;
    std::vector<float> nodes_, tris_, masks_;
    std::size_t shape_nodes_ = 0;    // where the instances' tree begins in nodes_
    bool shapes_moved_ = false;      // nodes_ / tris_ / masks_ to be sent whole
    bool stale_ = false;             // a shape made again: all laid again
    Texels nodes_tex_, tris_tex_, insts_tex_, masks_tex_;
    std::vector<float> insts_;
    int tlas_root_ = -1;
    std::size_t placed_tris_ = 0;    // triangles of every instance, as placed
    uint64_t things_of_ = 0;         // what the instances were made from
    // Each thing's shape, kept while its params and its model's making hold.
    struct Worn {
        uint64_t stamp = 0, made = 0;
        Shape* shape = nullptr;
    };
    std::unordered_map<const Element*, Worn> worn_;
    // A model's corners' content, by its corners and their making.
    struct Content {
        uint64_t made = 0;
        bool known = false;
        GLWorldView::CornersKey key;
    };
    std::unordered_map<const std::vector<float>*, Content> content_;
    Shape* shape_of(const Spatial3D& space, const Element& e);
    Shape* make_shape(const Spatial3D& space, const Element& e);
    void pose_skin(Shape& s);
    void gather_things(const std::vector<PlacedRoom>& rooms);
    Shape* cube_ = nullptr;          // a room's shell, as rays meet it: boxes of the one cube

    // The ground: its heights round the eye in two squares, fine and coarse.
    struct Square {
        gl::RenderTarget points, atlas;
        std::vector<gl::RenderTarget> levels;
        float x0 = 0, z0 = 0, cell = 0;
        int n = 0;            // points a side (0: none)
        double cx = 1e300, cz = 1e300, rev = -2;
    };
    Square squares_[2];
    gl::Vec3 ground_albedo_{0.5f, 0.4f, 0.3f};
    void lay_ground(const std::vector<PlacedRoom>& rooms, const gl::Vec3& eye);
    void lay_square(Square& s, const std::vector<const gl::Mesh*>& meshes, int n, float cell, const gl::Vec3& eye);

    // --- the sun, lamps and sky last drawn ------------------------------------
    struct Seen {
        bool sun = false;
        gl::Vec3 sun_dir{0, 1, 0}, sun_color{0, 0, 0}, sun_light{0, 0, 0};
        float sun_cone = 0.0f;
        int lamps = 0;
        std::array<float, 32> lamp_at{};
        std::array<float, 24> lamp_light{};
        std::array<float, 48> lamp_way{};  // its way, falloff, its cone's two cosines
        // The lamps whose shadows are traced: where each is, and its bulb's radius.
        int shadow_lamps = 0;
        std::array<float, 4 * kShadowLamps> shadow_lamp{};
        bool open = false;                 // the world open to the sky
        GLWorldView::Atmosphere atmosphere;
        const Spatial3D* world = nullptr;
        bool known = false;
    } seen_;

    // --- the pictures, and the programs that make them (Tracer.cpp) ----------
    gl::RenderTarget surf_[2];  // what each pixel's surface is, this frame's and the last
    gl::RenderTarget sun_raw_, sun_kept_[2], sun_a_, sun_b_;
    gl::RenderTarget mirror_raw_, mirror_kept_[2], mirror_a_, mirror_b_, mirror_full_;
    gl::RenderTarget lamps_raw_, lamps_kept_[2], lamps_a_, lamps_b_;
    gl::RenderTarget gi_raw_, gi_kept_[2], gi_a_, gi_b_, gi_full_;
    bool gi_made_ = false;    // the light from all round traced this frame or the last
    gl::RenderTarget sky_light_;
    const gl::RenderTarget* sun_out_ = nullptr;
    const gl::RenderTarget* lamps_out_ = nullptr;
    int lamps_made_ = -1;    // the frame the lamps' picture was made
    int now_ = 0;
    int shown_ = 0;           // the surfaces the pictures the scene reads were traced from
    gl::Mat4 shown_vp_;       // and the view they were of, and from where
    gl::Vec3 shown_eye_;
    double surfaces_ms_ = 0;  // (for timing)
    void trace(const std::vector<PlacedRoom>& rooms);
    int relaid_ = 0;          // (for timing: frames the ground was laid again)
    int sun_made_ = -1;      // the frame the sun's picture was made
    bool history_ = false;   // last frame's pictures are of this view, to be carried
    int w_ = 0, h_ = 0;
    gl::Mat4 vp_, last_vp_;
    gl::Vec3 eye_, last_eye_;
    gl::Vec3 fwd_, right_, up_;
    float jitter_[2] = {0, 0};
    float pixel_ = 0.0f;     // how wide a pixel is a metre from the eye
    int frame_ = 0;
    uint64_t sky_of_ = 0;
    std::unique_ptr<gl::Program> gi_prog_, sky_prog_, sun_prog_, lamps_prog_, mirror_prog_, carry_prog_, smooth_prog_, widen_prog_, heights_prog_, highest_prog_;
    void sized(int w, int h);
    void common(const gl::Program& p, int step) const;
    void world_to(const gl::Program& p) const;
    void make_sky_light();
    void pass(const gl::Program& p, const gl::RenderTarget& into) const;
    const gl::RenderTarget& carry(const gl::RenderTarget& now, gl::RenderTarget kept[2], int step, bool mirror, float clamp, float hold = -1.0f);
    const gl::RenderTarget& smooth(const gl::RenderTarget& from, gl::RenderTarget& a, gl::RenderTarget& b, int step, int passes,
                                   float penumbra, float sharp = 0.0f, bool held_only = false);
    void widen(const gl::RenderTarget& from, gl::RenderTarget& into, int step, float rough, bool held = false);
    void trace_gi(const std::vector<PlacedRoom>& rooms, double* ms);
};

}  // namespace sg::render::trace
