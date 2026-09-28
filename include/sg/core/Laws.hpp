// Stategine - the laws, checked on live data.
//
// The engine has two primitives: states, and the transitions between them.
// Everything with a law attached - an arrow inside a state, a functor across
// a transition, a lens behind a portal - is one of those two seen from a
// particular angle, and every law it owes has the same shape:
//
//     these two paths, run on the same data, leave the same result.
//
// So there is one checker, and the laws are equations it is handed. "The same
// result" is every element's parameters and whether it is alive, and every
// event queued - its name, its sender and its arguments, in the order queued:
//
//   identity        id ; f  ==  f  ==  f ; id
//   associativity   (f ; g) ; h  ==  f ; (g ; h)
//   composition     a registered composite does what its parts do, in order
//   functoriality   f then F  ==  F then F(f)    - the functor carries the
//                                                  arrow's *action*, not just
//                                                  its endpoints
//                   This is the square for each arrow F maps, not by itself
//                   the textbook F(id) = id and F(g . f) = F(g) . F(f): the
//                   identity is carried by construction, and a composite is
//                   held to the square only where F maps it.
//   put-get         write the view back, read it again: you see what you wrote
//   put-put         writing the same view twice is writing it once
//   settles         (get ; put) ; (get ; put)  ==  get ; put
//   drive           time acts on a driven state: step(0) == id, and, where
//                   the drive claims it, step(a) ; step(b) == step(a + b)
//   commutes        any two paths a caller declares equal
//   seam            where two like states meet, the meeting is two-way, its
//                   round trips are the identity, and both sides agree on
//                   what the seam itself is (a doorway, a door in it)
//
// A path is run on the states' current data, observed, and then undone, so
// checking the laws never changes the world. Whatever breaks an equation
// comes back as a counterexample: the element, the parameter, what it held
// before, and what each side left there.
//
// Structure that can be checked without data - does this compose at all - is
// checked before any of this: at compile time for typed handles (Typed.hpp),
// and by `StateGraph::validate` for everything else. What is left for here is
// what only the data can answer.
#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/Adjunction.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/core/Temporal.hpp"

namespace sg {

// ---------------------------------------------------------------------------
// A counterexample: where two sides of an equation came apart, and how.
// ---------------------------------------------------------------------------
struct Violation {
    std::string law;    // identity, associativity, composition, functoriality, ...
    std::string where;  // what it was checked on
    std::string lhs;    // the two paths, as run
    std::string rhs;
    Key state;          // where they disagree; empty when the paths do not even type
    Key element;
    std::string key;    // a parameter, or <element>, <alive>, <emitted>
    std::string before; // the value both sides started from
    std::string left;   // what each side left there
    std::string right;
    std::string args;   // the event arguments the arrows were run with
    std::string detail; // the reason, when there is no single value to show
    // Not a counterexample: the equation could not be checked at all, because
    // running a side would change what the graph is made of (see Trial).
    bool refused = false;
    // Not a counterexample either: a search stopped at the budget it was given
    // (LawOptions), and what lies past it was not looked at. Said, so that
    // "nothing found" is never read as "nothing there".
    bool bounded = false;

    std::string str() const;
};

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
// One step: an arrow inside the current state, or a functor / transition out
// of it. Steps normally name registered arrows; a trial composite that must
// not be registered (associativity builds both bracketings) is carried here.
// An event step fires an event at the state as a whole, the way a frame does:
// every arrow it triggers runs, and what they emit is dispatched in turn.
// A step may carry its own event arguments; otherwise the equation's are used.
struct Step {
    enum class Kind { Arrow, Functor, Transition, Event };
    Kind kind = Kind::Arrow;
    Key name;
    std::shared_ptr<const Morphism> arrow;
    std::shared_ptr<const Functor> functor;
    std::optional<Params> args;
    Key source;  // who sends an event step; the state itself when empty
};

// A path starts at an object - an element of a state, or the state as a
// whole when no element is named - and each step has to start where the last
// one ended. That is checked when the path is run; the typed handles in
// Typed.hpp check it when the path is written.
class Path {
public:
    Path() = default;
    explicit Path(Key state, Key element = Key{}) : state_(state), element_(element) {}

    Path& arrow(Key name) { return push(Step{Step::Kind::Arrow, name, nullptr, nullptr}); }
    Path& arrow(Morphism m);
    Path& functor(Key name) { return push(Step{Step::Kind::Functor, name, nullptr, nullptr}); }
    Path& functor(Functor f);
    Path& transition(Key name);
    // An arrow run with arguments of its own, and an event fired at the state.
    Path& arrow(Key name, Params args);
    Path& event(Key trigger, Params args = {});
    // The same event, as the engine sends it: its sender and its arguments.
    Path& event(const Event& ev);

