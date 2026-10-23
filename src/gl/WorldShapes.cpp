// The GL view's shapes: where each thing is, its mesh, the room's walls,
// and the sky and the ground of an open world.
#include <cstring>
#include "sg/gl/World.hpp"

#include <functional>
#include <thread>

namespace sg::render {

uint64_t GLWorldView::chain_stamp(const State& st, const Element& e) {
    uint64_t h = e.params.stamp();
    const Element* cur = &e;
    for (int i = 0; i < 8; ++i) {
        const std::string* parent_id = cur->params.text(keys::parent);
        if (!parent_id || parent_id->empty()) break;
        const Element* parent = st.find(Key{*parent_id});
        if (!parent) break;
        h = (h * 1099511628211ULL) ^ parent->params.stamp();
        cur = parent;
    }
    return h;
}

uint64_t GLWorldView::chain_stamp(const State& st, const Element& e, Chain& memo) const {
    if (memo.structure == st.structure() && memo.links > 0 && memo.link[0] == &e) {
        bool same = true;
        for (std::size_t i = 0; i < memo.links && same; ++i) same = memo.link[i]->params.stamp() == memo.seen[i];
        if (same) return memo.stamp;
    }
    // Found again, as chain_stamp finds it, keeping what it passed.
    memo.structure = st.structure();
    memo.links = 1;
    memo.link[0] = &e, memo.seen[0] = e.params.stamp();
    uint64_t h = e.params.stamp();
    const Element* cur = &e;
    for (int i = 0; i < 8; ++i) {
        const std::string* parent_id = cur->params.text(keys::parent);
        if (!parent_id || parent_id->empty()) break;
        const Element* parent = st.find(Key{*parent_id});
        if (!parent) break;
        h = (h * 1099511628211ULL) ^ parent->params.stamp();
        cur = parent;
        memo.link[memo.links] = parent, memo.seen[memo.links] = parent->params.stamp(), ++memo.links;
    }
    memo.stamp = h;
    return h;
}

auto GLWorldView::slot_of(const Element& e) const -> PlacedSlot& {
    const auto home = [this](const Element* p) {
        uint64_t h = static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(p) >> 4) * 0x9E3779B97F4A7C15ULL;
        return static_cast<std::size_t>(h ^ (h >> 31)) & (placed_index_.size() - 1);
    };
    if (placed_index_.empty()) placed_index_.assign(4096, {nullptr, 0});
    std::size_t i = home(&e);
    for (;; i = (i + 1) & (placed_index_.size() - 1)) {
        const auto& s = placed_index_[i];
        if (s.first == &e) return placed_[s.second];
        if (!s.first) break;
    }
    if ((placed_.size() + 1) * 2 > placed_index_.size()) {
        // Half full: twice the room, each where it falls now.
        std::vector<std::pair<const Element*, uint32_t>> old(placed_index_.size() * 2, {nullptr, 0});
        old.swap(placed_index_);
        for (const auto& s : old)
            if (s.first) {
                std::size_t j = home(s.first);
                while (placed_index_[j].first) j = (j + 1) & (placed_index_.size() - 1);
                placed_index_[j] = s;
            }
        i = home(&e);
        while (placed_index_[i].first) i = (i + 1) & (placed_index_.size() - 1);
    }
    placed_index_[i] = {&e, static_cast<uint32_t>(placed_.size())};
    return placed_.emplace_back();
}

auto GLWorldView::placed_of(const State& st, const Element& e) const -> Placed& { return placed_in(slot_of(e), st, e); }

auto GLWorldView::placed_in(PlacedSlot& slot, const State& st, const Element& e) const -> Placed& {
    const uint64_t stamp = chain_stamp(st, e, slot.chain);
    if (slot.placed.stamp != stamp) {
        if (slot.placed.boxed) slot.before = slot.placed.box, slot.moved_in = frame_count_;
        slot.placed = Placed{stamp};
    }
    return slot.placed;
}

Pose GLWorldView::pose_of(const State& st, const Element& e) const {
    Placed& p = placed_of(st, e);
    if (!p.posed) p.pose = world_pose(st, e), p.posed = true;
    return p.pose;
}

const RoomMatrix& GLWorldView::box_matrix(const State& st, const Element& e) const {
    Placed& p = placed_of(st, e);
    if (!p.boxed) p.box = box_model(st, e), p.boxed = true;
    return p.box;
}

void GLWorldView::draw_sky(const Camera& cam, float zfar) {
    const gl::Mat4 m = gl::Mat4::translate(cam.eye) * gl::Mat4::scale(gl::Vec3{1, 1, 1} * (zfar * 1.8f));
    scene_->set("uModel", m);
    scene_->set("uTexModel", m);
    scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
    scene_->set("uRoughness", 1.0f);
    scene_->set("uSurface", 9.0f);
    scene_->set("uEmissive", 0.0f);
    scene_->set("uHighlight", 0.0f);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
    sphere_.draw();
}

