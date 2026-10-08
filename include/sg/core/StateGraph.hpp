// Stategine - StateGraph: states as objects, transitions as arrows, functors
// as the typed data paths between them, embeddings as nested interfaces.
#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sg/core/Embedding.hpp"
#include "sg/core/Functor.hpp"
#include "sg/core/State.hpp"

namespace sg {

// What a transition does to the state stack when taken.
enum class TransitionKind {
    Switch,  // pop the current state, enter the target
    Push,    // pause the current state, enter the target on top
    Pop      // exit the current state, resume the one below (no target)
};

struct Transition {
    using Guard = std::function<bool(const State& from, const Event&)>;
    using Action = std::function<void(State& from, const Event&, Params& args)>;

    Key name;
    Key from;     // state id, or "*" for any state
    Key to;       // state id (empty for Pop)
    Key trigger;  // event name
    TransitionKind kind = TransitionKind::Switch;
    Guard guard;    // optional: the transition is taken only if this passes
    Action action;  // optional: fills the Params handed to on_enter
    Key functor;    // optional: carries the source's data into the target
    // What the state entered is told, as data: set in the Params handed to
    // on_enter before `action` runs. What is a constant is not a lambda.
    Params enter;
};

// A seam: two states that meet as one place - two rooms and the doorway
// between them. An interface, in the plainest terms: a boundary in each
// domain, and a gluing between the boundaries.
//
//   boundary_a         the objects of `a` that are the interface - a doorway,
//   boundary_b         a door hanging in it - and the same in `b`
//   glue_ab, glue_ba   functors between the boundaries: each defined on the
//                      whole of its boundary and nothing else, onto the whole
//                      of the other, and each the other's inverse - a
//                      bijection. And they *agree*: the boundary carried
//                      across is the boundary already there.
//   a_to_b, b_to_a     travel: what crosses (a viewer, a thrown ball), carried
//                      into the other side's frame. Mutually inverse, and
//                      never touching the boundary - that would move the
//                      doorway every time somebody walked through it.
//
// Between two states of the same kind every functor that crosses must be one
// direction of a seam's travel, so an interface between like states cannot
// be one way. `laws::seams` holds all of this to the data.
struct Seam {
    Key name;
    Key a, b;
    Key a_to_b, b_to_a;
    Key glue_ab, glue_ba;
    std::vector<Key> boundary_a, boundary_b;
    // The rings through it are the shape of the space, not a fault in it: a
    // torus's sides, a portal pair in one room, a surface of higher genus. Go
    // round one and you arrive turned or moved by the space itself. Only a
    // seam that wraps may join a state to itself; every ring that does not
    // pass through one must still close.
    bool wraps = false;
};

// In how many dimensions a state is walked: its `walk_dims` if it says (a
// world of three held to a plane says 2), else 3 for a space3d, 2 for a
// space2d, 0 for what is not walked. A seam joins like to like (validate).
int walked_dims(const State& s);

// Whether a driven state's time goes on only while it is active - the room
// you are in, and what is open in it - or always: what goes on in a room you
// stepped out of (a door still swinging, a record still turning).
// When a driven state's time goes on:
//   WhileActive   when it steps - the active state, and what is open in it
//   Always        every frame, wherever it is (a record still turning)
//   WhileShown    every frame some embedding shows it open - wherever that
//                 is, the active state or not (a game on a set, played by
//                 no one)
//   WhileFocused  only while it has the input (a game on a computer, which
//                 waits while another window is in front)
//   WhileEntered  only while it is where one is - the active state itself,
//                 not what is open in another (a world in a painting, still
//                 on the wall and going on only once someone is in it)
enum class Keeps { WhileActive, Always, WhileShown, WhileFocused, WhileEntered };

// A drive: `state` changes with the time `clock` keeps. Each frame the state
// steps, its line on the clock advances by dt and the state's arrows on
// `trigger` are fired with {dt, time, frame}. A line keeps one state's time
// and moves only when it steps (an idle state computes nothing, and loses no
// time); one clock may keep many lines. `additive` claims step(a) ; step(b) == step(a + b), and the laws hold
// it to that (see Temporal.hpp).
struct Drive {
    Key name;
    Key clock;
    Key state;
    Key trigger;
    bool additive = false;
    Key line;  // the clock's timeline this state's time is kept on; the state's name when empty
    Keeps keeps = Keeps::WhileActive;
};

class StateGraph;

// An edit: what a state says - an editor's request, a command line - that
// rewrites the graph. The one way the world changes what the world is made
// of while it runs: the state says it (State::says), the graph declares
// here that it is an edit and what it does, and the engine applies it at the
// start of the next frame, before anything else moves - never inside an
// arrow, a listener or a law's trial. What the edit answers is heard back by
// the state that asked, as `reply` (its own arrows show it).
struct Edit {
    using Apply = std::function<Params(StateGraph& g, const Event& asked)>;
    Key name;
    Key state;  // who asks
    Key event;  // what it says to ask
    Apply apply;
    Key reply;  // what it hears back; `<event>.done` when empty
    Key native;  // the native computation `apply` is, when a source declared one (Morphism::native)
};

// What a graph was made of at a moment, to go back to (StateGraph::checkpoint,
// rollback). Opaque: it is the graph's own to read.
class Checkpoint {
private:
    friend class StateGraph;
    std::set<Key> states, functors, composites, defaults;
    std::size_t transitions = 0, embeddings = 0, lenses = 0, ports = 0, kept = 0;
    std::deque<Seam> seams;
    std::deque<Drive> drives;
    std::deque<Edit> edits;
    Key initial;
};

// Who may change what. Whoever holds the graph itself - the code that builds
// the world, and whatever rewrites it while it runs - may change anything in
// it, through the states, functors and graph operations it hands back.
// Whoever holds it as `const` (an Engine's `graph()`, a renderer) may look at
// everything and change nothing: every state, element, functor and embedding
// reached from a const graph is const. That is enforced by the compiler and
// costs nothing when the game runs.
//
// What a state does to itself is its own arrows' business (they are handed
// the state), and what crosses between states is a functor's or an
// embedding's. What the graph is made of - states, their elements and arrows,
// functors and their maps, embeddings, seams, transitions - is declared here,
// and every change to it is counted (`revision()`): a single add, never a
// check. The checks run at their own time, when they see the count moved.
class StateGraph {
public:
    StateGraph() = default;
    StateGraph(const StateGraph&) = delete;
    StateGraph& operator=(const StateGraph&) = delete;

