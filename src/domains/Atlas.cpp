#include "sg/domains/Atlas.hpp"
#include "sg/domains/Room.hpp"
#include "sg/core/Work.hpp"

#include <set>
#include <sstream>
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
    // The viewer: carried by where the two doorways are in their states (a
    // doorway may hang from an anchor, move with a thing), at their scale.
    const StateGraph* graph = &g;
    const auto travel = [graph](Key x, Key px, Key y, Key py) {
        return [graph, x, px, y, py](const Element& src, Element& dst) {
            const State* X = graph->find(x);
            const State* Y = graph->find(y);
            const Element* here = X ? X->find(px) : nullptr;
            const Element* there = Y ? Y->find(py) : nullptr;
            if (here && there) portal_carry(*X, *here, *Y, *there)(src, dst);
        };
    };
    to_b.on_object(SpatialState::camera_id(), SpatialState::camera_id(), travel(a, pa, b, pb));
    to_a.on_object(SpatialState::camera_id(), SpatialState::camera_id(), travel(b, pb, a, pa));
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
                // A ball onto a world of another scale (a snow globe's) is a
                // window onto it, drawn as its own view, not a world in this one.
                if (std::fabs(seam_scale(*hp, *tp) - 1.0) > 1e-9) continue;
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

namespace {
// A doorway two rooms of one kind meet at, to be made one space: walked, a
// plain opening - not a ball, a screen, a world in its own look, a seam that
// says it differs - and said to be (`sheaf`), unless every doorway that
// agrees is asked for.
bool sheaf_door(const Element& p, bool every) {
    if (p.kind != kinds::portal || !p.alive) return false;
    if (!every && p.params.num(Key{"sheaf"}, 0.0) <= 0.5) return false;
    if (p.params.has(Key{"ball"}) || p.params.num(Key{"feed"}, 0.0) > 0.5 || p.params.num(Key{"crt"}, 0.0) > 0.5) return false;
    if (p.params.num(Key{"own_look"}, 0.0) > 0.5 || p.params.num(Key{"walk"}, 1.0) <= 0.5) return false;
    // (Walls that stand in each other - `differs walls` - are said, and
    // are no reason the two are not one space; anything else said to differ is.)
    const std::string* differs = p.params.text(Key{"differs"});
    if (!differs) return true;
    std::istringstream words(*differs);
    std::string w;
    while (words >> w)
        if (w != "walls") return false;
    return true;
}

// Where a room's walls and floor reach, in its own frame (its walls', or
// the size it says it is); empty if it says neither.
struct Box {
    Vec3d lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    bool empty() const { return lo.x > hi.x; }
    void add(const Vec3d& p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
};
Box bounds_of(const Spatial3D& s) {
    // Its walls, where it has them (they are where it is); else the size it
    // says it is, from its origin.
    Box b;
    for (const Element& e : s.elements()) {
        if (!e.alive || e.kind != kinds::wall) continue;
        const Pose w = world_pose(s, e);
        const double hx = e.params.num(keys::sx, 0.0) * 0.5, hz = e.params.num(keys::sz, 0.0) * 0.5;
        for (double x : {-hx, hx})
            for (double z : {-hz, hz}) {
                const Vec3d c = w.position + rotate_xz({x, 0, z}, w.yaw);
                b.add(c), b.add(c + Vec3d{0, e.params.num(keys::sy, 0.0), 0});
            }
    }
    const Params& p = s.params();
    if (b.empty() && p.has(Key{"room_w"}) && p.has(Key{"room_d"})) {
        b.add({0, 0, 0});
        b.add({p.num(Key{"room_w"}), p.num(Key{"room_h"}, 3.0), p.num(Key{"room_d"})});
    }
    return b;
}
// A box carried into another frame: the box round its eight corners.
Box placed_box(const Box& b, const Pose& at) {
    Box out;
    for (double x : {b.lo.x, b.hi.x})
        for (double y : {b.lo.y, b.hi.y})
            for (double z : {b.lo.z, b.hi.z}) out.add(at.position + rotate_xz({x, y, z}, at.yaw));
    return out;
}
// Two rooms stand in one another (more than a wall's breadth).
bool overlap(const Box& a, const Box& b, double slack) {
    return a.lo.x + slack < b.hi.x && b.lo.x + slack < a.hi.x && a.lo.z + slack < b.hi.z && b.lo.z + slack < a.hi.z &&
           a.lo.y + slack < b.hi.y && b.lo.y + slack < a.hi.y;
}
bool same_pose(const Pose& a, const Pose& b) {
    return distance(a.position, b.position) < 1e-4 && std::fabs(std::remainder(a.yaw - b.yaw, 6.283185307179586)) < 1e-5 &&
           std::fabs(a.pitch - b.pitch) < 1e-5 && std::fabs(a.roll - b.roll) < 1e-5;
}
}  // namespace

bool sheafable(const Spatial3D& a, const Element& pa, const Spatial3D& b, const Element& pb, Pose& b_in_a, bool every) {
    // Two rooms of one kind, at a doorway each has: a plain walked opening,
    // under a sky both or walled both, met by a rigid motion.
    if (!sheaf_door(pa, every) || !sheaf_door(pb, every)) return false;
    if ((a.params().num(Key{"sky"}, 0.0) > 0.5) != (b.params().num(Key{"sky"}, 0.0) > 0.5)) return false;
    if (std::fabs(seam_scale(pa, pb) - 1.0) > 1e-9) return false;
    // Where the far room's origin is, carried back across the doorway into
    // this one's frame - as Atlas::step and nests place a room.
    b_in_a = through_portal(world_pose(b, pb), world_pose(a, pa), Pose{});
    b_in_a.yaw = std::remainder(b_in_a.yaw, 6.283185307179586);
    return true;
}

namespace {
// One space grown from `seed` (its frame the seed's).
std::vector<PlacedRoom> grow(const StateGraph& g, const Spatial3D& seed, std::size_t most, std::size_t lamps, bool every) {
    const auto lamps_of = [](const Spatial3D& s) {
        std::size_t n = 0;
        for (const Element& e : s.elements()) n += e.alive && e.kind == kinds::light ? 1 : 0;
        return n;
    };
    std::vector<PlacedRoom> out;
    const Spatial3D* r = &seed;
    out.push_back(PlacedRoom{r, Pose{}, {}});
    std::vector<Box> boxes{bounds_of(*r)};
    std::size_t lit = lamps_of(*r);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const Spatial3D& here = *out[i].room;
        const Pose at = out[i].pose;
        for (const Seam& s : g.seams()) {
            if (s.wraps || s.boundary_a.empty() || s.boundary_b.empty() || s.a == s.b) continue;
            const bool forward = s.a == here.id();
            if (!forward && s.b != here.id()) continue;
            const auto* there = dynamic_cast<const Spatial3D*>(g.find(forward ? s.b : s.a));
            const Element* hp = here.find(forward ? s.boundary_a[0] : s.boundary_b[0]);
            const Element* tp = there ? there->find(forward ? s.boundary_b[0] : s.boundary_a[0]) : nullptr;
            Pose across;
            if (!there || !hp || !tp || !sheafable(here, *hp, *there, *tp, across, every)) continue;
            Pose placed = compose_pose(at, across);
            placed.yaw = std::remainder(placed.yaw, 6.283185307179586);
            const auto known = std::find_if(out.begin(), out.end(), [&](const PlacedRoom& p) { return p.room == there; });
            if (known != out.end()) {
                // Reached again: one with it here too only if this way puts it where
                // the other did.
                if (!same_pose(known->pose, placed)) continue;
                if (std::find(out[i].doorways.begin(), out[i].doorways.end(), hp->id) == out[i].doorways.end()) out[i].doorways.push_back(hp->id);
                if (std::find(known->doorways.begin(), known->doorways.end(), tp->id) == known->doorways.end()) known->doorways.push_back(tp->id);
                continue;
            }
            // (A doorway said to be one is one, however many rooms and lamps
            // it brings: what a world says it is is not cut to a budget.
            // Only the rooms that merely agree are.)
            const bool said = hp->params.num(Key{"sheaf"}, 0.0) > 0.5 && tp->params.num(Key{"sheaf"}, 0.0) > 0.5;
            if (!said && (out.size() >= most || lit + lamps_of(*there) > lamps)) continue;
            const Box box = placed_box(bounds_of(*there), placed);
            if (!box.empty() && std::any_of(boxes.begin(), boxes.end(), [&](const Box& b) { return !b.empty() && overlap(b, box, 0.6); })) continue;  // (more than two walls' breadth)
            out[i].doorways.push_back(hp->id);
            out.push_back(PlacedRoom{there, placed, {tp->id}});
            boxes.push_back(box);
            lit += lamps_of(*there);
        }
    }
    return out;
}

}  // namespace

