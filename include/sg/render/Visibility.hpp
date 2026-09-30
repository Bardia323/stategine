// Renderer query machinery: numbered draw bounds, separate from physics.
#pragma once
#include "sg/spatial/Index.hpp"

namespace sg::render {
class Visibility {
public:
    void update(std::vector<spatial::Index::Entry> bounds);
    std::vector<std::size_t> visible(const spatial::ConvexVolume& volume) const;
    std::size_t visited() const { return index_.visited(); }
private:
    spatial::Index index_;
    std::vector<spatial::Index::Entry> last_;
};
} // namespace sg::render
