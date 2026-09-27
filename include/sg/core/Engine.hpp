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
    explicit Engine(StateGraph& graph) : graph_(graph) {
        for (Key id : graph_.ids()) graph_.state(id).attach(this);
    }

    const StateGraph& graph() const { return graph_; }

    // --- stack ---------------------------------------------------------------
    const State* current() const { return stack_.empty() ? nullptr : stack_.back(); }
    // The states on the stack, in the order they were entered.
    std::vector<const State*> stack() const { return {stack_.begin(), stack_.end()}; }
    bool running() const { return running_; }

    void start(Key id = Key{}, Params args = {}) {
        const Key target = id.empty() ? graph_.initial() : id;
        if (target.empty()) throw std::runtime_error("engine: no initial state");
        stack_.clear();
        running_ = true;
        graph_.keep_defaults();  // how everything starts, to go back to
        clock_start_ = Clock::now();
        last_ = clock_start_;
        enter(graph_.state(target), args);
    }

    void stop() { running_ = false; }

    // --- events ---------------------------------------------------------------
    // Engine events drive transitions; state events drive morphisms.
    void fire(Event e) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("fired the engine: ") + e.name.str());
        pending_.push_back(std::move(e));
    }
    void fire(Key name) { fire(Event{name}); }

    // From the world outside, straight to a state, through a port the graph
    // declares (StateGraph::port): delivered at the start of the next frame,
    // and the state's arrows run on it then. An undeclared one is refused.
    void send(Key state, Event e) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("sent an event to a state: ") + e.name.str() + " to " + state.str());
        if (!graph_.has_port(state, e.name))
            throw std::runtime_error("no port " + e.name.str() + " on " + state.str() + " - declare it (StateGraph::port)");
        inputs_.push_back({state, std::move(e)});
    }

    // The stack moved by hand. Within the world, what moves the stack is a
    // transition the graph declares, taken on an event: no state can call
    // these (a state sees its engine const). They are for whoever holds the
    // engine - the program that built the graph, with the same right it has
    // to rewrite the graph: a debug teleport, a menu outside the world.
    void switch_to(Key id, Params args = {}) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("moved the stack: ") + "switch_to " + id.str());
        if (State* c = top()) {
            hook(*c, "on_exit", [&] { c->on_exit(); });
            stack_.pop_back();
        }
        enter(graph_.state(id), args);
    }

    void push_state(Key id, Params args = {}) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("moved the stack: ") + "push_state " + id.str());
        if (State* c = top()) hook(*c, "on_pause", [&] { c->on_pause(); });
        enter(graph_.state(id), args);
    }

    void pop_state() {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("moved the stack: ") + "pop_state");
        if (stack_.empty()) return;
        State* c = stack_.back();
        hook(*c, "on_exit", [&] { c->on_exit(); });
        stack_.pop_back();
        if (stack_.empty()) {
            running_ = false;
            return;
        }
        State* under = stack_.back();
        hook(*under, "on_resume", [&] { under->on_resume(); });
    }

    // --- embeddings -------------------------------------------------------------
    // Open a portal: run `in` to build the guest's view of the host, enter the
    // guest, and (if the embedding takes focus) route events to it.
    void open_embed(Key name, Params args = {}) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("opened an embedding: ") + name.str());
        Embedding* e = graph_.embedding_rw(name);
        if (!e) throw std::runtime_error("no embedding " + name.str());
        if (e->open) return;
        State& host = graph_.state(e->host);
        State& guest = graph_.state(e->guest);
        State& subject = graph_.state(e->subject.empty() ? e->host : e->subject);
        guest.attach(this);
        if (!e->in.empty()) {
            const Functor* f = graph_.functor(e->in);
            if (!f) throw std::runtime_error("embedding " + name.str() + ": no functor " +
                                             e->in.str());
            f->apply(subject, guest);
        }
        if (Element* portal = host.find(e->portal)) portal->params.set(keys::open, true);
        e->open = true;
        hook(guest, "on_enter", [&] { guest.on_enter(args); });
        if (e->focus) focus_.push_back(e->name);
        if (trace_)
            std::cout << "[sg] open  " << e->host.str() << "." << e->portal.str() << " <= "
                      << e->guest.str() << "\n";
    }

    // Close it. commit=true runs `out`, writing the guest's edits into the host;
    // commit=false discards them (a cancelled interface).
    void close_embed(Key name, bool commit = true) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("closed an embedding: ") + name.str());
        Embedding* e = graph_.embedding_rw(name);
        if (!e || !e->open) return;
        State& host = graph_.state(e->host);
        State& guest = graph_.state(e->guest);
        if (commit && !e->out.empty()) {
            const Functor* f = graph_.functor(e->out);
            if (!f) throw std::runtime_error("embedding " + name.str() + ": no functor " +
                                             e->out.str());
            f->apply(guest, graph_.state(e->subject.empty() ? e->host : e->subject));
        }
        hook(guest, "on_exit", [&] { guest.on_exit(); });
        e->open = false;
        if (Element* portal = host.find(e->portal)) portal->params.set(keys::open, false);
        focus_.erase(std::remove(focus_.begin(), focus_.end(), e->name), focus_.end());
        if (trace_)
            std::cout << "[sg] close " << e->host.str() << "." << e->portal.str()
                      << (commit ? " (commit)" : " (discard)") << "\n";
    }

    // Run now what a frame runs for an embedding - a View's `in`, a Live
    // one's `out` - whatever its propagation: synchronisation asked for.
    void sync_embed(Key name) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("synchronised an embedding: ") + name.str());
        const Embedding* e = graph_.embedding(name);
        if (!e || !e->open) return;
        State* guest = graph_.find(e->guest);
        State* subject = graph_.find(e->subject.empty() ? e->host : e->subject);
        if (!guest || !subject) return;
        if (e->sync == EmbedSync::View && !e->in.empty()) {
            if (const Functor* f = graph_.functor(e->in)) f->apply(*subject, *guest);
        } else if (e->sync == EmbedSync::Live && !e->out.empty()) {
            if (const Functor* f = graph_.functor(e->out)) f->apply(*guest, *subject);
        }
    }

    bool embed_open(Key name) {
        const Embedding* e = graph_.embedding(name);
        return e && e->open;
    }

    // Give an open embedding focus, or take it away: input goes to its guest
    // (the innermost focused one) or back to whoever had it before.
    void focus_embed(Key name, bool on) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("moved focus: ") + name.str());
        Embedding* e = graph_.embedding_rw(name);
        if (!e) return;
        e->focus = on;
        focus_.erase(std::remove(focus_.begin(), focus_.end(), name), focus_.end());
        if (on && e->open) focus_.push_back(name);
    }

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
    const std::vector<std::string>& problems() const { return problems_; }
    // Check now, whatever changed: what is wrong, all of it.
    std::vector<std::string> check_graph() {
        watched_ = graph_.revision();
        last_watch_ = elapsed();
        std::vector<std::string> now = graph_.validate();
        for (const std::string& p : now) report(p);
        if (strict_ && !now.empty()) throw std::runtime_error("stategine: the graph broke: " + now.front());
        return now;
    }

    // The guest currently receiving events, if any.
    const State* focused() const {
        for (auto it = focus_.rbegin(); it != focus_.rend(); ++it) {
            const Embedding* e = graph_.embedding(*it);
            if (e && e->open) return graph_.find(e->guest);
        }
        return nullptr;
    }

    // --- frame -------------------------------------------------------------------
    void tick(double dt) {
        if (!running_) return;
        time_ += dt;
        const Tick t{dt, time_, frame_++};
        apply_edits();
        take_inputs();
        process_transitions();
        if (!running_) return;
        if (State* c = top()) {
            // Edits made in an open Live guest since the last frame land in the
            // host before it updates, so the two never disagree within a frame.
            sync_live_out(*c);
            drive(*c, dt);
            step(*c, t);
            step_embeddings(*c, t);
        }
        keep_time(t);
        stepped_.clear();
        if (stack_.empty()) running_ = false;
        if (graph_.revision() != watched_ && elapsed() - last_watch_ >= watch_interval_) check_graph();
    }

    // Real-time loop. max_frames = 0 runs until stop() or the stack empties.
    void run(double target_fps = 60.0, uint64_t max_frames = 0) {
        if (!running_) start();
        const auto budget = std::chrono::duration<double>(target_fps > 0 ? 1.0 / target_fps : 0.0);
        while (running_ && (max_frames == 0 || frame_ < max_frames)) {
            const auto now = Clock::now();
            const double dt = std::chrono::duration<double>(now - last_).count();
            last_ = now;
            tick(dt);
            if (target_fps > 0) {
                const auto spent = Clock::now() - now;
                if (spent < budget) std::this_thread::sleep_for(budget - spent);
            }
        }
    }

    // Deterministic loop for tests and headless runs: fixed dt, no sleeping.
    void run_fixed(double dt, uint64_t frames) {
        if (!running_) start();
        for (uint64_t i = 0; i < frames && running_; ++i) tick(dt);
    }

    // Seconds of wall clock since the engine started: for pacing and for
    // watching, never for what the world means - a Tick's time is the sum of
    // the steps taken (simulated_time), so a fixed run is the same every run.
    double elapsed() const {
        return std::chrono::duration<double>(Clock::now() - clock_start_).count();
    }

    uint64_t frame() const { return frame_; }
    double simulated_time() const { return time_; }
    void set_trace(bool on) { trace_ = on; }

