// Stategine - Engine: owns the state stack, the frame, and the open portals.
#pragma once

#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "sg/core/StateGraph.hpp"

namespace sg {

class Engine {
public:
    explicit Engine(StateGraph& graph) : graph_(graph) {
        for (Key id : graph_.ids()) graph_.state(id).attach(this);
    }

    StateGraph& graph() { return graph_; }

    // --- stack ---------------------------------------------------------------
    State* current() { return stack_.empty() ? nullptr : stack_.back(); }
    const std::vector<State*>& stack() const { return stack_; }
    bool running() const { return running_; }

    void start(Key id = Key{}, Params args = {}) {
        const Key target = id.empty() ? graph_.initial() : id;
        if (target.empty()) throw std::runtime_error("engine: no initial state");
        stack_.clear();
        running_ = true;
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
        if (State* c = current()) {
            c->on_exit();
            stack_.pop_back();
        }
        enter(graph_.state(id), args);
    }

    void push_state(Key id, Params args = {}) {
        if (State* c = current()) c->on_pause();
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
        Embedding* e = graph_.embedding(name);
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
        Embedding* e = graph_.embedding(name);
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

    bool embed_open(Key name) {
        const Embedding* e = graph_.embedding(name);
        return e && e->open;
    }

    // The guest currently receiving events, if any.
    State* focused() {
        while (!focus_.empty()) {
            Embedding* e = graph_.embedding(focus_.back());
            if (e && e->open) return &graph_.state(e->guest);
            focus_.pop_back();
        }
        return nullptr;
    }

    // --- frame -------------------------------------------------------------------
    void tick(double dt) {
        if (!running_) return;
        const Tick t{dt, elapsed(), frame_++};
        process_transitions();
        if (!running_) return;
        if (State* c = current()) {
            // Edits made in an open Live guest since the last frame land in the
            // host before it updates, so the two never disagree within a frame.
            sync_live_out(*c);
            c->step(t);
            step_embeddings(*c, t);
        }
        if (stack_.empty()) running_ = false;
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

    void enter(State& s, const Params& args) {
        s.attach(this);
        stack_.push_back(&s);
        s.on_enter(args);
    }

    void sync_live_out(State& host) {
        for (Embedding* e : graph_.embeddings_of(host.id())) {
            if (!e->open || e->sync != EmbedSync::Live || e->out.empty()) continue;
            State& subject = graph_.state(e->subject.empty() ? e->host : e->subject);
            if (const Functor* f = graph_.functor(e->out)) f->apply(graph_.state(e->guest), subject);
        }
    }

    // Guests of the active host tick after it. Live embeddings write back every
    // frame, Commit ones wait for close_embed, and View ones are refreshed from
    // the host instead - nothing they do reaches back.
    void step_embeddings(State& host, const Tick& t) {
        for (Embedding* e : graph_.embeddings_of(host.id())) {
            if (!e->open) continue;
            State& guest = graph_.state(e->guest);
            State& subject = graph_.state(e->subject.empty() ? e->host : e->subject);
            if (e->sync == EmbedSync::View && !e->in.empty())
                if (const Functor* f = graph_.functor(e->in)) f->apply(subject, guest);
            guest.step(t);
            if (e->sync == EmbedSync::Live && !e->out.empty())
                if (const Functor* f = graph_.functor(e->out)) f->apply(guest, subject);
        }
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

            State* from = current();
            if (!from) return;

            const Transition* t = graph_.resolve(*from, ev);
            if (!t) {
                // Not a transition trigger: hand it to whoever holds focus.
                if (State* g = focused()) {
                    g->emit(ev);
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
    Clock::time_point clock_start_{};
    Clock::time_point last_{};
    uint64_t frame_ = 0;
    bool running_ = false;
    bool trace_ = false;
};

}  // namespace sg
