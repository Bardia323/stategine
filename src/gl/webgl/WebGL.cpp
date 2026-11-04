// The renderer in a browser: sg/gl's GL 3.3 calls answered by WebGL 2.
//
// WebGL 2 is OpenGL ES 3.0 - nearly the GL the renderer speaks, but its
// shaders are another language (GLSL ES 3.00: no implicit conversions, no
// buffer textures, precision said) and a few calls are missing or stricter.
// This is what stands between them, and nothing more: each shader the
// renderer hands GL is turned into GLSL ES as it is given - read by glslang
// as the GL it was written for, taken to SPIR-V, written back as GLSL ES by
// SPIRV-Cross - and kept on disk by what it was made from (sg::cache:
// "essl"); and each call WebGL lacks, or takes more strictly, is answered
// by the nearest it has. Nothing is drawn differently: what a call cannot
// do in a browser (a timer on the card, a draw waiting on an occlusion
// query) it does not do, and says nothing.
//
// Built where SG_WEBGL is (a browser's build; on the desktop, to try the
// same shaders on the desktop's GL, which takes GLSL ES as well).
#include "sg/gl/GL.hpp"
#include "sg/core/Cache.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <array>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_glsl.hpp>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5_webgl.h>
#endif

namespace sg::gl {
namespace {

// What the browser's calls are, beneath the ones answered here.
struct Real {
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*Enable)(GLenum) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (*TexParameterf)(GLenum, GLenum, GLfloat) = nullptr;
    void (*TexParameterfv)(GLenum, GLenum, const GLfloat*) = nullptr;
    void (*SamplerParameteri)(GLuint, GLenum, GLint) = nullptr;
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*TexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
    void (*RenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei) = nullptr;
    void (*DrawBuffers)(GLsizei, const GLenum*) = nullptr;
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*GetIntegerv)(GLenum, GLint*) = nullptr;
    void (*BindBuffer)(GLenum, GLuint) = nullptr;
    void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    void (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    void (*ClearDepthf)(GLfloat) = nullptr;
    void (*DepthRangef)(GLfloat, GLfloat) = nullptr;
    void (*BindTexture)(GLenum, GLuint) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;
    void (*FramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint) = nullptr;
    void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void (*DrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, GLchar*) = nullptr;
    GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    void (*BindSampler)(GLuint, GLuint) = nullptr;
    void (*CompressedTexImage2D)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
};
Real real;

constexpr GLenum kShaderType = 0x8B4F, kFramebufferBinding = 0x8CA6, kReadFramebufferBinding = 0x8CAA,
                 kPackBufferBinding = 0x88ED, kUnpackBufferBinding = 0x88EF, kTextureBinding2D = 0x8069;
constexpr GLenum kProgramPointSize = 0x8642, kFramebufferSrgb = 0x8DB9, kLineSmooth = 0x0B20, kPolygonSmooth = 0x0B41;
constexpr GLenum kLodBias = 0x8501, kDepthComponent32 = 0x81A7, kDepthComponent16 = 0x81A5;
constexpr GLenum kR8 = 0x8229, kRG8 = 0x822B, kR16F = 0x822D, kRG32F_ = 0x8230, kRGB32F = 0x8815, kR11G11B10F = 0x8C3A,
                 kRGB8 = 0x8051, kSRGB8 = 0x8C41, kRedInteger = 0x8D94, kRGBA_ = 0x1908, kRGB_ = 0x1907, kRED_ = 0x1903,
                 kUnsignedShort = 0x1403, kR16 = 0x822A, kRG16 = 0x822C, kRGBA16 = 0x805B;

// --- the shaders ---------------------------------------------------------------

void start_glslang() {
    static bool started = false;
    if (!started) glslang::InitializeProcess(), started = true;
}

// GLSL 3.30 (or what the renderer wrote) as GLSL ES 3.00, or empty and why.
std::string essl_of(EShLanguage stage, const std::string& src, std::string& why) {
    start_glslang();
    glslang::TShader sh(stage);
    const char* text = src.c_str();
    sh.setStrings(&text, 1);
    sh.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 100);
    sh.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    sh.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
    sh.setAutoMapLocations(true);
    sh.setAutoMapBindings(true);
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules);
    if (!sh.parse(GetDefaultResources(), 330, ECoreProfile, false, false, messages)) {
        why = sh.getInfoLog();
        return {};
    }
    glslang::TProgram program;
    program.addShader(&sh);
    if (!program.link(messages) || !program.mapIO()) {
        why = program.getInfoLog();
        return {};
    }
    std::vector<unsigned> spirv;
    glslang::SpvOptions keep;
    keep.generateDebugInfo = false;
    keep.disableOptimizer = true;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &keep);

    spirv_cross::CompilerGLSL out(std::move(spirv));
    spirv_cross::CompilerGLSL::Options o = out.get_common_options();
    o.version = 300;
    o.es = true;
    o.vulkan_semantics = false;
    o.enable_420pack_extension = false;
    o.emit_uniform_buffer_as_plain_uniforms = true;
    o.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
    o.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
    out.set_common_options(o);
    // GLSL ES 3.00 says where only a vertex's inputs and a fragment's
    // outputs are; the rest - uniforms, samplers, what passes between the
    // stages - are found by name, as the renderer finds them.
    const spirv_cross::ShaderResources r = out.get_shader_resources();
    const auto loose = [&](const spirv_cross::SmallVector<spirv_cross::Resource>& list) {
        for (const spirv_cross::Resource& x : list) {
            out.unset_decoration(x.id, spv::DecorationLocation);
            out.unset_decoration(x.id, spv::DecorationBinding);
            out.unset_decoration(x.id, spv::DecorationDescriptorSet);
        }
    };
    loose(r.sampled_images);
    loose(r.gl_plain_uniforms);
    loose(r.uniform_buffers);
    if (stage == EShLangVertex) loose(r.stage_outputs);
    if (stage == EShLangFragment) loose(r.stage_inputs);
    // (Depth tested before a fragment is shaded: a hint GLSL ES 3.00 cannot
    // give - the card does it where nothing the fragment does forbids it.)
    out.unset_execution_mode(spv::ExecutionModeEarlyFragmentTests);
    try {
        return out.compile();
    } catch (const std::exception& e) {
        why = e.what();
        return {};
    }
}