    // --- states -------------------------------------------------------------
    template <typename T, typename... Args>
    T& add(Args&&... args) {
        static_assert(std::is_base_of<State, T>::value, "T must derive from sg::State");
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        insert(std::move(owned));
        return *raw;
    }

    State& add(StatePtr s);

    State* find(Key id);
    const State* find(Key id) const;

    State& state(Key id);
    const State& state(Key id) const;

    bool contains(Key id) const { return state_index_.count(id) != 0; }
    std::size_t size() const { return states_.size(); }

    // Every state, in place: for whoever holds the graph (the engine, once a
    // frame) - no list made.
    template <typename F>
    void each_state(F&& f) {
        for (auto& kv : states_) f(*kv.second);
    }

    std::vector<Key> ids() const;

    // --- transitions ---------------------------------------------------------
    // A transition is declared whole - fill in a Transition, guard and all, and
    // connect it - and is read, not rewritten, once it is in the graph.
    // Its name is its identity - a law's path takes it by name - so no two
    // share one: a name given twice is refused, and one made up for it
    // (from-trigger->to) is told apart from an alternative on the same
    // event, guarded otherwise, by a number (#2, #3, ...).
    const Transition& connect(Transition t);

    const Transition& connect(Key from, Key trigger, Key to,
                              TransitionKind kind = TransitionKind::Switch);

    // A switch that carries the source's data into the target by `functor`.
    const Transition& connect(Key from, Key trigger, Key to, Key functor);

    // What a transition carries across, changed - a doorway rebuilt, a way
    // through unglued (empty: nothing carried). Counted like any rewiring.
    bool set_carry(Key transition, Key functor);

    const Transition& push(Key from, Key trigger, Key to) {
        return connect(from, trigger, to, TransitionKind::Push);
    }

    const Transition& pop(Key from, Key trigger) {
        return connect(from, trigger, Key{}, TransitionKind::Pop);
    }

