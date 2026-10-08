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

// Everything the surroundings are drawn from but the lamps: the room's
// shell and what stands in it, as feed_room - and each probe only by where
// its middle is (a bake written into it changes nothing it sees).
void feed_shell(Hasher& h, const State& room) {
    for (const char* k : {"room_w", "room_d", "room_h", "sky"}) h.number(room.params().num(Key{k}, 0.0));
    for (const Element& e : room.elements()) {
        if (!e.alive || e.kind == kinds::camera || e.kind == kinds::light) continue;
        h.text(e.id.str()).text(e.kind.str());
        if (e.kind == kinds::probe) {
            const Vec3d m = middle_of(room, e);
            h.number(m.x).number(m.y).number(m.z);
            continue;
        }
        for (const auto& [k, v] : e.params) {
            h.text(k.str());
            if (const double* d = std::get_if<double>(&v)) h.number(*d);
            else if (const int64_t* i = std::get_if<int64_t>(&v)) h.integer(*i);
            else if (const bool* b = std::get_if<bool>(&v)) h.integer(*b ? 1 : 0);
            else if (const std::string* s = std::get_if<std::string>(&v)) h.text(*s);
        }
    }
}

// What of a lamp its light on the room depends on (not how bright, nor its
// colour: a set is multiplied by those).
Digest place_of(const DrawLight& l, const Digest& shell, int size) {
    Hasher h;
    h.text("lamp").integer(static_cast<int64_t>(shell.hi)).integer(static_cast<int64_t>(shell.lo)).integer(size);
    for (float v : {l.pos.x, l.pos.y, l.pos.z, l.dir.x, l.dir.y, l.dir.z, l.inner, l.outer, l.falloff, l.range, l.frame_w,
                    l.frame_h, l.frame_soft})
        h.number(v);
    return h.digest();
}

