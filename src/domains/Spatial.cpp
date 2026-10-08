#include "sg/domains/Spatial.hpp"

#include <atomic>

#include "sg/domains/Probe.hpp"
#include "sg/domains/Walk.hpp"

#include "sg/spatial/Math.hpp"

namespace sg {

namespace {
using spatial::M3;
spatial::V3 v3(const Vec3d& v) { return {v.x, v.y, v.z}; }
Vec3d vd(const spatial::V3& v) { return {v.x, v.y, v.z}; }
M3 rot(const Pose& p) { return spatial::from_euler(p.yaw, p.pitch, p.roll); }
Pose pose_of(const Vec3d& at, const M3& r) {
    Pose p{at};
    spatial::to_euler(r, p.yaw, p.pitch, p.roll);
    return p;
}
// Where a box turns about: its middle, above its base.
double pivot(const Element& e) {
    return e.kind == kinds::mesh || e.kind == kinds::wall ? e.params.num(keys::sy, 1.0) * 0.5 : 0.0;
}
M3 from_quaternion(double w, double x, double y, double z) {
    const double n = std::sqrt(w * w + x * x + y * y + z * z);
    if (n < 1e-12) return M3{};
    w /= n, x /= n, y /= n, z /= n;
    M3 m;
    m.a = {1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
           2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
           2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)};
    return m;
}
void to_quaternion(const M3& m, double& w, double& x, double& y, double& z) {
    const double t = m(0, 0) + m(1, 1) + m(2, 2);
    if (t > 0) {
        const double s = std::sqrt(t + 1.0) * 2;
        w = 0.25 * s, x = (m(2, 1) - m(1, 2)) / s, y = (m(0, 2) - m(2, 0)) / s, z = (m(1, 0) - m(0, 1)) / s;
    } else if (m(0, 0) > m(1, 1) && m(0, 0) > m(2, 2)) {
        const double s = std::sqrt(1.0 + m(0, 0) - m(1, 1) - m(2, 2)) * 2;
        w = (m(2, 1) - m(1, 2)) / s, x = 0.25 * s, y = (m(0, 1) + m(1, 0)) / s, z = (m(0, 2) + m(2, 0)) / s;
    } else if (m(1, 1) > m(2, 2)) {
        const double s = std::sqrt(1.0 + m(1, 1) - m(0, 0) - m(2, 2)) * 2;
        w = (m(0, 2) - m(2, 0)) / s, x = (m(0, 1) + m(1, 0)) / s, y = 0.25 * s, z = (m(1, 2) + m(2, 1)) / s;
    } else {
        const double s = std::sqrt(1.0 + m(2, 2) - m(0, 0) - m(1, 1)) * 2;
        w = (m(1, 0) - m(0, 1)) / s, x = (m(0, 2) + m(2, 0)) / s, y = (m(1, 2) + m(2, 1)) / s, z = 0.25 * s;
    }
}
const Key kStandW{"stand_w"}, kStandX{"stand_x"}, kStandY{"stand_y"}, kStandZ{"stand_z"};
}  // namespace

Vec3d turn(const Pose& p, const Vec3d& v) {
    if (upright(p)) return rotate_xz(v, p.yaw);
    return vd(rot(p) * v3(v));
}

Vec3d unturn(const Pose& p, const Vec3d& v) {
    if (upright(p)) return rotate_xz(v, -p.yaw);
    return vd(spatial::transpose(rot(p)) * v3(v));
}

Vec3d place_in(const Pose& p, const Vec3d& local) {
    const Vec3d t = turn(p, local);
    return {p.position.x + t.x, p.position.y + t.y, p.position.z + t.z};
}

Vec3d local_of(const Pose& p, const Vec3d& at) {
    return unturn(p, {at.x - p.position.x, at.y - p.position.y, at.z - p.position.z});
}

Pose inverse(const Pose& p) {
    if (upright(p)) {
        const Vec3d back = rotate_xz({-p.position.x, -p.position.y, -p.position.z}, -p.yaw);
        return Pose{back, -p.yaw};
    }
    const M3 rt = spatial::transpose(rot(p));
    return pose_of(vd(rt * v3({-p.position.x, -p.position.y, -p.position.z})), rt);
}

