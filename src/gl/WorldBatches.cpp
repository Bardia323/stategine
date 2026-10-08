// The GL view's batches: things of one shape drawn together, nearest
// first, and the skins (textures) they wear.
#include "sg/gl/World.hpp"

#include "sg/domains/Texture.hpp"

#include <atomic>
#include <thread>

namespace sg::render {

bool GLWorldView::instanceable(const Element& e) const {
    static const Key worn{"skin"}, hung{"straddle"}, splat{"splat"}, lines{"lines"};
    if (!q_.instancing || e.id == highlight_ || e.params.has(worn) || e.params.num(hung, 0.0) > 0.5 || e.params.has(splat) ||
        e.params.num(lines, 0.0) > 0.5)
        return false;
    const auto skin = surfaces_.find(e.id);
    if (skin != surfaces_.end() && skin->second.surface) return false;
    // Hung from a thing that wears a texture, it wears it too: drawn on its
    // own (skin_holder says in whose frame).
    const std::string* parent = e.params.text(keys::parent);
    const auto worn_by = !parent || parent->empty() ? surfaces_.end() : surfaces_.find(Key{*parent});
    return worn_by == surfaces_.end() || !worn_by->second.surface;
}

const Element* GLWorldView::skin_holder(const State& st, const Element& e) const {
    const Element* at = &e;
    for (int i = 0; i < 8 && at; ++i) {
        const auto s = surfaces_.find(at->id);
        // A thing's parts wear only a texture, never a sheet bound to it.
        if (s != surfaces_.end() && s->second.surface && (at == &e || dynamic_cast<const Texture*>(s->second.surface)) &&
            declared_surface(*at, *s->second.surface))
            return at;
        const std::string* parent = at->params.text(keys::parent);
        at = !parent || parent->empty() ? nullptr : st.find(Key{*parent});
    }
    return nullptr;
}

namespace {
// The inverse of a matrix that moves, turns and scales (no projection).
gl::Mat4 affine_inverse(const gl::Mat4& a) {
    const float* m = a.m;
    const float c00 = m[5] * m[10] - m[9] * m[6], c01 = m[9] * m[2] - m[1] * m[10], c02 = m[1] * m[6] - m[5] * m[2];
    const float det = m[0] * c00 + m[4] * c01 + m[8] * c02;
    const float k = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
    gl::Mat4 r;
    r.m[0] = c00 * k, r.m[1] = c01 * k, r.m[2] = c02 * k;
    r.m[4] = (m[8] * m[6] - m[4] * m[10]) * k, r.m[5] = (m[0] * m[10] - m[8] * m[2]) * k, r.m[6] = (m[4] * m[2] - m[0] * m[6]) * k;
    r.m[8] = (m[4] * m[9] - m[8] * m[5]) * k, r.m[9] = (m[8] * m[1] - m[0] * m[9]) * k, r.m[10] = (m[0] * m[5] - m[4] * m[1]) * k;
    r.m[3] = r.m[7] = r.m[11] = 0.0f, r.m[15] = 1.0f;
    for (int i = 0; i < 3; ++i) r.m[12 + i] = -(r.m[i] * m[12] + r.m[4 + i] * m[13] + r.m[8 + i] * m[14]);
    return r;
}
}  // namespace

auto GLWorldView::skin_frame(const State& st, const Element& holder) const -> const SkinFrame& {
    SkinFrame& f = skin_frames_[&holder];
    if (f.frame == frame_count_) return f;
    f.frame = frame_count_;
    const bool mesh = holder.kind == kinds::mesh || holder.kind == kinds::wall;
    if (mesh) {
        f.to_unit = affine_inverse(box_matrix(st, holder).m);
        f.size = {static_cast<float>(holder.params.num(keys::sx, 1.0)), static_cast<float>(holder.params.num(keys::sy, 1.0)),
                  static_cast<float>(holder.params.num(keys::sz, 1.0))};
        return f;
    }
    // A thing of parts: the box round all of them, in its own turn.
    const Pose at = pose_of(st, holder);
    Vec3d lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    for (const Element& d : st.elements()) {
        if (!d.alive || (d.kind != kinds::mesh && d.kind != kinds::wall) || skin_holder(st, d) != &holder) continue;
        const gl::Mat4& m = box_matrix(st, d).m;
        for (float x : {-0.5f, 0.5f})
            for (float y : {-0.5f, 0.5f})
                for (float z : {-0.5f, 0.5f}) {
                    const gl::Vec3 c = m.transform_point({x, y, z});
                    const Vec3d l = local_of(at, {c.x, c.y, c.z});
                    lo = {std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z)};
                    hi = {std::max(hi.x, l.x), std::max(hi.y, l.y), std::max(hi.z, l.z)};
                }
    }
    if (lo.x > hi.x) lo = hi = Vec3d{};
    const gl::Vec3 size{static_cast<float>(std::max(hi.x - lo.x, 1e-3)), static_cast<float>(std::max(hi.y - lo.y, 1e-3)),
                        static_cast<float>(std::max(hi.z - lo.z, 1e-3))};
    const gl::Vec3 mid{static_cast<float>((lo.x + hi.x) * 0.5), static_cast<float>((lo.y + hi.y) * 0.5), static_cast<float>((lo.z + hi.z) * 0.5)};
    const gl::Mat4 frame = gl::Mat4::translate(to_vec3(at.position)) * panel_turn(at, holder) * gl::Mat4::translate(mid) * gl::Mat4::scale(size);
    f.to_unit = affine_inverse(frame);
    f.size = size;
    return f;
}

