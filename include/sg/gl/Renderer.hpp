// Stategine - GL resources: programs, meshes, textures, render targets.
#pragma once

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
    void create(int w, int h) {
        w_ = w;
        h_ = h;
        glGenTextures(1, &id_);
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void upload(const std::vector<unsigned char>& rgba) {
        glBindTexture(GL_TEXTURE_2D, id_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
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
    void create(int w, int h, GLenum internal_format = GL_RGBA16F, int samples = 0,
                bool with_depth = true) {
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
        if (with_depth) {
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

    void bind_color(int unit) const {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
        glBindTexture(GL_TEXTURE_2D, color_tex_);
    }

    void destroy() {
        if (fbo_) glDeleteFramebuffers(1, &fbo_);
        if (color_tex_) glDeleteTextures(1, &color_tex_);
        if (color_rb_) glDeleteRenderbuffers(1, &color_rb_);
        if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
        fbo_ = color_tex_ = color_rb_ = depth_rb_ = 0;
    }

    int width() const { return w_; }
    int height() const { return h_; }
    bool valid() const { return fbo_ != 0; }

private:
    GLuint fbo_ = 0, color_tex_ = 0, color_rb_ = 0, depth_rb_ = 0;
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
