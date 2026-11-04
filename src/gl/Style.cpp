// A style laid under a look (sg/gl/Style.hpp).
#include "sg/gl/Style.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "sg/core/State.hpp"
#include "sg/gl/Shaders.hpp"

namespace sg::gl {

namespace {

// The engine's own. `photoreal`: the settings docs/workflows.md section 10
// found by measuring a photographic interior against its references - a
// film's curve, the light from all round in probes and maps (nothing
// traced: steady wherever one looks), the detail TAA takes given back, no
// grain (its glass, a wide lens's bow and fringe, the eye's lens's: wide24). `psx`: a
// console of 1997's - few pixels, few colours, dithered, nothing soft.
// `video`: a handheld digital video camera in a dark house - the sensor's
// noise (its wide cheap lens the eye's: camcorder). `hi8`: a 1990s analog camcorder's
// tape - rich colour bled along its lines, soft, haloed, its noise streaked (the
// same lens). `vhs`: a VHS deck's tape - softer and noisier than Hi8's, its
// colour smeared further, its lines wandering, its bottom torn. `vhs_crt`:
// that tape played on a tube television - its picture bowed by the glass, the
// glass's phosphor stripes and scanlines over it, glowing. `standard`:
// nothing - the engine's defaults.
const std::map<std::string, std::string>& built_in() {
    static const std::map<std::string, std::string> s = {
        {"standard", "# the engine's defaults: nothing said\n"},
        {"photoreal", R"(# A photograph (docs/workflows.md, A photographic interior, section 10).
composite.uTonemap = 1
composite.uSharpen = 0.6
# (Its glass - a little bow and fringe - is the eye's lens's: wide24.)
composite.uGrain = 0
composite.uBloomStrength = 0.5
composite.uVignette = 0.42
composite.uGradeContrast = 0
scene.trace = 0
scene.trace.lamps = 0
scene.trace.gi = 0
scene.scatter.cell = 8
place.probe_cell = 1.3
)"},
        {"video", R"(# A handheld digital video camera in a dark house (P.T.'s look): its
# sensor's noise - blotchy, coloured, strongest in the shadows - not film's
# grain; a wide cheap lens, bowed and fringed at its edges, dark at its
# corners; bulbs blooming wide; nothing sharpened (video is soft).
composite.uSensor = 0.06 0.45 1.5
composite.uGrain = 0
# (Video clips its highlights where film rolls them off: the harder curve.)
composite.uTonemap = 0
composite.uSharpen = 0
# (Its glass - bowed, fringed, soft and smeared at its edges - is the eye's
# lens's: camcorder.)
composite.uVignette = 0.95
composite.uSharpenHalo = 0.6
composite.uChromaHalf = 1
# (A sensor's darks lose their colour first.)
composite.uDarkChroma = 0.6
composite.uReadout = 0.018
composite.uBloomStrength = 0.9
composite.uGradeContrast = 0.15
scene.trace = 0
scene.trace.lamps = 0
scene.trace.gi = 0
scene.scatter.cell = 8
# (Its exposure finds itself, slowly, a stop either way - walked toward a lamp
# the room behind it goes dark; into the dark, it opens a little and the noise
# comes up with it.)
scene.exposure.auto = 1
scene.exposure.key = 0.05
scene.exposure.min = -1
scene.exposure.max = 1
scene.exposure.rate = 0.7
place.probe_cell = 1.3
)"},
        {"hi8", R"(# A Hi8 camcorder, the 1990s' home video: analog tape - its colour
# rich and warm but carried in a narrow band, so it smears along each line
# and lags to the right of what it colours; its picture soft along the
# lines (some 400 of them across), its edges haloed by the camera's
# sharpening; the tape's noise streaked along its lines, each line a little
# early or late, the bottom rows torn where the heads change over; the
# CCD's own noise faint under it, its lights blooming - and every room's
# colour as its light gives it, rich, never drained.
composite.uTapeBleed = 1.2 7 2.5
# (Its lines kept faint: stronger, the picture read as scan lines over it.)
composite.uTapeRun = 0.12 0.007 5
# (And the tape's own noise under it, line by line: faint and fine in its
# brightness, long faint smears in its colour - a little more in the darks.)
composite.uTapeNoise = 0.045 0.07 0.5
composite.uGradeSaturation = 0.5
composite.uGradeShadows = 0.02 0.015 0.025
composite.uSensor = 0.01 0.3 2
composite.uGrain = 0
# (Its colours carried to white on film's gentler curve: the light's tint kept.)
composite.uTonemap = 1
composite.uSharpen = 0
composite.uSharpenHalo = 0.9
composite.uVignette = 0.6
composite.uBloomStrength = 1.0
composite.uHalation = 0.18
composite.uGradeContrast = 0.1
scene.trace = 0
scene.trace.lamps = 0
scene.trace.gi = 0
scene.scatter.cell = 8
place.probe_cell = 1.3
)"},
        {"vhs", R"(# A VHS tape, a generation or two from the camera (the tape the user
# compared against Hi8, 2026-11-03): the same signal as hi8's, worse -
# its brightness carried in about 240 lines across where Hi8 has 400 - softer;
# its colour narrower still - smeared further, later; each line wandering more,
# the bottom rows torn further where the heads change over; its noise and
# streaks stronger, faded colour more blotched; its camera's sharpening ringing.
composite.uTapeBleed = 2.2 9 3
composite.uTapeRun = 0.45 0.025 12
composite.uTapeNoise = 0.075 0.11 0.6
composite.uGradeSaturation = 0.35
composite.uGradeShadows = 0.02 0.015 0.025
composite.uSensor = 0.01 0.3 2
composite.uGrain = 0
composite.uTonemap = 1
composite.uSharpen = 0
composite.uSharpenHalo = 1.0
composite.uBloomStrength = 1.0
composite.uHalation = 0.18
composite.uGradeContrast = 0.1
scene.trace = 0
scene.trace.lamps = 0
scene.trace.gi = 0
scene.scatter.cell = 8
place.probe_cell = 1.3
composite.uVignette = 0.6
)"},
        {"vhs_crt", R"(# The VHS tape (vhs) played on a tube television and filmed off it: the
# picture bowed by the tube's curved face (the frame left square, the glass
# over it straight), the glass's aperture grille - columns of red, green,
# blue - and the dark between its lines, some 270 of them on a 1080-row
# screen; its phosphors glowing round what is bright; its corners dark.
# its brightness carried in about 240 lines across where Hi8 has 400 - softer;
# its colour narrower still - smeared further, later; each line wandering more,
# the bottom rows torn further where the heads change over; its noise and
# streaks stronger, faded colour more blotched; its camera's sharpening ringing.
composite.uTapeBleed = 2.2 9 3
composite.uTapeRun = 0.45 0.025 12
composite.uTapeNoise = 0.075 0.11 0.6
composite.uGradeSaturation = 0.35
composite.uGradeShadows = 0.02 0.015 0.025
composite.uSensor = 0.01 0.3 2
composite.uGrain = 0
composite.uTonemap = 1
composite.uSharpen = 0
composite.uSharpenHalo = 1.0
composite.uBloomStrength = 1.0
composite.uHalation = 0.18
composite.uGradeContrast = 0.1
scene.trace = 0
scene.trace.lamps = 0
scene.trace.gi = 0
scene.scatter.cell = 8
place.probe_cell = 1.3
composite.uTube = 0.12
composite.uCrt = 0.55 0.9 4
composite.uCrtGlow = 0.35
composite.uVignette = 0.8
)"},
        {"psx", R"(# A console of 1997: 240 rows, 32 steps a channel, ordered dither, hard edges.
composite.shader = pixel
composite.uPixels = 240
composite.uLevels = 32
composite.uDither = 1
composite.uContrast = 1.05
composite.uBloomStrength = 0.15
composite.ao = 0
scene.trace = 0
scene.trace.lamps = 0
scene.trace.gi = 0
scene.uShadowSoft = 0.3
)"},
    };
    return s;
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) return {};
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool look_has(const LookState& l, Key pass, const std::string& key) {
    const Element* e = l.find(pass);
    return e && (e->params.has(Key{key}) || e->params.has(Key{key + ".x"}));
}

}  // namespace

std::vector<std::string> built_in_styles() {
    std::vector<std::string> out;
    for (const auto& [k, v] : built_in()) out.push_back(k);
    return out;
}

std::string style_text(const std::string& name, const std::string& program, std::vector<std::string>* problems) {
    if (const auto it = built_in().find(name); it != built_in().end()) return it->second;
    std::vector<std::filesystem::path> where;
    if (const char* dirs = std::getenv("SG_STYLES"))
        for (std::stringstream ss(dirs); ss.good();) {
            std::string d;
            std::getline(ss, d, ':');
            if (!d.empty()) where.push_back(std::filesystem::path(d) / (name + ".style"));
        }
    if (!program.empty()) {
        std::error_code ec;
        where.push_back(std::filesystem::absolute(std::filesystem::path(program), ec).parent_path() / "styles" / (name + ".style"));
    }
    where.push_back(std::filesystem::path(name));
    for (const auto& p : where)
        if (std::string t = read_file(p); !t.empty()) return t;
    if (problems) problems->push_back("no style " + name);
    return {};
}

bool apply_style(LookState& look, State* world, const std::string& style, const std::string& program, std::vector<std::string>* problems) {
    const std::string text = style.find('\n') != std::string::npos ? style : style_text(style, program, problems);
    if (text.empty()) return false;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto trim = [](std::string s) {
            const auto a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string{} : s.substr(a, b - a + 1);
        };
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        const auto dot = key.find('.');
        if (dot == std::string::npos || value.empty()) {
            if (problems) problems->push_back("line " + std::to_string(n) + ": not pass.key = value");
            continue;
        }
        const std::string pass = key.substr(0, dot), name = key.substr(dot + 1);
        if (pass == "place") {
            if (world && !world->params().has(Key{name})) world->params().set(Key{name}, std::atof(value.c_str()));
            continue;
        }
        const Key p{pass};
        if (name == "shader") {
            if (look_has(look, p, "fs")) continue;
            if (value == "pixel" && p == passes::composite) look.shader(p, pixel_composite_fs());
            else if (value != "standard" && problems) problems->push_back("line " + std::to_string(n) + ": no shader " + value + " for " + pass);
            continue;
        }
        if (look_has(look, p, name)) continue;
        std::istringstream nums(value);
        double v[3] = {0, 0, 0};
        int count = 0;
        while (count < 3 && nums >> v[count]) ++count;
        if (count == 0) {
            if (problems) problems->push_back("line " + std::to_string(n) + ": " + value + " is not a number");
            continue;
        }
        if (is_uniform_key(Key{name})) {
            if (count == 3) look.uniform(p, name, v[0], v[1], v[2]);
            else look.uniform(p, name, v[0]);
        } else {
            look.setting(p, name, v[0]);
        }
    }
    return true;
}

}  // namespace sg::gl
