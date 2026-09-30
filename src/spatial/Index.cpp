#include "sg/spatial/Index.hpp"
#include <numeric>

namespace sg::spatial {
void Index::rebuild(std::vector<Entry> entries) {
    entries_=std::move(entries); nodes_.clear(); order_.resize(entries_.size());
    std::iota(order_.begin(),order_.end(),0);
    if(!entries_.empty()) { nodes_.reserve(entries_.size()*2-1); build(0,entries_.size()); }
}
int Index::build(std::size_t begin,std::size_t end) {
    const int at=static_cast<int>(nodes_.size()); nodes_.push_back({});
    Aabb bounds;
    for(std::size_t i=begin;i<end;++i) bounds.include(entries_[order_[i]].bounds);
    nodes_[at].bounds=bounds;
    if(end-begin==1) { nodes_[at].entry=order_[begin]; return at; }
    const V3 span=bounds.hi-bounds.lo;
    const int axis=span.x>=span.y && span.x>=span.z?0:span.y>=span.z?1:2;
    const auto centre=[&](std::size_t i) {
        const auto& b=entries_[i].bounds;
        return axis==0?b.lo.x+b.hi.x:axis==1?b.lo.y+b.hi.y:b.lo.z+b.hi.z;
    };
    const auto mid=begin+(end-begin)/2;
    std::nth_element(order_.begin()+begin,order_.begin()+mid,order_.begin()+end,[&](std::size_t a,std::size_t b) {
        const double x=centre(a),y=centre(b); return x<y || (x==y && entries_[a].id<entries_[b].id);
    });
    const int left=build(begin,mid),right=build(mid,end);
    nodes_[at].left=left; nodes_[at].right=right;
    return at;
}
void Index::refit(std::vector<Entry> entries) {
    bool same=entries.size()==entries_.size();
    if(same) for(std::size_t i=0;i<entries.size();++i) if(entries[i].id!=entries_[i].id) { same=false; break; }
    if(!same) { rebuild(std::move(entries)); return; }
    entries_=std::move(entries);
    for(auto it=nodes_.rbegin();it!=nodes_.rend();++it) {
        if(it->left<0) it->bounds=entries_[it->entry].bounds;
        else { it->bounds=nodes_[it->left].bounds; it->bounds.include(nodes_[it->right].bounds); }
    }
}
std::vector<std::size_t> Index::query(const Aabb& b) const { return query_with([&](const Aabb& a) { return a.overlaps(b); }); }
std::vector<std::size_t> Index::query(const ConvexVolume& v) const { return query_with([&](const Aabb& a) { return v.intersects(a); }); }
std::vector<std::size_t> Index::query(const Ray& r) const { return query_with([&](const Aabb& a) { return intersects(a,r); }); }
} // namespace sg::spatial