bool make_space(StateGraph& g, Key space, const std::vector<Key>& rooms, std::vector<std::string>* problems) {
    const auto no = [&](const std::string& why) {
        if (problems) problems->push_back("space " + space.str() + ": " + why);
        return false;
    };
    auto* made = dynamic_cast<Spatial3D*>(g.find(space));
    if (!made) return no("is not a state of three dimensions");
    if (rooms.size() < 2) return no("is made of two rooms or more");
    struct Placed {
        Spatial3D* room;
        Pose pose;
        std::vector<Key> inner;
    };
    std::vector<Placed> out;
    for (Key k : rooms) {
        if (!dynamic_cast<Spatial3D*>(g.find(k))) return no(k.str() + " is not a room of three dimensions: only spaces of one kind are made one");
        if (k == space) return no("is made of itself");
    }
    // Placed from the first named, through the seams between them, room by
    // room: each where the seam puts it; reached again another way, it must
    // stand where it stood (a ring that closes) - or the rooms are not one.
    out.push_back(Placed{static_cast<Spatial3D*>(g.find(rooms[0])), Pose{}, {}});
    const auto member = [&](Key k) { return std::find(rooms.begin(), rooms.end(), k) != rooms.end(); };
    std::vector<Key> seams;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const Spatial3D& here = *out[i].room;
        for (const Seam& sm : g.seams()) {
            if (sm.boundary_a.empty() || sm.boundary_b.empty()) continue;
            const bool forward = sm.a == here.id();
            if (!forward && sm.b != here.id()) continue;
            const Key other = forward ? sm.b : sm.a;
            if (!member(other)) continue;
            if (sm.wraps || other == here.id()) return no("seam " + sm.name.str() + " wraps " + here.id().str() + " onto itself: a space that wraps is not made one room by room");
            auto* there = static_cast<Spatial3D*>(g.find(other));
            const Element* hp = here.find(forward ? sm.boundary_a[0] : sm.boundary_b[0]);
            const Element* tp = there->find(forward ? sm.boundary_b[0] : sm.boundary_a[0]);
            Pose across;
            if (!hp || !tp || !sheafable(here, *hp, *there, *tp, across, true))
                return no("seam " + sm.name.str() + " does not join " + here.id().str() + " and " + other.str() +
                          " as one kind of space (a plain walked opening, one scale, both walled or both under a sky, nothing said to differ)");
            if (std::find(seams.begin(), seams.end(), sm.name) == seams.end()) seams.push_back(sm.name);
            Pose placed = compose_pose(out[i].pose, across);
            placed.yaw = std::remainder(placed.yaw, 6.283185307179586);
            const auto add_inner = [](Placed& p, Key d) {
                if (std::find(p.inner.begin(), p.inner.end(), d) == p.inner.end()) p.inner.push_back(d);
            };
            const auto known = std::find_if(out.begin(), out.end(), [&](const Placed& p) { return p.room == there; });
            if (known != out.end()) {
                if (!same_pose(known->pose, placed))
                    return no(other.str() + " stands in two places - through seam " + sm.name.str() + " and another way: its rooms do not close into one space (a holonomy)");
                add_inner(out[i], hp->id), add_inner(*known, tp->id);
                continue;
            }
            add_inner(out[i], hp->id);
            out.push_back(Placed{there, placed, {tp->id}});
        }
    }
    for (Key k : rooms)
        if (std::none_of(out.begin(), out.end(), [&](const Placed& p) { return p.room->id() == k; }))
            return no(k.str() + " is not reached from " + rooms[0].str() + " through the seams between them");
    for (std::size_t i = 0; i < out.size(); ++i)
        for (std::size_t j = i + 1; j < out.size(); ++j) {
            const Box a = placed_box(bounds_of(*out[i].room), out[i].pose), b = placed_box(bounds_of(*out[j].room), out[j].pose);
            if (!a.empty() && !b.empty() && overlap(a, b, 0.6))
                return no(out[i].room->id().str() + " and " + out[j].room->id().str() + " stand in each other where they are placed");
        }

    // Made one. Each doorway between them a hole in the walls: what would
    // fill an opening onto nothing there opens.
    for (Placed& p : out) {
        auto* room = dynamic_cast<Room*>(p.room);
        if (!room) continue;
        for (Key d : p.inner) {
            const Element* door = room->find(d);
            if (!door) continue;
            const Pose dp = world_pose(*room, *door);
            for (const plan::Opening& o : room->openings()) {
                const Element* blank = room->find(Key{o.id.str() + ".blank"});
                if (!blank) continue;
                const Vec3d mid = place_in(world_pose(*room, *blank), {0.0, blank->params.num(keys::sy, 1.0) * 0.5, 0.0});
                const Vec3d l = local_of(dp, mid);
                if (std::fabs(l.x) < 0.6 && std::fabs(l.z) < door->params.num(keys::w, 1.0) * 0.5 && std::fabs(l.y) < door->params.num(keys::h, 2.0) * 0.5)
                    room->opens(o.id, true);
            }
        }
    }
    // Its frame a room's: its floor plan from the origin, as any room's is
    // (0..room_w across, 0..room_d deep) - the first room where the rest put it.
    {
        Box all;
        for (const Placed& p : out) {
            const Params& rp = p.room->params();
            if (!rp.has(Key{"room_w"}) || !rp.has(Key{"room_d"})) continue;
            Box b;
            b.add({0, 0, 0});
            b.add({rp.num(Key{"room_w"}), 0, rp.num(Key{"room_d"})});
            b = placed_box(b, p.pose);
            all.add(b.lo), all.add(b.hi);
        }
        if (!all.empty())
            for (Placed& p : out) p.pose.position = p.pose.position - Vec3d{all.lo.x, 0.0, all.lo.z};
    }
    // Each room's own floor and ceiling - laid past its floor's edge, under
    // its walls, so no light finds its way between them - end at its walls'
    // outer faces: two rooms back to back, the one's would stand in the
    // other's, one plane, fighting.
    for (Placed& p : out) {
        const Params& rp = p.room->params();
        if (!rp.has(Key{"room_w"}) || !rp.has(Key{"room_d"})) continue;
        const double w = rp.num(Key{"room_w"}), d = rp.num(Key{"room_d"}), h = rp.num(Key{"room_h"}, 3.0), t = rp.num(Key{"wall_t"}, 0.2);
        for (Element& e : p.room->elements()) {
            if (!e.alive || (e.kind != kinds::mesh && e.kind != kinds::wall) || e.params.has(keys::parent)) continue;
            const double sx = e.params.num(keys::sx, 0.0), sy = e.params.num(keys::sy, 0.0), sz = e.params.num(keys::sz, 0.0);
            if (sy > 0.6 || sx * sz < 0.9 * w * d || std::fabs(std::remainder(e.params.num(keys::yaw, 0.0), 1.5707963267948966)) > 1e-6) continue;
            const Vec3d at = position_of(e);
            if (std::fabs(at.y + sy) > 0.02 && std::fabs(at.y - h) > 0.02 && std::fabs(at.y + sy - 0.0) > 0.02) continue;  // a floor's top at 0, a ceiling's foot at its height
            const bool turned = std::fabs(std::sin(e.params.num(keys::yaw, 0.0))) > 0.5;
            const double ex = turned ? sz : sx, ez = turned ? sx : sz;
            const double x0 = std::max(at.x - ex * 0.5, -t), x1 = std::min(at.x + ex * 0.5, w + t), z0 = std::max(at.z - ez * 0.5, -t), z1 = std::min(at.z + ez * 0.5, d + t);
            if (x1 <= x0 || z1 <= z0) continue;
            e.params.set(keys::x, (x0 + x1) * 0.5).set(keys::z, (z0 + z1) * 0.5);
            e.params.set(turned ? keys::sz : keys::sx, x1 - x0).set(turned ? keys::sx : keys::sz, z1 - z0);
        }
    }
    // Everything each is made of, into the one, where it stands in it; the
    // floor each covered, its own (what the light from all round is laid
    // over: `regions`); what each wears, worn by the one.
    Box all;
    int k = 0;
    Params& mp = made->params();
    // (What each room's floor was worn by - its field, its rectangle - read
    // before it goes: laid again as one, over the one floor.)
    struct Field {
        Spatial3D::Picture pic;
        double x, z, w, d;
        Pose at;
    };
    std::vector<Field> fields;
    for (const Placed& p : out) {
        const Params& rp = p.room->params();
        if (const std::string* f = rp.text(Key{"wear_field"}))
            if (const Spatial3D::Picture* pic = p.room->picture(Key{*f}); pic && pic->w > 0 && pic->h > 0)
                fields.push_back({*pic, rp.num(Key{"wear_x"}, 0.0), rp.num(Key{"wear_z"}, 0.0), rp.num(Key{"wear_w"}, 1.0), rp.num(Key{"wear_d"}, 1.0), p.pose});
    }
    for (Placed& p : out) {
        for (const std::string& w : made->absorb(*p.room, p.pose)) no(w);
        for (Key d : p.inner)
            if (const Element* door = made->find(d)) {
                // (Where two rooms were joined: a hole in the walls, no doorway - kept, as a hole.)
                Params kept = door->params;
                made->remove_element(d);
                Element& hole = made->add_element(d, Key{"hole"});
                hole.params = std::move(kept);
                hole.params.set("walk", 0.0).set("unseen", 1.0);
            }
        const Params& rp = p.room->params();
        if (rp.has(Key{"room_w"}) && rp.has(Key{"room_d"})) {
            Box b;
            b.add({0, 0, 0});
            b.add({rp.num(Key{"room_w"}), rp.num(Key{"room_h"}, 3.0), rp.num(Key{"room_d"})});
            b = placed_box(b, p.pose);
            const std::string r = "region" + std::to_string(k++) + "_";
            mp.set(Key{r + "x0"}, b.lo.x).set(Key{r + "z0"}, b.lo.z).set(Key{r + "x1"}, b.hi.x).set(Key{r + "z1"}, b.hi.z).set(Key{r + "h"}, b.hi.y);
            if (rp.has(Key{"probe_cell"})) mp.set(Key{r + "cell"}, rp.num(Key{"probe_cell"}));
            all.add(b.lo), all.add(b.hi);
        }
        std::vector<Embedding> worn;
        for (std::size_t at : g.embeddings_hosted_by(p.room->id())) worn.push_back(g.embeddings()[at]);
        for (Embedding e : worn) {
            g.drop_embedding(e.name);
            e.host = space;
            g.embed(std::move(e));
        }
    }
    // What the rooms said of themselves, as rooms - closed by their own
    // walls (`enclosed` 0: no box of the renderer's round them, which would
    // stand in their floors and ceilings and fight them), their pull, their
    // floor's sound, how far is seen - said of the one: the first room's
    // where they differ, and never what the one says itself already, nor what is the rooms' own shape (their
    // size, their parts' names, their wear's rectangle: laid again here).
    static const std::set<std::string> shaped = {"room_w", "room_d", "room_h", "names", "regions", "wear_field", "wear_x", "wear_z", "wear_w", "wear_d", "pieces"};
    for (const Placed& p : out)
        for (const auto& [key, value] : p.room->params()) {
            const std::string& kk = key.str();
            if (shaped.count(kk) || kk.rfind("region", 0) == 0 || mp.has(key)) continue;
            mp.set(key, value);
        }
    // (Closed by its own walls if any of them was.)
    for (const Placed& p : out)
        if (p.room->params().num(Key{"enclosed"}, 1.0) < 0.5) mp.set(Key{"enclosed"}, 0.0);
    mp.set(Key{"regions"}, static_cast<double>(k));
    if (!all.empty()) mp.set(Key{"room_w"}, all.hi.x).set(Key{"room_d"}, all.hi.z).set(Key{"room_h"}, all.hi.y);
    // Its floor's wear (the renderer's `wear_field`): each room's, where it
    // lay, in one picture over the whole - as fine each way as the finest of
    // theirs (a hall's runner: fine across, coarse along), read between
    // their pixels, so no edge of its stains is a pixel's.
    if (!fields.empty() && !all.empty()) {
        const double fw = all.hi.x, fd = all.hi.z;
        double per_x = 16.0, per_z = 16.0;  // pixels a metre, each way
        for (const Field& f : fields) {
            const bool turned = std::fabs(std::sin(f.at.yaw)) > 0.5;
            const double ax = f.pic.w / f.w, az = f.pic.h / f.d;
            per_x = std::max(per_x, turned ? az : ax), per_z = std::max(per_z, turned ? ax : az);
        }
        const int pw = std::clamp(static_cast<int>(fw * per_x), 16, 4096), ph = std::clamp(static_cast<int>(fd * per_z), 16, 4096);
        std::vector<unsigned char> rgba(static_cast<std::size_t>(pw) * ph * 4, 0);
        work::bands(ph, [&](int from, int to) {
        for (int y = from; y < to; ++y)
            for (int x = 0; x < pw; ++x) {
                const Vec3d at{(x + 0.5) / pw * fw, 0.0, (y + 0.5) / ph * fd};
                unsigned char* o = &rgba[(static_cast<std::size_t>(y) * pw + x) * 4];
                for (const Field& f : fields) {
                    const Vec3d l = local_of(f.at, at);
                    const double u = (l.x - f.x) / f.w * f.pic.w - 0.5, v = (l.z - f.z) / f.d * f.pic.h - 0.5;
                    if (u < -0.5 || u > f.pic.w - 0.5 || v < -0.5 || v > f.pic.h - 0.5) continue;
                    const int u0 = std::clamp(static_cast<int>(std::floor(u)), 0, f.pic.w - 1), v0 = std::clamp(static_cast<int>(std::floor(v)), 0, f.pic.h - 1);
                    const int u1 = std::min(u0 + 1, f.pic.w - 1), v1 = std::min(v0 + 1, f.pic.h - 1);
                    const double a = std::clamp(u - u0, 0.0, 1.0), b = std::clamp(v - v0, 0.0, 1.0);
                    const auto px = [&](int i, int j, int c) { return static_cast<double>(f.pic.rgba[(static_cast<std::size_t>(j) * f.pic.w + i) * 4 + c]); };
                    for (int c = 0; c < 4; ++c) {
                        const double val = (px(u0, v0, c) * (1 - a) + px(u1, v0, c) * a) * (1 - b) + (px(u0, v1, c) * (1 - a) + px(u1, v1, c) * a) * b;
                        o[c] = std::max(o[c], static_cast<unsigned char>(std::lround(val)));
                    }
                }
            }
        });
        const Key name{space.str() + ".wear"};
        made->picture(name, pw, ph, std::move(rgba));
        mp.set(Key{"wear_field"}, name.str()).set(Key{"wear_x"}, 0.0).set(Key{"wear_z"}, 0.0).set(Key{"wear_w"}, fw).set(Key{"wear_d"}, fd);
    }
    // (Where the world began, if in one of them, it begins in the one.)
    for (const Placed& p : out)
        if (g.initial() == p.room->id()) g.set_initial(space);
    // The rooms were; the one is. (Their seams with each other with them.)
    for (Key sm : seams) g.remove(g.removal_of_seam(sm));
    for (const Placed& p : out) g.remove(g.removal(p.room->id()));
    return true;
}

