// The GL view's light probes: a room's probes on the scene program, and
// their bake - the room seen every way from each, lamp by lamp, taken to
// spherical harmonics and handed back as data (sg/domains/Probe.hpp).
#include "sg/gl/World.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <sstream>

#include "sg/core/Cache.hpp"

#include "SG_PROBE_CODE.hpp"

namespace sg::render {

namespace {

struct ProbeNames {
    std::vector<std::string> at, half, lo, hi, sh;
};

const ProbeNames& probe_names(int most) {
    static const ProbeNames n = [most] {
        ProbeNames out;
        for (int i = 0; i < most; ++i) {
            const std::string k = "[" + std::to_string(i) + "]";
            out.at.push_back("uProbeAt" + k), out.half.push_back("uProbeHalf" + k);
            out.lo.push_back("uProbeSoftLo" + k), out.hi.push_back("uProbeSoftHi" + k);
        }
        for (int j = 0; j < most * 9; ++j) out.sh.push_back("uProbeSH[" + std::to_string(j) + "]");
        return out;
    }();
    return n;
}

// The faces of a cube round a point, as a camera sees each: which way, and up.
const Vec3d kFace[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
const Vec3d kFaceUp[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};

// Where a probe's light is gathered: the middle of its box.
Vec3d middle_of(const State& room, const Element& probe) {
    const Pose p = world_pose(room, probe);
    return {p.position.x, p.position.y + 0.5 * probe.params.num(keys::sy, 1.0), p.position.z};
}

// Everything a room's bake sees - every element but its eye, every param -
// so a bake kept on disk is taken only for the room as it is.
void feed_room(Hasher& h, const State& room) {
    for (const char* k : {"room_w", "room_d", "room_h", "sky"}) h.number(room.params().num(Key{k}, 0.0));
    for (const Element& e : room.elements()) {
        if (!e.alive || e.kind == kinds::camera) continue;
        h.text(e.id.str()).text(e.kind.str());
        for (const auto& [k, v] : e.params) {
            h.text(k.str());
            if (const double* d = std::get_if<double>(&v)) h.number(*d);
            else if (const int64_t* i = std::get_if<int64_t>(&v)) h.integer(*i);
            else if (const bool* b = std::get_if<bool>(&v)) h.integer(*b ? 1 : 0);
            else if (const std::string* s = std::get_if<std::string>(&v)) h.text(*s);
        }
    }
}

}  // namespace

const std::map<std::string, Sh9>& GLWorldView::sets_of(const Element& probe) const {
    ProbeSets& s = probe_sets_[&probe];
    if (s.stamp != probe.params.stamp()) s.stamp = probe.params.stamp(), s.sets = probe_sets(probe);
    return s.sets;
}

void GLWorldView::probes_to_program(const PlacedRoom& placed) {
    const ProbeNames& names = probe_names(kMaxProbes);
    int count = 0;
    // Relit live (light_rooms): the room's own boxes, or its declared
    // probes that hold no bake - each box's sets times its lamps' light now.
    if (const RoomLight* rl = placed.room && !baking_ ? live_light(*placed.room) : nullptr) {
        for (std::size_t i = 0; i < rl->boxes.size() && count < kMaxProbes; ++i) {
            const ProbeBox& b = rl->boxes[i];
            const Sh9 sh = i < rl->sets.size() ? probe_light(*placed.room, rl->sets[i]) : Sh9{};
            const Pose p = compose_pose(placed.pose, Pose{b.mid, b.yaw, 0.0, 0.0});
            scene_->set(names.at[count].c_str(), static_cast<float>(p.position.x), static_cast<float>(p.position.y),
                        static_cast<float>(p.position.z), static_cast<float>(p.yaw));
            scene_->set(names.half[count].c_str(), gl::Vec3{static_cast<float>(b.half.x), static_cast<float>(b.half.y), static_cast<float>(b.half.z)});
            scene_->set(names.lo[count].c_str(), gl::Vec3{static_cast<float>(b.soft_lo.x), static_cast<float>(b.soft_lo.y), static_cast<float>(b.soft_lo.z)});
            scene_->set(names.hi[count].c_str(), gl::Vec3{static_cast<float>(b.soft_hi.x), static_cast<float>(b.soft_hi.y), static_cast<float>(b.soft_hi.z)});
            for (std::size_t k = 0; k < 9; ++k)
                scene_->set(names.sh[static_cast<std::size_t>(count) * 9 + k].c_str(),
                            gl::Vec3{static_cast<float>(sh.c[k][0]), static_cast<float>(sh.c[k][1]), static_cast<float>(sh.c[k][2])});
            ++count;
        }
        scene_->set("uProbeCount", count);
        return;
    }
    // (A bake's first bounce sees the room lit by its lamp alone: no probe.)
    if (placed.room && (!baking_ || probe_override_)) {
        const auto& probes = index_of(*placed.room).probes;
        for (std::size_t i = 0; i < probes.size() && count < kMaxProbes; ++i) {
            const Element& e = *probes[i];
            if (!e.alive) continue;
            Sh9 sh;
            if (probe_override_) {
                if (i < probe_override_->size()) sh = (*probe_override_)[i];
            } else {
                sh = probe_light(*placed.room, sets_of(e));
            }
            const Pose p = compose_pose(placed.pose, pose_of(*placed.room, e));
            const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
            const double soft = e.params.num(Key{"soft"}, 0.5);
            const auto side = [&](const char* k) { return static_cast<float>(e.params.num(Key{k}, soft)); };
            scene_->set(names.at[count].c_str(), static_cast<float>(p.position.x), static_cast<float>(p.position.y + 0.5 * sy),
                        static_cast<float>(p.position.z), static_cast<float>(p.yaw));
            scene_->set(names.half[count].c_str(), gl::Vec3{static_cast<float>(0.5 * sx), static_cast<float>(0.5 * sy), static_cast<float>(0.5 * sz)});
            scene_->set(names.lo[count].c_str(), gl::Vec3{side("soft.nx"), side("soft.ny"), side("soft.nz")});
            scene_->set(names.hi[count].c_str(), gl::Vec3{side("soft.px"), side("soft.py"), side("soft.pz")});
            for (std::size_t k = 0; k < 9; ++k)
                scene_->set(names.sh[static_cast<std::size_t>(count) * 9 + k].c_str(),
                            gl::Vec3{static_cast<float>(sh.c[k][0]), static_cast<float>(sh.c[k][1]), static_cast<float>(sh.c[k][2])});
            ++count;
        }
    }
    scene_->set("uProbeCount", count);
}

Sh9 GLWorldView::capture(const Spatial3D& room, const Vec3d& at, int size) {
    ensure_resources();
    ensure_targets(size, size);
    const std::vector<PlacedRoom> rooms{PlacedRoom{&room, Pose{}, {}}};
    std::vector<float> px(static_cast<std::size_t>(size) * size * 4);
    Sh9 out;
    const double texel = 2.0 / size;
    for (int f = 0; f < 6; ++f) {
        Camera cam;
        cam.eye = to_vec3(at);
        cam.forward = to_vec3(kFace[f]);
        cam.up = to_vec3(kFaceUp[f]);
        cam.fov = 1.5707963f;
        draw_world(rooms, cam, 1.0f, scene_target_, 0, kNear);
        scene_target_.blit_to(resolve_);
        gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, resolve_.framebuffer());
        gl::glReadPixels(0, 0, size, size, gl::GL_RGBA, gl::GL_FLOAT, px.data());
        // Each pixel a way out of the face (the camera's right, its up),
        // weighed by the solid angle it covers.
        const Vec3d right = unit(cross(kFace[f], kFaceUp[f]));
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const double u = (x + 0.5) * texel - 1.0, v = (y + 0.5) * texel - 1.0;
                const Vec3d d = unit(kFace[f] + right * u + kFaceUp[f] * v);
                const double w = texel * texel / std::pow(1.0 + u * u + v * v, 1.5);
                const float* p = &px[(static_cast<std::size_t>(y) * size + x) * 4];
                out.add(d, Rgb{p[0], p[1], p[2]}, w);
            }
    }
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
    return out;
}

