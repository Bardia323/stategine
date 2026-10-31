#include "sg/domains/PhysicsCarry.hpp"

#include "sg/domains/Spatial.hpp"

namespace sg::physics {

Carry Carry::between(sg::rigid::V3 from_x, const sg::rigid::M3& from_r, sg::rigid::V3 to_x, const sg::rigid::M3& to_r) {
    Carry c;
    c.from_x = from_x, c.to_x = to_x, c.from_r = from_r, c.to_r = to_r;
    return c;
}

bool Carry::moved() const {
    return sg::rigid::length(to_x - from_x) > 1e-5 || sg::rigid::length(sg::rigid::log_map(to_r * sg::rigid::transpose(from_r))) > 1e-5;
}

void Carry::body(sg::rigid::V3& x, sg::rigid::M3& r) const {
    const sg::rigid::M3 turn = to_r * sg::rigid::transpose(from_r);
    x = to_x + turn * (x - from_x);
    r = sg::rigid::orthonormal(turn * r);
}

void Carry::solid(sg::Element& e) const {
    sg::rigid::V3 x{e.params.num(sg::keys::x), e.params.num(sg::keys::y), e.params.num(sg::keys::z)};
    sg::rigid::M3 r = sg::rigid::from_euler(e.params.num(sg::keys::yaw), e.params.num(sg::keys::pitch), e.params.num(sg::keys::roll));
    body(x, r);
    double yaw = 0, pitch = 0, roll = 0;
    sg::rigid::to_euler(r, yaw, pitch, roll);
    e.params.set(sg::keys::x, x.x).set(sg::keys::y, x.y).set(sg::keys::z, x.z);
    e.params.set(sg::keys::yaw, yaw).set(sg::keys::pitch, pitch).set(sg::keys::roll, roll);
}

void Carry::flat(sg::Element& e) const {
    double y0 = 0, p = 0, r = 0, y1 = 0;
    sg::rigid::to_euler(from_r, y0, p, r);
    sg::rigid::to_euler(to_r, y1, p, r);
    const double turn = y1 - y0;
    const sg::Vec3d rel = sg::rotate_xz({e.params.num(sg::keys::x) - from_x.x, 0.0, e.params.num(sg::keys::z) - from_x.z}, turn);
    e.params.set(sg::keys::x, to_x.x + rel.x).set(sg::keys::z, to_x.z + rel.z);
    e.params.set(sg::keys::y, e.params.num(sg::keys::y) + (to_x.y - from_x.y));
    e.params.set(sg::keys::yaw, e.params.num(sg::keys::yaw) + turn);
}

}  // namespace sg::physics