// The ground round the eye: squares one round another, each of the world's
// own lattice for its spacing - `step` in the middle one, each next twice
// the last, its middle on the lattice twice as coarse, a square the size of
// the one inside it cut out of it - as many as reach `reach`, about as many
// cells in all as an `n` by `n` grid. Every cell square, every point a point
// of the world: the eye moving on, a square moves a whole cell of the next
// at a time, its points where they were, as high.
//
// And no point comes or goes at once: towards a square's outer edge each
// point between two of the next coarser lattice (`fold`) slides along the
// ground onto the one before it, across and along, as far as the eye has
// left it behind - wholly there a cell short of the edge and the eye's
// `eye_step` short of that (how far it goes before the ground is taken
// again), so the edge is the next square's own edge, cut corner to corner as
// its cells are; it is taken away only so, and comes back so, settling to
// its own place as the eye nears. The ground drawn changes as smoothly as
// the eye moves - no pop where a spacing doubles, no crack between squares.
std::vector<float> GLWorldView::sample_ground(const std::function<double(double, double)>& height, int n, double reach, double step,
                                              double cx, double cz, double eye_step) {
    const int m = std::clamp(n / 6, 8, 128) / 2 * 2;  // cells from a square's middle to its edge (even: the square inside a whole number of these)
    int levels = 1;
    while (m * step * std::ldexp(1.0, levels - 1) < reach && levels < 24) ++levels;
    const int side = 2 * m + 1;  // points a side of each square
    const int wide = side + 4;   // and two more each way beyond it: which way its edge faces is as true as anywhere else
    struct Square {
        double s = 0, x0 = 0, z0 = 0;   // its spacing, its corner
        int hole_i0 = 0, hole_i1 = 0;   // the cells cut out of it (the square inside), across
        int hole_j0 = 0, hole_j1 = 0;   // and along; none if equal
        std::vector<double> h;          // the heights of its points and two beyond, a row a z
    };
    std::vector<Square> sq(static_cast<std::size_t>(levels));
    for (int L = 0; L < levels; ++L) {
        Square& q = sq[static_cast<std::size_t>(L)];
        q.s = step * std::ldexp(1.0, L);
        const double mx = std::round(cx / (2.0 * q.s)) * 2.0 * q.s, mz = std::round(cz / (2.0 * q.s)) * 2.0 * q.s;
        q.x0 = mx - m * q.s, q.z0 = mz - m * q.s;
        if (L > 0) {
            const Square& in = sq[static_cast<std::size_t>(L - 1)];
            q.hole_i0 = static_cast<int>(std::lround((in.x0 - q.x0) / q.s)), q.hole_i1 = q.hole_i0 + m;
            q.hole_j0 = static_cast<int>(std::lround((in.z0 - q.z0) / q.s)), q.hole_j1 = q.hole_j0 + m;
        }
        q.h.assign(static_cast<std::size_t>(wide) * wide, 0.0);
    }
    // Every point of every square, on every core at once.
    const int workers = static_cast<int>(std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w)
        pool.emplace_back([&, w] {
            for (int r = w; r < levels * wide; r += workers) {
                Square& q = sq[static_cast<std::size_t>(r / wide)];
                const int j = r % wide;
                for (int i = 0; i < wide; ++i) q.h[static_cast<std::size_t>(j) * wide + i] = height(q.x0 + (i - 2) * q.s, q.z0 + (j - 2) * q.s);
            }
        });
    for (std::thread& th : pool) th.join();

    std::vector<float> v;
    v.reserve(static_cast<std::size_t>(levels) * 4 * m * m * 6 * 8);
    for (int L = 0; L < levels; ++L) {
        const Square& q = sq[static_cast<std::size_t>(L)];
        const bool last = L + 1 == levels;
        const auto hgt = [&](int i, int j) { return q.h[static_cast<std::size_t>(std::clamp(j + 2, 0, wide - 1)) * wide + std::clamp(i + 2, 0, wide - 1)]; };
        // Which way the ground faces at a point, from its neighbours `k`
        // cells off (1: its own spacing; 2: the next square's).
        const auto facing = [&](int i, int j, int k) {
            const double dx = (hgt(i + k, j) - hgt(i - k, j)) / (2.0 * k * q.s), dz = (hgt(i, j + k) - hgt(i, j - k)) / (2.0 * k * q.s);
            return gl::Vec3{static_cast<float>(-dx), 1.0f, static_cast<float>(-dz)};
        };
        // How far a point is folded: from half way out to a cell and the
        // eye's step short of where this square ends (its inner edge, the
        // square inside's outer, a cell further in than where it begins).
        // (The eye's step twice: once for how far it went, once for the
        // ground taken again in the background meanwhile.)
        const double lo = 0.5 * m * q.s + (L > 0 ? q.s : 0.0), hi = std::max(m * q.s - 2.0 * q.s - 2.0 * eye_step, lo + q.s);
        const auto fold_at = [&](int i, int j) {
            if (last) return 0.0;
            const double d = std::max(std::fabs(q.x0 + i * q.s - cx), std::fabs(q.z0 + j * q.s - cz));
            const double t = std::clamp((d - lo) / (hi - lo), 0.0, 1.0);
            return t * t * (3.0 - 2.0 * t);
        };
        // (Odd: between two of the coarser lattice; the corner is on it.)
        const auto odd = [&](double at0, int i) { return std::llround((at0 + i * q.s) / q.s) % 2 != 0; };
        const auto put = [&](int i, int j) {
            const double k = fold_at(i, j);
            const double fx = odd(q.x0, i) ? k : 0.0, fz = odd(q.z0, j) ? k : 0.0;
            const int pi = fx > 0.0 ? i - 1 : i, pj = fz > 0.0 ? j - 1 : j;
            const auto mix = [](double a, double b, double t) { return a + (b - a) * t; };
            const double x = mix(q.x0 + i * q.s, q.x0 + pi * q.s, fx), z = mix(q.z0 + j * q.s, q.z0 + pj * q.s, fz);
            const double h = mix(mix(hgt(i, j), hgt(pi, j), fx), mix(hgt(i, pj), hgt(pi, pj), fx), fz);
            // (Facing as its own spacing says, folded with it - and, as far
            // as it is folded, as the coarser lattice says where it goes.)
            const float wx = static_cast<float>(fx), wz = static_cast<float>(fz);
            const gl::Vec3 a = facing(i, j, 1), b = facing(pi, j, 1), c = facing(i, pj, 1), d = facing(pi, pj, 1);
            const gl::Vec3 ab = a * (1.0f - wx) + b * wx, cd = c * (1.0f - wx) + d * wx;
            const gl::Vec3 fine = ab * (1.0f - wz) + cd * wz;
            const gl::Vec3 coarse = facing(pi, pj, 2);
            const float kk = static_cast<float>(k);
            const gl::Vec3 nn = gl::normalize(fine * (1.0f - kk) + coarse * kk);
            const float xf = static_cast<float>(x), zf = static_cast<float>(z);
            v.insert(v.end(), {xf, static_cast<float>(h), zf, nn.x, nn.y, nn.z, xf * 0.1f, zf * 0.1f});
        };
        for (int j = 0; j < 2 * m; ++j)
            for (int i = 0; i < 2 * m; ++i) {
                if (i >= q.hole_i0 && i < q.hole_i1 && j >= q.hole_j0 && j < q.hole_j1) continue;
                put(i, j);
                put(i, j + 1);
                put(i + 1, j + 1);
                put(i, j);
                put(i + 1, j + 1);
                put(i + 1, j);
            }
    }
    return v;
}