std::vector<ProbeBake> GLWorldView::bake_probes(const Spatial3D& room, int size, int bounces) {
    std::vector<ProbeBake> out;
    const auto& probes = index_of(room).probes;
    std::vector<Key> lamps;
    for (const Element* l : index_of(room).lights)
        if (l->alive && l->params.num(Key{"sun"}) < 0.5 && l->params.num(Key{"indirect"}) < 0.5) lamps.push_back(l->id.key());
    if (probes.empty()) return out;
    size = std::clamp(size, 4, 256);
    bounces = std::clamp(bounces, 1, 4);

    // Kept on disk by everything the bake sees, how it was asked, and the
    // code that bakes it: the same room bakes to the same numbers.
    Hasher h;
    h.text("probes").text(SG_PROBE_CODE).integer(size).integer(bounces);
    feed_room(h, room);
    const Digest key = h.digest();
    std::string kept;
    if (cache::load("probes", key, kept)) {
        std::istringstream in(kept);
        std::string line;
        std::map<std::string, ProbeBake> by;
        while (std::getline(in, line)) {
            const std::size_t a = line.find('\t'), b = line.find('\t', a + 1);
            if (a == std::string::npos || b == std::string::npos) continue;
            Sh9 sh;
            const std::string probe = line.substr(0, a), lamp = line.substr(a + 1, b - a - 1);
            if (!Sh9::parse(line.substr(b + 1), sh)) continue;
            by[probe].sets[lamp] = sh;
        }
        for (const Element* p : probes) {
            if (!p->alive) continue;
            ProbeBake pb = by[p->id.str()];
            pb.probe = p->id.key();
            pb.digest = probe_digest(room, *p);
            out.push_back(std::move(pb));
        }
        return out;
    }

    // (One view bakes every room, kept: its programs are made once.)
    if (!baker_) baker_ = std::make_unique<GLWorldView>(q_);
    GLWorldView& baker = *baker_;
    baker.graph_ = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr);
    baker.baking_ = true;
    // The first bounce, lamp by lamp: the room lit by it alone, seen from
    // each probe; then again with the probes lit by what that saw.
    std::vector<std::vector<Sh9>> seen(lamps.size(), std::vector<Sh9>(probes.size()));
    for (int pass = 0; pass < bounces; ++pass) {
        std::vector<std::vector<Sh9>> next(lamps.size(), std::vector<Sh9>(probes.size()));
        for (std::size_t li = 0; li < lamps.size(); ++li) {
            baker.solo_ = lamps[li];
            baker.lights_memo_.clear();
            baker.probe_override_ = pass > 0 ? &seen[li] : nullptr;
            for (std::size_t pi = 0; pi < probes.size(); ++pi)
                if (probes[pi]->alive) next[li][pi] = baker.capture(room, middle_of(room, *probes[pi]), size);
        }
        seen = std::move(next);
    }
    baker.probe_override_ = nullptr;
    baker.solo_ = Key{};
    baker.lights_memo_.clear();
    baker.shadow_sets_.clear();
    baker.room_casters_.clear();
    std::string bytes;
    for (std::size_t pi = 0; pi < probes.size(); ++pi) {
        if (!probes[pi]->alive) continue;
        ProbeBake pb;
        pb.probe = probes[pi]->id.key();
        pb.digest = probe_digest(room, *probes[pi]);
        for (std::size_t li = 0; li < lamps.size(); ++li) {
            pb.sets[lamps[li].str()] = seen[li][pi];
            bytes += pb.probe.str() + "\t" + lamps[li].str() + "\t" + seen[li][pi].text() + "\n";
        }
        out.push_back(std::move(pb));
    }
    cache::store("probes", key, bytes);
    return out;
}

