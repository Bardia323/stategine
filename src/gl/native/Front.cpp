// Stategine - the GL front: the renderer's GL calls (sg/gl/GL.hpp) answered
// by a native Device. It keeps what GL keeps - names, bindings, the state a
// draw is made with - and says each draw whole to the device, which knows
// nothing of GL. What GL does that a card does not (a blit, a mipmap made,
// a picture converted as it is read) it does with draws of its own.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "Device.hpp"
#include "Glsl.hpp"
#include "sg/gl/GL.hpp"
#include "sg/gl/Native.hpp"

namespace sg::gl {
namespace native {
namespace {

// --- numbers ------------------------------------------------------------------

std::uint16_t to_half(float f) {
    std::uint32_t x;
    std::memcpy(&x, &f, 4);
    const std::uint32_t sign = (x >> 16) & 0x8000u;
    int e = static_cast<int>((x >> 23) & 0xFF) - 127 + 15;
    std::uint32_t m = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFF) == 0xFF) return static_cast<std::uint16_t>(sign | 0x7C00u | (m ? 0x200u : 0u));
    if (e >= 31) return static_cast<std::uint16_t>(sign | 0x7C00u);
    if (e <= 0) {
        if (e < -10) return static_cast<std::uint16_t>(sign);
        m |= 0x800000u;
        const int shift = 14 - e;
        std::uint32_t h = m >> shift;
        if ((m >> (shift - 1)) & 1u) ++h;
        return static_cast<std::uint16_t>(sign | h);
    }
    std::uint32_t h = sign | static_cast<std::uint32_t>(e) << 10 | m >> 13;
    if (m & 0x1000u) ++h;  // (rounding up may carry into the exponent, as it should)
    return static_cast<std::uint16_t>(h);
}

float from_half(std::uint16_t h) {
    const std::uint32_t sign = (h & 0x8000u) << 16;
    std::uint32_t e = (h >> 10) & 0x1F, m = h & 0x3FFu, x;
    if (e == 0) {
        if (m == 0) {
            x = sign;
        } else {
            e = 127 - 15 + 1;
            while (!(m & 0x400u)) m <<= 1, --e;
            x = sign | e << 23 | (m & 0x3FFu) << 13;
        }
    } else if (e == 31) {
        x = sign | 0x7F800000u | m << 13;
    } else {
        x = sign | (e - 15 + 127) << 23 | m << 13;
    }
    float f;
    std::memcpy(&f, &x, 4);
    return f;
}

// --- formats ------------------------------------------------------------------

Format format_of(GLenum internal) {
    switch (internal) {
        case GL_RGBA8: case GL_RGBA: case GL_RGB: return Format::rgba8;
        case GL_SRGB8_ALPHA8: return Format::srgba8;
        case GL_RGBA16F: case GL_RGB16F: return Format::rgba16f;
        case GL_RGBA32F: return Format::rgba32f;
        case GL_RG16F: return Format::rg16f;
        case GL_RG32F: return Format::rg32f;
        case GL_R32F: return Format::r32f;
        case GL_R32UI: return Format::r32ui;
        case GL_RG32UI: return Format::rg32ui;
        case GL_COMPRESSED_RGBA_BPTC_UNORM: return Format::bc7;
        case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM: return Format::bc7_srgb;
        case GL_DEPTH_COMPONENT24: case GL_DEPTH_COMPONENT: return Format::d24;
        case GL_DEPTH_COMPONENT32F: return Format::d32f;
        default: return Format::none;
    }
}

int channels(Format f) {
    switch (f) {
        case Format::rg16f: case Format::rg32f: case Format::rg32ui: return 2;
        case Format::r32f: case Format::r32ui: case Format::d24: case Format::d32f: return 1;
        default: return 4;
    }
}

bool integer(Format f) { return f == Format::r32ui || f == Format::rg32ui; }

int components_of(GLenum format) {
    switch (format) {
        case GL_RED: case GL_DEPTH_COMPONENT: return 1;
        case GL_RG: case GL_RG_INTEGER: return 2;
        case GL_RGB: return 3;
        default: return 4;
    }
}

int type_bytes(GLenum type) {
    switch (type) {
        case GL_UNSIGNED_BYTE: return 1;
        case GL_HALF_FLOAT: return 2;
        default: return 4;
    }
}

// Rows as GL lays them out of memory or into it: 4-byte aligned (its default
// pack and unpack alignment, which the renderer never changes).
std::size_t row_bytes(int w, GLenum format, GLenum type) {
    const std::size_t raw = static_cast<std::size_t>(w) * static_cast<std::size_t>(components_of(format) * type_bytes(type));
    return (raw + 3) & ~std::size_t{3};
}

// One texel of a texture's format as four numbers (or whole numbers, bits).
void load_texel(Format f, const std::uint8_t* p, float out[4]) {
    out[0] = out[1] = out[2] = 0;
    out[3] = 1;
    switch (f) {
        case Format::rgba8: case Format::srgba8:
            for (int i = 0; i < 4; ++i) out[i] = p[i] / 255.0f;
            break;
        case Format::bgra8:
            out[0] = p[2] / 255.0f, out[1] = p[1] / 255.0f, out[2] = p[0] / 255.0f, out[3] = p[3] / 255.0f;
            break;
        case Format::rgba16f: case Format::rg16f: {
            const int n = f == Format::rgba16f ? 4 : 2;
            for (int i = 0; i < n; ++i) {
                std::uint16_t h;
                std::memcpy(&h, p + 2 * i, 2);
                out[i] = from_half(h);
            }
            break;
        }
        case Format::rgba32f: case Format::rg32f: case Format::r32f: case Format::d32f:
            std::memcpy(out, p, static_cast<std::size_t>(channels(f)) * 4);
            break;
        case Format::r32ui: case Format::rg32ui:
            std::memcpy(out, p, static_cast<std::size_t>(channels(f)) * 4);
            break;
        default: break;
    }
}

void store_texel(Format f, const float in[4], std::uint8_t* p) {
    switch (f) {
        case Format::rgba8: case Format::srgba8:
            for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(std::lround(std::clamp(in[i], 0.0f, 1.0f) * 255.0f));
            break;
        case Format::rgba16f: case Format::rg16f: {
            const int n = f == Format::rgba16f ? 4 : 2;
            for (int i = 0; i < n; ++i) {
                const std::uint16_t h = to_half(in[i]);
                std::memcpy(p + 2 * i, &h, 2);
            }
            break;
        }
        case Format::rgba32f: case Format::rg32f: case Format::r32f: case Format::r32ui: case Format::rg32ui:
            std::memcpy(p, in, static_cast<std::size_t>(channels(f)) * 4);
            break;
        default: break;
    }
}

// Pixels as GL is given them (format, type, rows 4-aligned) as the texture's
// own texels, tight.
std::vector<std::uint8_t> to_texels(Format f, int w, int h, int d, GLenum format, GLenum type, const void* src) {
    const std::size_t tb = static_cast<std::size_t>(texel_bytes(f));
    std::vector<std::uint8_t> out(tb * static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(d));
    const auto* in = static_cast<const std::uint8_t*>(src);
    const std::size_t pitch = row_bytes(w, format, type);
    const int n = components_of(format), sz = type_bytes(type);
    const bool same = (f == Format::rgba8 || f == Format::srgba8) ? (n == 4 && type == GL_UNSIGNED_BYTE)
                      : f == Format::rgba16f ? (n == 4 && type == GL_HALF_FLOAT)
                      : f == Format::rgba32f ? (n == 4 && type == GL_FLOAT)
                      : f == Format::rg32f ? (n == 2 && type == GL_FLOAT)
                      : f == Format::rg16f ? (n == 2 && type == GL_HALF_FLOAT)
                      : f == Format::r32f ? (n == 1 && type == GL_FLOAT)
                      : f == Format::r32ui ? (n == 1 && type == GL_UNSIGNED_INT)
                      : f == Format::rg32ui ? (n == 2 && type == GL_UNSIGNED_INT)
                      : false;
    for (int z = 0; z < d; ++z)
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* row = in + (static_cast<std::size_t>(z) * static_cast<std::size_t>(h) + static_cast<std::size_t>(y)) * pitch;
            std::uint8_t* to = out.data() + ((static_cast<std::size_t>(z) * static_cast<std::size_t>(h) + static_cast<std::size_t>(y)) * static_cast<std::size_t>(w)) * tb;
            if (same) {
                std::memcpy(to, row, tb * static_cast<std::size_t>(w));
                continue;
            }
            for (int x = 0; x < w; ++x) {
                float v[4] = {0, 0, 0, 1};
                const std::uint8_t* px = row + static_cast<std::size_t>(x * n * sz);
                for (int c = 0; c < n; ++c) {
                    if (type == GL_UNSIGNED_BYTE) v[c] = px[c] / 255.0f;
                    else if (type == GL_HALF_FLOAT) {
                        std::uint16_t hv;
                        std::memcpy(&hv, px + 2 * c, 2);
                        v[c] = from_half(hv);
                    } else std::memcpy(&v[c], px + 4 * c, 4);  // float, or an unsigned int's bits
                }
                store_texel(f, v, to + static_cast<std::size_t>(x) * tb);
            }
        }
    return out;
}

// A texture's texels (tight) as GL hands pixels back (format, type, rows
// 4-aligned). Unsigned bytes from a float are clamped and rounded, as GL does.
void from_texels(Format f, int w, int h, const std::uint8_t* in, GLenum format, GLenum type, void* dst) {
    const std::size_t tb = static_cast<std::size_t>(texel_bytes(f));
    const std::size_t pitch = row_bytes(w, format, type);
    const int n = components_of(format), sz = type_bytes(type);
    auto* out = static_cast<std::uint8_t*>(dst);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v[4];
            load_texel(f, in + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * tb, v);
            std::uint8_t* px = out + static_cast<std::size_t>(y) * pitch + static_cast<std::size_t>(x * n * sz);
            for (int c = 0; c < n; ++c) {
                if (type == GL_UNSIGNED_BYTE) px[c] = static_cast<std::uint8_t>(std::lround(std::clamp(v[c], 0.0f, 1.0f) * 255.0f));
                else if (type == GL_HALF_FLOAT) {
                    const std::uint16_t hv = to_half(v[c]);
                    std::memcpy(px + 2 * c, &hv, 2);
                } else std::memcpy(px + 4 * c, &v[c], 4);
            }
        }
}

int full_levels(int w, int h, int d) {
    int n = 1, m = std::max({w, h, d});
    while (m > 1) m >>= 1, ++n;
    return n;
}

Compare compare_of(GLenum f) { return static_cast<Compare>(f >= 0x0200 && f <= 0x0207 ? f - 0x0200 : 1); }

Factor factor_of(GLenum f) {
    switch (f) {
        case GL_ZERO: return Factor::zero;
        case GL_SRC_ALPHA: return Factor::src_alpha;
        case GL_ONE_MINUS_SRC_ALPHA: return Factor::one_minus_src_alpha;
        case GL_CONSTANT_ALPHA: return Factor::constant_alpha;
        case GL_ONE_MINUS_CONSTANT_ALPHA: return Factor::one_minus_constant_alpha;
        default: return Factor::one;
    }
}

