#include "sg/spatial/Geometry.hpp"
#include <stdexcept>

namespace sg::spatial {
Transform Transform::inverse() const {
    const double det = dot(linear.col(0), cross(linear.col(1), linear.col(2)));
    if (std::fabs(det) < 1e-18) throw std::invalid_argument("singular spatial transform");
    const M3 r = spatial::inverse(linear);
    return {r, -(r * translation)};
}
Transform Transform::operator*(const Transform& o) const { return {linear * o.linear, point(o.translation)}; }
bool Aabb::empty() const { return lo.x > hi.x || lo.y > hi.y || lo.z > hi.z; }
void Aabb::include(V3 p) {
    lo = {std::min(lo.x,p.x), std::min(lo.y,p.y), std::min(lo.z,p.z)};
    hi = {std::max(hi.x,p.x), std::max(hi.y,p.y), std::max(hi.z,p.z)};
}
void Aabb::include(const Aabb& b) { if (!b.empty()) { include(b.lo); include(b.hi); } }
bool Aabb::contains(V3 p) const { return p.x>=lo.x && p.x<=hi.x && p.y>=lo.y && p.y<=hi.y && p.z>=lo.z && p.z<=hi.z; }
bool Aabb::overlaps(const Aabb& b) const {
    return !empty() && !b.empty() && lo.x<=b.hi.x && hi.x>=b.lo.x && lo.y<=b.hi.y && hi.y>=b.lo.y && lo.z<=b.hi.z && hi.z>=b.lo.z;
}
Aabb Aabb::expanded(double m) const { return empty() ? *this : Aabb{lo-V3{m,m,m}, hi+V3{m,m,m}}; }
Aabb Aabb::transformed(const Transform& t) const {
    Aabb b;
    if (!empty()) for (int i=0;i<8;++i) b.include(t.point({i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z}));
    return b;
}
bool intersects(const Aabb& b, const Ray& r, double* first) {
    if (b.empty()) return false;
    double a=r.near, z=r.far;
    const double lo[]={b.lo.x,b.lo.y,b.lo.z}, hi[]={b.hi.x,b.hi.y,b.hi.z};
    const double o[]={r.origin.x,r.origin.y,r.origin.z}, d[]={r.direction.x,r.direction.y,r.direction.z};
    for (int i=0;i<3;++i) {
        if (d[i]==0) { if(o[i]<lo[i] || o[i]>hi[i]) return false; continue; }
        double p=(lo[i]-o[i])/d[i], q=(hi[i]-o[i])/d[i];
        if(p>q) std::swap(p,q);
        a=std::max(a,p); z=std::min(z,q);
        if(a>z) return false;
    }
    if(first) *first=a;
    return true;
}
HalfSpace HalfSpace::transformed(const Transform& t) const {
    const V3 n=transpose(t.inverse().linear)*normal;
    return {n,offset-dot(n,t.translation)};
}
bool ConvexVolume::contains(V3 p) const {
    for(const auto& h:planes) if(h.at(p)<-1e-10) return false;
    return true;
}
bool ConvexVolume::intersects(const Aabb& b) const {
    if(b.empty()) return false;
    for(const auto& h:planes) {
        const V3 p{h.normal.x>=0?b.hi.x:b.lo.x,h.normal.y>=0?b.hi.y:b.lo.y,h.normal.z>=0?b.hi.z:b.lo.z};
        if(h.at(p)<0) return false;
    }
    return true;
}
bool ConvexVolume::intersects_sphere(V3 c,double r) const {
    for(const auto& h:planes) if(h.at(c)<-r*length(h.normal)) return false;
    return true;
}
ConvexVolume ConvexVolume::transformed(const Transform& t) const {
    ConvexVolume v;
    for(const auto& h:planes) v.planes.push_back(h.transformed(t));
    return v;
}
ConvexVolume ConvexVolume::clip(const std::array<double,16>& m) {
    ConvexVolume v;
    for(int p=0;p<6;++p) {
        const int axis=p/2; const double sign=p%2?-1:1;
        HalfSpace h{{m[3]+sign*m[axis],m[7]+sign*m[4+axis],m[11]+sign*m[8+axis]},m[15]+sign*m[12+axis]};
        const double l=length(h.normal);
        if(l>0) { h.normal=h.normal*(1/l); h.offset/=l; }
        v.planes.push_back(h);
    }
    return v;
}
ConvexVolume Projector::volume() const {
    if(half_x<=0 || half_y<=0 || near<0 || far<near) throw std::invalid_argument("invalid projector cone");
    return ConvexVolume{{{{1,0,half_x},0},{{-1,0,half_x},0},{{0,1,half_y},0},{{0,-1,half_y},0},{{0,0,1},-near},{{0,0,-1},far}}}.transformed(pose);
}
Projection project(const Projector& projector,const Surface& s) {
    Projection result;
    if(s.half_u<=0 || s.half_v<=0) return result;
    auto& poly=result.polygon;
    for(const auto& ab:std::vector<std::pair<double,double>>{{-1,-1},{1,-1},{1,1},{-1,1}})
        poly.push_back(s.centre+s.u*(ab.first*s.half_u)+s.v*(ab.second*s.half_v));
    for(const auto& h:projector.volume().planes) {
        std::vector<V3> next;
        for(std::size_t i=0;i<poly.size();++i) {
            const V3 a=poly[i], b=poly[(i+1)%poly.size()]; const double da=h.at(a), db=h.at(b);
            if(da>=0) next.push_back(a);
            if((da>=0)!=(db>=0)) next.push_back(a+(b-a)*(da/(da-db)));
        }
        poly=std::move(next);
    }
    if(poly.size()<3) { poly.clear(); return result; }
    result.u0=result.v0=1; result.u1=result.v1=0;
    double area=0;
    for(std::size_t i=0;i<poly.size();++i) {
        const V3 p=poly[i]-s.centre, q=poly[(i+1)%poly.size()]-s.centre;
        area+=dot(p,s.u)*dot(q,s.v)-dot(q,s.u)*dot(p,s.v);
        const double u=0.5+dot(p,s.u)/(2*s.half_u), v=0.5+dot(p,s.v)/(2*s.half_v);
        result.u0=std::min(result.u0,u); result.u1=std::max(result.u1,u);
        result.v0=std::min(result.v0,v); result.v1=std::max(result.v1,v);
    }
    if(std::fabs(area)<1e-12) return {};
    return result;
}
} // namespace sg::spatial
