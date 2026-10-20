// Stategine - work on the card's own, against a real GL context (or Vulkan
// or D3D12 answering its calls: SG_GPU).
//
// A compute program reads a picture through a sampler and a storage buffer,
// and writes another storage buffer: read back, each value is what it was
// made from. Its writes are the commands of a draw drawn after it
// (glMultiDrawArraysIndirect): what it says to draw is drawn, what it says
// not to is not.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/gl/GL.hpp"
#include "sg/gl/Window.hpp"

namespace {
bool ok = true;
void check(bool c, const std::string& what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what.c_str());
    ok = ok && c;
}
using namespace sg::gl;

GLuint program_of(GLenum type, const char* src) {
    const GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint good = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &good);
    if (!good) {
        char log[2048] = {};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        std::printf("shader: %s\n", log);
    }
    const GLuint p = glCreateProgram();
    glAttachShader(p, s);
    glLinkProgram(p);
    glDeleteShader(s);
    return p;
}
}  // namespace

int main() {
    const int W = 64, H = 64;
    Window window(W, H, "compute");
    std::printf("drawn by %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    if (!glDispatchCompute || !glBindBufferBase || !glMultiDrawArraysIndirect || !glMemoryBarrier) {
        std::printf("no compute here: nothing to check\nall ok\n");
        return 0;
    }

    // A 4x4 picture of known values, and a buffer of as many to add.
    std::vector<float> texels(16 * 4);
    for (int i = 0; i < 16; ++i) texels[i * 4] = static_cast<float>(i), texels[i * 4 + 1] = 0.5f * static_cast<float>(i);
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, 4, 0, GL_RGBA, GL_FLOAT, texels.data());
    std::vector<float> add(16);
    for (int i = 0; i < 16; ++i) add[i] = 100.0f * static_cast<float>(i);
    GLuint in = 0, out = 0, commands = 0;
    glGenBuffers(1, &in), glGenBuffers(1, &out), glGenBuffers(1, &commands);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, in);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(add.size() * 4), add.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, out);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 16 * 4, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, commands);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 2 * 16, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    const GLuint work = program_of(GL_COMPUTE_SHADER, R"(#version 430
layout(local_size_x = 16) in;
layout(std430, binding = 0) readonly buffer In { float add[]; };
layout(std430, binding = 1) writeonly buffer Out { float made[]; };
layout(std430, binding = 2) writeonly buffer Commands { uvec4 draws[]; };
uniform sampler2D uPicture;
uniform float uScale;
void main() {
    uint i = gl_GlobalInvocationID.x;
    vec4 t = texelFetch(uPicture, ivec2(int(i) % 4, int(i) / 4), 0);
    made[i] = t.r * uScale + t.g + add[i];
    // Two draws of a triangle filling the left half and the right: the first
    // drawn (3 corners), the second not (none).
    if (i == 0u) draws[0] = uvec4(3u, 1u, 0u, 0u);
    if (i == 1u) draws[1] = uvec4(0u, 1u, 3u, 0u);
})");
    GLint linked = 0;
    glGetProgramiv(work, GL_LINK_STATUS, &linked);
    check(linked != 0, "a compute program reading a picture and buffers links");
    glUseProgram(work);
    glActiveTexture(GL_TEXTURE0 + 3);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(work, "uPicture"), 3);
    glUniform1f(glGetUniformLocation(work, "uScale"), 2.0f);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, in);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, out);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, commands);
    glDispatchCompute(1, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
    std::vector<float> made(16);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, out);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, 16 * 4, made.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    int right = 0;
    for (int i = 0; i < 16; ++i) right += std::fabs(made[i] - (2.0f * i + 0.5f * i + 100.0f * i)) < 1e-3f ? 1 : 0;
    std::printf("made: %.1f %.1f %.1f ... %.1f\n", made[0], made[1], made[2], made[15]);
    check(right == 16, "each value is what it was made from: the picture through its sampler, and the buffer (" + std::to_string(right) + " of 16)");

    // The commands it wrote, drawn: a triangle over the left half only.
    const char* vs = R"(#version 330 core
void main() {
    vec2 p = gl_VertexID == 0 ? vec2(-1.0, -1.0) : gl_VertexID == 1 ? vec2(0.0, -1.0) : vec2(-1.0, 1.0);
    if (gl_VertexID >= 3) p.x += 1.0;
    gl_Position = vec4(p, 0.0, 1.0);
})";
    const char* fs = R"(#version 330 core
out vec4 o;
void main() { o = vec4(1.0, 0.5, 0.25, 1.0); })";
    const GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(v, 1, &vs, nullptr), glCompileShader(v);
    glShaderSource(f, 1, &fs, nullptr), glCompileShader(f);
    const GLuint draw = glCreateProgram();
    glAttachShader(draw, v), glAttachShader(draw, f), glLinkProgram(draw);
    glDeleteShader(v), glDeleteShader(f);
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(draw);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, commands);
    glMultiDrawArraysIndirect(GL_TRIANGLES, nullptr, 2, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    const auto at = [&](int x, int y) { return px[(static_cast<std::size_t>(y) * W + x) * 4]; };
    std::printf("drawn: left %d, right %d\n", at(4, 8), at(W - 4, 8));
    check(at(4, 8) > 200, "what the compute program said to draw is drawn");
    check(at(W - 4, 8) < 20, "and what it said not to is not");
    std::printf(ok ? "all ok\n" : "FAILED\n");
    return ok ? 0 : 1;
}