// --- what GL names --------------------------------------------------------------

constexpr GLenum GL_INVALID_ENUM = 0x0500, GL_INVALID_VALUE = 0x0501, GL_INVALID_OPERATION = 0x0502;
constexpr GLenum GL_READ_FRAMEBUFFER_BINDING = 0x8CAA, GL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum GL_TEXTURE_2D_MULTISAMPLE = 0x9100;
constexpr int kUnits = 32;

// A slice of a host buffer that lasts a frame: what a buffer the processor
// rewrites (GL_DYNAMIC_DRAW, GL_STREAM_DRAW) is drawn from.
struct Slice {
    Buffer* buffer = nullptr;
    std::size_t at = 0;
    std::uint8_t* host = nullptr;
    std::uint64_t frame = ~std::uint64_t{0};
};

struct BufferObj {
    std::size_t size = 0;
    bool dynamic = true;               // its bytes kept here, and laid in a frame's slice as drawn
    std::vector<std::uint8_t> shadow;  // a dynamic buffer's bytes
    Slice slice;                       // where they lie this frame
    // A big dynamic buffer that is drawn as it was (a land's ground, given
    // again only as the eye moves on): on the card, as GL's drivers keep it,
    // laid again only when its bytes change - never copied into the frame's
    // host slices and read across the bus by every draw, every frame.
    Buffer* card = nullptr;
    bool card_fresh = false;           // the card's copy is its bytes as they are
    std::uint64_t changed = 0;         // the frame its bytes last changed
    int streak = 0;                    // frames in a row they have changed in (3 and more: it streams)
    void change(std::uint64_t frame) {
        if (frame != changed) streak = frame == changed + 1 ? streak + 1 : 1;
        changed = frame, card_fresh = false;
    }
    Buffer* gpu = nullptr;             // a static or read-back buffer's own
    // Pixels read into it (a read-back buffer): what they were, and what
    // they are to be handed back as.
    struct Pack {
        Format from = Format::none;
        int w = 0, h = 0;
        GLenum format = 0, type = 0;
        std::size_t at = 0;
    };
    std::vector<Pack> packs;
};

struct LevelSpec {
    int w = 0, h = 0, d = 0;
    bool defined = false;
};

struct TexObj {
    GLenum target = 0;
    Format format = Format::none;
    std::array<LevelSpec, 16> levels{};
    int samples = 1;
    SamplerInfo sampler;
    int base_level = 0, max_level = 1000;
    bool wants_mips = false;  // made mipmapped since: every level, whatever is defined
    Texture* gpu = nullptr;
    GLuint buffer = 0;  // a buffer texture's buffer
    bool renderbuffer = false;
};

struct AttachObj {
    GLuint tex = 0;
    bool rb = false;
    int level = 0, layer = 0;
};

struct FboObj {
    AttachObj color[8];
    AttachObj depth;
    GLenum draw[8] = {GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE};
    GLenum read = GL_COLOR_ATTACHMENT0;
};

struct AttribObj {
    bool enabled = false;
    GLuint buffer = 0;
    int size = 4;
    std::size_t offset = 0;
    int stride = 0;
    GLuint divisor = 0;
};

struct VaoObj {
    std::array<AttribObj, 16> a{};
};

struct ShaderObj {
    GLenum type = 0;
    std::string src, log;
    bool ok = false;
};

struct ProgObj {
    std::vector<GLuint> shaders;
    Translated t;
    Program* gpu = nullptr;
    bool linked = false;
    std::string log;
    std::vector<std::uint8_t> block;
    std::vector<int> units;  // each sampler's unit
    std::unordered_map<std::string, int> locations;
    int fix = -1;  // sg_Fix's offset in the block
    std::uint64_t made = 1;  // each change to the block another
    // Changed bytes written, and counted.
    void write(int at, const void* v, std::size_t n) {
        if (std::memcmp(block.data() + at, v, n) == 0) return;
        std::memcpy(block.data() + at, v, n);
        ++made;
    }
};

// A question put to the card: a slot of the device's.
struct QueryObj {
    int slot = -1;
    GLenum target = 0;
};

template <class T>
struct Names {
    std::vector<std::unique_ptr<T>> all;  // name 0 is none
    std::vector<GLuint> spare;
    Names() { all.emplace_back(); }
    GLuint make() {
        if (!spare.empty()) {
            const GLuint n = spare.back();
            spare.pop_back();
            all[n] = std::make_unique<T>();
            return n;
        }
        all.push_back(std::make_unique<T>());
        return static_cast<GLuint>(all.size() - 1);
    }
    T* get(GLuint n) const { return n < all.size() ? all[n].get() : nullptr; }
    void drop(GLuint n) {
        if (n && n < all.size() && all[n]) all[n].reset(), spare.push_back(n);
    }
};

// Internal programs: what GL does that a card does not.
enum Internal { blit_2d, blit_array, blit_ms, depth_2d, depth_array, depth_ms, kInternals };

struct State {
    std::unique_ptr<Device> dev;
    Api api = Api::gl;
    GLFWwindow* window = nullptr;
    std::string renderer, version = "3.3.0 stategine native";
    GLenum error = GL_NO_ERROR;
    std::uint64_t frame = 0;

    Names<BufferObj> buffers;
    Names<TexObj> textures;
    Names<FboObj> fbos;
    Names<VaoObj> vaos;
    Names<ShaderObj> shaders;
    Names<ProgObj> programs;
    Names<SamplerInfo> samplers;
    std::vector<QueryObj> queries{QueryObj{}};
    std::vector<int> spare_slots;
    GLuint asking = 0;  // the query begun and not yet ended

    FboObj window_fbo;
    GLuint draw_fbo = 0, read_fbo = 0;
    GLuint vao = 0, program = 0;
    GLuint array_buffer = 0, pack_buffer = 0, unpack_buffer = 0, texture_buffer = 0, other_buffer = 0;
    GLuint storage_buffer = 0, indirect_buffer = 0;
    std::array<GLuint, 16> storage_bound{};  // by binding (glBindBufferBase)
    int unit = 0;
    std::array<std::array<GLuint, 6>, kUnits> bound{};  // by unit: 2D, 2D array, 3D, cube, buffer, 2D multisampled
    std::array<GLuint, kUnits> sampler_bound{};
    std::array<std::array<float, 4>, 16> generic{};
    GLuint renderbuffer = 0;

    bool blend = false, depth_test = false, cull = false, scissor_test = false, depth_clamp = false, offset_fill = false;
    std::uint32_t clips = 0;
    GLenum src_rgb = GL_ONE, dst_rgb = GL_ZERO, src_a = GL_ONE, dst_a = GL_ZERO;
    float blend_color[4] = {0, 0, 0, 0};
    GLenum depth_func = GL_LESS;
    bool depth_mask = true;
    std::uint8_t color_mask = 0xF;
    GLenum cull_face = GL_BACK, front_face = GL_CCW;
    int viewport[4] = {0, 0, 0, 0}, scissor[4] = {0, 0, 0, 0};
    float clear_color[4] = {0, 0, 0, 0};
    double clear_depth = 1.0, depth_near = 0.0, depth_far = 1.0;
    bool zero_to_one = false;
    float bias_factor = 0, bias_units = 0;

    // Frames' slices of host buffers.
    struct Chunk {
        Buffer* buffer = nullptr;
        std::uint8_t* host = nullptr;
        std::size_t size = 0, used = 0;
        std::uint64_t frame = 0;
    };
    std::vector<Chunk> chunks;
    int chunk_now = -1;

    ProgObj internal_obj[kInternals];
};

State* g = nullptr;

void fail(GLenum e) {
    if (g->error == GL_NO_ERROR) g->error = e;
}

// --- buffers ------------------------------------------------------------------

Slice slice(std::size_t bytes) {
    State& s = *g;
    bytes = (bytes + 255) & ~std::size_t{255};
    auto fits = [&](State::Chunk& c) { return c.used + bytes <= c.size; };
    if (s.chunk_now < 0 || s.chunks[static_cast<std::size_t>(s.chunk_now)].frame != s.frame || !fits(s.chunks[static_cast<std::size_t>(s.chunk_now)])) {
        s.chunk_now = -1;
        const auto safe = static_cast<std::uint64_t>(s.dev->frames_in_flight());
        for (std::size_t i = 0; i < s.chunks.size(); ++i) {
            State::Chunk& c = s.chunks[i];
            if (c.frame + safe <= s.frame && c.size >= bytes) {
                c.used = 0, c.frame = s.frame;
                s.chunk_now = static_cast<int>(i);
                break;
            }
        }
        if (s.chunk_now < 0 && s.chunks.size() >= 32) {
            // (A frame long in coming - many drawn and none shown: once the
            // card is done with it all, every slice is laid again as drawn.)
            s.dev->finish();
            s.frame += static_cast<std::uint64_t>(s.dev->frames_in_flight()) + 1;
            for (State::Chunk& c : s.chunks) c.frame = 0, c.used = 0;
            s.chunk_now = 0;
            s.chunks[0].frame = s.frame;
        }
        if (s.chunk_now < 0) {
            State::Chunk c;
            c.size = std::max<std::size_t>(bytes, std::size_t{16} << 20);
            c.buffer = s.dev->create_buffer(c.size, Memory::upload);
            c.host = static_cast<std::uint8_t*>(s.dev->map(c.buffer));
            c.frame = s.frame;
            s.chunks.push_back(c);
            s.chunk_now = static_cast<int>(s.chunks.size() - 1);
        }
    }
    State::Chunk& c = s.chunks[static_cast<std::size_t>(s.chunk_now)];
    Slice out{c.buffer, c.used, c.host + c.used, s.frame};
    c.used += bytes;
    return out;
}

// Where a buffer's bytes are for the card now: laid again this frame if they
// were last laid in another - or, a big one (a megabyte and more) that does
// not stream (its bytes not changed three frames running), its copy on the
// card, made again when they change (a new buffer each time: the old one let
// go once the frames in flight are done with it, so no draw before reads
// what is written after). What streams is a frame's slice, as before.
std::pair<Buffer*, std::size_t> place(BufferObj& b) {
    if (!b.dynamic) return {b.gpu, 0};
    constexpr std::size_t kBig = std::size_t{1} << 20;
    if (b.size >= kBig && b.streak < 3) {
        if (!b.card_fresh || !b.card) {
            if (b.card) g->dev->destroy(b.card);
            b.card = g->dev->create_buffer(b.size, Memory::card);
            g->dev->upload(b.card, 0, b.shadow.data(), b.size);
            b.card_fresh = true;
        }
        return {b.card, 0};
    }
    if (b.slice.frame != g->frame) {
        b.slice = slice(std::max<std::size_t>(b.size, 4));
        if (b.size) std::memcpy(b.slice.host, b.shadow.data(), b.size);
    }
    return {b.slice.buffer, b.slice.at};
}

