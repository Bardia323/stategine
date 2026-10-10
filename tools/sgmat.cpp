// sgmat: a material seen the same way every time - on a ball, a bevelled
// cube and a leaning slab, beside an 18% grey ball, a chrome ball and six
// patches of known colour, in a neutral studio, by the engine's own renderer,
// to a PNG. For whoever writes a material to see it as it will be seen, and
// to compare one try with the next: the stage never moves.
//
//   sgmat rust.mat                         rust.studio.png
//   sgmat rust.mat --preset all            rust.studio/lamp/soft.png and rust.sheet.png
//   sgmat --set r=0.6 --set roughness=0.3  a material said on the line (sgmat.studio.png)
//   sgmat rust.mat -e "roughness=0.8; texture.relief=0.004"   the file, changed for one look
//   sgmat rust.mat -o out.png -w 1920 -h 1080 --frames 30
//   sgmat rust.mat --view ball             close on one specimen (ball, cube, slab)
//
// A material file is `key = value`, one a line, `#` a remark (docs/materials.md
// is the contract). A key is set on each specimen as it is said - `r g b
// roughness surface emissive mirror glass` and whatever else a thing reads;
// a key `texture.<k>` is set on the map of a Texture the specimens wear
// (`texture.generator`, `texture.seed`, `texture.tile`, `texture.relief`,
// `texture.layer`, `texture.surface_layer` and `texture.normal_layer` -
// PNG, JPEG, TGA, BMP or PPM files beside it (`texture.per_cell = 1` for one
// tile of a material in every cell);
// `texture.srgb = 0` when its colours are linear values, not colours as seen
// on a screen, which a texture's are unless it says).
//
// The stage is tools/sgmat.sg (and sgmat_worn.sg, the specimens wearing a
// texture); this program only sets what the material says on the specimens
// and picks a preset before the first frame, then looks. After the picture it
// prints what the eye measures: each specimen's colour on screen beside the
// grey ball's. It exits 2 when it printed a problem (`! ...`).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "sg/domains/Modeler.hpp"
#include "sg/domains/Texture.hpp"
#include "sg/pictures/Pictures.hpp"
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#include "sg/render/ViewPlan.hpp"
#include "sg/sg.hpp"