std::vector<PlacedRoom> sheafify(const StateGraph& g, Key root, std::size_t most, std::size_t lamps, bool every) {
    // Grown from `root` through the doorways it shares, room by room - what
    // the rooms say of each other, no more (sheafable) - and placed in the
    // frame of the first of them by name: so crossing among them, the frame
    // they are drawn in stays as it was.
    std::vector<PlacedRoom> out;
    const auto* r = dynamic_cast<const Spatial3D*>(g.find(root));
    if (!r) return out;
    out = grow(g, *r, most, lamps, every);
    for (PlacedRoom& p : out) p.sheafed = out.size() > 1;
    if (out.size() > 1) {
        const auto anchor = std::min_element(out.begin(), out.end(), [](const PlacedRoom& x, const PlacedRoom& y) { return x.room->id().str() < y.room->id().str(); });
        // (Rooms stand upright: the inverse of a turn about the vertical and a move.)
        Pose to_anchor;
        to_anchor.yaw = -anchor->pose.yaw;
        to_anchor.position = rotate_xz(anchor->pose.position * -1.0, -anchor->pose.yaw);
        for (PlacedRoom& p : out) {
            p.pose = compose_pose(to_anchor, p.pose);
            p.pose.yaw = std::remainder(p.pose.yaw, 6.283185307179586);
        }
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
