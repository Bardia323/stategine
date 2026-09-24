// Stategine - GL resources: programs, meshes, textures, render targets.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/gl/GL.hpp"
#include "sg/gl/Math.hpp"

namespace sg::gl {

inline GLuint compile(GLenum type, const char* src, const char* tag) {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(id, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("shader ") + tag + ": " + log);
    }
    return id;
}

class Program {
public:
    Program() = default;
    Program(const char* vs, const char* fs, const char* tag = "program") {
        const GLuint v = compile(GL_VERTEX_SHADER, vs, tag);
        const GLuint f = compile(GL_FRAGMENT_SHADER, fs, tag);
        id_ = glCreateProgram();
        glAttachShader(id_, v);
        glAttachShader(id_, f);
        glLinkProgram(id_);
        GLint ok = 0;
        glGetProgramiv(id_, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048] = {};
            glGetProgramInfoLog(id_, sizeof(log), nullptr, log);
            throw std::runtime_error(std::string("link ") + tag + ": " + log);
        }
        glDeleteShader(v);
        glDeleteShader(f);
    }

    void use() const { glUseProgram(id_); }

    // Locations are asked of the driver once per name and remembered: a frame
    // sets a few hundred uniforms, and the driver lookup is the slow part.
    GLint uniform(const char* name) const {
        auto it = locations_.find(name);
        if (it != locations_.end()) return it->second;
        const GLint loc = glGetUniformLocation(id_, name);
        locations_.emplace(name, loc);
        return loc;
    }

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
};

// An RGBA texture the CPU refills - how a 2D state's raster becomes a surface
// inside the 3D world. Uploads only when the content actually changed.
class Texture {
public:
    // With mipmaps, a detailed texture seen small or at a grazing angle - print
    // on a sheet across the room - averages out instead of shimmering.
    void create(int w, int h, bool mipmaps = false, bool srgb = false) {
        if (id_) glDeleteTextures(1, &id_);  // made again, at a new size
        w_ = w;
        h_ = h;
        mipmaps_ = mipmaps;
        glGenTextures(1, &id_);
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8), w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (mipmaps) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
            glGetError();  // anisotropy is core only from 4.6; without it, plain trilinear
        }
    }

    void upload(const std::vector<unsigned char>& rgba) {
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        if (mipmaps_) glGenerateMipmap(GL_TEXTURE_2D);
    }

    void bind(int unit = 0) const {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, id_);
    }

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
    void create(const std::vector<float>& verts) {
        count_ = static_cast<GLsizei>(verts.size() / 8);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_STATIC_DRAW);
        const GLsizei stride = 8 * sizeof(float);
        glVertexAttribPointer(0, 3, GL_FLOAT, 0, stride, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, 0, stride,
                              reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, 0, stride,
                              reinterpret_cast<void*>(6 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glBindVertexArray(0);
    }

    // New vertices for a mesh that changes - ground rebuilt around a walker.
    void update(const std::vector<float>& verts) {
        if (!valid()) {
            create(verts);
            return;
        }
        count_ = static_cast<GLsizei>(verts.size() / 8);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_DYNAMIC_DRAW);
    }

    void draw() const {
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, count_);
    }

    bool valid() const { return vao_ != 0; }

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei count_ = 0;
};

