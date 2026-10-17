// Stategine - a material as written: a `.mat` file (docs/materials.md), the
// sketch `sgmat` looks at, read as one thing for every program - so what was
// tuned there is what is worn in a game.
//
// A line is `key = value` (a number if it reads as one whole, else words;
// quotes kept off); `#` to the end of a line is a remark, unless quoted.
// `texture.<k>` is said of the map of a Texture the thing wears
// (`texture.srgb` of the texture itself); a `texture.*layer` file is found
// beside the `.mat`. Every other key is said of the thing that wears it.
//
//   sg::mat::File m = sg::mat::load(assets + "/textures/rust/rust.mat");
//   sg::mat::say(m, "texture.tile = 0.8", dir);            // a line said after the file's
//   if (m.worn()) sg::mat::texture(g, Key{"mat.rust"}, m, 1024);
//   sg::mat::dress(room.element(Key{"room.pipe"}), m);    // and worn as the room wears textures
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "sg/core/StateGraph.hpp"

namespace sg {

class Texture;

namespace mat {

struct File {
    std::vector<std::pair<std::string, Value>> thing, map;
    int srgb = -1;  // -1: as a texture is (sRGB)
    std::vector<std::string> errors;  // "<from>:<line>: why: what"
    bool worn() const { return !map.empty() || srgb >= 0; }
};

// A value as a `.mat` says one.
Value value_of(const std::string& text);
// One `key = value` said of `m` (a layer's file found in `dir`); false, and
// why, when it is not one.
bool say(File& m, const std::string& line, const std::string& dir, std::string* why = nullptr);
// Every line of `text`, as from `from` (for its errors).
File read(const std::string& text, const std::string& dir, const std::string& from = {});
// The file at `path` (its layers beside it); `found` false if there is none.
File load(const std::string& path, bool* found = nullptr);

// A Texture `id` in `g` of the material's map, `cell` pixels a cell - made if
// it is not there yet (one texture, worn by everything made of it).
Texture& texture(StateGraph& g, Key id, const File& m, int cell = 1024);
// The thing's params as the material says them.
void dress(Element& thing, const File& m);

}  // namespace mat
}  // namespace sg
