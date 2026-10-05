// The modeller's insides: points, fields, solids. Not the engine's interface -
// `sg/domains/Modeler.hpp` is. A solid is pieces; a piece is either exact faces
// (a union of shapes stays what it was) or a field (meshed where a cut or a
// blend made it one).
#pragma once

#include <cmath>
#include <cstdint>
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

// --- exact faces ----------------------------------------------------------------
Geom g_box(V3 size);
Geom g_lathe(const std::vector<P2>& profile, int sides);
Geom g_torus(double big, double small, int sides, int ring);
Geom g_extrude(const std::vector<P2>& outline, double depth, double chamfer);
Geom g_loft(const std::vector<std::vector<P2>>& rings, double height);
Geom g_sweep(const std::vector<P2>& profile, const std::vector<V3>& path, bool close_ends);
void transform(Geom& g, const Mat& m);

// Shaded: smooth where faces meet within the crease, sharp past it.
void shade(const Geom& g, std::vector<std::vector<float>>& by_material, int default_material);
struct Obj {
    Geom geom;
    std::vector<std::string> materials;
    bool ok = false;
};
Obj read_obj(const std::string& text);

// --- solids ---------------------------------------------------------------------
struct Piece {
    SdfP field;
    std::shared_ptr<const Geom> exact;  // null: a field to mesh
    Box bb;
    double res = 0;
    double crease = 40.0;
    int material = 0;
};
struct Solid {
    std::vector<Piece> pieces;
    bool empty() const { return pieces.empty(); }
};
enum class Mode { Add, Sub, And, Blend, Carve };
void join(Solid& into, const Solid& s, Mode mode, double k);
Solid moved(const Solid& s, const Mat& m);
Solid unite(const Solid& a, const Solid& b);

}  // namespace sg::sculpt::kernel
