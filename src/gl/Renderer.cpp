#include "sg/gl/Renderer.hpp"

namespace sg::gl {

GLuint compile(GLenum type, const char* src, const char* tag) {
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

Program::Program(const char* vs, const char* fs, const char* tag) {
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

GLint Program::uniform(const char* name) const {
    auto hit = by_address_.find(name);
    if (hit != by_address_.end() && std::strcmp(hit->second.first, name) == 0) return hit->second.second;
    auto it = locations_.find(name);
    GLint loc;
    if (it != locations_.end()) {
        loc = it->second;
    } else {
        loc = glGetUniformLocation(id_, name);
        it = locations_.emplace(name, loc).first;
    }
    by_address_[name] = {it->first.c_str(), loc};
    return loc;
}

void Program::put(GLint at, int n, const float* v) const {
    if (at < 0) return;
    const auto i = static_cast<std::size_t>(at);
    if (i >= held_.size()) held_.resize(i + 1), held_n_.resize(i + 1, 0);
    if (held_n_[i] == n && std::memcmp(held_[i].data(), v, sizeof(float) * static_cast<std::size_t>(n)) == 0) return;
    std::memcpy(held_[i].data(), v, sizeof(float) * static_cast<std::size_t>(n));
    held_n_[i] = static_cast<signed char>(n);
    switch (n) {
        case 1: glUniform1f(at, v[0]); break;
        case 2: glUniform2f(at, v[0], v[1]); break;
        case 3: glUniform3f(at, v[0], v[1], v[2]); break;
        case 4: glUniform4f(at, v[0], v[1], v[2], v[3]); break;
        default: glUniformMatrix4fv(at, 1, 0, v);
    }
}

void Program::set(const char* n, int i) const {
    const GLint at = uniform(n);
    if (at < 0) return;
    const auto k = static_cast<std::size_t>(at);
    if (k >= held_.size()) held_.resize(k + 1), held_n_.resize(k + 1, 0);
    float bits;
    std::memcpy(&bits, &i, sizeof bits);
    if (held_n_[k] == -1 && std::memcmp(&held_[k][0], &bits, sizeof bits) == 0) return;
    held_[k][0] = bits, held_n_[k] = -1;
    glUniform1i(at, i);
}

void Texture::create(int w, int h, bool mipmaps, bool srgb, bool pixel) {
    if (id_) glDeleteTextures(1, &id_);  // made again, at a new size
    w_ = w;
    h_ = h;
    mipmaps_ = mipmaps;
    packed_ = false;
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8), w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // Every level it will have, made now: the mipmaps made from the picture
    // later are then written where they are, not the texture made over again
    // at each upload to hold them.
    if (mipmaps)
        for (int level = 1, lw = w, lh = h; lw > 1 || lh > 1; ++level) {
            lw = std::max(1, lw / 2), lh = std::max(1, lh / 2);
            glTexImage2D(GL_TEXTURE_2D, level, static_cast<GLint>(srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8), lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         nullptr);
        }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (pixel) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    if (mipmaps && !pixel) {
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
        glGetError();  // anisotropy is core only from 4.6; without it, plain trilinear
    }
}

void Texture::upload(const std::vector<unsigned char>& rgba) {
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (mipmaps_) glGenerateMipmap(GL_TEXTURE_2D);
}

void Texture::create_packed(const render::Packed& p) {
    if (id_) glDeleteTextures(1, &id_);
    w_ = p.w;
    h_ = p.h;
    mipmaps_ = true;
    packed_ = true;
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    const GLenum format = p.srgb ? GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM : GL_COMPRESSED_RGBA_BPTC_UNORM;
    for (std::size_t level = 0; level < p.levels.size(); ++level) {
        const render::Packed::Level& l = p.levels[level];
        glCompressedTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), format, l.w, l.h, 0, static_cast<GLsizei>(l.size),
                               p.bytes.data() + l.at);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, 8.0f);
    glGetError();  // as create's: without anisotropy, plain trilinear
}

bool Texture::packs() {
    static const bool yes = [] {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; ++i)
            if (const unsigned char* e = glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)))
                if (std::strcmp(reinterpret_cast<const char*>(e), "GL_ARB_texture_compression_bptc") == 0) return true;
        return false;
    }();
    return yes;
}

void Texture::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, id_);
}

void Mesh::create(const std::vector<float>& verts) {
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

void Mesh::update(const std::vector<float>& verts) {
    if (!valid()) {
        create(verts);
        return;
    }
    count_ = static_cast<GLsizei>(verts.size() / 8);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
}

void Mesh::draw_instanced(GLuint buffer, GLsizei instances) const {
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    const GLsizei stride = kInstanceFloats * sizeof(float);
    for (GLuint i = 0; i < 7; ++i) {
        glVertexAttribPointer(3 + i, 4, GL_FLOAT, 0, stride, reinterpret_cast<void*>(i * 4 * sizeof(float)));
        glEnableVertexAttribArray(3 + i);
        glVertexAttribDivisor(3 + i, 1);
    }
    glDrawArraysInstanced(GL_TRIANGLES, 0, count_, instances);
    for (GLuint i = 0; i < 7; ++i) glDisableVertexAttribArray(3 + i);
}

void RenderTarget::mipmap() {
    if (!color_tex_) return;
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    if (!mipmapped_) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        mipmapped_ = true;
    }
    glGenerateMipmap(GL_TEXTURE_2D);
}

