#include "Glsl.hpp"

#include <algorithm>
#include <map>
#include <mutex>
#include <regex>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_hlsl.hpp>

namespace sg::gl::native {
namespace {

constexpr const char* kBlockName = "sg_Uniforms";

void start_glslang() {
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
}

// The vertex stage's own main, called from one that then takes its clip
// space as the card wants it (kFixName): y the card's way up, depth 0..1,
// and a clip distance that GL has off kept from cutting anything.
std::string wrap_vertex(const std::string& src) {
    std::string out = std::regex_replace(src, std::regex(R"(\bvoid\s+main\s*\()"), "void sg_main(");
    out += "\nuniform vec4 sg_Fix;\nvoid main() {\n    sg_main();\n";
    if (src.find("gl_ClipDistance") != std::string::npos)
        out += "    for (int i = 0; i < gl_ClipDistance.length(); ++i)\n"
               "        if (((int(sg_Fix.z) >> i) & 1) == 0) gl_ClipDistance[i] = 1.0;\n";
    out += "    if (sg_Fix.y == 0.0) gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n"
           "    gl_Position.y *= sg_Fix.x;\n}\n";
    return out;
}

EShLanguage language(Stage s) {
    return s == Stage::vertex ? EShLangVertex : s == Stage::fragment ? EShLangFragment : EShLangCompute;
}

std::unique_ptr<glslang::TShader> parse(Stage stage, const std::string& src, std::string& log) {
    auto sh = std::make_unique<glslang::TShader>(language(stage));
    const char* text = src.c_str();
    sh->setStrings(&text, 1);
    sh->setEnvInput(glslang::EShSourceGlsl, language(stage), glslang::EShClientVulkan, 100);
    sh->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    sh->setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    sh->setEnvInputVulkanRulesRelaxed();
    sh->setGlobalUniformBlockName(kBlockName);
    sh->setGlobalUniformSet(0);
    sh->setGlobalUniformBinding(0);
    sh->setAutoMapLocations(true);
    sh->setAutoMapBindings(true);
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!sh->parse(GetDefaultResources(), 330, ECoreProfile, false, false, messages)) {
        log += std::string(stage == Stage::vertex ? "vertex: " : stage == Stage::fragment ? "fragment: " : "compute: ") +
               sh->getInfoLog();
        return nullptr;
    }
    return sh;
}

UniformSlot::Kind kind_of(const spirv_cross::SPIRType& t) {
    using B = spirv_cross::SPIRType::BaseType;
    if (t.basetype == B::Int || t.basetype == B::Boolean) return UniformSlot::i1;
    if (t.basetype == B::UInt) return UniformSlot::u1;
    if (t.columns == 4) return UniformSlot::m4;
    if (t.columns == 3) return UniformSlot::m3;
    switch (t.vecsize) {
        case 1: return UniformSlot::f1;
        case 2: return UniformSlot::f2;
        case 3: return UniformSlot::f3;
        default: return UniformSlot::f4;
    }
}

// Every leaf of the loose uniforms' block, as GL would name it.
struct Leaf {
    std::string name;
    UniformSlot::Kind kind;
    int offset;
};

void flatten(const spirv_cross::Compiler& c, const spirv_cross::SPIRType& block, const std::string& prefix, int base,
             std::vector<Leaf>& out) {
    for (std::uint32_t i = 0; i < block.member_types.size(); ++i) {
        const auto& t = c.get_type(block.member_types[i]);
        const std::string name = prefix + c.get_member_name(block.self, i);
        const int at = base + static_cast<int>(c.type_struct_member_offset(block, i));
        const int n = t.array.empty() ? 1 : static_cast<int>(t.array[0]);
        const int stride = t.array.empty() ? 0 : static_cast<int>(c.type_struct_member_array_stride(block, i));
        for (int e = 0; e < n; ++e) {
            const std::string el = t.array.empty() ? name : name + "[" + std::to_string(e) + "]";
            if (t.basetype == spirv_cross::SPIRType::Struct) {
                // (The element's own type: arrays of structs flatten by element.)
                const auto& inner = c.get_type(t.self);
                flatten(c, inner, el + ".", at + e * stride, out);
            } else {
                out.push_back({el, kind_of(t), at + e * stride});
            }
        }
    }
}

Sampled sampled_kind(const spirv_cross::SPIRType& t) {
    const auto& im = t.image;
    if (im.dim == spv::DimBuffer) return Sampled::buffer;
    if (im.dim == spv::Dim3D) return Sampled::tex3d;
    if (im.dim == spv::DimCube) return Sampled::cube;
    if (im.ms) return Sampled::tex2d_ms;
    if (im.arrayed) return Sampled::tex2d_array;
    return Sampled::tex2d;
}

void patch(std::vector<std::uint32_t>& words, const spirv_cross::Compiler& c, std::uint32_t id, int binding) {
    std::uint32_t at = 0;
    if (c.get_binary_offset_for_decoration(id, spv::DecorationBinding, at)) words[at] = static_cast<std::uint32_t>(binding);
    if (c.get_binary_offset_for_decoration(id, spv::DecorationDescriptorSet, at)) words[at] = 0;
}

std::string to_hlsl(const std::vector<std::uint32_t>& words, std::string& log) {
    try {
        spirv_cross::CompilerHLSL h(words);
        spirv_cross::CompilerHLSL::Options o;
        o.shader_model = 60;
        // (A matrix read per vertex: TEXCOORD3 to 6, a location a column, as the input layout says.)
        o.flatten_matrix_vertex_input_semantics = true;
        // (Every storage buffer a UAV, read-only or not: each bound as one.)
        o.force_storage_buffer_as_uav = true;
        h.set_hlsl_options(o);
        spirv_cross::CompilerGLSL::Options common;
        common.vertex.fixup_clipspace = false;
        common.vertex.flip_vert_y = false;
        h.set_common_options(common);
        return h.compile();
    } catch (const std::exception& e) {
        log += std::string("hlsl: ") + e.what();
        return {};
    }
}

// Reads one stage's SPIR-V into `t` (slots merged by name across stages),
// and lays its bindings out as kBlockBinding etc. say.
void reflect(Translated& t, int slot, std::vector<std::uint32_t>& words, std::map<std::string, int>& by_name) {
    spirv_cross::Compiler c(words);
    const auto res = c.get_shader_resources();
    for (const auto& ub : res.uniform_buffers) {
        const auto& type = c.get_type(ub.base_type_id);
        if (c.get_name(ub.base_type_id) != kBlockName && ub.name != kBlockName) {
            t.log += "a named uniform block (" + ub.name + ") is not supported\n";
            continue;
        }
        t.block_size[slot] = static_cast<std::uint32_t>(c.get_declared_struct_size(type));
        patch(words, c, ub.id, kBlockBinding[slot]);
        std::vector<Leaf> leaves;
        flatten(c, type, "", 0, leaves);
        for (const Leaf& l : leaves) {
            auto it = by_name.find(l.name);
            if (it == by_name.end()) {
                it = by_name.emplace(l.name, static_cast<int>(t.uniforms.size())).first;
                UniformSlot u;
                u.name = l.name;
                u.kind = l.kind;
                t.uniforms.push_back(u);
            }
            t.uniforms[static_cast<std::size_t>(it->second)].offset[slot] = l.offset;
        }
    }
    for (const auto& si : res.sampled_images) {
        const auto& type = c.get_type(si.type_id);
        if (!type.array.empty()) {
            t.log += "an array of samplers (" + si.name + ") is not supported\n";
            continue;
        }
        auto found = std::find_if(t.samplers.begin(), t.samplers.end(), [&](const SamplerSlot& s) { return s.name == si.name; });
        if (found == t.samplers.end()) {
            SamplerSlot s;
            s.name = si.name;
            s.kind = sampled_kind(type);
            if (c.get_type(type.image.type).basetype == spirv_cross::SPIRType::UInt) s.kind = Sampled::utex2d;
            s.shadow = type.image.depth;
            s.binding = kFirstSampler + static_cast<int>(t.samplers.size());
            t.samplers.push_back(s);
            found = t.samplers.end() - 1;
        }
        patch(words, c, si.id, found->binding);
    }
    for (const auto& sb : res.storage_buffers) {
        const int b = static_cast<int>(c.get_decoration(sb.id, spv::DecorationBinding));
        if (std::none_of(t.storage.begin(), t.storage.end(), [&](const StorageSlot& s) { return s.binding == b; }))
            t.storage.push_back({b});
        patch(words, c, sb.id, kFirstStorage + b);
    }
    if (!res.separate_images.empty() || !res.separate_samplers.empty() || !res.storage_images.empty())
        t.log += "separate images, samplers or storage images are not supported\n";
    const auto model = c.get_execution_model();
    if (model == spv::ExecutionModelVertex)
        for (const auto& in : res.stage_inputs) {
            if (!c.has_decoration(in.id, spv::DecorationLocation)) continue;
            // (A matrix takes a location a column.)
            const auto& type = c.get_type(in.type_id);
            const std::uint32_t at = c.get_decoration(in.id, spv::DecorationLocation);
            for (std::uint32_t i = 0; i < std::max(type.columns, 1u); ++i) t.inputs |= 1u << (at + i);
        }
    if (model == spv::ExecutionModelFragment)
        for (const auto& out : res.stage_outputs) {
            const auto& type = c.get_type(out.type_id);
            const std::uint32_t at = c.get_decoration(out.id, spv::DecorationLocation);
            const std::uint32_t n = type.array.empty() ? 1 : type.array[0];
            for (std::uint32_t i = 0; i < n; ++i) t.outputs |= 1u << (at + i);
        }
    if (model == spv::ExecutionModelGLCompute)
        for (int i = 0; i < 3; ++i) t.local_size[i] = static_cast<int>(c.get_execution_mode_argument(spv::ExecutionModeLocalSize, static_cast<std::uint32_t>(i)));
}

// The fragment stage's inputs at the locations the vertex stage writes them
// to, matched by name, as GL links them (each stage was laid out alone).
void match_varyings(std::vector<std::uint32_t>& vs, std::vector<std::uint32_t>& fs, std::string& log) {
    spirv_cross::Compiler v(vs), f(fs);
    std::map<std::string, std::uint32_t> written;
    for (const auto& out : v.get_shader_resources().stage_outputs)
        if (v.has_decoration(out.id, spv::DecorationLocation)) written[out.name] = v.get_decoration(out.id, spv::DecorationLocation);
    for (const auto& in : f.get_shader_resources().stage_inputs) {
        auto it = written.find(in.name);
        if (it == written.end()) {
            log += "the fragment stage reads " + in.name + ", which the vertex stage does not write\n";
            continue;
        }
        std::uint32_t at = 0;
        if (f.get_binary_offset_for_decoration(in.id, spv::DecorationLocation, at)) fs[at] = it->second;
    }
}

std::vector<std::uint32_t> to_spirv(const glslang::TProgram& p, EShLanguage lang) {
    std::vector<std::uint32_t> words;
    glslang::SpvOptions o;
    o.disableOptimizer = true;
    glslang::GlslangToSpv(*p.getIntermediate(lang), words, &o);
    return words;
}

// Samplers bound in the order of their names, the same in every stage.
void name_samplers(Translated& t) {
    for (std::size_t i = 0; i < t.samplers.size(); ++i) {
        UniformSlot u;
        u.name = t.samplers[i].name;
        u.kind = UniformSlot::image;
        u.sampler = static_cast<int>(i);
        t.uniforms.push_back(u);
    }
}

// A struct's body (its lines between the braces) in generated HLSL, and where.
bool struct_body(const std::string& src, const std::string& name, std::size_t& from, std::size_t& to) {
    const std::size_t at = src.find("struct " + name + "\n{\n");
    if (at == std::string::npos) return false;
    from = at + name.size() + 10;
    to = src.find("};", from);
    return to != std::string::npos;
}

}  // namespace

