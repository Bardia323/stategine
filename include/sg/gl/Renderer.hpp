// Stategine - GL resources: programs, meshes, textures, render targets.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/gl/GL.hpp"
#include "sg/gl/Math.hpp"

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

    void set(const char* n, const Mat4& m) const { glUniformMatrix4fv(uniform(n), 1, 0, m.m); }
    void set(const char* n, const Vec3& v) const { glUniform3f(uniform(n), v.x, v.y, v.z); }
    void set(const char* n, float f) const { glUniform1f(uniform(n), f); }
    void set(const char* n, float a, float b) const { glUniform2f(uniform(n), a, b); }
    void set(const char* n, float a, float b, float c, float d) const {
        glUniform4f(uniform(n), a, b, c, d);
    }
    void set(const char* n, int i) const { glUniform1i(uniform(n), i); }

    GLuint id() const { return id_; }

private:
    GLuint id_ = 0;
    mutable std::unordered_map<std::string, GLint> locations_;
    mutable std::unordered_map<const char*, std::pair<const char*, GLint>> by_address_;
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

std::vector<float> cube_vertices();

// A cylinder standing on y, radius 0.5 and height 1, centred like the cube, so
// the same model matrix sizes it: sx and sz are its diameters, sy its height.
// A box with its edges and corners rounded to `r` metres, `sx` x `sy` x `sz`
// in size, and narrowing to `taper` of its width and depth at the top (1:
// straight up). Made in the unit box every mesh shares, so the same model
// matrix places it; each normal is given pre-divided by the size, so that
// once the model's scale has been applied to it, it points the way the
// rounded surface does.
std::vector<float> rounded_box_vertices(float sx, float sy, float sz, float r, float taper = 1.0f,
                                               int seg = 3);

std::vector<float> cylinder_vertices(int segments = 28, float taper = 1.0f);

// A sphere of radius 0.5, centred like the cube.
std::vector<float> sphere_vertices(int stacks = 14, int slices = 24);

// A unit quad standing in the y-z plane, facing +x: scale it by {1, height,
// width} and rotate it by the portal's yaw and it faces the way the portal
// does, with no extra quarter turn anywhere.
std::vector<float> quad_vertices();

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
