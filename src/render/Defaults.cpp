#include "sg/render/Defaults.hpp"
#include "sg/render/Graphics.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sg::render {
namespace {
// Every setting a file may say: its key, where it is, and what it is.
struct Setting {
    const char* key;
    int Quality::*whole = nullptr;
    float Quality::*number = nullptr;
    bool Quality::*flag = nullptr;
    const char* what;
    // A whole number said as a word: its words, in order from 0.
    std::vector<const char*> words = {};
};
const Setting kSettings[] = {
    {"shadow_size", &Quality::shadow_size, nullptr, nullptr, "pixels a side of each lamp's shadow map (1024 is cheaper, softer)"},
    {"shadow_budget_mb", &Quality::shadow_budget_mb, nullptr, nullptr,
     "how much of the card shadow maps may hold, MB (0: as the card allows; -1: no limit)"},
    {"msaa", &Quality::msaa, nullptr, nullptr, "samples a pixel, for smooth edges (0: none, 2, 4, 8)"},
    {"taa", nullptr, nullptr, &Quality::taa,
     "smooth edges and shimmer over frames, for less than msaa (true/false; with it, msaa = 0)"},
    {"render_scale", nullptr, &Quality::render_scale, nullptr,
     "with taa, the view drawn at this part of the screen's pixels each way and made whole over frames (0.5 to 1; 1: the screen's own)"},
    {"reflections", nullptr, nullptr, &Quality::reflections,
     "planes that say they reflect (reflects, floor_reflects) show the room in them (true/false)"},
    {"reflection_scale", nullptr, &Quality::reflection_scale, nullptr, "how sharp reflections are drawn: the pixels each may take, a part of the screen's (0.1 to 1)"},
    {"view_scale", nullptr, &Quality::view_scale, nullptr, "how sharp views through doorways are drawn: a part of the screen's pixels each way (0.4 to 1)"},
    {"bloom_passes", &Quality::bloom_passes, nullptr, nullptr, "blurs of the glow round bright things"},
    {"bloom_strength", nullptr, &Quality::bloom_strength, nullptr, "how much bright things glow (the default look's)"},
    {"bloom_threshold", nullptr, &Quality::bloom_threshold, nullptr, "how bright a thing must be to glow (the default look's)"},
    {"exposure", nullptr, &Quality::exposure, nullptr, "how bright the picture is made (the default look's)"},
    {"instancing", nullptr, nullptr, &Quality::instancing, "things of one shape drawn together (true/false)"},
    {"pixel", &Quality::pixel, nullptr, nullptr, "how many of the screen's pixels, each way, a pixel of the picture takes (1: the screen's own; 2, 3: an older day's)"},
    {"sheafify", nullptr, nullptr, &Quality::sheafify, "every doorway whose rooms agree made one space, not only those that say `sheaf` (true/false)"},
    {"pack", nullptr, nullptr, &Quality::pack, "what things wear sent to the card packed, a quarter of the memory (true/false)"},
    {"skip_unseen", nullptr, nullptr, &Quality::skip_unseen, "views through doorways the eye cannot see left out by the card (true/false)"},
    {"lamp_cells", nullptr, nullptr, &Quality::lamp_cells, "each part of a view asks only the lamps whose range reaches it (true/false)"},
    {"stream_textures", nullptr, nullptr, &Quality::stream_textures, "packed pictures sent coarse first, each finer level as it is drawn with (true/false)"},
    {"texture_most", &Quality::texture_most, nullptr, nullptr, "the most pixels each way of any packed picture the card holds (0: all; 1024 a quarter of 2048's memory)"},
    {"gpu_cull", nullptr, nullptr, &Quality::gpu_cull, "merged cells' things culled by the card itself, where it can (GL 4.3) (true/false)"},
    {"proxies", nullptr, nullptr, &Quality::proxies, "still things of a room's cells drawn together, each cell in a call or two (true/false)"},
    {"lod", nullptr, nullptr, &Quality::lod, "a model far off drawn with fewer faces, where no pixel would show the difference (true/false)"},
    {"trace", nullptr, nullptr, &Quality::trace, "light traced along rays where a look asks for it (`trace`, `path`); false: never (true/false)"},
    {"upscaler", &Quality::upscaler, nullptr, nullptr,
     "who makes the picture whole over frames: taa (the engine's), dlss (NVIDIA's DLSS, drawn at fewer pixels: Vulkan, an NVIDIA card), dlaa (DLSS at the screen's pixels)",
     {"taa", "dlss", "dlaa"}},
    {"dlss_mode", &Quality::dlss_mode, nullptr, nullptr,
     "with dlss, how few pixels it draws: quality (2/3 each way), balanced (0.58), performance (1/2), ultra_performance (1/3), native (all)",
     {"quality", "balanced", "performance", "ultra_performance", "native"}},
};
// A word's place among a setting's words; -1 if it is none of them.
int word_of(const Setting& s, const std::string& w) {
    for (std::size_t i = 0; i < s.words.size(); ++i)
        if (w == s.words[i]) return static_cast<int>(i);
    return -1;
}
std::string trimmed(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}
}  // namespace