GLuint* binding_for(GLenum target) {
    switch (target) {
        case GL_ARRAY_BUFFER: return &g->array_buffer;
        case GL_PIXEL_PACK_BUFFER: return &g->pack_buffer;
        case GL_PIXEL_UNPACK_BUFFER: return &g->unpack_buffer;
        case GL_TEXTURE_BUFFER: return &g->texture_buffer;
        case GL_SHADER_STORAGE_BUFFER: return &g->storage_buffer;
        case GL_DRAW_INDIRECT_BUFFER: return &g->indirect_buffer;
        default: return &g->other_buffer;
    }
}

BufferObj* bound_buffer(GLenum target) {
    GLuint* at = binding_for(target);
    return g->buffers.get(*at);
}

// A buffer the card itself writes or reads its draws from (storage, draw
// commands): on the card from now on, its bytes as they were.
Buffer* on_card(BufferObj& b) {
    if (!b.dynamic && b.gpu) return b.gpu;
    if (b.gpu) g->dev->destroy(b.gpu);
    if (b.card) g->dev->destroy(b.card);
    b.card = nullptr, b.card_fresh = false;
    b.gpu = g->dev->create_buffer(std::max<std::size_t>(b.size, 4), Memory::card);
    if (b.dynamic && b.size) g->dev->upload(b.gpu, 0, b.shadow.data(), b.size);
    b.dynamic = false;
    b.shadow.clear();
    b.slice = {};
    b.packs.clear();
    return b.gpu;
}

// --- textures -----------------------------------------------------------------

int unit_slot(GLenum target) {
    switch (target) {
        case GL_TEXTURE_2D_ARRAY: return 1;
        case GL_TEXTURE_3D: return 2;
        case GL_TEXTURE_CUBE_MAP: return 3;
        case GL_TEXTURE_BUFFER: return 4;
        case GL_TEXTURE_2D_MULTISAMPLE: return 5;
        default: return 0;
    }
}

TexObj* bound_texture(GLenum target) {
    if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6) target = GL_TEXTURE_CUBE_MAP;
    return g->textures.get(g->bound[static_cast<std::size_t>(g->unit)][static_cast<std::size_t>(unit_slot(target))]);
}

TexKind kind_of(const TexObj& t) {
    switch (t.target) {
        case GL_TEXTURE_2D_ARRAY: return TexKind::tex2d_array;
        case GL_TEXTURE_3D: return TexKind::tex3d;
        case GL_TEXTURE_CUBE_MAP: return TexKind::cube;
        default: return TexKind::tex2d;
    }
}

// The texture as the card holds it, made (or made again, its levels kept
// where they still fit) for what GL has said of it so far.
Texture* realize(TexObj& t) {
    if (t.format == Format::none) return nullptr;
    int base = -1;
    for (int l = 0; l < 16; ++l)
        if (t.levels[static_cast<std::size_t>(l)].defined) {
            base = l;
            break;
        }
    if (base < 0) return nullptr;
    const LevelSpec& b = t.levels[static_cast<std::size_t>(base)];
    TextureInfo want;
    want.kind = kind_of(t);
    want.format = t.format;
    want.w = std::max(1, b.w << base);
    want.h = std::max(1, b.h << base);
    want.depth = want.kind == TexKind::tex3d ? std::max(1, b.d << base) : std::max(1, b.d);
    if (want.kind == TexKind::cube) want.depth = 6;
    want.samples = t.samples;
    bool mipped = t.wants_mips || base > 0 || t.sampler.mip != SamplerInfo::none;
    for (int l = 1; l < 16; ++l) mipped = mipped || t.levels[static_cast<std::size_t>(l)].defined;
    want.levels = mipped ? full_levels(want.w, want.h, want.kind == TexKind::tex3d ? want.depth : 1) : 1;
    if (t.gpu) {
        const TextureInfo& have = g->dev->info(t.gpu);
        if (have.kind == want.kind && have.format == want.format && have.w == want.w && have.h == want.h && have.depth == want.depth &&
            have.samples == want.samples && have.levels >= want.levels)
            return t.gpu;
    }
    Texture* made = g->dev->create_texture(want);
    if (t.gpu) {
        const TextureInfo& have = g->dev->info(t.gpu);
        if (have.format == want.format && have.kind == want.kind && have.samples == want.samples)
            for (int l = 0; l < std::min(have.levels, want.levels); ++l) {
                const int ow = std::max(1, have.w >> l), oh = std::max(1, have.h >> l);
                const int nw = std::max(1, want.w >> l), nh = std::max(1, want.h >> l);
                if (ow != nw || oh != nh) continue;
                for (int z = 0; z < std::min(have.depth, want.depth); ++z) g->dev->copy(t.gpu, l, z, 0, 0, made, l, z, 0, 0, nw, nh);
            }
        g->dev->destroy(t.gpu);
    }
    t.gpu = made;
    return made;
}

void define(TexObj& t, int level, GLenum internal, int w, int h, int d) {
    const Format f = format_of(internal);
    if (f != t.format) {
        t.format = f;
        for (auto& l : t.levels) l = {};
    }
    if (level < 0 || level >= 16) return fail(GL_INVALID_VALUE);
    t.levels[static_cast<std::size_t>(level)] = {w, h, d, true};
}

// --- framebuffers ---------------------------------------------------------------

FboObj& fbo(GLuint n) {
    FboObj* f = n ? g->fbos.get(n) : nullptr;
    return f ? *f : g->window_fbo;
}

Attachment attachment(GLuint fbo_name, const AttachObj& a, bool depth) {
    if (fbo_name == 0) return {depth ? g->dev->window_depth() : g->dev->window_color(), 0, 0};
    TexObj* t = g->textures.get(a.tex);
    if (!t) return {};
    return {realize(*t), a.level, a.layer};
}

Target resolve_target(GLuint name) {
    Target out;
    FboObj& f = fbo(name);
    int w = 1 << 30, h = 1 << 30;
    auto size = [&](const Attachment& a) {
        if (!a.tex) return;
        const TextureInfo& i = g->dev->info(a.tex);
        w = std::min(w, std::max(1, i.w >> a.level));
        h = std::min(h, std::max(1, i.h >> a.level));
    };
    for (int i = 0; i < Target::kColors; ++i) {
        const GLenum d = f.draw[i];
        if (d == GL_NONE) continue;
        if (name == 0) {
            if (i == 0) out.color[0] = attachment(0, {}, false);
        } else if (d >= GL_COLOR_ATTACHMENT0 && d < GL_COLOR_ATTACHMENT0 + 8) {
            out.color[i] = attachment(name, f.color[d - GL_COLOR_ATTACHMENT0], false);
        }
        size(out.color[i]);
    }
    out.depth = name == 0 ? attachment(0, {}, true) : attachment(name, f.depth, true);
    size(out.depth);
    out.w = w == (1 << 30) ? 1 : w;
    out.h = h == (1 << 30) ? 1 : h;
    return out;
}

Attachment read_attachment(GLuint name, bool depth) {
    FboObj& f = fbo(name);
    if (depth) return name == 0 ? attachment(0, {}, true) : attachment(name, f.depth, true);
    if (name == 0) return attachment(0, {}, false);
    if (f.read < GL_COLOR_ATTACHMENT0 || f.read >= GL_COLOR_ATTACHMENT0 + 8) return {};
    return attachment(name, f.color[f.read - GL_COLOR_ATTACHMENT0], false);
}

// --- programs -------------------------------------------------------------------

int location(ProgObj& p, const char* name) {
    auto it = p.locations.find(name);
    if (it != p.locations.end()) return it->second;
    std::string n = name;
    if (n.size() > 3 && n.compare(n.size() - 3, 3, "[0]") == 0) {
        auto again = p.locations.find(n.substr(0, n.size() - 3));
        if (again != p.locations.end()) return again->second;
    }
    auto element = p.locations.find(n + "[0]");
    return element != p.locations.end() ? element->second : -1;
}

void link(ProgObj& p, const char* vs, const char* fs, const char* cs) {
    p.t = cs ? translate_compute(cs, g->api == Api::d3d12) : translate(vs, fs, g->api == Api::d3d12);
    p.linked = false;
    if (!p.t.ok()) {
        p.log = p.t.log;
        return;
    }
    if (!p.t.compute && p.t.block_size[0] && p.t.block_size[1] && p.t.block_size[0] != p.t.block_size[1]) {
        p.log = "the stages' uniforms are laid out differently";
        return;
    }
    std::string why;
    Program* made = g->dev->create_program(p.t, why);
    if (!made) {
        p.log = why;
        return;
    }
    if (p.gpu) g->dev->destroy(p.gpu);
    p.gpu = made;
    p.block.assign(std::max(p.t.block_size[0], p.t.block_size[1]), 0);
    p.units.assign(p.t.samplers.size(), 0);
    p.locations.clear();
    for (std::size_t i = 0; i < p.t.uniforms.size(); ++i) p.locations[p.t.uniforms[i].name] = static_cast<int>(i);
    p.fix = -1;
    if (auto it = p.locations.find(kFixName); it != p.locations.end()) {
        const UniformSlot& u = p.t.uniforms[static_cast<std::size_t>(it->second)];
        p.fix = u.offset[0] >= 0 ? u.offset[0] : u.offset[1];
    }
    p.linked = true;
    p.log.clear();
}

int offset_of(const UniformSlot& u) { return u.offset[0] >= 0 ? u.offset[0] : u.offset[1]; }

// A value set at a location, as GL takes it: of the uniform's own type and
// size, or refused (GL_INVALID_OPERATION, nothing set).
void put(GLint at, const void* v, std::size_t bytes, bool whole = false) {
    ProgObj* p = g->programs.get(g->program);
    if (!p || at < 0) return;
    if (static_cast<std::size_t>(at) >= p->t.uniforms.size()) return fail(GL_INVALID_OPERATION);
    const UniformSlot& u = p->t.uniforms[static_cast<std::size_t>(at)];
    static constexpr std::size_t sizes[] = {4, 8, 12, 16, 64, 36, 4, 4, 4};
    const bool is_whole = u.kind == UniformSlot::i1 || u.kind == UniformSlot::u1;
    if (u.kind == UniformSlot::image || is_whole != whole || sizes[u.kind] != bytes) return fail(GL_INVALID_OPERATION);
    const int o = offset_of(u);
    if (o < 0 || static_cast<std::size_t>(o) + bytes > p->block.size()) return;
    p->write(o, v, bytes);
}

// --- what a draw is made with -------------------------------------------------------

Raster raster() {
    State& s = *g;
    Raster r;
    r.blend = s.blend;
    r.src_rgb = factor_of(s.src_rgb), r.dst_rgb = factor_of(s.dst_rgb);
    r.src_a = factor_of(s.src_a), r.dst_a = factor_of(s.dst_a);
    r.color_mask = s.color_mask;
    r.depth_test = s.depth_test;
    r.depth_write = s.depth_mask;
    r.depth_op = compare_of(s.depth_func);
    r.cull = s.cull;
    r.cull_front = s.cull_face == GL_FRONT;
    r.front_cw = s.front_face != GL_CCW;
    r.depth_clamp = s.depth_clamp;
    r.bias = s.offset_fill;
    r.bias_factor = s.bias_factor, r.bias_units = s.bias_units;
    return r;
}

