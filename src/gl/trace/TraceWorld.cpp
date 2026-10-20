// The world as rays meet it: its things' triangles under trees of boxes,
// their instances under one more, and its ground as heights round the eye.
// Each shape's tree made once, by what it is made from (a skin's boxes fitted
// round it again as its joints move); the instances' tree again only when a
// thing has moved; the ground's heights again only when the eye has gone on.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "TraceShaders.hpp"
#include "Tracer.hpp"

namespace sg::render::trace {

namespace {

constexpr int kTriFloats = 12;  // a triangle: three texels

float linear(unsigned char c) { return std::pow(c / 255.0f, 2.2f); }
// A colour kept in eight bits each as its square root (finer in the dark,
// where the eye sees steps), the three in one whole number.
float pack_colour(float r, float g, float b) {
    const auto q = [](float v) { return static_cast<int>(std::lround(std::sqrt(std::clamp(v, 0.0f, 1.0f)) * 255.0f)); };
    return static_cast<float>(q(r) | (q(g) << 8) | (q(b) << 16));
}

struct Picture {
    const Spatial3D::Picture* p = nullptr;
    // Its texel at (u, v), as GL reads it (the first row at v = 0), laid again past its edges.
    const unsigned char* at(float u, float v) const {
        const int x = ((static_cast<int>(std::floor(u * p->w)) % p->w) + p->w) % p->w;
        const int y = ((static_cast<int>(std::floor(v * p->h)) % p->h) + p->h) % p->h;
        return &p->rgba[(static_cast<std::size_t>(y) * p->w + x) * 4];
    }
};

// The corners of a shape (8 floats each: where, which way, uv), as the
// renderer makes its meshes (GLWorldView::find_shape) - a skin's as it was
// bound (posed after).
bool corners_of(const Spatial3D& space, const Element& e, std::vector<float>& out, std::string& key, Key& skin, uint64_t& made,
                const std::function<std::string(const std::vector<float>&, uint64_t)>& content) {
    static const Key shape{"shape"}, model{"model"}, bevel{"bevel"}, taper{"taper"};
    const std::string s = e.kind == kinds::wall ? std::string() : e.params.get_or<std::string>(shape, "");
    made = 0;
    if (s == "model") {
        const Key name{e.params.get_or<std::string>(model, "")};
        if (e.params.num(Key{"skinned"}, 0.0) > 0.5) {
            const std::vector<float>* bound = space.skin_bound(name);
            if (!bound || bound->empty()) return false;
            out.clear();
            out.reserve(bound->size() / Spatial3D::kSkinFloats * 8);
            for (std::size_t i = 0; i + Spatial3D::kSkinFloats <= bound->size(); i += Spatial3D::kSkinFloats)
                out.insert(out.end(), bound->begin() + static_cast<std::ptrdiff_t>(i), bound->begin() + static_cast<std::ptrdiff_t>(i + 8));
            skin = name;
            made = space.skin_revision(name);
            key = "skin:" + std::to_string(reinterpret_cast<std::uintptr_t>(&space)) + ":" + name.str() + ":" + e.id.str();
            return true;
        }
        const std::vector<float>* corners = space.model(name);
        if (!corners || corners->empty()) return false;
        out = *corners;
        made = space.model_revision(name);
        // (By what its corners are, not whose: the same model in two rooms is one shape.)
        key = "model:" + content(*corners, made);
        return true;
    }
    const double tp = e.params.num(taper, 1.0), r = e.params.num(bevel, 0.0);
    if (s == "sphere") {
        static const std::vector<float> ball = gl::sphere_vertices();
        out = ball, key = "sphere";
        return true;
    }
    if (s == "cylinder") {
        out = gl::cylinder_vertices(28, static_cast<float>(tp)), key = "cylinder:" + std::to_string(std::lround(tp * 1000));
        return true;
    }
    if (r <= 0.0 && tp == 1.0) {
        static const std::vector<float> box = gl::cube_vertices();
        out = box, key = "cube";
        return true;
    }
    const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
    out = gl::rounded_box_vertices(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz), static_cast<float>(r), static_cast<float>(tp));
    const auto mm = [](double v) { return std::to_string(std::lround(v * 1000)); };
    key = "box:" + mm(sx) + "," + mm(sy) + "," + mm(sz) + "," + mm(r) + "," + mm(tp);
    return true;
}

// A tree's boxes (its nodes `first` on, `count` of them, as laid) fitted
// again round what is under them, its things moved (`things`, the laid
// things from `thing_base` on): the children, made after their parents, first.
void refit(float* nodes, uint32_t first, uint32_t count, const std::vector<Box>& things, uint32_t thing_base) {
    for (uint32_t k = first + count; k-- > first;) {
        float* node = nodes + static_cast<std::size_t>(k) * kNodeFloats;
        for (int side = 0; side < 2; ++side) {
            float* c = node + side * 8;
            if (c[0] > c[4]) continue;
            Box b;
            const uint32_t ref = static_cast<uint32_t>(c[3]), n = static_cast<uint32_t>(c[7]);
            if (n > 0) {
                for (uint32_t i = 0; i < n; ++i) b.grow(things[ref - thing_base + i]);
            } else {
                const float* child = nodes + static_cast<std::size_t>(ref) * kNodeFloats;
                for (int s2 = 0; s2 < 2; ++s2) {
                    const float* cc = child + s2 * 8;
                    if (cc[0] > cc[4]) continue;
                    Box cb;
                    cb.lo[0] = cc[0], cb.lo[1] = cc[1], cb.lo[2] = cc[2], cb.hi[0] = cc[4], cb.hi[1] = cc[5], cb.hi[2] = cc[6];
                    b.grow(cb);
                }
            }
            c[0] = b.lo[0], c[1] = b.lo[1], c[2] = b.lo[2], c[4] = b.hi[0], c[5] = b.hi[1], c[6] = b.hi[2];
        }
    }
}

void tri_floats(float* out, const float* a, const float* b, const float* c) {
    out[0] = a[0], out[1] = a[1], out[2] = a[2];
    out[4] = b[0] - a[0], out[5] = b[1] - a[1], out[6] = b[2] - a[2];
    out[8] = c[0] - a[0], out[9] = c[1] - a[1], out[10] = c[2] - a[2];
}

}  // namespace

