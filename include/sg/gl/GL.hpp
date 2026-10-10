// Stategine - a hand-rolled loader for the GL 3.3 core subset the renderer
// uses. Small enough that pulling in glad or glew would cost more than it saves.
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace sg::gl {

using GLenum = unsigned int;
using GLbitfield = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLfloat = float;
using GLclampf = float;
using GLboolean = unsigned char;
using GLchar = char;
using GLintptr = std::intptr_t;
using GLsizeiptr = std::ptrdiff_t;

// --- enums ------------------------------------------------------------------
constexpr GLenum GL_NONE = 0;
constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_DEPTH_BUFFER_BIT = 0x00000100;
constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
constexpr GLenum GL_TRIANGLES = 0x0004;
constexpr GLenum GL_LEQUAL = 0x0203;
constexpr GLenum GL_LESS = 0x0201;
constexpr GLenum GL_GREATER = 0x0204;
constexpr GLenum GL_LOWER_LEFT = 0x8CA1;
constexpr GLenum GL_NEGATIVE_ONE_TO_ONE = 0x935E;
constexpr GLenum GL_ZERO_TO_ONE = 0x935F;
constexpr GLenum GL_ALWAYS = 0x0207;
constexpr GLenum GL_DEPTH_CLAMP = 0x864F;
constexpr GLenum GL_TEXTURE_CUBE_MAP = 0x8513;
constexpr GLenum GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515;
constexpr GLenum GL_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F;
constexpr GLenum GL_TEXTURE_WRAP_R = 0x8072;
constexpr GLenum GL_FRONT = 0x0404;
constexpr GLenum GL_BACK = 0x0405;
constexpr GLenum GL_CCW = 0x0901;
constexpr GLenum GL_CULL_FACE = 0x0B44;
constexpr GLenum GL_DEPTH_TEST = 0x0B71;
constexpr GLenum GL_BLEND = 0x0BE2;
constexpr GLenum GL_SRC_ALPHA = 0x0302;
constexpr GLenum GL_ONE = 1;
constexpr GLenum GL_ZERO = 0;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_CLIP_DISTANCE0 = 0x3000;
constexpr GLenum GL_CONSTANT_ALPHA = 0x8003;
constexpr GLenum GL_ONE_MINUS_CONSTANT_ALPHA = 0x8004;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
constexpr GLenum GL_TEXTURE_2D_ARRAY = 0x8C1A;
constexpr GLenum GL_TEXTURE_3D = 0x806F;
constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
constexpr GLenum GL_FLOAT = 0x1406;
constexpr GLenum GL_HALF_FLOAT = 0x140B;
constexpr GLenum GL_DEPTH_COMPONENT = 0x1902;
constexpr GLenum GL_RGB = 0x1907;
constexpr GLenum GL_RGBA = 0x1908;
constexpr GLenum GL_TEXTURE_BORDER_COLOR = 0x1004;
constexpr GLenum GL_NEAREST = 0x2600;
constexpr GLenum GL_NEAREST_MIPMAP_NEAREST = 0x2700;
constexpr GLenum GL_REPEAT = 0x2901;
constexpr GLenum GL_LINEAR = 0x2601;
constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
constexpr GLenum GL_POLYGON_OFFSET_FILL = 0x8037;
constexpr GLenum GL_RGBA8 = 0x8058;
constexpr GLenum GL_SRGB8_ALPHA8 = 0x8C43;
constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
constexpr GLenum GL_CLAMP_TO_BORDER = 0x812D;
constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
constexpr GLenum GL_MULTISAMPLE = 0x809D;
constexpr GLenum GL_RGBA16F = 0x881A;
constexpr GLenum GL_RGBA32F = 0x8814;
constexpr GLenum GL_RGB16F = 0x881B;
constexpr GLenum GL_RG = 0x8227;
constexpr GLenum GL_RG16F = 0x822F;
constexpr GLenum GL_PIXEL_PACK_BUFFER = 0x88EB;
constexpr GLenum GL_STREAM_READ = 0x88E1;
constexpr GLenum GL_TEXTURE_MAX_ANISOTROPY = 0x84FE;
// BC7 blocks (BPTC: core from 4.2, ARB_texture_compression_bptc before).
constexpr GLenum GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8E8C;
constexpr GLenum GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM = 0x8E8D;
constexpr GLenum GL_EXTENSIONS = 0x1F03;
constexpr GLenum GL_NUM_EXTENSIONS = 0x821D;
constexpr GLenum GL_TEXTURE_COMPARE_MODE = 0x884C;
constexpr GLenum GL_TEXTURE_COMPARE_FUNC = 0x884D;
constexpr GLenum GL_COMPARE_REF_TO_TEXTURE = 0x884E;
constexpr GLenum GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
constexpr GLenum GL_STATIC_DRAW = 0x88E4;
constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_DEPTH_COMPONENT32F = 0x8CAC;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
constexpr GLenum GL_RENDERBUFFER = 0x8D41;
constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
constexpr GLenum GL_MAX_SAMPLES = 0x8D57;
constexpr GLenum GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;

// --- entry points -------------------------------------------------------------
#define SG_GL_FUNCS(X)                                                                        \
    X(void, Enable, (GLenum))                                                                 \
    X(void, Disable, (GLenum))                                                                \
    X(void, Clear, (GLbitfield))                                                              \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                 \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                       \
    X(void, DepthFunc, (GLenum))                                                              \
    X(void, DepthRange, (double, double))                                                     \
    X(void, ClearDepth, (double))                                                             \
    X(void, BlendFunc, (GLenum, GLenum))                                                      \
    X(void, BlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                 \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                              \
    X(void, CullFace, (GLenum))                                                               \
    X(void, FrontFace, (GLenum))                                                              \
    X(void, PolygonOffset, (GLfloat, GLfloat))                                                \
    X(GLenum, GetError, ())                                                                   \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                             \
    X(void, DrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))                           \
    X(void, VertexAttribDivisor, (GLuint, GLuint))                                            \
    X(void, DisableVertexAttribArray, (GLuint))                                               \
    X(GLuint, CreateShader, (GLenum))                                                         \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))              \
    X(void, CompileShader, (GLuint))                                                          \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                            \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                           \
    X(void, DeleteShader, (GLuint))                                                           \
    X(GLuint, CreateProgram, ())                                                              \
    X(void, AttachShader, (GLuint, GLuint))                                                   \
    X(void, LinkProgram, (GLuint))                                                            \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                           \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                          \
    X(void, UseProgram, (GLuint))                                                             \
    X(void, DeleteProgram, (GLuint))                                                          \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                     \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                    \
    X(void, Uniform1i, (GLint, GLint))                                                        \
    X(void, Uniform1f, (GLint, GLfloat))                                                      \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                             \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                    \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                           \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                              \
    X(void, BindVertexArray, (GLuint))                                                        \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                     \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                   \
    X(void, BindBuffer, (GLenum, GLuint))                                                     \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                            \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                          \
    X(void, GetBufferSubData, (GLenum, GLintptr, GLsizeiptr, void*))                          \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))    \
    X(void, EnableVertexAttribArray, (GLuint))                                                \
    X(void, GenTextures, (GLsizei, GLuint*))                                                  \
    X(void, BindTexture, (GLenum, GLuint))                                                    \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                         \
    X(void, ActiveTexture, (GLenum))                                                          \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                           \
    X(void, TexParameterfv, (GLenum, GLenum, const GLfloat*))                                 \
    X(void, TexParameterf, (GLenum, GLenum, GLfloat))                                         \
    X(void, GenerateMipmap, (GLenum))                                                         \
    X(void, TexImage2D,                                                                       \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))           \
    X(void, TexImage3D,                                                                       \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))  \
    X(void, TexSubImage2D,                                                                    \
      (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*))           \
    X(void, CompressedTexImage2D,                                                             \
      (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*))                 \
    X(const unsigned char*, GetStringi, (GLenum, GLuint))                                     \
    X(void, GetTexImage, (GLenum, GLint, GLenum, GLenum, void*))                              \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                              \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                     \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                    \
    X(void, FramebufferTextureLayer, (GLenum, GLenum, GLuint, GLint, GLint))                  \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                        \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                               \
    X(void, GenRenderbuffers, (GLsizei, GLuint*))                                             \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                               \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint*))                                    \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                          \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))      \
    X(void, BlitFramebuffer,                                                                  \
      (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))           \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))              \
    X(void, DrawBuffer, (GLenum))                                                             \
    X(void, DrawBuffers, (GLsizei, const GLenum*))                                            \
    X(void, ReadBuffer, (GLenum))                                                             \
    X(void, GetIntegerv, (GLenum, GLint*))                                                    \
    X(void, DepthMask, (GLboolean))                                                           \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                        \
    X(void, Finish, ())

