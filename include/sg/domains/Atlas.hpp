// Stategine - an atlas of spaces glued by doorways.
//
// A room does not have a position. It is a chart: its own local coordinates,
// with no origin more real than any other. What exists is the *relation*
// between two charts, and that relation is carried by a doorway - a pair of
// portal elements, one in each room, understood to be the same doorway seen
// from either side.
//
// From those arrows everything else is composition:
//
//   placement(A, B)              where B sits, expressed in A's coordinates
//   placement(B, A)              the inverse, and equally valid
//   placement(A, C) = placement(A, B) . placement(B, C)
//
// So "where is the annex" is not a question the engine can answer, and never
// needs to: the renderer roots the atlas at whichever room the viewer is
// standing in, and every other room is placed by composing the doorways along
// a path to it. Move a doorway inside room A and, from a viewer standing in
// room B, it is room A that swings round to a new edge - which is exactly the
// same fact as room B moving, told from the other side.
#pragma once

#include <algorithm>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/Sheaf.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg {

// One doorway, named from both sides.
struct Doorway {
    Key name;
    Key room_a, portal_a;
    Key room_b, portal_b;
};

// Where a room sits, in some other room's coordinates.
struct Chart {
    Key room;
    Pose pose;  // identity for the root
};

class Atlas {
public:
    // Glue two rooms along a doorway. The portals are elements of their own
    // rooms; neither room is the parent of the other.
    Doorway& glue(Key name, Key room_a, Key portal_a, Key room_b, Key portal_b) {
        if (name.empty()) name = Key{room_a.str() + "<->" + room_b.str()};
        doorways_.push_back(Doorway{name, room_a, portal_a, room_b, portal_b});
        Doorway& d = doorways_.back();
        by_room_[room_a].push_back(doorways_.size() - 1);
        by_room_[room_b].push_back(doorways_.size() - 1);
        return d;
    }

    Doorway& glue(Key room_a, Key portal_a, Key room_b, Key portal_b) {
        return glue(Key{}, room_a, portal_a, room_b, portal_b);
    }

    const std::deque<Doorway>& doorways() const { return doorways_; }

    // One step: where `there` sits in `here`'s coordinates, across `d`.
    // Both directions come from the same pair of portals, so they cannot
    // disagree - one is the other read backwards.
    static bool step(const StateGraph& g, const Doorway& d, Key here, Pose& out) {
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

    // Every room reachable from `root`, each with its pose in root coordinates.
    // The root itself comes first, with the identity: not because it is
    // special, but because it is the one we chose to ask from.
    std::vector<Chart> charts(const StateGraph& g, Key root, int max_depth = 3) const {
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

    // Where `other` sits in `root`'s coordinates, along the shortest path.
    bool placement(const StateGraph& g, Key root, Key other, Pose& out,
                   int max_depth = 3) const {
        for (const Chart& c : charts(g, root, max_depth)) {
            if (c.room != other) continue;
            out = c.pose;
            return true;
        }
        return false;
    }

private:
    // A portal may itself be anchored inside its room, so its pose in that
    // room's coordinates is the composed one.
    static Pose world_pose_of(const State& s, const Element& e) { return world_pose(s, e); }

    std::deque<Doorway> doorways_;
    std::unordered_map<Key, std::vector<std::size_t>> by_room_;
};

// --- the atlas as a cover --------------------------------------------------------
// Rooms glued along doorways are one instance of local data over a cover, so
// the atlas hands itself to the general machinery rather than re-deriving what
// "consistent" means. Each doorway becomes an overlap whose transition is
// portal_carry in each direction - the same arrow that places the rooms and
// the same arrow you travel along when you walk through.
inline Cover as_cover(const Atlas& atlas, StateGraph& g) {
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
        // own door every time somebody walked through it.
        Functor to_b(fwd, d.room_a, d.room_b);
        to_b.on_object(SpatialState::camera_id(), SpatialState::camera_id(),
                       portal_carry(*pa, *pb));
        Functor to_a(back, d.room_b, d.room_a);
        to_a.on_object(SpatialState::camera_id(), SpatialState::camera_id(),
                       portal_carry(*pb, *pa));
        g.set_functor(std::move(to_b));
        g.set_functor(std::move(to_a));

        cover.add(d.name, d.room_a, d.room_b, fwd, back);
    }
    return cover;
}

// A doorway's transition carries the traveller, not the doorway. A functor
// that writes the far side's portal moves the room you are walking into, every
// time anybody walks into it - which is a thing this engine has actually done.
// `as_cover` derives its transitions and so cannot make this mistake; this
// catches a cover whose transitions were registered by hand.
inline std::vector<std::string> travel_defects(const Cover& cover, const StateGraph& g) {
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

// Everything that stops this atlas being one space: a doorway whose two sides
// disagree, or a ring of rooms that does not close up.
//
// The generic part - transitions inverse on each overlap, loops closing - is
// the Cover's. The part that is specifically spatial is checked here, without
// applying anything to anybody: placing one room by the doorway must land its
// doorway exactly on the other's, facing back the other way. A doorway that
// fails this is one the two rooms disagree about.
// Two glued rooms are adjacent, not overlapping. A doorway's plane is the
// boundary between them: each room's solid geometry lies on its own side of
// it, and the two only touch there. A wall centred on the glue plane puts half
// of itself inside the neighbour, and the two rooms then fight to draw the
// same surface. This names every solid that reaches across, and how far.
inline std::vector<std::string> adjacency_defects(const Atlas& atlas, const StateGraph& g,
                                                  double tolerance = 1e-6) {
    std::vector<std::string> out;
    for (const Doorway& d : atlas.doorways()) {
        const std::pair<Key, Key> sides[2] = {{d.room_a, d.portal_a}, {d.room_b, d.portal_b}};
        for (const auto& side : sides) {
            const State* room = g.find(side.first);
            const Element* portal = room ? room->find(side.second) : nullptr;
            if (!portal) continue;  // descent_defects names a missing side
            const HalfSpace own = room_side(*room, *portal, Pose{});
            for (const Element& e : room->elements()) {
                if (!e.alive || (e.kind != kinds::wall && e.kind != kinds::mesh)) continue;
                // A box sits on the floor at its pose, sx by sz, turned by yaw.
                const Pose p = world_pose(*room, e);
                const double hx = e.params.num(keys::sx, 1.0) * 0.5;
                const double hz = e.params.num(keys::sz, 1.0) * 0.5;
                double deepest = 0.0;
                for (const double cx : {-hx, hx})
                    for (const double cz : {-hz, hz}) {
                        const Vec3d c = rotate_xz({cx, 0.0, cz}, p.yaw);
                        deepest = std::min(
                            deepest,
                            own.at({p.position.x + c.x, p.position.y, p.position.z + c.z}));
                    }
                if (deepest < -tolerance)
                    out.push_back(side.first.str() + "." + e.id.str() + " reaches " +
                                  std::to_string(-deepest) + " m past doorway " + d.name.str() +
                                  ", into the room on the other side");
            }
        }
    }
    return out;
}

inline std::vector<std::string> descent_defects(const Atlas& atlas, StateGraph& g,
                                                int max_cycle = 4) {
    const Cover cover = as_cover(atlas, g);
    std::vector<std::string> out = cover.descent_defects(g, max_cycle);
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

        Pose placement;
        if (!atlas.placement(g, d.room_b, d.room_a, placement)) continue;
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

inline std::vector<PlacedRoom> place_rooms(StateGraph& g, const Atlas& atlas, Key root,
                                           int max_depth = 3) {
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