Pose pose_between(const Vec3d& from, const Vec3d& to) {
    const spatial::V3 a = spatial::normalize(v3(from)), b = spatial::normalize(v3(to));
    const double c = std::clamp(spatial::dot(a, b), -1.0, 1.0);
    spatial::V3 axis = spatial::cross(a, b);
    if (spatial::length(axis) < 1e-12) {
        if (c > 0) return Pose{};
        // Opposite: half a turn about anything square to them.
        axis = spatial::cross(a, std::fabs(a.x) < 0.9 ? spatial::V3{1, 0, 0} : spatial::V3{0, 0, 1});
    }
    return pose_of({}, spatial::axis_angle(spatial::normalize(axis), std::acos(c)));
}

Vec3d period_of(const State& s) {
    return {s.params().num(Key{"period_x"}, 0.0), s.params().num(Key{"period_y"}, 0.0), s.params().num(Key{"period_z"}, 0.0)};
}

std::vector<Vec3d> images(const State& s, const Vec3d& at, double reach) {
    const Vec3d p = period_of(s);
    std::vector<Vec3d> out{{0, 0, 0}};
    const auto span = [&](double period, double c) {
        if (period <= 0) return std::pair<int, int>{0, 0};
        return std::pair<int, int>{static_cast<int>(std::floor((c - reach) / period + 0.5)), static_cast<int>(std::ceil((c + reach) / period - 0.5))};
    };
    const auto [x0, x1] = span(p.x, at.x);
    const auto [y0, y1] = span(p.y, at.y);
    const auto [z0, z1] = span(p.z, at.z);
    for (int i = x0; i <= x1; ++i)
        for (int j = y0; j <= y1; ++j)
            for (int k = z0; k <= z1; ++k)
                if (i || j || k) out.push_back({i * p.x, j * p.y, k * p.z});
    return out;
}

Vec3d wrapped(const State& s, const Vec3d& at) {
    const Vec3d p = period_of(s);
    const auto in = [](double c, double period) { return period > 0 ? c - period * std::floor(c / period + 0.5) : c; };
    return {in(at.x, p.x), in(at.y, p.y), in(at.z, p.z)};
}

Pose standing(const Element& camera) {
    if (!camera.params.has(kStandW)) return Pose{};
    return pose_of({}, from_quaternion(camera.params.num(kStandW, 1.0), camera.params.num(kStandX), camera.params.num(kStandY),
                                       camera.params.num(kStandZ)));
}

void set_standing(Element& camera, const Pose& ground) {
    // Level ground is no ground of its own: its turn is a heading. One pose,
    // written one way - so whatever reads only the heading reads it right.
    if (std::fabs(ground.pitch) < 1e-6 && std::fabs(ground.roll) < 1e-6) {
        camera.params.set(keys::yaw, camera.params.num(keys::yaw) + ground.yaw);
        for (Key k : {kStandW, kStandX, kStandY, kStandZ}) camera.params.erase(k);
        return;
    }
    double w, x, y, z;
    to_quaternion(rot(ground), w, x, y, z);
    camera.params.set(kStandW, w).set(kStandX, x).set(kStandY, y).set(kStandZ, z);
}

Pose eye_pose(const Element& camera) {
    const Pose look{position_of(camera), camera.params.num(keys::yaw), camera.params.num(keys::pitch), camera.params.num(keys::roll)};
    if (!camera.params.has(kStandW)) return look;
    return pose_of(look.position, rot(standing(camera)) * rot(look));
}

Vec3d up_of(const Element& camera) { return up_of(eye_pose(camera)); }

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

std::vector<std::string> Spatial3D::faults() const { return probe_faults(*this); }

void Spatial3D::model(Key name, std::vector<float> corners) {
    // (One count for every state's models, from every thread that makes one.)
    static std::atomic<uint64_t> makings{0};
    models_[name.str()] = {std::make_shared<const std::vector<float>>(std::move(corners)), ++makings};
}

const std::vector<float>* Spatial3D::model(Key name) const {
    const auto it = models_.find(name.str());
    return it == models_.end() ? nullptr : it->second.corners.get();
}

uint64_t Spatial3D::model_revision(Key name) const {
    const auto it = models_.find(name.str());
    return it == models_.end() ? 0 : it->second.revision;
}

Element& Spatial3D::terrain(Key id, Height height) {
    Element& e = find(id) ? element(id) : add_element(id, Key{"terrain"});
    grounds_[id.str()] = std::make_shared<const Height>(std::move(height));
    // Drawn again from its new function (the renderer's resample).
    e.params.set("rev", e.params.num("rev", 0.0) + 1.0);
    return e;
}