// The GLSL ES a shader is in a browser, kept on disk by what it was made
// from: the first start in a browser makes them, every later one reads them.
std::string essl(GLenum type, const std::string& src, std::string& why) {
    const EShLanguage stage = type == GL_VERTEX_SHADER ? EShLangVertex : type == GL_FRAGMENT_SHADER ? EShLangFragment : EShLangCompute;
    const Digest key = Hasher{}.text("essl 2").integer(static_cast<long long>(stage)).text(src).digest();
    std::string kept;
    if (cache::load("essl", key, kept) && !kept.empty()) return kept;
    std::string made = essl_of(stage, src, why);
    if (!made.empty()) cache::store("essl", key, made);
    return made;
}

void shader_source(GLuint shader, GLsizei n, const GLchar* const* strings, const GLint* lengths) {
    std::string src;
    for (GLsizei i = 0; i < n; ++i)
        src.append(strings[i], lengths && lengths[i] >= 0 ? static_cast<std::size_t>(lengths[i]) : std::strlen(strings[i]));
    // (Already GLSL ES: as it is.)
    if (src.find("#version 300 es") != std::string::npos) return real.ShaderSource(shader, n, strings, lengths);
    GLint type = 0;
    real.GetShaderiv(shader, kShaderType, &type);
    std::string why;
    const std::string es = essl(static_cast<GLenum>(type), src, why);
    if (es.empty()) {
        // Refused here as GL would refuse it: the shader does not compile,
        // and its log is GL's (GLSL ES that cannot be made says why).
        std::fprintf(stderr, "stategine: a shader could not be made GLSL ES: %s\n", why.c_str());
        const char* bad = "#version 300 es\n#error not made GLSL ES\n";
        return real.ShaderSource(shader, 1, &bad, nullptr);
    }
#if defined(__EMSCRIPTEN__)
    // (WebGL's own name for clip distances in a shader.)
    std::string named = es;
    for (std::size_t at; (at = named.find("GL_EXT_clip_cull_distance")) != std::string::npos;)
        named.replace(at, 25, "GL_ANGLE_clip_cull_distance");
    const char* text = named.c_str();
#else
    const char* text = es.c_str();
#endif
    // (SG_ESSL_DUMP=<folder>: each shader as the browser is given it, kept there, to look at.)
    if (const char* dump = std::getenv("SG_ESSL_DUMP")) {
        static int n = 0;
        char name[32];
        std::snprintf(name, sizeof name, "/%03d.%s", n++, type == GL_VERTEX_SHADER ? "vert" : "frag");
        if (FILE* f = std::fopen((std::string(dump) + name).c_str(), "w")) std::fputs(text, f), std::fclose(f);
    }
    real.ShaderSource(shader, 1, &text, nullptr);
}