void GLWorldView::upload_skin(BoundSurface& bound) {
    const uint64_t frame = (root_ ? root_->frame_count_ : frame_count_) + 1;
    if (bound.asked == frame && bound.texture.valid()) return;
    bound.asked = frame;
    refresh(bound);
}

void GLWorldView::refresh(BoundSurface& bound) {
    Surface2D& surf = *bound.surface;
    const auto& pixels = surf.raster();
    if (bound.packed && bound.packed_revision == surf.revision()) {
        if (!bound.texture.packed() || bound.revision != surf.revision()) {
            // The card's one copy of that picture, made the first time.
            PackedOnCard& on = packed_on_card_[bound.packed.get()];
            if (!on.texture.valid()) {
                on.keep = bound.packed;
                on.texture.create_packed(*bound.packed);
            }
            if (!bound.shared && bound.texture.valid()) {
                const gl::GLuint was = bound.texture.id();  // (its own, from before: let go)
                gl::glDeleteTextures(1, &was);
            }
            bound.texture = on.texture;
            bound.shared = true;
            bound.revision = surf.revision();
        }
        return;
    }
    // Changed since it was packed (painted on, written over): its pixels from
    // now on, as any surface's - in a texture of its own.
    bound.packed.reset();
    if (bound.shared) bound.texture = gl::Texture{}, bound.shared = false;
    if (!bound.texture.valid() || bound.texture.packed() || bound.texture.width() != surf.px_w() ||
        bound.texture.height() != surf.px_h()) {
        bound.texture.create(surf.px_w(), surf.px_h(), /*mipmaps=*/true, surf.srgb());
        bound.revision = ~uint64_t{0};
    }
    if (bound.revision != surf.revision()) {
        bound.texture.upload(pixels);
        bound.revision = surf.revision();
    }
}