// --- relighting: the lamps taken out of the bake ------------------------------

namespace {

// How many relights a thing must stand still before it is the room's own
// again (seen by its probes, its place in their surroundings): a chair put
// down is, after a moment; a body breathing, a clock's hand, never.
constexpr uint32_t kSettle = 90;
// And how many relights the room must be quiet - nothing starting to move
// or coming to rest - before what its probes see is taken again.
constexpr uint32_t kQuiet = 60;

// What of a lamp its light on the room depends on (not how bright, nor its
// colour: a set is multiplied by those), and the shell it lights.
Digest place_of(const DrawLight& l, const Digest& shell, int size, int bounces) {
    Hasher h;
    h.text("lamp").integer(static_cast<int64_t>(shell.hi)).integer(static_cast<int64_t>(shell.lo)).integer(size).integer(bounces);
    for (float v : {l.pos.x, l.pos.y, l.pos.z, l.dir.x, l.dir.y, l.dir.z, l.inner, l.outer, l.falloff, l.range, l.frame_w,
                    l.frame_h, l.frame_soft, l.extent})
        h.number(v);
    h.integer(l.sun ? 1 : 0);
    return h.digest();
}

// How much of a lamp's light reaches `p`, and the way to it (`l`): the
// scene shader's light_reach, not let in at a doorway.
double reach_of(const DrawLight& L, const Vec3d& p, Vec3d& l) {
    if (L.sun) {
        l = unit(Vec3d{-L.dir.x, -L.dir.y, -L.dir.z});
        return L.power;
    }
    const Vec3d at{L.pos.x, L.pos.y, L.pos.z};
    const Vec3d to = at - p;
    const double dist = length(to);
    l = to * (1.0 / std::max(dist, 1e-4));
    const Vec3d f = unit(Vec3d{L.dir.x, L.dir.y, L.dir.z});
    double cone;
    if (L.frame_w > 0.0f && L.frame_h > 0.0f) {
        const Vec3d r = std::fabs(f.y) > 0.99 ? Vec3d{1, 0, 0} : unit(cross(f, Vec3d{0, 1, 0}));
        const Vec3d u = cross(r, f), v = p - at;
        const double z = dot(v, f);
        if (z <= 1e-4) return 0.0;
        const double qx = std::fabs(dot(v, r) / z) / std::max<double>(L.frame_w, 1e-4);
        const double qy = std::fabs(dot(v, u) / z) / std::max<double>(L.frame_h, 1e-4);
        const auto smooth = [](double a, double b, double x) {
            const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        };
        const double s = L.frame_soft;
        cone = std::sqrt((1.0 - smooth(1.0 - s, 1.0, qx)) * (1.0 - smooth(1.0 - s, 1.0, qy)));
    } else {
        const double ci = std::cos(L.inner), co = std::cos(L.outer);
        cone = std::clamp((dot(l * -1.0, f) - co) / std::max(ci - co, 1e-4), 0.0, 1.0);
    }
    if (cone <= 0.0) return 0.0;
    const double soft = 1.0 / (1.0 + 0.22 * dist + 0.14 * dist * dist);
    const double square = 1.0 / (1.0 + 2.0 * dist * dist);
    const double window = range_window(static_cast<float>(dist), L.range);
    if (window <= 0.0) return 0.0;
    return L.power * (soft + (square - soft) * L.falloff) * window * cone * cone;
}

// How much probe `e` holds `p`: the scene shader's probe_weight.

// How much a probe's box holds `p`: the scene shader's probe_weight.
double weight_of(const GLWorldView::ProbeBox& b, const Vec3d& p) {
    const Vec3d d = p - b.mid;
    const double c = std::cos(b.yaw), s = std::sin(b.yaw);
    const Vec3d q{c * d.x + s * d.z, d.y, -s * d.x + c * d.z};
    const auto one = [](double lo, double hi, double slo, double shi) {
        double w = std::clamp(1.0 + lo / std::max(slo, 1e-4), 0.0, 1.0) * std::clamp(1.0 + hi / std::max(shi, 1e-4), 0.0, 1.0);
        return w * w * (3.0 - 2.0 * w);
    };
    return one(q.x + b.half.x, b.half.x - q.x, b.soft_lo.x, b.soft_hi.x) * one(q.y + b.half.y, b.half.y - q.y, b.soft_lo.y, b.soft_hi.y) *
           one(q.z + b.half.z, b.half.z - q.z, b.soft_lo.z, b.soft_hi.z);
}

bool same_boxes(const std::vector<GLWorldView::ProbeBox>& a, const std::vector<GLWorldView::ProbeBox>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || length(a[i].mid - b[i].mid) > 1e-9 || length(a[i].half - b[i].half) > 1e-9 ||
            length(a[i].soft_lo - b[i].soft_lo) > 1e-9 || length(a[i].soft_hi - b[i].soft_hi) > 1e-9 || a[i].yaw != b[i].yaw)
            return false;
    return true;
}

