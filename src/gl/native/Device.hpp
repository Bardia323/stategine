// Stategine - what a card is asked to do when the renderer's GL calls are
// carried to Vulkan or D3D12: the few kinds of thing it holds (buffers,
// pictures, programs) and the few things it does with them (copy, draw,
// show). The GL front (Front.cpp) keeps GL's state and says each draw whole;
// a backend only does what it is told, in the order told.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "Glsl.hpp"

struct GLFWwindow;

namespace sg::gl::native {

enum class Format : std::uint8_t {
    none,
    rgba8,
    srgba8,
    bgra8,
    rgba16f,
    rgba32f,
    rg16f,
    rg32f,
    r32f,
    r32ui,
    rg32ui,
    rgba8ui,  // (four small whole numbers a texel: an index map - the probe boxes over each floor cell)
    bc7,
    bc7_srgb,
    d24,
    d32f,
};

inline bool is_depth(Format f) { return f == Format::d24 || f == Format::d32f; }
inline bool is_packed(Format f) { return f == Format::bc7 || f == Format::bc7_srgb; }
// Bytes a texel (a 4x4 block, packed).
// inline: every picture written or read asks it, row by row.
inline int texel_bytes(Format f) {
    switch (f) {
        case Format::rgba16f: case Format::rg32f: return 8;
        case Format::rgba32f: case Format::bc7: case Format::bc7_srgb: return 16;
        case Format::rg16f: case Format::r32f: case Format::r32ui: return 4;
        case Format::rg32ui: return 8;
        case Format::none: return 0;
        default: return 4;
    }
}

enum class TexKind : std::uint8_t { tex2d, tex2d_array, tex3d, cube };

struct TextureInfo {
    TexKind kind = TexKind::tex2d;
    Format format = Format::rgba8;
    int w = 1, h = 1, depth = 1;  // depth: a volume's depth, an array's layers (6, a cube's faces)
    int levels = 1;
    int samples = 1;
    bool storage = false;  // written by work on the card's own (an upscaler's picture)
};

// How a picture is read (GL's texture or sampler parameters).
struct SamplerInfo {
    enum Filter : std::uint8_t { nearest, linear };
    enum Mip : std::uint8_t { none, mip_nearest, mip_linear };
    enum Wrap : std::uint8_t { repeat, clamp_edge, clamp_border };
    Filter min = linear, mag = linear;
    Mip mip = none;
    Wrap wrap[3] = {repeat, repeat, repeat};
    bool compare = false;
    std::uint8_t compare_op = 3;  // as DepthTest's ops
    bool border_white = false;    // otherwise transparent black
    std::uint8_t anisotropy = 1;
    // inline: every picture bound for every draw is looked up by it.
    std::uint64_t key() const {
        return std::uint64_t(min) | std::uint64_t(mag) << 1 | std::uint64_t(mip) << 2 | std::uint64_t(wrap[0]) << 4 |
               std::uint64_t(wrap[1]) << 6 | std::uint64_t(wrap[2]) << 8 | std::uint64_t(compare) << 10 |
               std::uint64_t(compare_op) << 11 | std::uint64_t(border_white) << 14 | std::uint64_t(anisotropy) << 16;
    }
    // inline: and a sampler made from it the first time, beside key, its inverse.
    static SamplerInfo from_key(std::uint64_t k) {
        SamplerInfo s;
        s.min = static_cast<Filter>(k & 1), s.mag = static_cast<Filter>(k >> 1 & 1), s.mip = static_cast<Mip>(k >> 2 & 3);
        for (int i = 0; i < 3; ++i) s.wrap[i] = static_cast<Wrap>(k >> (4 + 2 * i) & 3);
        s.compare = (k >> 10 & 1) != 0, s.compare_op = static_cast<std::uint8_t>(k >> 11 & 7), s.border_white = (k >> 14 & 1) != 0;
        s.anisotropy = static_cast<std::uint8_t>(k >> 16 & 0xFF);
        return s;
    }
};

// Where a buffer lies: on the card; where the processor writes and the card
// reads (its writes combined, never read back); or where the card writes and
// the processor reads (cached).
enum class Memory : std::uint8_t { card, upload, readback };

struct Texture;  // each backend's own
struct Buffer;
struct Program;

// One picture drawn into: a texture's level and layer (a volume's slice).
struct Attachment {
    Texture* tex = nullptr;
    int level = 0, layer = 0;
    // An sRGB picture drawn into takes what is drawn as it is (GL's way: the
    // renderer never turns GL_FRAMEBUFFER_SRGB on) - unless this says to code it.
    bool encode = false;
    bool operator==(const Attachment& o) const { return tex == o.tex && level == o.level && layer == o.layer && encode == o.encode; }
    bool operator!=(const Attachment& o) const { return !(*this == o); }
};

struct Target {
    static constexpr int kColors = 8;
    Attachment color[kColors];
    Attachment depth;
    int w = 0, h = 0;
    // inline: every draw asks whether it draws into what the last one did.
    bool operator==(const Target& o) const {
        for (int i = 0; i < kColors; ++i)
            if (color[i] != o.color[i]) return false;
        return depth == o.depth && w == o.w && h == o.h;
    }
};

// Comparison ops, GL's order from GL_NEVER: never, less, equal, lequal,
// greater, notequal, gequal, always.
enum class Compare : std::uint8_t { never, less, equal, lequal, greater, notequal, gequal, always };
// Blend factors the renderer uses.
enum class Factor : std::uint8_t { zero, one, src_alpha, one_minus_src_alpha, constant_alpha, one_minus_constant_alpha };

// What a pipeline is made with, beyond its program and target.
struct Raster {
    bool blend = false;
    Factor src_rgb = Factor::one, dst_rgb = Factor::zero, src_a = Factor::one, dst_a = Factor::zero;
    std::uint8_t color_mask = 0xF;
    bool depth_test = false, depth_write = true;
    Compare depth_op = Compare::less;
    bool cull = false, cull_front = false;  // which face: back unless cull_front
    bool front_cw = false;                  // what faces the eye turns clockwise (GL_CW)
    bool depth_clamp = false;
    bool bias = false;
    float bias_factor = 0, bias_units = 0;
};

// One input of a vertex stage: from a buffer, or one value for every vertex.
struct VertexInput {
    Buffer* buffer = nullptr;  // none: `value`
    std::uint32_t offset = 0, stride = 0;
    std::uint8_t components = 4;
    bool per_instance = false;
    float value[4] = {0, 0, 0, 1};
};

// A picture as a sampler slot reads it.
struct Bound {
    Texture* tex = nullptr;  // none: the backend's empty one of `kind`
    Buffer* buffer = nullptr;  // a samplerBuffer's: its texels RGBA32F
    std::size_t buffer_at = 0, buffer_bytes = 0;
    Sampled kind = Sampled::tex2d;
    int base_level = 0, max_level = 1000;
    SamplerInfo sampler;
};

struct Draw {
    Program* program = nullptr;
    Raster raster;
    VertexInput inputs[16];
    const void* uniforms = nullptr;  // the program's block, as the front holds it
    // Which making of that block it is (each change another): the same
    // making need not be sent again. 0: unknown, always sent.
    std::uint64_t uniforms_made = 0;
    Bound bound[48];
    int viewport[4] = {0, 0, 1, 1};
    float depth_range[2] = {0, 1};
    int scissor[4] = {0, 0, 1, 1};
    float blend_constant[4] = {0, 0, 0, 0};
    int first = 0, count = 0, instances = 1;
    // Or drawn as the card says to itself: `count` of GL's draw commands
    // (count, instances, first, first instance: 16 bytes each) read from
    // `indirect` at `indirect_at` - written there, it may be, by a dispatch.
    Buffer* indirect = nullptr;
    std::size_t indirect_at = 0;
};

// A buffer on the card a compute program reads or writes, from `at`.
struct Storage {
    Buffer* buffer = nullptr;
    std::size_t at = 0, bytes = 0;
};

// Work on the card's own: a compute program run in `groups`.
struct Dispatch {
    Program* program = nullptr;
    const void* uniforms = nullptr;
    Storage storage[16];  // by the binding the program declares
    Bound bound[16];      // what its samplers read, as a draw's
    int groups[3] = {1, 1, 1};
};

class Device {
public:
    virtual ~Device() = default;
    virtual const char* name() const = 0;  // what glGetString(GL_RENDERER) says
    // The sign clip space's y takes on this card for rows to lie as GL lays them.
    virtual float y_flip() const = 0;
    // How many frames the card may still be drawing while the next is asked:
    // what a host buffer written in one frame is safe to write again after.
    virtual int frames_in_flight() const = 0;

