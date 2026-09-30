#include "sg/render/Visibility.hpp"
namespace sg::render {
void Visibility::update(std::vector<spatial::Index::Entry> bounds) {
    bool same=bounds.size()==last_.size();
    if(same) for(std::size_t i=0;i<bounds.size();++i) {
        const auto& a=bounds[i]; const auto& b=last_[i];
        if(a.id!=b.id || a.bounds.lo.x!=b.bounds.lo.x || a.bounds.lo.y!=b.bounds.lo.y || a.bounds.lo.z!=b.bounds.lo.z ||
           a.bounds.hi.x!=b.bounds.hi.x || a.bounds.hi.y!=b.bounds.hi.y || a.bounds.hi.z!=b.bounds.hi.z) { same=false; break; }
    }
    if(same) return;
    // Pose changes refit existing leaves; additions/removals rebuild in Index.
    index_.refit(bounds); last_=std::move(bounds);
}
std::vector<std::size_t> Visibility::visible(const spatial::ConvexVolume& volume) const { return index_.query(volume); }
} // namespace sg::render