// The pixel stage's inputs declared as the vertex stage's outputs are, one
// for one, in the same order: DXIL links the two by where each lies, not by
// name. (Its own system values - which face, which sample - after them.)
std::string match_signature(const std::string& vs, const std::string& ps) {
    std::size_t vf, vt, pf, pt;
    if (!struct_body(vs, "SPIRV_Cross_Output", vf, vt) || !struct_body(ps, "SPIRV_Cross_Input", pf, pt)) return ps;
    std::string body = vs.substr(vf, vt - vf);
    const std::string position = "gl_Position : SV_Position";
    if (const std::size_t at = body.find(position); at != std::string::npos) body.replace(at, position.size(), "gl_FragCoord : SV_Position");
    const std::string own = ps.substr(pf, pt - pf);
    std::size_t line = 0;
    while (line < own.size()) {
        const std::size_t end = own.find('\n', line);
        const std::string l = own.substr(line, end == std::string::npos ? std::string::npos : end - line + 1);
        if (l.find(": SV_") != std::string::npos && l.find("SV_Position") == std::string::npos && l.find("SV_ClipDistance") == std::string::npos)
            body += l;
        if (end == std::string::npos) break;
        line = end + 1;
    }
    return ps.substr(0, pf) + body + ps.substr(pt);
}