    virtual Buffer* create_buffer(std::size_t bytes, Memory) = 0;
    virtual void destroy(Buffer*) = 0;  // once the card is done with it
    virtual void* map(Buffer*) = 0;     // an upload or readback buffer's bytes
    // Bytes copied into a buffer, in order with what is drawn.
    virtual void upload(Buffer*, std::size_t at, const void* data, std::size_t bytes) = 0;
    // Bytes read back, now: everything asked before them done first.
    virtual void download(Buffer*, std::size_t at, void* out, std::size_t bytes) = 0;

    virtual Texture* create_texture(const TextureInfo&) = 0;
    virtual void destroy(Texture*) = 0;
    virtual const TextureInfo& info(const Texture*) const = 0;
    // Texels (blocks, packed) written into a level's region, tightly packed
    // rows of the texture's own format; z: the first layer or slice.
    virtual void write(Texture*, int level, int x, int y, int z, int w, int h, int d, const void* data) = 0;
    // Read back, now, likewise packed.
    virtual void read(Texture*, int level, int layer, int x, int y, int w, int h, void* out) = 0;
    // Into a buffer, in order with what is drawn (read later with download).
    virtual void read_into(Texture*, int level, int layer, int x, int y, int w, int h, Buffer*, std::size_t at) = 0;
    // A level's region copied into another texture's (the same format).
    virtual void copy(Texture* from, int from_level, int from_layer, int fx, int fy, Texture* to, int to_level, int to_layer, int tx,
                      int ty, int w, int h) = 0;