void fill_view(Draw& d, const Target& t) {
    State& s = *g;
    std::copy(s.viewport, s.viewport + 4, d.viewport);
    d.depth_range[0] = static_cast<float>(s.depth_near), d.depth_range[1] = static_cast<float>(s.depth_far);
    if (s.scissor_test) {
        std::copy(s.scissor, s.scissor + 4, d.scissor);
    } else {
        d.scissor[0] = d.scissor[1] = 0, d.scissor[2] = t.w, d.scissor[3] = t.h;
    }
    std::copy(s.blend_color, s.blend_color + 4, d.blend_constant);
}

int sampled_slot(Sampled k) {
    switch (k) {
        case Sampled::tex2d_array: return 1;
        case Sampled::tex3d: return 2;
        case Sampled::cube: return 3;
        case Sampled::buffer: return 4;
        case Sampled::tex2d_ms: return 5;
        default: return 0;
    }
}

void bind_samplers(Bound* bound, std::size_t most, ProgObj& p) {
    State& s = *g;
    for (std::size_t i = 0; i < p.t.samplers.size() && i < most; ++i) {
        const SamplerSlot& slot = p.t.samplers[i];
        Bound& b = bound[i];
        b.kind = slot.kind;
        const int unit = std::clamp(p.units[i], 0, kUnits - 1);
        TexObj* t = s.textures.get(s.bound[static_cast<std::size_t>(unit)][static_cast<std::size_t>(sampled_slot(slot.kind))]);
        if (!t) continue;
        if (slot.kind == Sampled::buffer) {
            if (BufferObj* buf = s.buffers.get(t->buffer)) {
                auto [where, at] = place(*buf);
                b.buffer = where, b.buffer_at = at, b.buffer_bytes = buf->size;
            }
            continue;
        }
        b.tex = realize(*t);
        if (const SamplerInfo* so = s.samplers.get(s.sampler_bound[static_cast<std::size_t>(unit)])) b.sampler = *so;
        else b.sampler = t->sampler;
        b.sampler.compare = slot.shadow;
        b.base_level = t->base_level, b.max_level = t->max_level;
    }
}

void fill_inputs(Draw& d, const ProgObj& p) {
    State& s = *g;
    const VaoObj* v = s.vaos.get(s.vao);
    for (int loc = 0; loc < 16; ++loc) {
        if (!(p.t.inputs & (1u << loc))) continue;
        VertexInput& in = d.inputs[loc];
        std::copy(s.generic[static_cast<std::size_t>(loc)].begin(), s.generic[static_cast<std::size_t>(loc)].end(), in.value);
        if (!v) continue;
        const AttribObj& a = v->a[static_cast<std::size_t>(loc)];
        if (!a.enabled) continue;
        BufferObj* b = s.buffers.get(a.buffer);
        if (!b) continue;
        auto [where, at] = place(*b);
        in.buffer = where;
        in.offset = static_cast<std::uint32_t>(at + a.offset);
        in.components = static_cast<std::uint8_t>(a.size);
        in.stride = static_cast<std::uint32_t>(a.stride ? a.stride : a.size * 4);
        in.per_instance = a.divisor > 0;
    }
}

void set_fix(ProgObj& p) {
    if (p.fix < 0) return;
    const float fix[4] = {g->dev->y_flip(), g->zero_to_one ? 1.0f : 0.0f, static_cast<float>(g->clips), 0.0f};
    p.write(p.fix, fix, sizeof fix);
}

// `indirect`: `count` commands read from it, at `indirect_at`.
void draw(int first, int count, int instances, Buffer* indirect = nullptr, std::size_t indirect_at = 0) {
    State& s = *g;
    ProgObj* p = s.programs.get(s.program);
    if (!p || !p->linked || p->t.compute || count <= 0 || instances <= 0) return;
    const Target t = resolve_target(s.draw_fbo);
    s.dev->target(t);
    Draw d;
    d.program = p->gpu;
    d.raster = raster();
    fill_view(d, t);
    fill_inputs(d, *p);
    bind_samplers(d.bound, std::size(d.bound), *p);
    set_fix(*p);
    d.uniforms = p->block.empty() ? nullptr : p->block.data();
    d.uniforms_made = p->made;
    d.first = first, d.count = count, d.instances = instances;
    d.indirect = indirect, d.indirect_at = indirect_at;
    s.dev->draw(d);
}

// Work on the card's own, with the program in use and the storage buffers
// bound where it declares them.
void dispatch(GLuint x, GLuint y, GLuint z) {
    State& s = *g;
    ProgObj* p = s.programs.get(s.program);
    if (!p || !p->linked || !p->t.compute) return fail(GL_INVALID_OPERATION);
    if (!x || !y || !z) return;
    Dispatch d;
    d.program = p->gpu;
    d.uniforms = p->block.empty() ? nullptr : p->block.data();
    for (const StorageSlot& slot : p->t.storage) {
        BufferObj* b = slot.binding >= 0 && slot.binding < 16 ? s.buffers.get(s.storage_bound[static_cast<std::size_t>(slot.binding)]) : nullptr;
        if (!b || !b->size) return fail(GL_INVALID_OPERATION);
        d.storage[slot.binding] = {on_card(*b), 0, b->size};
    }
    if (p->t.samplers.size() > std::size(d.bound)) return fail(GL_INVALID_OPERATION);
    bind_samplers(d.bound, std::size(d.bound), *p);
    d.groups[0] = static_cast<int>(x), d.groups[1] = static_cast<int>(y), d.groups[2] = static_cast<int>(z);
    s.dev->dispatch(d);
}

// --- what GL does with draws of its own ------------------------------------------------

const char* kBlitVs = R"(#version 330 core
uniform vec4 uRect;
out vec2 vUV;
void main() {
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    vUV = mix(uRect.xy, uRect.zw, p * 0.5 + 0.5);
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

const char* kInternalFs[kInternals] = {
    R"(#version 330 core
uniform sampler2D uSrc;
in vec2 vUV;
out vec4 o;
void main() { o = textureLod(uSrc, vUV, 0.0); }
)",
    R"(#version 330 core
uniform sampler2DArray uSrc;
uniform float uLayer;
in vec2 vUV;
out vec4 o;
void main() { o = textureLod(uSrc, vec3(vUV, uLayer), 0.0); }
)",
    R"(#version 330 core
uniform sampler2DMS uSrc;
uniform int uSamples;
in vec2 vUV;
out vec4 o;
void main() {
    ivec2 at = ivec2(vUV * vec2(textureSize(uSrc)));
    vec4 sum = vec4(0.0);
    for (int i = 0; i < uSamples; ++i) sum += texelFetch(uSrc, at, i);
    o = sum / float(uSamples);
}
)",
    R"(#version 330 core
uniform sampler2D uSrc;
in vec2 vUV;
void main() { gl_FragDepth = textureLod(uSrc, vUV, 0.0).r; }
)",
    R"(#version 330 core
uniform sampler2DArray uSrc;
uniform float uLayer;
in vec2 vUV;
void main() { gl_FragDepth = textureLod(uSrc, vec3(vUV, uLayer), 0.0).r; }
)",
    R"(#version 330 core
uniform sampler2DMS uSrc;
in vec2 vUV;
void main() { gl_FragDepth = texelFetch(uSrc, ivec2(vUV * vec2(textureSize(uSrc))), 0).r; }
)",
};

ProgObj* internal(Internal which) {
    State& s = *g;
    ProgObj& p = s.internal_obj[which];
    if (!p.linked) {
        link(p, kBlitVs, kInternalFs[which], nullptr);
        if (!p.linked) std::fprintf(stderr, "stategine native: internal program %d: %s\n", which, p.log.c_str());
    }
    return p.linked ? &p : nullptr;
}

void set_internal(ProgObj& p, const char* name, const float* v, std::size_t n) {
    const int at = location(p, name);
    if (at < 0) return;
    const int o = offset_of(p.t.uniforms[static_cast<std::size_t>(at)]);
    if (o >= 0) p.write(o, v, n * sizeof(float));
}

// `src`'s rectangle (x0 y0 x1 y1, in its level's texels) drawn into `dst`'s.
void blit_one(const Attachment& src, const int sr[4], const Target& dst, const int dr[4], bool linear, bool depth) {
    State& s = *g;
    if (!src.tex) return;
    const TextureInfo& si = s.dev->info(src.tex);
    const int lw = std::max(1, si.w >> src.level), lh = std::max(1, si.h >> src.level);
    Internal which;
    if (si.samples > 1) which = depth ? depth_ms : blit_ms;
    else if (si.kind == TexKind::tex2d) which = depth ? depth_2d : blit_2d;
    else which = depth ? depth_array : blit_array;
    ProgObj* p = internal(which);
    if (!p) return;
    s.dev->target(dst);
    Draw d;
    d.program = p->gpu;
    d.raster.depth_test = depth;
    d.raster.depth_op = Compare::always;
    d.raster.depth_write = depth;
    d.raster.color_mask = depth ? 0 : 0xF;
    const int x0 = std::min(dr[0], dr[2]), y0 = std::min(dr[1], dr[3]);
    d.viewport[0] = x0, d.viewport[1] = y0, d.viewport[2] = std::abs(dr[2] - dr[0]), d.viewport[3] = std::abs(dr[3] - dr[1]);
    if (s.scissor_test) std::copy(s.scissor, s.scissor + 4, d.scissor);
    else d.scissor[0] = 0, d.scissor[1] = 0, d.scissor[2] = dst.w, d.scissor[3] = dst.h;
    // (A rectangle given backwards is the picture turned.)
    float rect[4] = {static_cast<float>(sr[0]) / lw, static_cast<float>(sr[1]) / lh, static_cast<float>(sr[2]) / lw, static_cast<float>(sr[3]) / lh};
    if (dr[2] < dr[0]) std::swap(rect[0], rect[2]);
    if (dr[3] < dr[1]) std::swap(rect[1], rect[3]);
    set_internal(*p, "uRect", rect, 4);
    const float layer = static_cast<float>(src.layer);
    set_internal(*p, "uLayer", &layer, 1);
    if (const int at = location(*p, "uSamples"); at >= 0) {
        const int n = si.samples;
        p->write(offset_of(p->t.uniforms[static_cast<std::size_t>(at)]), &n, 4);
    }
    set_fix(*p);
    Bound& b = d.bound[0];
    b.tex = src.tex;
    b.kind = si.samples > 1 ? Sampled::tex2d_ms : si.kind == TexKind::tex2d ? Sampled::tex2d : Sampled::tex2d_array;
    b.base_level = b.max_level = src.level;
    b.sampler.min = b.sampler.mag = linear ? SamplerInfo::linear : SamplerInfo::nearest;
    b.sampler.mip = SamplerInfo::none;
    b.sampler.wrap[0] = b.sampler.wrap[1] = b.sampler.wrap[2] = SamplerInfo::clamp_edge;
    d.uniforms = p->block.data();
    d.uniforms_made = p->made;
    d.count = 3;
    s.dev->draw(d);
}