void GLWorldView::bind_terrain(Key terrain_element, std::function<double(double, double)> height) {
    TerrainMesh& t = terrains_[terrain_element];
    t.height = std::move(height), t.bound = true;
}

void GLWorldView::ensure_terrain(const Element& e, const Camera& cam) {
    auto it = terrains_.find(e.id);
    if (it == terrains_.end() || !it->second.height) return;
    TerrainMesh& t = it->second;
    const int n = std::max(16, std::min(768, static_cast<int>(e.params.num(Key{"cells"}, 160.0)))) / 2 * 2;
    const double reach = e.params.num(Key{"reach"}, 320.0);
    const double step = e.params.num(Key{"step"}, 0.5);
    const double snap = e.params.num(Key{"snap"}, 2.0);
    const double rev = e.params.num(Key{"rev"}, 0.0);
    const double cx = std::floor(cam.eye.x / snap) * snap, cz = std::floor(cam.eye.z / snap) * snap;

    // A finished resample goes in, if it is still the ground wanted (a land
    // that drifts: the newest there is).
    if (t.job.valid() && t.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::vector<float> v = t.job.get();
        if (t.job_rev == rev || e.params.num(Key{"drifts"}, 0.0) > 0.5) {
            t.mesh.update(v);
            t.cx = t.job_cx;
            t.cz = t.job_cz;
            t.rev = t.job_rev;
        }
    }
    if (t.mesh.valid() && cx == t.cx && cz == t.cz && rev == t.rev) return;
    // A land that moves as its world's time goes (`drifts`: dunes creeping)
    // is drawn as it was until the ground it now is has been sampled, in the
    // background, as a step aside is; any other land asked again (`rev`) is
    // waited for.
    const bool drifts = e.params.num(Key{"drifts"}, 0.0) > 0.5;
    if (!t.mesh.valid() || (rev != t.rev && !drifts)) {
        // Nothing sensible to draw meanwhile: wait for it.
        if (t.job.valid()) t.job.wait();
        t.mesh.update(sample_ground(t.height, n, reach, step, cx, cz, snap));
        t.cx = cx;
        t.cz = cz;
        t.rev = rev;
        return;
    }
    if (t.job.valid()) return;  // one at a time; the next frame asks again
    t.job_cx = cx;
    t.job_cz = cz;
    t.job_rev = rev;
    t.job = std::async(std::launch::async, [height = t.height, n, reach, step, cx, cz, snap] {
        return sample_ground(height, n, reach, step, cx, cz, snap);
    });
}

auto GLWorldView::ground_of(Key terrain_element) const -> const TerrainMesh* {
    const auto& from = home_ ? home_->terrains_ : terrains_;
    const auto it = from.find(terrain_element);
    if (it != from.end() && it->second.mesh.valid()) return &it->second;
    if (!home_) return nullptr;
    const auto own = terrains_.find(terrain_element);
    return own != terrains_.end() && own->second.mesh.valid() ? &own->second : nullptr;
}

