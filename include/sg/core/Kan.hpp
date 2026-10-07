// Stategine - Kan extensions: a functor found, not a thing added.
//
// Given K : A -> B and F : A -> C, the left Kan extension of F along K is the
// functor Lan_K F : B -> C that does on B what F does on A, as well as B's
// structure lets it: at each object b of B,
//
//     Lan_K F (b)  =  the colimit in C of  F(a)  over every  K(a) -> b
//     Ran_K F (b)  =  the limit    in C of  F(a)  over every  b -> K(a)
//
// This header is a compiler, not a new kind of thing. It reads states,
// functors and arrows the graph already has, works the extension out, and
// hands back an ordinary `Functor` - which the graph takes like any other
// (`graph.add_functor(std::move(*r.functor))`), and which nothing afterwards
// can tell from one written by hand. The engine gains no object, no path and
// no propagation: what is costly (the search, the proof) is done here, once,
// and what runs is a table looked up and a transport carried.
//
// Nothing is invented. A colimit is not a new object of C: it is an element
// C already has, with arrows C already has, that has the universal property
// - every other cocone passes through it by exactly one arrow. It is searched
// for among C's own elements; if none has it, the extension does not exist
// at b, and the result says so (`defects`). A world that lacks it can be given
// it the way any world is changed - an edit adds the element and its arrows -
// and compiled again.
//
// A state is read as the category its arrows generate. An arrow is a word of
// generating arrows: a composite is the word of its parts, and a loop that
// does nothing (no handler, not a composite) is an identity, the empty word -
// as `State::composite` reads it. Two arrows are equal when they are the same
// word. Hom-sets are searched up to `Options::max_path` arrows and cones up to
// `max_cones`; a search cut short says where (`Hole::Budget`), as the laws do: not
// looked at is never read as not there.
//
// Where the extension exists, what it maps is forced:
//   objects    b goes to the apex of its colimit (limit);
//   arrows     b -> b' goes to the one arrow between the apexes that the
//              universal property gives - which must be an arrow C already
//              names (a generator, a composite, an identity loop), or it is a
//              hole to fill (`holes`);
//   data       a transport is a function, and no universal property makes
//              one. It is F . K^-1, from declared stages only: at b = K(a),
//              the leg there the identity, K's transport at a undone where
//              its stages prove it invertible (a whole copy; a one to one
//              renaming, F reading only what it carries), then F's. Or it is
//              supplied (`Options::supply`). Otherwise a hole: never a copy
//              of everything, never code guessed.
//
// So a result is one of three, and says which:
//   the extension exists    `functor`: an ordinary functor;
//   it cannot exist         `defects`: no (co)limit at some b, where the
//                           search there looked at everything; K or F not a
//                           functor;
//   not derivable (yet)     `holes`: Arrow (the universal property sends an
//                           arrow of B to one C does not name), Transport (a
//                           transport not provably invertible, or reading
//                           what K drops), Budget (a search cut short),
//                           Unsupported (what this compiler cannot make
//                           executable: an opaque transport, K not an
//                           inclusion).
// And it keeps to:
//   functor present   => complete, no hole, no defect
//   defect present    => complete: a search that finished proved it
//   hole present      => no functor
//   complete == false => neither existence nor nonexistence is claimed
// `complete` is about search alone: every search the answer rests on ran to
// its end. An Unsupported hole may stand with it true - the category was
// searched through, but the mapping cannot be made executable.
//
// For now K is an inclusion - one to one on objects and arrows: a known piece
// A of a larger world B. The extension is defined on what K's image forces:
// every object of B reached from it (for Ran, every object reaching it). An
// object nothing of A reaches is outside it, as a functor may leave an
// object out - which is exactly the Kan extension along K into the part of
// B that K reaches, since every way into a reached object runs through
// reached ones.
//
// A result is materialised and kept; `current(graph)` says whether what it
// read - the three states' structure, the two functors' maps - is as it was.
// Data moving under the same structure leaves it current: compile again only
// when it is not.
#pragma once

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "sg/core/Adjunction.hpp"
#include "sg/core/Functor.hpp"
#include "sg/core/State.hpp"
#include "sg/core/StateGraph.hpp"

