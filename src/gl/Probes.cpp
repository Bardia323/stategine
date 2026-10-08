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
            Sh9 sh = i < rl->sets.size() ? probe_light(*placed.room, rl->sets[i]) : Sh9{};
            if (i < rl->let_in.size()) sh.add(rl->let_in[i], Rgb{1.0, 1.0, 1.0});
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
// How many relights light let in that keeps changing waits between being
// worked out again.
constexpr uint64_t kLetInEvery = 20;
// How many relights a room is new for: what moves in it is taken once, after.
constexpr uint64_t kWarm = 180;

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

bool same_boxes(const std::vector<GLWorldView::ProbeBox>& a, const std::vector<GLWorldView::ProbeBox>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || length(a[i].mid - b[i].mid) > 1e-9 || length(a[i].half - b[i].half) > 1e-9 ||
            length(a[i].soft_lo - b[i].soft_lo) > 1e-9 || length(a[i].soft_hi - b[i].soft_hi) > 1e-9 || a[i].yaw != b[i].yaw)
            return false;
    return true;
}

}  // namespace

GLWorldView::OwnedBuffer::~OwnedBuffer() {
    if (id) gl::glDeleteBuffers(1, &id);
}

void GLWorldView::see_into(const Spatial3D& room, const Vec3d& at, const Vec3d& forward, const Vec3d& up, float fov, int size,
                           int mode, float znear, const gl::RenderTarget& into, int x, int y) {
    ensure_resources();
    ensure_targets(size, size);
    const std::vector<PlacedRoom> rooms{PlacedRoom{&room, Pose{}, {}}};
    surface_only_ = mode;
    Camera cam;
    cam.eye = to_vec3(at);
    cam.forward = to_vec3(forward);
    cam.up = to_vec3(up);
    cam.fov = fov;
    draw_world(rooms, cam, 1.0f, scene_target_, 0, znear);
    scene_target_.blit_to(resolve_);
    gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, resolve_.framebuffer());
    gl::glBindFramebuffer(gl::GL_DRAW_FRAMEBUFFER, into.framebuffer());
    gl::glBlitFramebuffer(0, 0, size, size, x, y, x + size, y + size, gl::GL_COLOR_BUFFER_BIT, gl::GL_NEAREST);
    surface_only_ = 0;
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
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
    ++rl.calls;
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
    // (While a room is new to the relight - its things settling as it comes
    // to life - what moves is followed but not taken: once, after.)
    const bool warm = rl.calls > kWarm;
    if (rl.restless && ((warm && rl.quiet >= kQuiet) || all_now || !rl.shell_known || rl.calls == kWarm + 1)) {
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

    // The views that draw: the surroundings, and lights' distances (each of
    // its own: one view drawing both would make its pictures again at every
    // turn), what moves left out of both.
    if (!baker_) baker_ = std::make_unique<GLWorldView>(q_);
    if (!lamp_seer_) lamp_seer_ = std::make_unique<GLWorldView>(q_);
    if (!sun_seer_) sun_seer_ = std::make_unique<GLWorldView>(q_);
    GLWorldView& baker = *baker_;
    GLWorldView& seer = *lamp_seer_;
    GLWorldView& sun_seer = *sun_seer_;
    // (Made ready only if one of them draws this time, and put back only then:
    // a relight that draws nothing costs them nothing.)
    bool views_ready = false;
    const auto ready = [&] {
        if (views_ready) return;
        views_ready = true;
        for (GLWorldView* v : {&baker, &seer, &sun_seer}) {
            v->graph_ = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr);
            v->baking_ = true;
            v->solo_ = Key{};  // no lamp: only what the surfaces are is drawn
            v->probe_override_ = nullptr;
            v->hidden_ = &rl.hidden;
            v->lights_memo_.clear();
        }
    };
    // A frame's share of the work: one face of one box, or one face of a
    // lamp's cube (a sun's view, whole), unless all of it is wanted now.
    int jobs = all_now ? 1 << 30 : 1;

    // What each box sees every way - where each surface is, which way it
    // faces, what of the light it scatters - drawn for the shell as it is,
    // into pictures kept on the card (a box a row of six faces), a face a job.
    const int atlas_w = 6 * size, atlas_h = static_cast<int>(boxes.size()) * size;
    for (OwnedTarget* t : {&rl.where, &rl.facing, &rl.scatter, &rl.light})
        if (!t->t.valid() || t->t.width() != atlas_w || t->t.height() != atlas_h) {
            t->t.destroy();
            t->t.create(atlas_w, atlas_h, gl::GL_RGBA32F, 0, false);
        }
    for (OwnedTarget* t : {&rl.sh, &rl.sh2})
        if (!t->t.valid()) t->t.create(9, kMaxProbes * 6, gl::GL_RGBA32F, 0, false);
    for (std::size_t bf = 0; bf < rl.drawn.size() && jobs > 0; ++bf) {
        if (rl.drawn[bf] == rl.shell) continue;
        --jobs;
        rl.drawn[bf] = rl.shell;
        const int bi = static_cast<int>(bf / 6), f = static_cast<int>(bf % 6);
        const Vec3d at = boxes[static_cast<std::size_t>(bi)].mid;
        ready();
        int mode = 1;
        for (const OwnedTarget* t : {&rl.where, &rl.facing, &rl.scatter})
            baker.see_into(room, at, kFace[f], kFaceUp[f], 1.5707963f, size, mode++, kNear, t->t, f * size, bi * size);
    }
    const bool drawn_all = std::all_of(rl.drawn.begin(), rl.drawn.end(), [&](const Digest& d) { return d == rl.shell; });

    // Each light's own light on them, worked out again only when it moved
    // (or the shell did) - and only once every box sees the shell as it is.
    std::map<std::string, DrawLight> now;
    for (const Element* le : index_of(room).lights) {
        if (!le->alive) continue;
        DrawLight L = light_of(room, *le);
        if (L.bounce) continue;  // a stand-in for what the probes hold
        L.power = 26.0f;         // at 1, in white: what it gives now is this times its light now
        L.color = {1.0f, 1.0f, 1.0f};
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
    // And whatever light comes in at its openings from the rooms and worlds
    // beyond - their lamps, their sun, their sky (through_doorways) - each
    // as it is, gated by its opening: whatever an opening is glued to, its
    // light is the room's. (Not of this room, they are kept as they are,
    // in their own colour and strength, and worked out again as they change.)
    std::set<std::string> let_in;
    {
        // (Looked at again every kLetInEvery relights - what is beyond a
        // room's openings is many rooms' worth to look at every frame.)
        if (all_now || rl.let_in_seen == 0 || rl.calls - rl.let_in_seen >= kLetInEvery) {
            rl.let_in_lights = through_doorways(PlacedRoom{&room, Pose{}, {}});
            rl.let_in_seen = rl.calls;
        }
        const std::vector<Light>& in = rl.let_in_lights;
        for (std::size_t k = 0; k < in.size(); ++k) {
            DrawLight L = in[k];
            if (L.hung_only || L.open <= 0.0f || L.power <= 0.0f) continue;
            if (L.sun) {
                const Vec3d d = unit(Vec3d{std::round(L.dir.x * 300.0f) / 300.0, std::round(L.dir.y * 300.0f) / 300.0,
                                           std::round(L.dir.z * 300.0f) / 300.0});
                L.dir = {static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.z)};
                L.pos = {0.0f, 0.0f, 0.0f};
            }
            const std::string name = "in." + std::to_string(L.gate) + "." + std::to_string(k);
            now[name] = L;
            let_in.insert(name);
        }
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
    // What was worked out on the card before, read back now - a frame on, so
    // nothing waits for it (as the eye's exposure is read).
    const auto collect = [&](LampLight& ll) {
        std::vector<float> read(static_cast<std::size_t>(9) * boxes.size() * 6 * 4);
        gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, ll.pbo.id);
        gl::glGetBufferSubData(gl::GL_PIXEL_PACK_BUFFER, 0, static_cast<long>(read.size() * sizeof(float)), read.data());
        gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, 0);
        ll.sets.assign(boxes.size(), Sh9{});
        for (std::size_t bi = 0; bi < boxes.size(); ++bi)
            for (std::size_t c = 0; c < 9; ++c)
                for (std::size_t ch = 0; ch < 3; ++ch) {
                    double sum = 0.0;
                    for (std::size_t f = 0; f < 6; ++f) sum += read[((bi * 6 + f) * 9 + c) * 4 + ch];
                    ll.sets[bi].c[c][ch] = sum;
                }
        ll.pending = false;
        published = true;
    };
    for (auto& [name, ll] : rl.lamps)
        if (ll.pending) collect(ll);
    if (!relight_prog_) relight_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::relight_fs(), "relight");
    if (!relight_sh_prog_) relight_sh_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::relight_sh_fs(), "relight harmonics");
    for (const auto& [name, L] : now) {
        if (!drawn_all || jobs <= 0) break;
        Digest place = place_of(L, rl.shell, size, bounces);
        if (let_in.count(name)) {
            // (Let in: its colour, strength and opening are in its set too.)
            Hasher h;
            h.integer(static_cast<int64_t>(place.hi)).integer(static_cast<int64_t>(place.lo));
            // (In steps: a sun's colour easing with the hour, a door a
            // hair from where it was, are the same light.)
            const auto step = [](double v, double by) { return std::round(v / by); };
            for (double v : {step(L.color.x, 1.0 / 128), step(L.color.y, 1.0 / 128), step(L.color.z, 1.0 / 128),
                             step(L.power, std::max(0.01, L.power / 64.0)), step(L.open, 1.0 / 64), step(L.gate_at.x, 0.01),
                             step(L.gate_at.y, 0.01), step(L.gate_at.z, 0.01), step(L.gate_across.x, 1.0 / 256),
                             step(L.gate_across.z, 1.0 / 256), step(L.gate_w, 0.01), step(L.gate_h, 0.01), step(L.focus.x, 0.05),
                             step(L.focus.y, 0.05), step(L.focus.z, 0.05)})
                h.number(v);
            place = h.digest();
        }
        LampLight& ll = rl.lamps[name];
        ll.let_in = let_in.count(name) > 0;
        if (ll.place == place && (!ll.sets.empty() || ll.pending)) continue;
        // (Light let in that keeps changing - what is beyond keeps moving -
        // is worked out again every so often, not every frame.)
        if (ll.let_in && !ll.sets.empty() && !all_now && rl.calls - ll.done_at < kLetInEvery) continue;
        // Its shadow: how far it sees every way - all but its own fitting,
        // which would otherwise stand in the way of all it lights - a face a
        // job. A sun's: what it reaches, seen from far up its way, narrowly
        // enough to hold what its own shadow holds (its pin, its extent).
        if (ll.drawing != place) {
            ll.drawing = place;
            ll.faces = 0;
        }
        // (A lamp that moved in a room as it was - carried, turned - is drawn
        // whole at once: it may move again before a face a frame were done.
        // Its faces are spread over frames only when the room changed and
        // every lamp is due.)
        if (ll.shell == rl.shell) jobs = std::max(jobs, 6);
        Vec3d from{L.pos.x, L.pos.y, L.pos.z};
        Vec3d sun_f{0, -1, 0}, sun_r{1, 0, 0}, sun_u{0, 0, 1};
        double sun_t = 1.0;
        const int far_size = L.sun ? kSunSize : kLampSize;
        if (L.sun) {
            const Vec3d focus = L.pinned ? Vec3d{L.focus.x, L.focus.y, L.focus.z}
                                         : Vec3d{room.params().num(Key{"room_w"}, 4.0) * 0.5, room.params().num(Key{"room_h"}, 3.0) * 0.5,
                                                 room.params().num(Key{"room_d"}, 4.0) * 0.5};
            sun_f = unit(Vec3d{L.dir.x, L.dir.y, L.dir.z});
            from = focus - sun_f * kSunAway;
            sun_r = unit(cross(sun_f, std::fabs(sun_f.y) > 0.99 ? Vec3d{0, 0, 1} : Vec3d{0, 1, 0}));
            sun_u = cross(sun_r, sun_f);
            sun_t = std::max<double>(L.extent, 1.0) * 1.2 / kSunAway;
        }
        const int far_w = L.sun ? far_size : 6 * far_size;
        if (!ll.far.t.valid() || ll.far.t.width() != far_w || ll.far.t.height() != far_size) {
            ll.far.t.destroy();
            ll.far.t.create(far_w, far_size, gl::GL_RGBA32F, 0, false);
        }
        ready();
        if (L.sun) {
            if (ll.faces < 1) {
                --jobs;
                const Vec3d up = std::fabs(sun_f.y) > 0.99 ? Vec3d{0, 0, 1} : Vec3d{0, 1, 0};
                sun_seer.see_into(room, from, sun_f, up, static_cast<float>(2.0 * std::atan(sun_t)), far_size, 4, 1.0f, ll.far.t, 0, 0);
                ll.faces = 6;
            }
        } else {
            seer.own_lamp_ = Key{name};
            for (; ll.faces < 6 && jobs > 0; ++ll.faces) {
                --jobs;
                seer.see_into(room, from, kFace[ll.faces], kFaceUp[ll.faces], 1.5707963f, far_size, 4, kLampNear, ll.far.t,
                              ll.faces * far_size, 0);
            }
            seer.own_lamp_ = Key{};
        }
        if (ll.faces < 6) break;  // (its next face, next frame)
        ll.place = place;
        ll.shell = rl.shell;
        ll.done_at = rl.calls;
        // Its light on what each box sees, and that taken to harmonics -
        // and again, with the boxes' light it gave them (kept on the card),
        // for the second bounce.
        const gl::Program& P = *relight_prog_;
        const gl::Program& H = *relight_sh_prog_;
        gl::glDisable(gl::GL_DEPTH_TEST);
        gl::glDisable(gl::GL_BLEND);
        const OwnedTarget* before = nullptr;
        for (int b = 0; b < bounces; ++b) {
            const OwnedTarget& into = b % 2 == 0 ? rl.sh : rl.sh2;
            rl.light.t.bind();
            P.use();
            rl.where.t.bind_color(0), rl.facing.t.bind_color(1), rl.scatter.t.bind_color(2), ll.far.t.bind_color(3);
            P.set("uWhere", 0), P.set("uFacing", 1), P.set("uScatter", 2), P.set("uFar", 3);
            P.set("uLightCount", 1), P.set("uShadowCount", 0), P.set("uStraddle", 0.0f);
            lamp_to(P, 0, L);
            P.set("uFarSun", L.sun ? 1 : 0), P.set("uFarSize", far_size);
            P.set("uFarAt", to_vec3(from)), P.set("uFarF", to_vec3(sun_f)), P.set("uFarR", to_vec3(sun_r)), P.set("uFarU", to_vec3(sun_u));
            P.set("uFarT", static_cast<float>(sun_t));
            P.set("uBoxes", static_cast<int>(boxes.size()));
            for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
                const ProbeBox& x = boxes[bi];
                const std::string k = "[" + std::to_string(bi) + "]";
                P.set(("uBoxAt" + k).c_str(), static_cast<float>(x.mid.x), static_cast<float>(x.mid.y), static_cast<float>(x.mid.z),
                      static_cast<float>(x.yaw));
                P.set(("uBoxHalf" + k).c_str(), to_vec3(x.half));
                P.set(("uBoxLo" + k).c_str(), to_vec3(x.soft_lo));
                P.set(("uBoxHi" + k).c_str(), to_vec3(x.soft_hi));
            }
            if (before) before->t.bind_color(4);
            P.set("uBefore", 4);
            P.set("uBounce", before ? 1 : 0);
            screen_.draw();
            into.t.bind();
            gl::glViewport(0, 0, 9, static_cast<int>(boxes.size()) * 6);
            H.use();
            rl.light.t.bind_color(0);
            H.set("uLight", 0), H.set("uSize", size);
            screen_.draw();
            before = &into;
        }
        // The last bounce's harmonics, read into its buffer on the card, to
        // be read back next frame - or now, if all of it is wanted now.
        const long bytes = static_cast<long>(9 * boxes.size() * 6 * 4 * sizeof(float));
        if (!ll.pbo.id) gl::glGenBuffers(1, &ll.pbo.id);
        gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, ll.pbo.id);
        gl::glBufferData(gl::GL_PIXEL_PACK_BUFFER, bytes, nullptr, gl::GL_STREAM_READ);
        gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, before->t.framebuffer());
        gl::glReadPixels(0, 0, 9, static_cast<int>(boxes.size()) * 6, gl::GL_RGBA, gl::GL_FLOAT, nullptr);
        gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, 0);
        ll.pending = true;
        if (all_now) collect(ll);
        gl::glActiveTexture(gl::GL_TEXTURE0);
        gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
    }
    if (views_ready)
        for (GLWorldView* v : {&baker, &seer, &sun_seer}) {
            v->hidden_ = nullptr;
            v->lights_memo_.clear();
            v->shadow_sets_.clear();
            v->room_casters_.clear();
        }
    if (published) {
        rl.sets.assign(boxes.size(), {});
        rl.let_in.assign(boxes.size(), Sh9{});
        for (const auto& [name, ll] : rl.lamps)
            if (ll.sets.size() == boxes.size())
                for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
                    if (ll.let_in) rl.let_in[bi].add(ll.sets[bi], Rgb{1.0, 1.0, 1.0});
                    else rl.sets[bi][name] = ll.sets[bi];
                }
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
