// Stategine - Engine: owns the state stack, the frame, and the open portals.
#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sg/core/StateGraph.hpp"
#include "sg/core/Temporal.hpp"

namespace sg {

// The engine runs the graph it is given: it enters states, routes events to
// their arrows, carries data through embeddings and transitions. It changes
// the world only by running what the graph declares. What it shows of the
// world is const - the state it is in, the graph, the focused guest - so a
// caller holding the engine watches the world and acts on it by firing
// events, never by reaching in; whoever built the graph still holds it, and
// with it the right to rewrite it.
class Engine {
public:
    explicit Engine(StateGraph& graph);

    const StateGraph& graph() const { return graph_; }

    // --- stack ---------------------------------------------------------------
    const State* current() const { return stack_.empty() ? nullptr : stack_.back(); }
    // The states on the stack, in the order they were entered.
    std::vector<const State*> stack() const { return {stack_.begin(), stack_.end()}; }
    bool running() const { return running_; }

    void start(Key id = Key{}, Params args = {});

    void stop() { running_ = false; }

    // --- events ---------------------------------------------------------------
    // Engine events drive transitions; state events drive morphisms.
    void fire(Event e);
    void fire(Key name) { fire(Event{name}); }

    // From the world outside, straight to a state, through a port the graph
    // declares (StateGraph::port): delivered at the start of the next frame,
    // and the state's arrows run on it then. An undeclared one is refused.
    void send(Key state, Event e);

    // The stack moved by hand. Within the world, what moves the stack is a
    // transition the graph declares, taken on an event: no state can call
    // these (a state sees its engine const). They are for whoever holds the
    // engine - the program that built the graph, with the same right it has
    // to rewrite the graph: a debug teleport, a menu outside the world.
    // (A state entered by hand hears it as one entered by a transition does:
    // `state.entered {from}`, `by` empty.)
    void switch_to(Key id, Params args = {});

    void push_state(Key id, Params args = {});

    void pop_state();

private:
    void arrived(Key from);
    // The top of the stack left, and what is under it resumed.
    void leave_top();

public:

    // --- embeddings -------------------------------------------------------------
    // Open a portal: run `in` to build the guest's view of the host, enter the
    // guest, and (if the embedding takes focus) route events to it.
    void open_embed(Key name, Params args = {});

    // Close it. commit=true runs `out`, writing the guest's edits into the host;
    // commit=false discards them (a cancelled interface).
    void close_embed(Key name, bool commit = true);

    // Run now what a frame runs for an embedding - a View's `in`, a Live
    // one's `out` - whatever its propagation: synchronisation asked for.
    void sync_embed(Key name);

    bool embed_open(Key name);

    // Give an open embedding focus, or take it away: input goes to its guest
    // (the innermost focused one) or back to whoever had it before.
    void focus_embed(Key name, bool on);

    // --- taking things away ------------------------------------------------------
    // Something taken out of the world, with what goes with it as each
    // relation touching it says (StateGraph::removal, Cleanup). Asked for now,
    // done at the start of the next frame, before the edits asked for: what
    // it takes that is open is closed through the engine first (discarding),
    // then everything goes as one change. One refused - a relation refuses,
    // or it would take a state the engine is in - takes nothing, and is
    // reported (on_problem).
    void remove(Removal r);
    // The same, done now, by whoever may rewrite the graph - an edit's own
    // apply, the code that builds the world: empty if done, else why not,
    // and nothing was done.
    std::string remove_now(const Removal& r);

