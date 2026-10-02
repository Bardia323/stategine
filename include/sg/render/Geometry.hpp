#pragma once
#include <vector>
namespace sg::render {
std::vector<float> cube_vertices();

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