// Which face of a cube round a point a way `d` falls on, and where on it
// (0..size), as see_round drew them.
int face_of(const Vec3d& d, int size, int& x, int& y) {
    const double ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
    const int f = ax >= ay && ax >= az ? (d.x > 0 ? 0 : 1) : ay >= az ? (d.y > 0 ? 2 : 3) : (d.z > 0 ? 4 : 5);
    const Vec3d right = unit(cross(kFace[f], kFaceUp[f]));
    const double z = dot(d, kFace[f]);
    const double u = dot(d, right) / z, v = dot(d, kFaceUp[f]) / z;
    x = std::clamp(static_cast<int>((u + 1.0) * 0.5 * size), 0, size - 1);
    y = std::clamp(static_cast<int>((v + 1.0) * 0.5 * size), 0, size - 1);
    return f;
}

}  // namespace

std::vector<float> GLWorldView::see_from(const Spatial3D& room, const Vec3d& at, const Vec3d& forward, const Vec3d& up, float fov,
                                         int size, int mode, float znear) {
    ensure_resources();
    ensure_targets(size, size);
    const std::vector<PlacedRoom> rooms{PlacedRoom{&room, Pose{}, {}}};
    std::vector<float> out(static_cast<std::size_t>(size) * size * 4);
    surface_only_ = mode;
    Camera cam;
    cam.eye = to_vec3(at);
    cam.forward = to_vec3(forward);
    cam.up = to_vec3(up);
    cam.fov = fov;
    draw_world(rooms, cam, 1.0f, scene_target_, 0, znear);
    scene_target_.blit_to(resolve_);
    gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, resolve_.framebuffer());
    gl::glReadPixels(0, 0, size, size, gl::GL_RGBA, gl::GL_FLOAT, out.data());
    surface_only_ = 0;
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
    return out;
}

std::vector<float> GLWorldView::see_round(const Spatial3D& room, const Vec3d& at, int size, int mode, float znear) {
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(size) * size * 4 * 6);
    for (int f = 0; f < 6; ++f) {
        const std::vector<float> face = see_from(room, at, kFace[f], kFaceUp[f], 1.5707963f, size, mode, znear);
        out.insert(out.end(), face.begin(), face.end());
    }
    return out;
}

auto GLWorldView::declared_boxes(const Spatial3D& room) const -> std::vector<ProbeBox> {
    std::vector<ProbeBox> out;
    for (const Element* e : index_of(room).probes) {
        if (!e->alive || static_cast<int>(out.size()) >= kMaxProbes) continue;
        const Pose p = world_pose(room, *e);
        const double sx = e->params.num(keys::sx, 1.0), sy = e->params.num(keys::sy, 1.0), sz = e->params.num(keys::sz, 1.0);
        const double soft = e->params.num(Key{"soft"}, 0.5);
        const auto side = [&](const char* k) { return e->params.num(Key{k}, soft); };
        ProbeBox b;
        b.id = e->id.key();
        b.mid = {p.position.x, p.position.y + 0.5 * sy, p.position.z};
        b.half = {0.5 * sx, 0.5 * sy, 0.5 * sz};
        b.soft_lo = {side("soft.nx"), side("soft.ny"), side("soft.nz")};
        b.soft_hi = {side("soft.px"), side("soft.py"), side("soft.pz")};
        b.yaw = p.yaw;
        out.push_back(b);
    }
    return out;
}