// --- what is on the card, as WebGL checks it -----------------------------------
// Which picture is on which unit, for which target; what each picture holds
// (its format; a depth compared or read); which sampler object is on a unit.
constexpr int kUnits = 32;
constexpr GLenum kTex2D = 0x0DE1, kTex2DArray = 0x8C1A, kTex3D = 0x806F, kTexCube = 0x8513, kCompareMode = 0x884C;
struct Bound {
    GLuint t2d = 0, array = 0, t3d = 0, cube = 0;
    GLuint& on(GLenum target) { return target == kTex2D ? t2d : target == kTex2DArray ? array : target == kTex3D ? t3d : cube; }
};
Bound units[kUnits];
GLenum active = GL_TEXTURE0;
GLuint draw_fb = 0, read_fb = 0;
std::unordered_map<GLuint, std::array<GLuint, 6>> written;  // a framebuffer's pictures, by attachment (four colours, depth, stencil)
struct Picture {
    GLenum internal = 0;
    bool compares = false;
};
std::unordered_map<GLuint, Picture> pictures;
GLuint sampler_on[kUnits] = {};
std::unordered_map<GLuint, bool> sampler_compares;

GLuint bound_now(GLenum target) {
    const unsigned u = active - GL_TEXTURE0;
    return u < kUnits ? units[u].on(target) : 0;
}
// (A cube's faces are its target's.)
GLenum target_of(GLenum t) { return t >= 0x8515 && t <= 0x851A ? kTexCube : t; }

// --- what WebGL takes more strictly ----------------------------------------------

// What the desktop has that GLSL ES's GL is always, or never: nothing to do.
bool not_here(GLenum cap) {
    return cap == GL_TEXTURE_CUBE_MAP_SEAMLESS || cap == GL_MULTISAMPLE || cap == kProgramPointSize || cap == kFramebufferSrgb ||
           cap == kLineSmooth || cap == kPolygonSmooth;
}
void enable(GLenum cap) {
    if (!not_here(cap)) real.Enable(cap);
}
void disable(GLenum cap) {
    if (!not_here(cap)) real.Disable(cap);
}

// A picture's edge: no border in GLSL ES's GL, its edge instead.
GLint wrap(GLenum pname, GLint v) {
    const bool wraps = pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T || pname == GL_TEXTURE_WRAP_R;
    return wraps && static_cast<GLenum>(v) == GL_CLAMP_TO_BORDER ? static_cast<GLint>(GL_CLAMP_TO_EDGE) : v;
}
void tex_parameteri(GLenum target, GLenum pname, GLint v) {
    if (pname == kLodBias) return;
    if (pname == kCompareMode)
        if (const GLuint t = bound_now(target)) pictures[t].compares = v != 0;
    real.TexParameteri(target, pname, wrap(pname, v));
}
void tex_parameterf(GLenum target, GLenum pname, GLfloat v) {
    if (pname == kLodBias) return;
    real.TexParameterf(target, pname, v);
}
void tex_parameterfv(GLenum target, GLenum pname, const GLfloat* v) {
    if (pname == GL_TEXTURE_BORDER_COLOR || pname == kLodBias) return;
    real.TexParameterfv(target, pname, v);
}
void sampler_parameteri(GLuint s, GLenum pname, GLint v) {
    if (pname == kLodBias) return;
    if (pname == kCompareMode) sampler_compares[s] = v != 0;
    real.SamplerParameteri(s, pname, wrap(pname, v));
}
void bind_sampler(GLuint unit, GLuint s) {
    if (unit < kUnits) sampler_on[unit] = s;
    real.BindSampler(unit, s);
}