void RenderTarget::create(int w, int h, GLenum internal_format, int samples, bool with_depth, bool depth_texture) {
    destroy();
    mipmapped_ = false;
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

void CubeMap::create(int size) {
    if (tex_ && size == size_) return;
    if (!tex_) glGenTextures(1, &tex_), glGenFramebuffers(1, &fbo_);
    size_ = size;
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_);
    for (int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(i), 0, static_cast<GLint>(GL_RGBA16F), size, size, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (GLenum w : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTexParameteri(GL_TEXTURE_CUBE_MAP, w, GL_CLAMP_TO_EDGE);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
}

void CubeMap::take(int face, GLuint from, int w, int h) {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(face), tex_, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
    glBlitFramebuffer(0, 0, w, h, 0, 0, size_, size_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CubeMap::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_);
    glActiveTexture(GL_TEXTURE0);
}

void RenderTarget::blit_to(const RenderTarget& dst) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo_);
    glBlitFramebuffer(0, 0, w_, h_, 0, 0, dst.w_, dst.h_, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::blit_depth_to(const RenderTarget& dst) const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.fbo_);
    glBlitFramebuffer(0, 0, w_, h_, 0, 0, dst.w_, dst.h_, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::bind_depth(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, depth_tex_);
}

void RenderTarget::bind_color(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, color_tex_);
}

void RenderTarget::destroy() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (color_tex_) glDeleteTextures(1, &color_tex_);
    if (color_rb_) glDeleteRenderbuffers(1, &color_rb_);
    if (depth_rb_) glDeleteRenderbuffers(1, &depth_rb_);
    if (depth_tex_) glDeleteTextures(1, &depth_tex_);
    fbo_ = color_tex_ = color_rb_ = depth_rb_ = depth_tex_ = 0;
}

void ShadowMap::create(int size) {
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

void ShadowMap::bind_depth(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, depth_);
}

GLuint InstanceBuffer::upload(const std::vector<float>& data) {
    if (!vbo_) glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(), GL_DYNAMIC_DRAW);
    return vbo_;
}

bool ShadowArray::ensure(int size, int layers) {
    layers = std::max(layers, 1);
    if (depth_ != 0 && size == size_ && layers <= layers_) return false;
    release();
    size_ = size;
    layers_ = layers;
    glGenTextures(1, &depth_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depth_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24), size, size, layers, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const GLfloat border[4] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, static_cast<GLint>(GL_COMPARE_REF_TO_TEXTURE));
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(GL_LEQUAL));
    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth_, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("incomplete shadow framebuffer");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void ShadowArray::bind_layer(int layer) const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth_, 0, layer);
    glViewport(0, 0, size_, size_);
}

void ShadowArray::bind_depth(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D_ARRAY, depth_);
}

void ShadowArray::release() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (depth_) glDeleteTextures(1, &depth_);
    fbo_ = depth_ = 0;
    layers_ = 0;
}

bool LayerArray::ensure(int w, int h, int layers, bool volume) {
    const GLenum target = volume ? GL_TEXTURE_3D : GL_TEXTURE_2D_ARRAY;
    if (tex_ != 0 && w == w_ && h == h_ && layers == layers_ && target == target_) return false;
    release();
    w_ = w, h_ = h, layers_ = layers, target_ = target;
    glGenTextures(1, &tex_);
    glBindTexture(target, tex_);
    glTexImage3D(target, 0, static_cast<GLint>(GL_RGBA16F), w, h, layers, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    // A framebuffer for each group of layers, each layer of it an output:
    // made once, so drawing into them changes no attachment.
    GLenum outputs[kGroup];
    for (int i = 0; i < kGroup; ++i) outputs[i] = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
    fbos_.assign(static_cast<std::size_t>((layers + kGroup - 1) / kGroup), 0);
    for (std::size_t g = 0; g < fbos_.size(); ++g) {
        glGenFramebuffers(1, &fbos_[g]);
        glBindFramebuffer(GL_FRAMEBUFFER, fbos_[g]);
        for (int i = 0; i < kGroup; ++i)
            glFramebufferTextureLayer(GL_FRAMEBUFFER, outputs[i], tex_, 0, std::min(static_cast<int>(g) * kGroup + i, layers - 1));
        glDrawBuffers(kGroup, outputs);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("incomplete layered framebuffer");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void LayerArray::bind_group(int group) const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbos_[static_cast<std::size_t>(group)]);
    glViewport(0, 0, w_, h_);
}

void LayerArray::bind_color(int unit) const {
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(target_, tex_);
}

void LayerArray::release() {
    for (GLuint f : fbos_)
        if (f) glDeleteFramebuffers(1, &f);
    fbos_.clear();
    if (tex_) glDeleteTextures(1, &tex_);
    tex_ = 0;
    w_ = h_ = layers_ = 0;
}

}  // namespace sg::gl