auto Spatial3D::ground(Key id) const -> const Height* {
    const auto it = grounds_.find(id.str());
    return it == grounds_.end() ? nullptr : it->second.get();
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
    if (camera.params.has(kStandW)) return facing(eye_pose(camera));
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
    Pose p{position_of(e), e.params.num(keys::yaw), e.params.num(keys::pitch), e.params.num(keys::roll)};
    if (!upright(p)) {
        // Turned about its middle: its frame is where its base goes.
        const double h = pivot(e);
        const Vec3d up = turn(p, {0.0, h, 0.0});
        p.position = {p.position.x - up.x, p.position.y + h - up.y, p.position.z - up.z};
    }
    return p;
}

Pose compose_pose(const Pose& parent, const Pose& local) {
    if (upright(parent)) {
        const Vec3d turned = rotate_xz(local.position, parent.yaw);
        return Pose{{parent.position.x + turned.x, parent.position.y + turned.y,
                     parent.position.z + turned.z},
                    parent.yaw + local.yaw, local.pitch, local.roll};
    }
    return pose_of(place_in(parent, local.position), rot(parent) * rot(local));
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
    const Vec3d n = upright(p) ? heading(p.yaw) : facing(p);
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

Pose through_portal(const Pose& here, const Pose& there, const Pose& p) {
    if (upright(here) && upright(there)) {
        const double delta = portal_delta(here.yaw, there.yaw);
        const Vec3d turned = rotate_xz({p.position.x - here.position.x, 0.0, p.position.z - here.position.z}, delta);
        return Pose{{there.position.x + turned.x, p.position.y - here.position.y + there.position.y,
                     there.position.z + turned.z},
                    p.yaw + delta, p.pitch, p.roll};
    }
    // Into the doorway's frame, half round about its up, out of the other's.
    return compose_pose(there, compose_pose(Pose{{}, 3.14159265358979}, compose_pose(inverse(here), p)));
}

Pose through_portal(const Pose& here, const Pose& there, const Vec3d& pos, double yaw) {
    return through_portal(here, there, Pose{pos, yaw});
}

Pose through_ball(const Pose& here, const Pose& there, double k, const Pose& p) {
    Pose q = compose_pose(inverse(here), p);
    q.position = q.position * k;
    return compose_pose(there, compose_pose(Pose{{}, 3.14159265358979}, q));
}

double seam_scale(const Element& here, const Element& there) {
    const double a = here.params.num(Key{"ball"}, 0.0), b = there.params.num(Key{"ball"}, 0.0);
    return a > 0.0 && b > 0.0 ? b / a : 1.0;
}

void carry_camera(const Pose& here, const Pose& there, const Element& src, Element& dst, double scale) {
    // (Through two balls of one size, or any doorway, as it always was.)
    const Pose at = scale == 1.0 ? through_portal(here, there, Pose{position_of(src)}) : through_ball(here, there, scale, Pose{position_of(src)});
    set_position(dst, at.position);
    // The whole of the eye's turn, whatever `dst` held before: the ground it
    // stands on turned as the doorway turns it, the look within it as it was
    // (level ground, a heading: set_standing).
    dst.params.set(keys::yaw, src.params.num(keys::yaw)).set(keys::pitch, src.params.num(keys::pitch));
    dst.params.set(keys::roll, src.params.num(keys::roll));
    set_standing(dst, compose_pose(Pose{{}, at.yaw, at.pitch, at.roll}, standing(src)));
    // What it was moving with goes with it, turned as it is turned.
    if (src.params.has(keys::vx)) {
        const Vec3d v = turn(Pose{{}, at.yaw, at.pitch, at.roll}, {src.params.num(keys::vx), src.params.num(keys::vy), src.params.num(keys::vz)}) * scale;
        dst.params.set(keys::vx, v.x).set(keys::vy, v.y).set(keys::vz, v.z);
    }
    // And whether it is on its feet: a walker that was striding is striding
    // still, not falling, the step after it is through.
    if (src.params.has(Key{"grounded"})) dst.params.set(Key{"grounded"}, src.params.num(Key{"grounded"}));
    // Its lens, whole: what the eye sees with is the eye's, not the far
    // camera's - a far side seen square on (`ortho`) is seen through the
    // doorway as the eye sees, in depth.
    dst.params.set(keys::fov, src.params.num(keys::fov, 70.0));
    for (const Key k : {Key{"ortho"}, Key{"back"}}) {
        if (src.params.has(k)) dst.params.set(k, src.params.num(k));
        else dst.params.erase(k);
    }
}

std::function<void(const Element&, Element&)> portal_carry(const Element& here, const Element& there) {
    const Pose h = local_pose(here), t = local_pose(there);
    return [h, t](const Element& src, Element& dst) { carry_camera(h, t, src, dst); };
}

std::function<void(const Element&, Element&)> portal_carry(const State& a, const Element& here, const State& b, const Element& there) {
    const Pose h = world_pose(a, here), t = world_pose(b, there);
    const double k = seam_scale(here, there);
    return [h, t, k](const Element& src, Element& dst) { carry_camera(h, t, src, dst, k); };
}

namespace {
// A pose carried across a doorway, written to `dst`: its pitch and roll only
// where it is not upright, or had them before.
void write_pose(Element& dst, const Pose& p) {
    set_position(dst, p.position);
    // A turn is said only where there is one (or where it was said before).
    const bool level = std::fabs(p.pitch) < 1e-9 && std::fabs(p.roll) < 1e-9;
    if (level && !dst.params.has(keys::pitch) && !dst.params.has(keys::roll)) {
        dst.params.set(keys::yaw, p.yaw);  // level: a heading
        return;
    }
    dst.params.set(keys::yaw, p.yaw);
    if (std::fabs(p.pitch) >= 1e-9 || dst.params.has(keys::pitch)) dst.params.set(keys::pitch, p.pitch);
    if (std::fabs(p.roll) >= 1e-9 || dst.params.has(keys::roll)) dst.params.set(keys::roll, p.roll);
}
Pose pose_params(const Element& e) {
    return Pose{position_of(e), e.params.num(keys::yaw), e.params.num(keys::pitch), e.params.num(keys::roll)};
}
}  // namespace

std::function<void(const Element&, Element&)> seam_carry(const Element& here, const Element& there) {
    const Pose h = local_pose(here), t = local_pose(there);
    return [h, t](const Element& src, Element& dst) {
        // The same doorway, facing back into its own room.
        write_pose(dst, compose_pose(through_portal(h, t, pose_params(src)), Pose{{}, 3.14159265358979}));
        for (Key k : {keys::w, keys::h})
            if (src.params.has(k)) dst.params.set(k, src.params.num(k));
    };
}

std::function<void(const Element&, Element&)> pose_carry(const Element& here, const Element& there) {
    const Pose h = local_pose(here), t = local_pose(there);
    return [h, t](const Element& src, Element& dst) { write_pose(dst, through_portal(h, t, pose_params(src))); };
}

Params SpatialState::passage() const {
    const Element* eye = find(camera_id());
    return eye ? Params{}.set(keys::x, eye->params.num(keys::x)).set(keys::y, eye->params.num(keys::y)).set(keys::z, eye->params.num(keys::z))
               : Params{};
}

bool SpatialState::passed(const Params& before, Key boundary) const {
    const Element* door = find(boundary);
    const Element* eye = find(camera_id());
    if (!door || !eye || door->kind != kinds::portal || !before.has(keys::x)) return false;
    if (door->params.num(Key{"walk"}, 0.0) < 0.5 || door->params.num(Key{"leave"}, 1.0) < 0.5) return false;
    return crossed_portal(*door, {before.num(keys::x), before.num(keys::y), before.num(keys::z)}, position_of(*eye));
}

void size_opening(Element& door, double w, double h) {
    const double was = door.params.num(keys::h, h);
    const Vec3d lift = up_of(local_pose(door)) * (0.5 * (h - was));
    door.params.set(keys::x, door.params.num(keys::x) + lift.x).set(keys::y, door.params.num(keys::y) + lift.y)
        .set(keys::z, door.params.num(keys::z) + lift.z);
    door.params.set(keys::w, w).set(keys::h, h);
}

Vec3d out_through(const Pose& door, const Vec3d& v) {
    const Vec3d in = facing(door), up = up_of(door), across = across_of(door);
    return {-(v.x * in.x + v.y * in.y + v.z * in.z), v.x * up.x + v.y * up.y + v.z * up.z, -(v.x * across.x + v.y * across.y + v.z * across.z)};
}

void on_level_ground(Params& out, const Pose& door, double h, double (*ground)(const void*, double, double), const void* of) {
    if (!out.has(Key{"down.x"})) {
        const Vec3d d = out_through(door, {0.0, -1.0, 0.0});
        out.set(Key{"down.x"}, d.x).set(Key{"down.y"}, d.y).set(Key{"down.z"}, d.z);
    }
    if (!out.has(Key{"floor"})) {
        const Vec3d foot = place_in(door, {0.05, -0.5 * h, 0.0});
        out.set(Key{"floor"}, foot.y - ground(of, foot.x, foot.z));
    }
}

Params SpatialState::overlap(Key boundary) const {
    const Element* door = find(boundary);
    if (!door || door->kind != kinds::portal) return {};
    Params out;
    out.set(Key{"walk"}, door->params.num(Key{"walk"}, 0.0) > 0.5 ? 1.0 : 0.0);
    out.set(Key{"leave"}, door->params.num(Key{"leave"}, 1.0) > 0.5 ? 1.0 : 0.0);
    // How high whoever crosses carries their eye here (sg::walk's `height`).
    if (const Element* eye = find(camera_id())) out.set(Key{"eye"}, eye->params.num(Key{"height"}, 1.65));
    // A ball (a world round another) has no foot or face to measure from.
    if (door->params.has(Key{"ball"})) return out;
    const Pose at = world_pose(*this, *door);
    const Vec3d in = facing(at), up = up_of(at);
    const double h = door->params.num(keys::h, 2.0);
    const Vec3d foot = at.position + up * (-0.5 * h) + in * 0.05;
    field::Solver pull;
    pull.rebuild(fields_of(*this));
    const auto g = pull.evaluate({foot.x + up.x, foot.y + up.y, foot.z + up.z}, 0.0, {{"gravity", field::Response::Acceleration, 1.0}}).acceleration;
    const double gl = std::sqrt(g.x * g.x + g.y * g.y + g.z * g.z);
    if (gl > 1e-6) {
        const Vec3d d = out_through(at, {g.x / gl, g.y / gl, g.z / gl});
        out.set(Key{"down.x"}, d.x).set(Key{"down.y"}, d.y).set(Key{"down.z"}, d.z);
    }
    Vec3d hit, normal;
    double dist = 0;
    if (ray(*this, foot + up * 0.5, up * -1.0, 1.5, hit, normal, &dist)) out.set(Key{"floor"}, dist - 0.5);
    return out;
}

double portal_inset(const Element& portal) {
    return portal.params.num(Key{"inset"}, portal.params.num(Key{"walk"}, 0.0) > 0.5 ? 0.0 : 0.06);
}

bool opens_from(const Element& portal, const Vec3d& at) {
    if (portal.params.has(Key{"ball"})) return true;
    if (portal.params.num(Key{"walk"}, 0.0) < 0.5 && portal.params.num(Key{"oneway"}, 0.0) < 0.5) return true;
    const Pose p = local_pose(portal);
    const Vec3d face = upright(p) ? heading(p.yaw) : facing(p);
    return (at.x - p.position.x) * face.x + (at.y - p.position.y) * face.y + (at.z - p.position.z) * face.z > 0.0;
}

bool crossed_portal(const Element& portal, const Vec3d& from, const Vec3d& to) {
    if (portal.params.has(Key{"ball"})) {
        const Vec3d c = position_of(portal);
        const double r = portal.params.num("ball");
        const double d0 = distance(from, c), d1 = distance(to, c);
        return portal.params.num("ball_out", 0.0) > 0.5 ? (d0 < r && d1 >= r) : (d0 > r && d1 <= r);
    }
    const Pose at = local_pose(portal);
    const bool level = upright(at);
    const Vec3d p = at.position;
    const Vec3d face = level ? heading(at.yaw) : facing(at);  // the way it faces
    const Vec3d side = level ? across(at.yaw) : across_of(at);  // along the opening
    const Vec3d up = level ? Vec3d{0.0, 1.0, 0.0} : up_of(at);
    const Vec3d f{from.x - p.x, level ? 0.0 : from.y - p.y, from.z - p.z}, g{to.x - p.x, level ? 0.0 : to.y - p.y, to.z - p.z};
    const double d0 = dot(f, face), d1 = dot(g, face);
    if (!(d0 > 0.0 && d1 <= 0.0)) return false;  // only front to back
    const double span = d0 - d1;
    const double t = span > 1e-9 ? d0 / span : 0.0;
    const Vec3d hit{f.x + (g.x - f.x) * t, f.y + (g.y - f.y) * t, f.z + (g.z - f.z) * t};
    const double half_w = portal.params.num(keys::w, 2.0) * 0.5;
    if (std::fabs(dot(hit, side)) > half_w) return false;
    const double half_h = portal.params.num(keys::h, 2.0) * 0.5;
    // Upright, a walker's eye is above the opening's middle: up to its head.
    if (level) return std::fabs(to.y - p.y) <= half_h + 0.9;
    return std::fabs(dot(hit, up)) <= half_h;
}

}  // namespace sg