    // Concatenation; the seam is checked when the path runs.
    Path& then(const Path& p);

    Key state() const { return state_; }
    Key element() const { return element_; }
    const std::vector<Step>& steps() const { return steps_; }

    std::string str() const;

private:
    Path& push(Step s) {
        steps_.push_back(std::move(s));
        return *this;
    }

    Key state_;
    Key element_;
    std::vector<Step> steps_;
};

// An equation between two paths, and the event arguments to run arrows with.
struct Equation {
    std::string law;
    std::string where;
    Path lhs;
    Path rhs;
    Params args;
};

// A diagram is a set of equations the caller claims hold. Build one from live
// data - "whatever the inventory holds now, sorting then filtering is filtering
// then sorting" - and hand it to `check`.
class Diagram {
public:
    explicit Diagram(std::string name = "diagram") : name_(std::move(name)) {}

    Diagram& commutes(Path lhs, Path rhs, Params args = {});

    const std::string& name() const { return name_; }
    const std::vector<Equation>& equations() const { return equations_; }

private:
    std::string name_;
    std::vector<Equation> equations_;
};

namespace laws {
// What answers some equations faster than running them - compiled, and run
// in a batch (sg/algebra) - and hands every other one back. It takes an
// equation or it does not (`take`); what it took it answers at the end
// (`finish`), and every counterexample it gives is the verifier's own: an
// equation it finds broken is checked again here, by running it, and only
// what that finds is said. It is never a second truth.
class Accelerator {
public:
    virtual ~Accelerator() = default;
    virtual bool take(StateGraph& g, const Equation& eq) = 0;
    virtual std::vector<Violation> finish(StateGraph& g) = 0;
};
// The accelerator the laws offer their equations to now, if any.
inline Accelerator*& accelerating() {
    static thread_local Accelerator* a = nullptr;
    return a;
}
// For as long as it lives, the laws offer their equations to `a` first.
class Accelerating {
public:
    explicit Accelerating(Accelerator* a) : was_(accelerating()) { accelerating() = a; }
    ~Accelerating() { accelerating() = was_; }
    Accelerating(const Accelerating&) = delete;
    Accelerating& operator=(const Accelerating&) = delete;

private:
    Accelerator* was_;
};
}  // namespace laws

// How the automatic laws probe arrows that read event arguments. An integrator
// that returns early when dt is zero satisfies every law vacuously; give it a
// dt and the laws are about something.
struct LawOptions {
    Params args;                                 // handed to every trial event
    std::unordered_map<Key, Params> args_for;    // per trigger, overriding `args`
    std::size_t max_triples = 256;               // associativity budget, per state
    double drive_dt = 0.5;                       // the step a drive's laws take
    // Offered every equation first, when set (see laws::Accelerator).
    laws::Accelerator* accelerate = nullptr;
};

namespace laws {
struct Outcome;
}

// ---------------------------------------------------------------------------
// LawCache: what the laws found, kept for as long as it still holds.
//
// Checking a law runs arrows and functors on trial copies of the data. An
// equation's answer depends only on what its paths read: the data of the
// states they pass through, and the functors they cross. Both carry stamps
// (see Stamps in Core.hpp), so an answer can be kept with the versions it was
// found on, and given again while they are the same - an unchanged law then
// costs a comparison. When only part of an equation changed, one side of it
// may still stand, and only the other is run again: in a square
// A -> B -> D = A -> C -> D whose B -> D was replaced, only that side runs.
//
// Keeping is not free: the versions are a pass over each state's elements, a
// side kept is a copy of the state it ended in. So each equation is watched:
// one that is cheaper to run than to keep, or that never comes out the same
// twice (its data changes every time), is checked directly for a while, and
// looked at again later. Direct and Incremental force one or the other - to
// measure against, and for tests.
//
// Nothing here is part of what the laws say. A cache can be cleared at any
// time; the answers are the same, only slower to find.
// ---------------------------------------------------------------------------
class LawCache {
public:
    enum class Strategy { Adaptive, Direct, Incremental };

    struct Stats {
        std::size_t equations = 0;     // equations asked about
        std::size_t direct = 0;        // run without keeping
        std::size_t reused = 0;        // answered from what was kept
        std::size_t recomputed = 0;    // run again, and kept
        std::size_t sides_reused = 0;  // one side kept, only the other run
        double run_ns = 0;             // time spent running paths
        double bookkeeping_ns = 0;     // time spent on versions and keys
    };

