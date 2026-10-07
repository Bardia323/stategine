// The modeller's insides: points, fields, solids. Not the engine's interface -
// `sg/domains/Modeler.hpp` is. A solid is pieces; a piece is either exact faces
// (a union of shapes stays what it was) or a field (meshed where a cut or a
// blend made it one).
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sg::sculpt::kernel {

struct V3 {
    double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 unit(V3 a) { return len(a) > 1e-12 ? a * (1 / len(a)) : V3{0, 1, 0}; }

// A 3x3 and a move: p -> m p + t.
struct Mat {
    double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    V3 t;
};
V3 apply(const Mat& a, V3 p);
Mat operator*(const Mat& a, const Mat& b);  // b first, then a
Mat inverse(const Mat& a);
double det(const Mat& a);
double min_scale(const Mat& a);
Mat translate(V3 t);
Mat scaling(V3 s);
Mat rotation(double rx_deg, double ry_deg, double rz_deg);  // about x, then y, then z

struct Box {
    V3 lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    bool empty() const { return lo.x > hi.x; }
    void grow(V3 p);
    void add(const Box& b);
    bool overlaps(const Box& b, double pad) const;
    Box moved(const Mat& a) const;
};

// Faces not yet shaded: points, and three indices a face; a material for each
// face where the pieces differ.
struct Geom {
    std::vector<V3> p;
    std::vector<int> t;
    std::vector<int> mat;  // per face, or empty
    // Where each corner is on its material's picture (u, v; v down, as a
    // picture's rows are), two numbers a corner of `t` - or empty: faces made
    // here are given where they stand instead (shade).
    std::vector<float> uv;
    double crease = 40.0;
};

// --- fields ---------------------------------------------------------------------
// A signed distance, never more than the true one in size (so a block far from
// the surface can be skipped), negative inside.
struct Sdf {
    Box bb;
    int material = 0;
    virtual ~Sdf() = default;
    virtual double d(V3 p) const = 0;
    virtual int pick(V3 /*p*/) const { return material; }
};
using SdfP = std::shared_ptr<const Sdf>;

struct P2 {
    double x, y;
};
SdfP sdf_box(V3 half, double round);
SdfP sdf_sphere(double r);
SdfP sdf_cyl(double r, double half_h, double round);
SdfP sdf_cone(double r1, double r2, double half_h);
SdfP sdf_torus(double big, double small);
SdfP sdf_capsule(double r, double half_straight);
SdfP sdf_lathe(const std::vector<P2>& profile);
SdfP sdf_extrude(const std::vector<P2>& outline, double half_depth, double chamfer);
SdfP sdf_loft(const std::vector<std::vector<P2>>& rings, double height);
SdfP sdf_tube(const std::vector<V3>& path, double r);
SdfP sdf_sweep(const std::vector<P2>& profile, const std::vector<V3>& path);
SdfP sdf_xform(SdfP child, const Mat& m);
// The field of a mesh: the distance to its nearest face, negative where its
// winding number says inside - so meshes in one another are one solid. Its
// faces' materials go with it.
SdfP sdf_mesh(const Geom& g);
SdfP sdf_union(std::vector<SdfP> children);
SdfP sdf_sub(SdfP a, SdfP b, double k);
SdfP sdf_and(SdfP a, SdfP b);
SdfP sdf_blend(SdfP a, SdfP b, double k);
SdfP with_material(SdfP s, int material);  // the same field, as this material
Mat basis(V3 origin, V3 x, V3 y, V3 z);    // the move that takes the axes to these
V3 perpendicular(V3 d);                    // some unit vector across d

// The surface of a field, as dual contouring finds it (sharp where the field
// is). `warn` says if the cell had to be made coarser.
void surface(const Sdf& f, double cell, int max_grid, Geom& out, std::string* warn);
// The same, only in a box of space: open where the surface leaves it. What
// is returned is the box its grid really took (a little larger).
Box surface_in(const Sdf& f, const Box& region, double cell, int max_grid, Geom& out, std::string* warn);
// Dual contouring on a grid of nx * ny * nz cells of `h` from `lo`.
void contour(const Sdf& f, V3 lo, double h, int nx, int ny, int nz, Geom& out);

// --- fewer faces ---------------------------------------------------------------
// Edges collapsed where the surface says least, cheapest first (each vertex
// carries the planes of the faces it was made from; a collapse costs how far
// it moves off them), while that is under `tol` metres - or, with `target`,
// until that few faces are left. A vertex `locked` stays (by index: the exact
// faces a cut kept); so do the ends of an open edge and where two materials
// meet. No face is turned over and nothing is pinched: a closed surface stays
// closed. Flat spans go to a few large faces; detail keeps its own.
void simplify(Geom& g, double tol, std::size_t target, const std::vector<char>* locked = nullptr);
// The points of g welded where they are one point (to 1e-7 m), faces of no
// area between welded points dropped.
void weld(Geom& g);

// --- where a cut is ----------------------------------------------------------------
// The faces of g outside box b, cut where they cross it: what is inside is
// gone, and the faces cut end on the box. Every face given a material.
Geom clip_outside(const Geom& g, const Box& b, int material);
// Two surfaces that meet across the face of a box made one: `outer` ends on
// the box (clip_outside), `inner` ends just inside it (surface_in); the strip
// between each pair of their open rims is filled with faces, so the two are
// one closed surface. False (and nothing done) if their rims do not pair up.
bool zip(const Geom& outer, const Geom& inner, const Box& b, double cell, Geom& out);
// A closed surface of field f meshed at `coarse` (g), made again finer -
// as the face `budget` allows, a quarter of the cell at most - wherever it
// misses the field by more than a tenth of its cell (a small feature, a
// tight curve), each such place in a box of its own, zipped in. Flat spans
// and sharp edges, which the coarse cell already meets, stay as they are.
void refine(Geom& g, const Sdf& f, double coarse, std::size_t budget, int max_grid);

// --- exact faces ----------------------------------------------------------------
Geom g_box(V3 size);
Geom g_lathe(const std::vector<P2>& profile, int sides);
Geom g_torus(double big, double small, int sides, int ring);
Geom g_extrude(const std::vector<P2>& outline, double depth, double chamfer);
Geom g_loft(const std::vector<std::vector<P2>>& rings, double height);
// A path to carry an outline along: its points, and a frame for each of its
// segments - the way on (t), across (n), and the third (b = t x n) - each
// turned from the last by the least turn that takes one way on to the next,
// so nothing twists (a rotation-minimizing frame). A path that ends where it
// began goes round, and its frames are made to close.
struct Frame {
    V3 t, n, b;
};
struct Path {
    std::vector<V3> points;
    std::vector<Frame> frames;
    bool closed = false;
    // The outline at point i: carried in along the way in, it meets the plane
    // half way between the ways in and out there (the mitre) - where the
    // outline carried out meets it too, so a corner neither pinches nor twists.
    std::vector<V3> section(std::size_t i, const std::vector<P2>& outline) const;
};
Path along(const std::vector<V3>& points);
// A path with its corners rounded over radius r (no corner cut back past half
// a side).
std::vector<V3> rounded_path(const std::vector<V3>& path, double r, int sides);
Geom g_sweep(const std::vector<P2>& profile, const std::vector<V3>& path, bool close_ends);
void transform(Geom& g, const Mat& m);

// Shaded: smooth where faces meet within the crease, sharp past it.
void shade(const Geom& g, std::vector<std::vector<float>>& by_material, int default_material);
struct Obj {
    Geom geom;
    std::vector<std::string> materials;
    std::string mtllib;  // the material library it names, if it does
    bool ok = false;
};
Obj read_obj(const std::string& text);
// A material library (.mtl): each material's picture (`map_Kd`), as written.
std::map<std::string, std::string> read_mtl(const std::string& text);

// --- growing ---------------------------------------------------------------------
// A plant grown from an L-system (ModelerGrow.cpp): the axiom rewritten by its
// rules `n` times, the word walked by a turtle - F a step of wood (G too), f
// a step without, + - a turn about up, & ^ a pitch, \ / a roll (each by
// `angle` degrees, or by its own: `+(30)`), | a turn about, [ ] a branch
// (its steps `branch` times as long), ' a step `shorten` times as long, L
// leaves here, ! no leaves at the tips of what grows after it (roots). Its wood is as thick as the pipe model says, `width` at the
// foot; every tip, and every L, carries leaves.
struct Growth {
    std::string axiom = "F";
    std::vector<std::string> rules;  // `A:rhs`, or `A:weight:rhs`
    int n = 4;
    double angle = 25, len = 1, width = 0.15;
    double shorten = 0.9, branch = 0.8;
    double jitter = 0;                // degrees each turn and step wanders, at random
    double bend = 0;                  // how hard the tropism pulls (radians a step at right angles)
    V3 tropism{0, -1, 0};             // the way it pulls: down (droop), up (to the light)
    double height = 0;                // > 0: scaled to stand this high
    uint64_t seed = 1;
    int sides = 7;                    // faces round the trunk; fewer round thinner wood
    double leaf = 0.35;               // a clump's radius, or a card's half width
    int leaves = 1;                   // 0 none, 1 clumps, 2 crossed cards, 3 leaves (blades on stalks), 4 tufts of needles
    double leaf_width = 0.32;         // a blade's width, as a share of its length
    int leaf_detail = 1;              // how round a clump is (0: eight faces)
    int leafy = 1;                    // clumps at a tip
    double min = 0.004;               // wood thinner than this is pruned
    double merge = 4;                 // degrees within which a run of steps is made one span
    double pipe = 2.2;                // the pipe model's exponent (2: area kept)
    int most = 60000;                 // steps of wood at most
};
// Wood faces of material `wood`, leaves of `leaf`; what went wrong to `errors`.
Geom grow(const Growth& g, int wood, int leaf, std::string& errors);

// --- solids ---------------------------------------------------------------------
// A piece's exact faces: given (a shape as it is made), or made the first time
// they are asked for (a cut, zipped in where it was made) - so faces nobody
// asks for, a block made again as one surface by `remesh`, are never made.
// None, or none made after all (a cut that would not zip): the piece is its
// field, meshed whole.
class Faces {
public:
    Faces() = default;
    explicit Faces(std::shared_ptr<const Geom> g);
    static Faces later(std::function<std::shared_ptr<const Geom>()> make);
    std::shared_ptr<const Geom> get() const;    // made now, if they are to be
    bool known() const { return bool(lazy_); }  // there are faces, or there may be
    Faces moved(const Mat& m) const;

private:
    struct Lazy;
    std::shared_ptr<Lazy> lazy_;
};

struct Piece {
    SdfP field;
    Faces exact;  // none: a field to mesh
    Box bb;
    double res = 0;
    double crease = 40.0;
    int material = 0;
};
// An opening asked of the walls round a solid (`opening`): where its foot's
// middle is, half its width across, its height up and which way it looks -
// vectors, so whatever moves the solid moves and sizes it too.
struct Hole {
    V3 at, across, up, facing;
    std::string head;
    bool walk = false;
    double recess = 0;
};
// A joint of a solid (`moves`): where its axis is and which way, as vectors,
// so whatever moves the solid moves the axis with it.
struct Hinge {
    std::string name, parent, with;
    bool slide = false;
    V3 at, axis{0, 1, 0};
    double lo = 0, hi = 0, follow = 0;
    std::vector<V3> path;  // a track, ridden by two points `span` apart, the first `from` along it
    double from = 0, span = 0, step = 0;
};
struct Solid {
    std::vector<Piece> pieces;
    std::vector<Hole> holes;
    std::vector<Hinge> joints;
    bool empty() const { return pieces.empty(); }
};
enum class Mode { Add, Sub, And, Blend, Carve };
// How a cut is meshed where it is made: the cell (a piece's or the cut's own
// `res` is finer), the grid's limit, and where to say it was made coarser.
struct Mesher {
    double cell = 0.1;
    int max_grid = 220;
    std::string* warn = nullptr;
};
// A field meshed whole, closed, and then made fewer where it is flat.
std::shared_ptr<const Geom> mesh_whole(const Sdf& f, double cell, double crease, const Mesher& m);
void join(Solid& into, const Solid& s, Mode mode, double k, const Mesher& m);
Solid moved(const Solid& s, const Mat& m);
Solid unite(const Solid& a, const Solid& b);

}  // namespace sg::sculpt::kernel
