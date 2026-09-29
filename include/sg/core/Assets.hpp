// Stategine - Assets: where a state keeps what is its own.
//
// A project's files are one folder, and in it one folder per state that owns
// files - never a file loose in the root, never two states in one folder:
//
//   <root>/<owner>/<name>
//
// A state names its folder by its own id, so what one state writes another
// cannot overwrite, and a state taken out takes its folder with it. Owner and
// name are plain relative paths: one that would climb out (`..`), or start
// from a drive or the root, is refused - a state cannot reach another's files
// by the path it is given. What a run writes (pictures, dumps) is kept the
// same way, in an Assets of its own rooted under the build (`<build>/out`).
//
//   sg::Assets assets("assets");
//   assets.folder("computer");                    // assets/computer, made
//   store.bind("paper.3", assets, "hall", "papers/paper.3.txt");
//   assets.adopt("old_assets", "computer");       // an older flat folder, moved in
#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace sg {

class Assets {
public:
    explicit Assets(std::filesystem::path root) : root_(std::move(root)) {}

    const std::filesystem::path& root() const { return root_; }

    // `owner`'s folder, made if it is not there. Throws std::invalid_argument
    // for an owner that is not one plain name.
    std::filesystem::path folder(const std::string& owner) const;

    // `name` in `owner`'s folder (its own folders made). Throws for a name
    // that would leave the folder.
    std::filesystem::path file(const std::string& owner, const std::string& name) const;

    // The owners that have a folder now.
    std::vector<std::string> owners() const;

    // Every file directly in `from` moved into `owner`'s folder, under
    // `rename(file)` - a path relative to the folder; empty leaves the file
    // where it is. Nothing is moved over a file already there. `from` goes,
    // if emptied and it is not the root. Returns how many were moved.
    using Rename = std::function<std::filesystem::path(const std::filesystem::path&)>;
    int adopt(const std::filesystem::path& from, const std::string& owner, const Rename& rename = {}) const;

private:
    std::filesystem::path root_;
};

}  // namespace sg
