// Stategine - algebra: the laws' equations, compiled; and the accelerator.
//
// The laws are equations of two paths run on the same data (sg/core/Laws.hpp),
// and they stay the truth. Some paths can be answered without running them:
// every step on them says what it does (sg/core/Declared.hpp) - an affine
// arrow, a transport that copies, renames or sums - so what the path does to
// the data it reads is an operator on a vector of values (Operator.hpp).
//
//     Equation {lhs, rhs}  --try_compile-->  two Programs, a vector, the slots to compare
//                          (or nothing: a step that says nothing, a transition,
//                          an event, an element to be made, sides that end
//                          apart - the verifier runs it, as before)
//
// A slot is one parameter of one element of one state, as the live data has
// it (or of the element made fresh between two stages of a transport). The
// compiler runs each side over the slots in its head: which parameters are
// there, what kind of value each is - so a row that would not run is not
// written, a copy keeps what a value is, and arithmetic is on numbers. What
// it compares is every slot either side wrote in the state they end in, as
// the verifier compares values (same_value: numbers to a hair, headings on
// the circle, the rest exactly). Anything it cannot say for certain it does
// not compile.
//
// `Accelerated` is a laws::Accelerator: offered each equation, it compiles it
// (or takes it from its cache: compiled programs are kept per graph, and read
// again only when what they were compiled on changed), adds it to a batch,
// and at the end runs the batch on its backend. An equation the batch finds
// broken is checked again by the verifier, by running it, and the verifier's
// counterexample is what is reported: the report is the verifier's.
#pragma once

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/algebra/Backend.hpp"
#include "sg/algebra/Program.hpp"
#include "sg/core/Laws.hpp"

namespace sg::algebra {

// What a value is, as far as comparing it goes.
enum class Kind : uint8_t { Absent, None, Flag, Int, Real, Word };

Kind kind_of(const Value& v);
inline bool numeric(Kind k) { return k == Kind::Int || k == Kind::Real; }

// An equation, compiled against the data as it is laid out now.
struct Compiled {
    Program lhs, rhs;
    std::vector<Program::Slot> compare;
    // Where each slot's starting value is read, each time: an element and a
    // parameter (null for a slot that starts empty - a fresh element's).
    struct Source {
        const Element* element = nullptr;
        Key param;
    };
    std::vector<Source> sources;
    // What it was compiled on: the states it reads, their make-up, and the
    // parameters of the elements it reads (names and kinds).
    struct Read {
        const State* state;
        uint64_t structure;
    };
    std::vector<Read> states;
    std::vector<const Element*> elements;
    uint64_t layout = 0;
    uint64_t revision = 0;
};

namespace detail {

inline uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ULL; }

// What the parameters of these elements are, and what kind: a change in
// either is a different compilation.
uint64_t layout_of(const std::vector<const Element*>& es);

// Each side is run in the head as expressions: a slot's value is a node - a
// value as the data holds it at the start (a leaf), or a sum of nodes times
// numbers, plus a number. Nodes are shared (the same sum of the same nodes is
// one node), so two sides that leave a slot the very same expression leave it
// the same value, bit for bit - the same operations on the same values in the
// same order, as the verifier's runs of the same declared rows would do - and
// it need not be compared. What is left to compare is where they differ, and
// the program is only what those need.
//
// A Workspace holds what equations of one graph share while they are checked
// together: the slots, the nodes, and each functor carried from untouched data
// (most sides carry a functor over data one arrow touched at most: the rest
// of its objects are what they always are, found once).
class Workspace {
public:
    explicit Workspace(StateGraph& g) : g_(g), revision_(g.revision()) {}
    const StateGraph& graph() const { return g_; }
    uint64_t revision() const { return revision_; }

    std::optional<Compiled> compile(const Equation& eq);

private:
    static constexpr uint32_t kNone = 0xffffffffu;
    struct SlotInfo {
        Key state, element, param;
        bool middle = false;
        const Element* read_from = nullptr;  // where its starting value is read
        Kind start = Kind::Absent;
    };
    struct Node {
        bool leaf = false;
        uint32_t slot = 0;                               // a leaf's
        double bias = 0.0;
        std::vector<std::pair<double, uint32_t>> terms;  // (k, node)
    };
    struct NodeKey {
        double bias;
        std::vector<std::pair<double, uint32_t>> terms;
        bool operator==(const NodeKey& o) const { return bias == o.bias && terms == o.terms; }
    };
    struct NodeHash {
        std::size_t operator()(const NodeKey& k) const;
    };
    struct PlaceKey {
        Key state, element;
        bool operator==(const PlaceKey& o) const { return state == o.state && element == o.element; }
    };
    struct PlaceHash {
        std::size_t operator()(const PlaceKey& k) const;
    };
    // An element, as a side sees it: a live one, or one made fresh.
    struct Place {
        Key state, element;
        const Element* live = nullptr;  // null: made fresh between two stages
        uint32_t id = kNone;            // its place in the workspace
    };
    // Every parameter slot of a place, by name, found once.
    struct PlaceSlots {
        std::unordered_map<Key, uint32_t> by_param;
        std::vector<Key> start_params;  // the parameters it has at the start, in order
    };
    // One side: what each slot it wrote holds now (a node, and what kind of
    // value), where it is on its path, which places it has touched, and what
    // it has put where there was nothing.
    struct Side {
        Key here, at;
        std::vector<uint32_t> values;  // slot -> node (kNone: as at the start)
        std::vector<uint8_t> kinds;    // slot -> kind + 1 (0: as at the start)
        std::vector<uint32_t> written;
        std::vector<uint8_t> dirty;    // place -> touched
        std::unordered_map<uint32_t, std::vector<Key>> added;  // place -> parameters it did not have
        Kind kind(const Workspace& w, uint32_t i) const;
        uint32_t value(Workspace& w, uint32_t i) const;
        bool touched(uint32_t place) const { return place < dirty.size() && dirty[place]; }
    };
    // A functor carried from untouched data: for each object, what it writes
    // where (on the element it lands on), and what it puts there new.
    struct Write {
        uint32_t slot, node;
        Kind kind;
    };
    struct Carried {
        uint32_t src, dst;  // places
        Key src_id, dst_id; // their elements
        std::vector<Write> writes;
        std::vector<Key> added;
        bool reads_target = false;
    };
    struct FunctorMemo {
        bool ok = true;
        std::vector<Carried> objects;
    };

