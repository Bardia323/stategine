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
    bool wraps = false;  // its rings are the space's shape (Seam::wraps)
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
    Doorway& glue(Key name, Key room_a, Key portal_a, Key room_b, Key portal_b);

    Doorway& glue(Key room_a, Key portal_a, Key room_b, Key portal_b) {
        return glue(Key{}, room_a, portal_a, room_b, portal_b);
    }

    const std::deque<Doorway>& doorways() const { return doorways_; }

    // One step: where `there` sits in `here`'s coordinates, across `d`.
    // Both directions come from the same pair of portals, so they cannot
    // disagree - one is the other read backwards.
    static bool step(const StateGraph& g, const Doorway& d, Key here, Pose& out);

    // Every room reachable from `root`, each with its pose in root coordinates.
    // The root itself comes first, with the identity: not because it is
    // special, but because it is the one we chose to ask from.
    std::vector<Chart> charts(const StateGraph& g, Key root, int max_depth = 3) const;

    // Where `other` sits in `root`'s coordinates, along the shortest path.
    bool placement(const StateGraph& g, Key root, Key other, Pose& out,
                   int max_depth = 3) const;

private:
    // A portal may itself be anchored inside its room, so its pose in that
    // room's coordinates is the composed one.
    static Pose world_pose_of(const State& s, const Element& e) { return world_pose(s, e); }

    std::deque<Doorway> doorways_;
    std::unordered_map<Key, std::vector<std::size_t>> by_room_;
};

// --- a doorway, as a seam -----------------------------------------------------------
// Glue room `a` to room `b` at the doorway `pa` / `pb`. The boundary on each
// side is the doorway and anything else hanging in it (`also` - a door in its
// frame, say, which each room keeps its own copy of); glue carries the
// doorway (seam_carry) and the rest (pose_carry) between them. Travel carries
// the camera (portal_carry, both ways). All four functors
// are rebuilt from the two portals, so call this again whenever a doorway
// moves; the seam law then checks that both sides still agree.
const Seam& glue_doorway(StateGraph& g, Key name, Key a, Key pa, Key b, Key pb,
                          const std::vector<std::pair<Key, Key>>& also = {}, bool wraps = false);

// The standard doorway: glued (glue_doorway) and crossed - for each side
// walked through (`walk`, and not only a way in), a transition carrying the
// seam's travel, `walk.<name>.ab` / `.ba`, which the engine takes in the frame
// the step goes through (Engine::cross). It is seamless as made: the overlap
// law (laws::overlaps) holds it so. A way that is not - one way only, a cut -
// is glue_doorway and the transitions it means, and says what `differs`.
const Seam& walkway(StateGraph& g, Key name, Key a, Key pa, Key b, Key pb,
                    const std::vector<std::pair<Key, Key>>& also = {}, bool wraps = false);

// The seam glue_doorway declares, as data: what it is called and what it joins,
// worked out from the names alone (its functors are made from the portals).
Seam doorway_seam(Key name, Key a, Key pa, Key b, Key pb, const std::vector<std::pair<Key, Key>>& also = {}, bool wraps = false);

// --- the atlas as a cover --------------------------------------------------------
// Rooms glued along doorways are one instance of local data over a cover, so
// the atlas hands itself to the general machinery rather than re-deriving what
// "consistent" means. Each doorway becomes an overlap whose transition is
// portal_carry in each direction - the same arrow that places the rooms and
// the same arrow you travel along when you walk through.
Cover as_cover(const Atlas& atlas, StateGraph& g);

// A doorway's transition carries the traveller, not the doorway. A functor
// that writes the far side's portal moves the room you are walking into, every
// time anybody walks into it - which is a thing this engine has actually done.
// `as_cover` derives its transitions and so cannot make this mistake; this
// catches a cover whose transitions were registered by hand.
std::vector<std::string> travel_defects(const Cover& cover, const StateGraph& g);

// Everything that stops this atlas being one space: a doorway whose two sides
// disagree, or a ring of rooms that does not close up.
//
// The generic part - transitions inverse on each overlap, loops closing - is
// the Cover's. The part that is specifically spatial is checked here, without
// applying anything to anybody: placing one room by the doorway must land its
// doorway exactly on the other's, facing back the other way. A doorway that
// fails this is one the two rooms disagree about.
// Two glued rooms are adjacent, not overlapping. A doorway's opening is the
// boundary between them: nothing of a room reaches past its plane within the
// opening - there it would stand in what is seen of the other side, and the
// two rooms would fight to draw the same place. (Beside the opening, a room
// is its own: a wall the door is cut in, the sand under a doorway standing
// on it.) A doorway that is a ball bounds a world inside another: the inner
// world keeps within it, the outer out of it. This names every solid that
// reaches across, and how far.
std::vector<std::string> adjacency_defects(const Atlas& atlas, const StateGraph& g,
                                                  double tolerance = 1e-6);

std::vector<std::string> descent_defects(const Atlas& atlas, StateGraph& g);

std::vector<PlacedRoom> place_rooms(StateGraph& g, const Atlas& atlas, Key root,
                                           int max_depth = 3);

// Worlds inside worlds: every state joined to `root` by a seam whose doorways
// are balls (a planet in its system, the system round its planet), placed in
// root's coordinates by the same carry as whoever crosses - not seen through
// a picture, but there, where they are. The root itself is not among them.
std::vector<PlacedRoom> nests(const StateGraph& g, Key root, int max_depth = 3);

}  // namespace sg