void GLWorldView::draw_terrain(const State& st, const Element& e) {
    const TerrainMesh* ground = ground_of(e.id);
    if (!ground) return;
    set_model(room_local(gl::Mat4::identity()));
    scene_->set("uAlbedo", color_of(e, {0.80f, 0.62f, 0.42f}));
    scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.85)));
    scene_->set("uSurface", static_cast<float>(e.params.num(Key{"surface"}, 8.0)));
    scene_->set("uEmissive", 0.0f);
    scene_->set("uHighlight", 0.0f);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
    const bool beds = bind_beds(st, e);
    const bool land = !beds && bind_land(st, e);
    // Or a photographed ground (`skin`, its `skin_normal` and `skin_surface`):
    // laid flat over the world every `tile` metres (2 unless it says), its
    // repeats broken by a second, larger laying (`tile_break`, 0..1; 0.6).
    static const Key skin{"skin"}, normal{"skin_normal"}, surface{"skin_surface"};
    bool worn = false, normals = false, surfaced = false;
    if (!land && !beds && e.params.has(skin) && bind_worn(st, e.params.get_or<std::string>(skin, ""), true, 0)) {
        worn = true;
        scene_->set("uTexMix", 1.0f);
        scene_->set("uSkin", 2.0f);
        scene_->set("uTile", static_cast<float>(e.params.num(Key{"tile"}, 2.0)));
        scene_->set("uTileBreak", static_cast<float>(std::clamp(e.params.num(Key{"tile_break"}, 0.6), 0.0, 1.0)));
        scene_->set("uScreenUV", 0.0f);
        scene_->set("uCRT", 0.0f);
        if (const std::string* n = e.params.text(normal)) normals = bind_worn(st, *n, false, 10);
        if (const std::string* s = e.params.text(surface)) surfaced = bind_worn(st, *s, false, 9);
        scene_->set("uNormalMapOn", normals ? 1.0f : 0.0f);
        scene_->set("uSurfaceMapOn", surfaced ? 1.0f : 0.0f);
        if (normals) {
            scene_->set("uNormalStrength", static_cast<float>(e.params.num(Key{"normal_strength"}, 1.0)));
            scene_->set("uNormalFlipY", -1.0f);
        }
    }
    // Damp by its water: where it stands (`wet_level`) and how far up
    // (`wet_band`, metres; 0 none).
    const double wet_band = e.params.num(Key{"wet_band"}, 0.0);
    // And its sand's ripples (surface 8): how far apart (`ripple`, metres;
    // 0 as sand has them, a hand; below 0 none) and the way the wind that
    // laid them blew (`ripple_dir`, radians round from +x towards +z).
    static const Key ripple{"ripple"}, ripple_dir{"ripple_dir"};
    scene_->set("uLand", static_cast<float>(e.params.num(Key{"wet_level"}, 0.0)), static_cast<float>(std::max(wet_band, 0.0)),
                static_cast<float>(e.params.num(ripple, 0.0)), static_cast<float>(e.params.num(ripple_dir, 0.0)));
    ground->mesh.draw();
    scene_->set("uLand", 0.0f, 0.0f, 0.0f, 0.0f);
    if (land || beds) unbind_land();
    if (worn) {
        scene_->set("uTexMix", 0.0f), scene_->set("uSkin", 0.0f), scene_->set("uTileBreak", 0.0f);
        if (normals) scene_->set("uNormalMapOn", 0.0f), scene_->set("uNormalStrength", 1.0f);
        if (surfaced) scene_->set("uSurfaceMapOn", 0.0f);
    }
}

bool GLWorldView::bind_land(const State& st, const Element& e) {
    static const Key splat{"splat"};
    if (!e.params.has(splat) || !bind_picture(st, e.params.get_or<std::string>(splat, ""), true)) return false;
    const auto three = [](const std::string& t, gl::Vec3 fallback) {
        float v[3] = {fallback.x, fallback.y, fallback.z};
        std::sscanf(t.c_str(), "%f,%f,%f", &v[0], &v[1], &v[2]);
        return gl::Vec3{v[0], v[1], v[2]};
    };
    if (e.kind == terrain_kind()) {
        // Ground: each layer's share over the land's own rectangle.
        scene_->set("uSplat", 1.0f);
        const float w = static_cast<float>(e.params.num(Key{"splat_w"}, 1.0)), d = static_cast<float>(e.params.num(Key{"splat_d"}, 1.0));
        scene_->set("uSplatRect", static_cast<float>(e.params.num(Key{"splat_x"}, 0.0)), static_cast<float>(e.params.num(Key{"splat_z"}, 0.0)),
                    1.0f / std::max(w, 1e-3f), 1.0f / std::max(d, 1e-3f));
        float sf[4] = {16, 16, 16, 16};
        std::sscanf(e.params.get_or<std::string>(Key{"layers"}, "").c_str(), "%f,%f,%f,%f", &sf[0], &sf[1], &sf[2], &sf[3]);
        scene_->set("uLayerSurface", sf[0], sf[1], sf[2], sf[3]);
        for (int k = 0; k < 4; ++k)
            scene_->set(("uLayerColor[" + std::to_string(k) + "]").c_str(), three(e.params.get_or<std::string>(Key{"layer" + std::to_string(k)}, ""), {0.35f, 0.42f, 0.22f}));
    } else {
        // Water: how deep it is under each point of its own uv.
        scene_->set("uSplat", 2.0f);
        scene_->set("uCalm", static_cast<float>(e.params.num(Key{"calm"}, 0.0)));
        scene_->set("uWaterDeepest", static_cast<float>(e.params.num(Key{"deepest"}, 4.0)));
        scene_->set("uWaterDeep", gl::Vec3{static_cast<float>(e.params.num(Key{"deep_r"}, 0.03)), static_cast<float>(e.params.num(Key{"deep_g"}, 0.07)),
                                           static_cast<float>(e.params.num(Key{"deep_b"}, 0.08))});
    }
    return true;
}

