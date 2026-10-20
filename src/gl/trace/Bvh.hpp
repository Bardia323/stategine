// Stategine - a tree of boxes over what rays may meet (src/gl/trace), as
// the tracing shaders read it.
//
// Two of a node's children, each by its own box: a ray asks both boxes of a
// node at once and goes on into the nearer first, never reading a child it
// misses. A node is sixteen numbers, four texels of a picture of floats:
//
//   left's lowest corner, its reference   | left's highest corner, how many
//   right's lowest corner, its reference  | right's highest corner, how many
//
// A child that holds things (`how many` > 0) refers to the first of them;
// one that holds none is another node (its index). A child that is not there
// has a box no ray meets (lowest above highest). Every number is a whole
// number kept as a float - exact to sixteen million - never bits a float
// must not be (a card may change what is not a number as it reads it).
#pragma once

#include <cstdint>
#include <vector>

namespace sg::render::trace {

struct Box {
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    // inline: every corner of every triangle grows a box by it, as trees are made and fitted.
    void grow(const float p[3]) {
        for (int k = 0; k < 3; ++k) lo[k] = p[k] < lo[k] ? p[k] : lo[k], hi[k] = p[k] > hi[k] ? p[k] : hi[k];
    }
    // inline: and every node's box by its children's.
    void grow(const Box& b) {
        for (int k = 0; k < 3; ++k) lo[k] = b.lo[k] < lo[k] ? b.lo[k] : lo[k], hi[k] = b.hi[k] > hi[k] ? b.hi[k] : hi[k];
    }
    bool empty() const { return lo[0] > hi[0]; }
    float area() const;
};

constexpr int kNodeFloats = 16;

struct Tree {
    std::vector<float> nodes;     // kNodeFloats a node, the root first
    std::vector<uint32_t> order;  // the things, in the order the leaves hold them
    Box bounds;
};

// The tree over `boxes` (each thing's), at most `leaf` things a leaf, split
// where the surface areas say a ray meets least (binned SAH). References are
// as in `nodes` and `order` here: whoever lays several trees in one picture
// moves them (shift).
Tree build(const std::vector<Box>& boxes, int leaf);

// A tree laid at node `node_base` and its things at `thing_base`: its
// references moved there.
void shift(std::vector<float>& nodes, std::size_t from, uint32_t node_base, uint32_t thing_base);

}  // namespace sg::render::trace