    // --- the graph, watched ---------------------------------------------------------
    // States are distinct, and meet only through what the graph declares:
    // transitions, embeddings, functors and seams. After any tick in which
    // what the graph is made of changed, the engine checks it again
    // (StateGraph::validate - every state reachable through an interface,
    // every arrow's ends there, every functor lawful) - at most once a
    // `watch_interval` of seconds - and reports what is newly wrong: to
    // `on_problem` if set, else to stderr; with `strict`, it throws.
    std::function<void(const std::string&)> on_problem;
    void set_strict(bool on) { strict_ = on; }
    // Watch every hook a state has - on_update, on_event, on_render, and
    // on_enter, on_exit, on_pause, on_resume as the engine runs them: one that
    // changes the state's data, rather than emitting for an arrow to act on,
    // is behaviour outside any arrow, and is reported once, as a problem.
    void set_watch_updates(bool on) { watch_updates_ = on; }
    void set_watch_hooks(bool on) { watch_updates_ = on; }
    void set_watch_interval(double seconds) { watch_interval_ = seconds; }
    // How much of a frame the watch may take, in milliseconds. Checking the
    // graph again is a seam at a time (validate, then each seam's overlap), a
    // few at a frame, so a graph that moved in play costs the frame a slice
    // and not the whole check. 0 checks it all at once. A strict engine, and a
    // watch with no interval, always check all of it in the frame the graph
    // moved: what a test asks of the watch is the same frame.
    void set_watch_slice(double milliseconds) { watch_slice_ms_ = milliseconds; }
    const std::vector<std::string>& problems() const { return problems_; }
    // Check now, whatever changed: what is wrong, all of it.
    std::vector<std::string> check_graph();

    // The guest currently receiving events, if any: the innermost focused
    // embedding that is open, in a host that is itself live - the state the
    // engine is in, or open in one that is. (A game focused in a computer
    // nobody sits at hears nothing.)
    const State* focused() const;
    // How deep a state is open, from the state the engine is in (0), through
    // open embeddings: the fewest it is reached by; -1 if it is not live.
    // The embeddings may go round - a camera filming the room its screen
    // stands in, a computer in a game in a computer - so each state is
    // looked at once, however deep it lies: no depth is too deep to count,
    // and a cycle ends the search, not the world.
    int depth(Key id) const;
    bool live(Key id) const { return depth(id) >= 0; }

    // --- frame -------------------------------------------------------------------
    void tick(double dt);

    // Real-time loop. max_frames = 0 runs until stop() or the stack empties.
    void run(double target_fps = 60.0, uint64_t max_frames = 0);

    // Deterministic loop for tests and headless runs: fixed dt, no sleeping.
    void run_fixed(double dt, uint64_t frames);

    // Seconds of wall clock since the engine started: for pacing and for
    // watching, never for what the world means. The engine keeps no time of
    // its own: each state's time is its line on a clock, the sum of its own
    // steps (Temporal.hpp), so a fixed run is the same every run.
    double elapsed() const;

    uint64_t frame() const { return frame_; }
    void set_trace(bool on) { trace_ = on; }

private:
    using Clock = std::chrono::steady_clock;

    State* top() { return stack_.empty() ? nullptr : stack_.back(); }

    // The guest receiving events, forgetting embeddings closed since.
    State* focused_guest();

    // The focused embedding input goes to: open, in a live host, and the
    // deepest of those - a game focused in a focused computer before the
    // computer; of two as deep, the one focused last.
    const Embedding* innermost() const;

    void report(const std::string& p);

    // --- time -------------------------------------------------------------------------
    // A state about to step is driven: each clock that keeps its time moves on
    // by dt - through its own arrow, like any change to any state - and what it
    // then says is handed to the state's arrows. A line keeps one state's
    // time and moves only when that state steps, so its time is always the
    // sum of the steps the state has taken: a state set aside and come back to
    // finds no time missing, and no jump.
    void drive(State& s, double dt);

    // What keeps its time always and was not stepped with the active state
    // this frame steps now, once, in the order its drives were declared, with
    // what is open in it - and whatever it is shown in Live hears of what it
    // did, as it would had it stepped there.
    void keep_time(const Tick& t);

    // Each kept functor carries what changed in its source since it last did
    // (StateGraph::keep), at the start of the frame, once the edits asked
    // for are done - so a target that asked for something (a block let go
    // on a model) is answered before it follows its source again.
    void carry_kept();