std::string check(Stage stage, const char* src) {
    start_glslang();
    std::string log;
    parse(stage, stage == Stage::vertex ? wrap_vertex(src) : std::string(src), log);
    return log;
}

Translated translate(const char* vs, const char* fs, bool hlsl) {
    start_glslang();
    Translated t;
    const std::string vsrc = wrap_vertex(vs);
    auto v = parse(Stage::vertex, vsrc, t.log);
    auto f = parse(Stage::fragment, fs, t.log);
    if (!v || !f) return t;
    glslang::TProgram p;
    p.addShader(v.get());
    p.addShader(f.get());
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!p.link(messages) || !p.mapIO()) {
        t.log = std::string("link: ") + p.getInfoLog();
        return t;
    }
    t.spirv[0] = to_spirv(p, EShLangVertex);
    t.spirv[1] = to_spirv(p, EShLangFragment);
    match_varyings(t.spirv[0], t.spirv[1], t.log);
    std::map<std::string, int> by_name;
    reflect(t, 0, t.spirv[0], by_name);
    reflect(t, 1, t.spirv[1], by_name);
    name_samplers(t);
    if (!t.ok()) return t;
    if (hlsl) {
        for (int s = 0; s < 2; ++s) t.hlsl[s] = to_hlsl(t.spirv[s], t.log);
        t.hlsl[1] = match_signature(t.hlsl[0], t.hlsl[1]);
    }
    return t;
}

Translated translate_compute(const char* cs, bool hlsl) {
    start_glslang();
    Translated t;
    t.compute = true;
    auto c = parse(Stage::compute, cs, t.log);
    if (!c) return t;
    glslang::TProgram p;
    p.addShader(c.get());
    const auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!p.link(messages) || !p.mapIO()) {
        t.log = std::string("link: ") + p.getInfoLog();
        return t;
    }
    t.spirv[0] = to_spirv(p, EShLangCompute);
    std::map<std::string, int> by_name;
    reflect(t, 0, t.spirv[0], by_name);
    name_samplers(t);
    if (t.ok() && hlsl) t.hlsl[0] = to_hlsl(t.spirv[0], t.log);
    return t;
}

}  // namespace sg::gl::native