auto GLWorldView::grid_of(const Spatial3D& room) -> std::vector<ProbeBox> {
    // A room that says how big it is and is closed round, unless it says
    // `gi` 0: boxes about two and a half metres across - fewer if that is
    // more than the scene shader takes - from wall to wall and floor to
    // ceiling, each fading into the next and out past the walls.
    std::vector<ProbeBox> out;
    const auto& p = room.params();
    if (p.num(Key{"gi"}, 1.0) < 0.5 || p.num(Key{"enclosed"}, 1.0) < 0.5 || p.num(Key{"sky"}, 0.0) > 0.5 || !p.has(Key{"room_w"}) ||
        !p.has(Key{"room_d"}) || !p.has(Key{"room_h"}))
        return out;
    const double w = p.num(Key{"room_w"}), d = p.num(Key{"room_d"}), h = p.num(Key{"room_h"});
    if (w <= 0.0 || d <= 0.0 || h <= 0.0) return out;
    constexpr double kCell = 2.5;
    int nx = std::clamp(static_cast<int>(std::ceil(w / kCell)), 1, kMaxProbes);
    int nz = std::clamp(static_cast<int>(std::ceil(d / kCell)), 1, kMaxProbes);
    while (nx * nz > kMaxProbes) (w / nx < d / nz ? nx : nz) -= 1;
    const double cw = w / nx, cd = d / nz, soft = std::min({cw, cd, h}) * 0.3;
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < nz; ++j) {
            ProbeBox b;
            b.id = Key{"gi." + std::to_string(i) + "." + std::to_string(j)};
            b.mid = {cw * (i + 0.5), 0.5 * h, cd * (j + 0.5)};
            b.half = {0.5 * cw, 0.5 * h, 0.5 * cd};
            b.soft_lo = b.soft_hi = {soft, soft, soft};
            out.push_back(b);
        }
    return out;
}

