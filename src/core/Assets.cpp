#include "sg/core/Assets.hpp"

#include <stdexcept>

namespace sg {

namespace {

// A plain relative path: nothing in it climbs out, nothing names a drive or a root.
bool stays_inside(const std::filesystem::path& p) {
    if (p.empty() || p.has_root_name() || p.has_root_directory()) return false;
    for (const auto& part : p)
        if (part == ".." || part.empty()) return false;
    return true;
}

}  // namespace

std::filesystem::path Assets::folder(const std::string& owner) const {
    const std::filesystem::path o(owner);
    if (!stays_inside(o) || std::distance(o.begin(), o.end()) != 1)
        throw std::invalid_argument("assets: '" + owner + "' is not the name of one state");
    std::error_code ec;
    std::filesystem::create_directories(root_ / o, ec);
    return root_ / o;
}

std::filesystem::path Assets::file(const std::string& owner, const std::string& name) const {
    const std::filesystem::path n(name);
    if (!stays_inside(n)) throw std::invalid_argument("assets: '" + name + "' leaves the folder of " + owner);
    const std::filesystem::path dir = folder(owner);
    std::error_code ec;
    std::filesystem::create_directories((dir / n).parent_path(), ec);
    return dir / n;
}

std::vector<std::string> Assets::owners() const {
    std::vector<std::string> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(root_, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) out.push_back(it->path().filename().string());
    return out;
}

int Assets::adopt(const std::filesystem::path& from, const std::string& owner, const Rename& rename) const {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) files.push_back(it->path());
    int moved = 0;
    for (const auto& f : files) {
        const std::filesystem::path leaf = rename ? rename(f) : f.filename();
        if (leaf.empty() || !stays_inside(leaf)) continue;
        const std::filesystem::path dest = folder(owner) / leaf;
        if (std::filesystem::exists(dest, ec)) continue;
        std::filesystem::create_directories(dest.parent_path(), ec);
        std::filesystem::rename(f, dest, ec);
        if (!ec) ++moved;
    }
    if (!std::filesystem::equivalent(from, root_, ec)) std::filesystem::remove(from, ec);  // only if empty
    return moved;
}

}  // namespace sg
