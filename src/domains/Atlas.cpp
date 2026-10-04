#include "sg/domains/Atlas.hpp"
#include "sg/domains/Room.hpp"

#include <algorithm>

namespace sg {

Doorway& Atlas::glue(Key name, Key room_a, Key portal_a, Key room_b, Key portal_b) {
    if (name.empty()) name = Key{room_a.str() + "<->" + room_b.str()};
    doorways_.push_back(Doorway{name, room_a, portal_a, room_b, portal_b});
    Doorway& d = doorways_.back();
    by_room_[room_a].push_back(doorways_.size() - 1);
    by_room_[room_b].push_back(doorways_.size() - 1);
    return d;
}

bool Atlas::step(const StateGraph& g, const Doorway& d, Key here, Pose& out) {
    const bool forward = (d.room_a == here);
    const Key other = forward ? d.room_b : d.room_a;
    const Key here_portal = forward ? d.portal_a : d.portal_b;
    const Key there_portal = forward ? d.portal_b : d.portal_a;

    const State* here_state = g.find(here);
    const State* there_state = g.find(other);
    if (!here_state || !there_state) return false;
    const Element* hp = here_state->find(here_portal);
    const Element* tp = there_state->find(there_portal);
    if (!hp || !tp) return false;

    // The far room's origin, carried back through the doorway into this
    // room's frame. Composing this pose with any local pose of the far
    // room places it here.
    out = through_portal(world_pose_of(*there_state, *tp), world_pose_of(*here_state, *hp),
                         Vec3d{0, 0, 0}, 0.0);
    return true;
}

std::vector<Chart> Atlas::charts(const StateGraph& g, Key root, int max_depth) const {
    std::vector<Chart> out;
    if (!g.contains(root)) return out;
    out.push_back(Chart{root, Pose{}});

    std::unordered_map<Key, std::size_t> seen{{root, 0}};
    std::vector<std::pair<Key, int>> queue{{root, 0}};
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const Key here = queue[i].first;
        const int depth = queue[i].second;
        if (depth >= max_depth) continue;
        const Pose here_pose = out[seen[here]].pose;

        auto it = by_room_.find(here);
        if (it == by_room_.end()) continue;
        for (std::size_t di : it->second) {
            const Doorway& d = doorways_[di];
            const Key other = (d.room_a == here) ? d.room_b : d.room_a;
            if (seen.count(other)) continue;
            Pose rel;
            if (!step(g, d, here, rel)) continue;
            seen.emplace(other, out.size());
            out.push_back(Chart{other, compose_pose(here_pose, rel)});
            queue.emplace_back(other, depth + 1);
        }
    }
    return out;
}

bool Atlas::placement(const StateGraph& g, Key root, Key other, Pose& out, int max_depth) const {
    for (const Chart& c : charts(g, root, max_depth)) {
        if (c.room != other) continue;
        out = c.pose;
        return true;
    }
    return false;
}

Seam doorway_seam(Key name, Key a, Key pa, Key b, Key pb, const std::vector<std::pair<Key, Key>>& also, bool wraps) {
    Seam seam{name, a, b, Key{name.str() + ".ab"}, Key{name.str() + ".ba"}, Key{name.str() + ".glue.ab"},
              Key{name.str() + ".glue.ba"}, {pa}, {pb}, wraps};
    for (const auto& [x, y] : also) {
        seam.boundary_a.push_back(x);
        seam.boundary_b.push_back(y);
    }
    return seam;
}

namespace {
// A carry made from the two boundary elements where they are when it is used,
// not where they were when glued: it moves with them, and cannot drift from
// them. (It reads its two boundaries besides its own two elements.)
template <class Make>
Transport between(const StateGraph& g, Key a, Key pa, Key b, Key pb, Make make) {
    const StateGraph* graph = &g;
    return [graph, a, pa, b, pb, make](const Element& src, Element& dst) {
        const State* A = graph->find(a);
        const State* B = graph->find(b);
        const Element* here = A ? A->find(pa) : nullptr;
        const Element* there = B ? B->find(pb) : nullptr;
        if (here && there) make(*here, *there)(src, dst);
    };
}
}  // namespace

