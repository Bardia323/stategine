#include "sg/domains/Spatial.hpp"

namespace sg {

SpatialState::SpatialState(Key id, int dims) : State(id), dims_(dims < 3 ? 2 : 3) {
    step_event_ = dims_ == 3 ? Key{"space3.step"} : Key{"space2.step"};
    Element& cam = add_element(camera_id(), kinds::camera);
    cam.params.set(keys::x, 0.0).set(keys::y, 0.0).set(keys::z, 0.0);
    cam.params.set(keys::yaw, 0.0).set(keys::pitch, 0.0).set(keys::fov, 70.0);
}

Element& SpatialState::body(Key id, Vec3d pos, Key kind, char glyph) {
    Element& e = add_element(id, kind.empty() ? default_kind() : kind);
    set_position(e, pos);
    e.params.set(keys::vx, 0.0).set(keys::vy, 0.0).set(keys::vz, 0.0);
    e.params.set(keys::glyph, std::string(1, glyph));
    add_integrator(id);
    return e;
}

const Morphism& SpatialState::add_integrator(Key id) {
    Affine a;
    const auto axis = [&](Key p, Key v) {
        a.set(p, {Affine::Term{p, 1.0, Key{}, true}, Affine::Term{v, 1.0, keys::dt, true}});
    };
    axis(keys::x, keys::vx);
    axis(keys::y, keys::vy);
    if (dims_ == 3) axis(keys::z, keys::vz);
    return affine(Key{"move." + id.str()}, id, step_event_, std::move(a));
}

void SpatialState::on_update(const Tick& t) {
    if (!integrate_) return;
    emit(Event{step_event_, Params{}.set(keys::dt, t.dt)});
}

Spatial2D::Spatial2D(Key id, int cols, int rows) : SpatialState(id, 2) {
    params().set(keys::w, static_cast<int64_t>(cols));
    params().set(keys::h, static_cast<int64_t>(rows));
}

Element& Spatial2D::sprite(Key id, double x, double y, char glyph) {
    return body(id, {x, y, 0.0}, kinds::sprite, glyph);
}

Spatial3D::Spatial3D(Key id) : SpatialState(id, 3) {
    camera().params.set(keys::z, -10.0);
    // camera --orbit--> camera
    loop(Key{"orbit"}, camera_id(), Key{"space3.orbit"},
         [](State&, Element& c, Element*, const Event& ev) {
             c.params.set(keys::yaw, c.params.num(keys::yaw) + ev.args.num(Key{"dyaw"}));
             c.params.set(keys::pitch, c.params.num(keys::pitch) + ev.args.num(Key{"dpitch"}));
         });
}

Element& Spatial3D::mesh(Key id, double x, double y, double z, char glyph) {
    return body(id, {x, y, z}, kinds::mesh, glyph);
}

Element& Spatial3D::fixture(Key id, double x, double y, double z) {
    Element& e = add_element(id, kinds::mesh);
    set_position(e, {x, y, z});
    return e;
}

Element& Spatial3D::light(Key id, Vec3d pos, double r, double g, double b) {
    Element& e = add_element(id, kinds::light);
    set_position(e, pos);
    e.params.set(keys::r, r).set(keys::g, g).set(keys::b, b);
    e.params.set(keys::intensity, 1.0);
    return e;
}

Element& Spatial3D::wall(Key id, Vec3d centre, double sx, double sy, double sz, double yaw) {
    Element& e = add_element(id, kinds::wall);
    set_position(e, centre);
    e.params.set(keys::sx, sx).set(keys::sy, sy).set(keys::sz, sz).set(keys::yaw, yaw);
    return e;
}

Element& Spatial3D::anchor(Key id, Vec3d pos, double yaw) {
    Element& e = add_element(id, kinds::anchor);
    set_position(e, pos);
    e.params.set(keys::yaw, yaw);
    return e;
}

Element& Spatial3D::portal(Key id, Vec3d pos, double width, double height, double yaw) {
    Element& e = add_element(id, kinds::portal);
    set_position(e, pos);
    e.params.set(keys::w, width).set(keys::h, height).set(keys::yaw, yaw);
    e.params.set(keys::open, false);
    return e;
}

void Spatial3D::model(Key name, std::vector<float> corners) {
    models_[name.str()] = std::make_shared<const std::vector<float>>(std::move(corners));
}

const std::vector<float>* Spatial3D::model(Key name) const {
    const auto it = models_.find(name.str());
    return it == models_.end() ? nullptr : it->second.get();
}

void Spatial3D::picture(Key name, int w, int h, std::vector<unsigned char> rgba) {
    auto& p = pictures_[name.str()];
    if (!p) p = std::make_shared<Picture>();
    p->w = w, p->h = h, p->rgba = std::move(rgba);
    p->revision = ++picture_revisions_;
}

auto Spatial3D::picture(Key name) const -> const Picture* {
    const auto it = pictures_.find(name.str());
    return it == pictures_.end() ? nullptr : it->second.get();
}

Vec3d forward_of(const Element& camera) {
    const double pitch = camera.params.num(keys::pitch);
    const Vec3d flat = heading(camera.params.num(keys::yaw));
    const double cp = std::cos(pitch);
    return {flat.x * cp, std::sin(pitch), flat.z * cp};
}

double distance(const Vec3d& a, const Vec3d& b) {
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Pose local_pose(const Element& e) {
    return Pose{position_of(e), e.params.num(keys::yaw)};
}

Pose compose_pose(const Pose& parent, const Pose& local) {
    const Vec3d turned = rotate_xz(local.position, parent.yaw);
    return Pose{{parent.position.x + turned.x, parent.position.y + turned.y,
                 parent.position.z + turned.z},
                parent.yaw + local.yaw};
}

Pose world_pose(const State& s, const Element& e) {
    Pose p = local_pose(e);
    const Element* cur = &e;
    std::vector<const Element*> seen{cur};
    for (;;) {
        // (The parent's name read where it is, not copied out.)
        if (!cur->params.has(keys::parent)) break;
        const std::string* parent_id = std::get_if<std::string>(&cur->params.get(keys::parent));
        if (!parent_id || parent_id->empty()) break;
        const Element* parent = s.find(Key{*parent_id});
        if (!parent || std::find(seen.begin(), seen.end(), parent) != seen.end()) break;
        seen.push_back(parent);
        p = compose_pose(local_pose(*parent), p);
        cur = parent;
    }
    return p;
}

Element& attach_to(Element& e, Key anchor) {
    e.params.set(keys::parent, anchor.str());
    return e;
}

double distance_to(const SpatialState& s, Key element_id) {
    const Element* e = s.find(element_id);
    if (!e) return 1e9;
    return distance(world_position(s, s.element(SpatialState::camera_id())),
                    world_position(s, *e));
}

bool looking_at(const SpatialState& s, Key element_id, double max_dist, double min_facing) {
    const Element* e = s.find(element_id);
    if (!e) return false;
    const Element& cam = s.element(SpatialState::camera_id());
    const Vec3d eye = world_position(s, cam);
    const Vec3d target = world_position(s, *e);
    const Vec3d d{target.x - eye.x, target.y - eye.y, target.z - eye.z};
    const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len > max_dist || len < 1e-6) return len <= max_dist;
    const Vec3d f = forward_of(cam);
    return (d.x * f.x + d.y * f.y + d.z * f.z) / len >= min_facing;
}

double HalfSpace::at(const Vec3d& p) const {
    return normal.x * p.x + normal.y * p.y + normal.z * p.z + offset;
}

HalfSpace room_side(const State& room, const Element& portal, const Pose& placed) {
    const Pose p = compose_pose(placed, world_pose(room, portal));
    const Vec3d n = heading(p.yaw);
    return HalfSpace{n, -(n.x * p.position.x + n.y * p.position.y + n.z * p.position.z)};
}

void resolve_wall_collisions(const State& s, Element& mover, double radius, double head, const Pose& frame) {
    for (const auto& e : s.elements()) {
        if (e.kind != kinds::wall || !e.alive) continue;
        // `frame` places the whole room: a neighbouring room's walls are solid
        // too, and they are solid where that room actually sits relative to
        // the one the walker is standing in.
        const Pose w = compose_pose(frame, world_pose(s, e));
        if (w.position.y >= head) continue;

        const double hx = e.params.num(keys::sx, 1.0) * 0.5 + radius;
        const double hz = e.params.num(keys::sz, 1.0) * 0.5 + radius;

        // Into the wall's own frame.
        const Vec3d p = position_of(mover);
        const Vec3d local = rotate_xz({p.x - w.position.x, 0.0, p.z - w.position.z}, -w.yaw);
        const double lx = local.x, lz = local.z;
        if (std::fabs(lx) >= hx || std::fabs(lz) >= hz) continue;

        // Out through whichever face is closest.
        const double push_x = hx - std::fabs(lx);
        const double push_z = hz - std::fabs(lz);
        double ox = 0, oz = 0;
        if (push_x < push_z) {
            ox = lx >= 0 ? push_x : -push_x;
        } else {
            oz = lz >= 0 ? push_z : -push_z;
        }
        const Vec3d push = rotate_xz({ox, 0.0, oz}, w.yaw);
        mover.params.set(keys::x, p.x + push.x);
        mover.params.set(keys::z, p.z + push.z);
    }
}

double portal_delta(const Element& here, const Element& there) {
    return portal_delta(here.params.num(keys::yaw), there.params.num(keys::yaw));
}

Pose through_portal(const Pose& here, const Pose& there, const Vec3d& pos, double yaw) {
    const double delta = portal_delta(here.yaw, there.yaw);
    const Vec3d turned =
        rotate_xz({pos.x - here.position.x, 0.0, pos.z - here.position.z}, delta);
    return Pose{{there.position.x + turned.x, pos.y - here.position.y + there.position.y,
                 there.position.z + turned.z},
                yaw + delta};
}

std::function<void(const Element&, Element&)> portal_carry(const Element& here, const Element& there) {
    const Vec3d hp = position_of(here);
    const Vec3d tp = position_of(there);
    const double delta = portal_delta(here, there);
    return [hp, tp, delta](const Element& src, Element& dst) {
        const Vec3d p = position_of(src);
        const Vec3d turned = rotate_xz({p.x - hp.x, 0.0, p.z - hp.z}, delta);
        set_position(dst, {tp.x + turned.x, p.y - hp.y + tp.y, tp.z + turned.z});
        dst.params.set(keys::yaw, src.params.num(keys::yaw) + delta);
        dst.params.set(keys::pitch, src.params.num(keys::pitch));
        dst.params.set(keys::fov, src.params.num(keys::fov, 70.0));
    };
}

std::function<void(const Element&, Element&)> seam_carry(const Element& here, const Element& there) {
    const Vec3d hp = position_of(here), tp = position_of(there);
    const double delta = portal_delta(here, there);
    return [hp, tp, delta](const Element& src, Element& dst) {
        const Vec3d p = position_of(src);
        const Vec3d turned = rotate_xz({p.x - hp.x, 0.0, p.z - hp.z}, delta);
        set_position(dst, {tp.x + turned.x, p.y - hp.y + tp.y, tp.z + turned.z});
        dst.params.set(keys::yaw, src.params.num(keys::yaw) + delta + 3.14159265358979);
        for (Key k : {keys::w, keys::h})
            if (src.params.has(k)) dst.params.set(k, src.params.num(k));
    };
}

std::function<void(const Element&, Element&)> pose_carry(const Element& here, const Element& there) {
    const Vec3d hp = position_of(here), tp = position_of(there);
    const double delta = portal_delta(here, there);
    return [hp, tp, delta](const Element& src, Element& dst) {
        const Vec3d p = position_of(src);
        const Vec3d turned = rotate_xz({p.x - hp.x, 0.0, p.z - hp.z}, delta);
        set_position(dst, {tp.x + turned.x, p.y - hp.y + tp.y, tp.z + turned.z});
        dst.params.set(keys::yaw, src.params.num(keys::yaw) + delta);
    };
}

bool crossed_portal(const Element& portal, const Vec3d& from, const Vec3d& to) {
    const Vec3d p = position_of(portal);
    const double yaw = portal.params.num(keys::yaw);
    const Vec3d face = heading(yaw);  // the way it faces
    const Vec3d side = across(yaw);   // along the opening
    const double nx = face.x, nz = face.z, tx = side.x, tz = side.z;
    const double d0 = (from.x - p.x) * nx + (from.z - p.z) * nz;
    const double d1 = (to.x - p.x) * nx + (to.z - p.z) * nz;
    if (!(d0 > 0.0 && d1 <= 0.0)) return false;  // only front to back
    const double span = d0 - d1;
    const double t = span > 1e-9 ? d0 / span : 0.0;
    const double hx = from.x + (to.x - from.x) * t;
    const double hz = from.z + (to.z - from.z) * t;
    const double lateral = (hx - p.x) * tx + (hz - p.z) * tz;
    const double half_w = portal.params.num(keys::w, 2.0) * 0.5;
    if (std::fabs(lateral) > half_w) return false;
    const double half_h = portal.params.num(keys::h, 2.0) * 0.5;
    return std::fabs(to.y - p.y) <= half_h + 0.9;
}

}  // namespace sg