    explicit LawCache(Strategy s = Strategy::Adaptive) : strategy_(s) {}

    Strategy strategy() const { return strategy_; }
    const Stats& stats() const { return stats_; }
    void reset_stats() { stats_ = Stats{}; }
    void clear() {
        entries_.clear();
        sides_.clear();
        side_elements_ = 0;
    }
    // How many elements' worth of ended states may be kept, over all sides.
    void set_side_budget(std::size_t elements) { side_budget_ = elements; }
    // Roughly what is kept, in bytes.
    std::size_t memory() const;

    // --- for the laws -----------------------------------------------------------
    // A new round of checks: versions are read afresh.
    void begin(StateGraph& g);

    // What a path reads, as numbers: each state it passes through (its data,
    // arrows included), and each functor it crosses - a registered one by its
    // own stamp, one made on the spot (a composite made to check a law) by
    // every functor at its two ends, since it was made from those.
    void deps_of(const Path& p, std::vector<uint64_t>& out);

    struct Entry {
        std::vector<uint64_t> deps;
        std::vector<Violation> result;
        double cost_ns = 0;         // what running it took, last time
        double keep_ns = 0;         // what keeping it took, last time
        unsigned misses = 0;        // came out needing a run, in a row
        uint64_t direct_until = 0;  // checked directly until this round
        bool kept = false;
    };

    struct Side {
        std::vector<uint64_t> deps;
        std::shared_ptr<const laws::Outcome> outcome;
        std::size_t elements = 0;
    };

    Entry& entry(const std::string& key) { return entries_[key]; }
    bool direct(const Entry& e) const;
    // After a check: should this equation be checked directly for a while?
    void judge(Entry& e, bool hit);

    const Side* side(const std::string& key, const std::vector<uint64_t>& deps) const;
    void keep_side(const std::string& key, std::vector<uint64_t> deps,
                   std::shared_ptr<const laws::Outcome> o, std::size_t elements);

    Stats& tally() { return stats_; }

private:
    uint64_t state_version(Key id);
    uint64_t functor_version(Key state);

    Strategy strategy_;
    Stats stats_;
    StateGraph* g_ = nullptr;
    uint64_t round_ = 0;
    std::unordered_map<const State*, uint64_t> versions_;
    std::unordered_map<Key, uint64_t> functor_versions_;
    std::unordered_map<std::string, Entry> entries_;
    std::unordered_map<std::string, Side> sides_;
    std::size_t side_elements_ = 0, side_budget_ = 1u << 20;
};

namespace laws {

// --- trial runs --------------------------------------------------------------
// Takes a snapshot of each state the first time a path touches it and puts
// every one of them back on destruction. While it lives the graph is sealed
// (StateGraph::Sealed): a trial undoes data, and what the graph is made of -
// what joins its states, and what is in each, elements and arrows - is not
// data. A path that would change any of it is stopped there and reported as
// not running; nothing structural is ever rolled back, because nothing
// structural is ever let happen.
class Trial {
public:
    explicit Trial(StateGraph& g) : g_(g) {
        sealed_.emplace(g);
        ++detail::trials();
    }
    Trial(const Trial&) = delete;
    Trial& operator=(const Trial&) = delete;
    ~Trial();

    bool touched(Key id) const { return saved_.count(id) != 0; }