    const std::deque<Transition>& transitions() const { return transitions_; }
    // Where a transition can be taken from, as the graph knows it: its own
    // `from`, or - taken from any state - the states that say its trigger
    // (State::says). Empty if it is from any state and nothing declares it.
    std::vector<Key> sources(const Transition& t) const;

    const Transition* transition(Key name) const;

    // First transition out of `from` for this event whose guard passes.
    // Concrete sources win over "*".
    const Transition* resolve(const State& from, const Event& ev) const;

    static Key any() { return Key{"*"}; }

    // Taking a transition, as far as what it does to data: its action on the
    // state it leaves (filling the arguments the target is entered with), then
    // its functor, carrying the event that took it along. The engine takes a
    // transition by this, and so does a law's path: one transition, one
    // meaning.
    void cross(const Transition& t, State& from, State* target, const Event& ev, Params& args) const;
    // What a state entered by a transition hears (cross): {from, by}.
    static Key entered_event() { return Key{"state.entered"}; }

    // --- functors -------------------------------------------------------------
    Functor& add_functor(Functor f);

    Functor& add_functor(Key name, Key from, Key to) { return add_functor(Functor{name, from, to}); }

    // Replace a functor, or add it if new. Transitions that are *derived* from
    // something else - a doorway derived from the two portal elements it joins -
    // are rebuilt rather than declared, so that they cannot drift out of step
    // with what they describe.
    Functor& set_functor(Functor f);

    // Take a functor away, and whatever declares it a lens with another. One
    // an embedding or a transition still uses is refused: take those first.
    bool drop_functor(Key name);

    const Functor* functor(Key name) const;

    Functor* functor(Key name);

    const std::map<Key, Functor>& functors() const { return functors_; }

    // Composite of a chain of registered functors, registered under `name`.
    Functor& compose_functors(Key name, const std::vector<Key>& chain);

    // What a registered composite was built from, first applied first. A
    // composite is a claim - "this one arrow does what that chain does" - and
    // keeping the chain is what lets the claim be checked.
    const std::vector<Key>* composite_chain(Key name) const;

    // Declare F and G together: the usual way one domain gets an editable view
    // in another. Declaring checks nothing - there is no data yet to check on;
    // the round trip is held to the lens laws (put-get, put-put, settles) by
    // `sg::verify` / `laws::lenses` once the pair is used by an embedding.
    struct Lens {
        Functor& in;   // host -> guest
        Functor& out;  // guest -> host
    };

    // Two functors held to be a lens on their own, with no embedding: `get`
    // shows one state in another, `put` writes the other back - a window
    // showing a board it can also draw on. `sg::verify` holds the pair to the
    // lens laws as it does an embedding's.
    struct LensPair {
        Key get;
        Key put;
    };
    void lens(Key get, Key put);
    const std::vector<LensPair>& lenses() const { return lenses_; }

    Lens add_lens(Key in_name, Key out_name, Key host, Key guest,
                  const std::vector<std::pair<Key, Key>>& objects,  // {host id, guest id}
                  Transport to_guest, Transport to_host);

    // --- embeddings ------------------------------------------------------------
    // An embedding is declared whole - fill in an Embedding, or name its parts
    // - and is read, not rewritten, once it is in the graph: what it joins
    // changes only through `set_sync`, `set_propagation` or dropping it and
    // embedding another, each counted. Whether it takes focus when opened is
    // declared (`set_focus`); whether it is open, and focused, is its host's -
    // what its portal says (`open_key`, `focus_key`) - and the engine follows.
    const Embedding& embed(Embedding e);

    const Embedding& embed(Key name, Key host, Key portal, Key guest, Key in, Key out,
                           EmbedSync sync = EmbedSync::Commit, Key subject = Key{});

    const Embedding& embed(Key host, Key portal, Key guest, Key in = Key{}, Key out = Key{},
                           EmbedSync sync = EmbedSync::Commit);

    // Whether it takes input when it is opened. Returns the embedding's name,
    // so a declaration can say it in one line.
    // Whether it is open exactly while its portal's `open` says (see
    // Embedding::follows). Returns the embedding's name.
    Key set_follows(Key name, bool on);
    Key set_focus(Key name, bool on);
    // When its every-frame direction runs (see Propagation).
    bool set_propagation(Key name, Propagation p);
    // Which way data runs through it (see EmbedSync).
    bool set_sync(Key name, EmbedSync s);

