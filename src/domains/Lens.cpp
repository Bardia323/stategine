// A lens: the glass a camera sees through (sg/domains/Lens.hpp).
#include "sg/domains/Lens.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace sg {

namespace {

// The engine's own, by what they are. A cheap camcorder's wide zoom (P.T.'s:
// bowed, fringed, dark and soft at its corners, smeared at its edges); a
// good wide prime (a little bow); the normal lens; a portrait lens; a long
// one (nothing bowed: its lines stay straight, its corners barely darker).
const std::map<std::string, std::string>& built_in() {
    static const std::map<std::string, std::string> s = {
        {"camcorder", R"(# A handheld camcorder's wide zoom, as P.T. was seen through.
focal = 18
barrel = 0.14
fringe = 2.2
vignette = 0.95
corner_soft = 0.8
edge_smear = 6
)"},
        {"wide24", R"(# A good 24 mm prime: a little bow at its corners.
focal = 24
barrel = 0.05
fringe = 0.6
vignette = 0.4
)"},
        {"normal50", R"(# The normal lens: as the eye sees, nearly nothing of its own.
focal = 50
barrel = 0.01
fringe = 0.2
vignette = 0.25
)"},
        {"portrait85", R"(# An 85 mm portrait lens, wide open: a face sharp, the room behind it soft.
focal = 85
fstop = 1.8
fringe = 0.1
vignette = 0.2
)"},
        {"tele135", R"(# A 135 mm telephoto: lines straight, the far flattened onto the near.
focal = 135
fstop = 2.8
fringe = 0.1
vignette = 0.15
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

}  // namespace

const std::vector<Key>& lens_keys() {
    static const std::vector<Key> k = {Key{"lens"}, Key{"focal"}, Key{"fstop"}, Key{"barrel"}, Key{"fringe"},
                                       Key{"vignette"}, Key{"corner_soft"}, Key{"edge_smear"}};
    return k;
}

std::vector<std::string> lens_names() {
    std::vector<std::string> out;
    for (const auto& [k, v] : built_in()) out.push_back(k);
    return out;
}

std::string lens_text(const std::string& name, const std::string& program, std::vector<std::string>* problems) {
    if (const auto it = built_in().find(name); it != built_in().end()) return it->second;
    std::vector<std::filesystem::path> where;
    if (const char* dirs = std::getenv("SG_LENSES"))
        for (std::stringstream ss(dirs); ss.good();) {
            std::string d;
            std::getline(ss, d, ':');
            if (!d.empty()) where.push_back(std::filesystem::path(d) / (name + ".lens"));
        }
    if (!program.empty()) {
        std::error_code ec;
        where.push_back(std::filesystem::absolute(std::filesystem::path(program), ec).parent_path() / "lenses" / (name + ".lens"));
    }
    where.push_back(std::filesystem::path(name));
    for (const auto& p : where)
        if (std::string t = read_file(p); !t.empty()) return t;
    if (problems) problems->push_back("no lens " + name);
    return {};
}

std::optional<LensKind> lens_named(const std::string& name, const std::string& program, std::vector<std::string>* problems) {
    const bool is_text = name.find('\n') != std::string::npos;
    const std::string text = is_text ? name : lens_text(name, program, problems);
    if (text.empty()) return std::nullopt;
    LensKind k;
    k.name = is_text ? std::string{"lens"} : std::filesystem::path(name).stem().string();
    const std::map<std::string, double*> fields = {{"focal", &k.focal},       {"fstop", &k.fstop},
                                                   {"barrel", &k.barrel},     {"fringe", &k.fringe},
                                                   {"vignette", &k.vignette}, {"corner_soft", &k.corner_soft},
                                                   {"edge_smear", &k.edge_smear}};
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::istringstream ks(line.substr(0, eq)), vs(line.substr(eq + 1));
        std::string key;
        double v = 0.0;
        ks >> key;
        const auto f = fields.find(key);
        if (f == fields.end() || !(vs >> v)) {
            if (problems) problems->push_back("lens " + k.name + " line " + std::to_string(n) + ": not one of its keys = a number");
            continue;
        }
        *f->second = v;
    }
    if (k.focal <= 0.0) {
        if (problems) problems->push_back("lens " + k.name + ": its focal length is not above 0");
        return std::nullopt;
    }
    return k;
}

double lens_fov(double focal_mm, double film_height_mm) {
    return 2.0 * std::atan(0.5 * film_height_mm / std::max(focal_mm, 1e-3)) * 180.0 / 3.14159265358979323846;
}

Event mount_event(Key event, const LensKind& lens, double film_height_mm) {
    Event ev{event};
    ev.args.set(Key{"lens"}, lens.name);
    ev.args.set(keys::fov, lens_fov(lens.focal, film_height_mm));
    ev.args.set(Key{"focal"}, lens.focal).set(Key{"fstop"}, lens.fstop);
    ev.args.set(Key{"barrel"}, lens.barrel).set(Key{"fringe"}, lens.fringe).set(Key{"vignette"}, lens.vignette);
    ev.args.set(Key{"corner_soft"}, lens.corner_soft).set(Key{"edge_smear"}, lens.edge_smear);
    return ev;
}

void mount(Element& camera, const Event& ev) {
    const std::string* name = ev.args.text(Key{"lens"});
    if (!name) return;
    camera.params.set(Key{"lens"}, *name);
    if (ev.args.has(keys::fov)) camera.params.set(keys::fov, std::clamp(ev.args.num(keys::fov), 1.0, 150.0));
    for (const Key& k : lens_keys()) {
        if (k == Key{"lens"}) continue;
        // (A lens that says no aperture is all sharp: the last one's is taken off.)
        if (ev.args.has(k) && !(k == Key{"fstop"} && ev.args.num(k) <= 0.0)) camera.params.set(k, ev.args.num(k));
        else camera.params.erase(k);
    }
}

}  // namespace sg
