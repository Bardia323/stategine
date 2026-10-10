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
//   surface_layer  a file of what the surface is (its red occlusion, green
//               roughness, blue metal), read as `layer` is, over the material's
//   normals     1: a normal map made from its height, as finely as its
//               generator says it (`relief` metres high, `tile` metres a cell)
//   normal_layer  a file of a normal map (OpenGL's way: green up its cells;
//               `normal_dx` 1 for DirectX's, green down), over the made one
//   normal_strength  how strongly the normal map bends the surface (1)
//   per_cell    1: each layer file is one tile, laid whole in every cell (a
//               photographed material); 0, stretched over the six (a painting)
//
// What the surface is beside its colour - how occluded its hollows are, how
// rough, how metal - is a second picture of the same six cells, its surface
// map (red, green, blue), made only when something says it: a material
// (`define_material`, every channel at once) or a `surface_layer`. A thing
// wearing a texture with a surface map takes its roughness and metal from
// it, and its occlusion dims the light it is given from all round.
//
// Which way its surface faces at every point is a third picture of the same
// cells, its normal map: x along a cell's u, y up its v, z out of it, made
// from the height (`normals`) or read (`normal_layer`). A thing wearing it is
// bent by it, in place of what the screen can tell from its relief.
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

    // Everything a surface is at a point, 0..1 each: its colour, how high its
    // paint stands, how much of the light from all round its hollows let in
    // (1 open, 0 shut), how rough, how metal.
    struct Channels {
        double r = 1, g = 1, b = 1, height = 1;
        double occlusion = 1, roughness = 0.6, metal = 0;
    };
    // A material: every channel of a point (u, v) of a cell, from the map's
    // params - a generator that says what the surface is, not only its colour.
    // It must tile where the texture does, and give the same for the same.
    using Material = std::function<Channels(const Params& map, int cell, double u, double v)>;

    static Key map_id() { return Key{"map"}; }
    static Key set_event() { return Key{"texture.set"}; }

    // A generator by name, for every texture (the built-in ones: plain,
    // noise, checks).
    static void define(const std::string& name, Generator g);
    // A material by name (built in: rust - steel, painted and rusting): it
    // paints the colour as a generator does, and the surface map as well.
    static void define_material(const std::string& name, Material m);
    static void set_reader(Reader r);

    Texture(Key id, int cell_px = 256);

    Key kind() const override { return Key{"texture"}; }

    const Element& map() const { return element(map_id()); }

    // Whether it has a surface map: its generator is a material, or it says a
    // `surface_layer`.
    bool has_surface() const;
    // Its surface map, RGBA rows of the picture's size (red occlusion, green
    // roughness, blue metal; linear), made when asked and again only when the
    // map's params or the surface layer's file change; empty without one.
    const std::vector<unsigned char>& surface_raster();
    // Which surface map `surface_raster` last made: it moves when it changes.
    uint64_t surface_revision() const { return surface_revision_; }

    // Whether it has a normal map: it says `normals`, or a `normal_layer`.
    bool has_normals() const;
    // Its normal map, RGBA rows of the picture's size (each direction 0..1 as
    // 0.5 + 0.5 x; linear), made when asked and again only when the map's
    // params or the normal layer's file change; empty without one.
    const std::vector<unsigned char>& normal_raster();
    uint64_t normal_revision() const { return normal_revision_; }

protected:
    void paint() override;
    // Memoised on the map's params and on the layer file's stamp.
    bool stale() override;

private:
    uint64_t painted_stamp_ = 0;
    long long layer_time_ = 0;
    std::string layer_path_;
    std::vector<unsigned char> surface_px_;
    uint64_t surface_stamp_ = 0, surface_revision_ = 0;
    long long surface_layer_time_ = 0;
    std::vector<unsigned char> normal_px_;
    uint64_t normal_stamp_ = 0, normal_revision_ = 0;
    long long normal_layer_time_ = 0;
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