void GLWorldView::unbind_land() { scene_->set("uSplat", 0.0f), scene_->set("uCalm", 0.0f); }

namespace {

// One channel (or the colour) of a picture, read at `size` x `size` over the
// whole of it - a box of its texels for each, so a larger picture is
// averaged down, not sampled - into `into` (RGBA8, `size` squared) at the
// channels from `to` on, `n` of them. A picture not there leaves `fill`.
void lay(const Spatial3D::Picture* pic, int from, int n, int size, std::vector<unsigned char>& into, std::size_t at, int to,
         unsigned char fill) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            unsigned char* out = &into[at + (static_cast<std::size_t>(y) * size + x) * 4 + to];
            if (!pic) {
                for (int c = 0; c < n; ++c) out[c] = fill;
                continue;
            }
            const int x0 = x * pic->w / size, x1 = std::max(x0 + 1, (x + 1) * pic->w / size);
            const int y0 = y * pic->h / size, y1 = std::max(y0 + 1, (y + 1) * pic->h / size);
            for (int c = 0; c < n; ++c) {
                unsigned sum = 0;
                for (int j = y0; j < y1; ++j)
                    for (int i = x0; i < x1; ++i) sum += pic->rgba[(static_cast<std::size_t>(j) * pic->w + i) * 4 + from + c];
                out[c] = static_cast<unsigned char>(sum / static_cast<unsigned>((x1 - x0) * (y1 - y0)));
            }
        }
}

}  // namespace

bool GLWorldView::bind_beds(const State& st, const Element& e) {
    static const Key beds{"beds"};
    if (e.kind != terrain_kind() || !e.params.has(beds)) return false;
    const auto* space = dynamic_cast<const Spatial3D*>(&st);
    if (!space) return false;
    // Each bed's colour (`bed<k>`), and beside it its normal map, its surface
    // map (roughness in green) and its height (grey) if it has them.
    const Spatial3D::Picture* pics[4][4] = {};
    int count = 0;
    for (int k = 0; k < 4; ++k) {
        const std::string name = "bed" + std::to_string(k);
        const std::string* color = e.params.text(Key{name});
        if (!color) break;
        pics[k][0] = space->picture(Key{*color});
        if (!pics[k][0] || pics[k][0]->w <= 0) return false;
        int m = 1;
        for (const char* map : {"_normal", "_surface", "_height"}) {
            const std::string* p = e.params.text(Key{name + map});
            pics[k][m++] = p ? space->picture(Key{*p}) : nullptr;
        }
        ++count;
    }
    if (count == 0) return false;
    // Stacked at the largest colour's size (a power of two, 2048 at most),
    // once - and again only when a picture of theirs is painted again.
    std::vector<uint64_t> made;
    int size = 1;
    for (int k = 0; k < count; ++k) {
        for (const Spatial3D::Picture* p : pics[k]) made.push_back(p ? p->revision + 1 : 0);
        while (size * 2 <= std::min(pics[k][0]->w, 2048)) size *= 2;
    }
    BedArrays& a = (home_ ? home_->beds_ : beds_)[e.id];
    if (!a.color.valid() || a.made != made) {
        const std::size_t layer = static_cast<std::size_t>(size) * size * 4;
        std::vector<unsigned char> color(layer * count), normal(layer * count);
        for (int k = 0; k < count; ++k) {
            lay(pics[k][0], 0, 3, size, color, layer * k, 0, 0);
            lay(pics[k][3], 0, 1, size, color, layer * k, 3, 128);
            lay(pics[k][1], 0, 3, size, normal, layer * k, 0, 128);
            if (!pics[k][1])
                for (std::size_t i = layer * k; i < layer * (k + 1); i += 4) normal[i + 2] = 255;  // (no map: facing straight out)
            lay(pics[k][2], 1, 1, size, normal, layer * k, 3, static_cast<unsigned char>(std::lround(255.0 * e.params.num(Key{"roughness"}, 0.85))));
        }
        a.color.create(size, count, color, true);
        a.normal.create(size, count, normal, false);
        a.made = std::move(made);
    }
    // Where each lies: the land's picture of their shares (`beds`, RGBA one
    // a bed) over its rectangle - as a land's layers' (bind_land).
    if (!bind_picture(st, e.params.get_or<std::string>(beds, ""), true)) return false;
    const float w = static_cast<float>(e.params.num(Key{"beds_w"}, 1.0)), d = static_cast<float>(e.params.num(Key{"beds_d"}, 1.0));
    scene_->set("uSplat", 3.0f);
    scene_->set("uSplatRect", static_cast<float>(e.params.num(Key{"beds_x"}, 0.0)), static_cast<float>(e.params.num(Key{"beds_z"}, 0.0)),
                1.0f / std::max(w, 1e-3f), 1.0f / std::max(d, 1e-3f));
    // (Each bed's metres a picture - in the layers' surfaces' place - and its
    // tint, in their colours'.)
    // (And how far each one's patches may be turned - `bed<k>_turn`, radians;
    // any way unless it says, a pattern with a way to it less.)
    float tile[4] = {2, 2, 2, 2}, turn[4] = {3.14159265f, 3.14159265f, 3.14159265f, 3.14159265f};
    for (int k = 0; k < count; ++k) {
        const std::string name = "bed" + std::to_string(k);
        tile[k] = static_cast<float>(std::max(e.params.num(Key{name + "_tile"}, 2.0), 0.01));
        turn[k] = static_cast<float>(std::clamp(e.params.num(Key{name + "_turn"}, 3.14159265), 0.0, 3.14159265));
        scene_->set(("uLayerColor[" + std::to_string(k) + "]").c_str(),
                    gl::Vec3{static_cast<float>(e.params.num(Key{name + "_r"}, 1.0)), static_cast<float>(e.params.num(Key{name + "_g"}, 1.0)),
                             static_cast<float>(e.params.num(Key{name + "_b"}, 1.0))});
    }
    // (A bed not laid takes no share: its tile says so, below zero.)
    for (int k = count; k < 4; ++k) tile[k] = -1.0f;
    scene_->set("uLayerSurface", tile[0], tile[1], tile[2], tile[3]);
    scene_->set("uBedTurn", turn[0], turn[1], turn[2], turn[3]);
    a.color.bind(kBedsUnit);
    a.normal.bind(kBedsUnit + 1);
    gl::glActiveTexture(gl::GL_TEXTURE0);
    return true;
}