void make_mipmaps(TexObj& t) {
    State& s = *g;
    t.wants_mips = true;
    Texture* tex = realize(t);
    if (!tex || is_packed(t.format) || integer(t.format)) return;
    const TextureInfo& i = s.dev->info(tex);
    const int layers = i.kind == TexKind::tex3d ? 1 : i.depth;
    for (int l = 1; l < i.levels; ++l) {
        const int pw = std::max(1, i.w >> (l - 1)), ph = std::max(1, i.h >> (l - 1));
        const int w = std::max(1, i.w >> l), h = std::max(1, i.h >> l);
        for (int z = 0; z < layers; ++z) {
            Target dst;
            dst.color[0] = {tex, l, z, true};
            dst.w = w, dst.h = h;
            const int sr[4] = {0, 0, pw, ph}, dr[4] = {0, 0, w, h};
            const bool scissor = s.scissor_test;
            s.scissor_test = false;
            blit_one({tex, l - 1, z}, sr, dst, dr, true, false);
            s.scissor_test = scissor;
        }
    }
    for (int l = 0; l < i.levels && l < 16; ++l)
        if (!t.levels[static_cast<std::size_t>(l)].defined)
            t.levels[static_cast<std::size_t>(l)] = {std::max(1, i.w >> l), std::max(1, i.h >> l), i.kind == TexKind::tex3d ? 1 : i.depth, true};
}

// --- the entry points ---------------------------------------------------------------

void Enable_(GLenum cap, bool on) {
    State& s = *g;
    switch (cap) {
        case GL_BLEND: s.blend = on; break;
        case GL_DEPTH_TEST: s.depth_test = on; break;
        case GL_CULL_FACE: s.cull = on; break;
        case GL_SCISSOR_TEST: s.scissor_test = on; break;
        case GL_DEPTH_CLAMP: s.depth_clamp = on; break;
        case GL_POLYGON_OFFSET_FILL: s.offset_fill = on; break;
        case GL_MULTISAMPLE: case GL_TEXTURE_CUBE_MAP_SEAMLESS: break;
        default:
            if (cap >= GL_CLIP_DISTANCE0 && cap < GL_CLIP_DISTANCE0 + 8) {
                const std::uint32_t bit = 1u << (cap - GL_CLIP_DISTANCE0);
                s.clips = on ? (s.clips | bit) : (s.clips & ~bit);
            }
    }
}
void Enable(GLenum cap) { Enable_(cap, true); }
void Disable(GLenum cap) { Enable_(cap, false); }

void Clear(GLbitfield mask) {
    State& s = *g;
    const Target t = resolve_target(s.draw_fbo);
    s.dev->target(t);
    int rect[4] = {0, 0, t.w, t.h};
    if (s.scissor_test) std::copy(s.scissor, s.scissor + 4, rect);
    std::uint8_t colors = 0;
    if ((mask & GL_COLOR_BUFFER_BIT) && s.color_mask)
        for (int i = 0; i < Target::kColors; ++i)
            if (t.color[i].tex) colors |= static_cast<std::uint8_t>(1u << i);
    const bool depth = (mask & GL_DEPTH_BUFFER_BIT) && s.depth_mask && t.depth.tex;
    if (colors || depth) s.dev->clear(rect, colors, s.clear_color, depth, static_cast<float>(s.clear_depth));
}
void ClearColor(GLfloat r, GLfloat gr, GLfloat b, GLfloat a) {
    g->clear_color[0] = r, g->clear_color[1] = gr, g->clear_color[2] = b, g->clear_color[3] = a;
}
void Viewport(GLint x, GLint y, GLsizei w, GLsizei h) { g->viewport[0] = x, g->viewport[1] = y, g->viewport[2] = w, g->viewport[3] = h; }
void Scissor(GLint x, GLint y, GLsizei w, GLsizei h) { g->scissor[0] = x, g->scissor[1] = y, g->scissor[2] = w, g->scissor[3] = h; }
void DepthFunc(GLenum f) { g->depth_func = f; }
void DepthRange(double n, double f) { g->depth_near = n, g->depth_far = f; }
void ClearDepth(double d) { g->clear_depth = d; }
void BlendFunc(GLenum src, GLenum dst) { g->src_rgb = g->src_a = src, g->dst_rgb = g->dst_a = dst; }
void BlendFuncSeparate(GLenum sr, GLenum dr, GLenum sa, GLenum da) { g->src_rgb = sr, g->dst_rgb = dr, g->src_a = sa, g->dst_a = da; }
void BlendColor(GLfloat r, GLfloat gr, GLfloat b, GLfloat a) {
    g->blend_color[0] = r, g->blend_color[1] = gr, g->blend_color[2] = b, g->blend_color[3] = a;
}
void CullFace(GLenum f) { g->cull_face = f; }
void FrontFace(GLenum f) { g->front_face = f; }
void PolygonOffset(GLfloat factor, GLfloat units) { g->bias_factor = factor, g->bias_units = units; }
GLenum GetError() {
    const GLenum e = g->error;
    g->error = GL_NO_ERROR;
    return e;
}
void DrawArrays(GLenum, GLint first, GLsizei count) { draw(first, count, 1); }
void DrawArraysInstanced(GLenum, GLint first, GLsizei count, GLsizei n) { draw(first, count, n); }
void MultiDrawArraysIndirect(GLenum, const void* offset, GLsizei n, GLsizei stride) {
    if (stride != 0 && stride != 16) return fail(GL_INVALID_VALUE);  // (GL's commands as it lays them, one after another)
    BufferObj* b = bound_buffer(GL_DRAW_INDIRECT_BUFFER);
    if (!b) return fail(GL_INVALID_OPERATION);
    draw(0, n, 1, on_card(*b), static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(offset)));
}
void DispatchCompute(GLuint x, GLuint y, GLuint z) { dispatch(x, y, z); }
// (What the card writes is seen by whatever is asked after it: nothing to say.)
void MemoryBarrier_(GLbitfield) {}
void BindBufferBase(GLenum target, GLuint index, GLuint n) {
    if (target != GL_SHADER_STORAGE_BUFFER) return fail(GL_INVALID_ENUM);
    if (index >= 16) return fail(GL_INVALID_VALUE);
    g->storage_bound[index] = n;
    g->storage_buffer = n;
}

VaoObj* vao() { return g->vaos.get(g->vao); }
void VertexAttribDivisor(GLuint i, GLuint d) {
    if (VaoObj* v = vao(); v && i < 16) v->a[i].divisor = d;
}
void EnableVertexAttribArray(GLuint i) {
    if (VaoObj* v = vao(); v && i < 16) v->a[i].enabled = true;
}
void DisableVertexAttribArray(GLuint i) {
    if (VaoObj* v = vao(); v && i < 16) v->a[i].enabled = false;
}
void VertexAttribPointer(GLuint i, GLint size, GLenum, GLboolean, GLsizei stride, const void* offset) {
    VaoObj* v = vao();
    if (!v || i >= 16) return fail(GL_INVALID_OPERATION);
    AttribObj& a = v->a[i];
    a.buffer = g->array_buffer;
    a.size = size;
    a.stride = stride;
    a.offset = reinterpret_cast<std::size_t>(offset);
}
void VertexAttrib4f(GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (i < 16) g->generic[i] = {x, y, z, w};
}

GLuint CreateShader(GLenum type) {
    const GLuint n = g->shaders.make();
    g->shaders.get(n)->type = type;
    return n;
}
void ShaderSource(GLuint n, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    ShaderObj* sh = g->shaders.get(n);
    if (!sh) return fail(GL_INVALID_VALUE);
    sh->src.clear();
    for (GLsizei i = 0; i < count; ++i)
        sh->src += lengths && lengths[i] >= 0 ? std::string(strings[i], static_cast<std::size_t>(lengths[i])) : std::string(strings[i]);
}
void CompileShader(GLuint n) {
    ShaderObj* sh = g->shaders.get(n);
    if (!sh) return fail(GL_INVALID_VALUE);
    const Stage st = sh->type == GL_VERTEX_SHADER ? Stage::vertex : sh->type == GL_FRAGMENT_SHADER ? Stage::fragment : Stage::compute;
    sh->log = check(st, sh->src.c_str());
    sh->ok = sh->log.empty();
}
void GetShaderiv(GLuint n, GLenum what, GLint* out) {
    ShaderObj* sh = g->shaders.get(n);
    if (!sh) return fail(GL_INVALID_VALUE);
    if (what == GL_COMPILE_STATUS) *out = sh->ok ? 1 : 0;
    else *out = static_cast<GLint>(sh->log.size() + 1);
}
void copy_log(const std::string& log, GLsizei size, GLsizei* length, GLchar* out) {
    if (size <= 0 || !out) return;
    const std::size_t n = std::min(log.size(), static_cast<std::size_t>(size - 1));
    std::memcpy(out, log.data(), n);
    out[n] = 0;
    if (length) *length = static_cast<GLsizei>(n);
}
void GetShaderInfoLog(GLuint n, GLsizei size, GLsizei* length, GLchar* out) {
    if (ShaderObj* sh = g->shaders.get(n)) copy_log(sh->log, size, length, out);
}
void DeleteShader(GLuint n) { g->shaders.drop(n); }
GLuint CreateProgram() { return g->programs.make(); }
void AttachShader(GLuint p, GLuint s) {
    if (ProgObj* po = g->programs.get(p)) po->shaders.push_back(s);
}
void LinkProgram(GLuint n) {
    ProgObj* p = g->programs.get(n);
    if (!p) return fail(GL_INVALID_VALUE);
    const char *vs = nullptr, *fs = nullptr, *cs = nullptr;
    for (GLuint s : p->shaders)
        if (ShaderObj* sh = g->shaders.get(s)) {
            if (!sh->ok) {
                p->linked = false;
                p->log = "a shader attached did not compile";
                return;
            }
            (sh->type == GL_VERTEX_SHADER ? vs : sh->type == GL_FRAGMENT_SHADER ? fs : cs) = sh->src.c_str();
        }
    if (cs) {
        if (vs || fs) {
            p->linked = false;
            p->log = "a compute shader is linked alone";
            return;
        }
        link(*p, nullptr, nullptr, cs);
        return;
    }
    if (!vs || !fs) {
        p->linked = false;
        p->log = "a program needs a vertex and a fragment shader";
        return;
    }
    link(*p, vs, fs, nullptr);
}
void GetProgramiv(GLuint n, GLenum what, GLint* out) {
    ProgObj* p = g->programs.get(n);
    if (!p) return fail(GL_INVALID_VALUE);
    if (what == GL_LINK_STATUS) *out = p->linked ? 1 : 0;
    else *out = static_cast<GLint>(p->log.size() + 1);
}
void GetProgramInfoLog(GLuint n, GLsizei size, GLsizei* length, GLchar* out) {
    if (ProgObj* p = g->programs.get(n)) copy_log(p->log, size, length, out);
}
void UseProgram(GLuint n) { g->program = n; }
void DeleteProgram(GLuint n) {
    if (ProgObj* p = g->programs.get(n); p && p->gpu) g->dev->destroy(p->gpu);
    g->programs.drop(n);
    if (g->program == n) g->program = 0;
}
GLint GetUniformLocation(GLuint n, const GLchar* name) {
    ProgObj* p = g->programs.get(n);
    if (!p || !p->linked) return -1;
    return location(*p, name);
}
void Uniform4fv(GLint at, GLsizei count, const GLfloat* v) {
    ProgObj* p = g->programs.get(g->program);
    if (!p) return;
    for (GLsizei i = 0; i < count; ++i) put(at + i, v + 4 * i, 16);
    (void)p;
}
void Uniform1ui(GLint at, GLuint v) { put(at, &v, 4, true); }
void UniformMatrix4fv(GLint at, GLsizei count, GLboolean, const GLfloat* v) {
    for (GLsizei i = 0; i < count; ++i) put(at + i, v + 16 * i, 64);
}
void Uniform1i(GLint at, GLint v) {
    ProgObj* p = g->programs.get(g->program);
    if (!p || at < 0 || static_cast<std::size_t>(at) >= p->t.uniforms.size()) return;
    const UniformSlot& u = p->t.uniforms[static_cast<std::size_t>(at)];
    if (u.kind == UniformSlot::image) {
        p->units[static_cast<std::size_t>(u.sampler)] = v;
        return;
    }
    put(at, &v, 4, true);
}
void Uniform1f(GLint at, GLfloat x) { put(at, &x, 4); }
void Uniform2f(GLint at, GLfloat x, GLfloat y) {
    const float v[2] = {x, y};
    put(at, v, 8);
}
void Uniform3f(GLint at, GLfloat x, GLfloat y, GLfloat z) {
    const float v[3] = {x, y, z};
    put(at, v, 12);
}
void Uniform4f(GLint at, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    const float v[4] = {x, y, z, w};
    put(at, v, 16);
}
void GetUniformfv(GLuint n, GLint at, GLfloat* out) {
    ProgObj* p = g->programs.get(n);
    if (!p || at < 0 || static_cast<std::size_t>(at) >= p->t.uniforms.size()) return fail(GL_INVALID_OPERATION);
    const UniformSlot& u = p->t.uniforms[static_cast<std::size_t>(at)];
    if (u.kind == UniformSlot::image) {
        *out = static_cast<float>(p->units[static_cast<std::size_t>(u.sampler)]);
        return;
    }
    const int sizes[] = {1, 2, 3, 4, 16, 12, 1, 1};
    std::memcpy(out, p->block.data() + offset_of(u), static_cast<std::size_t>(sizes[u.kind]) * 4);
}
void GetUniformiv(GLuint n, GLint at, GLint* out) {
    ProgObj* p = g->programs.get(n);
    if (!p || at < 0 || static_cast<std::size_t>(at) >= p->t.uniforms.size()) return fail(GL_INVALID_OPERATION);
    const UniformSlot& u = p->t.uniforms[static_cast<std::size_t>(at)];
    if (u.kind == UniformSlot::image) *out = p->units[static_cast<std::size_t>(u.sampler)];
    else std::memcpy(out, p->block.data() + offset_of(u), 4);
}

