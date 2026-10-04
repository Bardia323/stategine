// Stategine - GL resources: programs, meshes, textures, render targets.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/gl/GL.hpp"
#include "sg/gl/Math.hpp"
#include "sg/render/Geometry.hpp"

namespace sg::gl {

GLuint compile(GLenum type, const char* src, const char* tag);

class Program {
public:
    Program() = default;
    Program(const char* vs, const char* fs, const char* tag = "program");

    void use() const { glUseProgram(id_); }

    // Locations are asked of the driver once per name and remembered: a frame
    // sets a few hundred uniforms, and the driver lookup is the slow part.
    // Names are nearly always string literals: found first by where their
    // characters are (checked against the name, so a reused buffer cannot be
    // taken for another), and only then by the characters themselves.
    GLint uniform(const char* name) const;

    // Whether the linked program has this uniform. One that is declared but
    // unused has been optimised away, and does not count.
    bool has(const char* name) const { return uniform(name) >= 0; }

    void set(const char* n, const Mat4& m) const { put(uniform(n), 16, m.m); }
    void set(const char* n, const Vec3& v) const { const float f[3]{v.x, v.y, v.z}; put(uniform(n), 3, f); }
    void set(const char* n, float f) const { put(uniform(n), 1, &f); }
    void set(const char* n, float a, float b) const { const float f[2]{a, b}; put(uniform(n), 2, f); }
    void set(const char* n, float a, float b, float c, float d) const { const float f[4]{a, b, c, d}; put(uniform(n), 4, f); }
    void set(const char* n, int i) const;
    // A value at a location (1, 2, 3, 4 floats, or a 4x4 matrix): sent to
    // the driver only if the program does not hold it already - the many
    // views of one world set the same lights and look again and again.
    void put(GLint at, int n, const float* v) const;

    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
    mutable std::unordered_map<std::string, GLint> locations_;
    mutable std::unordered_map<const char*, std::pair<const char*, GLint>> by_address_;
    // What each location holds (as floats; an int as its bits), by location.
    mutable std::vector<std::array<float, 16>> held_;
    mutable std::vector<signed char> held_n_;
};

// An RGBA texture the CPU refills - how a 2D state's raster becomes a surface
// inside the 3D world. Uploads only when the content actually changed.
class Texture {
public:
    // With mipmaps, a detailed texture seen small or at a grazing angle - print
    // on a sheet across the room - averages out instead of shimmering.
    // `pixel`: pixel art - each texel a hard square, nearest, repeating.
    void create(int w, int h, bool mipmaps = false, bool srgb = false, bool pixel = false);

    void upload(const std::vector<unsigned char>& rgba);

    void bind(int unit = 0) const;

    int width() const { return w_; }
    int height() const { return h_; }
    bool valid() const { return id_ != 0; }
    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
    int w_ = 0;
    int h_ = 0;
    bool mipmaps_ = false;
};

// Interleaved position(3), normal(3), uv(2).
class Mesh {
public:
    void create(const std::vector<float>& verts);

    // New vertices for a mesh that changes - ground rebuilt around a walker.
    void update(const std::vector<float>& verts);

    void draw() const {
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, count_);
    }

    // Drawn `instances` times in one call, each as `buffer` says: per
    // instance kInstanceFloats floats - a model matrix (column by column, at
    // attributes 3-6) and two vec4s of material (7, 8).
    static constexpr int kInstanceFloats = 24;
    void draw_instanced(GLuint buffer, GLsizei instances) const;