private:
    using Clock = std::chrono::steady_clock;

    State* top() { return stack_.empty() ? nullptr : stack_.back(); }

    // The guest receiving events, forgetting embeddings closed since.
    State* focused_guest() {
        while (!focus_.empty()) {
            const Embedding* e = graph_.embedding(focus_.back());
            if (e && e->open) return graph_.find(e->guest);
            focus_.pop_back();
        }
        return nullptr;
    }

    void report(const std::string& p) {
        if (std::find(problems_.begin(), problems_.end(), p) != problems_.end()) return;
        problems_.push_back(p);
        if (on_problem) on_problem(p);
        else std::cerr << "[sg] " << p << "\n";
    }

    // --- time -------------------------------------------------------------------------
    // A state about to step is driven: each clock that keeps its time moves on
    // by dt - through its own arrow, like any change to any state - and what it
    // then says is handed to the state's arrows. A line keeps one state's
    // time and moves only when that state steps, so its time is always the
    // sum of the steps the state has taken: a state set aside and come back to
    // finds no time missing, and no jump.
    void drive(State& s, double dt) {
        if (drive_revision_ != graph_.topology()) index_drives();
        auto it = drive_index_.find(s.id());
        if (it == drive_index_.end()) return;
        for (const Drive* dp : it->second) {
            const Drive& d = *dp;
            State* c = graph_.find(d.clock);
            const Key line = timeline_of(d);
            if (!c || !c->find(line)) continue;  // validate() names it
            c->hear(Event{Temporal::advance_event(line), Params{}.set(keys::dt, dt)});
            c->dispatch_pending();
            s.hear(drive_event(d, *c, dt));
        }
    }

    // What keeps its time always and was not stepped with the active state
    // this frame steps now, once, in the order its drives were declared, with
    // what is open in it - and whatever it is shown in Live hears of what it
    // did, as it would had it stepped there.
    void keep_time(const Tick& t) {
        if (drive_revision_ != graph_.topology()) index_drives();
        for (const Drive* d : always_) {
            State* s = graph_.find(d->state);
            if (!s || !stepped_.insert(s).second) continue;
            drive(*s, t.dt);
            step(*s, t);
            step_embeddings_in(*s, t);  // and what is open in it, as anywhere
            for (std::size_t j : graph_.embeddings_holding(s->id())) {
                const Embedding& e = graph_.embeddings()[j];
                if (!e.open || e.sync != EmbedSync::Live || e.out.empty()) continue;
                const Functor* f = graph_.functor(e.out);
                State* subject = graph_.find(e.subject.empty() ? e.host : e.subject);
                if (f && subject) carry(*f, *s, *subject, kept_memos_[e.name], e);
            }
        }
    }

    void index_drives() {
        drive_index_.clear();
        always_.clear();
        for (const Drive& d : graph_.drives()) {
            drive_index_[d.state].push_back(&d);
            if (d.keeps == Keeps::Always &&
                std::none_of(always_.begin(), always_.end(), [&](const Drive* o) { return o->state == d.state; }))
                always_.push_back(&d);
        }
        kept_memos_.clear();
        drive_revision_ = graph_.topology();
    }

    void step(State& s, const Tick& t) {
        const bool had = s.wrote_in_update();
        s.step(t, watch_updates_);
        noticed(s, had);
        heard_from(s);
    }

    // A lifecycle hook, run as the engine runs it: watched, if hooks are,
    // and what the state says in it heard.
    template <typename F>
    void hook(State& s, const char* name, F&& run) {
        const bool had = s.wrote_in_update();
        s.watched(name, std::forward<F>(run), watch_updates_);
        noticed(s, had);
        heard_from(s);
    }

    void noticed(State& s, bool had) {
        if (had || !s.wrote_in_update()) return;
        const std::string p = "state " + s.id().str() + " changes its data in " + s.wrote_in() +
                              ", outside any arrow: " +
                              (s.wrote_in() == "on_update" ? "drive it from a clock (graph.drive)"
                                                           : "let an arrow on an event do it");
        report(p);
        if (strict_) throw std::runtime_error("stategine: " + p);
    }

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
    void heard_from(State& s, int depth = 0) {
        if (s.said_out().empty()) return;
        std::vector<Event> said = s.take_said();
        if (!graph_.edits().empty())
            for (const Event& e : said)
                for (const Edit& ed : graph_.edits())
                    if (ed.state == s.id() && ed.event == e.name) asked_.push_back({ed.name, e});
        if (carriers_revision_ != graph_.topology()) index_carriers();
        auto it = carriers_.find(s.id());
        if (it != carriers_.end() && depth < 16) {
            const std::vector<Carrier> by = it->second;  // a state reached may rewire nothing, but be safe
            for (const Event& e : said)
                for (const Carrier& c : by) {
                    if ((c.e && !c.e->open) || !c.f->maps_event(e.name)) continue;
                    State* to = graph_.find(c.to);
                    if (!to) continue;
                    // A functor carries its objects with the event, as a
                    // transition's does - what moved, and that it moved,
                    // arrive together. (An embedding's `in` carrying what its
                    // host says, not its subject, has nothing of the host's
                    // to carry: only the event goes.)
                    if (c.f->from() == s.id()) c.f->apply(s, *to, e);
                    else to->hear(c.f->carried(e, s));
                    to->dispatch_pending();
                    heard_from(*to, depth + 1);
                }
        }
        for (Event& e : said) said_.push_back(std::move(e));
    }

    struct Carrier {
        const Embedding* e;  // the embedding it belongs to, if any: carries while open
        const Functor* f;
        Key to;
    };
    void index_carriers() {
        carriers_.clear();
        std::unordered_set<Key> taken;  // a transition's functor, or an embedding's
        for (const Transition& t : graph_.transitions())
            if (!t.functor.empty()) taken.insert(t.functor);
        for (const Embedding& e : graph_.embeddings()) {
            const Key subject = e.subject.empty() ? e.host : e.subject;
            const Functor* in = e.in.empty() ? nullptr : graph_.functor(e.in);
            const Functor* out = e.out.empty() ? nullptr : graph_.functor(e.out);
            if (in) taken.insert(e.in);
            if (out) taken.insert(e.out);
            if (in && in->maps_events()) {
                carriers_[e.host].push_back({&e, in, e.guest});
                if (subject != e.host) carriers_[subject].push_back({&e, in, e.guest});
            }
            if (out && out->maps_events()) carriers_[e.guest].push_back({&e, out, subject});
        }
        for (const auto& kv : graph_.functors()) {
            const Functor& f = kv.second;
            if (!f.maps_events() || taken.count(kv.first)) continue;
            carriers_[f.from()].push_back({nullptr, &f, f.to()});
        }
        carriers_revision_ = graph_.topology();
    }

    void enter(State& s, const Params& args) {
        s.attach(this);
        stack_.push_back(&s);
        hook(s, "on_enter", [&] { s.on_enter(args); });
    }

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

    std::vector<Route>& routes_of(State& host) {
        if (routes_revision_ != graph_.topology()) {
            routes_.clear();
            routes_revision_ = graph_.topology();
        }
        auto it = routes_.find(&host);
        if (it != routes_.end()) return it->second;
        std::vector<Route>& rs = routes_[&host];
        const auto& all = graph_.embeddings();
        for (std::size_t i : graph_.embeddings_hosted_by(host.id())) {
            const Embedding& e = all[i];
            Route r;
            r.e = &e;
            r.guest = graph_.find(e.guest);
            r.subject = graph_.find(e.subject.empty() ? e.host : e.subject);
            if (!r.guest || !r.subject) continue;  // validate() names it
            r.in = e.in.empty() ? nullptr : graph_.functor(e.in);
            r.out = e.out.empty() ? nullptr : graph_.functor(e.out);
            if (e.sync == EmbedSync::Live && r.out)
                for (std::size_t j : graph_.embeddings_holding(e.guest)) {
                    const Embedding& o = all[j];
                    if (&o == &e || o.sync != EmbedSync::Live || o.out.empty()) continue;
                    State* s = graph_.find(o.subject.empty() ? o.host : o.subject);
                    const Functor* f = graph_.functor(o.out);
                    if (s && f) r.also.push_back({&o, s, f, {}});
                }
            rs.push_back(std::move(r));
        }
        return rs;
    }

    // One direction of an embedding, run as its propagation says.
    void carry(const Functor& f, const State& src, State& dst, Functor::Memo& memo, const Embedding& e) {
        switch (e.propagate) {
            case Propagation::OnChange:
                f.apply(src, dst, memo);
                break;
            case Propagation::Continuous:
                f.apply(src, dst);
                break;
            case Propagation::OnEvent:
                if (std::find(due_.begin(), due_.end(), e.name) != due_.end()) f.apply(src, dst);
                break;
            case Propagation::Manual:
                break;
        }
    }

    void sync_live_out(State& host) {
        for (Route& r : routes_of(host)) {
            if (!r.e->open || r.e->sync != EmbedSync::Live || !r.out) continue;
            carry(*r.out, *r.guest, *r.subject, r.out_memo, *r.e);
        }
    }

    // Guests of the active host tick after it, in the order they were
    // embedded: the frame's order is the graph's, never chance. Live embeddings write back every
    // frame, Commit ones wait for close_embed, and View ones are refreshed from
    // the host instead - nothing they do reaches back. "Every frame" is as the
    // embedding's propagation says: by default, whenever there is something
    // to carry.
    // Every open embedding, all the way down: a guest open in a guest (a
    // world on a tape in a deck in a room) has its moment as the one it is
    // open in does - each state once a frame, however many hold it.
    void step_embeddings(State& host, const Tick& t) {
        stepped_.clear();
        stepped_.insert(&host);
        step_embeddings_in(host, t);
        due_.clear();
    }
    void step_embeddings_in(State& host, const Tick& t) {
        // Each route is found again, by its embedding's name, before and
        // after its guest's step: a step may rewrite the graph (a door glued
        // as someone goes through it), and the routes are then made anew -
        // never read from the old ones. Those it opens are stepped after.
        std::vector<Key> names;
        for (const Route& r : routes_of(host)) names.push_back(r.e->name);
        const auto route = [&](Key name) -> Route* {
            for (Route& r : routes_of(host))
                if (r.e->name == name) return &r;
            return nullptr;
        };
        std::vector<State*> inner;
        for (Key name : names) {
            Route* r = route(name);
            if (!r || !r->e->open) continue;
            if (r->e->sync == EmbedSync::View && r->in) carry(*r->in, *r->subject, *r->guest, r->in_memo, *r->e);
            State* guest = r->guest;
            const bool first = stepped_.insert(guest).second;
            if (first) {
                drive(*guest, t.dt);
                step(*guest, t);
            }
            r = route(name);
            if (!r || !r->e->open) continue;
            if (r->e->sync == EmbedSync::Live && r->out) {
                carry(*r->out, *r->guest, *r->subject, r->out_memo, *r->e);
                // One guest open in several places - a door hanging in a
                // doorway both rooms embed - is one state: what it did this
                // frame reaches every place it is shown, not only here.
                for (Route::Also& o : r->also)
                    if (o.e->open) carry(*o.out, *r->guest, *o.subject, o.memo, *o.e);
            }
            if (first) inner.push_back(guest);
        }
        for (State* g : inner) step_embeddings_in(*g, t);
    }

    // The edits asked for since the last frame, applied now, in the order
    // asked, before anything else moves this frame: the one place the world
    // rewrites itself. Each answer is heard by the state that asked.
    void apply_edits() {
        if (asked_.empty()) return;
        std::vector<Asked> asked;
        asked.swap(asked_);
        for (const Asked& a : asked) {
            const Edit* ed = nullptr;
            for (const Edit& e : graph_.edits())
                if (e.name == a.edit) ed = &e;
            if (!ed || !ed->apply) continue;  // dropped since it was asked
            const Key who = ed->state, reply = ed->reply;
            Params answer = ed->apply(graph_, a.event);  // may rewrite the graph, and `ed` with it
            if (State* s = graph_.find(who)) s->hear(Event{reply, std::move(answer)});
        }
    }

    // What came in through the ports since the last frame.
    void take_inputs() {
        if (inputs_.empty()) return;
        std::vector<std::pair<Key, Event>> in;
        in.swap(inputs_);
        for (auto& kv : in)
            if (State* s = graph_.find(kv.first)) {
                s->hear(std::move(kv.second));
                s->dispatch_pending();
                heard_from(*s);
            }
    }

    void process_transitions() {
        // What states said since the last frame: to transitions only.
        if (!said_.empty()) {
            std::vector<Event> said;
            said.swap(said_);
            for (const Event& ev : said) {
                State* from = top();
                if (!from) return;
                if (const Transition* t = graph_.resolve(*from, ev)) take(*t, *from, ev);
                if (!running_) return;
            }
        }
        inbox_.clear();
        inbox_.swap(pending_);
        for (const Event& ev : inbox_) {
            // Portal control events, usable from anywhere:
            //   embed.open {name, ...}    embed.close {name, commit=true}
            if (ev.name == embed_open_event()) {
                open_embed(Key{ev.args.get_or<std::string>(keys::name, "")}, ev.args);
                continue;
            }
            if (ev.name == embed_close_event()) {
                close_embed(Key{ev.args.get_or<std::string>(keys::name, "")},
                            ev.args.get_or<bool>(keys::commit, true));
                continue;
            }

            State* from = top();
            if (!from) return;

            const Transition* t = graph_.resolve(*from, ev);
            if (!t) {
                // Not a transition trigger: hand it to whoever holds focus.
                if (State* g = focused_guest()) {
                    g->hear(ev);
                    due_.push_back(focus_.back());  // it crossed that embedding
                } else {
                    from->hear(ev);
                }
                continue;
            }
            take(*t, *from, ev);
            if (!running_) return;
        }
    }

    static Key embed_open_event() {
        static const Key k{"embed.open"};
        return k;
    }

    static Key embed_close_event() {
        static const Key k{"embed.close"};
        return k;
    }

    void take(const Transition& t, State& from, const Event& ev) {
        Params args;

        if (trace_)
            std::cout << "[sg] " << from.id().str() << " --" << ev.name.str() << "--> "
                      << (t.kind == TransitionKind::Pop ? std::string("<pop>") : t.to.str())
                      << (t.functor.empty() ? "" : "  via " + t.functor.str()) << "\n";

        State* target = nullptr;
        if (t.kind == TransitionKind::Pop) {
            if (stack_.size() >= 2) target = stack_[stack_.size() - 2];
        } else {
            target = graph_.find(t.to);
            if (!target)
                throw std::runtime_error("transition " + t.name.str() + ": no state " + t.to.str());
        }

        // What it carries, before the target is entered, so on_enter already
        // sees it (StateGraph::cross - the laws take it the same way).
        graph_.cross(t, from, target, ev, args);

        switch (t.kind) {
            case TransitionKind::Switch:
                hook(from, "on_exit", [&] { from.on_exit(); });
                stack_.pop_back();
                enter(*target, args);
                break;
            case TransitionKind::Push:
                hook(from, "on_pause", [&] { from.on_pause(); });
                enter(*target, args);
                break;
            case TransitionKind::Pop:
                pop_state();
                break;
        }
    }

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
    double time_ = 0.0;  // simulated: the sum of every dt ticked
    bool running_ = false;
    bool watch_updates_ = false;
    // Which drives move which state, found again when the interfaces change.
    std::unordered_map<Key, std::vector<const Drive*>> drive_index_;
    std::vector<const Drive*> always_;  // one per state that keeps its time always
    std::unordered_map<Key, Functor::Memo> kept_memos_;
    uint64_t drive_revision_ = ~uint64_t{0};
    bool trace_ = false;
    bool strict_ = false;
    uint64_t watched_ = ~uint64_t{0};
    double watch_interval_ = 1.0, last_watch_ = -1e9;
    std::vector<std::string> problems_;
};

}  // namespace sg