namespace sg {
namespace kan {

using Word = std::vector<Key>;

enum class Side { Left, Right };

struct Options {
    std::size_t max_path = 6;     // the longest word a hom-set is searched for
    std::size_t max_cones = 4096; // how many cocones (cones) one object's search may look at

    // A transport for an object of B whose data no declared transport derives:
    // what you give is what runs.
    struct Supplied {
        Transport run;
        std::shared_ptr<const Stages> declared;
    };
    std::unordered_map<Key, Supplied> supplied;
    Options& supply(Key b, Transport t);
    Options& supply(Key b, transport::Declared t);
};

// Why an extension is not (yet) made, where it is.
struct Hole {
    enum class Kind {
        Arrow,        // the universal property sends an arrow of B to one C does not name
        Transport,    // no declared transport composes into the data an object needs
        Budget,       // a search stopped at its budget: past it, nothing was looked at
        Unsupported,  // a case this compiler does not take (K not an inclusion)
    };
    Kind kind = Kind::Transport;
    Key at;           // the object or arrow of B it is at (empty: the whole)
    std::string why;

    static const char* name(Kind k);
    std::string str() const { return std::string(name(kind)) + (at.empty() ? "" : " at " + at.str()) + ": " + why; }
};

struct Result {
    // The executable extension: only when there is no hole and no defect.
    std::optional<Functor> functor;
    std::vector<Hole> holes;             // not resolved: see Hole
    std::vector<std::string> defects;    // cannot exist: proven so where `complete`
    bool complete = false;               // no search was cut short

    Side side = Side::Left;
    Key name, along, extends;  // the extension, K, F
    Key from, to;              // B, C

    // What was found: b -> its apex, an arrow of B -> the arrow of C it goes
    // to, and the 2-cell at each object a of A - the leg at (a, id), a word
    // of C: F(a) -> Lan(K(a)), or Ran(K(a)) -> F(a).
    std::unordered_map<Key, Key> objects;
    std::unordered_map<Key, Key> arrows;
    std::unordered_map<Key, Word> cells;


    bool ok() const { return functor.has_value(); }
    std::size_t holes_of(Hole::Kind k) const;

    // Whether what it was worked out from is as it was.
    bool current(const StateGraph& g) const;

    std::string str() const;

    std::vector<uint64_t> read;  // what it was worked out from (current)
};

// A functor's transport at one object, undone - where what it declares proves
// it can be (see `data` above): a whole copy is undone by a whole copy (`whole`),
// a pure renaming by the renaming turned round, which brings back only what it
// carried (`carried`, by the source's names). An opaque transport, arithmetic, a
// renaming that is not one to one: none, and why (`kind`, `why`). The extension
// composes it with F; a save (sg::Save) carries a kept state's data back along
// a link into the state it came from by it.
struct Inverse {
    transport::Declared back;
    bool whole = true;
    std::unordered_set<Key> carried;
};
std::optional<Inverse> inverse(const Functor& k, Key a, Hole::Kind* kind = nullptr, std::string* why = nullptr);

namespace detail {
Result compile(Side side, const StateGraph& g, Key k_name, Key f_name, Key name, const Options& o);
}  // namespace detail

// Lan_K F : B -> C, for K : A -> B and F : A -> C registered in the graph.
inline Result left(const StateGraph& g, Key k, Key f, Key name, const Options& o = {}) {
    return detail::compile(Side::Left, g, k, f, name, o);
}

// Ran_K F : B -> C.
inline Result right(const StateGraph& g, Key k, Key f, Key name, const Options& o = {}) {
    return detail::compile(Side::Right, g, k, f, name, o);
}

}  // namespace kan
}  // namespace sg