#define SG_GL_DECLARE(ret, name, args) inline ret(*gl##name) args = nullptr;
SG_GL_FUNCS(SG_GL_DECLARE)
#undef SG_GL_DECLARE
// Where the driver has it (GL 4.5, or ARB_clip_control): how clip space's
// depth is taken - for depth reversed (Renderer.hpp, reversed_depth).
inline void (*glClipControl)(GLenum, GLenum) = nullptr;

using ProcLoader = void* (*)(const char*);

// Pass glfwGetProcAddress. Throws on the first entry point the driver lacks.
// inline: GL.hpp is the loader's own header, macros and all.
inline void load(ProcLoader loader) {
#define SG_GL_LOAD(ret, name, args)                                              \
    gl##name = reinterpret_cast<ret(*) args>(loader("gl" #name));                \
    if (!gl##name) throw std::runtime_error("GL entry point missing: gl" #name);
    SG_GL_FUNCS(SG_GL_LOAD)
#undef SG_GL_LOAD
    glClipControl = reinterpret_cast<void (*)(GLenum, GLenum)>(loader("glClipControl"));
}

// What keeps a window answering while the engine works long with it waiting
// (a prepare, a warm, a bake): its events taken, so the system never calls it
// "not responding" and paints it white. The window sets it (Window does, as
// it is made); the renderer calls `answer()` now and then, on the thread the
// context is current on. Unset, nothing is done.
inline void (*answer_hook)() = nullptr;
inline void answer() {  // inline: one line of the loader's own header
    if (answer_hook) answer_hook();
}

// Cheap guard used after each pipeline stage while bringing a scene up.
// inline: GL.hpp is the loader's own header, macros and all.
inline void check(const char* where) {
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        throw std::runtime_error(std::string("GL error 0x") + std::to_string(err) + " at " +
                                 where);
}

}  // namespace sg::gl