void GLWorldView::pack_skins() {
    if (!q_.pack || !gl::Texture::packs()) return;
    // Each texture once, however many things it is bound to; its picture
    // made here, on this thread, if it was not yet.
    struct Job {
        Surface2D* surface;
        const std::vector<unsigned char>* pixels;
        uint64_t revision;
        std::shared_ptr<const render::Packed> packed;
    };
    std::vector<Job> jobs;
    std::unordered_map<const Surface2D*, std::size_t> job_of;
    // (Surfaces that say their pixels are the same - made from the same
    // strokes, at the same size - are packed once, and share the picture.)
    struct Same {
        Digest made;
        int w = 0, h = 0;
        bool srgb = false;
        bool operator==(const Same& o) const { return made == o.made && w == o.w && h == o.h && srgb == o.srgb; }
    };
    struct SameHash {
        std::size_t operator()(const Same& k) const { return static_cast<std::size_t>(k.made.hi ^ (k.made.lo * 31) ^ static_cast<uint64_t>(k.w) ^ (static_cast<uint64_t>(k.h) << 20)); }
    };
    std::unordered_map<Same, std::size_t, SameHash> job_by_made;
    for (auto& [id, bound] : surfaces_) {
        Surface2D* s = bound.surface;
        if (!s || !dynamic_cast<const Texture*>(s) || job_of.count(s)) continue;
        const std::vector<unsigned char>* pixels = &s->raster();
        if (!render::packable(s->px_w(), s->px_h())) continue;
        Digest made;
        if (s->pixels_digest(made)) {
            const auto [it, fresh] = job_by_made.emplace(Same{made, s->px_w(), s->px_h(), s->srgb()}, jobs.size());
            if (!fresh) {
                job_of.emplace(s, it->second);
                continue;
            }
        }
        job_of.emplace(s, jobs.size());
        jobs.push_back(Job{s, pixels, s->revision(), nullptr});
    }
    // The biggest first, so no core is left with one at the end; each a
    // picture of its own pixels alone.
    std::vector<std::size_t> order(jobs.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return jobs[a].pixels->size() > jobs[b].pixels->size();
    });
    std::atomic<std::size_t> next{0};
    const auto hand = [&] {
        for (std::size_t k; (k = next.fetch_add(1)) < order.size();) {
            Job& j = jobs[order[k]];
            // (By what its pixels were made from, if it says: they are then
            // not read through to be known.)
            Digest made;
            j.packed = std::make_shared<const render::Packed>(
                j.surface->pixels_digest(made)
                    ? render::pack_kept(j.pixels->data(), j.surface->px_w(), j.surface->px_h(), j.surface->srgb(), made)
                    : render::pack_kept(j.pixels->data(), j.surface->px_w(), j.surface->px_h(), j.surface->srgb()));
        }
    };
    std::vector<std::thread> hands;
    const std::size_t n = std::min<std::size_t>(std::max(1u, std::thread::hardware_concurrency()), jobs.size());
    for (std::size_t t = 1; t < n; ++t) hands.emplace_back(hand);
    hand();
    for (std::thread& t : hands) t.join();
    for (auto& [id, bound] : surfaces_) {
        const auto it = bound.surface ? job_of.find(bound.surface) : job_of.end();
        if (it == job_of.end()) continue;
        bound.packed = jobs[it->second].packed;
        // (Its own surface's revision: a surface that shares another's picture
        // shows it while it is still the picture it was packed as.)
        bound.packed_revision = bound.surface->revision();
    }
}

bool GLWorldView::batch_skinned(const State& st, const Element& e) {
    static const Key hung{"straddle"}, worn{"skin"};
    if (!q_.instancing || e.id == highlight_ || e.params.num(hung, 0.0) > 0.5 || e.params.has(worn)) return false;
    if (skin_holder(st, e) != &e) return false;
    const auto it = surfaces_.find(e.id);
    if (it == surfaces_.end() || !dynamic_cast<const Texture*>(it->second.surface)) return false;
    upload_skin(it->second);
    append_record(st, e, batch_for(shape_of(st, e), &it->second));
    return true;
}

void GLWorldView::batch_crate(const State& st, const Element& e) { append_record(st, e, batch_for(shape_of(st, e))); }

void GLWorldView::append_record(const State& st, const Element& e, Batch& b) {
    Placed& p = placed_of(st, e);
    if (!p.recorded) {
        const gl::Mat4& m = box_matrix(st, e).m;
        const gl::Vec3 c = color_of(e, {0.8f, 0.5f, 0.25f});
        std::copy(m.m, m.m + 16, p.record.begin());
        const float mat[8] = {c.x, c.y, c.z, static_cast<float>(e.params.num(Key{"roughness"}, 0.6)),
                              static_cast<float>(e.params.num(Key{"surface"}, 3.0)),
                              static_cast<float>(e.params.num(Key{"emissive"}, 0.0)), 0.0f,
                              static_cast<float>(e.params.num(Key{"mirror"}, 0.0))};
        std::copy(mat, mat + 8, p.record.begin() + 16);
        p.record[24] = static_cast<float>(e.params.num(Key{"depth_layer"}, 0.0)), p.record[25] = p.record[26] = p.record[27] = 0.0f;
        p.recorded = true;
    }
    b.data.insert(b.data.end(), p.record.begin(), p.record.end());
}

auto GLWorldView::batch_for(const gl::Mesh& mesh, BoundSurface* skin) -> Batch& {
    const auto [at, made] = batch_index_.try_emplace({&mesh, skin}, batches_.size());
    if (!made) return batches_[at->second];
    return batches_.emplace_back(Batch{&mesh, {}, skin});
}

void GLWorldView::batch(const gl::Mesh& mesh, const gl::Mat4& local, const gl::Vec3& albedo, float roughness, float surface, float emissive, float highlight, float mirror) {
    Batch& b = batch_for(mesh);
    b.data.insert(b.data.end(), local.m, local.m + 16);
    b.data.insert(b.data.end(), {albedo.x, albedo.y, albedo.z, roughness, surface, emissive, highlight, mirror, 0.0f, 0.0f, 0.0f, 0.0f});
}