    bool valid() const { return vao_ != 0; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei count_ = 0;
};

using render::cube_vertices;
using render::rounded_box_vertices;
using render::cylinder_vertices;
using render::sphere_vertices;
using render::quad_vertices;

// A single oversized triangle covering the screen: no VBO, no attributes.
class FullscreenTriangle {
public:
    void create() {
        glGenVertexArrays(1, &vao_);
    }
    void draw() const {
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

private:
    GLuint vao_ = 0;
};

// --- render targets -----------------------------------------------------------
// HDR colour with an optional multisampled twin, so the scene can be drawn with
// MSAA and then resolved into a texture the post chain can sample.
// A cube of six square pictures - what is seen every way from one point -
// sampled by a direction: what a polished thing there reflects.
class CubeMap {
public:
    void create(int size);
    bool valid() const { return tex_ != 0; }
    int size() const { return size_; }
    // Face `face` (+x, -x, +y, -y, +z, -z) from what `from` drew.
    void take(int face, GLuint from, int w, int h);
    void bind(int unit) const;

private:
    GLuint tex_ = 0, fbo_ = 0;
    int size_ = 0;
};

class RenderTarget {
public:
    // `depth_texture`: a single-sampled target keeps its depth as a texture a
    // later pass can read (bind_depth), not a renderbuffer only it can use.
    void create(int w, int h, GLenum internal_format = GL_RGBA16F, int samples = 0,
                bool with_depth = true, bool depth_texture = false);

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, w_, h_);
    }

    // Resolve a multisampled target into a plain one.
    void blit_to(const RenderTarget& dst) const;
    // Its framebuffer, for what copies out of it (CubeMap::take).
    GLuint framebuffer() const { return fbo_; }

    // Resolve depth too: a multisampled target's depth into a plain one's.
    void blit_depth_to(const RenderTarget& dst) const;

    void bind_depth(int unit) const;

    void bind_color(int unit) const;

    // Its picture made again at every smaller size, and sampled between
    // them from then on: for a picture that is shown smaller than it was
    // drawn - a feed on a screen across the room - so its fine detail does
    // not shimmer as the screen is neared or left.
    void mipmap();

    void destroy();

    int width() const { return w_; }
    int height() const { return h_; }
    bool valid() const { return fbo_ != 0; }

private:
    GLuint fbo_ = 0, color_tex_ = 0, color_rb_ = 0, depth_rb_ = 0, depth_tex_ = 0;
    int w_ = 0, h_ = 0, samples_ = 0;
    bool mipmapped_ = false;
};

// Depth-only target sampled with hardware comparison, for shadows.
class ShadowMap {
public:
    void create(int size);

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, size_, size_);
    }

    void bind_depth(int unit) const;

    int size() const { return size_; }
    bool valid() const { return fbo_ != 0; }

private:
    GLuint fbo_ = 0;
    GLuint depth_ = 0;
    int size_ = 0;
};

// Where each of a batch of instances is, and what it is made of: one buffer,
// filled anew for each batch drawn.
class InstanceBuffer {
public:
    InstanceBuffer() = default;
    InstanceBuffer(const InstanceBuffer&) = delete;
    InstanceBuffer& operator=(const InstanceBuffer&) = delete;
    ~InstanceBuffer() {
        if (vbo_) glDeleteBuffers(1, &vbo_);
    }
    GLuint upload(const std::vector<float>& data);

private:
    GLuint vbo_ = 0;
};

// Depth maps for many lights in one texture, a layer each, so a shader reads
// any of them through one sampler, by the light's number. It grows when more
// layers are wanted and never shrinks; grown, every layer starts empty.
class ShadowArray {
public:
    ShadowArray() = default;
    ShadowArray(const ShadowArray&) = delete;
    ShadowArray& operator=(const ShadowArray&) = delete;
    ~ShadowArray() { release(); }

    // At least `layers` maps of `size` square. True if it was made anew.
    bool ensure(int size, int layers);

    // Draw into one layer.
    void bind_layer(int layer) const;

    void bind_depth(int unit) const;

    int size() const { return size_; }
    int layers() const { return layers_; }

private:
    void release();
    GLuint fbo_ = 0;
    GLuint depth_ = 0;
    int size_ = 0, layers_ = 0;
};

}  // namespace sg::gl