bool Asked::operator==(const Asked& o) const {
    return sun == o.sun && mirror == o.mirror && lamps == o.lamps && step == o.step && rough == o.rough && far == o.far && sky == o.sky && now == o.now;
}

void Texels::put(const std::vector<float>& data, std::size_t from, std::size_t count) {
    static const std::vector<float> nothing(4, 0.0f);
    const std::vector<float>& d = data.empty() ? nothing : data;
    if (!buffer) gl::glGenBuffers(1, &buffer), gl::glGenTextures(1, &texture);
    gl::glBindBuffer(gl::GL_TEXTURE_BUFFER, buffer);
    if (d.size() > floats) {
        // (Room for more than now, so a world growing does not make it anew each frame.)
        floats = d.size() + d.size() / 4;
        gl::glBufferData(gl::GL_TEXTURE_BUFFER, static_cast<gl::GLsizeiptr>(floats * sizeof(float)), nullptr, gl::GL_STATIC_DRAW);
        from = 0, count = d.size();
        gl::glBindTexture(gl::GL_TEXTURE_BUFFER, texture);
        gl::glTexBuffer(gl::GL_TEXTURE_BUFFER, gl::GL_RGBA32F, buffer);
        gl::glBindTexture(gl::GL_TEXTURE_BUFFER, 0);
    }
    count = std::min(count, d.size() - std::min(from, d.size()));
    if (count > 0)
        gl::glBufferSubData(gl::GL_TEXTURE_BUFFER, static_cast<gl::GLintptr>(from * sizeof(float)), static_cast<gl::GLsizeiptr>(count * sizeof(float)),
                            d.data() + from);
    gl::glBindBuffer(gl::GL_TEXTURE_BUFFER, 0);
}