void GenVertexArrays(GLsizei n, GLuint* out) {
    for (GLsizei i = 0; i < n; ++i) out[i] = g->vaos.make();
}
void BindVertexArray(GLuint n) { g->vao = n; }
void DeleteVertexArrays(GLsizei n, const GLuint* names) {
    for (GLsizei i = 0; i < n; ++i) {
        g->vaos.drop(names[i]);
        if (g->vao == names[i]) g->vao = 0;
    }
}

void GenBuffers(GLsizei n, GLuint* out) {
    for (GLsizei i = 0; i < n; ++i) out[i] = g->buffers.make();
}
void BindBuffer(GLenum target, GLuint n) { *binding_for(target) = n; }
void release(BufferObj& b) {
    if (b.gpu) g->dev->destroy(b.gpu);
    if (b.card) g->dev->destroy(b.card);
    b.gpu = nullptr, b.card = nullptr, b.card_fresh = false;
}
void BufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    BufferObj* b = bound_buffer(target);
    if (!b) return fail(GL_INVALID_OPERATION);
    const auto bytes = static_cast<std::size_t>(size);
    release(*b);
    b->packs.clear();
    b->size = bytes;
    b->slice = {};
    if (usage == GL_STREAM_READ || target == GL_PIXEL_PACK_BUFFER) {
        // Read back into: the card writes it, the processor reads it.
        b->dynamic = false;
        b->shadow.clear();
        b->gpu = g->dev->create_buffer(std::max<std::size_t>(bytes, 4), Memory::readback);
        if (data) std::memcpy(g->dev->map(b->gpu), data, bytes);
    } else if ((usage == GL_STATIC_DRAW && target != GL_PIXEL_UNPACK_BUFFER) || target == GL_SHADER_STORAGE_BUFFER ||
               target == GL_DRAW_INDIRECT_BUFFER) {
        // (Or one the card writes, or reads its draws from: on the card too.)
        b->dynamic = false;
        b->shadow.clear();
        b->gpu = g->dev->create_buffer(std::max<std::size_t>(bytes, 4), Memory::card);
        if (data && bytes) g->dev->upload(b->gpu, 0, data, bytes);
    } else {
        b->dynamic = true;
        b->shadow.resize(bytes);
        if (data && bytes) std::memcpy(b->shadow.data(), data, bytes);
        b->change(g->frame);
    }
}
void BufferSubData(GLenum target, GLintptr at, GLsizeiptr size, const void* data) {
    BufferObj* b = bound_buffer(target);
    if (!b || at < 0 || static_cast<std::size_t>(at + size) > b->size) return fail(GL_INVALID_VALUE);
    if (b->dynamic) {
        std::memcpy(b->shadow.data() + at, data, static_cast<std::size_t>(size));
        b->slice = {};  // (laid again as drawn: what was drawn before keeps its bytes)
        b->change(g->frame);
    } else {
        g->dev->upload(b->gpu, static_cast<std::size_t>(at), data, static_cast<std::size_t>(size));
    }
}
void DeleteBuffers(GLsizei n, const GLuint* names) {
    for (GLsizei i = 0; i < n; ++i) {
        if (BufferObj* b = g->buffers.get(names[i])) release(*b);
        g->buffers.drop(names[i]);
        for (GLuint* at : {&g->array_buffer, &g->pack_buffer, &g->unpack_buffer, &g->texture_buffer, &g->other_buffer, &g->storage_buffer,
                           &g->indirect_buffer})
            if (*at == names[i]) *at = 0;
        for (GLuint& at : g->storage_bound)
            if (at == names[i]) at = 0;
    }
}
void GetBufferSubData(GLenum target, GLintptr at, GLsizeiptr size, void* out) {
    BufferObj* b = bound_buffer(target);
    if (!b || at < 0 || static_cast<std::size_t>(at + size) > b->size) return fail(GL_INVALID_VALUE);
    if (b->dynamic) {
        std::memcpy(out, b->shadow.data() + at, static_cast<std::size_t>(size));
        return;
    }
    std::vector<std::uint8_t> raw(b->size);
    g->dev->download(b->gpu, 0, raw.data(), raw.size());
    // Pixels read into it, handed back as they were asked for.
    for (const BufferObj::Pack& p : b->packs) {
        const std::size_t tight = static_cast<std::size_t>(p.w) * static_cast<std::size_t>(p.h) * static_cast<std::size_t>(texel_bytes(p.from));
        if (p.at + tight > raw.size()) continue;
        std::vector<std::uint8_t> texels(raw.begin() + static_cast<std::ptrdiff_t>(p.at), raw.begin() + static_cast<std::ptrdiff_t>(p.at + tight));
        std::vector<std::uint8_t> asked(row_bytes(p.w, p.format, p.type) * static_cast<std::size_t>(p.h));
        from_texels(p.from, p.w, p.h, texels.data(), p.format, p.type, asked.data());
        std::memcpy(raw.data() + p.at, asked.data(), std::min(asked.size(), raw.size() - p.at));
    }
    std::memcpy(out, raw.data() + at, static_cast<std::size_t>(size));
}
void* MapBufferRange(GLenum target, GLintptr at, GLsizeiptr size, GLbitfield) {
    BufferObj* b = bound_buffer(target);
    if (!b || !b->dynamic || static_cast<std::size_t>(at + size) > b->size) {
        fail(GL_INVALID_OPERATION);
        return nullptr;
    }
    b->slice = {};
    b->change(g->frame);  // (written through the map)
    return b->shadow.data() + at;
}
GLboolean UnmapBuffer(GLenum) { return 1; }

void GenTextures(GLsizei n, GLuint* out) {
    for (GLsizei i = 0; i < n; ++i) out[i] = g->textures.make();
}
void BindTexture(GLenum target, GLuint n) {
    if (TexObj* t = g->textures.get(n); t && t->target == 0) t->target = target;
    g->bound[static_cast<std::size_t>(g->unit)][static_cast<std::size_t>(unit_slot(target))] = n;
}
void DeleteTextures(GLsizei n, const GLuint* names) {
    for (GLsizei i = 0; i < n; ++i) {
        if (TexObj* t = g->textures.get(names[i]); t && t->gpu) g->dev->destroy(t->gpu);
        g->textures.drop(names[i]);
        for (auto& u : g->bound)
            for (GLuint& b : u)
                if (b == names[i]) b = 0;
    }
}
void ActiveTexture(GLenum unit) { g->unit = std::clamp(static_cast<int>(unit - GL_TEXTURE0), 0, kUnits - 1); }

