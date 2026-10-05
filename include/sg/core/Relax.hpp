// Stategine - a relation, and the time it takes to hold.
//
// A functor between two states says at once what the target should hold,
// given the source: the relation is instantaneous. Whether the target holds it
// is another matter. Where the target's own arrows, driven on a `Temporal`,
// move it toward what the functor says - the functor's answer carried into
// the target's own params as its aim, and the arrow relaxing toward that -
// the relation is reached in time, or not at all. Nothing here adds a kind of
// thing: the relation is a functor, the aim is a kept functor, the dynamics
// is a driven arrow, and these are three ways of reading what they do.
//
//   gap               how far a state is from what a functor says it should
//                     hold: the functor run on its source, into a scratch
//                     state, against the target's own data, number by number
//
//   orbit             what a run of gaps did: came to nothing (converges),
//                     came back to itself after k steps without coming to
//                     nothing (periodic), stayed where it was or wandered
//                     without returning (persists), or grew. It is to the
//                     orbit of a driven step what `Monodromy` is to a ring of
//                     overlaps: the order of a map, counted from what it does.
//
//   locality_defects  whether a state is no more than its pieces: every
//                     object of `whole` carried to some piece of the cover
//                     and back as it was, whole, and nothing of it carried by
//                     none. Descent asks the pieces to agree where they meet;
//                     this asks the converse - that the glued section is the
//                     whole state, so the whole is determined by its pieces
//                     (the sheaf's locality). What it names is a datum the
//                     whole holds and no piece can: a global correlation.
#pragma once

#include <string>
#include <vector>

#include "sg/core/Functor.hpp"
#include "sg/core/Sheaf.hpp"
#include "sg/core/StateGraph.hpp"

namespace sg {

// What a functor says its target should hold, less what it holds: one number
// per numeric parameter the functor carries that the target has, named
// `element.param` and sorted by name, so two gaps of the same functor line up.
struct Gap {
    std::vector<std::string> where;
    std::vector<double> by;
    double norm() const;
};

Gap gap(const StateGraph& g, const Functor& f);

// What a sequence of gaps, taken one step apart, did. `tol` is how near
// nothing counts as nothing, and how near counts as the same.
struct Orbit {
    enum class Kind { Converges, Periodic, Persists, Grows };
    static constexpr int kMostPeriod = 64;
    Kind kind = Kind::Persists;
    // Steps until the gap is what it was: k >= 2 for a periodic orbit; 1 for
    // a gap that persists unmoving (a fixed point that is not the target); 0
    // where it does not come back within kMostPeriod (or converges).
    int period = 0;
    bool monotone = true;  // the size of the gap never rose from one step to the next
    double first = 0, last = 0, peak = 0;  // its size, at the start, at the end, at most
    std::string str() const;
};

Orbit orbit(const std::vector<Gap>& seen, double tol);

std::vector<std::string> locality_defects(const StateGraph& g, const Cover& c, Key whole);

}  // namespace sg