void Texels::bind(int unit) const {
    gl::glActiveTexture(gl::GL_TEXTURE0 + static_cast<gl::GLenum>(unit));
    gl::glBindTexture(gl::GL_TEXTURE_BUFFER, texture);
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

Texels::~Texels() {
    if (texture) gl::glDeleteTextures(1, &texture);
    if (buffer) gl::glDeleteBuffers(1, &buffer);
}

auto Tracer::shape_of(const Spatial3D& space, const Element& e) -> Shape* {
    // (Asked of every thing every frame: what it was last, while its params
    // and its model's making hold - its corners never read again for it.)
    static const Key shape{"shape"}, model{"model"};
    const std::string* name = e.params.is(shape, "model") ? e.params.text(model) : nullptr;
    const uint64_t made = name ? (e.params.num(Key{"skinned"}, 0.0) > 0.5 ? space.skin_revision(Key{*name}) : space.model_revision(Key{*name})) : 0;
    Worn& w = worn_[&e];
    if (w.shape && w.stamp == e.params.stamp() && w.made == made) return w.shape;
    w.stamp = e.params.stamp(), w.made = made;
    w.shape = make_shape(space, e);
    return w.shape;
}

auto Tracer::make_shape(const Spatial3D& space, const Element& e) -> Shape* {
    std::vector<float> corners;
    std::string key;
    Key skin;
    uint64_t made = 0;
    const auto content = [&](const std::vector<float>& c, uint64_t rev) {
        Content& k = content_[&c];
        if (!k.known || k.made != rev) k.made = rev, k.known = true, k.key = GLWorldView::corners_key(c);
        return std::to_string(k.key.a) + ":" + std::to_string(k.key.b) + ":" + std::to_string(k.key.n);
    };
    if (!corners_of(space, e, corners, key, skin, made, content)) return nullptr;
    // What it wears, and where it is cut out, are the shape's too.
    static const Key skin_key{"skin"}, uv_key{"uv"}, cutout_key{"cutout"};
    const std::string worn = e.params.num(uv_key, 0.0) > 0.5 ? e.params.get_or<std::string>(skin_key, "") : std::string();
    const Spatial3D::Picture* pic = worn.empty() ? nullptr : space.picture(Key{worn});
    const double cutout = pic ? e.params.num(cutout_key, 0.0) : 0.0;
    // (A skin's picture changes as it talks - its face: its shape is its own
    // all the same, coloured as it was first worn.)
    if (pic && skin.empty())
        key += "|" + std::to_string(reinterpret_cast<std::uintptr_t>(pic)) + ":" + std::to_string(pic->revision) + ":" + std::to_string(cutout);
    auto it = shapes_.find(key);
    if (it != shapes_.end()) {
        // (Its key is what its corners are: made again, it is another key.)
        return &it->second;
    }
    Shape s;
    s.made = made, s.skin = skin, s.space = &space;
    const std::size_t n = corners.size() / 24;
    std::vector<Box> boxes;
    std::vector<float> tris;
    std::vector<float> masks;
    std::vector<int> mask_of;
    boxes.reserve(n), tris.reserve(n * kTriFloats);
    Picture pc{pic};
    for (std::size_t t = 0; t < n; ++t) {
        const float* a = &corners[t * 24];
        const float* b = a + 8;
        const float* c = a + 16;
        float albedo[3] = {1.0f, 1.0f, 1.0f};
        int mask = 0;
        if (pic && pic->w > 0 && pic->h > 0 && pic->rgba.size() >= static_cast<std::size_t>(pic->w) * pic->h * 4) {
            // Its colour: the picture's, at four places over it.
            float sum[3] = {0, 0, 0};
            const float at[4][2] = {{1.0f / 3, 1.0f / 3}, {2.0f / 3, 1.0f / 6}, {1.0f / 6, 2.0f / 3}, {1.0f / 6, 1.0f / 6}};
            for (const auto& w : at) {
                const float u = a[6] + w[0] * (b[6] - a[6]) + w[1] * (c[6] - a[6]), v = a[7] + w[0] * (b[7] - a[7]) + w[1] * (c[7] - a[7]);
                const unsigned char* px = pc.at(u, v);
                for (int k = 0; k < 3; ++k) sum[k] += linear(px[k]) * 0.25f;
            }
            std::copy_n(sum, 3, albedo);
            if (cutout > 0.0) {
                // Cut out of its picture: which of its 64 parts are solid
                // (each, where most of four points of it are) - a software
                // opacity micromap, so a leaf's shadow is a leaf's.
                uint32_t bits[4] = {0, 0, 0, 0};
                int solid = 0;
                for (int j = 0; j < 8; ++j)
                    for (int i = 0; i + j < 8; ++i)
                        for (int up = 0; up < 2; ++up) {
                            if (up && i + j > 6) continue;
                            // (Its corners, in eighths of the triangle's edges.)
                            float cu[3], cv[3];
                            if (!up) cu[0] = i, cv[0] = j, cu[1] = i + 1, cv[1] = j, cu[2] = i, cv[2] = j + 1;
                            else cu[0] = i + 1, cv[0] = j, cu[1] = i + 1, cv[1] = j + 1, cu[2] = i, cv[2] = j + 1;
                            const float mu = (cu[0] + cu[1] + cu[2]) / 24.0f, mv = (cv[0] + cv[1] + cv[2]) / 24.0f;
                            int held = 0;
                            for (int k = 0; k < 4; ++k) {
                                const float pu = k == 3 ? mu : 0.5f * (mu + cu[k] / 8.0f), pv = k == 3 ? mv : 0.5f * (mv + cv[k] / 8.0f);
                                const float u = a[6] + pu * (b[6] - a[6]) + pv * (c[6] - a[6]), v = a[7] + pu * (b[7] - a[7]) + pv * (c[7] - a[7]);
                                if (pc.at(u, v)[3] >= cutout * 255.0) ++held;
                            }
                            if (held >= 2) {
                                const int bit = up ? (7 - i) + 8 * (7 - j) : i + 8 * j;
                                bits[bit >> 4] |= 1u << (bit & 15);
                                ++solid;
                            }
                        }
                if (solid == 0) continue;  // (wholly cut away: no triangle at all)
                if (solid < 64) {
                    mask = static_cast<int>(masks.size() / 4) + 1;
                    for (uint32_t w : bits) masks.push_back(static_cast<float>(w));
                }
            }
        }
        float f[kTriFloats] = {};
        tri_floats(f, a, b, c);
        f[3] = pack_colour(albedo[0], albedo[1], albedo[2]);
        f[7] = static_cast<float>(mask);
        tris.insert(tris.end(), f, f + kTriFloats);
        mask_of.push_back(mask);
        Box bx;
        bx.grow(a), bx.grow(b), bx.grow(c);
        boxes.push_back(bx);
    }
    if (boxes.empty()) return nullptr;
    Tree tree = build(boxes, 4);
    s.order = tree.order;
    s.bounds = tree.bounds;
    // Laid after the shapes already laid (their triangles in the tree's
    // order) - where the instances' tree was, which is laid after them again.
    nodes_.resize(shape_nodes_);
    s.node_base = static_cast<uint32_t>(nodes_.size() / kNodeFloats);
    s.tri_base = static_cast<uint32_t>(tris_.size() / kTriFloats);
    s.nodes = static_cast<uint32_t>(tree.nodes.size() / kNodeFloats);
    s.tris = static_cast<uint32_t>(tree.order.size());
    const uint32_t mask_base = static_cast<uint32_t>(masks_.size() / 4);
    for (uint32_t i : tree.order) {
        float* f = &tris[static_cast<std::size_t>(i) * kTriFloats];
        if (f[7] > 0.5f) f[7] += static_cast<float>(mask_base);
        tris_.insert(tris_.end(), f, f + kTriFloats);
    }
    masks_.insert(masks_.end(), masks.begin(), masks.end());
    const std::size_t at = nodes_.size();
    nodes_.insert(nodes_.end(), tree.nodes.begin(), tree.nodes.end());
    shift(nodes_, at, s.node_base, s.tri_base);
    shape_nodes_ = nodes_.size();
    shapes_moved_ = true;
    Shape& kept = shapes_.emplace(key, std::move(s)).first->second;
    if (!kept.skin.empty()) kept.made = 0, pose_skin(kept);
    return &kept;
}

void Tracer::pose_skin(Shape& s) {
    if (!s.space) return;
    const uint64_t now = s.space->pose_revision(s.skin);
    if (now == s.made) return;
    s.made = now;
    const std::vector<float>* bound = s.space->skin_bound(s.skin);
    const std::vector<float>* joints = s.space->skin_pose(s.skin);
    if (!bound || !joints) return;
    // Each corner where its joints have it now, as the scene's vertex stage
    // poses it (posed_by_joints).
    const std::size_t corners = bound->size() / Spatial3D::kSkinFloats;
    std::vector<float> at(corners * 3);
    for (std::size_t i = 0; i < corners; ++i) {
        const float* c = &(*bound)[i * Spatial3D::kSkinFloats];
        const float* jt = c + 8;
        const float* wt = c + 12;
        float p[3] = {c[0], c[1], c[2]};
        if (wt[0] + wt[1] + wt[2] + wt[3] > 0.0f) {
            p[0] = p[1] = p[2] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                if (wt[k] <= 0.0f) continue;
                const std::size_t j = static_cast<std::size_t>(jt[k] + 0.5f) * Spatial3D::kJointFloats;
                if (j + 12 > joints->size()) continue;
                const float* m = &(*joints)[j];
                for (int r = 0; r < 3; ++r) p[r] += wt[k] * (m[r * 4] * c[0] + m[r * 4 + 1] * c[1] + m[r * 4 + 2] * c[2] + m[r * 4 + 3]);
            }
        }
        std::copy_n(p, 3, &at[i * 3]);
    }
    std::vector<Box> boxes(s.tris);
    for (uint32_t k = 0; k < s.tris; ++k) {
        const uint32_t t = s.order[k];
        if (static_cast<std::size_t>(t) * 3 + 2 >= corners) continue;
        float* f = &tris_[(static_cast<std::size_t>(s.tri_base) + k) * kTriFloats];
        tri_floats(f, &at[t * 9], &at[t * 9 + 3], &at[t * 9 + 6]);
        boxes[k].grow(&at[t * 9]), boxes[k].grow(&at[t * 9 + 3]), boxes[k].grow(&at[t * 9 + 6]);
    }
    refit(nodes_.data(), s.node_base, s.nodes, boxes, s.tri_base);
    s.bounds = Box{};
    for (const Box& b : boxes) s.bounds.grow(b);
    tris_tex_.put(tris_, static_cast<std::size_t>(s.tri_base) * kTriFloats, static_cast<std::size_t>(s.tris) * kTriFloats);
    nodes_tex_.put(nodes_, static_cast<std::size_t>(s.node_base) * kNodeFloats, static_cast<std::size_t>(s.nodes) * kNodeFloats);
}