    const Embedding* embedding(Key name) const;

    // Taken away: the guest no longer lives in that portal. (Close it first
    // if it is open; a closed one leaves nothing behind.)
    bool drop_embedding(Key name);

    const std::deque<Embedding>& embeddings() const { return embeddings_; }
    // Where the embeddings of a host, or of a guest, sit in `embeddings()` -
    // no list built. Good until the graph's revision moves.
    const std::vector<std::size_t>& embeddings_hosted_by(Key host_id) const;
    const std::vector<std::size_t>& embeddings_holding(Key guest_id) const;

    // --- drives -----------------------------------------------------------------
    // Registered by name; registering the same name again replaces it.
    const Drive& drive(Drive d);
    const Drive& drive(Key name, Key clock, Key state, Key trigger, bool additive = false);
    void drop_drive(Key name);
    const std::deque<Drive>& drives() const { return drives_; }

    // --- ports ------------------------------------------------------------------
    // Where the world outside may speak to a state directly: a program's
    // output reaching the shell that shows it, a sensor its gauge. Declared,
    // so `Engine::send` delivers only what the graph says may come in.
    void port(Key state, Key event);
    bool has_port(Key state, Key event) const;
    const std::vector<std::pair<Key, Key>>& ports() const { return ports_; }

    // --- kept functors ------------------------------------------------------------
    // A functor whose target follows its source: whenever an object it maps
    // changed, the engine carries it (by the frame, with a memo - nothing
    // changed costs a comparison). A model on a table kept to the room it is
    // of, with no portal between them. Kept again, nothing changes.
    void keep(Key functor, bool on = true);
    const std::vector<Key>& kept() const { return kept_; }

    // --- edits ------------------------------------------------------------------
    // Registered by name (`state:event` when empty); again, it is replaced.
    const Edit& edit(Edit e);
    const Edit& edit(Key state, Key event, Edit::Apply apply);
    void drop_edit(Key name);
    const std::deque<Edit>& edits() const { return edits_; }

    // --- seams ------------------------------------------------------------------
    // Registered by name; registering the same name again replaces it (a seam
    // is rebuilt whenever its doorways move).
    const Seam& add_seam(Seam s);
    // Unglued: the two sides are no longer one place, and nothing holds them
    // to agree.
    void drop_seam(Key name);
    const std::deque<Seam>& seams() const { return seams_; }
    const Seam* seam(Key name) const;

    void set_initial(Key id) {
        rev_.rewired("set_initial");
        initial_ = id;
    }
    // Counts every change to what the graph is made of - a state, an element
    // or an arrow in one, a functor or its maps, an embedding, a seam, a
    // transition - so whoever checks it knows when it must look again.
    // Changing a value (an element's params) is not a change of structure and
    // does not count.
    uint64_t revision() const { return rev_.all; }
    // The same, for the interfaces alone - states, functors, embeddings,
    // seams, transitions: what joins states, not what is in them.
    uint64_t topology() const { return rev_.topology; }

    // Whether a law's trial is running: the graph cannot be changed now.
    bool sealed() const { return rev_.sealed > 0; }

    // --- all or nothing ------------------------------------------------------------
    // A change made of many - a whole construction from a source - is made all
    // or not at all: take a checkpoint, make it, and if any of it fails, or the
    // graph it made does not validate, `rollback` puts back what was there. The
    // graph is the same graph (its states are not copied or replaced): what was
    // added since is taken away, and what was replaced by name comes back.
    // Everything reads and counts as a change of structure, so whatever the
    // engine derived from the graph is derived again. Refused while sealed.
    // The states a change touched *inside* (a look slot added to a host, a
    // timeline added to a clock) are theirs to put back: State::snapshot / restore.
    Checkpoint checkpoint() const;
    void rollback(const Checkpoint& c);

    // While a Sealed lives, the interfaces cannot change: a law's trial runs
    // arrows and functors on the live graph, and one that tried to rewrite
    // what joins the states would leave the world changed after the check
    // undid its data. Such a change throws RewriteRefused, the trial is
    // undone, and the check reports it. The same holds inside a state: an
    // element or arrow added or removed on trial is refused too (see Revision
    // in Core.hpp) - a trial undoes data, and structure is not data.
    class Sealed {
    public:
        explicit Sealed(StateGraph& g) : g_(g) { ++g_.rev_.sealed; }
        ~Sealed() { --g_.rev_.sealed; }
        Sealed(const Sealed&) = delete;
        Sealed& operator=(const Sealed&) = delete;