// A picture's format said as GLSL ES's GL wants it: sized, with the one
// format and type that go with it where no pixels come (a target drawn
// into), and drawable (no RGB of floats is, so those are RGBA).
struct Format {
    GLint internal;
    GLenum format, type;
};
Format sized(GLint internal, GLenum format, GLenum type, bool empty) {
    const auto g = static_cast<GLenum>(internal);
    if (g == GL_DEPTH_COMPONENT || g == GL_DEPTH_COMPONENT24) return {static_cast<GLint>(GL_DEPTH_COMPONENT24), GL_DEPTH_COMPONENT, GL_UNSIGNED_INT};
    if (g == kDepthComponent32 || g == GL_DEPTH_COMPONENT32F) return {static_cast<GLint>(GL_DEPTH_COMPONENT32F), GL_DEPTH_COMPONENT, GL_FLOAT};
    if (g == kDepthComponent16) return {internal, GL_DEPTH_COMPONENT, kUnsignedShort};
    // Unsized pictures of floats, as the desktop takes them.
    if (type == GL_FLOAT && (g == kRGBA_ || g == kRGB_ || g == GL_RG || g == kRED_))
        internal = static_cast<GLint>(g == kRGBA_ ? GL_RGBA32F : g == kRGB_ ? kRGB32F : g == GL_RG ? GL_RG32F : GL_R32F);
    if (!empty) return {internal, format, type};
    switch (static_cast<GLenum>(internal)) {
    case GL_RGB16F: return {static_cast<GLint>(GL_RGBA16F), GL_RGBA, GL_HALF_FLOAT};
    case kRGB32F: return {static_cast<GLint>(GL_RGBA32F), GL_RGBA, GL_FLOAT};
    case GL_RGBA16F: return {internal, GL_RGBA, GL_HALF_FLOAT};
    case GL_RGBA32F: return {internal, GL_RGBA, GL_FLOAT};
    case GL_RG16F: return {internal, GL_RG, GL_HALF_FLOAT};
    case GL_RG32F: return {internal, GL_RG, GL_FLOAT};
    case kR16F: return {internal, kRED_, GL_HALF_FLOAT};
    case GL_R32F: return {internal, kRED_, GL_FLOAT};
    case kR11G11B10F: return {internal, kRGB_, GL_HALF_FLOAT};
    case GL_RGBA8: case GL_SRGB8_ALPHA8: return {internal, GL_RGBA, GL_UNSIGNED_BYTE};
    case kRGB8: case kSRGB8: return {internal, kRGB_, GL_UNSIGNED_BYTE};
    case kR8: return {internal, kRED_, GL_UNSIGNED_BYTE};
    case kRG8: return {internal, GL_RG, GL_UNSIGNED_BYTE};
    case kR16: return {internal, kRED_, kUnsignedShort};
    case kRG16: return {internal, GL_RG, kUnsignedShort};
    case kRGBA16: return {internal, GL_RGBA, kUnsignedShort};
    case GL_R32UI: return {internal, kRedInteger, GL_UNSIGNED_INT};
    case GL_RG32UI: return {internal, GL_RG_INTEGER, GL_UNSIGNED_INT};
    case GL_RGBA8UI: return {internal, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE};
    default: return {internal, format, type};
    }
}
bool unpacking() {
    GLint b = 0;
    real.GetIntegerv(kUnpackBufferBinding, &b);
    return b != 0;
}
void tex_image_2d(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void* px) {
    const Format f = sized(internal, format, type, !px && !unpacking());
    if (level == 0)
        if (const GLuint t = bound_now(target_of(target))) pictures[t].internal = static_cast<GLenum>(f.internal);
    real.TexImage2D(target, level, f.internal, w, h, border, f.format, f.type, px);
}
void compressed_tex_image_2d(GLenum target, GLint level, GLenum internal, GLsizei w, GLsizei h, GLint border, GLsizei n, const void* px) {
    if (level == 0 || !pictures.count(bound_now(target_of(target))))
        if (const GLuint t = bound_now(target_of(target))) pictures[t].internal = internal;
    real.CompressedTexImage2D(target, level, internal, w, h, border, n, px);
}
void delete_textures(GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) pictures.erase(ids[i]);
    real.DeleteTextures(n, ids);
}
void tex_image_3d(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type,
                  const void* px) {
    const Format f = sized(internal, format, type, !px && !unpacking());
    if (level == 0)
        if (const GLuint t = bound_now(target)) pictures[t].internal = static_cast<GLenum>(f.internal);
    real.TexImage3D(target, level, f.internal, w, h, d, border, f.format, f.type, px);
}
GLenum renderable(GLenum internal) {
    const Format f = sized(static_cast<GLint>(internal), 0, 0, true);
    return static_cast<GLenum>(f.internal);
}
void renderbuffer_storage(GLenum target, GLenum internal, GLsizei w, GLsizei h) { real.RenderbufferStorage(target, renderable(internal), w, h); }
void renderbuffer_storage_multisample(GLenum target, GLsizei samples, GLenum internal, GLsizei w, GLsizei h) {
    real.RenderbufferStorageMultisample(target, samples, renderable(internal), w, h);
}

// --- a picture drawn into is never one a draw may read --------------------------
// Desktop GL draws into a picture while it is still bound to a unit the
// shader does not read; WebGL refuses the draw if any sampler of the
// program points at it, read or not. So just before a draw, a picture the
// framebuffer draws into is taken off every unit it is still on (as the
// renderer means it: it reads nothing it writes).

