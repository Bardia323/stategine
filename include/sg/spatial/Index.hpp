// An index of numbered bounds, rebuilt/refitted from its caller's data.
// No objects, ownership, state identity or graph relations are stored here.
#pragma once
#include "sg/spatial/Geometry.hpp"
#include <cstddef>

namespace sg::spatial {
class Index {
public:
    struct Entry { std::size_t id; Aabb bounds; };
    void rebuild(std::vector<Entry> entries);
    // Same leaf IDs/order: refit in linear time. Otherwise rebuild.
    void refit(std::vector<Entry> entries);
    std::vector<std::size_t> query(const Aabb& box) const;
    std::vector<std::size_t> query(const ConvexVolume& volume) const;
    std::vector<std::size_t> query(const Ray& ray) const;
    std::size_t size() const { return entries_.size(); }
    std::size_t visited() const { return visited_; }
private:
    struct Node { Aabb bounds; int left=-1, right=-1; std::size_t entry=0; };
    std::vector<Entry> entries_;
    std::vector<Node> nodes_;
    std::vector<std::size_t> order_;
    mutable std::size_t visited_=0;
    int build(std::size_t begin, std::size_t end);
    // inline: shared traversal for each geometric predicate; no semantics.
    template<class Predicate> std::vector<std::size_t> query_with(Predicate accepts) const {
        std::vector<std::size_t> result;
        visited_=0;
        if(nodes_.empty()) return result;
        // Median splits bound stack depth by the number of bits in a leaf count.
        std::array<int,std::numeric_limits<std::size_t>::digits+1> stack{};
        std::size_t pending=1;
        while(pending>0) {
            const Node& n=nodes_[static_cast<std::size_t>(stack[--pending])]; ++visited_;
            if(!accepts(n.bounds)) continue;
            if(n.left<0) result.push_back(entries_[n.entry].id);
            else { stack[pending++]=n.right; stack[pending++]=n.left; }
        }
        std::sort(result.begin(),result.end());
        return result;
    }
};
} // namespace sg::spatial
