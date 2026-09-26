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
#include <vector>

#include "sg/core/StateGraph.hpp"

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
    // The states on the stack, bottom first.
    std::vector<const State*> stack() const { return {stack_.begin(), stack_.end()}; }
    std::size_t depth() const { return stack_.size(); }
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
    void fire(Event e) { pending_.push_back(std::move(e)); }
    void fire(Key name) { fire(Event{name}); }

    // Direct stack operations. Prefer transitions declared in the graph.
    void switch_to(Key id, Params args = {}) {
        if (State* c = top()) {
            c->on_exit();
            stack_.pop_back();
        }
        enter(graph_.state(id), args);
    }

    void push_state(Key id, Params args = {}) {
        if (State* c = top()) c->on_pause();
        enter(graph_.state(id), args);
    }

    void pop_state() {
        if (stack_.empty()) return;
        stack_.back()->on_exit();
        stack_.pop_back();
        if (stack_.empty()) {
            running_ = false;
            return;
        }
        stack_.back()->on_resume();
    }

    // --- embeddings -------------------------------------------------------------
    // Open a portal: run `in` to build the guest's view of the host, enter the
    // guest, and (if the embedding takes focus) route events to it.
    void open_embed(Key name, Params args = {}) {
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
        guest.on_enter(args);
        if (e->focus) focus_.push_back(e->name);
        if (trace_)
            std::cout << "[sg] open  " << e->host.str() << "." << e->portal.str() << " <= "
                      << e->guest.str() << "\n";
    }

    // Close it. commit=true runs `out`, writing the guest's edits into the host;
    // commit=false discards them (a cancelled interface).
    void close_embed(Key name, bool commit = true) {
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
        guest.on_exit();
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
    void set_watch_interval(double seconds) { watch_interval_ = seconds; }
    const std::vector<std::string>& problems() const { return problems_; }
    // Check now, whatever changed: what is wrong, all of it.
    std::vector<std::string> check_graph() {
        watched_ = graph_.revision();
        last_watch_ = elapsed();
        std::vector<std::string> now = graph_.validate();
        for (const std::string& p : now) {
            if (std::find(problems_.begin(), problems_.end(), p) != problems_.end()) continue;
            problems_.push_back(p);
            if (on_problem) on_problem(p);
            else std::cerr << "[sg] " << p << "\n";
        }
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
        const Tick t{dt, elapsed(), frame_++};
        process_transitions();
        if (!running_) return;
        if (State* c = top()) {
            // Edits made in an open Live guest since the last frame land in the
            // host before it updates, so the two never disagree within a frame.
            sync_live_out(*c);
            c->step(t);
            step_embeddings(*c, t);
        }
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

    double elapsed() const {
        return std::chrono::duration<double>(Clock::now() - clock_start_).count();
    }

    uint64_t frame() const { return frame_; }
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

    void enter(State& s, const Params& args) {
        s.attach(this);
        stack_.push_back(&s);
        s.on_enter(args);
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

    // Guests of the active host tick after it. Live embeddings write back every
    // frame, Commit ones wait for close_embed, and View ones are refreshed from
    // the host instead - nothing they do reaches back. "Every frame" is as the
    // embedding's propagation says: by default, whenever there is something
    // to carry.
    void step_embeddings(State& host, const Tick& t) {
        std::vector<Route>& rs = routes_of(host);
        for (Route& r : rs) {
            if (!r.e->open) continue;
            if (r.e->sync == EmbedSync::View && r.in) carry(*r.in, *r.subject, *r.guest, r.in_memo, *r.e);
            r.guest->step(t);
            if (r.e->sync == EmbedSync::Live && r.out) {
                carry(*r.out, *r.guest, *r.subject, r.out_memo, *r.e);
                // One guest open in several places - a door hanging in a
                // doorway both rooms embed - is one state: what it did this
                // frame reaches every place it is shown, not only here.
                for (Route::Also& o : r.also)
                    if (o.e->open) carry(*o.out, *r.guest, *o.subject, o.memo, *o.e);
            }
        }
        due_.clear();
    }

    void process_transitions() {
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
                    g->emit(ev);
                    due_.push_back(focus_.back());  // it crossed that embedding
                } else {
                    from->emit(ev);
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
        if (t.action) t.action(from, ev, args);

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

        // Functorial transport, before the target is entered, so on_enter
        // already sees the carried elements.
        if (!t.functor.empty() && target) {
            const Functor* f = graph_.functor(t.functor);
            if (!f)
                throw std::runtime_error("transition " + t.name.str() + ": no functor " +
                                         t.functor.str());
            carry_.clear();
            carry_.push_back(ev);
            f->apply(from, *target, carry_);
        }

        switch (t.kind) {
            case TransitionKind::Switch:
                from.on_exit();
                stack_.pop_back();
                enter(*target, args);
                break;
            case TransitionKind::Push:
                from.on_pause();
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
    std::vector<Event> inbox_;
    std::vector<Event> carry_;
    std::vector<Key> focus_;  // open, focused embeddings, innermost last
    std::unordered_map<const State*, std::vector<Route>> routes_;
    uint64_t routes_revision_ = ~uint64_t{0};
    std::vector<Key> due_;  // OnEvent embeddings an event crossed this frame
    Clock::time_point clock_start_{};
    Clock::time_point last_{};
    uint64_t frame_ = 0;
    bool running_ = false;
    bool trace_ = false;
    bool strict_ = false;
    uint64_t watched_ = ~uint64_t{0};
    double watch_interval_ = 1.0, last_watch_ = -1e9;
    std::vector<std::string> problems_;
};

}  // namespace sg
