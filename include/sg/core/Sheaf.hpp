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
//   cocycle         around any loop of overlaps, the composite is an
//                   automorphism: round and back is where you were. The
//                   identity, and the pieces glue into one chart; otherwise
//                   the loop has monodromy - walk the ring of rooms and you
//                   come back moved, turned, swapped - and that is the
//                   space's shape, named (`Monodromy`), not a fault.
//
// Both conditions are statements that some composite *is the identity*, and
// measuring the gap between a composite and the identity is exactly what
// `Adjunction` already does - its unit and counit defects are the descent
// failures, reported per object and per parameter. So this header adds no new
// notion of correctness; it applies the one already in the engine to the
// arrows of a cover.
//
// What it buys, concretely: you cannot quietly build a space that does not
// agree with itself, nor one stranger than you meant. `descent_defects` names
// the seam before anything is drawn, `monodromy` says what shape the rest
// is, and `Cover::sections` glues only a space that closes.
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
    // What passes through it, as a seam's doorway says it (Channel: view
    // light sound objects); empty, all of them. An overlap that is not a
    // seam - a painting gone into by a transition - says what it is: `view`.
    std::string admits;
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

// A ring's monodromy: what going once round it does. Going round is a
// composite of the cover's own transitions, so it is a functor from the
// ring's state to itself - nothing new, and held to the laws already here:
// whether it is the identity (`identity_defects`), and how many times round
// it takes until it is. A ring that is not the identity is not a mistake -
// it is the shape of the space: a corridor that comes back on itself further
// on, a room you go round four times before you are where you began, a hall
// of mirrors, a world inside itself. What it moves says which: an object
// that comes back as another is the space itself going round; a parameter
// that comes back changed is what lives in it, twisted. Any domain, any
// parameter: the functor says, the cover only reads it.
//
// What is said of one ring is said of two: their commutator - round one,
// round the other, back round the first, back round the second - is a
// composite like any other, and the identity when they commute.
struct Monodromy {
    static constexpr int kMostOrder = 24;
    std::string ring;  // the states round it, in order
    bool wraps = false;  // a ring the cover says is its shape (Overlap::wraps)
    // Times round until it is the identity: 1, it closes; 0, not within kMostOrder.
    int order = 1;
    bool permutes = false;            // objects come back as others
    std::vector<std::string> moves;   // what comes back changed, once round
    bool trivial() const { return order == 1; }
    std::string str() const;
};

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

    // Loops that are not automorphisms: round one way and back the other is
    // not where you were. A ring that is an automorphism but not the identity
    // is the space's shape, not a defect - `monodromy` names it.
    //
    // Every loop, not those up to some length. Grow a tree over each connected
    // piece of the cover from a root; each overlap the tree does not use closes
    // exactly one loop - out along the tree, across it, back along the tree -
    // and every loop of the cover is made of these. So when each of them is
    // an automorphism, every loop is: one check per overlap, not one per path.
    //
    // The trees grow only along overlaps that do not wrap; an overlap that
    // wraps is the space's own shape - a generator of its loops - and is a
    // ring of its own.
    std::vector<std::string> cocycle_defects(const StateGraph& g) const;

    // --- the shape of the space -----------------------------------------------
    // What each generating ring does: one per overlap that closes a loop, and
    // one per overlap that wraps. All trivial, the cover is ordinary space
    // cut into pieces; otherwise this says which rings are not, and how.
    std::vector<Monodromy> monodromy(const StateGraph& g) const;

    // --- gluing -------------------------------------------------------------------
    // The composite transition from `root` to every state it can reach: the
    // change of coordinates that expresses that state's local data in the
    // root's terms. This is the glued section - and it is only well defined
    // because descent holds and every ring that does not wrap is trivial (one
    // chart of a space whose rings move what goes round them would have to be
    // in two places at once), so both are asked first: a cover that fails glues to
    // nothing, and why is put in `seams` if asked for. The section covers
    // every piece joined to the root, however far.
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

    // Every generating ring of the cover: its overlap, its name, and its loop
    // functor round each way.
    template <typename Fn>
    void each_ring(const StateGraph& g, Fn&& fn) const;

    // The states of the cover, in the order they were first named - so the
    // same cover is always walked, and reported, the same way.
    std::vector<Key> states_in_order() const;

    std::vector<Overlap> overlaps_;
    std::unordered_map<Key, std::vector<std::size_t>> by_state_;
};

}  // namespace sg