    virtual Program* create_program(const Translated&, std::string& log) = 0;
    virtual void destroy(Program*) = 0;

    // What is drawn next is drawn into this, until another is set.
    virtual void target(const Target&) = 0;
    // Parts of the target cleared, within `rect` (x, y, w, h).
    virtual void clear(const int rect[4], std::uint8_t colors, const float color[4], bool depth, float depth_value) = 0;
    virtual void draw(const Draw&) = 0;
    // In order with what is drawn: what it writes is seen by whatever is
    // asked after it (draws, their commands, copies, reads).
    virtual void dispatch(const Dispatch&) = 0;

    // Questions put to the card, each in a slot of the backend's (0 to
    // kQueries - 1): whether anything drawn between begin and end passed the
    // depth test, or when the card got to a point (stamp, in nanoseconds).
    static constexpr int kQueries = 4096;
    virtual void begin_query(int slot) = 0;
    virtual void end_query(int slot) = 0;
    virtual void stamp(int slot) = 0;
    // Its answer (samples that passed, or the time) if it is known - or,
    // `wait`, once it is.
    virtual bool answer(int slot, bool wait, std::uint64_t& out) = 0;
    // Draws that follow skipped while what `slot` asked found nothing (-1: none).
    virtual void only_if(int slot) = 0;

    // The window's own picture (GL's framebuffer 0): drawn into as any.
    virtual Texture* window_color() = 0;
    virtual Texture* window_depth() = 0;
    // Shown: window_color, rows turned GL's way up. Then the next frame.
    virtual void present(bool vsync) = 0;

    // The card's own upscaler (NVIDIA's DLSS, on Vulkan), in order with what
    // is drawn: `output` made from the jittered `color` at fewer pixels (or
    // as many: DLAA), its `depth`, how each pixel moved (`motion`), and the
    // pictures before. False, with why, where there is none: nothing done.
    struct Upscale {
        Texture *color = nullptr, *depth = nullptr, *motion = nullptr, *output = nullptr;
        float jitter_x = 0, jitter_y = 0, motion_x = 1, motion_y = 1;
        bool reset = false, reversed = false;
        int quality = 0;
        float frame_ms = 0, exposure = 0;
    };
    virtual bool can_upscale(std::string& why) {
        why = "this backend has no upscaler of the card's";
        return false;
    }
    virtual bool upscale(const Upscale&, std::string& why) { return can_upscale(why); }
    // Everything asked done.
    virtual void finish() = 0;
};

std::unique_ptr<Device> make_vulkan(GLFWwindow*, std::string& why);
std::unique_ptr<Device> make_d3d12(GLFWwindow*, std::string& why);

}  // namespace sg::gl::native