    State& touch(Key id);

private:
    StateGraph& g_;
    std::unordered_map<Key, State::Snapshot> saved_;
    std::optional<StateGraph::Sealed> sealed_;
};

struct Outcome {
    std::string error;  // non-empty when the path does not type or cannot run
    bool refused = false;  // it would have changed the graph's structure, so it was not run
    Key state;          // where it ended
    Key element;
    State::Snapshot data;
};

Params args_for(const LawOptions& o, Key trigger);

// Run `p` on the live data, record where it ended and what it left there,
// and undo it.
Outcome run_steps(StateGraph& g, const Path& p, const Params& args);

// The same, and a path that tries to rewrite the graph - add a functor, an
// embedding - is stopped there, undone, and said not to run.
Outcome run(StateGraph& g, const Path& p, const Params& args);

// Two values agree if they are the same value, where numbers - ints and
// doubles alike - are compared with a tolerance, and headings on the circle.
bool same_value(Key k, const Value& a, const Value& b);

std::string args_str(const Params& p);

// Two events are the same event if they have the same name, came from the
// same sender and carry the same arguments - compared as values are, so a
// number a hair apart is still the same number.
bool same_params(const Params& a, const Params& b);

bool same_event(const Event& a, const Event& b);

std::string event_str(const Event& e);

// Every place where two outcomes disagree, as counterexamples.
std::vector<Violation> diff(const Equation& eq, const Outcome& l, const Outcome& r,
                                   const State::Snapshot& before);

// Where two outcomes disagree. What the data held before is needed only to
// say so, so it is copied only when there is something to say.
std::vector<Violation> settle(StateGraph& g, const Equation& eq, const Outcome& l,
                                     const Outcome& r);

// Asks the cache first: `deps` are what the answer is found on
// (LawCache::deps_of), `find` finds it when it must be found.
template <typename Find>
std::vector<Violation> kept(LawCache& cache, const std::string& key,
                            std::chrono::steady_clock::time_point asked,
                            std::vector<uint64_t> deps, Find&& find) {
    using clock = std::chrono::steady_clock;
    const auto ns = [](clock::duration d) { return std::chrono::duration<double, std::nano>(d).count(); };
    LawCache::Stats& st = cache.tally();
    LawCache::Entry& e = cache.entry(key);
    const auto looked = clock::now();
    if (e.kept && e.deps == deps) {
        ++st.reused;
        e.keep_ns = ns(looked - asked);
        st.bookkeeping_ns += e.keep_ns;
        cache.judge(e, true);
        return e.result;
    }
    std::vector<Violation> out = find();
    const auto found = clock::now();
    ++st.recomputed;
    e.deps = std::move(deps);
    e.result = out;
    e.kept = true;
    e.cost_ns = ns(found - looked);
    st.run_ns += e.cost_ns;
    e.keep_ns = ns(looked - asked) + ns(clock::now() - found);
    st.bookkeeping_ns += e.keep_ns;
    cache.judge(e, false);
    return out;
}

std::vector<Violation> check_direct(StateGraph& g, const Equation& eq);

// Run both sides of one equation and report where they disagree - or, with a
// cache, say what was found before, if what it was found on still holds.
std::vector<Violation> check(StateGraph& g, LawCache* cache, const Equation& eq);

std::vector<Violation> check(StateGraph& g, const Equation& eq);

void append(std::vector<Violation>& to, std::vector<Violation> from);

// --- the laws ----------------------------------------------------------------

// id ; f == f == f ; id, for every arrow of every state and every functor.
// The identities are the engine's own; a unit that fails here means
// composition with it is broken, which is exactly what used to happen when
// the identity functor was a snapshot of an object list.
std::vector<Violation> identity(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr);

// A search that stopped at its budget, said as such.
Violation bounded(const std::string& law, const std::string& where, std::size_t budget);

// (f ; g) ; h == f ; (g ; h) == f ; g ; h, for composable triples of arrows in
// each state and of registered functors. Past `max_triples` the search stops,
// and says so.
std::vector<Violation> associativity(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr);

// A registered composite is a claim that one arrow does what a chain does.
// For arrows the chain is recorded by State::compose, for functors by
// StateGraph::compose_functors; either way the claim is run and checked.
std::vector<Violation> composition(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr);

// f then F == F then F(f): a functor maps an arrow to one that does the same
// thing on the other side. `check_laws` already holds the endpoints to this;
// here it is the data.
std::vector<Violation> functoriality(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr);

// Time acts on every driven state as a monoid acts (see Temporal.hpp): no time
// is no change, and, where the drive claims it, two steps are one step as long
// as both. Each side fires the drive's event as a frame does, with the time
// and frame the clock would say.
std::vector<Violation> drives(StateGraph& g, const LawOptions& o = {},
                                     LawCache* cache = nullptr);

// The lens behind every two-way portal. `in` reads the subject into the
// guest (get), `out` writes the guest back (put). A view may lose detail -
// cells for metres, dozens for units - so get ; put need not be the identity,
// but these hold for any view worth the name:
//
//   put-get   what you wrote through the view reads back as written. Checked
//             on a freshly opened view, and on the guest's own data while the
//             portal is open and the guest holds real edits.
//   settles   get ; put, done twice, is done once.
//   put-put   writing the same view twice is writing it once - no write-back
//             that adds rather than sets.
std::vector<Violation> lenses(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr);

// Caller-declared diagrams, against the data as it stands.
std::vector<Violation> diagram(StateGraph& g, const Diagram& d, LawCache* cache = nullptr);

// --- adjunctions -----------------------------------------------------------------
// F -| G on live data: the four equations `Adjunction::check` decides on the
// arrows (unit and counit naturality, both triangles), each run from its
// object on the states' current data. The structural check says the paths
// are the same word; this says they do the same thing, which is all a pair
// of different words can be asked for.
std::vector<Violation> adjunction(StateGraph& g, const Adjunction& adj,
                                         const LawOptions& o = {}, LawCache* cache = nullptr);


// --- seams ---------------------------------------------------------------------
// Where two states meet as one place (see `Seam` in StateGraph.hpp): a
// boundary in each, glued. A seam is sound when:
//
//   two-way     between two states of the same kind, every functor that
//               crosses - a transition's, a window's - is one direction of a
//               seam's travel, so there is always a way back;
//   travel      across and back is the identity, both ways round, and travel
//               never touches either boundary;
//   glue        each glue is defined on exactly its boundary and lands on
//               exactly the other one, one to one, and the two glues are
//               inverse - a bijection of boundaries;
//   agreement   the boundary carried across *is* the boundary on the other
//               side: every value the glue gives the far copy is the value it
//               already has.
//
// The last is the gluing condition of a sheaf - sections agree on the overlap -
// and it is what catches a doorway that is only in one room, a door that
// swings on one side and not the other, a portal moved without its twin.
namespace seam_detail {

void report(std::vector<Violation>& out, const std::string& where, const std::string& lhs,
                   const std::string& rhs, const std::string& detail);

// Every value `image` holds, held the same by `have`.
void agree(std::vector<Violation>& out, const std::string& where, const std::string& lhs,
                  const std::string& rhs, const Element& image, const Element* have, const std::string& what);

// Across by `there`, back by `back`: everything mapped comes home unchanged.
void round_trip(std::vector<Violation>& out, const std::string& where, const Functor& there,
                       const Functor& back, const State& side);

bool travels(const StateGraph& g, Key f);

}  // namespace seam_detail

std::vector<Violation> seams(const StateGraph& g);

}  // namespace laws

// ---------------------------------------------------------------------------
// Everything the graph owns at once: the structure `validate` checks, then
// identity, associativity, composition, functoriality, the lens laws, the
// drives, the seams, and any diagrams handed in. What a graph does not own is checked
// where it is declared: descent on a `Cover` (`descent_defects`, Sheaf.hpp),
// an `Adjunction`'s unit and counit, `interface_defects` on embeddings.
// ---------------------------------------------------------------------------
struct LawReport {
    std::vector<std::string> structure;  // StateGraph::validate
    std::vector<Violation> violations;
    // Equations that could not be checked: a side would have rewritten what
    // the graph is made of (added an element, a functor...), which a trial
    // never lets happen. Neither broken nor shown to hold.
    std::vector<Violation> unchecked;
    // Searches that stopped at the budget the caller gave them: what lies past
    // it was not looked at.
    std::vector<Violation> bounded;