    uint32_t place(Key state, Key element, const Element* live);
    Place live(const State& s, Key element, const Element* e) { return Place{s.id(), element, e, place(s.id(), element, e)}; }
    Place fresh();

    uint32_t slot(const Place& p, Key param);
    uint32_t leaf(uint32_t slot);
    uint32_t sum(double bias, std::vector<std::pair<double, uint32_t>> terms);
    void reads(const State& s, const Element* e);

    // The parameters an element has on this side, now.
    std::vector<Key> params_of(const Side& side, const Place& p);

    void write(Side& side, const Place& dst, Key param, uint32_t to, uint32_t node, Kind k);

    // What `run(Affine)` does (sg/core/Declared.hpp), as expressions.
    void affine(Side& side, const Affine& a, const Place& src, const Place& dst, const Params& args, bool* reads_target = nullptr);

    // Stages through elements made fresh between them.
    void stages(Side& side, const Stages& st, const Place& src, const Place& dst, const Params& args, bool* reads_target = nullptr);

    // A functor carried over untouched data, once: what each object writes.
    const FunctorMemo& carried(const Functor& f, const State& from, const State& to, const Params& args);

    bool side(const Path& p, Side& s);

    StateGraph& g_;
    uint64_t revision_;
    const Equation* eq_ = nullptr;
    std::vector<SlotInfo> slots_;
    std::vector<uint32_t> leaves_;
    std::vector<Node> nodes_;
    std::unordered_map<NodeKey, uint32_t, NodeHash> sums_;
    std::unordered_map<PlaceKey, uint32_t, PlaceHash> places_;
    std::vector<PlaceSlots> place_slots_;
    std::unordered_map<std::string, FunctorMemo> memo_;
    uint64_t fresh_ = 0;
    // What the equation being compiled reads.
    std::unordered_map<Key, const State*> states_;
    std::unordered_map<const Element*, bool> read_;
    std::vector<const Element*> elements_;
};

}  // namespace detail

// The equation as two programs on the data as it is laid out now - or
// nothing, if any step of it says nothing of what it does, or the compiler
// cannot say for certain what running it would show.
inline std::optional<Compiled> try_compile(StateGraph& g, const Equation& eq) {
    detail::Workspace w(g);
    return w.compile(eq);
}

// Whether what `c` was compiled on is still how the graph is.
bool still_holds(const StateGraph& g, const Compiled& c);

// The values the slots start from, as the data holds them now. What is not a
// number stands as one: a flag 0 or 1, a word its place among the words
// seen (`words`, shared by a batch), nothing a number of its own.
std::vector<double> start_of(const Compiled& c, std::unordered_map<std::string, double>& words);

// The laws' accelerator: equations compiled, kept per graph, run in a batch
// on a backend, and every one found broken checked again by the verifier.
class Accelerated : public laws::Accelerator {
public:
    struct Stats {
        std::size_t offered = 0, taken = 0, compiled = 0, reused = 0, refused = 0;
        std::size_t broken = 0;       // found broken by the batch (then checked by the verifier)
        std::size_t unsure = 0;       // too near the tolerance to say (checked by the verifier)
        std::size_t proved = 0;       // held by what they are: the same expressions, nothing to run
        double compile_ms = 0, run_ms = 0, recheck_ms = 0;
    };

    explicit Accelerated(Backend& backend) : backend_(backend) {}

    bool take(StateGraph& g, const Equation& eq) override;

    std::vector<Violation> finish(StateGraph& g) override;

    const Stats& stats() const { return stats_; }
    void reset_stats() { stats_ = Stats{}; }
    // Forget what was compiled (for one graph, or all).
    void forget(const StateGraph* g = nullptr);

private:
    Backend& backend_;
    std::unique_ptr<detail::Workspace> ws_;
    Batch batch_;
    std::vector<Equation> taken_;
    std::unordered_map<std::string, double> words_;
    std::unordered_map<const StateGraph*, std::unordered_map<std::string, std::unique_ptr<Compiled>>> cache_;
    std::unordered_map<const StateGraph*, std::unordered_map<std::string, uint64_t>> refused_at_;
    Stats stats_;
};

}  // namespace sg::algebra
