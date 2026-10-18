// The GL view's proxies (Quality::proxies): the still things of a room, cell
// by cell, merged into a mesh or two whose every corner carries its thing's
// instance record - drawn by the scene's instanced path, every thing as it
// would be drawn with others of its shape, a cell in a call or two. Merged a
// cell or two a frame, never a room at once; a cell a thing in which changed
// merged again once it has stood a while, without what changed.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <cmath>

namespace sg::render {

std::vector<float> GLWorldView::corners_for(const State& st, const Element& e) const {
    static const Key shape{"shape"}, bevel{"bevel"}, taper{"taper"}, model{"model"}, bend{"bend"};
    const std::string s = e.params.get_or<std::string>(shape, "");
    if (s == "model") {
        const auto* space = dynamic_cast<const Spatial3D*>(&st);
        const std::vector<float>* corners = space ? space->model(Key{e.params.get_or<std::string>(model, "")}) : nullptr;
        if (corners && !corners->empty()) return *corners;
        return gl::cube_vertices();
    }
    const double tp = e.params.num(taper, 1.0);
    if (s == "sphere") return gl::sphere_vertices();
    if (s == "cylinder") return tp == 1.0 ? gl::cylinder_vertices() : gl::cylinder_vertices(28, static_cast<float>(tp));
    const double r = e.params.num(bevel, 0.0);
    if (r <= 0.0 && tp == 1.0) return e.params.num(bend, 0.0) != 0.0 ? gl::grid_cube_vertices(14) : gl::cube_vertices();
    return gl::rounded_box_vertices(static_cast<float>(e.params.num(keys::sx, 1.0)), static_cast<float>(e.params.num(keys::sy, 1.0)),
                                    static_cast<float>(e.params.num(keys::sz, 1.0)), static_cast<float>(r), static_cast<float>(tp));
}

namespace {
// Whether a thing may be merged at all, by what it is (not where): what the
// scene would draw with others of its shape, hung from nothing.
bool mergeable(const GLWorldView& view, const Element& e, bool instanceable, bool skinned) {
    static const Key glass{"glass"}, add{"add"}, unseen{"unseen"}, cutout{"cutout"}, skin{"skin"}, shape{"shape"}, sprite{"sprite"};
    (void)view;
    if (!e.alive || e.kind != kinds::mesh || skinned || !instanceable) return false;
    if (e.params.num(glass, 0.0) > 0.0 || e.params.num(add, 0.0) > 0.5 || e.params.num(unseen, 0.0) > 0.5 || e.params.has(keys::parent)) return false;
    if (e.params.num(cutout, 0.0) > 0.0 && e.params.has(skin)) return false;
    return !e.params.is(shape, "sprite");
}
}  // namespace

auto GLWorldView::proxies_in(const Spatial3D& room) -> RoomProxies* {
    static const Key cell_key{"proxy_cell"};
    RoomProxies& rp = proxies_[&room];
    const auto& elements = room.elements();
    if (rp.structure != room.structure()) {
        // Which cell each thing that may be merged stands in: found again
        // when things come or go; their meshes merged a cell at a time.
        rp.structure = room.structure();
        rp.last.assign(elements.size(), 0);
        rp.cells.clear();
        const double cell = std::max(room.params().num(cell_key, 16.0), 1.0);
        std::map<std::pair<long long, long long>, std::size_t> at;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const Element& e = elements[i];
            if (!mergeable(*this, e, instanceable(e), skinned(e)) || is_sprite(e) || cuts_out(e)) continue;
            const gl::Mat4& m = box_matrix(room, e).m;
            const auto [it, made] = at.try_emplace({static_cast<long long>(std::floor(m.m[12] / cell)), static_cast<long long>(std::floor(m.m[14] / cell))},
                                                   rp.cells.size());
            if (made) rp.cells.push_back(std::make_unique<ProxyCell>());
            rp.cells[it->second]->candidates.push_back(i);
        }
    }
    // Cells not merged yet, or changed and since stood a while: merged, a
    // few a frame (proxy_budget_) - until then their things are drawn each
    // on its own.
    for (const auto& c : rp.cells) {
        if (proxy_budget_ <= 0) break;
        if (c->candidates.size() < 4) continue;
        if (!c->merged || (c->changed != 0 && frame_count_ > c->changed + 30)) {
            merge_cell(room, rp, *c);
            --proxy_budget_;
        }
    }
    return &rp;
}

void GLWorldView::merge_cell(const Spatial3D& room, RoomProxies& rp, ProxyCell& c) {
    const auto& elements = room.elements();
    const auto plan = draw_plans_.find(&room);
    constexpr std::size_t k = gl::Mesh::kInstanceFloats;
    static std::vector<float> boxes, others;
    boxes.clear(), others.clear();
    c.members.clear(), c.stamps.clear();
    std::size_t corners_in = 0;
    for (std::size_t i : c.candidates) {
        if (i >= elements.size()) continue;
        const Element& e = elements[i];
        const uint64_t was = rp.last[i];
        rp.last[i] = e.params.stamp();
        // Standing still: not moved since its room was laid out, the same
        // since it was last merged (one that changed waits out a merging).
        if (!mergeable(*this, e, instanceable(e), skinned(e))) continue;
        if (plan != draw_plans_.end() && i < plan->second.mover.size() && plan->second.mover[i]) continue;
        if (was != 0 && was != e.params.stamp()) continue;
        // Small things only: a big model is drawn as well with others of its
        // shape, and its every corner carrying a record would be its size four
        // times over.
        const gl::Mesh& mesh = shape_of(room, e);
        const std::vector<float> corners = corners_for(room, e);
        const std::size_t n = corners.size() / 8;
        if (n > kProxyCorners || corners_in + n > kProxyCellCorners) continue;
        corners_in += n;
        const auto& record = record_of(room, e);
        std::vector<float>& into = &mesh == &cube_ || &mesh == &bent_ ? boxes : others;
        for (std::size_t v = 0; v + 8 <= corners.size(); v += 8) {
            into.insert(into.end(), corners.begin() + static_cast<std::ptrdiff_t>(v), corners.begin() + static_cast<std::ptrdiff_t>(v + 8));
            into.insert(into.end(), record.begin(), record.begin() + k);
        }
        c.members.push_back(i);
        c.stamps.push_back(e.params.stamp());
    }
    c.any_boxes = !boxes.empty(), c.any_others = !others.empty();
    if (c.any_boxes) c.boxes.create_merged(boxes);
    if (c.any_others) c.others.create_merged(others);
    c.merged = true, c.changed = 0;
    ++times_.proxies_merged;
}

void GLWorldView::draw_proxies(const std::vector<const ProxyCell*>& cells) {
    const gl::Program& p = *scene_;
    p.set("uInstanced", 1);
    p.set("uFrame", frame_matrix_);
    p.set("uTexMix", 0.0f);
    p.set("uGlow", 0.0f);
    p.set("uSkin", 0.0f);
    p.set("uScreenUV", 0.0f);
    p.set("uCRT", 0.0f);
    // (A box's far side not drawn, as its batch draws it; the rest whole.)
    gl::glEnable(gl::GL_CULL_FACE);
    gl::glCullFace(gl::GL_BACK);
    for (const ProxyCell* c : cells)
        if (c->any_boxes) c->boxes.draw(), ++times_.proxy_calls;
    gl::glDisable(gl::GL_CULL_FACE);
    for (const ProxyCell* c : cells) {
        if (c->any_others) c->others.draw(), ++times_.proxy_calls;
        times_.proxied += static_cast<int>(c->members.size());
    }
    p.set("uInstanced", 0);
}

}  // namespace sg::render