int slot_of(GLenum attachment) {
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + 4) return static_cast<int>(attachment - GL_COLOR_ATTACHMENT0);
    if (attachment == GL_DEPTH_ATTACHMENT) return 4;
    return 5;
}
void bind_texture(GLenum target, GLuint tex) {
    const unsigned u = active - GL_TEXTURE0;
    if (u < kUnits) units[u].on(target) = tex;
    real.BindTexture(target, tex);
}
void active_texture(GLenum unit) {
    active = unit;
    real.ActiveTexture(unit);
}
void bind_framebuffer(GLenum target, GLuint fb) {
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) draw_fb = fb;
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) read_fb = fb;
    real.BindFramebuffer(target, fb);
}
void attach(GLenum target, GLenum attachment, GLuint tex) {
    const GLuint fb = target == GL_READ_FRAMEBUFFER ? read_fb : draw_fb;
    if (fb) written[fb][static_cast<std::size_t>(slot_of(attachment))] = tex;
}
void framebuffer_texture_2d(GLenum target, GLenum attachment, GLenum textarget, GLuint tex, GLint level) {
    attach(target, attachment, tex);
    real.FramebufferTexture2D(target, attachment, textarget, tex, level);
}
void framebuffer_texture_layer(GLenum target, GLenum attachment, GLuint tex, GLint level, GLint layer) {
    attach(target, attachment, tex);
    real.FramebufferTextureLayer(target, attachment, tex, level, layer);
}
void nothing_read_that_is_written() {
    if (!draw_fb) return;
    const auto it = written.find(draw_fb);
    if (it == written.end()) return;
    for (GLuint tex : it->second) {
        if (!tex) continue;
        for (int u = 0; u < kUnits; ++u) {
            Bound& b = units[u];
            const auto off = [&](GLuint& slot, GLenum target) {
                if (slot != tex) return;
                real.ActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(u));
                real.BindTexture(target, 0);
                slot = 0;
            };
            off(b.t2d, kTex2D), off(b.array, kTex2DArray), off(b.t3d, kTex3D), off(b.cube, kTexCube);
        }
    }
    real.ActiveTexture(active);
}
void unsaid_samplers_aside();
double now_ms();
void if_long(const char* what, double from);
void draw_arrays(GLenum mode, GLint first, GLsizei n) {
    nothing_read_that_is_written();
    unsaid_samplers_aside();
    const double from = now_ms();
    real.DrawArrays(mode, first, n);
    if_long("a draw", from);
}
void draw_arrays_instanced(GLenum mode, GLint first, GLsizei n, GLsizei count) {
    nothing_read_that_is_written();
    unsaid_samplers_aside();
    const double from = now_ms();
    real.DrawArraysInstanced(mode, first, n, count);
    if_long("a draw", from);
}

// --- a sampler left unsaid -------------------------------------------------------
// A sampler the renderer never points anywhere reads unit 0, as GL leaves
// it - and WebGL refuses a draw where any sampler's unit holds a picture of
// another kind than it reads (floats, a depth compared, integers), or where
// samplers of two targets share a unit, read or not. So each program's
// samplers are known from when it is linked, and which of them the
// renderer has said a unit for; just before a draw, one it has not said
// whose unit would be refused is pointed at a unit with nothing on it.
constexpr GLenum kLinkStatus = 0x8B82, kActiveUniforms = 0x8B86;
enum class Holds : unsigned char { floats, compared, ints, uints };
struct Sampler {
    GLint at = -1;
    GLenum target = 0;
    Holds wants = Holds::floats;
    GLint unit = 0;
    bool said = false;
};
std::unordered_map<GLuint, std::vector<Sampler>> samplers;  // a linked program's
GLuint using_program = 0;

