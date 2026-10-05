// Stategine - Modeler: a shape made from a recipe, as a state of its own.
//
// What `sg::shapes` does by hand - an outline pushed out, a profile turned -
// this does from words: a recipe, one operation to a line, kept as data (as a
// Paint keeps its strokes), and the mesh it makes, as a function of the recipe
// alone. The mesh is triangles in the format every model here is (eight
// numbers a corner: position, normal, uv), sized with `shapes::fit`, so any
// Spatial3D takes it with `model(name, triangles)` and draws it as
// `shape` = "model".
//
// The recipe language (one statement a line, `#` to the end is a comment;
// numbers may be expressions - `$r*1.5`, `floor($len/0.6)`, `sin(30)`):
//
//   shapes, standing on y = 0 about `at` (centre=1 centres them):
//     box sx [sy sz]            cyl r h          cone r1 r2 h | cone r h
//     sphere r                  torus R r        capsule r h
//   shapes, as drawn (coordinates are where they are):
//     lathe r,y r,y ...         turned about y, closed at the ends
//     extrude depth x,y ...     an outline in x,y pushed out along z
//     prism h x,z x,z ...       an outline in the ground plane, pushed up
//     loft h ring / ring ...    rings x,z (same count) joined, equally high
//     tube r x,y,z ...         a round bar along a path
//     sweep x,y ... / x,y,z ... an outline carried along a path
//     import file               a Wavefront .obj, read by the program's files
//   options on any of them: at=x,y,z  rot=ry | rx,ry,rz (degrees)  scale=s |
//     sx,sy,sz  mat=<material>  round=r (rounded: made from its field)
//     chamfer=c  sides=n  res=metres  crease=degrees  centre=1
//
//   combining, the word before a statement: add (the default), sub, and, and
//   smooth ones `blend=k` and `carve=k` (a radius k): what came before in the
//   block is the left hand. Only what a cut touches is meshed from a field;
//   what it does not touch stays the exact faces it began as.
//
//   blocks, to `end`:  group [name]   array n=N step=dx,dy,dz [turn=deg]
//     radial n=N [axis=y] [arc=360]   mirror x|y|z
//     A block takes the same options and prefix words as a shape.
//   copy <name>               a named group, again
//   let name value            set res=.. sides=.. crease=.. mat=..
//   define name p1 p2=default ... end    a macro; `name a b at=...` calls it
//
// The library of macros (`recipes()`) is written in this language.
//
//   Modeler:  elements `recipe` (params `ops`, `cell`, `sides`, `crease`)
//     recipe --op-->    recipe   model.op {line}: a line added
//     recipe --set-->   recipe   model.set {ops}: the whole recipe
//     recipe --undo-->  recipe   model.undo {n}: the last n lines taken back
//     recipe --clear--> recipe   model.clear
//     recipe --select--> recipe  model.select {line}: the line worked on (`selected`)
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "sg/core/State.hpp"

namespace sg {

class StateGraph;

namespace sculpt {

struct Options {
    double cell = 0.1;      // metres a field is meshed at
    int sides = 24;         // faces round a turned thing
    double crease = 40.0;   // degrees past which an edge is left sharp
    int max_grid = 220;     // cells across a field, at most
};

// What a program gives to read files by: the text of one, and its time stamp.
struct Files {
    std::function<bool(const std::string& path, std::string& text)> read;
    std::function<long long(const std::string& path)> stamp;
};

// The faces of one material.
struct Part {
    std::string material;
    std::vector<float> corners;  // 8 floats a corner, 3 corners a face
};

struct Model {
    std::vector<Part> parts;
    Vec3d lo, hi;                 // the box round it all
    std::size_t triangles = 0;
    std::string errors;           // what the recipe got wrong, a line each
    std::vector<std::string> imports;

    std::vector<float> all() const;
    Vec3d size() const { return hi - lo; }
};

// The mesh of a recipe: a function of the text, the options and the files it
// imports.
Model build(const std::string& recipe, const Options& options = {}, const Files* files = nullptr);

// The built-in macros: one to a line, its name, parameters and what it is -
// and with a library's name, that library's (`use <name>`).
std::string recipes(const std::string& library = {});
// Libraries of macros, by name, for `use <name>`: the modeller's own (the
// architect's `arch` and its styles), and any a program adds - a style is a
// library that says the same words (docs/modeler.md, Architecture) its way.
void define_library(const std::string& name, const std::string& text);
std::vector<std::string> libraries();
unsigned library_revision();  // moves when a program defines one
// Wavefront text of a model, one object per material.
std::string to_obj(const Model& m, const std::string& name);

// --- looking at it ---------------------------------------------------------------
// Four views of a model in one `w` x `h` picture (rgb, a row at a time):
// three-quarters from above, front (from +z), side (from +x) and top, each
// fitted to its square, shaded by one light, each material its own colour.
// For whoever writes a recipe to see it; `png` makes it a file.
std::vector<unsigned char> picture(const Model& m, int w, int h);
// One view in perspective from `eye` - inside a room, say - turned `yaw`
// degrees about up (0 looks along -z) and `pitch` up, `fov` degrees across;
// a square `size` pixels.
std::vector<unsigned char> picture_from(const Model& m, const Vec3d& eye, double yaw, double pitch, double fov, int size);
std::string png(const std::vector<unsigned char>& rgb, int w, int h);

// --- a recipe, read and written as placements ---------------------------------
// What a picture of a recipe can move: each shape (or macro, or copy) said at
// the top of it - in no block, so where it stands is where it is - with where
// it stands, how it is turned and how big (`at=`, `rot=` in degrees,
// `scale=`). A line whose numbers are expressions is read, not editable.
// Lines count from 0, as the text has them.
struct Placed {
    int line = -1;
    std::string word;   // the shape, macro or `copy`
    Vec3d at, rot, scale{1, 1, 1};
    bool editable = false;
};
std::vector<Placed> placements(const std::string& recipe);
Placed placement(const std::string& recipe, int line);
// The recipe with that line standing, turned and sized so, its other words
// and its comment kept, its numbers to a millimetre and a tenth of a degree.
// The same placement again is the same text (writing back what was read
// changes nothing).
std::string place(const std::string& recipe, int line, const Vec3d& at, const Vec3d& rot, const Vec3d& scale);

}  // namespace sculpt

class Modeler : public State {
public:
    static Key recipe_id() { return Key{"recipe"}; }
    static Key op_event() { return Key{"model.op"}; }
    static Key set_event() { return Key{"model.set"}; }
    static Key undo_event() { return Key{"model.undo"}; }
    static Key select_event() { return Key{"model.select"}; }
    static Key clear_event() { return Key{"model.clear"}; }

    explicit Modeler(Key id);
    Key kind() const override { return Key{"modeler"}; }

    // The program's files, for `import` (as Texture's reader).
    static void set_files(sculpt::Files f);

    const std::string& text() const;
    int count() const;  // lines of the recipe
    void ports(StateGraph& g) const;

    // Made again only when the recipe, its settings or an imported file moved.
    const sculpt::Model& model();
    // Every part fitted into the unit box a mesh is sized from; `size` is
    // what to draw it at. One part (by index) fitted in the box of the whole,
    // so the parts fit together.
    std::vector<float> fitted(Vec3d& size);
    std::vector<float> fitted_part(std::size_t part, Vec3d& size);

private:
    std::shared_ptr<const sculpt::Model> built_;
    uint64_t built_stamp_ = 0;
    std::vector<long long> import_stamps_;
    bool fresh();
};

}  // namespace sg