void Tracer::gather_things(const std::vector<PlacedRoom>& rooms) {
    static const Key trace_key{"trace"}, cast_key{"cast"}, glass_key{"glass"}, add_key{"add"};
    struct Inst {
        Box box;
        float rows[12];
        uint32_t root;
        float tint, glow;
    };
    std::vector<Inst> insts;
    placed_tris_ = 0;
    // Shapes no longer traced (a room long left, a model made again) let go
    // of, all laid again, once they are a good part of what is laid.
    if (frame_ % 300 == 0 && !shapes_.empty()) {
        std::size_t idle = 0;
        for (const auto& [k, sh] : shapes_)
            if (static_cast<uint64_t>(frame_) > sh.used + 600) idle += sh.tris;
        if (idle > std::max<std::size_t>(200000, tris_.size() / 12 * 3 / 10)) stale_ = true;
    }
    if (stale_) {
        shapes_.clear(), nodes_.clear(), tris_.clear(), masks_.clear(), worn_.clear(), content_.clear();
        shape_nodes_ = 0, shapes_moved_ = true, stale_ = false;
    }
    uint64_t of = 1469598103934665603ULL;
    const auto mix = [&](uint64_t v) { of = (of ^ v) * 1099511628211ULL; };
    for (const PlacedRoom& placed : rooms) {
        if (!placed.room || placed.image) continue;
        if (&placed != &rooms.front() && !placed.sheafed) continue;
        const Spatial3D& room = *placed.room;
        const gl::Mat4 frame = GLWorldView::frame_of(placed.pose);
        for (const Element& e : room.elements()) {
            if (!e.alive || (e.kind != kinds::mesh && e.kind != kinds::wall)) continue;
            // What says it is not traced (`trace` 0), casts nothing (`cast` 0),
            // is glass or light added, or a picture standing up: not met.
            if (e.params.num(trace_key, 1.0) <= 0.0 || e.params.num(cast_key, 1.0) <= 0.0 || e.params.num(glass_key, 0.0) > 0.0 ||
                e.params.num(add_key, 0.0) > 0.5 || GLWorldView::is_sprite(e))
                continue;
            Shape* s = shape_of(room, e);
            if (!s) continue;
            if (!s->skin.empty()) pose_skin(*s), mix(s->made);
            s->used = static_cast<uint64_t>(frame_);
            const gl::Mat4 m = frame * v_.box_matrix(room, e).m;
            const gl::Mat4 inv = m.inverse();
            Inst in;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c) in.rows[r * 4 + c] = inv.m[c * 4 + r];
            // Its box in the world: its shape's corners carried there.
            for (int k = 0; k < 8; ++k) {
                const gl::Vec3 p{(k & 1) ? s->bounds.hi[0] : s->bounds.lo[0], (k & 2) ? s->bounds.hi[1] : s->bounds.lo[1], (k & 4) ? s->bounds.hi[2] : s->bounds.lo[2]};
                const gl::Vec3 w = m.transform_point(p);
                const float f[3] = {w.x, w.y, w.z};
                in.box.grow(f);
            }
            const gl::Vec3 tint = GLWorldView::color_of(e, {0.8f, 0.8f, 0.8f});
            in.tint = pack_colour(tint.x, tint.y, tint.z);
            in.glow = static_cast<float>(e.params.num(Key{"emissive"}, 0.0));
            in.root = s->node_base;
            placed_tris_ += s->tris;
            for (float f : m.m) {
                uint32_t b;
                std::memcpy(&b, &f, 4);
                mix(b);
            }
            mix(s->node_base), mix(e.params.stamp());
            insts.push_back(in);
        }
    }
    mix(insts.size()), mix(shape_nodes_);
    if (shapes_moved_) {
        // (Every shape's tree laid again: what was laid of the instances' goes too.)
        nodes_.resize(shape_nodes_);
        tris_tex_.put(tris_);
        masks_tex_.put(masks_);
        things_of_ = 0;
    }
    if (of == things_of_ && !shapes_moved_) return;
    things_of_ = of;
    // The instances' tree, after the shapes' in the same picture.
    std::vector<Box> boxes(insts.size());
    for (std::size_t i = 0; i < insts.size(); ++i) boxes[i] = insts[i].box;
    Tree tree = build(boxes, 2);
    insts_.clear();
    insts_.reserve(insts.size() * 16);
    for (uint32_t i : tree.order) {
        const Inst& in = insts[i];
        insts_.insert(insts_.end(), in.rows, in.rows + 12);
        insts_.insert(insts_.end(), {static_cast<float>(in.root), in.tint, in.glow, 0.0f});
    }
    nodes_.resize(shape_nodes_);
    const uint32_t root = static_cast<uint32_t>(nodes_.size() / kNodeFloats);
    nodes_.insert(nodes_.end(), tree.nodes.begin(), tree.nodes.end());
    shift(nodes_, shape_nodes_, root, 0);
    tlas_root_ = insts.empty() ? -1 : static_cast<int>(root);
    if (shapes_moved_) nodes_tex_.put(nodes_);
    else nodes_tex_.put(nodes_, shape_nodes_, nodes_.size() - shape_nodes_);
    insts_tex_.put(insts_);
    shapes_moved_ = false;
}

