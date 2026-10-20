#pragma once
#include <cstdint>
#include <vector>
namespace sg::render {
// Corners (8 floats each: where, which way, uv; 3 a face) as the different
// ones once each, and for every corner which of them it is: the same faces,
// to the bit, in what a card that draws by index holds - a smooth surface's
// corners are shared by the faces round them.
struct Indexed {
    std::vector<float> corners;
    std::vector<uint32_t> index;
};
Indexed indexed(const std::vector<float>& corners);

// Which way each corner's u runs along its surface, and which way its v
// (4 floats a corner: the u way, unit and square to the corner's normal; and
// +1 or -1, which side of it v runs - the corner's normal crossed with the u
// way, times this, is the v way): what a normal map drawn on the corners' own
// uv is laid along. Each face's ways from how its uv runs over it, shared by
// the faces round a corner they share, weighed by the corner's angle in each
// (as MikkTSpace weighs them, which is what bakers write normal maps in). A
// face whose uv does not run (all its corners at one place on the picture)
// gives none; a corner with none is given a way square to its normal. Pure.
std::vector<float> tangents(const std::vector<float>& corners);

// What a mesh's own shape asks of its wear, 4 floats a corner: how convex it
// is there (0..1: an edge, worn first), how concave (0..1: a crease, where
// dirt keeps), how shut it is from the sky round it by the mesh itself (0
// open .. 1: a hollow, under a lip), and 0 (made; a mesh given none reads 1
// there). Its corners welded where they stand, so a crease's two corners
// are one point. The bend is the turn of the surface along each edge times
// the edge's length (a box's edge an edge however finely it is made); the
// shut, `rays` rays over each point's hemisphere, as light comes, each that
// meets the mesh again within a third of its size shut. Pure.
// `size`: the mesh's corners are a unit box's (a model fitted, as a world
// keeps them), drawn this big - its wear made at the size it is seen.
std::vector<float> wear(const std::vector<float>& corners, int rays = 32, double spread = 0.1, const float* size = nullptr);

std::vector<float> cube_vertices();
// The same cube, each face cut into `n` x `n` (its uv the same, 0..1 across
// each face): for a thing that bends (`bend`), whose faces must curve.
std::vector<float> grid_cube_vertices(int n);
// A panel's quad (as quad_vertices: in x = 0, facing +x, its u across from
// +z to -z), cut into `n` columns across: for a panel that curls (`curl`).
std::vector<float> quad_columns_vertices(int n);

// A cylinder standing on y, radius 0.5 and height 1, centred like the cube, so
// the same model matrix sizes it: sx and sz are its diameters, sy its height.
// A box with its edges and corners rounded to `r` metres, `sx` x `sy` x `sz`
// in size, and narrowing to `taper` of its width and depth at the top (1:
// straight up). Made in the unit box every mesh shares, so the same model
// matrix places it; each normal is given pre-divided by the size, so that
// once the model's scale has been applied to it, it points the way the
// rounded surface does.
std::vector<float> rounded_box_vertices(float sx, float sy, float sz, float r, float taper = 1.0f,
                                               int seg = 3);

std::vector<float> cylinder_vertices(int segments = 28, float taper = 1.0f);

// A sphere of radius 0.5, centred like the cube.
std::vector<float> sphere_vertices(int stacks = 14, int slices = 24);

// A unit quad standing in the y-z plane, facing +x: scale it by {1, height,
// width} and rotate it by the portal's yaw and it faces the way the portal
// does, with no extra quarter turn anywhere.
std::vector<float> quad_vertices();
}