auto GLWorldView::relight_room(const Spatial3D& room, const std::vector<ProbeBox>& boxes, int size, int bounces, bool all_now)
    -> RoomLight& {
    RoomLight& rl = room_light_[&room];
    size = std::clamp(size, 4, 256);
    bounces = std::clamp(bounces, 1, 4);
    constexpr int kLampSize = 64;       // a lamp's cube of distances, a face
    constexpr float kLampNear = 0.1f;   // where it begins, as a lamp's shadow map does
    constexpr int kSunSize = 256;       // a sun's view down its way
    constexpr double kSunAway = 60.0;   // from how far up it
    const auto& things = room.elements();

    if (!same_boxes(rl.boxes, boxes) || rl.size != size || rl.bounces != bounces) {
        rl.boxes = boxes;
        rl.size = size;
        rl.bounces = bounces;
        rl.of.assign(boxes.size() * 6, {});
        rl.drawn.assign(boxes.size() * 6, Digest{});
        rl.lamps.clear();
        rl.sets.assign(boxes.size(), {});
        ++rl.revision;
    }
    if (boxes.empty()) return rl;
    // Everything at once the first time (a room is seen whole from its first
    // frame), or when asked; else a frame's share.
    if (std::all_of(rl.drawn.begin(), rl.drawn.end(), [](const Digest& d) { return d == Digest{}; })) all_now = true;

    // What moves: a thing whose params moved is out of what the probes see
    // until it has stood still kSettle relights - and so is all that hangs
    // from it. What the probes see is taken again only when the room has
    // been quiet a while (kQuiet): things settling one by one (a cord's
    // links) make one change, not one each.
    const auto tracked = [](const Element& e) { return e.kind != kinds::camera && e.kind != kinds::light && e.kind != kinds::probe; };
    bool restless = false;  // which things move is not what the probes see
    if (rl.structure != room.structure() || rl.stamps.size() != things.size()) {
        rl.structure = room.structure();
        rl.stamps.assign(things.size(), ~uint64_t{0});
        rl.still.assign(things.size(), kSettle);
        rl.index.clear();
        for (std::size_t i = 0; i < things.size(); ++i) rl.index.emplace(things[i].id, i);
        restless = true;
        rl.quiet = kQuiet;  // (a new make-up is taken at once)
    }
    for (std::size_t i = 0; i < things.size(); ++i) {
        const Element& e = things[i];
        if (!tracked(e)) continue;
        const uint64_t stamp = (e.params.stamp() << 1) | (e.alive ? 1u : 0u);
        if (rl.stamps[i] == ~uint64_t{0}) {
            rl.stamps[i] = stamp;  // (as it was found: standing)
            continue;
        }
        if (stamp != rl.stamps[i]) {
            rl.stamps[i] = stamp;
            if (rl.still[i] >= kSettle) restless = true, rl.quiet = 0;
            rl.still[i] = 0;
            continue;
        }
        if (rl.still[i] < kSettle && ++rl.still[i] == kSettle) restless = true, rl.quiet = 0;
    }
    rl.restless = rl.restless || restless;
    if (rl.quiet < kQuiet) ++rl.quiet;
    if (rl.restless && (rl.quiet >= kQuiet || all_now || !rl.shell_known)) {
        rl.restless = false;
        rl.hidden.clear();
        for (std::size_t i = 0; i < things.size(); ++i) {
            if (!tracked(things[i])) continue;
            std::size_t at = i;
            for (int k = 0; k < 8; ++k) {
                if (tracked(things[at]) && rl.still[at] < kSettle) {
                    rl.hidden.insert(things[i].id.key());
                    break;
                }
                const std::string* parent = things[at].params.text(keys::parent);
                const auto up = parent && !parent->empty() ? rl.index.find(Key{*parent}) : rl.index.end();
                if (up == rl.index.end()) break;
                at = up->second;
            }
        }
        // The shell: the room's own and what stands still in it, as it is.
        Hasher h;
        h.text("surroundings").text(SG_PROBE_CODE).integer(size);
        for (const char* k : {"room_w", "room_d", "room_h", "sky"}) h.number(room.params().num(Key{k}, 0.0));
        for (const Element& e : things) {
            if (!e.alive || !tracked(e) || rl.hidden.count(e.id.key())) continue;
            h.text(e.id.str()).text(e.kind.str());
            for (const auto& [k, v] : e.params) {
                h.text(k.str());
                if (const double* d = std::get_if<double>(&v)) h.number(*d);
                else if (const int64_t* n = std::get_if<int64_t>(&v)) h.integer(*n);
                else if (const bool* b = std::get_if<bool>(&v)) h.integer(*b ? 1 : 0);
                else if (const std::string* s = std::get_if<std::string>(&v)) h.text(*s);
            }
        }
        for (const ProbeBox& b : boxes) h.number(b.mid.x).number(b.mid.y).number(b.mid.z);
        rl.shell = h.digest();
        rl.shell_known = true;
    }

    // The views that draw: the surroundings, and lamps' cubes of distances
    // (each of its own: one view drawing both would make its pictures again
    // at every turn), what moves left out of both.
    if (!baker_) baker_ = std::make_unique<GLWorldView>(q_);
    if (!lamp_seer_) lamp_seer_ = std::make_unique<GLWorldView>(q_);
    GLWorldView& baker = *baker_;
    GLWorldView& seer = *lamp_seer_;
    for (GLWorldView* v : {&baker, &seer}) {
        v->graph_ = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr);
        v->baking_ = true;
        v->solo_ = Key{};  // no lamp: only what the surfaces are is drawn
        v->probe_override_ = nullptr;
        v->hidden_ = &rl.hidden;
        v->lights_memo_.clear();
    }
    // A frame's share of the work: one face of one box, or one face of a
    // lamp's cube (a sun's view, whole), unless all of it is wanted now.
    int jobs = all_now ? 1 << 30 : 1;

    // What each box sees every way, drawn for the shell as it is, a face a job.
    const double texel = 2.0 / size;
    for (std::size_t bf = 0; bf < rl.drawn.size() && jobs > 0; ++bf) {
        if (rl.drawn[bf] == rl.shell) continue;
        --jobs;
        rl.drawn[bf] = rl.shell;
        const std::size_t bi = bf / 6;
        const int f = static_cast<int>(bf % 6);
        const Vec3d at = boxes[bi].mid;
        const std::vector<float> where = baker.see_from(room, at, kFace[f], kFaceUp[f], 1.5707963f, size, 1);
        const std::vector<float> facing = baker.see_from(room, at, kFace[f], kFaceUp[f], 1.5707963f, size, 2);
        const std::vector<float> scatter = baker.see_from(room, at, kFace[f], kFaceUp[f], 1.5707963f, size, 3);
        std::vector<ProbeTexel>& of = rl.of[bf];
        of.clear();
        const Vec3d right = unit(cross(kFace[f], kFaceUp[f]));
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4;
                const Vec3d n{facing[i], facing[i + 1], facing[i + 2]};
                if (length(n) < 0.5) continue;  // nothing there
                const double u = (x + 0.5) * texel - 1.0, v = (y + 0.5) * texel - 1.0;
                ProbeTexel t;
                t.at = {where[i], where[i + 1], where[i + 2]};
                t.n = unit(n);
                t.dir = unit(kFace[f] + right * u + kFaceUp[f] * v);
                t.w = texel * texel / std::pow(1.0 + u * u + v * v, 1.5);
                t.albedo = Rgb{scatter[i], scatter[i + 1], scatter[i + 2]};
                // How much each box holds it, as the scene shader blends
                // them (probe_light), and so how much of their light it is given.
                double total = 0.0;
                for (std::size_t bj = 0; bj < boxes.size() && bj < t.held.size(); ++bj) {
                    t.held[bj] = static_cast<float>(weight_of(boxes[bj], t.at));
                    total += t.held[bj];
                }
                if (total > 0.0)
                    for (float& w : t.held) w = static_cast<float>(w * std::min(total, 1.0) / total);
                of.push_back(t);
            }
    }
    const bool drawn_all = std::all_of(rl.drawn.begin(), rl.drawn.end(), [&](const Digest& d) { return d == rl.shell; });

    // Each lamp's light on them, worked out again only when it moved (or the
    // shell did) - and only once every box sees the shell as it is.
    std::map<std::string, DrawLight> now;
    for (const Element* le : index_of(room).lights) {
        if (!le->alive) continue;
        DrawLight L = light_of(room, *le);
        if (L.bounce) continue;  // a stand-in for what the probes hold
        L.power = 26.0f;         // at 1, in white: what it gives now is this times its light now
        if (L.sun) {
            // (Its way taken in steps of about a fifth of a degree, as its
            // shadow's is: a sun creeping across the sky is relit when it has
            // moved that far, not every frame of the day.)
            const Vec3d d = unit(Vec3d{std::round(L.dir.x * 300.0f) / 300.0, std::round(L.dir.y * 300.0f) / 300.0,
                                       std::round(L.dir.z * 300.0f) / 300.0});
            L.dir = {static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)};
            L.pos = {0.0f, 0.0f, 0.0f};
        }
        now[le->id.str()] = L;
    }
    bool published = false;
    for (auto it = rl.lamps.begin(); it != rl.lamps.end();) {
        if (now.count(it->first)) {
            ++it;
            continue;
        }
        it = rl.lamps.erase(it);  // (gone)
        published = true;
    }
    for (const auto& [name, L] : now) {
        if (!drawn_all || jobs <= 0) break;
        const Digest place = place_of(L, rl.shell, size, bounces);
        LampLight& ll = rl.lamps[name];
        if (ll.place == place && !ll.by_bounce.empty()) continue;
        // Its shadow: how far it sees every way - all but its own fitting,
        // which would otherwise stand in the way of all it lights - a face a
        // job. A sun's: what it reaches, seen from far up its way, narrowly
        // enough to hold what its own shadow holds (its pin, its extent).
        Vec3d lamp{L.pos.x, L.pos.y, L.pos.z};
        Vec3d sun_f{0, -1, 0}, sun_r{1, 0, 0}, sun_u{0, 0, 1};
        double sun_t = 1.0;
        if (ll.drawing != place) {
            ll.drawing = place;
            ll.faces = 0;
            ll.far.clear();
        }
        // (A lamp that moved in a room as it was - carried, turned - is drawn
        // whole at once: it may move again before a face a frame were done.
        // Its faces are spread over frames only when the room changed and
        // every lamp is due.)
        if (ll.shell == rl.shell) jobs = std::max(jobs, 6);
        if (L.sun) {
            const Vec3d focus = L.pinned ? Vec3d{L.focus.x, L.focus.y, L.focus.z}
                                         : Vec3d{room.params().num(Key{"room_w"}, 4.0) * 0.5, room.params().num(Key{"room_h"}, 3.0) * 0.5,
                                                 room.params().num(Key{"room_d"}, 4.0) * 0.5};
            sun_f = unit(Vec3d{L.dir.x, L.dir.y, L.dir.z});
            lamp = focus - sun_f * kSunAway;
            const Vec3d up = std::fabs(sun_f.y) > 0.99 ? Vec3d{0, 0, 1} : Vec3d{0, 1, 0};
            sun_r = unit(cross(sun_f, up));
            sun_u = cross(sun_r, sun_f);
            sun_t = std::max<double>(L.extent, 1.0) * 1.2 / kSunAway;
            if (ll.faces < 1) {
                --jobs;
                ll.far = seer.see_from(room, lamp, sun_f, up, static_cast<float>(2.0 * std::atan(sun_t)), kSunSize, 4, 1.0f);
                ll.faces = 6;
            }
        } else {
            seer.own_lamp_ = Key{name};
            while (ll.faces < 6 && jobs > 0) {
                --jobs;
                const std::vector<float> face =
                    seer.see_from(room, lamp, kFace[ll.faces], kFaceUp[ll.faces], 1.5707963f, kLampSize, 4, kLampNear);
                ll.far.insert(ll.far.end(), face.begin(), face.end());
                ++ll.faces;
            }
            seer.own_lamp_ = Key{};
        }
        if (ll.faces < 6) break;  // (its next face, next frame)
        ll.place = place;
        ll.shell = rl.shell;
        published = true;
        const std::vector<float>& far = ll.far;
        const auto lit = [&](const Vec3d& p) {
            const Vec3d d = p - lamp;
            const double d_len = length(d);
            std::size_t at = 0;
            if (L.sun) {
                const double z = dot(d, sun_f);
                if (z <= 0.0) return false;
                const double u = dot(d, sun_r) / (z * sun_t), v = dot(d, sun_u) / (z * sun_t);
                if (std::fabs(u) >= 1.0 || std::fabs(v) >= 1.0) return true;  // beyond what its shadow holds: lit
                const int x = std::clamp(static_cast<int>((u + 1.0) * 0.5 * kSunSize), 0, kSunSize - 1);
                const int y = std::clamp(static_cast<int>((v + 1.0) * 0.5 * kSunSize), 0, kSunSize - 1);
                at = (static_cast<std::size_t>(y) * kSunSize + x) * 4;
            } else {
                int x = 0, y = 0;
                const int f = face_of(d, kLampSize, x, y);
                at = ((static_cast<std::size_t>(f) * kLampSize + y) * kLampSize + x) * 4;
            }
            const double seen = far[at];
            return seen <= 0.0 || d_len <= seen + 0.05 + 0.03 * (L.sun ? 1.0 : d_len);
        };
        // What it gives each surface a box sees, at once (the first bounce's
        // own light), shadowed.
        std::vector<std::vector<Rgb>> direct(rl.of.size());
        for (std::size_t bf = 0; bf < rl.of.size(); ++bf) {
            direct[bf].reserve(rl.of[bf].size());
            for (const ProbeTexel& t : rl.of[bf]) {
                Vec3d l;
                const double r = reach_of(L, t.at, l);
                const double ndl = dot(t.n, l);
                const double e = r > 0.0 && ndl > 0.0 && lit(t.at) ? r * ndl : 0.0;
                direct[bf].push_back(Rgb{t.albedo.r * e, t.albedo.g * e, t.albedo.b * e});
            }
        }
        ll.far.clear();
        ll.far.shrink_to_fit();
        ll.by_bounce.assign(static_cast<std::size_t>(bounces), std::vector<Sh9>(boxes.size()));
        for (int b = 0; b < bounces; ++b) {
            const std::vector<Sh9>* before = b > 0 ? &ll.by_bounce[static_cast<std::size_t>(b - 1)] : nullptr;
            for (std::size_t bf = 0; bf < rl.of.size(); ++bf) {
                Sh9& sh = ll.by_bounce[static_cast<std::size_t>(b)][bf / 6];
                for (std::size_t ti = 0; ti < rl.of[bf].size(); ++ti) {
                    const ProbeTexel& t = rl.of[bf][ti];
                    Rgb c = direct[bf][ti];
                    if (before) {
                        // And the boxes' light on it, as the scene shader
                        // gives it: blended by how much each holds it.
                        for (std::size_t bj = 0; bj < boxes.size() && bj < t.held.size(); ++bj) {
                            const double w = t.held[bj];
                            if (w <= 0.0) continue;
                            const Rgb e = (*before)[bj].irradiance(t.n);
                            c.r += t.albedo.r * std::max(e.r, 0.0) * w, c.g += t.albedo.g * std::max(e.g, 0.0) * w,
                                c.b += t.albedo.b * std::max(e.b, 0.0) * w;
                        }
                    }
                    sh.add(t.dir, c, t.w);
                }
            }
        }
    }
    for (GLWorldView* v : {&baker, &seer}) {
        v->hidden_ = nullptr;
        v->lights_memo_.clear();
        v->shadow_sets_.clear();
        v->room_casters_.clear();
    }
    if (published) {
        rl.sets.assign(boxes.size(), {});
        for (const auto& [name, ll] : rl.lamps)
            if (!ll.by_bounce.empty())
                for (std::size_t bi = 0; bi < boxes.size(); ++bi) rl.sets[bi][name] = ll.by_bounce.back()[bi];
        ++rl.revision;
    }
    return rl;
}

