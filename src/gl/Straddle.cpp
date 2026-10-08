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
    // Which things of a room move: it, or what it hangs from, has a velocity
    // (a body) or says it is `loose` (moved from outside, by a physics) -
    // since what stands still is the room's own building, each side its own:
    // a door in its frame, the steps of a stair, a print in a gallery, built
    // on both sides and drawn twice it fights itself. Not what hangs on an
    // opening, nor a door's leaf (it hangs in both already). What says so -
    // the room's structure, and on each thing its velocity, `loose`, what it
    // hangs on and from - is asked once a frame, however many doorways and
    // views draw it, and only of a thing whose params moved (by its stamp);
    // the things are looked through again only when one of them says
    // otherwise, not whenever anything in the room moves (a clock's hand).
    const uint64_t frame = root_ ? root_->frame_count_ : frame_count_;
    const auto movers = [&](const Spatial3D& st) -> const std::vector<Straddlers::Ball>& {
        Straddlers& known = straddlers_[&st];
        if (known.checked == frame) return known.balls;
        known.checked = frame;
        const auto& things = st.elements();
        bool changed = st.structure() != known.structure || known.stamps.size() != things.size();
        if (changed) {
            known.structure = st.structure();
            known.stamps.assign(things.size(), ~uint64_t{0});
            known.says.assign(things.size(), 0);
        }
        known.moved.assign(things.size(), 0);
        for (std::size_t i = 0; i < things.size(); ++i) {
            const Element& e = things[i];
            const uint64_t stamp = (e.params.stamp() << 1) | (e.alive ? 1u : 0u);
            if (stamp == known.stamps[i]) continue;
            known.stamps[i] = stamp;
            known.moved[i] = 1;
            uint64_t says = mix_stamp(0x9e3779b97f4a7c15ull, (e.alive ? 1u : 0u) | (e.params.has(keys::vx) ? 2u : 0u) |
                                                                 (e.params.num(loose, 0.0) > 0.5 ? 4u : 0u) | (e.params.has(hangs) ? 8u : 0u));
            if (const std::string* parent = e.params.text(keys::parent)) says = mix_stamp(says, std::hash<std::string>{}(*parent));
            if (says != known.says[i]) known.says[i] = says, changed = true;
        }
        if (changed) {
            // Each mover, and what it hangs from (its pose is theirs too).
            known.balls.clear();
            known.chains.clear();
            std::unordered_map<Key, std::size_t> index;
            for (std::size_t i = 0; i < things.size(); ++i) index.emplace(things[i].id, i);
            for (std::size_t i = 0; i < things.size(); ++i) {
                const Element& e = things[i];
                if (!e.alive || e.kind != kinds::mesh || e.id == camera) continue;
                bool built = false, moves = false;
                std::vector<std::size_t> chain;
                std::size_t at = i;
                for (int k = 0; k < 8 && !built; ++k) {
                    const Element& a = things[at];
                    built = a.params.has(hangs);
                    moves = moves || a.params.has(keys::vx) || a.params.num(loose, 0.0) > 0.5;
                    chain.push_back(at);
                    const std::string* parent = a.params.text(keys::parent);
                    const auto up = parent && !parent->empty() ? index.find(Key{*parent}) : index.end();
                    if (up == index.end()) break;
                    at = up->second;
                }
                if (built || !moves) continue;
                known.balls.push_back({i, {}, 0.0, false});
                known.chains.push_back(std::move(chain));
            }
        }
        // Where each is, a ball round it - not glass, nor a picture on a
        // card - found again only where it or what it hangs from moved.
        for (std::size_t k = 0; k < known.balls.size(); ++k) {
            const std::vector<std::size_t>& chain = known.chains[k];
            if (!changed && std::none_of(chain.begin(), chain.end(), [&](std::size_t j) { return known.moved[j] != 0; })) continue;
            Straddlers::Ball& b = known.balls[k];
            const Element& e = things[b.index];
            b.shown = !(e.params.num(hung, 0.0) > 0.5 || e.params.num(unseen, 0.0) > 0.5 || e.params.num(glass, 0.0) > 0.0 || is_sprite(e));
            if (!b.shown) continue;
            const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
            b.centre = place_in(world_pose(st, e), {0.0, sy * 0.5, 0.0});
            b.radius = 0.5 * std::sqrt(sx * sx + sy * sy + sz * sz);
        }
        return known.balls;
    };
    // Which of them cross its doorway `door`: centred in the opening, their
    // bounds through its plane.
    const auto crossing_at = [&](const Spatial3D& st, const Element& door) {
        std::vector<std::size_t> out;
        const std::vector<Straddlers::Ball>& moving = movers(st);
        if (moving.empty()) return out;
        const Pose d = world_pose(st, door);
        const Vec3d n = facing(d), side = across_of(d), up = up_of(d);
        const double half_w = door.params.num(keys::w, 3.0) * 0.5, h = door.params.num(keys::h, 2.0);
        for (const Straddlers::Ball& b : moving) {
            if (!b.shown) continue;
            const std::size_t i = b.index;
            const Vec3d c = b.centre - d.position;
            const double r = b.radius;
            const double out_of = c.x * n.x + c.y * n.y + c.z * n.z;
            const double along = c.x * side.x + c.y * side.y + c.z * side.z;
            const double above = c.x * up.x + c.y * up.y + c.z * up.z;
            if (std::fabs(out_of) < r && std::fabs(along) < half_w && above > -h && above < h) out.push_back(i);
        }
        return out;
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