const Seam& glue_doorway(StateGraph& g, Key name, Key a, Key pa, Key b, Key pb, const std::vector<std::pair<Key, Key>>& also, bool wraps) {
    (void)g.state(a).element(pa), (void)g.state(b).element(pb);  // both sides are there
    const Key ab{name.str() + ".ab"}, ba{name.str() + ".ba"};
    const Key gab{name.str() + ".glue.ab"}, gba{name.str() + ".glue.ba"};
    Functor to_b(ab, a, b), to_a(ba, b, a);
    to_b.on_object(SpatialState::camera_id(), SpatialState::camera_id(), between(g, a, pa, b, pb, portal_carry));
    to_a.on_object(SpatialState::camera_id(), SpatialState::camera_id(), between(g, b, pb, a, pa, portal_carry));
    Functor glue_b(gab, a, b), glue_a(gba, b, a);
    glue_b.on_object(pa, pb, between(g, a, pa, b, pb, seam_carry));
    glue_a.on_object(pb, pa, between(g, b, pb, a, pa, seam_carry));
    // A room's opening glued to something leads somewhere: it is open, not
    // filled in (Room::opens).
    for (const auto& [s, p] : {std::pair{a, pa}, std::pair{b, pb}})
        if (auto* room = dynamic_cast<Room*>(&g.state(s))) room->opens(p, true);
    // The boundary on each side: the doorway, and whatever hangs in it.
    Seam seam = doorway_seam(name, a, pa, b, pb, also, wraps);
    for (const auto& [x, y] : also) {
        glue_b.on_object(x, y, between(g, a, pa, b, pb, pose_carry));
        glue_a.on_object(y, x, between(g, b, pb, a, pa, pose_carry));
    }
    g.set_functor(std::move(to_b));
    g.set_functor(std::move(to_a));
    g.set_functor(std::move(glue_b));
    g.set_functor(std::move(glue_a));
    return g.add_seam(std::move(seam));
}

const Seam& walkway(StateGraph& g, Key name, Key a, Key pa, Key b, Key pb, const std::vector<std::pair<Key, Key>>& also, bool wraps) {
    const Seam& seam = glue_doorway(g, name, a, pa, b, pb, also, wraps);
    for (const bool ab : {true, false}) {
        const State& from = g.state(ab ? a : b);
        const Element& door = from.element(ab ? pa : pb);
        if (door.params.num(Key{"walk"}, 0.0) < 0.5 || door.params.num(Key{"leave"}, 1.0) < 0.5) continue;
        const Key travel = ab ? seam.a_to_b : seam.b_to_a;
        const bool made = std::any_of(g.transitions().begin(), g.transitions().end(), [&](const Transition& t) {
            return t.functor == travel && t.from == from.id();
        });
        if (!made) g.connect(from.id(), Key{"walk." + travel.str()}, ab ? b : a, travel);
    }
    return seam;
}

Cover as_cover(const Atlas& atlas, StateGraph& g) {
    Cover cover;
    for (const Doorway& d : atlas.doorways()) {
        const State* a = g.find(d.room_a);
        const State* b = g.find(d.room_b);
        if (!a || !b) continue;
        const Element* pa = a->find(d.portal_a);
        const Element* pb = b->find(d.portal_b);
        if (!pa || !pb) continue;

        // Rebuilt from the portals every time: a doorway's transition is not
        // separate data that could disagree with the doorway, it *is* the
        // doorway. That is what makes an inconsistent gluing unrepresentable
        // rather than merely detectable.
        const Key fwd{d.name.str() + ".ab"};
        const Key back{d.name.str() + ".ba"};
        // Only the viewer's pose crosses. A doorway is a *relation between*
        // the two portals, not a map that overwrites one with the other:
        // carrying the near doorway onto the far one would move the far room's
        // own door every time somebody walked through it. The doorway itself
        // is the seam's glue, which is checked, never applied in passing.
        glue_doorway(g, d.name, d.room_a, d.portal_a, d.room_b, d.portal_b, {}, d.wraps);

        cover.add(d.name, d.room_a, d.room_b, fwd, back).wraps = d.wraps;
    }
    return cover;
}