    // Each embedding opened, closed and focused as its portal now says
    // (`open_key`, `focus_key`): what its host's arrows decided this frame,
    // or a host put back from a snapshot, done by the end of it. A following
    // embedding is looked at every frame; any other only when its portal's
    // params moved, and only for what the portal says. Found by index,
    // remade when the graph is rewired.
    void follow_portals();

    // A room glued to others is seen into from where it is seen: across each
    // seam of the state the engine is in, its travel carries the viewer's
    // eye to the far side, so the room beyond stands where it will when you
    // walk through - its sky, its ground, whatever it shows, aimed from
    // there. The first seam onto a room is the one it is seen through.
    void look_across(const State& here);
    // Whoever this frame's step took through one of `here`'s seams
    // (State::passed, from `before` the step) is taken through it now, by
    // the transition that carries the seam's own travel. The state they are
    // in after. (Only a step: a walker put somewhere is not walked there.)
    State* cross(State& here, const Params& before);

    // A state stepped on its own - the active one, or one that keeps time
    // always - is carried, Live, into whatever it is open in: a room into
    // the camera filming it, as a guest stepped inside its host would be.
    void carry_to_hosts(State& s);

    // Whether some open embedding shows it.
    bool shown(Key id) const;

    void index_drives();

    void step(State& s, const Tick& t);

    // A lifecycle hook, run as the engine runs it: watched, if hooks are,
    // and what the state says in it heard.
    template <typename F>
    void hook(State& s, const char* name, F&& run) {
        const bool had = s.wrote_in_update();
        s.watched(name, std::forward<F>(run), watch_updates_);
        noticed(s, had);
        heard_from(s);
    }

    void noticed(State& s, bool had);

    // What a state said outward (State::says) goes where the graph says it
    // goes, and nowhere else:
    //   - to an edit declared on it (graph.edit), applied next frame;
    //   - to the graph's transitions (next frame, with what was fired);
    //   - across each functor out of the state that names the event
    //     (Functor::on_event), relabelled, to the state it goes to - with the
    //     objects it maps, as a transition's functor carries them - whose
    //     arrows run on it at once. A functor is an interface: it maps
    //     objects, arrows and events. An embedding's functor carries only
    //     while the embedding is open (its `in` from the host as well as the
    //     subject), a transition's only when the transition is taken.
    // One nothing declares is not handed to any state: states meet only
    // through what the graph declares.
    void heard_from(State& s, int depth = 0);

    struct Carrier {
        const Embedding* e;  // the embedding it belongs to, if any: carries while open
        const Functor* f;
        Key to;
    };
    void index_carriers();

    void enter(State& s, const Params& args);

    // --- routes: the embeddings of a host, looked up once ------------------------
    // Which states and functors an embedding joins is resolved when first
    // needed and kept until the graph's revision moves; then it is thrown
    // away and found again. A route holds nothing of its own: pointers into
    // the graph, and the memo of what its functor last carried (see
    // Functor::Memo) - both derived, both disposable.
    struct Route {
        const Embedding* e = nullptr;
        State* guest = nullptr;
        State* subject = nullptr;
        const Functor* in = nullptr;
        const Functor* out = nullptr;
        Functor::Memo in_memo, out_memo;
        // Other places the same guest is shown Live, whose subjects hear of
        // what it did too.
        struct Also {
            const Embedding* e;
            State* subject;
            const Functor* out;
            Functor::Memo memo;
        };
        std::vector<Also> also;
    };

    std::vector<Route>& routes_of(State& host);

    // One direction of an embedding, run as its propagation says.
    void carry(const Functor& f, const State& src, State& dst, Functor::Memo& memo, const Embedding& e);

    void sync_live_out(State& host);

    // Guests of the active host tick after it, in the order they were
    // embedded: the frame's order is the graph's, never chance. Live embeddings write back every
    // frame, Commit ones wait for close_embed, and View ones are refreshed from
    // the host instead - nothing they do reaches back. "Every frame" is as the
    // embedding's propagation says: by default, whenever there is something
    // to carry.
    // Every open embedding, all the way down: a guest open in a guest (a
    // world on a tape in a deck in a room) has its moment as the one it is
    // open in does - each state once a frame, however many hold it.
    void step_embeddings(State& host, const Tick& t);
    void step_embeddings_in(State& host, const Tick& t);