// How much of a lamp's light reaches `p`, and the way to it (`l`): the
// scene shader's light_reach, for a lamp (not a sun), not let in at a doorway.
double reach_of(const DrawLight& L, const Vec3d& p, Vec3d& l) {
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
double weight_of(const State& room, const Element& e, const Vec3d& p) {
    const Pose pose = world_pose(room, e);
    const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
    const double soft = e.params.num(Key{"soft"}, 0.5);
    const auto side = [&](const char* k) { return std::max(e.params.num(Key{k}, soft), 1e-4); };
    const Vec3d d = p - Vec3d{pose.position.x, pose.position.y + 0.5 * sy, pose.position.z};
    const double c = std::cos(pose.yaw), s = std::sin(pose.yaw);
    const Vec3d q{c * d.x + s * d.z, d.y, -s * d.x + c * d.z};
    const Vec3d half{0.5 * sx, 0.5 * sy, 0.5 * sz};
    const auto one = [](double lo, double hi, double slo, double shi) {
        double w = std::clamp(1.0 + lo / slo, 0.0, 1.0) * std::clamp(1.0 + hi / shi, 0.0, 1.0);
        return w * w * (3.0 - 2.0 * w);
    };
    return one(q.x + half.x, half.x - q.x, side("soft.nx"), side("soft.px")) *
           one(q.y + half.y, half.y - q.y, side("soft.ny"), side("soft.py")) *
           one(q.z + half.z, half.z - q.z, side("soft.nz"), side("soft.pz"));
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

std::vector<float> GLWorldView::see_round(const Spatial3D& room, const Vec3d& at, int size, int mode, float znear) {
    ensure_resources();
    ensure_targets(size, size);
    const std::vector<PlacedRoom> rooms{PlacedRoom{&room, Pose{}, {}}};
    const std::size_t face = static_cast<std::size_t>(size) * size * 4;
    std::vector<float> out(face * 6);
    surface_only_ = mode;
    for (int f = 0; f < 6; ++f) {
        Camera cam;
        cam.eye = to_vec3(at);
        cam.forward = to_vec3(kFace[f]);
        cam.up = to_vec3(kFaceUp[f]);
        cam.fov = 1.5707963f;
        draw_world(rooms, cam, 1.0f, scene_target_, 0, znear);
        scene_target_.blit_to(resolve_);
        gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, resolve_.framebuffer());
        gl::glReadPixels(0, 0, size, size, gl::GL_RGBA, gl::GL_FLOAT, &out[face * static_cast<std::size_t>(f)]);
    }
    surface_only_ = 0;
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
    return out;
}

std::vector<ProbeBake> GLWorldView::relight_probes(const Spatial3D& room, int size, int bounces) {
    std::vector<ProbeBake> out;
    const auto& probes = index_of(room).probes;
    if (probes.empty()) return out;
    size = std::clamp(size, 4, 256);
    bounces = std::clamp(bounces, 1, 4);
    constexpr int kLampSize = 64;      // a lamp's cube of distances, a face
    constexpr float kLampClear = 0.25f;  // and how far out from it that cube begins

    if (!baker_) baker_ = std::make_unique<GLWorldView>(q_);
    GLWorldView& baker = *baker_;
    baker.graph_ = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr);
    baker.baking_ = true;
    baker.solo_ = Key{};  // no lamp: only what the surfaces are is drawn
    baker.probe_override_ = nullptr;
    baker.lights_memo_.clear();
    // (Lamps' cubes are drawn by a view of their own, at their own size: one
    // view drawing both would make its pictures again at every turn.)
    if (!lamp_seer_) lamp_seer_ = std::make_unique<GLWorldView>(q_);
    GLWorldView& seer = *lamp_seer_;
    seer.graph_ = baker.graph_;
    seer.baking_ = true;
    seer.solo_ = Key{};

    // What each probe sees every way, drawn for the shell alone.
    Hasher h;
    h.text("surroundings").text(SG_PROBE_CODE).integer(size);
    feed_shell(h, room);
    const Digest shell = h.digest();
    ProbeSurroundings& sur = surroundings_[&room];
    if (sur.shell != shell || sur.size != size || sur.of.size() != probes.size()) {
        sur.shell = shell;
        sur.size = size;
        sur.of.assign(probes.size(), {});
        lamp_sets_[&room].clear();
        const double texel = 2.0 / size;
        for (std::size_t pi = 0; pi < probes.size(); ++pi) {
            if (!probes[pi]->alive) continue;
            const Vec3d at = middle_of(room, *probes[pi]);
            const std::vector<float> where = baker.see_round(room, at, size, 1);
            const std::vector<float> facing = baker.see_round(room, at, size, 2);
            const std::vector<float> scatter = baker.see_round(room, at, size, 3);
            for (int f = 0; f < 6; ++f) {
                const Vec3d right = unit(cross(kFace[f], kFaceUp[f]));
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        const std::size_t i = ((static_cast<std::size_t>(f) * size + y) * size + x) * 4;
                        const Vec3d n{facing[i], facing[i + 1], facing[i + 2]};
                        if (length(n) < 0.5) continue;  // nothing there: the room's clear colour
                        const double u = (x + 0.5) * texel - 1.0, v = (y + 0.5) * texel - 1.0;
                        ProbeTexel t;
                        t.at = {where[i], where[i + 1], where[i + 2]};
                        t.n = unit(n);
                        t.dir = unit(kFace[f] + right * u + kFaceUp[f] * v);
                        t.w = texel * texel / std::pow(1.0 + u * u + v * v, 1.5);
                        t.albedo = Rgb{scatter[i], scatter[i + 1], scatter[i + 2]};
                        // How much each probe holds it (probe_weight), and
                        // so how much of the probes' light it is given.
                        double total = 0.0;
                        for (std::size_t pj = 0; pj < probes.size() && pj < t.held.size(); ++pj) {
                            t.held[pj] = probes[pj]->alive ? static_cast<float>(weight_of(room, *probes[pj], t.at)) : 0.0f;
                            total += t.held[pj];
                        }
                        if (total > 0.0)
                            for (float& w : t.held) w = static_cast<float>(w * std::min(total, 1.0) / total);
                        sur.of[pi].push_back(t);
                    }
            }
        }
    }

    // Each lamp's sets: kept while it stands where it was.
    auto& kept = lamp_sets_[&room];
    std::map<std::string, const LampSets*> sets;
    for (const Element* le : index_of(room).lights) {
        if (!le->alive || le->params.num(Key{"sun"}) > 0.5 || le->params.num(Key{"indirect"}) > 0.5) continue;
        DrawLight L = light_of(room, *le);
        L.power = 26.0f;  // at 1, in white, as a bake lights it
        const Digest place = place_of(L, shell, size);
        LampSets& ls = kept[le->id.str()];
        sets[le->id.str()] = &ls;
        if (ls.place == place && static_cast<int>(ls.by_bounce.size()) == bounces) continue;
        ls.place = place;
        // Its shadow: how far it sees every way - from beyond its own
        // fitting (kLampClear), which would otherwise shadow all it lights.
        const Vec3d lamp{L.pos.x, L.pos.y, L.pos.z};
        const std::vector<float> far = seer.see_round(room, lamp, kLampSize, 4, kLampClear);
        const auto lit = [&](const Vec3d& p) {
            int x = 0, y = 0;
            const Vec3d d = p - lamp;
            const int f = face_of(d, kLampSize, x, y);
            const double seen = far[((static_cast<std::size_t>(f) * kLampSize + y) * kLampSize + x) * 4];
            const double d_len = length(d);
            return d_len <= seen + 0.05 + 0.03 * d_len;
        };
        // What it gives each surface a probe sees, at once (the first
        // bounce's own light), shadowed.
        std::vector<std::vector<Rgb>> direct(probes.size());
        for (std::size_t pi = 0; pi < probes.size(); ++pi) {
            direct[pi].reserve(sur.of[pi].size());
            for (const ProbeTexel& t : sur.of[pi]) {
                Vec3d l;
                const double r = reach_of(L, t.at, l);
                const double ndl = dot(t.n, l);
                const double e = r > 0.0 && ndl > 0.0 && lit(t.at) ? r * ndl : 0.0;
                direct[pi].push_back(Rgb{t.albedo.r * e, t.albedo.g * e, t.albedo.b * e});
            }
        }
        ls.by_bounce.assign(static_cast<std::size_t>(bounces), std::vector<Sh9>(probes.size()));
        for (int b = 0; b < bounces; ++b) {
            const std::vector<Sh9>* before = b > 0 ? &ls.by_bounce[static_cast<std::size_t>(b - 1)] : nullptr;
            for (std::size_t pi = 0; pi < probes.size(); ++pi) {
                Sh9 sh;
                for (std::size_t ti = 0; ti < sur.of[pi].size(); ++ti) {
                    const ProbeTexel& t = sur.of[pi][ti];
                    Rgb c = direct[pi][ti];
                    if (before) {
                        // And the probes' light on it, as the scene shader
                        // gives it (probe_light): blended by how much each holds it.
                        for (std::size_t pj = 0; pj < probes.size() && pj < t.held.size(); ++pj) {
                            const double w = t.held[pj];
                            if (w <= 0.0) continue;
                            const Rgb e = (*before)[pj].irradiance(t.n);
                            c.r += t.albedo.r * std::max(e.r, 0.0) * w, c.g += t.albedo.g * std::max(e.g, 0.0) * w,
                                c.b += t.albedo.b * std::max(e.b, 0.0) * w;
                        }
                    }
                    sh.add(t.dir, c, t.w);
                }
                ls.by_bounce[static_cast<std::size_t>(b)][pi] = sh;
            }
        }
    }
    // (Lamps gone go.)
    for (auto it = kept.begin(); it != kept.end();) it = sets.count(it->first) ? std::next(it) : kept.erase(it);
    for (GLWorldView* v : {&baker, &seer}) {
        v->lights_memo_.clear();
        v->shadow_sets_.clear();
        v->room_casters_.clear();
    }

    for (std::size_t pi = 0; pi < probes.size(); ++pi) {
        if (!probes[pi]->alive) continue;
        ProbeBake pb;
        pb.probe = probes[pi]->id.key();
        pb.digest = probe_digest(room, *probes[pi]);
        for (const auto& [lamp, ls] : sets) pb.sets[lamp] = ls->by_bounce.back()[pi];
        out.push_back(std::move(pb));
    }
    return out;
}

}  // namespace sg::render
