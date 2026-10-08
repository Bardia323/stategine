// The GL view's light probes: a room's probes on the scene program, and
// their bake - the room seen every way from each, lamp by lamp, taken to
// spherical harmonics and handed back as data (sg/domains/Probe.hpp).
#include "sg/gl/World.hpp"

#include <cmath>
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

}  // namespace sg::render