namespace sgen {
void build_sgmat(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
void build_sgmat_worn(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
}  // namespace sgen

namespace fs = std::filesystem;

namespace {

const char* const kSpecimens[] = {"ball", "cube", "slab"};
const char* const kMeasured[] = {"ball", "cube", "slab", "grey", "chrome", "white", "mid", "black", "red", "green", "blue"};
const char* const kPresets[] = {"studio", "lamp", "soft"};

// What a material says: what is set on the specimens, and on their texture's map.
struct Material {
    std::vector<std::pair<std::string, sg::Value>> things, map;
    int srgb = -1;  // -1: as a texture is (sRGB)
    bool worn() const { return !map.empty() || srgb >= 0; }
};

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// A number if it reads as one whole, else text (quotes taken off).
sg::Value value_of(const std::string& raw) {
    const std::string v = trim(raw);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (!v.empty() && end && *end == '\0') return d;
    return v;
}

// One `key = value`; false (and why) when it is not one.
bool say(Material& m, const std::string& line, const fs::path& dir, std::string& why) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) return why = "no '='", false;
    const std::string key = trim(line.substr(0, eq));
    if (key.empty()) return why = "no key", false;
    sg::Value v = value_of(line.substr(eq + 1));
    if (key.rfind("texture.", 0) == 0) {
        const std::string k = key.substr(8);
        if (k == "srgb") {
            m.srgb = std::holds_alternative<double>(v) && std::get<double>(v) != 0.0 ? 1 : 0;
            return true;
        }
        if (k.size() >= 5 && k.compare(k.size() - 5, 5, "layer") == 0 && std::holds_alternative<std::string>(v)) {
            const fs::path p = std::get<std::string>(v);
            v = (p.is_absolute() ? p : dir / p).string();
        }
        m.map.emplace_back(k, std::move(v));
    } else {
        m.things.emplace_back(key, std::move(v));
    }
    return true;
}

bool read(Material& m, const std::string& text, const fs::path& dir, const std::string& from) {
    bool ok = true;
    std::size_t n = 0, at = 0;
    while (at <= text.size()) {
        const auto nl = text.find('\n', at);
        std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++n;
        bool quoted = false;
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"') quoted = !quoted;
            if (line[i] == '#' && !quoted) {
                line.resize(i);
                break;
            }
        }
        if (trim(line).empty()) continue;
        std::string why;
        if (!say(m, line, dir, why)) std::fprintf(stderr, "%s:%zu: %s: %s\n", from.c_str(), n, why.c_str(), trim(line).c_str()), ok = false;
    }
    return ok;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Shot {
    std::vector<unsigned char> rgb;  // rows top first
    std::vector<std::string> notes;  // what the eye measures
};

// Where a point of the stage lands in the picture (pixels, from the top
// left) and how far from the eye it is; false behind the eye.
bool project(const sg::render::ViewCamera& c, const sg::Vec3d& p, int W, int H, double& x, double& y, double& depth) {
    const sg::Vec3d f = sg::unit(c.forward), right = sg::unit(sg::cross(f, c.up)), up = sg::cross(right, f);
    const sg::Vec3d d = p - c.eye;
    depth = sg::dot(d, f);
    if (depth <= 0.01) return false;
    const double t = std::tan(c.fov * 3.14159265358979 / 360.0), aspect = static_cast<double>(W) / H;
    x = (sg::dot(d, right) / (depth * t * aspect) * 0.5 + 0.5) * W;
    y = (0.5 - sg::dot(d, up) / (depth * t) * 0.5) * H;
    return true;
}

Shot shoot(const Material& m, const std::string& preset, const std::string& look_at, int W, int H, int frames, int cell) {
    Shot shot;
    sg::StateGraph g;
    sg::dsl::Bindings bindings;
    sgen::build_sgmat(g, {}, bindings);
    auto& stage = dynamic_cast<sg::Spatial3D&>(*g.find(sg::Key{"stage"}));

    // What the material says, on each specimen - before anything is drawn.
    for (const char* id : kSpecimens)
        for (const auto& [k, v] : m.things) stage.element(sg::Key{id}).params.set(sg::Key{k}, v);
    if (m.worn()) {
        auto& swatch = g.add<sg::Texture>(sg::Key{"swatch"}, cell);
        if (m.srgb >= 0) swatch.set_srgb(m.srgb == 1);
        for (const auto& [k, v] : m.map) swatch.element(sg::Texture::map_id()).params.set(sg::Key{k}, v);
        sgen::build_sgmat_worn(g, {}, bindings);
    }
    // The preset: each light as bright as it says for it, and its look worn.
    for (sg::Element& e : stage.elements())
        if (e.kind == sg::kinds::light && e.params.has(sg::Key{preset})) e.params.set(sg::keys::intensity, e.params.num(sg::Key{preset}));
    sg::set_look(stage, sg::Key{"stage." + preset});
    // A close look at one specimen: the eye where it stands, turned to it,
    // its lens narrowed until the thing fills the picture.
    if (!look_at.empty()) {
        sg::Element& eye = stage.element(sg::Key{"camera"});
        const sg::Element& thing = stage.element(sg::Key{look_at});
        const double sy = thing.params.num(sg::keys::sy, 1), sx = thing.params.num(sg::keys::sx, 1);
        const double dx = thing.params.num(sg::keys::x) - eye.params.num(sg::keys::x),
                     dy = thing.params.num(sg::keys::y) + sy * 0.5 - eye.params.num(sg::keys::y),
                     dz = thing.params.num(sg::keys::z) - eye.params.num(sg::keys::z);
        const double flat = std::hypot(dx, dz), far = std::hypot(flat, dy);
        eye.params.set(sg::keys::yaw, std::atan2(dz, dx)).set(sg::keys::pitch, std::atan2(dy, flat));
        eye.params.set(sg::keys::fov, 2.0 * std::atan(std::max(sx, sy) * 0.62 / far) * 180.0 / 3.14159265358979);
    }

    for (const auto& p : g.validate(false)) shot.notes.push_back("! " + p);
    // A layer file that cannot be read is left out without a word by the
    // texture (the reader says no): said here, where it was asked for.
    for (const auto& [k, v] : m.map)
        if (k.size() >= 5 && k.compare(k.size() - 5, 5, "layer") == 0 && std::holds_alternative<std::string>(v) &&
            !std::get<std::string>(v).empty() && !fs::exists(std::get<std::string>(v)))
            shot.notes.push_back("! texture." + k + ": no file " + std::get<std::string>(v));
    const sg::LawReport laws = sg::verify(g);
    if (!laws.ok()) shot.notes.push_back("! laws: " + laws.str());

    sg::render::GLWorldView view;
    for (const auto& p : view.prepare(g)) shot.notes.push_back("! " + p);
    // What the graph says a thing wears, the renderer is given the pixels of:
    // a binding supplies the picture, the embedding is what allows it.
    int worn = 0;
    for (const sg::Embedding& e : g.embeddings())
        if (e.host == sg::Key{"stage"})
            if (auto* skin = dynamic_cast<sg::Surface2D*>(g.find(e.guest))) view.bind_surface(e.portal, skin), ++worn;
    if (m.worn() && worn != static_cast<int>(std::size(kSpecimens)))
        shot.notes.push_back("! the specimens wear " + std::to_string(worn) + " textures, not " + std::to_string(std::size(kSpecimens)));
    view.set_fixed_step(1.0 / 60);
    for (int f = 0; f < frames; ++f) view.render(stage, W, H);
    std::vector<unsigned char> px(static_cast<std::size_t>(W) * H * 3);
    sg::gl::glReadPixels(0, 0, W, H, sg::gl::GL_RGB, sg::gl::GL_UNSIGNED_BYTE, px.data());
    shot.rgb.resize(px.size());
    for (int y = 0; y < H; ++y)
        std::copy_n(&px[static_cast<std::size_t>(H - 1 - y) * W * 3], static_cast<std::size_t>(W) * 3, &shot.rgb[static_cast<std::size_t>(y) * W * 3]);

    // What the eye measures: the middle of each thing as it shows on screen
    // (0..255, after the look's tone curve), and its brightness beside the
    // grey ball's - 1.0 is as bright as 18% grey in the same light.
    const sg::render::ViewCamera cam = sg::render::view_camera(stage.element(sg::Key{"camera"}));
    std::vector<std::pair<std::string, std::array<double, 3>>> seen;
    for (const char* id : kMeasured) {
        const sg::Element& e = stage.element(sg::Key{id});
        const double sy = e.params.num(sg::keys::sy, 1), sx = e.params.num(sg::keys::sx, 1);
        const sg::Vec3d at{e.params.num(sg::keys::x), e.params.num(sg::keys::y) + sy * 0.5, e.params.num(sg::keys::z)};
        double x = 0, y = 0, depth = 0;
        if (!project(cam, at, W, H, x, y, depth)) continue;
        const double half = std::tan(cam.fov * 3.14159265358979 / 360.0) * depth;
        const int r = std::max(1, static_cast<int>(std::min(sx, sy) * 0.18 / half * H * 0.5));
        std::array<double, 3> sum{0, 0, 0};
        int n = 0;
        for (int yy = static_cast<int>(y) - r; yy <= static_cast<int>(y) + r; ++yy)
            for (int xx = static_cast<int>(x) - r; xx <= static_cast<int>(x) + r; ++xx) {
                if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                const unsigned char* q = &shot.rgb[(static_cast<std::size_t>(yy) * W + xx) * 3];
                sum[0] += q[0], sum[1] += q[1], sum[2] += q[2], ++n;
            }
        if (n) seen.emplace_back(id, std::array<double, 3>{sum[0] / n, sum[1] / n, sum[2] / n});
    }
    const auto lum = [](const std::array<double, 3>& c) { return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]; };
    double grey = 0;
    for (const auto& [id, c] : seen)
        if (id == "grey") grey = lum(c);
    char line[160];
    std::snprintf(line, sizeof line, "%-8s %15s  %s", preset.c_str(), "on screen", "vs grey");
    shot.notes.emplace_back(line);
    for (const auto& [id, c] : seen) {
        std::snprintf(line, sizeof line, "  %-7s %4.0f %4.0f %4.0f    %5.2f", id.c_str(), c[0], c[1], c[2], grey > 0 ? lum(c) / grey : 0.0);
        shot.notes.emplace_back(line);
    }
    return shot;
}