// A sampler's kind: its target and what it reads; false if not a sampler.
bool sampler_kind(GLenum kind, GLenum& target, Holds& wants) {
    switch (kind) {
    case 0x8B5E: target = kTex2D, wants = Holds::floats; return true;
    case 0x8B5F: target = kTex3D, wants = Holds::floats; return true;
    case 0x8B60: target = kTexCube, wants = Holds::floats; return true;
    case 0x8DC1: target = kTex2DArray, wants = Holds::floats; return true;
    case 0x8B62: target = kTex2D, wants = Holds::compared; return true;
    case 0x8DC4: target = kTex2DArray, wants = Holds::compared; return true;
    case 0x8DC5: target = kTexCube, wants = Holds::compared; return true;
    case 0x8DCA: target = kTex2D, wants = Holds::ints; return true;
    case 0x8DCB: target = kTex3D, wants = Holds::ints; return true;
    case 0x8DCC: target = kTexCube, wants = Holds::ints; return true;
    case 0x8DCF: target = kTex2DArray, wants = Holds::ints; return true;
    case 0x8DD2: target = kTex2D, wants = Holds::uints; return true;
    case 0x8DD3: target = kTex3D, wants = Holds::uints; return true;
    case 0x8DD4: target = kTexCube, wants = Holds::uints; return true;
    case 0x8DD7: target = kTex2DArray, wants = Holds::uints; return true;
    default: return false;
    }
}
// What the picture on `unit` holds, as a sampler there reads it.
Holds holds(GLuint tex, int unit) {
    const auto it = pictures.find(tex);
    if (it == pictures.end()) return Holds::floats;
    switch (it->second.internal) {
    case 0x8236: case 0x823C: case 0x8D7C: case 0x8D70: case 0x8232: case 0x8234: case 0x8238: case 0x823A: case 0x8D76: case 0x906F:
        return Holds::uints;
    case 0x8235: case 0x823B: case 0x8D82: case 0x8231: case 0x8233: case 0x8D8E: case 0x8D88: case 0x8237: case 0x8239:
        return Holds::ints;
    case 0x81A5: case 0x81A6: case 0x81A7: case 0x8CAC: case 0x88F0: case 0x8CAD: {
        const GLuint smp = sampler_on[unit];
        return (smp ? sampler_compares[smp] : it->second.compares) ? Holds::compared : Holds::floats;
    }
    default: return Holds::floats;
    }
}
#if defined(__EMSCRIPTEN__)
double now_ms() { return emscripten_get_now(); }
#else
double now_ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
#endif
// (Said when the browser's card takes long over one: a shader made, a draw - in a
// browser on Direct3D a shader is made at its first draw.)
void if_long(const char* what, double from) {
    const double ms = now_ms() - from;
    if (ms > 250.0) std::fprintf(stderr, "webgl: %s took %.0f ms\n", what, ms);
}
#if defined(__EMSCRIPTEN__)
EM_JS(int, sg_parallel_compile, (), { return GLctx.getExtension('KHR_parallel_shader_compile') ? 1 : 0; });
#endif
void link_program(GLuint program) {
    const double from = now_ms();
    real.LinkProgram(program);
#if defined(__EMSCRIPTEN__)
    // Made by the browser's card on threads of its own where it can
    // (KHR_parallel_shader_compile): waited for here, the page given its
    // turn meanwhile - on Direct3D a big shader takes long, and a card held
    // that long is reset, the page's drawing lost with it.
    static const bool parallel = sg_parallel_compile() != 0;
    if (parallel) {
        constexpr GLenum kCompletionStatus = 0x91B1;
        for (GLint done = 0;;) {
            real.GetProgramiv(program, kCompletionStatus, &done);
            if (done) break;
            emscripten_sleep(10);
        }
    }
#endif
    if_long("a program's link", from);
    std::vector<Sampler>& list = samplers[program];
    list.clear();
    GLint ok = 0, n = 0;
    real.GetProgramiv(program, kLinkStatus, &ok);
    if (!ok) return;
    real.GetProgramiv(program, kActiveUniforms, &n);
    for (GLint i = 0; i < n; ++i) {
        GLchar name[256];
        GLsizei len = 0;
        GLint size = 0;
        GLenum kind = 0;
        real.GetActiveUniform(program, static_cast<GLuint>(i), sizeof name, &len, &size, &kind, name);
        Sampler sm;
        if (!sampler_kind(kind, sm.target, sm.wants)) continue;
        sm.at = real.GetUniformLocation(program, name);
        if (sm.at >= 0) list.push_back(sm);
    }
}
void use_program(GLuint program) {
    using_program = program;
    real.UseProgram(program);
}
void uniform_1i(GLint at, GLint v) {
    real.Uniform1i(at, v);
    const auto it = samplers.find(using_program);
    if (it == samplers.end()) return;
    for (Sampler& sm : it->second)
        if (sm.at == at) sm.unit = v, sm.said = true;
}
bool empty_unit(int u) {
    const Bound& b = units[u];
    return !b.t2d && !b.array && !b.t3d && !b.cube;
}
void unsaid_samplers_aside() {
    const auto it = samplers.find(using_program);
    if (it == samplers.end()) return;
    std::vector<Sampler>& list = it->second;
    // Each unit's first claim: its target and what it reads (said ones first).
    GLenum claim_target[kUnits] = {};
    Holds claim_wants[kUnits] = {};
    bool said_on[kUnits] = {};
    for (const Sampler& sm : list)
        if (sm.said && sm.unit >= 0 && sm.unit < kUnits && !claim_target[sm.unit])
            claim_target[sm.unit] = sm.target, claim_wants[sm.unit] = sm.wants, said_on[sm.unit] = true;
    const auto fits = [&](const Sampler& sm, int u) {
        if (u < 0 || u >= kUnits) return false;
        if (claim_target[u] && (claim_target[u] != sm.target || claim_wants[u] != sm.wants)) return false;
        const GLuint tex = units[u].on(sm.target);
        return !tex || holds(tex, u) == sm.wants;
    };
    for (Sampler& sm : list) {
        if (sm.said) continue;
        if (!fits(sm, sm.unit)) {
            // (A unit with nothing on it, claimed by none - or by one like it.)
            int to = -1;
            for (int u = kUnits - 1; u >= 0 && to < 0; --u)
                if (!said_on[u] && empty_unit(u) && fits(sm, u)) to = u;
            if (to < 0) continue;
            real.Uniform1i(sm.at, to);
            sm.unit = to;
        }
        if (!claim_target[sm.unit]) claim_target[sm.unit] = sm.target, claim_wants[sm.unit] = sm.wants;
    }
}

// --- what WebGL has not ----------------------------------------------------------