    // The edits asked for since the last frame, applied now, in the order
    // asked, before anything else moves this frame: the one place the world
    // rewrites itself. Each answer is heard by the state that asked.
    void apply_edits();

    // What came in through the ports since the last frame.
    void take_inputs();

    // The removals asked for since the last frame (remove), in the order asked.
    void apply_removals();
    std::vector<Removal> removals_;

    void process_transitions();

    // A host's word about its own portal (State::says): `portal.open`,
    // `portal.close` {portal, commit} and `portal.focus` {portal, on} open,
    // close or focus whatever the graph embeds in that portal of the state
    // that said it - a computer opening its game's window.
    bool portal(const Event& ev);

    static Key embed_open_event() {
        static const Key k{"embed.open"};
        return k;
    }

    static Key embed_close_event() {
        static const Key k{"embed.close"};
        return k;
    }

    void take(const Transition& t, State& from, const Event& ev);

    StateGraph& graph_;
    std::vector<State*> stack_;
    std::vector<Event> pending_;
    std::vector<Event> said_;  // said by states (State::says), for the transitions
    std::unordered_map<Key, std::vector<Carrier>> carriers_;
    struct Asked {
        Key edit;
        Event event;
    };
    std::vector<Asked> asked_;  // edits said since the last frame
    std::vector<std::pair<Key, Event>> inputs_;  // sent in through ports
    uint64_t carriers_revision_ = ~uint64_t{0};
    std::vector<Event> inbox_;
    std::vector<Key> focus_;  // open, focused embeddings, innermost last
    std::unordered_set<const State*> stepped_;  // this frame's, so each has its moment once
    std::unordered_map<const State*, std::vector<Route>> routes_;
    uint64_t routes_revision_ = ~uint64_t{0};
    std::vector<Key> due_;  // OnEvent embeddings an event crossed this frame
    Clock::time_point clock_start_{};
    Clock::time_point last_{};
    uint64_t frame_ = 0;
    bool running_ = false;
    bool watch_updates_ = false;
    // Which drives move which state, found again when the interfaces change.
    std::unordered_map<Key, std::vector<const Drive*>> drive_index_;
    std::vector<const Drive*> always_;  // one per state that keeps its time always
    std::unordered_map<Key, Functor::Memo> kept_memos_;
    std::vector<Key> looked_;  // the rooms seen into this frame (look_across)
    // Every embedding, with where its portal is and the stamp of the portal's
    // params last looked at, so a frame reads only portals that moved
    // (follow_portals). Pointers hold while the host's structure does.
    struct Followed {
        Key name, open, focus;
        bool follows = false;
        State* host = nullptr;
        uint64_t structure = 0, stamp = 0;
        const Element* portal = nullptr;
    };
    std::vector<Followed> following_;
    bool restamp_ = false;  // the engine wrote portals since follow_portals last looked
    // Whether an embedding takes input, as its portal says, else as declared.
    bool takes_focus(const Embedding& e) const;
    std::unordered_map<Key, Functor::Memo> kept_carries_;  // what each kept functor last carried
    uint64_t follow_revision_ = 0;
    bool followed_ = false;
    uint64_t drive_revision_ = ~uint64_t{0};
    bool trace_ = false;
    bool strict_ = false;
    uint64_t watched_ = ~uint64_t{0};
    double watch_interval_ = 1.0, last_watch_ = -1e9;
    // The watch in slices (watch_slice): where a check of the graph has got
    // to - the revision it began at, whether its validate is done, the next
    // seam to look at, and what it has found.
    void watch_slice();
    double watch_slice_ms_ = 0.25;
    bool sweeping_ = false, swept_structure_ = false;
    uint64_t sweep_revision_ = 0;
    std::size_t sweep_seam_ = 0;
    std::vector<std::string> sweep_found_;
    std::vector<std::string> problems_;
};

}  // namespace sg
