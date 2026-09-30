#include "sg/spatial/Index.hpp"
#include "sg/render/Visibility.hpp"
#include <cstdio>

using namespace sg::spatial;
int main() {
    int failed=0;
    const auto check=[&](bool ok,const char* name) { std::printf("%s %s\n",ok?"ok":"FAIL",name); failed+=!ok; };
    Transform t{from_euler(0.7,-0.3,0.4),{4,5,-2}};
    t.linear=t.linear*M3{{2,0.2,0,0,3,0,0,0,0.5}};
    const V3 p{1,2,3};
    check(length(t.inverse().point(t.point(p))-p)<1e-10,"full affine inverse, including roll, pitch, scale and shear");
    check(length((t*t.inverse()).point(p)-p)<1e-10,"transform composition");
    const HalfSpace plane{{0,1,0},-2};
    const auto moved=plane.transformed(t);
    check(std::fabs(moved.at(t.point({1,2,3})))<1e-10 && moved.at(t.point({1,3,3}))>0,"planes transform by inverse transpose");
    Aabb box{{-1,-2,-3},{1,2,3}};
    const auto bounds=box.transformed(t);
    bool all=true;
    for(int i=0;i<8;++i) all &= bounds.contains(t.point({i&1?1.0:-1.0,i&2?2.0:-2.0,i&4?3.0:-3.0}));
    check(all,"transformed AABB contains every corner");
    double hit=0;
    check(intersects(box,{{-2,0,0},{1,0,0},0,10},&hit) && hit==1 &&
          !intersects(box,{{-2,3,0},{1,0,0},0,10}),"ray slabs handle parallel axes and first distance");
    ConvexVolume volume{{{{1,0,0},1},{{-1,0,0},1},{{0,1,0},1},{{0,-1,0},1},{{0,0,1},1},{{0,0,-1},1}}};
    check(volume.contains({0,0,0}) && !volume.contains({2,0,0}) && volume.intersects(box),"convex volume is shared geometry");
    Index index;
    std::vector<Index::Entry> entries;
    for(std::size_t i=0;i<4096;++i) { const double x=double(i)*4; entries.push_back({i,{{x,-0.5,-0.5},{x+1,0.5,0.5}}}); }
    index.rebuild(entries);
    check(index.query(Aabb{{399.9,-1,-1},{401.1,1,1}})==std::vector<std::size_t>{100} && index.visited()<80,"BVH prunes thousands of unrelated bounds");
    check(index.query(Ray{{400.5,3,0},{0,-1,0},0,5})==std::vector<std::size_t>{100},"ray query preserves numbered leaves");
    entries[100].bounds={{-0.5,-0.5,-0.5},{0.5,0.5,0.5}}; index.refit(entries);
    check(index.query(volume)==std::vector<std::size_t>({0,100}),"refit follows moved bounds");
    entries.erase(entries.begin()+100); index.refit(entries);
    check(index.query(volume)==std::vector<std::size_t>{0},"leaf removal rebuilds without stale results");
    sg::render::Visibility visibility;
    visibility.update(entries);
    check(visibility.visible(volume)==std::vector<std::size_t>{0},"render visibility uses an independent cache");
    Projector projector;
    Surface wall{{0,0,4},{1,0,0},{0,1,0},4,4};
    const auto projected=project(projector,wall);
    check(projected.polygon.size()==4 && std::fabs(projected.u0-0.25)<1e-10 && std::fabs(projected.v1-0.75)<1e-10,"projector cone clips a finite wall and derives crop");
    wall.centre.z=-4;
    check(project(projector,wall).polygon.empty(),"a wall behind the projector has no projected region");
    wall.centre={0,0,4}; wall.u={std::sqrt(0.5),0,std::sqrt(0.5)};
    check(!project(projector,wall).polygon.empty(),"oblique surfaces produce a clipped polygon");
    index.rebuild({});
    check(index.query(volume).empty(),"empty index is empty");
    return failed?1:0;
}