void sampler_param(SamplerInfo& s, TexObj* t, GLenum what, GLint v) {
    auto wrap = [](GLint w) {
        return w == static_cast<GLint>(GL_REPEAT) ? SamplerInfo::repeat
               : w == static_cast<GLint>(GL_CLAMP_TO_BORDER) ? SamplerInfo::clamp_border
                                                             : SamplerInfo::clamp_edge;
    };
    switch (what) {
        case GL_TEXTURE_MIN_FILTER:
            s.min = (v == static_cast<GLint>(GL_NEAREST) || v == static_cast<GLint>(GL_NEAREST_MIPMAP_NEAREST)) ? SamplerInfo::nearest : SamplerInfo::linear;
            s.mip = v == static_cast<GLint>(GL_LINEAR_MIPMAP_LINEAR) ? SamplerInfo::mip_linear
                    : v == static_cast<GLint>(GL_NEAREST_MIPMAP_NEAREST) ? SamplerInfo::mip_nearest
                                                                         : SamplerInfo::none;
            break;
        case GL_TEXTURE_MAG_FILTER: s.mag = v == static_cast<GLint>(GL_NEAREST) ? SamplerInfo::nearest : SamplerInfo::linear; break;
        case GL_TEXTURE_WRAP_S: s.wrap[0] = wrap(v); break;
        case GL_TEXTURE_WRAP_T: s.wrap[1] = wrap(v); break;
        case GL_TEXTURE_WRAP_R: s.wrap[2] = wrap(v); break;
        case GL_TEXTURE_COMPARE_MODE: break;  // (what compares is the sampler the shader declares)
        case GL_TEXTURE_COMPARE_FUNC: s.compare_op = static_cast<std::uint8_t>(compare_of(static_cast<GLenum>(v))); break;
        case GL_TEXTURE_BASE_LEVEL: if (t) t->base_level = v; break;
        case GL_TEXTURE_MAX_LEVEL: if (t) t->max_level = v; break;
        default: break;
    }
}
void TexParameteri(GLenum target, GLenum what, GLint v) {
    if (TexObj* t = bound_texture(target)) sampler_param(t->sampler, t, what, v);
}
void TexParameterfv(GLenum target, GLenum what, const GLfloat* v) {
    TexObj* t = bound_texture(target);
    if (t && what == GL_TEXTURE_BORDER_COLOR) t->sampler.border_white = v[0] > 0.5f;
}
void TexParameterf(GLenum target, GLenum what, GLfloat v) {
    TexObj* t = bound_texture(target);
    if (!t) return;
    if (what == GL_TEXTURE_MAX_ANISOTROPY) t->sampler.anisotropy = static_cast<std::uint8_t>(std::clamp(v, 1.0f, 16.0f));
    else sampler_param(t->sampler, t, what, static_cast<GLint>(v));
}
void GenerateMipmap(GLenum target) {
    if (TexObj* t = bound_texture(target)) make_mipmaps(*t);
}
void GenSamplers(GLsizei n, GLuint* out) {
    for (GLsizei i = 0; i < n; ++i) out[i] = g->samplers.make();
}
void DeleteSamplers(GLsizei n, const GLuint* names) {
    for (GLsizei i = 0; i < n; ++i) {
        g->samplers.drop(names[i]);
        for (GLuint& b : g->sampler_bound)
            if (b == names[i]) b = 0;
    }
}
void BindSampler(GLuint unit, GLuint n) {
    if (unit < kUnits) g->sampler_bound[unit] = n;
}
void SamplerParameteri(GLuint n, GLenum what, GLint v) {
    if (SamplerInfo* s = g->samplers.get(n)) sampler_param(*s, nullptr, what, v);
}

// Where pixels given come from: the bound unpack buffer's bytes, or memory.
const void* unpack_source(const void* data) {
    if (BufferObj* b = g->buffers.get(g->unpack_buffer); b && b->dynamic)
        return b->shadow.data() + reinterpret_cast<std::size_t>(data);
    return data;
}

void TexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint, GLenum format, GLenum type, const void* data) {
    TexObj* t = bound_texture(target);
    if (!t) return fail(GL_INVALID_OPERATION);
    const bool face = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6;
    define(*t, level, static_cast<GLenum>(internal), w, h, face ? 6 : 1);
    if (!data && !g->unpack_buffer) return;
    Texture* tex = realize(*t);
    if (!tex) return;
    const auto texels = to_texels(t->format, w, h, 1, format, type, unpack_source(data));
    g->dev->write(tex, level, 0, 0, face ? static_cast<int>(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X) : 0, w, h, 1, texels.data());
}
void TexImage3D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d, GLint, GLenum format, GLenum type, const void* data) {
    TexObj* t = bound_texture(target);
    if (!t) return fail(GL_INVALID_OPERATION);
    define(*t, level, static_cast<GLenum>(internal), w, h, d);
    if (!data) return;
    Texture* tex = realize(*t);
    if (!tex) return;
    const auto texels = to_texels(t->format, w, h, d, format, type, data);
    g->dev->write(tex, level, 0, 0, 0, w, h, d, texels.data());
}
void TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data) {
    TexObj* t = bound_texture(target);
    if (!t) return fail(GL_INVALID_OPERATION);
    Texture* tex = realize(*t);
    if (!tex) return fail(GL_INVALID_OPERATION);
    const bool face = target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6;
    const auto texels = to_texels(t->format, w, h, 1, format, type, unpack_source(data));
    g->dev->write(tex, level, x, y, face ? static_cast<int>(target - GL_TEXTURE_CUBE_MAP_POSITIVE_X) : 0, w, h, 1, texels.data());
}
void CompressedTexImage2D(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h, GLint, GLsizei, const void* data) {
    TexObj* t = bound_texture(target);
    if (!t) return fail(GL_INVALID_OPERATION);
    define(*t, level, internal, w, h, 1);
    Texture* tex = realize(*t);
    if (tex && data) g->dev->write(tex, level, 0, 0, 0, w, h, 1, data);
}
void TexBuffer(GLenum, GLenum, GLuint buffer) {
    if (TexObj* t = bound_texture(GL_TEXTURE_BUFFER)) t->buffer = buffer;
}

// What the front offers beyond GL 3.3, as a 3.3 context with them says.
const char* const kExtensions[] = {"GL_ARB_clip_control",   "GL_ARB_texture_compression_bptc",    "GL_ARB_texture_filter_anisotropic",
                                   "GL_ARB_compute_shader", "GL_ARB_shader_storage_buffer_object", "GL_ARB_multi_draw_indirect",
                                   "GL_ARB_draw_indirect"};

const unsigned char* GetStringi(GLenum what, GLuint i) {
    if (what != GL_EXTENSIONS || i >= std::size(kExtensions)) return nullptr;
    return reinterpret_cast<const unsigned char*>(kExtensions[i]);
}
const unsigned char* GetString(GLenum what) {
    switch (what) {
        case GL_VENDOR: return reinterpret_cast<const unsigned char*>("stategine");
        case GL_RENDERER: return reinterpret_cast<const unsigned char*>(g->renderer.c_str());
        case GL_VERSION: return reinterpret_cast<const unsigned char*>(g->version.c_str());
        default: return nullptr;
    }
}

void blit_one(const Attachment& src, const int sr[4], const Target& dst, const int dr[4], bool linear, bool depth);

void hand_back(Texture* tex, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out) {
    if (!tex || w <= 0 || h <= 0) return;
    if (const Format packed = g->dev->info(tex).format; is_packed(packed)) {
        // A packed picture is handed back unpacked, as GL does: drawn into a
        // plain one (sRGB kept as it is coded), and that read.
        TextureInfo plain;
        plain.format = packed == Format::bc7_srgb ? Format::srgba8 : Format::rgba8;
        plain.w = w, plain.h = h;
        Texture* into = g->dev->create_texture(plain);
        Target t;
        t.color[0] = {into, 0, 0, true};
        t.w = w, t.h = h;
        const int sr[4] = {x, y, x + w, y + h}, dr[4] = {0, 0, w, h};
        const bool scissor = g->scissor_test;
        g->scissor_test = false;
        blit_one({tex, level, layer}, sr, t, dr, false, false);
        g->scissor_test = scissor;
        hand_back(into, 0, 0, 0, 0, w, h, format, type, out);
        g->dev->destroy(into);
        return;
    }
    const Format f = g->dev->info(tex).format;
    if (BufferObj* b = g->buffers.get(g->pack_buffer); b && b->gpu) {
        const std::size_t at = reinterpret_cast<std::size_t>(out);
        g->dev->read_into(tex, level, layer, x, y, w, h, b->gpu, at);
        // (What it held there before is written over.)
        const std::size_t end = at + row_bytes(w, format, type) * static_cast<std::size_t>(h);
        b->packs.erase(std::remove_if(b->packs.begin(), b->packs.end(),
                                      [&](const BufferObj::Pack& p) {
                                          return p.at < end && at < p.at + row_bytes(p.w, p.format, p.type) * static_cast<std::size_t>(p.h);
                                      }),
                       b->packs.end());
        b->packs.push_back({f, w, h, format, type, at});
        return;
    }
    std::vector<std::uint8_t> texels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * static_cast<std::size_t>(texel_bytes(f)));
    g->dev->read(tex, level, layer, x, y, w, h, texels.data());
    from_texels(f, w, h, texels.data(), format, type, out);
}
void GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* out) {
    TexObj* t = bound_texture(target);
    if (!t) return fail(GL_INVALID_OPERATION);
    Texture* tex = realize(*t);
    if (!tex) return fail(GL_INVALID_OPERATION);
    const TextureInfo& i = g->dev->info(tex);
    hand_back(tex, level, 0, 0, 0, std::max(1, i.w >> level), std::max(1, i.h >> level), format, type, out);
}
void ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* out) {
    const Attachment a = read_attachment(g->read_fbo, format == GL_DEPTH_COMPONENT);
    hand_back(a.tex, a.level, a.layer, x, y, w, h, format, type, out);
}

