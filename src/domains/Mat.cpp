#include "sg/domains/Mat.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "sg/domains/Texture.hpp"

namespace sg::mat {

namespace {

namespace fs = std::filesystem;

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

}  // namespace

Value value_of(const std::string& text) {
    const std::string v = trim(text);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') return v.substr(1, v.size() - 2);
    char* end = nullptr;
    const double d = std::strtod(v.c_str(), &end);
    if (!v.empty() && end && *end == '\0') return d;
    return v;
}

bool say(File& m, const std::string& line, const std::string& dir, std::string* why) {
    const auto eq = line.find('=');
    const std::string key = eq == std::string::npos ? std::string() : trim(line.substr(0, eq));
    if (key.empty()) {
        if (why) *why = eq == std::string::npos ? "no '='" : "no key";
        return false;
    }
    Value v = value_of(line.substr(eq + 1));
    if (key.rfind("texture.", 0) == 0) {
        const std::string k = key.substr(8);
        if (k == "srgb") {
            m.srgb = std::holds_alternative<double>(v) && std::get<double>(v) != 0.0 ? 1 : 0;
            return true;
        }
        if (k.size() >= 5 && k.compare(k.size() - 5, 5, "layer") == 0 && std::holds_alternative<std::string>(v)) {
            const fs::path p = std::get<std::string>(v);
            v = (p.is_absolute() || dir.empty() ? p : fs::path(dir) / p).generic_string();
        }
        m.map.emplace_back(k, std::move(v));
    } else {
        m.thing.emplace_back(key, std::move(v));
    }
    return true;
}

File read(const std::string& text, const std::string& dir, const std::string& from) {
    File m;
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
        if (!say(m, line, dir, &why)) m.errors.push_back(from + ":" + std::to_string(n) + ": " + why + ": " + trim(line));
    }
    return m;
}

File load(const std::string& path, bool* found) {
    std::ifstream in(path, std::ios::binary);
    if (found) *found = static_cast<bool>(in);
    if (!in) return {};
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return read(text, fs::path(path).parent_path().generic_string(), path);
}

Texture& texture(StateGraph& g, Key id, const File& m, int cell) {
    if (g.contains(id)) return dynamic_cast<Texture&>(g.state(id));
    auto& t = g.add<Texture>(id, cell);
    if (m.srgb >= 0) t.set_srgb(m.srgb == 1);
    for (const auto& [k, v] : m.map) t.element(Texture::map_id()).params.set(Key{k}, v);
    return t;
}

void dress(Element& thing, const File& m) {
    for (const auto& [k, v] : m.thing) thing.params.set(Key{k}, v);
}

}  // namespace sg::mat