void read_quality(const std::string& text, Quality& into, std::vector<std::string>* problems) {
    std::istringstream in(text);
    std::string raw;
    int n = 0;
    const auto say = [&](const std::string& why) {
        if (problems) problems->push_back("line " + std::to_string(n) + ": " + why);
    };
    while (std::getline(in, raw)) {
        ++n;
        const std::string line = trimmed(raw.substr(0, raw.find('#')));
        if (line.empty()) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            say("not key = value: " + line);
            continue;
        }
        const std::string key = trimmed(line.substr(0, eq)), value = trimmed(line.substr(eq + 1));
        const Setting* s = nullptr;
        for (const Setting& k : kSettings)
            if (key == k.key) s = &k;
        if (!s) {
            say("no setting " + key);
            continue;
        }
        char* end = nullptr;
        if (s->flag) {
            if (value == "true" || value == "1" || value == "on" || value == "yes") into.*(s->flag) = true;
            else if (value == "false" || value == "0" || value == "off" || value == "no") into.*(s->flag) = false;
            else say(key + " is true or false, not " + value);
        } else if (s->whole && word_of(*s, value) >= 0) {
            into.*(s->whole) = word_of(*s, value);
        } else if (s->whole) {
            const long v = std::strtol(value.c_str(), &end, 10);
            if (value.empty() || *end != '\0') {
                std::string said = key + " is a whole number, not " + value;
                if (!s->words.empty()) {
                    said = key + " is one of";
                    for (const char* w : s->words) said += std::string(" ") + w;
                    said += ", not " + value;
                }
                say(said);
            } else into.*(s->whole) = static_cast<int>(v);
        } else {
            const float v = std::strtof(value.c_str(), &end);
            if (value.empty() || *end != '\0') say(key + " is a number, not " + value);
            else into.*(s->number) = v;
        }
    }
}

void quality_to_params(const Quality& q, Params& out) {
    for (const Setting& s : kSettings) {
        const double v = s.flag ? (q.*(s.flag) ? 1.0 : 0.0) : s.whole ? static_cast<double>(q.*(s.whole)) : static_cast<double>(q.*(s.number));
        out.set(Key{s.key}, v);
    }
}

Quality quality_from_params(const Params& p, Quality base) {
    for (const Setting& s : kSettings) {
        const Key k{s.key};
        if (!p.has(k)) continue;
        // A setting said as a word ("true", "4") is read as render.conf reads it.
        const Value& said = p.get(k);
        double v = p.num(k);
        if (const bool* b = std::get_if<bool>(&said)) v = *b ? 1.0 : 0.0;
        else if (const std::string* t = std::get_if<std::string>(&said)) {
            const std::string w = trimmed(*t);
            if (w == "true" || w == "on" || w == "yes") v = 1.0;
            else if (w == "false" || w == "off" || w == "no") v = 0.0;
            else if (word_of(s, w) >= 0) v = word_of(s, w);
            else {
                char* end = nullptr;
                v = std::strtod(w.c_str(), &end);
                if (w.empty() || *end) continue;
            }
        }
        if (s.flag) base.*(s.flag) = v > 0.5;
        else if (s.whole) base.*(s.whole) = static_cast<int>(std::lround(v));
        else base.*(s.number) = static_cast<float>(v);
    }
    return base;
}

Quality kept_to_bounds(Quality q) {
    int size = 256;
    while (size < q.shadow_size && size < 8192) size *= 2;
    q.shadow_size = size;
    q.shadow_budget_mb = std::max(q.shadow_budget_mb, -1);
    q.msaa = q.msaa >= 8 ? 8 : q.msaa >= 4 ? 4 : q.msaa >= 2 ? 2 : 0;
    q.bloom_passes = std::clamp(q.bloom_passes, 0, 8);
    q.bloom_strength = std::clamp(q.bloom_strength, 0.0f, 4.0f);
    q.bloom_threshold = std::clamp(q.bloom_threshold, 0.0f, 16.0f);
    q.exposure = std::clamp(q.exposure, 0.05f, 16.0f);
    q.reflection_scale = std::clamp(q.reflection_scale, 0.1f, 1.0f);
    q.view_scale = std::clamp(q.view_scale, 0.4f, 1.0f);
    q.pixel = std::clamp(q.pixel, 1, 8);
    q.upscaler = std::clamp(q.upscaler, 0, 2);
    q.dlss_mode = std::clamp(q.dlss_mode, 0, 4);
    return q;
}