void depth_range(double n, double f) { real.DepthRangef(static_cast<GLfloat>(n), static_cast<GLfloat>(f)); }
void clear_depth(double d) { real.ClearDepthf(static_cast<GLfloat>(d)); }
void draw_buffer(GLenum b) { real.DrawBuffers(1, &b); }
void tex_buffer(GLenum, GLenum, GLuint) {}  // (a skin's joints are a picture here: Mesh::pose)
void begin_conditional_render(GLuint, GLenum) {}  // (drawn whatever the query found)
void end_conditional_render() {}
void* map_buffer_range(GLenum, GLintptr, GLsizeiptr, GLbitfield) { return nullptr; }  // (the caller writes it plainly)
GLboolean unmap_buffer(GLenum) { return 1; }

#if defined(__EMSCRIPTEN__)
EM_JS(void, sg_get_buffer_sub_data, (GLenum target, GLintptr offset, GLsizeiptr size, void* data), {
    GLctx.getBufferSubData(target, offset, HEAPU8, data, size);
});
void get_buffer_sub_data(GLenum target, GLintptr offset, GLsizeiptr size, void* data) { sg_get_buffer_sub_data(target, offset, size, data); }
#endif

// A level of the picture bound to 2D read back, by drawing it into a
// framebuffer of its own and reading that (RGBA, as a browser reads); into
// the pixel-pack buffer bound, or `out` with only the channels asked for.
void get_tex_image(GLenum target, GLint level, GLenum format, GLenum type, void* out) {
    if (target != GL_TEXTURE_2D || type != GL_FLOAT) return;
    GLint tex = 0, was_read = 0, was_draw = 0, pack = 0;
    real.GetIntegerv(kTextureBinding2D, &tex);
    real.GetIntegerv(kReadFramebufferBinding, &was_read);
    real.GetIntegerv(kFramebufferBinding, &was_draw);
    real.GetIntegerv(kPackBufferBinding, &pack);
    static GLuint fb = 0;
    if (!fb) real.GenFramebuffers(1, &fb);
    real.BindFramebuffer(GL_READ_FRAMEBUFFER, fb);
    real.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, static_cast<GLuint>(tex), level);
    // (Its size at that level: as the renderer asks, the last - one texel.
    // Larger reads are not asked of it.)
    const GLsizei w = 1, h = 1;
    if (pack) {
        real.ReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out);
    } else {
        float rgba[4] = {0, 0, 0, 0};
        real.ReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, rgba);
        const int n = format == GL_RG ? 2 : format == kRED_ ? 1 : format == kRGB_ ? 3 : 4;
        std::memcpy(out, rgba, sizeof(float) * static_cast<std::size_t>(n));
    }
    real.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    real.BindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(was_read));
    real.BindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(was_draw));
}

template <class F>
void* as_proc(F f) {
    return reinterpret_cast<void*>(f);
}

}  // namespace

#if defined(__EMSCRIPTEN__)
namespace {
EM_ASYNC_JS(void, sg_next_frame, (), { await new Promise(resolve => requestAnimationFrame(resolve)); });
double last_breath = 0.0;
}  // namespace

EM_JS(void, sg_shown, (), { if (Module.onShown) { Module.onShown(); Module.onShown = null; } });
void web_frame() {
    // (The page told once the program draws: after the window's own two black frames.)
    static int frames = 0;
    if (++frames == 3) sg_shown();
    sg_next_frame();
    last_breath = emscripten_get_now();
}

void web_breathe() {
    const double now = emscripten_get_now();
    if (now - last_breath < 100.0) return;
    emscripten_sleep(0);
    last_breath = emscripten_get_now();
}
#endif

void webgl_start() {
#if defined(__EMSCRIPTEN__)
    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_get_current_context();
    for (const char* ext : {"EXT_color_buffer_float", "EXT_color_buffer_half_float", "OES_texture_float_linear", "EXT_float_blend",
                            "EXT_texture_compression_bptc", "WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_s3tc_srgb",
                            "WEBGL_clip_cull_distance", "EXT_depth_clamp", "EXT_texture_filter_anisotropic", "EXT_texture_norm16",
                            "EXT_disjoint_timer_query_webgl2", "KHR_parallel_shader_compile"})
        if (!emscripten_webgl_enable_extension(ctx, ext)) std::fprintf(stderr, "stategine: this browser's WebGL has no %s\n", ext);
#endif
}