void GLWorldView::light_rooms(const std::vector<PlacedRoom>& rooms) {
    // Each room seen, relit before anything is drawn (relighting draws):
    // its declared probes that hold no bake, or - declaring none - its own
    // boxes (grid_of). A probe that holds a bake is the bake's. Seen means
    // the rooms asked for and those through their seams, two doorways on:
    // a room is lit the same seen through a doorway as stood in.
    std::vector<const Spatial3D*> seen;
    for (const PlacedRoom& placed : rooms)
        if (placed.room && !placed.image) seen.push_back(placed.room);
    if (const StateGraph* g = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr)) {
        for (std::size_t from = 0, depth = 0, end = seen.size(); depth < 2 && from < end; ++depth, from = end, end = seen.size())
            for (std::size_t i = from; i < end; ++i)
                for (const Seam& sm : g->seams()) {
                    const Key other = sm.a == seen[i]->id() ? sm.b : sm.b == seen[i]->id() ? sm.a : Key{};
                    if (other.empty()) continue;
                    if (const auto* s = dynamic_cast<const Spatial3D*>(g->find(other)))
                        if (std::find(seen.begin(), seen.end(), s) == seen.end()) seen.push_back(s);
                }
    }
    std::set<const Spatial3D*> done;
    for (const Spatial3D* at : seen) {
        if (!done.insert(at).second) continue;
        const Spatial3D& room = *at;
        std::vector<ProbeBox> boxes;
        const auto& declared = index_of(room).probes;
        if (room.params().num(Key{"gi"}, 1.0) < 0.5) {
            // (It says not.)
        } else if (declared.empty()) {
            boxes = grid_of(room);
        } else if (std::none_of(declared.begin(), declared.end(), [&](const Element* e) { return !sets_of(*e).empty(); })) {
            boxes = declared_boxes(room);
        }
        if (boxes.empty()) {
            room_light_.erase(&room);
            continue;
        }
        relight_room(room, boxes, 32, 2, false);
    }
}

const GLWorldView::RoomLight* GLWorldView::live_light(const Spatial3D& room) const {
    const GLWorldView& top = root_ ? *root_ : *this;
    const auto it = top.room_light_.find(&room);
    return it == top.room_light_.end() || it->second.boxes.empty() ? nullptr : &it->second;
}

std::vector<ProbeBake> GLWorldView::relight_probes(const Spatial3D& room, int size, int bounces, bool* changed) {
    std::vector<ProbeBake> out;
    const std::vector<ProbeBox> boxes = declared_boxes(room);
    if (boxes.empty()) {
        if (changed) *changed = false;
        return out;
    }
    const uint64_t was = room_light_[&room].revision;
    const RoomLight& rl = relight_room(room, boxes, size, bounces, true);
    if (changed) *changed = rl.revision != was;
    for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
        const Element* e = room.find(boxes[bi].id);
        if (!e) continue;
        ProbeBake pb;
        pb.probe = boxes[bi].id;
        pb.digest = probe_digest(room, *e);
        pb.sets = bi < rl.sets.size() ? rl.sets[bi] : std::map<std::string, Sh9>{};
        out.push_back(std::move(pb));
    }
    return out;
}

}  // namespace sg::render