std::vector<std::string> travel_defects(const Cover& cover, const StateGraph& g) {
    std::vector<std::string> out;
    for (const Overlap& o : cover.overlaps()) {
        for (const Key fname : {o.u_to_v, o.v_to_u}) {
            const Functor* f = g.functor(fname);
            if (!f) continue;
            const State* target = g.find(f->to());
            if (!target) continue;
            f->for_each_object([&](Key, Key image) {
                const Element* e = target->find(image);
                if (e && e->kind == kinds::portal)
                    out.push_back("overlap " + o.name.str() + ": its transition " + fname.str() +
                                  " writes " + f->to().str() + "." + image.str() +
                                  ", which is a doorway, not a traveller");
            });
        }
    }
    return out;
}

std::vector<std::string> adjacency_defects(const Atlas& atlas, const StateGraph& g, double tolerance) {
    std::vector<std::string> out;
    for (const Doorway& d : atlas.doorways()) {
        const std::pair<Key, Key> sides[2] = {{d.room_a, d.portal_a}, {d.room_b, d.portal_b}};
        for (const auto& side : sides) {
            const State* room = g.find(side.first);
            const Element* portal = room ? room->find(side.second) : nullptr;
            if (!portal) continue;  // descent_defects names a missing side
            const Pose at = world_pose(*room, *portal);
            const bool ball = portal->params.has(Key{"ball"});
            const double r = portal->params.num(Key{"ball"}), hw = portal->params.num(keys::w, 1.0) * 0.5,
                         hh = portal->params.num(keys::h, 2.0) * 0.5;
            const bool inside = portal->params.num(Key{"ball_out"}, 0.0) > 0.5;
            for (const Element& e : room->elements()) {
                if (!e.alive || (e.kind != kinds::wall && e.kind != kinds::mesh)) continue;
                // A box: its eight corners, however it is turned.
                const Pose p = world_pose(*room, e);
                const double sx = e.params.num(keys::sx, 1.0) * 0.5, sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0) * 0.5;
                double lo[3] = {1e18, 1e18, 1e18}, hi[3] = {-1e18, -1e18, -1e18}, far = 0.0, near = 1e18;
                for (const double cx : {-sx, sx})
                    for (const double cy : {0.0, sy})
                        for (const double cz : {-sz, sz}) {
                            const Vec3d c = place_in(p, {cx, cy, cz});
                            const double dist = distance(c, at.position);
                            far = std::max(far, dist), near = std::min(near, dist);
                            const Vec3d l = local_of(at, c);
                            const double v[3] = {l.x, l.y, l.z};
                            for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], v[k]), hi[k] = std::max(hi[k], v[k]);
                        }
                double reach = 0.0;
                if (ball) {
                    // A ball's inner world keeps within its sky; the outer keeps out of it.
                    reach = inside ? far - r : r - near;
                } else if (lo[0] < -tolerance && hi[0] > tolerance && hi[1] > -hh + tolerance && lo[1] < hh - tolerance && hi[2] > -hw + tolerance &&
                           lo[2] < hw - tolerance) {
                    // From the room's side past a doorway's plane, in its opening:
                    // in the way of what is seen through it. (What lies wholly
                    // behind it - the wall a doorway hangs on - is the other
                    // side of a surface the doorway opens.)
                    reach = -lo[0];
                }
                if (reach > tolerance)
                    out.push_back(side.first.str() + "." + e.id.str() + " reaches " + std::to_string(reach) + " m past doorway " +
                                  d.name.str() + ", into the room on the other side");
            }
        }
    }
    return out;
}

