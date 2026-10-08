// The GL view's shapes: where each thing is, its mesh, the room's walls,
// and the sky and the ground of an open world.
#include "sg/gl/World.hpp"

#include <functional>
#include <thread>

namespace sg::render {

uint64_t GLWorldView::chain_stamp(const State& st, const Element& e) {
    uint64_t h = e.params.stamp();
    const Element* cur = &e;
    for (int i = 0; i < 8; ++i) {
        if (!cur->params.has(keys::parent)) break;
        const std::string* parent_id = std::get_if<std::string>(&cur->params.get(keys::parent));
        if (!parent_id || parent_id->empty()) break;
        const Element* parent = st.find(Key{*parent_id});
        if (!parent) break;
        h = (h * 1099511628211ULL) ^ parent->params.stamp();
        cur = parent;
    }
    return h;
}

auto GLWorldView::placed_of(const State& st, const Element& e) const -> Placed& {
    Placed& p = placed_[&e];
    const uint64_t stamp = chain_stamp(st, e);
    if (p.stamp != stamp) p = Placed{stamp};
    return p;
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

std::vector<float> GLWorldView::sample_ground(const std::function<double(double, double)>& height, int n, double reach, double step, double cx, double cz) {
    const int half = n / 2;
    const double grow = std::max(0.0, (reach - step * half) / (static_cast<double>(half) * half));
    std::vector<double> off(static_cast<std::size_t>(n + 1));
    for (int i = 0; i <= n; ++i) {
        const double k = i - half, a = std::fabs(k);
        off[static_cast<std::size_t>(i)] = (k < 0 ? -1.0 : 1.0) * (step * a + grow * a * a);
    }
    const auto at = [n](int i, int j) { return static_cast<std::size_t>(j) * static_cast<std::size_t>(n + 1) + static_cast<std::size_t>(i); };
    std::vector<double> hgt(static_cast<std::size_t>((n + 1) * (n + 1)));
    const int workers = static_cast<int>(std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w)
        pool.emplace_back([&, w] {
            for (int j = w; j <= n; j += workers)
                for (int i = 0; i <= n; ++i)
                    hgt[at(i, j)] = height(cx + off[static_cast<std::size_t>(i)], cz + off[static_cast<std::size_t>(j)]);
        });
    for (std::thread& th : pool) th.join();
    std::vector<gl::Vec3> nrm(hgt.size());
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const int i0 = std::max(0, i - 1), i1 = std::min(n, i + 1);
            const int j0 = std::max(0, j - 1), j1 = std::min(n, j + 1);
            const double dx = (hgt[at(i1, j)] - hgt[at(i0, j)]) / (off[static_cast<std::size_t>(i1)] - off[static_cast<std::size_t>(i0)]);
            const double dz = (hgt[at(i, j1)] - hgt[at(i, j0)]) / (off[static_cast<std::size_t>(j1)] - off[static_cast<std::size_t>(j0)]);
            nrm[at(i, j)] = gl::normalize({static_cast<float>(-dx), 1.0f, static_cast<float>(-dz)});
        }
    std::vector<float> v;
    v.reserve(static_cast<std::size_t>(n) * n * 6 * 8);
    const auto put = [&](int i, int j) {
        const std::size_t k = at(i, j);
        const float x = static_cast<float>(cx + off[static_cast<std::size_t>(i)]);
        const float z = static_cast<float>(cz + off[static_cast<std::size_t>(j)]);
        v.insert(v.end(), {x, static_cast<float>(hgt[k]), z, nrm[k].x, nrm[k].y, nrm[k].z, x * 0.1f, z * 0.1f});
    };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            put(i, j);
            put(i, j + 1);
            put(i + 1, j + 1);
            put(i, j);
            put(i + 1, j + 1);
            put(i + 1, j);
        }
    return v;
}