bool GLWorldView::is_doorway(const State& host,const Element& e) const {
    auto it = worlds_.find(e.id);
    if(it==worlds_.end() || !it->second.world) return false;
    return declared_world(host,e,*it->second.world);
}

const gl::Mesh& GLWorldView::shape_of(const State& st, const Element& e) const {
    // A box that bends (`bend`: metres its corners stand off its middle,
    // along its thinnest way - a sheet held, sagging): the cube cut finer,
    // its faces curved by the scene's vertex stage.
    static const Key bend{"bend"};
    const gl::Mesh& m = shape_plain(st, e);
    return &m == &cube_ && e.params.num(bend, 0.0) != 0.0 ? bent_ : m;
}

const gl::Mesh& GLWorldView::shape_plain(const State& st, const Element& e) const {
    if (e.kind != kinds::mesh && e.kind != kinds::strewn) return cube_;
    // Asked every frame, in every pass, of every thing: remembered while
    // the thing's parameters, or what its mesh is made of, hold.
    ShapeMemo& memo = slot_of(e).shape;
    const auto* space = dynamic_cast<const Spatial3D*>(&st);
    const auto made = [&] {
        return space && memo.modelled ? (memo.skinned ? space->skin_revision(memo.model) : space->model_revision(memo.model)) : 0;
    };
    // (A skin, posed from where its joints are now, as it is asked for.)
    const auto posed = [&](const gl::Mesh& m) -> const gl::Mesh& {
        if (!memo.skinned || !space) return m;
        const uint64_t now = space->pose_revision(memo.model);
        if (m.posed != now) {
            if (const std::vector<float>* joints = space->skin_pose(memo.model)) m.pose(*joints);
            m.posed = now;
        }
        m.bind_joints();
        return m;
    };
    if (memo.mesh && memo.stamp == e.params.stamp() && (memo.made == 0 || memo.made == made())) return posed(*memo.mesh);
    static const Key shape{"shape"}, model{"model"}, bevel{"bevel"}, taper{"taper"};
    static const std::string none;
    const std::string* shape_name = e.params.text(shape);
    const std::string* model_name = e.params.text(model);
    if (!shape_name) shape_name = &none;
    if (!model_name) model_name = &none;
    const double tp = e.params.num(taper, 1.0), r = e.params.num(bevel, 0.0);
    const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
    const bool skin = skinned(e);
    if (memo.mesh && *shape_name == memo.shape_name && *model_name == memo.model_name && tp == memo.taper && r == memo.bevel &&
        sx == memo.sx && sy == memo.sy && sz == memo.sz && skin == memo.skinned && (memo.made == 0 || memo.made == made())) {
        memo.stamp = e.params.stamp();
        return posed(*memo.mesh);
    }
    const gl::Mesh& m = find_shape(st, e);
    memo.stamp = e.params.stamp(), memo.mesh = &m;
    memo.modelled = *shape_name == "model";
    memo.skinned = memo.modelled && skin;
    memo.model = memo.modelled ? Key{*model_name} : Key{};
    memo.shape_name = *shape_name, memo.model_name = *model_name;
    memo.taper = tp, memo.bevel = r, memo.sx = sx, memo.sy = sy, memo.sz = sz;
    memo.made = made();
    return posed(m);
}

