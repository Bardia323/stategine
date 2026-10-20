// Stategine - the renderer's GLSL made ready for a card that does not speak
// GL: compiled to SPIR-V (Vulkan reads it as it is), and from that to HLSL
// (D3D12, through the system's shader compiler). What a program declares -
// its loose uniforms, its pictures, its inputs - is read back so the GL
// calls that set them (sg/gl/GL.hpp) can be kept as they are.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sg::gl::native {

enum class Stage { vertex, fragment, compute };

// What a sampler reads: its picture's kind, and whether it compares depths.
enum class Sampled { tex2d, tex2d_array, tex3d, cube, buffer, tex2d_ms, utex2d };

struct SamplerSlot {
    std::string name;
    Sampled kind = Sampled::tex2d;
    bool shadow = false;  // a sampler...Shadow: compares as it reads
    int binding = 0;      // where the backend binds it (the same in every stage)
};

// One value a program holds, as GL names it: a member of the loose uniforms
// (an array's every element is one, `name[i]`, at consecutive locations).
struct UniformSlot {
    std::string name;
    enum Kind { f1, f2, f3, f4, m4, m3, i1, u1, image } kind = f4;
    int offset[2] = {-1, -1};  // in each stage's block (vertex, fragment), or -1
    int sampler = -1;          // a sampler's index in samplers
};

struct StorageSlot {
    int binding = 0;  // as the shader declares it
};

struct Translated {
    // One per stage: vertex and fragment, or compute alone.
    std::vector<std::uint32_t> spirv[2];
    std::string hlsl[2];
    std::vector<UniformSlot> uniforms;  // by location
    std::vector<SamplerSlot> samplers;
    std::vector<StorageSlot> storage;
    std::uint32_t block_size[2] = {0, 0};  // each stage's loose uniforms, in bytes
    std::uint32_t inputs = 0;              // the vertex inputs read, a bit a location
    std::uint32_t outputs = 0;             // the fragment outputs written, a bit a location
    int local_size[3] = {1, 1, 1};
    bool compute = false;
    std::string log;  // why it failed, if it did
    bool ok() const { return log.empty(); }
};

// Bindings as both backends lay them out: each stage's loose uniforms, then
// samplers, then storage buffers.
constexpr int kBlockBinding[2] = {0, 1};
constexpr int kFirstSampler = 2;
constexpr int kFirstStorage = 64;  // (past the last of a program's 48 samplers)
// The uniform the vertex stage is given beside the program's own: how its
// clip space is taken (x: 1 or -1, its y; y: 1 if depth is 0..1 already,
// 0 if GL's -1..1; z: the clip distances on, a bit each).
constexpr const char* kFixName = "sg_Fix";

// One stage parsed alone, as GL compiles a shader before linking it: why it
// does not build, or nothing.
std::string check(Stage, const char* src);

// A pixel stage's HLSL with its inputs laid out as the vertex stage's outputs.
std::string match_signature(const std::string& vs, const std::string& ps);

// `vs`/`fs` (or `cs` alone): GLSL as GL takes it. `hlsl`: HLSL too.
Translated translate(const char* vs, const char* fs, bool hlsl);
Translated translate_compute(const char* cs, bool hlsl);

}  // namespace sg::gl::native