    private:
        StateGraph& g_;
    };

    // --- defaults ------------------------------------------------------------------
    // Every state has a starting point - how it was when it was made - and can
    // be put back to it. `keep_defaults` takes it for every state that has
    // none yet (the engine does, as it starts; a state made later keeps its
    // own when this is called again, or by `keep_default`).
    void keep_defaults();
    void keep_default(Key id);
    bool has_default(Key id) const { return defaults_.count(id) != 0; }
    // Put `id` back as it started; with `guests`, whatever lives in its
    // portals too, and theirs. False if it has no default.
    bool restore_default(Key id, bool guests = false) {
        std::set<Key> done;
        return restore_default(id, guests, done);
    }
    bool restore_default(Key id, bool guests, std::set<Key>& done);
    Key initial() const { return initial_; }

    // --- analysis -------------------------------------------------------------
    // With `reuse` off, everything is checked from scratch - the plain way,
    // kept to measure against; the answer is the same.
    std::vector<std::string> validate(bool reuse = true) const;

    std::set<Key> reachable() const;

    // Graphviz: states (optionally with their elements and internal arrows),
    // transitions, functors, and the portals that nest one state in another.
    std::string to_dot(bool with_internals = true) const;

private:
    friend class Engine;  // opens, closes and focuses embeddings

    Embedding* embedding_rw(Key name);

    static const std::vector<std::size_t>& none() {
        static const std::vector<std::size_t> empty;
        return empty;
    }

    void index_embedding(std::size_t i);

    // Everything reachable from the initial state: through a transition, a
    // host's portal (a guest is reached through it), or a seam (a doorway goes
    // both ways). Each interface is followed once, from an index of where it
    // leaves - not found by scanning all of them at every state reached.
    std::unordered_set<Key> reach() const;

    void insert(StatePtr s);

    void check_portal_functor(std::vector<std::string>& errors, const Embedding& e, Key fname,
                              Key want_from, Key want_to) const;

    std::map<Key, StatePtr> states_;  // ordered: deterministic dot output
    std::unordered_map<Key, State*> state_index_;  // the same states by name, found in constant time
    std::deque<Transition> transitions_;
    std::unordered_map<Key, std::vector<std::size_t>> by_trigger_;
    std::unordered_map<Key, std::size_t> transition_by_name_;
    std::map<Key, Functor> functors_;
    std::map<Key, std::vector<Key>> composites_;
    std::deque<Embedding> embeddings_;
    detail::Revision rev_;
    std::unordered_map<Key, State::Snapshot> defaults_;
    std::deque<Seam> seams_;
    std::deque<Drive> drives_;
    std::deque<Edit> edits_;
    std::vector<LensPair> lenses_;
    std::vector<std::pair<Key, Key>> ports_;
    std::vector<Key> kept_;  // functors whose targets follow their sources (keep)
    std::unordered_map<Key, std::vector<std::size_t>> by_host_;
    std::unordered_map<Key, std::vector<std::size_t>> by_guest_;
    std::unordered_map<Key, std::size_t> by_name_;
    Key initial_;

    // What validate() found last time, and on what: looked at again only when
    // that has changed. Derived, and disposable.
    struct StateCheck {
        bool valid = false;
        uint64_t structure = 0;
        std::vector<std::string> errors;
        // The triggers its arrows answer to, made when a drive first asks and
        // kept as long as the structure is what it was.
        bool triggers_made = false;
        std::unordered_set<Key> triggers;
    };
    struct FunctorCheck {
        bool valid = false;
        uint64_t functor = 0;
        const State* a = nullptr;
        const State* b = nullptr;
        uint64_t a_structure = 0, b_structure = 0;
        std::vector<std::string> errors;
    };
    mutable std::unordered_map<const State*, StateCheck> state_checks_;
    // Who is reachable follows from the interfaces alone, every one of which
    // moves the revision.
    mutable std::unordered_set<Key> reach_;
    mutable uint64_t reach_revision_ = ~uint64_t{0};
    mutable std::unordered_map<const Functor*, FunctorCheck> functor_checks_;
};

}  // namespace sg