const gl::Mesh& GLWorldView::shape_seen(const State& st, const Element& e) const {
    const gl::Mesh& m = shape_of(st, e);
    if (!q_.lod || fewer_.empty() || lod_px_ <= 0.0f) return m;
    const auto it = fewer_.find(&m);
    if (it == fewer_.end() || it->second.empty()) return m;
    static const Key uv_key{"uv"};
    if (e.params.num(uv_key, 0.0) > 0.5) return m;
    // How far off, and how big (its box's longest way: what its model's
    // units are on the picture at most).
    const gl::Mat4& b = box_matrix(st, e).m;
    const gl::Vec3 away = gl::Vec3{b.m[12], b.m[13], b.m[14]} - cam_eye_;
    const float d = std::sqrt(gl::dot(away, away));
    const float size = std::sqrt(std::max({b.m[0] * b.m[0] + b.m[1] * b.m[1] + b.m[2] * b.m[2], b.m[4] * b.m[4] + b.m[5] * b.m[5] + b.m[6] * b.m[6],
                                           b.m[8] * b.m[8] + b.m[9] * b.m[9] + b.m[10] * b.m[10]}));
    const gl::Mesh* seen = &m;
    for (const auto& f : it->second) {
        if (f->error * size * lod_px_ >= 0.35f * d) break;
        seen = &f->mesh;
    }
    if (seen != &m) ++times_.lod_drawn;
    return *seen;
}

bool GLWorldView::skinned(const Element& e) {
    static const Key skinned_key{"skinned"};
    return e.params.num(skinned_key, 0.0) > 0.5;
}

auto GLWorldView::corners_key(const std::vector<float>& corners) -> CornersKey {
    // Four lanes over the corners' bits, eight bytes a step each (so the
    // processor keeps them all going at once), folded into two.
    const std::size_t bytes = corners.size() * sizeof(float);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(corners.data());
    uint64_t lane[4] = {0x9e3779b97f4a7c15ull, 0xc2b2ae3d27d4eb4full, 0x165667b19e3779f9ull, 0x27d4eb2f165667c5ull};
    const auto mix = [](uint64_t h, uint64_t w) {
        h ^= w * 0xff51afd7ed558ccdull;
        h = (h << 31) | (h >> 33);
        return h * 0xc4ceb9fe1a85ec53ull;
    };
    std::size_t i = 0;
    for (; i + 32 <= bytes; i += 32)
        for (int k = 0; k < 4; ++k) {
            uint64_t w;
            std::memcpy(&w, p + i + 8 * k, 8);
            lane[k] = mix(lane[k], w);
        }
    for (int k = 0; i < bytes; i += 8, ++k) {
        uint64_t w = 0;
        std::memcpy(&w, p + i, std::min<std::size_t>(8, bytes - i));
        lane[k & 3] = mix(lane[k & 3], w);
    }
    CornersKey key;
    key.a = mix(mix(lane[0], lane[1]), bytes);
    key.b = mix(mix(lane[2], lane[3]), bytes ^ 0x5bd1e995ull);
    key.n = corners.size();
    return key;
}

const gl::Mesh& GLWorldView::find_shape(const State& st, const Element& e) const {
    static const Key shape{"shape"}, bevel{"bevel"}, taper{"taper"}, model{"model"};
    const std::string s = e.params.get_or<std::string>(shape, "");
    // One of its state's own models (sg/domains/Shapes.hpp): made into a
    // mesh the first time it is drawn, kept after.
    if (s == "model") {
        const auto* space = dynamic_cast<const Spatial3D*>(&st);
        const Key name{e.params.get_or<std::string>(model, "")};
        // A skin: its corners as bound, sent once (and again when it is
        // bound again), posed on the card as it is drawn (shape_plain).
        if (skinned(e)) {
            const std::vector<float>* bound = space ? space->skin_bound(name) : nullptr;
            if (!bound || bound->empty()) return cube_;
            ModelMesh& m = skin_meshes_[{&st, name.str()}];
            const uint64_t made = space->skin_revision(name);
            if (m.revision != made || !m.own.valid()) {
                m.own.create_bound(*bound);
                m.revision = made, m.own.posed = 0;
            }
            return m.own;
        }
        const std::vector<float>* corners = space ? space->model(name) : nullptr;
        if (!corners || corners->empty()) return cube_;
        // (Kept by whose model it is and which making, never by where its
        // corners lie: a model made again may be given an old one's place.)
        ModelMesh& m = model_meshes_[{&st, name.str()}];
        const uint64_t made = space->model_revision(name);
        if (m.revision == made && m.mesh().valid()) return m.mesh();
        m.revision = made;
        if (!m.shared && !m.own.valid()) {
            // First seen: the mesh of all that are the same, sent once.
            std::unique_ptr<gl::Mesh>& same = (home_ ? home_->meshes_made_ : meshes_made_)[corners_key(*corners)];
            if (!same) {
                same = std::make_unique<gl::Mesh>();
                making_ahead_ ? ++times_.warmed_ahead : ++times_.made_late;
                same->create(*corners);
            }
            m.shared = same.get();
            return *m.shared;
        }
        // Made again: a mesh of its own from now on, changed in place.
        m.shared = nullptr;
        m.own.update(*corners);
        return m.own;
    }
    const double tp = e.params.num(taper, 1.0);
    if (s == "sphere") return sphere_;
    if (s == "cylinder") {
        if (tp == 1.0) return cylinder_;
        const std::string key = "c" + std::to_string(std::lround(tp * 1000));
        gl::Mesh& m = shaped_[key];
        if (!m.valid()) m.create(gl::cylinder_vertices(28, static_cast<float>(tp))), making_ahead_ ? ++times_.warmed_ahead : ++times_.made_late;
        return m;
    }
    const double r = e.params.num(bevel, 0.0);
    if (r <= 0.0 && tp == 1.0) return cube_;
    // Sizes to the millimetre: near enough the same box is the same mesh.
    const auto mm = [](double v) { return std::to_string(std::lround(v * 1000)); };
    const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
    const std::string key = "b" + mm(sx) + "," + mm(sy) + "," + mm(sz) + "," + mm(r) + "," + mm(tp);
    gl::Mesh& m = shaped_[key];
    if (!m.valid())
        (making_ahead_ ? ++times_.warmed_ahead : ++times_.made_late),
        m.create(gl::rounded_box_vertices(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz),
                                          static_cast<float>(r), static_cast<float>(tp)));
    return m;
}

