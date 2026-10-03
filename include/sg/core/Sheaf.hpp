// Stategine - covers, descent, and gluing.
//
// An atlas of rooms is one instance of a much older operation: you have data
// defined locally, in pieces, and you want one global thing. The pieces are
// states; a `Cover` says which pieces overlap and gives the transition between
// them as a pair of functors. Gluing is then only allowed when the pieces
// actually agree, and "agree" has a precise, checkable meaning:
//
//   separatedness   on each overlap, going across and back is the identity.
//                   A doorway seen from either side is the same doorway.
//
//   cocycle         around any loop of overlaps, the composite is the
//                   identity. Otherwise the loop has holonomy: walk the ring
//                   of rooms and you come back somewhere else, and there is no
//                   global object to glue to, only a seam.
//
// Both conditions are statements that some composite *is the identity*, and
// measuring the gap between a composite and the identity is exactly what
// `Adjunction` already does - its unit and counit defects are the descent
// failures, reported per object and per parameter. So this header adds no new
// notion of correctness; it applies the one already in the engine to the
// arrows of a cover.
//
// What it buys, concretely: you cannot quietly build a space that does not
// close up. `descent_defects` names the seam before anything is drawn, and
// `Cover::sections` will not glue a cover that has one.
#pragma once

#include <cmath>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/Adjunction.hpp"
#include "sg/core/Functor.hpp"
#include "sg/core/StateGraph.hpp"

namespace sg {

// Two states that share a region, and the transition between them. The two
// functors must be mutually inverse on that region - that is the condition,
// not an assumption.
struct Overlap {
    Key name;
    Key u, v;
    Key u_to_v;  // functor registered in the graph
    Key v_to_u;
    // Its rings are the space's own shape (Seam::wraps): not held to closing.
    bool wraps = false;
};

// How far a composite may stray from the identity before it counts as a seam.
// A transition carries what it carries - a doorway moves a position and leaves
// the colour of the wall alone - so by default only what comes back is held to
// coming back the same. `whole` asks more: that everything comes back, and an
// object or a parameter the composite drops is a defect too.
std::vector<std::string> identity_defects(const StateGraph& g, const Functor& f,
                                                 const std::string& tag, bool whole = false);

// --- the law every interface owes, in every domain -----------------------------
// A view and its write-back form a round trip on the thing being viewed. That
// round trip is almost never the identity - a map that shows metres as cells
// quantises, a summary rounds, a form trims whitespace - and demanding that it
// be the identity would be wrong. What it must be is *idempotent*:
//
//     round . round  ==  round
//
// which says that looking at something and writing it back settles, rather
// than nudging it a little further every time. An interface that fails this
// moves your data simply by being opened and closed, and it does so whatever
// the data is: positions, prices, text. This is the check, and it knows
// nothing about any domain.
std::vector<std::string> idempotence_defects(const StateGraph& g, const Functor& round,
                                                    const std::string& tag);

// Does the round trip lose anything at all? Not a defect either way: a lossless
// view is an isomorphism onto its image, a lossy one is a projection. Worth
// being able to ask, and worth not confusing with correctness. Lossless means
// every object the round trip maps comes back as itself, with every parameter
// it had, holding what it held: dropping a parameter loses it as surely as
// changing it. Objects the round trip does not map are outside the view.
inline bool is_lossless(const StateGraph& g, const Functor& round) {
    return identity_defects(g, round, "round", true).empty();
}

// Every interface registered in a graph, held to that law. An embedding with
// both directions declared is a view of its subject, whatever either of them
// happens to be about.
std::vector<std::string> interface_defects(const StateGraph& g);

class Cover {
public:
    Overlap& add(Key name, Key u, Key v, Key u_to_v, Key v_to_u);

    const std::vector<Overlap>& overlaps() const { return overlaps_; }

    // The transition out of `here` along `o`, in the direction that leaves it.
    const Functor* transition(const StateGraph& g, const Overlap& o, Key here) const;

    Key other_side(const Overlap& o, Key here) const { return o.u == here ? o.v : o.u; }

    // --- descent ----------------------------------------------------------------
    // Everything that stops these pieces gluing into one space. Complete: every
    // overlap and every loop of the cover, however long, is held to it.
    std::vector<std::string> descent_defects(const StateGraph& g) const;

    // Loops that do not close: walk the ring and you arrive somewhere else.
    //
    // Every loop, not those up to some length. Grow a tree over each connected
    // piece of the cover from a root; each overlap the tree does not use closes
    // exactly one loop - out along the tree, across it, back along the tree -
    // and every loop of the cover is made of these. So when each of them is
    // the identity (and separatedness makes going there and back cancel),
    // every loop is: one check per overlap, not one per path.
    //
    // The trees grow only along overlaps that do not wrap, so every loop made
    // of those alone is held to closing; an overlap that wraps is the space's
    // own shape - a generator of its loops - and is held only to going across
    // and back being the identity.
    std::vector<std::string> cocycle_defects(const StateGraph& g) const;

    // --- gluing -------------------------------------------------------------------
    // The composite transition from `root` to every state it can reach: the
    // change of coordinates that expresses that state's local data in the
    // root's terms. This is the glued section - and it is only well defined
    // because descent holds, so descent is asked first: a cover that fails it
    // glues to nothing, and its defects are put in `seams` if asked for. The
    // section covers every piece joined to the root, however far.
    std::vector<std::pair<Key, Functor>> sections(const StateGraph& g, Key root,
                                                  std::vector<std::string>* seams = nullptr) const;

private:
    // A tree over the piece of the cover joined to `root`: for each state, the
    // way there from the root and the way back, and the overlaps it leaves out.
    struct Tree {
        std::vector<Key> order;                             // as reached, root first
        std::unordered_map<Key, Functor> to, back;          // root -> k, k -> root
        std::unordered_map<Key, std::vector<Key>> path;     // root ... k
        std::vector<std::size_t> closing;                   // overlaps not in the tree
    };

    Tree grow(const StateGraph& g, Key root) const;

    // The states of the cover, in the order they were first named - so the
    // same cover is always walked, and reported, the same way.
    std::vector<Key> states_in_order() const;

    std::vector<Overlap> overlaps_;
    std::unordered_map<Key, std::vector<std::size_t>> by_state_;
};

}  // namespace sg