void Tracer::lay_square(Square& s, const std::vector<const gl::Mesh*>& meshes, int n, float cell, const gl::Vec3& eye) {
    // Its corner on a lattice of sixteen cells, so its points are where they
    // were as it moves.
    const float snap = 16.0f * cell;
    const float half = 0.5f * static_cast<float>(n - 1) * cell;
    s.x0 = std::round((eye.x - half) / snap) * snap, s.z0 = std::round((eye.z - half) / snap) * snap;
    s.cell = cell, s.n = n;
    if (!s.points.valid()) {
        s.points.create(n, n, gl::GL_R32F, 0, true);
        s.atlas.create(n, 2 * n, gl::GL_R32F, 0, false);
        for (int l = 0; (n >> l) >= 1; ++l) s.levels.emplace_back().create(n >> l, n >> l, gl::GL_R32F, 0, false);
    }
    if (!heights_prog_) {
        heights_prog_ = std::make_unique<gl::Program>(heights_vs().c_str(), heights_fs().c_str(), "trace heights");
        highest_prog_ = std::make_unique<gl::Program>(gl::post_vs(), highest_fs().c_str(), "trace highest");
    }
    // Its heights at its points: the ground as drawn, seen from above.
    s.points.bind();
    gl::glViewport(0, 0, n, n);
    gl::glDisable(gl::GL_SCISSOR_TEST);
    gl::glDisable(gl::GL_BLEND);
    gl::glDisable(gl::GL_CULL_FACE);
    gl::glClearColor(-1e4f, 0.0f, 0.0f, 1.0f);
    gl::glClearDepth(1.0);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glDepthFunc(gl::GL_LESS);
    heights_prog_->use();
    heights_prog_->set("uSquare", s.x0, s.z0, cell, static_cast<float>(n));
    heights_prog_->set("uDepth", -2000.0f, 6000.0f);
    for (const gl::Mesh* m : meshes) m->draw();
    gl::glDisable(gl::GL_DEPTH_TEST);
    // The highest of each cell, then of each two by two, each level laid one
    // over another in its atlas.
    highest_prog_->use();
    highest_prog_->set("uBelow", 4);
    for (std::size_t l = 0; l < s.levels.size(); ++l) {
        const gl::RenderTarget& below = l == 0 ? s.points : s.levels[l - 1];
        below.bind_color(4);
        highest_prog_->set("uFromPoints", l == 0 ? 1 : 0);
        s.levels[l].bind();
        gl::glViewport(0, 0, s.levels[l].width(), s.levels[l].height());
        v_.screen_.draw();
    }
    for (std::size_t l = 0; l < s.levels.size(); ++l) {
        const int row = l == 0 ? 0 : 2 * n - (n >> (l - 1)), w = s.levels[l].width();
        gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, s.levels[l].framebuffer());
        gl::glBindFramebuffer(gl::GL_DRAW_FRAMEBUFFER, s.atlas.framebuffer());
        gl::glBlitFramebuffer(0, 0, w, w, 0, row, w, row + w, gl::GL_COLOR_BUFFER_BIT, gl::GL_NEAREST);
    }
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

