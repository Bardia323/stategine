// Stategine - Texture: a picture things wear, as a state of its own.
//
// A thing is not unwrapped. It is seen from each way of each axis - from the
// front and the back, the left and the right, above and below - and each of
// those six views is a cell of one picture, an atlas three cells across and
// two down (+x, -x, +z on top; -z, +y, -y below, as `skin_uv` reads it). Every
// point of its surface takes its colour from the cell of the way it most
// faces, or from a blend of the cells of the ways it half faces (`blend`), so
// any shape - a box, a ball, a model - wears the same picture without seams to
// cut. The map is the picture: paint over it and the thing is painted.
//
// What the picture is, is this state's: one element, `map`, with
//
//   generator   by name, what paints it (`define`): noise, checks, pipes ...
//   seed, ...   whatever that generator reads
//   tint_r/g/b  over all of it
//   layer       a file painted over it (any paint program; `reader` reads it),
//               followed as it changes
//   layer_mix   how much of the layer shows
//   tile        metres of the thing one cell covers, its pattern going on
//               round it from face to face; 0, each face the whole cell
//   blend       0 a face its own cell; 1 a curve shading softly between them
//   relief      metres its paint stands at full height (its alpha): the
//               surface bent by it, so what is painted thick catches the light
//
//   map --set--> map   texture.set {key: value ...}: whichever are given
//
// Whatever wears it is embedded in: a texture embedded in a mesh is that mesh's
// skin. One texture can be worn by many things, a thing wears one; to wear
// another is to be embedded in another, and a room's variant is the same room
// in other textures.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "sg/domains/Surface.hpp"

namespace sg {

class Texture : public Surface2D {
public:
    // A colour, 0..1 each and alpha, for a point (u, v) of a cell (0..1 each,
    // v up), from the map's params. Where the texture tiles (`tile` > 0) it
    // must tile: what is at u = 1 is what is at u = 0.
    using Generator = std::function<std::array<double, 4>(const Params& map, int cell, double u, double v)>;
    // A picture file read as RGBA rows, top first - the program's to give,
    // as a device: the engine reads no image format of its own.
    using Reader = std::function<bool(const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba)>;

    static Key map_id() { return Key{"map"}; }
    static Key set_event() { return Key{"texture.set"}; }

    // A generator by name, for every texture (the built-in ones: plain,
    // noise, checks).
    static void define(const std::string& name, Generator g);
    static void set_reader(Reader r);

    Texture(Key id, int cell_px = 256);

    Key kind() const override { return Key{"texture"}; }

    const Element& map() const { return element(map_id()); }

protected:
    void paint() override;
    // Memoised on the map's params and on the layer file's stamp.
    bool stale() override;

private:
    uint64_t painted_stamp_ = 0;
    long long layer_time_ = 0;
    std::string layer_path_;
};

// The six views of a thing, cell by cell as a texture's map has them - its
// triangles (a model's, eight numbers a corner, in the unit box), seen
// straight on from each way of each axis, shaded by how they face and
// outlined - over a grid of cells: what to paint over, in place of an unwrap.
std::vector<unsigned char> projection_guide(const std::vector<float>& triangles, int cell_px);

// A box, a ball or a cylinder, as the triangles of the unit box it is drawn
// in (`shape`), for a guide.
std::vector<float> unit_shape(const std::string& shape);

}  // namespace sg
