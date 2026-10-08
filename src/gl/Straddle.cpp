// What stands half through a doorway, seen whole from either side
// (GLWorldView::draw_straddlers).
#include <algorithm>
#include <cmath>

#include "sg/gl/World.hpp"

namespace sg::render {

void GLWorldView::draw_straddlers(const PlacedRoom& placed, const Spatial3D& room) {
    if (placed.image || !scene_) return;
    const StateGraph* g = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr);
    if (!g || g->seams().empty()) return;
    static const Key ball{"ball"}, hangs{"hangs_on"}, hung{"straddle"}, unseen{"unseen"}, glass{"glass"};
    const Key camera = SpatialState::camera_id();
    static const Key loose{"loose"};
    // Which things of `st` cross its doorway `door`: centred in the opening,
    // their bounds through its plane. Only a thing that moves - it, or what
    // it hangs from, has a velocity (a body) or says it is `loose` (moved
    // from outside, by a physics) - since what stands still is the room's
    // own building, each side its own: a door in its frame, the steps of a
    // stair, a print in a gallery, built on both sides and drawn twice it
    // fights itself. Not what hangs on an opening, not a door's leaf (it
    // hangs in both already), not glass or a picture on a card.
    const auto crossing = [&](const Spatial3D& st, const Element& door) {
        std::vector<std::size_t> out;
        const Pose d = world_pose(st, door);
        const Vec3d n = facing(d), side = across_of(d), up = up_of(d);
        const double half_w = door.params.num(keys::w, 3.0) * 0.5, h = door.params.num(keys::h, 2.0);
        const auto& things = st.elements();
        for (std::size_t i = 0; i < things.size(); ++i) {
            const Element& e = things[i];
            if (!e.alive || e.kind != kinds::mesh || e.params.num(hung, 0.0) > 0.5 || e.params.num(unseen, 0.0) > 0.5 ||
                e.params.num(glass, 0.0) > 0.0 || is_sprite(e))
                continue;
            bool built = false, moves = false;
            const Element* at = &e;
            for (int k = 0; k < 8 && at && !built; ++k) {
                built = at->params.has(hangs);
                moves = moves || at->params.has(keys::vx) || at->params.num(loose, 0.0) > 0.5;
                const std::string parent = at->params.get_or<std::string>(keys::parent, "");
                at = parent.empty() ? nullptr : st.find(Key{parent});
            }
            if (built || !moves) continue;
            const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
            const Pose w = world_pose(st, e);
            const Vec3d c = place_in(w, {0.0, sy * 0.5, 0.0}) - d.position;
            const double r = 0.5 * std::sqrt(sx * sx + sy * sy + sz * sz);
            const double out_of = c.x * n.x + c.y * n.y + c.z * n.z;
            const double along = c.x * side.x + c.y * side.y + c.z * side.z;
            const double above = c.x * up.x + c.y * up.y + c.z * up.z;
            if (std::fabs(out_of) < r && std::fabs(along) < half_w && above > -h && above < h) out.push_back(i);
        }
        return out;
    };
    // What crosses a doorway, kept until something in its room moves - not
    // its camera, which the eye carried across moves every frame.
    const auto crossing_at = [&](const Spatial3D& st, const Element& door) -> const std::vector<std::size_t>& {
        Straddlers& known = straddlers_[&st];
        uint64_t v = mix_stamp(0x9e3779b97f4a7c15ull, st.elements().size());
        for (const Element& e : st.elements())
            if (e.id != camera) v = mix_stamp(v, (e.params.stamp() << 1) | (e.alive ? 1u : 0u));
        if (v != known.version) known.version = v, known.by_doorway.clear();
        auto found = known.by_doorway.find(door.id);
        if (found == known.by_doorway.end()) found = known.by_doorway.emplace(door.id, crossing(st, door)).first;
        return found->second;
    };
    const Pose was = frame_;
    const int clips = clip_count_;
    for (const Seam& sm : g->seams()) {
        if (sm.a == sm.b || sm.boundary_a.empty() || sm.boundary_b.empty()) continue;
        const bool here_a = sm.a == room.id();
        if ((!here_a && sm.b != room.id()) || !g->admits(sm, Channel::Objects)) continue;
        const auto* far = dynamic_cast<const Spatial3D*>(g->find(here_a ? sm.b : sm.a));
        const Element* mine = room.find(here_a ? sm.boundary_a.front() : sm.boundary_b.front());
        const Element* theirs = far ? far->find(here_a ? sm.boundary_b.front() : sm.boundary_a.front()) : nullptr;
        // (Through two balls a world is at another scale: what is in the
        // glass is the world's own, seen through it.)
        if (!mine || !theirs || mine->params.has(ball) || theirs->params.has(ball)) continue;
        const std::vector<std::size_t>& found = crossing_at(*far, *theirs);
        if (found.empty() || clips >= kMaxBounds) continue;
        // The far room where this doorway puts it. A thing of this room's
        // own that stands where one of them is carried, the same size, is
        // its twin (one thing each side keeps a copy of): drawn here already.
        const Pose carry = through_portal(pose_of(*far, *theirs), pose_of(room, *mine), Pose{});
        const std::vector<std::size_t>& own = crossing_at(room, *mine);
        std::vector<std::size_t> draw;
        for (const std::size_t i : found) {
            const Element& e = far->elements()[i];
            const Vec3d at = compose_pose(carry, pose_of(*far, e)).position;
            const bool twin = std::any_of(own.begin(), own.end(), [&](std::size_t j) {
                const Element& o = room.elements()[j];
                const Vec3d d = pose_of(room, o).position - at;
                return d.x * d.x + d.y * d.y + d.z * d.z < 1e-4 &&
                       std::fabs(o.params.num(keys::sx, 1.0) - e.params.num(keys::sx, 1.0)) < 1e-3 &&
                       std::fabs(o.params.num(keys::sy, 1.0) - e.params.num(keys::sy, 1.0)) < 1e-3 &&
                       std::fabs(o.params.num(keys::sz, 1.0) - e.params.num(keys::sz, 1.0)) < 1e-3;
            });
            if (!twin) draw.push_back(i);
        }
        if (draw.empty()) continue;
        // Cut to this side of the doorway.
        set_frame(compose_pose(was, carry));
        const HalfSpace cut = room_side(room, *mine, was);
        scene_->set(clip_uniform(clips), static_cast<float>(cut.normal.x), static_cast<float>(cut.normal.y),
                    static_cast<float>(cut.normal.z), static_cast<float>(cut.offset));
        scene_->set("uClipCount", clips + 1);
        for (const std::size_t i : draw) draw_crate(*far, far->elements()[i]);
        scene_->set("uClipCount", clips);
        set_frame(was);
    }
}

}  // namespace sg::render