void Tracer::lay_ground(const std::vector<PlacedRoom>& rooms, const gl::Vec3& eye) {
    std::vector<const gl::Mesh*> meshes;
    double cx = 0, cz = 0, rev = 0;
    if (!rooms.empty() && rooms.front().room)
        for (const Element* e : v_.index_of(*rooms.front().room).terrains) {
            if (!e->alive || e->params.num(Key{"trace"}, 1.0) <= 0.0) continue;
            const auto it = v_.terrains_.find(e->id);
            if (it == v_.terrains_.end() || !it->second.mesh.valid()) continue;
            meshes.push_back(&it->second.mesh);
            cx += it->second.cx, cz += it->second.cz, rev += it->second.rev;
            if (meshes.size() == 1) ground_albedo_ = GLWorldView::color_of(*e, {0.80f, 0.62f, 0.42f});
        }
    if (meshes.empty()) {
        squares_[0].n = squares_[1].n = 0;
        return;
    }
    // Laid again when the ground drawn has been taken again two metres or
    // more from where they were laid, or is another ground.
    for (int k = 0; k < 2; ++k) {
        Square& s = squares_[k];
        const double moved = std::hypot(cx - s.cx, cz - s.cz);
        if (s.n > 0 && rev == s.rev && moved < (k == 0 ? 2.0 : 32.0)) continue;
        lay_square(s, meshes, 2048, k == 0 ? 0.5f : 4.0f, eye);
        s.cx = cx, s.cz = cz, s.rev = rev;
    }
}

}  // namespace sg::render::trace