float dlss_scale(int mode) {
    static const float kScale[] = {2.0f / 3.0f, 0.58f, 0.5f, 1.0f / 3.0f, 1.0f};
    return kScale[std::clamp(mode, 0, 4)];
}

Quality with_machine(Quality q) {
    const char* said = std::getenv("SG_QUALITY");
    if (!said || !*said) return q;
    std::string text = said;
    std::replace(text.begin(), text.end(), ';', '\n');
    std::vector<std::string> problems;
    read_quality(text, q, &problems);
    // (Said once a program: it is the machine's word, not a world's.)
    static bool told = false;
    if (!told) {
        told = true;
        for (const std::string& p : problems) std::fprintf(stderr, "SG_QUALITY: %s\n", p.c_str());
    }
    return q;
}

bool load_quality(const std::string& file, const std::string& program, Quality& into, std::vector<std::string>* problems) {
    std::error_code ec;
    const std::filesystem::path path =
        !file.empty() ? std::filesystem::path(file) : std::filesystem::absolute(std::filesystem::path(program), ec).parent_path() / "render.conf";
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream text;
    text << f.rdbuf();
    read_quality(text.str(), into, problems);
    return true;
}

std::string quality_text(const Quality& q) {
    std::ostringstream out;
    out << "# How this project is drawn. Every line is optional: a setting left out\n"
           "# keeps the engine's default. `key = value`; `#` starts a remark.\n\n";
    for (const Setting& s : kSettings) {
        std::string value = s.flag ? (q.*(s.flag) ? "true" : "false") : s.whole ? std::to_string(q.*(s.whole)) : std::to_string(q.*(s.number));
        if (s.whole && q.*(s.whole) >= 0 && static_cast<std::size_t>(q.*(s.whole)) < s.words.size()) value = s.words[static_cast<std::size_t>(q.*(s.whole))];
        if (s.number) {
            while (value.size() > 1 && value.back() == '0') value.pop_back();
            if (value.back() == '.') value.pop_back();
        }
        out << "# " << s.what << "\n" << s.key << " = " << value << "\n\n";
    }
    return out.str();
}

void standard_look(LookState &l, const Quality &q) {
    l.uniform(passes::scene, "uFogColor", 0.05, 0.06, 0.09)
        .uniform(passes::scene, "uFogDensity", 0.018)
        .uniform(passes::scene, "uFogStart", 0.0)
        .uniform(passes::scene, "uFogFull", 0.0)
        .uniform(passes::scene, "uFogFalloff", 0.0)
        .uniform(passes::scene, "uFogFloor", 0.0)
        .uniform(passes::scene, "uFogSunG", 0.0)
        .uniform(passes::scene, "uFogSunAmount", 0.0)
        .uniform(passes::scene, "uSky", 0.10, 0.13, 0.20)
        .uniform(passes::scene, "uGround", 0.14, 0.10, 0.07)
        .uniform(passes::scene, "uAmbient", 0.55)
        .uniform(passes::scene, "uShadowSoft", 1.0)
        .uniform(passes::scene, "uShadowFloor", 0.0)
        .uniform(passes::scene, "uWind", 0.0)
        .uniform(passes::scene, "uStars", 0.0)
        .uniform(passes::scene, "uClouds", 0.0)
        .uniform(passes::scene, "uCloudColor", 1.0, 0.95, 0.92)
        .uniform(passes::scene, "uCloudShade", 0.55, 0.52, 0.62)
        .uniform(passes::scene, "uSkyTop", 0.20, 0.40, 0.75)
        .uniform(passes::scene, "uSkyHorizon", 0.72, 0.78, 0.84)
        .setting(passes::scene, "clear.x", 0.012)
        .setting(passes::scene, "clear.y", 0.014)
        .setting(passes::scene, "clear.z", 0.022)
        .uniform(passes::bright, "uThreshold", q.bloom_threshold)
        .setting(passes::blur, "passes", q.bloom_passes)
        .uniform(passes::composite, "uBloomStrength", q.bloom_strength)
        .uniform(passes::composite, "uExposure", q.exposure)
        .uniform(passes::composite, "uTint", 1.0, 1.0, 1.0)
        .uniform(passes::composite, "uSaturation", 1.0)
        .uniform(passes::composite, "uVignette", 0.55)
        .uniform(passes::composite, "uGrain", 0.015)
        .uniform(passes::composite, "uRays", 0.0)
        .uniform(passes::composite, "uRayCut", 1.0)
        .uniform(passes::composite, "uRaySpread", 0.3)
        .uniform(passes::composite, "uRayColor", 1.0, 1.0, 1.0)
        .uniform(passes::composite, "uRayDir", 0.0, 1.0, 0.0)
        .uniform(passes::composite, "uCamFwd", 0.0, 0.0, -1.0)
        .uniform(passes::composite, "uTanHalf", 0.7);
}

} // namespace sg::render