RoomMatrix GLWorldView::box_model(const State& st, const Element& e) const {
    return room_local(box_transform(st,e));
}

// (Its whole turn is in its pose, its anchors' with it.)
gl::Mat4 GLWorldView::panel_turn(const Pose& pose, const Element&) {
    return gl::Mat4::rotate_y(static_cast<float>(pose.yaw)) *
           gl::Mat4::rotate_z(static_cast<float>(pose.pitch)) *
           gl::Mat4::rotate_x(static_cast<float>(pose.roll));
}

gl::Vec3 GLWorldView::panel_normal(const Pose& pose, const Element&) {
    return to_vec3(facing(pose));
}

float GLWorldView::sheet_thickness(const Element& e) {
    return static_cast<float>(e.params.num(Key{"thick"}, 0.004));
}

RoomMatrix GLWorldView::portal_frame_model(const State& st, const Element& e) const {
    const Pose pose = pose_of(st, e);
    const float w = static_cast<float>(e.params.num(keys::w, 3.0));
    const float h = static_cast<float>(e.params.num(keys::h, 2.0));
    const float border = static_cast<float>(e.params.num(Key{"border"}, 0.15));
    const gl::Vec3 size = framed(e) ? gl::Vec3{0.08f, h + 2 * border - 0.08f, w + 2 * border - 0.08f}
                                    : gl::Vec3{sheet_thickness(e), h, w};
    return room_local(gl::Mat4::translate(to_vec3(pose.position)) * panel_turn(pose, e) *
                      gl::Mat4::scale(size));
}

void GLWorldView::draw_solid(const RoomMatrix& local, const gl::Vec3& albedo, float roughness, float surface, float emissive, float highlight) {
    ++times_.draws;
    set_model(local);
    scene_->set("uAlbedo", albedo);
    scene_->set("uRoughness", roughness);
    scene_->set("uSurface", surface);
    scene_->set("uEmissive", emissive);
    scene_->set("uHighlight", highlight);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
    cube_.draw();
}

bool GLWorldView::has_walls(const State& st) {
    for (const auto& e : st.elements())
        if (e.kind == kinds::wall && e.alive) return true;
    return false;
}

void GLWorldView::set_model(const RoomMatrix& local) {
    last_model_ = frame_matrix_ * local.m;
    scene_->set("uModel", last_model_);
    scene_->set("uTexModel", local.m);
    // A thing drawn on its own while the view's motion is kept: where it
    // stood last frame (or here, the first), and where it stands now kept
    // for the next - by its room, its name and which of its draws this is.
    if (motion_pass_ && moving_) {
        const uint64_t key = (static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(moving_room_)) * 0x9E3779B97F4A7C15ULL) ^
                             (static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(moving_->id.handle())) * 0xC2B2AE3D27D4EB4FULL) ^
                             (static_cast<uint64_t>(moving_part_++) * 0x165667B19E3779F9ULL);
        const auto was = moved_last_.find(key);
        // (Not drawn on its own last frame: where its box stood before it
        // moved, if that was this frame - else it stood here.)
        const PlacedSlot& slot = slot_of(*moving_);
        const gl::Mat4& before = was != moved_last_.end() ? was->second : moving_part_ == 1 && slot.moved_in == frame_count_ ? slot.before.m : local.m;
        scene_->set("uWasModel", before);
        scene_->set("uMoving", 1.0f);
        moved_now_[key] = local.m;
    }
}

void GLWorldView::draw_moving(const Spatial3D& room, const Element& e) {
    moving_room_ = &room, moving_ = &e, moving_part_ = 0;
    draw_crate(room, e);
    moving_room_ = nullptr, moving_ = nullptr;
    scene_->set("uMoving", 0.0f);
}

void GLWorldView::draw_room(const Spatial3D& world) {
    for(const auto& draw:enclosure(world))
        draw_solid(room_local(draw.model),{static_cast<float>(draw.colour.r),static_cast<float>(draw.colour.g),static_cast<float>(draw.colour.b)},static_cast<float>(draw.roughness),static_cast<float>(draw.surface));
}

void GLWorldView::draw_wall_element(const State& st, const Element& e) {
    // `surface` picks a wall's material; plaster if it does not say.
    draw_solid(box_matrix(st, e), color_of(e, {0.52f, 0.50f, 0.48f}), 0.9f, static_cast<float>(e.params.num(Key{"surface"}, 2.0)));
}

}  // namespace sg::render