inline std::vector<float> cube_vertices() {
    const float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    std::vector<float> v;
    auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, const float* nrm) {
        const Vec3 pts[6] = {a, b, c, a, c, d};
        const float uv[6][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 0}, {1, 1}, {0, 1}};
        for (int i = 0; i < 6; ++i)
            v.insert(v.end(), {pts[i].x, pts[i].y, pts[i].z, nrm[0], nrm[1], nrm[2], uv[i][0],
                               uv[i][1]});
    };
    const float h = 0.5f;
    face({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, n[0]);
    face({h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, n[1]);
    face({h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, n[2]);
    face({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, n[3]);
    face({-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, n[4]);
    face({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, n[5]);
    return v;
}

// A cylinder standing on y, radius 0.5 and height 1, centred like the cube, so
// the same model matrix sizes it: sx and sz are its diameters, sy its height.
// A box with its edges and corners rounded to `r` metres, `sx` x `sy` x `sz`
// in size, and narrowing to `taper` of its width and depth at the top (1:
// straight up). Made in the unit box every mesh shares, so the same model
// matrix places it; each normal is given pre-divided by the size, so that
// once the model's scale has been applied to it, it points the way the
// rounded surface does.
inline std::vector<float> rounded_box_vertices(float sx, float sy, float sz, float r, float taper = 1.0f,
                                               int seg = 3) {
    const float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    r = std::max(0.0f, std::min(r, std::min({hx, hy, hz}) * 0.95f));
    const float ix = hx - r, iy = hy - r, iz = hz - r;
    // Where the grid lines run along one axis: round the edge, then straight.
    const auto stops = [&](float h, float inner) {
        std::vector<float> s;
        for (int k = 0; k <= seg; ++k) {
            const float a = 1.5707963f * (1.0f - static_cast<float>(k) / seg);
            s.push_back(-inner - r * std::sin(a));
        }
        for (int k = 0; k <= seg; ++k) {
            const float a = 1.5707963f * static_cast<float>(k) / seg;
            s.push_back(inner + r * std::sin(a));
        }
        (void)h;
        return s;
    };
    const std::vector<float> xs = stops(hx, ix), ys = stops(hy, iy), zs = stops(hz, iz);
    std::vector<float> v;
    // A point on the (unrounded) surface, onto the rounded one: its nearest
    // point on the inner box, and out from there by r.
    const auto put = [&](Vec3 p, Vec3 face_n) {
        const Vec3 q{std::max(-ix, std::min(ix, p.x)), std::max(-iy, std::min(iy, p.y)), std::max(-iz, std::min(iz, p.z))};
        Vec3 d{p.x - q.x, p.y - q.y, p.z - q.z};
        const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        Vec3 n = len > 1e-7f ? Vec3{d.x / len, d.y / len, d.z / len} : face_n;
        Vec3 at = len > 1e-7f ? Vec3{q.x + n.x * r, q.y + n.y * r, q.z + n.z * r} : p;
        // The taper: narrower towards the top, and the sides lean in with it.
        const float t = (at.y + hy) / std::max(sy, 1e-6f);
        const float k = 1.0f + (taper - 1.0f) * t;
        at.x *= k, at.z *= k;
        if (taper != 1.0f) {
            n.y += (n.x * at.x / std::max(hx, 1e-6f) + n.z * at.z / std::max(hz, 1e-6f)) * (1.0f - taper) * 0.5f *
                   (std::fabs(n.y) < 0.99f ? 1.0f : 0.0f);
        }
        // Into the unit box; the normal pre-divided so the model's scale
        // turns it back the right way (the shader normalises it).
        v.insert(v.end(), {at.x / sx, at.y / sy, at.z / sz, n.x / sx, n.y / sy, n.z / sz,
                           (at.x / sx + 0.5f), (at.y / sy + 0.5f)});
    };
    // Each face: a grid of the stops of its two axes, at +-h on the third.
    const auto face = [&](int axis, float sign) {
        const std::vector<float>& a = axis == 0 ? ys : axis == 1 ? zs : xs;
        const std::vector<float>& b = axis == 0 ? zs : axis == 1 ? xs : ys;
        const float h = axis == 0 ? hx : axis == 1 ? hy : hz;
        const Vec3 fn = axis == 0 ? Vec3{sign, 0, 0} : axis == 1 ? Vec3{0, sign, 0} : Vec3{0, 0, sign};
        const auto P = [&](float u, float w) {
            return axis == 0 ? Vec3{sign * h, u, w} : axis == 1 ? Vec3{w, sign * h, u} : Vec3{u, w, sign * h};
        };
        for (std::size_t i = 0; i + 1 < a.size(); ++i)
            for (std::size_t j = 0; j + 1 < b.size(); ++j) {
                if (a[i + 1] - a[i] < 1e-7f || b[j + 1] - b[j] < 1e-7f) continue;
                Vec3 p00 = P(a[i], b[j]), p10 = P(a[i + 1], b[j]), p11 = P(a[i + 1], b[j + 1]), p01 = P(a[i], b[j + 1]);
                if (sign < 0) std::swap(p10, p01);
                for (const Vec3& p : {p00, p10, p11, p00, p11, p01}) put(p, fn);
            }
    };
    for (int axis = 0; axis < 3; ++axis) {
        face(axis, 1.0f);
        face(axis, -1.0f);
    }
    return v;
}

inline std::vector<float> cylinder_vertices(int segments = 28, float taper = 1.0f) {
    std::vector<float> v;
    const float h = 0.5f, pi2 = 6.2831853f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = pi2 * i / segments, a1 = pi2 * (i + 1) / segments;
        const float x0 = std::cos(a0) * h, z0 = std::sin(a0) * h, x1 = std::cos(a1) * h, z1 = std::sin(a1) * h;
        const float t = taper;  // the top's radius, against the bottom's
        const float u0 = static_cast<float>(i) / segments, u1 = static_cast<float>(i + 1) / segments;
        // A cone's side leans in: its normal tips up by as much.
        const float lean = (1.0f - t) * h, ny = lean / std::sqrt(lean * lean + 1.0f);
        const float nr = 1.0f / std::sqrt(lean * lean + 1.0f);
        const float n0x = std::cos(a0) * nr, n0z = std::sin(a0) * nr, n1x = std::cos(a1) * nr, n1z = std::sin(a1) * nr;
        v.insert(v.end(), {x0, -h, z0, n0x, ny, n0z, u0, 1, x1, -h, z1, n1x, ny, n1z, u1, 1,
                           x1 * t, h,  z1 * t, n1x, ny, n1z, u1, 0, x0, -h, z0, n0x, ny, n0z, u0, 1,
                           x1 * t, h,  z1 * t, n1x, ny, n1z, u1, 0, x0 * t, h,  z0 * t, n0x, ny, n0z, u0, 0});
        v.insert(v.end(), {0, h, 0, 0, 1, 0, 0.5f, 0.5f, x1 * t, h, z1 * t, 0, 1, 0, 0.5f + x1 * t, 0.5f + z1 * t,
                           x0 * t, h, z0 * t, 0, 1, 0, 0.5f + x0 * t, 0.5f + z0 * t});
        v.insert(v.end(), {0, -h, 0, 0, -1, 0, 0.5f, 0.5f, x0, -h, z0, 0, -1, 0, 0.5f + x0, 0.5f + z0,
                           x1, -h, z1, 0, -1, 0, 0.5f + x1, 0.5f + z1});
    }
    return v;
}

// A sphere of radius 0.5, centred like the cube.
inline std::vector<float> sphere_vertices(int stacks = 14, int slices = 24) {
    std::vector<float> v;
    const float pi = 3.14159265f;
    auto point = [&](int i, int j) {
        const float t = pi * i / stacks, p = 2 * pi * j / slices;
        const float nx = std::sin(t) * std::cos(p), ny = std::cos(t), nz = std::sin(t) * std::sin(p);
        v.insert(v.end(), {nx * 0.5f, ny * 0.5f, nz * 0.5f, nx, ny, nz, static_cast<float>(j) / slices,
                           static_cast<float>(i) / stacks});
    };
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            point(i, j);
            point(i + 1, j);
            point(i + 1, j + 1);
            point(i, j);
            point(i + 1, j + 1);
            point(i, j + 1);
        }
    return v;
}

// A unit quad standing in the y-z plane, facing +x: scale it by {1, height,
// width} and rotate it by the portal's yaw and it faces the way the portal
// does, with no extra quarter turn anywhere.
inline std::vector<float> quad_vertices() {
    const float h = 0.5f;
    return {
        0, -h, h,  1, 0, 0, 0, 1,  //
        0, -h, -h, 1, 0, 0, 1, 1,  //
        0, h,  -h, 1, 0, 0, 1, 0,  //
        0, -h, h,  1, 0, 0, 0, 1,  //
        0, h,  -h, 1, 0, 0, 1, 0,  //
        0, h,  h,  1, 0, 0, 0, 0,  //
    };
}

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
                bool with_depth = true, bool depth_texture = false) {
        destroy();
        w_ = w;
        h_ = h;
        samples_ = samples;
        glGenFramebuffers(1, &fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        if (samples > 0) {
            glGenRenderbuffers(1, &color_rb_);
            glBindRenderbuffer(GL_RENDERBUFFER, color_rb_);
            glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, internal_format, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                      color_rb_);
        } else {
            glGenTextures(1, &color_tex_);
            glBindTexture(GL_TEXTURE_2D, color_tex_);
            glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal_format), w, h, 0, GL_RGBA,
                         GL_HALF_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   color_tex_, 0);
        }
        if (with_depth && depth_texture && samples == 0) {
            glGenTextures(1, &depth_tex_);
            glBindTexture(GL_TEXTURE_2D, depth_tex_);
            glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), w, h, 0,
                         GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_tex_, 0);
        } else if (with_depth) {
            glGenRenderbuffers(1, &depth_rb_);
            glBindRenderbuffer(GL_RENDERBUFFER, depth_rb_);
            if (samples > 0) {
                glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w,
                                                 h);
            } else {
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            }
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                      depth_rb_);
        }
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("incomplete framebuffer");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, w_, h_);
    }

    // Resolve a multisampled target into a plain one.
    void blit_to(const RenderTarget& dst) const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo_);
        glBlitFramebuffer(0, 0, w_, h_, 0, 0, dst.w_, dst.h_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Resolve depth too: a multisampled target's depth into a plain one's.
    void blit_depth_to(const RenderTarget& dst) const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo_);
        glBlitFramebuffer(0, 0, w_, h_, 0, 0, dst.w_, dst.h_, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void bind_depth(int unit) const {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, depth_tex_);
    }

    void bind_color(int unit) const {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, color_tex_);
    }

    void destroy() {
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
        if (color_tex_) glDeleteTextures(1, &color_tex_);
        if (color_rb_) glDeleteRenderbuffers(1, &color_rb_);
        if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
        if (depth_tex_) glDeleteTextures(1, &depth_tex_);
        fbo_ = color_tex_ = color_rb_ = depth_rb_ = depth_tex_ = 0;
    }

    int width() const { return w_; }
    int height() const { return h_; }
    bool valid() const { return fbo_ != 0; }

private:
    GLuint fbo_ = 0, color_tex_ = 0, color_rb_ = 0, depth_rb_ = 0, depth_tex_ = 0;
    int w_ = 0, h_ = 0, samples_ = 0;
};

// Depth-only target sampled with hardware comparison, for shadows.
class ShadowMap {
public:
    void create(int size) {
        size_ = size;
        glGenFramebuffers(1, &fbo_);
        glGenTextures(1, &depth_);
        glBindTexture(GL_TEXTURE_2D, depth_);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), size, size, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const GLfloat border[4] = {1, 1, 1, 1};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                        static_cast<GLint>(GL_COMPARE_REF_TO_TEXTURE));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(GL_LEQUAL));
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("incomplete shadow framebuffer");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, size_, size_);
    }

    void bind_depth(int unit) const {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, depth_);
    }

    int size() const { return size_; }
    bool valid() const { return fbo_ != 0; }

private:
    GLuint fbo_ = 0;
    GLuint depth_ = 0;
    int size_ = 0;
};

}  // namespace sg::gl