void* webgl_proc(const char* name, ProcLoader loader) {
    const std::string n = name;
    // What a browser cannot do at all, none (on the desktop too, trying it).
    if (n == "glClipControl" || n == "glDispatchCompute" || n == "glMemoryBarrier" || n == "glMultiDrawArraysIndirect") return nullptr;
    void* const got = loader(name);
    // Answered here always (beneath them, the browser's own).
    const auto over = [&](auto& slot, auto mine) -> void* {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(got);
        return got ? as_proc(mine) : nullptr;
    };
    if (n == "glShaderSource") return over(real.ShaderSource, &shader_source);
    if (n == "glEnable") return over(real.Enable, &enable);
    if (n == "glDisable") return over(real.Disable, &disable);
    if (n == "glTexParameteri") return over(real.TexParameteri, &tex_parameteri);
    if (n == "glTexParameterf") return over(real.TexParameterf, &tex_parameterf);
    if (n == "glTexParameterfv") return over(real.TexParameterfv, &tex_parameterfv);
    if (n == "glSamplerParameteri") return over(real.SamplerParameteri, &sampler_parameteri);
    if (n == "glTexImage2D") return over(real.TexImage2D, &tex_image_2d);
    if (n == "glTexImage3D") return over(real.TexImage3D, &tex_image_3d);
    if (n == "glRenderbufferStorage") return over(real.RenderbufferStorage, &renderbuffer_storage);
    if (n == "glRenderbufferStorageMultisample") return over(real.RenderbufferStorageMultisample, &renderbuffer_storage_multisample);
    if (n == "glBindTexture") return over(real.BindTexture, &bind_texture);
    if (n == "glActiveTexture") return over(real.ActiveTexture, &active_texture);
    if (n == "glBindFramebuffer") return over(real.BindFramebuffer, &bind_framebuffer);
    if (n == "glFramebufferTexture2D") return over(real.FramebufferTexture2D, &framebuffer_texture_2d);
    if (n == "glFramebufferTextureLayer") return over(real.FramebufferTextureLayer, &framebuffer_texture_layer);
    if (n == "glDrawArrays") return over(real.DrawArrays, &draw_arrays);
    if (n == "glDrawArraysInstanced") return over(real.DrawArraysInstanced, &draw_arrays_instanced);
    if (n == "glLinkProgram") {
        real.GetActiveUniform = reinterpret_cast<decltype(real.GetActiveUniform)>(loader("glGetActiveUniform"));
        return over(real.LinkProgram, &link_program);
    }
    if (n == "glUseProgram") return over(real.UseProgram, &use_program);
    if (n == "glBindSampler") return over(real.BindSampler, &bind_sampler);
    if (n == "glCompressedTexImage2D") return over(real.CompressedTexImage2D, &compressed_tex_image_2d);
    if (n == "glDeleteTextures") return over(real.DeleteTextures, &delete_textures);
    if (n == "glUniform1i") return over(real.Uniform1i, &uniform_1i);
    if (n == "glMapBufferRange") return as_proc(&map_buffer_range);
    if (n == "glUnmapBuffer") return as_proc(&unmap_buffer);
    // What the answers here call.
    if (n == "glGetShaderiv") real.GetShaderiv = reinterpret_cast<decltype(real.GetShaderiv)>(got);
    if (n == "glGetProgramiv") real.GetProgramiv = reinterpret_cast<decltype(real.GetProgramiv)>(got);
    if (n == "glGetUniformLocation") real.GetUniformLocation = reinterpret_cast<decltype(real.GetUniformLocation)>(got);
    if (n == "glDrawBuffers") real.DrawBuffers = reinterpret_cast<decltype(real.DrawBuffers)>(got);
    if (n == "glReadPixels") real.ReadPixels = reinterpret_cast<decltype(real.ReadPixels)>(got);
    if (n == "glGetIntegerv") real.GetIntegerv = reinterpret_cast<decltype(real.GetIntegerv)>(got);
    if (n == "glBindBuffer") real.BindBuffer = reinterpret_cast<decltype(real.BindBuffer)>(got);
    if (n == "glBufferSubData") real.BufferSubData = reinterpret_cast<decltype(real.BufferSubData)>(got);
    if (n == "glGenFramebuffers") real.GenFramebuffers = reinterpret_cast<decltype(real.GenFramebuffers)>(got);
    if (got) return got;
    // What the browser has not: the nearest it has.
    if (n == "glDepthRange") return (real.DepthRangef = reinterpret_cast<decltype(real.DepthRangef)>(loader("glDepthRangef"))) ? as_proc(&depth_range) : nullptr;
    if (n == "glClearDepth") return (real.ClearDepthf = reinterpret_cast<decltype(real.ClearDepthf)>(loader("glClearDepthf"))) ? as_proc(&clear_depth) : nullptr;
    if (n == "glDrawBuffer") return as_proc(&draw_buffer);
    if (n == "glTexBuffer") return as_proc(&tex_buffer);
    if (n == "glBeginConditionalRender") return as_proc(&begin_conditional_render);
    if (n == "glEndConditionalRender") return as_proc(&end_conditional_render);
    if (n == "glGetTexImage") return as_proc(&get_tex_image);
#if defined(__EMSCRIPTEN__)
    if (n == "glGetBufferSubData") return as_proc(&get_buffer_sub_data);
#endif
    return nullptr;
}

}  // namespace sg::gl