void write_png(const std::string& path, const std::vector<unsigned char>& rgb, int w, int h) {
    std::ofstream o(path, std::ios::binary);
    o << sg::sculpt::png(rgb, w, h);
    std::printf("wrote %s\n", path.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    Material m;
    std::string file, out, preset = "studio", look_at;
    int W = 1280, H = 720, frames = 12, cell = 256;
    bool ok = true;
    std::vector<std::string> sets;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "-o") out = next();
        else if (a == "-w") W = std::max(64, std::atoi(next().c_str()));
        else if (a == "-h") H = std::max(64, std::atoi(next().c_str()));
        else if (a == "--frames") frames = std::max(1, std::atoi(next().c_str()));
        else if (a == "--cell") cell = std::clamp(std::atoi(next().c_str()), 16, 2048);
        else if (a == "--preset") preset = next();
        else if (a == "--view") look_at = next();
        else if (a == "--set") sets.push_back(next());
        else if (a == "-e") {
            std::string e = next();
            for (std::size_t at = 0; at <= e.size();) {
                const auto semi = e.find(';', at);
                sets.push_back(e.substr(at, semi == std::string::npos ? std::string::npos : semi - at));
                at = semi == std::string::npos ? e.size() + 1 : semi + 1;
            }
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            ok = false;
        } else file = a;
    }
    if (!ok || (file.empty() && sets.empty())) {
        std::fprintf(stderr, "sgmat <material.mat> [--set key=value ...] [-e \"key=value; ...\"] [--preset studio|lamp|soft|all] "
                             "[-o out.png] [-w px -h px] [--view ball|cube|slab] [--frames n] [--cell px]\n");
        return 1;
    }
    std::string name = "sgmat";
    fs::path dir = ".";
    if (!file.empty()) {
        if (!fs::exists(file)) return std::fprintf(stderr, "no file %s\n", file.c_str()), 1;
        dir = fs::path(file).parent_path();
        name = fs::path(file).stem().string();  // pictures go where it runs, never beside the source
        ok = read(m, slurp(file), dir, file);
    }
    for (const auto& s : sets) {
        if (trim(s).empty()) continue;
        std::string why;
        if (!say(m, s, dir, why)) std::fprintf(stderr, "--set %s: %s\n", s.c_str(), why.c_str()), ok = false;
    }
    if (!ok) return 1;
    if (!look_at.empty() && std::find(std::begin(kSpecimens), std::end(kSpecimens), look_at) == std::end(kSpecimens))
        return std::fprintf(stderr, "no specimen %s (ball, cube, slab)\n", look_at.c_str()), 1;
    std::vector<std::string> presets;
    if (preset == "all") presets.assign(std::begin(kPresets), std::end(kPresets));
    else if (std::find(std::begin(kPresets), std::end(kPresets), preset) != std::end(kPresets)) presets.push_back(preset);
    else return std::fprintf(stderr, "no preset %s (studio, lamp, soft, all)\n", preset.c_str()), 1;
    sg::Texture::set_reader(sg::pictures::read);

    // Each preset's picture: `-o` itself for one; beside it, named by preset, for several.
    std::string base = name;
    if (!out.empty()) {
        std::string stem = fs::path(out).stem().string();
        if (stem.size() > 6 && stem.compare(stem.size() - 6, 6, ".sheet") == 0) stem.resize(stem.size() - 6);
        base = (fs::path(out).parent_path() / stem).string();
    }

    sg::gl::Window window(W, H, "sgmat");
    std::vector<Shot> shots;
    bool problems = false;  // any `!` line: the picture is not to be trusted
    for (const auto& p : presets) {
        shots.push_back(shoot(m, p, look_at, W, H, frames, cell));
        for (const auto& n : shots.back().notes) {
            std::printf("%s\n", n.c_str());
            problems = problems || n.rfind("! ", 0) == 0;
        }
        write_png(presets.size() == 1 && !out.empty() ? out : base + "." + p + ".png", shots.back().rgb, W, H);
    }
    if (presets.size() > 1) {
        // The presets side by side, each at half size.
        const int w = W / 2, h = H / 2, SW = w * static_cast<int>(shots.size());
        std::vector<unsigned char> sheet(static_cast<std::size_t>(SW) * h * 3);
        for (std::size_t s = 0; s < shots.size(); ++s)
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    for (int k = 0; k < 3; ++k) {
                        int sum = 0;
                        for (int dy = 0; dy < 2; ++dy)
                            for (int dx = 0; dx < 2; ++dx)
                                sum += shots[s].rgb[(static_cast<std::size_t>(y * 2 + dy) * W + x * 2 + dx) * 3 + k];
                        sheet[(static_cast<std::size_t>(y) * SW + s * w + x) * 3 + k] = static_cast<unsigned char>(sum / 4);
                    }
        write_png(out.empty() ? name + ".sheet.png" : out, sheet, SW, h);
    }
    return problems ? 2 : 0;
}