void GenFramebuffers(GLsizei n, GLuint* out) {
    for (GLsizei i = 0; i < n; ++i) out[i] = g->fbos.make();
}
void BindFramebuffer(GLenum target, GLuint n) {
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) g->draw_fbo = n;
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) g->read_fbo = n;
}
void DeleteFramebuffers(GLsizei n, const GLuint* names) {
    for (GLsizei i = 0; i < n; ++i) {
        g->fbos.drop(names[i]);
        if (g->draw_fbo == names[i]) g->draw_fbo = 0;
        if (g->read_fbo == names[i]) g->read_fbo = 0;
    }
}
FboObj* fbo_for(GLenum target) {
    return g->fbos.get(target == GL_READ_FRAMEBUFFER ? g->read_fbo : g->draw_fbo);
}
AttachObj* attach_point(FboObj& f, GLenum attachment) {
    if (attachment == GL_DEPTH_ATTACHMENT) return &f.depth;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + 8) return &f.color[attachment - GL_COLOR_ATTACHMENT0];
    return nullptr;
}
void FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level) {
    FboObj* f = fbo_for(target);
    AttachObj* a = f ? attach_point(*f, attachment) : nullptr;
    if (!a) return fail(GL_INVALID_OPERATION);
    const bool face = textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && textarget < GL_TEXTURE_CUBE_MAP_POSITIVE_X + 6;
    *a = {tex, false, level, face ? static_cast<int>(textarget - GL_TEXTURE_CUBE_MAP_POSITIVE_X) : 0};
}
void FramebufferTextureLayer(GLenum target, GLenum attachment, GLuint tex, GLint level, GLint layer) {
    FboObj* f = fbo_for(target);
    AttachObj* a = f ? attach_point(*f, attachment) : nullptr;
    if (!a) return fail(GL_INVALID_OPERATION);
    *a = {tex, false, level, layer};
}
void FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum, GLuint rb) {
    FboObj* f = fbo_for(target);
    AttachObj* a = f ? attach_point(*f, attachment) : nullptr;
    if (!a) return fail(GL_INVALID_OPERATION);
    *a = {rb, true, 0, 0};
}
GLenum CheckFramebufferStatus(GLenum) { return GL_FRAMEBUFFER_COMPLETE; }
// Renderbuffers are textures nothing samples, named among the textures.
void GenRenderbuffers(GLsizei n, GLuint* out) {
    for (GLsizei i = 0; i < n; ++i) {
        out[i] = g->textures.make();
        TexObj* t = g->textures.get(out[i]);
        t->target = GL_TEXTURE_2D;
        t->renderbuffer = true;
    }
}
void BindRenderbuffer(GLenum, GLuint n) { g->renderbuffer = n; }
void DeleteRenderbuffers(GLsizei n, const GLuint* names) { DeleteTextures(n, names); }
void RenderbufferStorageMultisample(GLenum, GLsizei samples, GLenum internal, GLsizei w, GLsizei h) {
    TexObj* t = g->textures.get(g->renderbuffer);
    if (!t) return fail(GL_INVALID_OPERATION);
    t->samples = std::max(1, static_cast<int>(samples));
    define(*t, 0, internal, w, h, 1);
}
void RenderbufferStorage(GLenum target, GLenum internal, GLsizei w, GLsizei h) { RenderbufferStorageMultisample(target, 1, internal, w, h); }
void BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter) {
    const Target dst = resolve_target(g->draw_fbo);
    const int sr[4] = {sx0, sy0, sx1, sy1}, dr[4] = {dx0, dy0, dx1, dy1};
    if (mask & GL_COLOR_BUFFER_BIT) {
        Target colors = dst;
        colors.depth = {};
        blit_one(read_attachment(g->read_fbo, false), sr, colors, dr, filter == GL_LINEAR, false);
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        Target depth;
        depth.depth = dst.depth;
        depth.w = dst.w, depth.h = dst.h;
        blit_one(read_attachment(g->read_fbo, true), sr, depth, dr, false, true);
    }
}
void DrawBuffer(GLenum b) {
    FboObj& f = fbo(g->draw_fbo);
    f.draw[0] = b == GL_BACK ? GL_COLOR_ATTACHMENT0 : b;
    for (int i = 1; i < 8; ++i) f.draw[i] = GL_NONE;
}
void DrawBuffers(GLsizei n, const GLenum* bs) {
    FboObj& f = fbo(g->draw_fbo);
    for (int i = 0; i < 8; ++i) f.draw[i] = i < n ? bs[i] : GL_NONE;
}
void ReadBuffer(GLenum b) { fbo(g->read_fbo).read = b == GL_BACK ? GL_COLOR_ATTACHMENT0 : b; }

void GetIntegerv(GLenum what, GLint* out) {
    State& s = *g;
    switch (what) {
        case GL_NUM_EXTENSIONS: *out = static_cast<GLint>(std::size(kExtensions)); break;
        case GL_MAX_TEXTURE_IMAGE_UNITS: *out = kUnits; break;
        case GL_MAX_SAMPLES: *out = 8; break;
        case GL_CURRENT_PROGRAM: *out = static_cast<GLint>(s.program); break;
        case GL_DRAW_FRAMEBUFFER_BINDING: *out = static_cast<GLint>(s.draw_fbo); break;
        case GL_READ_FRAMEBUFFER_BINDING: *out = static_cast<GLint>(s.read_fbo); break;
        default: *out = 0, fail(GL_INVALID_ENUM);
    }
}
void DepthMask(GLboolean on) { g->depth_mask = on != 0; }
void ColorMask(GLboolean r, GLboolean gr, GLboolean b, GLboolean a) {
    g->color_mask = static_cast<std::uint8_t>((r ? 1 : 0) | (gr ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
}


void GenQueries(GLsizei n, GLuint* out) {
    for (GLsizei i = 0; i < n; ++i) {
        if (g->spare_slots.empty()) {
            out[i] = 0;
            fail(GL_INVALID_OPERATION);
            continue;
        }
        g->queries.push_back({g->spare_slots.back(), 0});
        g->spare_slots.pop_back();
        out[i] = static_cast<GLuint>(g->queries.size() - 1);
    }
}
void DeleteQueries(GLsizei n, const GLuint* names) {
    for (GLsizei i = 0; i < n; ++i)
        if (names[i] < g->queries.size() && g->queries[names[i]].slot >= 0) {
            g->spare_slots.push_back(g->queries[names[i]].slot);
            g->queries[names[i]].slot = -1;
        }
}
QueryObj* query(GLuint n) { return n < g->queries.size() && g->queries[n].slot >= 0 ? &g->queries[n] : nullptr; }
void BeginQuery(GLenum target, GLuint n) {
    QueryObj* q = query(n);
    if (!q) return fail(GL_INVALID_OPERATION);
    q->target = target;
    // (Asked of what is drawn into: the target as it is now.)
    g->dev->target(resolve_target(g->draw_fbo));
    g->dev->begin_query(q->slot);
    g->asking = n;
}
void EndQuery(GLenum) {
    if (QueryObj* q = query(g->asking)) g->dev->end_query(q->slot);
    g->asking = 0;
}
void QueryCounter(GLuint n, GLenum target) {
    QueryObj* q = query(n);
    if (!q) return fail(GL_INVALID_OPERATION);
    q->target = target;
    g->dev->stamp(q->slot);
}
void GetQueryObjectiv(GLuint n, GLenum what, GLint* out) {
    QueryObj* q = query(n);
    if (!q) return fail(GL_INVALID_OPERATION);
    std::uint64_t v = 0;
    const bool known = g->dev->answer(q->slot, what != GL_QUERY_RESULT_AVAILABLE, v);
    if (what == GL_QUERY_RESULT_AVAILABLE) *out = known ? 1 : 0;
    else *out = q->target == GL_ANY_SAMPLES_PASSED ? (v > 0 ? 1 : 0) : static_cast<GLint>(v);
}
void GetQueryObjectui64v(GLuint n, GLenum what, std::uint64_t* out) {
    QueryObj* q = query(n);
    if (!q) return fail(GL_INVALID_OPERATION);
    std::uint64_t v = 0;
    const bool known = g->dev->answer(q->slot, what != GL_QUERY_RESULT_AVAILABLE, v);
    *out = what == GL_QUERY_RESULT_AVAILABLE ? (known ? 1 : 0) : q->target == GL_ANY_SAMPLES_PASSED ? (v > 0 ? 1 : 0) : v;
}
void BeginConditionalRender(GLuint n, GLenum) {
    if (QueryObj* q = query(n)) g->dev->only_if(q->slot);
}
void EndConditionalRender() { g->dev->only_if(-1); }
void Finish() { g->dev->finish(); }
void ClipControl(GLenum, GLenum depth) { g->zero_to_one = depth == GL_ZERO_TO_ONE; }

}  // namespace

#if !defined(_WIN32)
// (D3D12 is Windows' own: built nowhere else.)
std::unique_ptr<Device> make_d3d12(GLFWwindow*, std::string& why) {
    why = "D3D12 is Windows' own";
    return nullptr;
}
#endif
}  // namespace native

namespace {
Api asked = Api::gl;
}

Api asked_api() {
    if (const char* e = std::getenv("SG_GPU"); e && *e) {
        const std::string s = e;
        if (s == "vulkan" || s == "vk") return Api::vulkan;
        if (s == "d3d12" || s == "dx12" || s == "d3d") return Api::d3d12;
        if (s == "gl" || s == "opengl") return Api::gl;
    }
    return asked;
}
void use_api(Api a) { asked = a; }
const char* api_name(Api a) { return a == Api::vulkan ? "Vulkan" : a == Api::d3d12 ? "D3D12" : "OpenGL"; }
Api current_api() { return native::g ? native::g->api : Api::gl; }

bool start_native(Api api, GLFWwindow* window, std::string& why) {
    using namespace native;
    if (api == Api::gl) {
        why = "GL is not native";
        return false;
    }
    auto dev = api == Api::vulkan ? make_vulkan(window, why) : make_d3d12(window, why);
    if (!dev) return false;
    g = new State;
    g->dev = std::move(dev);
    g->api = api;
    g->window = window;
    g->renderer = std::string(g->dev->name()) + " (" + api_name(api) + ")";
    for (int i = Device::kQueries - 1; i >= 0; --i) g->spare_slots.push_back(i);
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    g->viewport[2] = g->scissor[2] = w;
    g->viewport[3] = g->scissor[3] = h;
#define SG_NATIVE_SET(ret, name, args) sg::gl::gl##name = &native::name;
    SG_GL_FUNCS(SG_NATIVE_SET)
#undef SG_NATIVE_SET
    glClipControl = &native::ClipControl;
    glDispatchCompute = &native::DispatchCompute;
    glMemoryBarrier = &native::MemoryBarrier_;
    glBindBufferBase = &native::BindBufferBase;
    glMultiDrawArraysIndirect = &native::MultiDrawArraysIndirect;
    return true;
}

void stop_native() {
    using namespace native;
    if (!g) return;
    g->dev->finish();
    // (What the card holds for the calls let go of before the card is.)
    for (auto& b : g->buffers.all)
        if (b && b->gpu) g->dev->destroy(b->gpu);
    for (auto& t : g->textures.all)
        if (t && t->gpu) g->dev->destroy(t->gpu);
    for (auto& p : g->programs.all)
        if (p && p->gpu) g->dev->destroy(p->gpu);
    for (ProgObj& p : g->internal_obj)
        if (p.gpu) g->dev->destroy(p.gpu);
    for (State::Chunk& c : g->chunks) g->dev->destroy(c.buffer);
    g->dev->finish();
#define SG_NATIVE_UNSET(ret, name, args) sg::gl::gl##name = nullptr;
    SG_GL_FUNCS(SG_NATIVE_UNSET)
#undef SG_NATIVE_UNSET
    glClipControl = nullptr;
    glDispatchCompute = nullptr;
    glMemoryBarrier = nullptr;
    glBindBufferBase = nullptr;
    glMultiDrawArraysIndirect = nullptr;
    delete g;
    g = nullptr;
}

void present_native(bool vsync) {
    using namespace native;
    if (!g) return;
    g->dev->present(vsync);
    ++g->frame;
}

bool can_upscale(std::string& why) {
    using namespace native;
    if (!g) {
        why = "drawn by OpenGL, which has no upscaler of the card's";
        return false;
    }
    return g->dev->can_upscale(why);
}

bool upscale(const Upscale& u, std::string& why) {
    using namespace native;
    if (!g) return can_upscale(why);
    const auto tex = [](unsigned name) -> Texture* {
        TexObj* t = g->textures.get(name);
        return t ? realize(*t) : nullptr;
    };
    Device::Upscale d;
    d.color = tex(u.color), d.depth = tex(u.depth), d.motion = tex(u.motion), d.output = tex(u.output);
    d.jitter_x = u.jitter_x, d.jitter_y = u.jitter_y, d.motion_x = u.motion_x, d.motion_y = u.motion_y;
    d.reset = u.reset, d.reversed = u.reversed, d.quality = u.quality, d.frame_ms = u.frame_ms, d.exposure = u.exposure;
    return g->dev->upscale(d, why);
}

}  // namespace sg::gl