    // No counterexample found. Not the same as the laws holding: see
    // all_checked() and complete().
    bool ok() const { return structure.empty() && violations.empty(); }
    bool all_checked() const { return unchecked.empty(); }
    bool complete() const { return bounded.empty(); }
    // No counterexample, and every equation was run: the laws hold, as far as
    // the budgets reach.
    bool holds() const { return ok() && all_checked(); }

    std::string str() const;
};

namespace laws {
// Counterexamples to one side, equations that could not be checked to the other.
void sort_into(LawReport& r, std::vector<Violation> from);
}  // namespace laws

LawReport verify(StateGraph& g, const std::vector<Diagram>& diagrams = {},
                        const LawOptions& o = {});

// The same, keeping what it finds in `cache` and answering from it wherever
// what an answer was found on has not changed: the report is verify's, and a
// graph checked again unchanged costs next to nothing.
LawReport verify(StateGraph& g, LawCache& cache, const std::vector<Diagram>& diagrams = {},
                        const LawOptions& o = {});

// For callers that would rather not start at all than start on a lie.
struct LawError : std::runtime_error {
    explicit LawError(LawReport r)
        : std::runtime_error(std::string(r.ok() ? "stategine: the graph cannot be shown to keep its laws"
                                                : "stategine: the graph breaks its laws") +
                             "\n" + r.str()),
          report(std::move(r)) {}
    LawReport report;
};

// Refuses a graph that breaks a law, and one with an equation that could not
// be checked at all: what is unknown is not taken for lawful. A search cut
// short by a budget the caller set is not refused - the caller chose it - but
// is in the report.
void enforce(StateGraph& g, const std::vector<Diagram>& diagrams = {},
                    const LawOptions& o = {});

}  // namespace sg