std::vector<std::string> descent_defects(const Atlas& atlas, StateGraph& g) {
    const Cover cover = as_cover(atlas, g);
    std::vector<std::string> out = cover.descent_defects(g);
    for (const auto& d : travel_defects(cover, g)) out.push_back(d);
    for (const auto& d : adjacency_defects(atlas, g)) out.push_back(d);

    for (const Doorway& d : atlas.doorways()) {
        const State* a = g.find(d.room_a);
        const State* b = g.find(d.room_b);
        if (!a) out.push_back("doorway " + d.name.str() + ": unknown room " + d.room_a.str());
        if (!b) out.push_back("doorway " + d.name.str() + ": unknown room " + d.room_b.str());
        if (!a || !b) continue;
        const Element* pa = a->find(d.portal_a);
        const Element* pb = b->find(d.portal_b);
        if (!pa || !pb) {
            out.push_back("doorway " + d.name.str() + ": a side is missing its portal element");
            continue;
        }
        if (pa->kind != kinds::portal || pb->kind != kinds::portal)
            out.push_back("doorway " + d.name.str() + ": a side is not a portal element");

        // Placed by the shortest way - which, for a doorway whose rings are the
        // space's shape, is across that doorway itself.
        Pose placement;
        if (d.wraps)
            placement = through_portal(world_pose(*a, *pa), world_pose(*b, *pb), Vec3d{0, 0, 0}, 0.0);
        else if (!atlas.placement(g, d.room_b, d.room_a, placement))
            continue;
        const Pose landed = compose_pose(placement, world_pose(*a, *pa));
        const Pose target = world_pose(*b, *pb);
        if (!same_number(keys::x, landed.position.x, target.position.x, 1e-6) ||
            !same_number(keys::z, landed.position.z, target.position.z, 1e-6))
            out.push_back("doorway " + d.name.str() + ": the two sides are not in the same place");
        // Back to back: the far side faces the way you came from.
        if (!same_number(keys::yaw, landed.yaw, target.yaw + 3.14159265358979, 1e-6))
            out.push_back("doorway " + d.name.str() + ": the two sides do not face each other");
    }
    return out;
}

std::vector<PlacedRoom> nests(const StateGraph& g, Key root, int max_depth) {
    std::vector<PlacedRoom> out;
    std::vector<std::pair<Key, Pose>> todo{{root, Pose{}}};
    std::vector<Key> seen{root};
    for (int depth = 0; depth < max_depth && !todo.empty(); ++depth) {
        std::vector<std::pair<Key, Pose>> next;
        for (const auto& [here, at] : todo)
            for (const Seam& s : g.seams()) {
                if (s.boundary_a.empty() || s.boundary_b.empty() || s.a == s.b) continue;
                const bool forward = s.a == here;
                if (!forward && s.b != here) continue;
                const Key there = forward ? s.b : s.a;
                if (std::find(seen.begin(), seen.end(), there) != seen.end()) continue;
                const State* hs = g.find(here);
                const auto* ts = dynamic_cast<const Spatial3D*>(g.find(there));
                const Element* hp = hs ? hs->find(forward ? s.boundary_a[0] : s.boundary_b[0]) : nullptr;
                const Element* tp = ts ? ts->find(forward ? s.boundary_b[0] : s.boundary_a[0]) : nullptr;
                if (!ts || !hp || !tp || !hp->params.has(Key{"ball"})) continue;
                // Where the far world's origin is, carried back across the
                // boundary into this one's frame - as Atlas::step places a room.
                const Pose placed = compose_pose(at, through_portal(world_pose(*ts, *tp), world_pose(*hs, *hp), Pose{}));
                out.push_back(PlacedRoom{ts, placed, {}});
                seen.push_back(there);
                next.push_back({there, placed});
            }
        todo = std::move(next);
    }
    return out;
}

std::vector<PlacedRoom> place_rooms(StateGraph& g, const Atlas& atlas, Key root, int max_depth) {
    std::vector<PlacedRoom> out;
    for (const Chart& c : atlas.charts(g, root, max_depth)) {
        State* s = g.find(c.room);
        if (!s) continue;
        PlacedRoom placed{static_cast<Spatial3D*>(s), c.pose, {}};
        // The same doorways that place the rooms bound them.
        for (const Doorway& d : atlas.doorways()) {
            if (d.room_a == c.room) placed.doorways.push_back(d.portal_a);
            if (d.room_b == c.room) placed.doorways.push_back(d.portal_b);
        }
        out.push_back(std::move(placed));
    }
    return out;
}

}  // namespace sg