void GLWorldView::flush_batches(const gl::Program& p, bool scene) {
    bool any = false;
    // Every batch's things, each batch nearest first, sent to the card in
    // one upload; each batch is drawn from where its own begin in it.
    constexpr std::size_t k = gl::Mesh::kInstanceFloats;
    instance_stream_.clear();
    for (Batch& b : batches_) {
        if (b.data.empty()) continue;
        if (scene && b.data.size() > k) nearest_first(b.data);
        instance_stream_.insert(instance_stream_.end(), b.data.begin(), b.data.end());
    }
    const gl::GLuint stream = instance_stream_.empty() ? 0 : instances_.upload(instance_stream_);
    std::size_t first = 0;
    // A box's far side is never seen from outside it: in the scene it is not
    // drawn, or far off - where depth is coarser than a wall is thick - the
    // two sides fight for every pixel. (Only the box: its faces are wound
    // outward; other shapes are drawn whole. The shadow pass culls as it
    // likes: its own, set before it.)
    for (Batch& b : batches_) {
        if (b.data.empty()) continue;
        if (scene) {
            if (b.mesh == &cube_) gl::glEnable(gl::GL_CULL_FACE), gl::glCullFace(gl::GL_BACK);
            else gl::glDisable(gl::GL_CULL_FACE);
        }
        if (!any) {
            p.set("uInstanced", 1);
            p.set("uFrame", frame_matrix_);
            if (scene) {
                p.set("uTexMix", 0.0f);
                p.set("uGlow", 0.0f);
                p.set("uSkin", 0.0f);
                p.set("uScreenUV", 0.0f);
                p.set("uCRT", 0.0f);
            }
            any = true;
        }
        const auto n = static_cast<gl::GLsizei>(b.data.size() / k);
        const auto* tex = scene && b.skin ? dynamic_cast<const Texture*>(b.skin->surface) : nullptr;
        if (tex) {
            const Element& m = tex->map();
            b.skin->texture.bind(0);
            p.set("uTexMix", 1.0f), p.set("uSkin", 1.0f), p.set("uSkinFramed", 1.0f), p.set("uSkinOwn", 1.0f);
            p.set("uSkinTile", static_cast<float>(m.params.num("tile", 0.0)));
            p.set("uSkinBlend", static_cast<float>(m.params.num("blend", 0.0)));
            p.set("uSkinRelief", static_cast<float>(m.params.num("relief", 0.0)));
        }
        b.mesh->draw_instanced(stream, n, first);
        first += static_cast<std::size_t>(n);
        if (tex) p.set("uTexMix", 0.0f), p.set("uSkin", 0.0f), p.set("uSkinFramed", 0.0f), p.set("uSkinOwn", 0.0f), p.set("uSkinTile", 0.0f), p.set("uSkinBlend", 0.0f), p.set("uSkinRelief", 0.0f);
        if (scene) ++times_.draws, times_.instanced += n;
        b.data.clear();
    }
    if (any) p.set("uInstanced", 0);
    if (scene) gl::glDisable(gl::GL_CULL_FACE);
}

void GLWorldView::nearest_first(std::vector<float>& data) {
    // Each by how far its far side can be from the eye: what is near and
    // small first, the walls round everything last - so what is hidden is
    // found hidden before it is shaded.
    constexpr std::size_t k = gl::Mesh::kInstanceFloats;
    const std::size_t n = data.size() / k;
    order_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float* m = &data[i * k];
        const gl::Vec3 c = frame_matrix_.transform_point({m[12], m[13], m[14]}) - cam_eye_;
        const float r2 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2] + m[4] * m[4] + m[5] * m[5] + m[6] * m[6] +
                         m[8] * m[8] + m[9] * m[9] + m[10] * m[10];
        order_[i] = {std::sqrt(gl::dot(c, c)) + 0.5f * std::sqrt(r2), static_cast<uint32_t>(i)};
    }
    if (std::is_sorted(order_.begin(), order_.end())) return;
    std::sort(order_.begin(), order_.end());
    sorted_.resize(data.size());
    for (std::size_t i = 0; i < n; ++i) std::copy_n(&data[order_[i].second * k], k, &sorted_[i * k]);
    data.swap(sorted_);
}

}  // namespace sg::render
