// A recipe read and written as placements: where each shape said at the top of
// it stands, how it is turned and how big - so that a picture of it can move
// a shape and the words say so. Pure functions of the text.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

#include "sg/domains/Modeler.hpp"

namespace sg::sculpt {

namespace {
std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at <= text.size()) {
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) {
            if (at < text.size()) out.push_back(text.substr(at));
            break;
        }
        out.push_back(text.substr(at, nl - at));
        at = nl + 1;
    }
    return out;
}

std::vector<std::string> words_of(const std::string& line) {
    std::vector<std::string> t;
    std::istringstream in(line.substr(0, line.find('#')));
    for (std::string w; in >> w;) t.push_back(w);
    return t;
}

bool prefix(const std::string& w) {
    return w == "add" || w == "sub" || w == "and" || w.rfind("blend=", 0) == 0 || w.rfind("carve=", 0) == 0;
}

// Numbers written plainly, one or three of them; false for an expression.
bool numbers(const std::string& s, std::vector<double>& out) {
    out.clear();
    std::size_t at = 0;
    while (at <= s.size()) {
        const std::size_t comma = s.find(',', at);
        const std::string piece = s.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        char* end = nullptr;
        const double v = std::strtod(piece.c_str(), &end);
        if (piece.empty() || end != piece.c_str() + piece.size()) return false;
        out.push_back(v);
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return out.size() == 1 || out.size() == 3;
}

std::string num(double v, double step) {
    const double q = std::round(v / step) * step;
    char b[48];
    std::snprintf(b, sizeof b, "%.4f", std::fabs(q) < step * 0.5 ? 0.0 : q);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

bool same(const Vec3d& a, const Vec3d& b, double step) {
    return std::fabs(a.x - b.x) < step * 0.5 && std::fabs(a.y - b.y) < step * 0.5 && std::fabs(a.z - b.z) < step * 0.5;
}

constexpr double kMetre = 0.001, kDegree = 0.1, kScale = 0.001;

// One line read: its words, and where it stands if it is a placement.
Placed read(const std::string& line, int index, int depth) {
    Placed p;
    const auto w = words_of(line);
    if (w.empty() || depth > 0) return p;
    std::size_t h = prefix(w[0]) ? 1 : 0;
    if (h >= w.size()) return p;
    static const std::set<std::string> not_placed{"define", "for", "group", "array", "radial", "mirror", "end", "let", "set", "remesh", "if", "else", "use"};
    if (not_placed.count(w[h])) return p;
    p.line = index;
    p.word = w[h];
    p.editable = true;
    std::vector<double> n;
    std::set<std::string> said;
    for (std::size_t i = h + 1; i < w.size(); ++i) {
        const std::size_t eq = w[i].find('=');
        if (eq == std::string::npos) continue;
        const std::string k = w[i].substr(0, eq), v = w[i].substr(eq + 1);
        if (k != "at" && k != "rot" && k != "scale") continue;
        // Said again (turns composed, or a line refused): read, not written back.
        if (!said.insert(k).second) p.editable = false;
        if (!numbers(v, n)) {
            p.editable = false;
            continue;
        }
        if (k == "at") p.at = n.size() == 3 ? Vec3d{n[0], n[1], n[2]} : Vec3d{n[0], 0, 0};
        if (k == "rot") p.rot = n.size() == 3 ? Vec3d{n[0], n[1], n[2]} : Vec3d{0, n[0], 0};
        if (k == "scale") p.scale = n.size() == 3 ? Vec3d{n[0], n[1], n[2]} : Vec3d{n[0], n[0], n[0]};
    }
    return p;
}

int depth_after(const std::string& line, int depth) {
    const auto w = words_of(line);
    if (w.empty()) return depth;
    const std::size_t h = prefix(w[0]) && w.size() > 1 ? 1 : 0;
    static const std::set<std::string> opens{"define", "for", "group", "array", "radial", "mirror", "if"};
    if (opens.count(w[h])) return depth + 1;
    if (w[h] == "end") return std::max(0, depth - 1);
    return depth;
}
}  // namespace

std::vector<Placed> placements(const std::string& recipe) {
    std::vector<Placed> out;
    int depth = 0, i = 0;
    for (const std::string& line : lines_of(recipe)) {
        const Placed p = read(line, i, depth);
        if (p.line >= 0) out.push_back(p);
        depth = depth_after(line, depth);
        ++i;
    }
    return out;
}

Placed placement(const std::string& recipe, int line) {
    for (const Placed& p : placements(recipe))
        if (p.line == line) return p;
    return {};
}

std::string place(const std::string& recipe, int line, const Vec3d& at, const Vec3d& rot, const Vec3d& scale) {
    const Placed was = placement(recipe, line);
    if (!was.editable) return recipe;
    if (same(was.at, at, kMetre) && same(was.rot, rot, kDegree) && same(was.scale, scale, kScale)) return recipe;
    auto lines = lines_of(recipe);
    std::string& l = lines[static_cast<std::size_t>(line)];
    const std::size_t hash = l.find('#');
    const std::string comment = hash == std::string::npos ? std::string() : l.substr(hash);
    std::string out;
    for (const std::string& w : words_of(l)) {
        const std::size_t eq = w.find('=');
        const std::string k = eq == std::string::npos ? std::string() : w.substr(0, eq);
        if (k == "at" || k == "rot" || k == "scale") continue;
        out += (out.empty() ? "" : " ") + w;
    }
    out += " at=" + num(at.x, kMetre) + "," + num(at.y, kMetre) + "," + num(at.z, kMetre);
    if (!same(rot, {}, kDegree))
        out += std::fabs(rot.x) < kDegree * 0.5 && std::fabs(rot.z) < kDegree * 0.5 ? " rot=" + num(rot.y, kDegree)
                                                                                 : " rot=" + num(rot.x, kDegree) + "," + num(rot.y, kDegree) + "," + num(rot.z, kDegree);
    if (!same(scale, {1, 1, 1}, kScale))
        out += std::fabs(scale.x - scale.y) < kScale * 0.5 && std::fabs(scale.x - scale.z) < kScale * 0.5
                   ? " scale=" + num(scale.x, kScale)
                   : " scale=" + num(scale.x, kScale) + "," + num(scale.y, kScale) + "," + num(scale.z, kScale);
    if (!comment.empty()) out += "  " + comment;
    l = out;
    std::string text;
    for (const std::string& s : lines) text += s + "\n";
    if (!recipe.empty() && recipe.back() != '\n') text.pop_back();
    return text;
}

}  // namespace sg::sculpt