void GLWorldView::ensure_terrain(const Element& e, const Camera& cam) {
    auto it = terrains_.find(e.id);
    if (it == terrains_.end() || !it->second.height) return;
    TerrainMesh& t = it->second;
    const int n = std::max(16, std::min(400, static_cast<int>(e.params.num(Key{"cells"}, 160.0)))) / 2 * 2;
    const double reach = e.params.num(Key{"reach"}, 320.0);
    const double step = e.params.num(Key{"step"}, 0.5);
    const double snap = e.params.num(Key{"snap"}, 2.0);
    const double rev = e.params.num(Key{"rev"}, 0.0);
    const double cx = std::floor(cam.eye.x / snap) * snap, cz = std::floor(cam.eye.z / snap) * snap;

    // A finished resample goes in, if it is still the ground wanted.
    if (t.job.valid() && t.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::vector<float> v = t.job.get();
        if (t.job_rev == rev) {
            t.mesh.update(v);
            t.cx = t.job_cx;
            t.cz = t.job_cz;
            t.rev = t.job_rev;
        }
    }
    if (t.mesh.valid() && cx == t.cx && cz == t.cz && rev == t.rev) return;
    if (!t.mesh.valid() || rev != t.rev) {
        // Nothing sensible to draw meanwhile: wait for it.
        if (t.job.valid()) t.job.wait();
        t.mesh.update(sample_ground(t.height, n, reach, step, cx, cz));
        t.cx = cx;
        t.cz = cz;
        t.rev = rev;
        return;
    }
    if (t.job.valid()) return;  // one at a time; the next frame asks again
    t.job_cx = cx;
    t.job_cz = cz;
    t.job_rev = rev;
    t.job = std::async(std::launch::async, [height = t.height, n, reach, step, cx, cz] {
        return sample_ground(height, n, reach, step, cx, cz);
    });
}

void GLWorldView::draw_terrain(const State& st, const Element& e) {
    auto it = terrains_.find(e.id);
    if (it == terrains_.end() || !it->second.mesh.valid()) return;
    set_model(room_local(gl::Mat4::identity()));
    scene_->set("uAlbedo", color_of(e, {0.80f, 0.62f, 0.42f}));
    scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.85)));
    scene_->set("uSurface", static_cast<float>(e.params.num(Key{"surface"}, 8.0)));
    scene_->set("uEmissive", 0.0f);
    scene_->set("uHighlight", 0.0f);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
    const bool land = bind_land(st, e);
    it->second.mesh.draw();
    if (land) unbind_land();
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

bool GLWorldView::is_doorway(const State& host,const Element& e) const {
    auto it = worlds_.find(e.id);
    if(it==worlds_.end() || !it->second.world) return false;
    return declared_world(host,e,*it->second.world);
}

const gl::Mesh& GLWorldView::shape_of(const State& st, const Element& e) const {
    if (e.kind != kinds::mesh) return cube_;
    // Asked every frame, in every pass, of every thing: remembered until
    // the thing's parameters change.
    auto& memo = shape_memo_[&e];
    const auto made = [&] {
        static const Key shape{"shape"}, model{"model"};
        const auto* space = dynamic_cast<const Spatial3D*>(&st);
        return space && e.params.is(shape, "model") ? space->model_revision(Key{e.params.get_or<std::string>(model, "")}) : 0;
    };
    if (memo.mesh && memo.stamp == e.params.stamp() && (memo.made == 0 || memo.made == made())) return *memo.mesh;
    const gl::Mesh& m = find_shape(st, e);
    memo = {e.params.stamp(), made(), &m};
    return m;
}

const gl::Mesh& GLWorldView::find_shape(const State& st, const Element& e) const {
    static const Key shape{"shape"}, bevel{"bevel"}, taper{"taper"}, model{"model"};
    const std::string s = e.params.get_or<std::string>(shape, "");
    // One of its state's own models (sg/domains/Shapes.hpp): made into a
    // mesh the first time it is drawn, kept after.
    if (s == "model") {
        const auto* space = dynamic_cast<const Spatial3D*>(&st);
        const Key name{e.params.get_or<std::string>(model, "")};
        const std::vector<float>* corners = space ? space->model(name) : nullptr;
        if (!corners || corners->empty()) return cube_;
        // (Kept by whose model it is and which making, never by where its
        // corners lie: a model made again may be given an old one's place.)
        ModelMesh& m = model_meshes_[{&st, name.str()}];
        const uint64_t made = space->model_revision(name);
        if (m.revision != made || !m.mesh.valid()) m.mesh.update(*corners), m.revision = made;
        return m.mesh;
    }
    const double tp = e.params.num(taper, 1.0);
    if (s == "sphere") return sphere_;
    if (s == "cylinder") {
        if (tp == 1.0) return cylinder_;
        const std::string key = "c" + std::to_string(std::lround(tp * 1000));
        gl::Mesh& m = shaped_[key];
        if (!m.valid()) m.create(gl::cylinder_vertices(28, static_cast<float>(tp)));
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
    scene_->set("uModel", frame_matrix_ * local.m);
    scene_->set("uTexModel", local.m);
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
