#include "Bvh.hpp"

#include <algorithm>
#include <cmath>

namespace sg::render::trace {

float Box::area() const {
    if (empty()) return 0.0f;
    const float x = hi[0] - lo[0], y = hi[1] - lo[1], z = hi[2] - lo[2];
    return 2.0f * (x * y + y * z + z * x);
}

namespace {

constexpr int kBins = 16;

struct Job {
    uint32_t node;   // where it is written
    int side;        // which of its children (0 left, 1 right)
    uint32_t first, count;
    Box box;
};

void put_child(std::vector<float>& nodes, uint32_t node, int side, const Box& b, float ref, float count) {
    float* n = &nodes[static_cast<std::size_t>(node) * kNodeFloats + static_cast<std::size_t>(side) * 8];
    n[0] = b.lo[0], n[1] = b.lo[1], n[2] = b.lo[2], n[3] = ref;
    n[4] = b.hi[0], n[5] = b.hi[1], n[6] = b.hi[2], n[7] = count;
}

uint32_t new_node(std::vector<float>& nodes) {
    const uint32_t at = static_cast<uint32_t>(nodes.size() / kNodeFloats);
    nodes.resize(nodes.size() + kNodeFloats, 0.0f);
    // (Neither child there until said: boxes no ray meets.)
    const Box none;
    put_child(nodes, at, 0, none, 0.0f, 0.0f);
    put_child(nodes, at, 1, none, 0.0f, 0.0f);
    return at;
}

}  // namespace

Tree build(const std::vector<Box>& boxes, int leaf) {
    Tree t;
    const uint32_t n = static_cast<uint32_t>(boxes.size());
    t.order.resize(n);
    for (uint32_t i = 0; i < n; ++i) t.order[i] = i;
    std::vector<float> mid(static_cast<std::size_t>(n) * 3);
    for (uint32_t i = 0; i < n; ++i) {
        for (int k = 0; k < 3; ++k) mid[i * 3 + k] = 0.5f * (boxes[i].lo[k] + boxes[i].hi[k]);
        t.bounds.grow(boxes[i]);
    }
    t.nodes.reserve(static_cast<std::size_t>(std::max<uint32_t>(n, 1)) * kNodeFloats);
    const uint32_t root = new_node(t.nodes);
    if (n == 0) return t;
    if (n <= static_cast<uint32_t>(leaf)) {
        put_child(t.nodes, root, 0, t.bounds, 0.0f, static_cast<float>(n));
        return t;
    }
    // The root's two halves, then each half's, until a half is a leaf.
    std::vector<Job> jobs;
    const auto split = [&](uint32_t node, uint32_t first, uint32_t count) {
        // Where to cut: of the centres' spread, the axis and bin that leaves
        // the least area times things on either side.
        Box centres;
        for (uint32_t i = first; i < first + count; ++i) centres.grow(&mid[t.order[i] * 3]);
        int axis = -1, cut = 0;
        float best = 1e30f;
        for (int k = 0; k < 3; ++k) {
            const float extent = centres.hi[k] - centres.lo[k];
            if (extent <= 1e-12f) continue;
            Box bin[kBins];
            uint32_t in[kBins] = {};
            const float scale = kBins / extent;
            for (uint32_t i = first; i < first + count; ++i) {
                const uint32_t p = t.order[i];
                const int b = std::min(kBins - 1, static_cast<int>((mid[p * 3 + k] - centres.lo[k]) * scale));
                bin[b].grow(boxes[p]);
                ++in[b];
            }
            float left_area[kBins];
            uint32_t left_n[kBins];
            Box acc;
            uint32_t acc_n = 0;
            for (int b = 0; b < kBins - 1; ++b) {
                acc.grow(bin[b]), acc_n += in[b];
                left_area[b] = acc.area(), left_n[b] = acc_n;
            }
            acc = Box{}, acc_n = 0;
            for (int b = kBins - 1; b > 0; --b) {
                acc.grow(bin[b]), acc_n += in[b];
                if (left_n[b - 1] == 0 || acc_n == 0) continue;
                const float cost = left_area[b - 1] * static_cast<float>(left_n[b - 1]) + acc.area() * static_cast<float>(acc_n);
                if (cost < best) best = cost, axis = k, cut = b;
            }
        }
        uint32_t m = first + count / 2;
        if (axis >= 0) {
            const float extent = centres.hi[axis] - centres.lo[axis], scale = kBins / extent;
            const auto at = std::partition(t.order.begin() + first, t.order.begin() + first + count, [&](uint32_t p) {
                return std::min(kBins - 1, static_cast<int>((mid[p * 3 + axis] - centres.lo[axis]) * scale)) < cut;
            });
            m = static_cast<uint32_t>(at - t.order.begin());
        }
        // (All in one bin - things standing on one another: halved by count.)
        if (m == first || m == first + count) {
            const int k = centres.hi[0] - centres.lo[0] >= centres.hi[1] - centres.lo[1]
                              ? (centres.hi[0] - centres.lo[0] >= centres.hi[2] - centres.lo[2] ? 0 : 2)
                              : (centres.hi[1] - centres.lo[1] >= centres.hi[2] - centres.lo[2] ? 1 : 2);
            m = first + count / 2;
            std::nth_element(t.order.begin() + first, t.order.begin() + m, t.order.begin() + first + count,
                             [&](uint32_t a, uint32_t b) { return mid[a * 3 + k] < mid[b * 3 + k]; });
        }
        for (int side = 0; side < 2; ++side) {
            const uint32_t f = side == 0 ? first : m, c = side == 0 ? m - first : first + count - m;
            Box b;
            for (uint32_t i = f; i < f + c; ++i) b.grow(boxes[t.order[i]]);
            jobs.push_back(Job{node, side, f, c, b});
        }
    };
    split(root, 0, n);
    while (!jobs.empty()) {
        const Job j = jobs.back();
        jobs.pop_back();
        if (j.count <= static_cast<uint32_t>(leaf)) {
            put_child(t.nodes, j.node, j.side, j.box, static_cast<float>(j.first), static_cast<float>(j.count));
            continue;
        }
        const uint32_t child = new_node(t.nodes);
        put_child(t.nodes, j.node, j.side, j.box, static_cast<float>(child), 0.0f);
        split(child, j.first, j.count);
    }
    return t;
}

void shift(std::vector<float>& nodes, std::size_t from, uint32_t node_base, uint32_t thing_base) {
    for (std::size_t at = from; at + kNodeFloats <= nodes.size(); at += kNodeFloats)
        for (int side = 0; side < 2; ++side) {
            float* c = &nodes[at + static_cast<std::size_t>(side) * 8];
            if (c[0] > c[4]) continue;  // (not there)
            c[3] += static_cast<float>(c[7] > 0.0f ? thing_base : node_base);
        }
}

}  // namespace sg::render::trace
