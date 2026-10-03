#include "sg/core/Engine.hpp"

namespace sg {

Engine::Engine(StateGraph& graph) : graph_(graph) {
    for (Key id : graph_.ids()) graph_.state(id).attach(this);
}

void Engine::start(Key id, Params args) {
    const Key target = id.empty() ? graph_.initial() : id;
    if (target.empty()) throw std::runtime_error("engine: no initial state");
    stack_.clear();
    running_ = true;
    graph_.keep_defaults();  // how everything starts, to go back to
    clock_start_ = Clock::now();
    last_ = clock_start_;
    enter(graph_.state(target), args);
    follow_portals();
}

void Engine::fire(Event e) {
    if (detail::observing() > 0) detail::refused_to_observer(std::string("fired the engine: ") + e.name.str());
    pending_.push_back(std::move(e));
}

void Engine::send(Key state, Event e) {
    if (detail::observing() > 0) detail::refused_to_observer(std::string("sent an event to a state: ") + e.name.str() + " to " + state.str());
    if (!graph_.has_port(state, e.name))
        throw std::runtime_error("no port " + e.name.str() + " on " + state.str() + " - declare it (StateGraph::port)");
    inputs_.push_back({state, std::move(e)});
}

void Engine::switch_to(Key id, Params args) {
    if (detail::observing() > 0) detail::refused_to_observer(std::string("moved the stack: ") + "switch_to " + id.str());
    const Key from = top() ? top()->id() : Key{};
    if (State* c = top()) {
        hook(*c, "on_exit", [&] { c->on_exit(); });
        stack_.pop_back();
    }
    enter(graph_.state(id), args);
    arrived(from);
}

void Engine::push_state(Key id, Params args) {
    if (detail::observing() > 0) detail::refused_to_observer(std::string("moved the stack: ") + "push_state " + id.str());
    const Key from = top() ? top()->id() : Key{};
    if (State* c = top()) hook(*c, "on_pause", [&] { c->on_pause(); });
    enter(graph_.state(id), args);
    arrived(from);
}

void Engine::pop_state() {
    if (detail::observing() > 0) detail::refused_to_observer(std::string("moved the stack: ") + "pop_state");
    const Key from = top() ? top()->id() : Key{};
    leave_top();
    arrived(from);
}

void Engine::arrived(Key from) {
    if (State* now = top(); now && !from.empty())
        now->hear(Event{StateGraph::entered_event(), Params{}.set("from", from.str()).set("by", std::string())});
}

void Engine::leave_top() {
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

void Engine::open_embed(Key name, Params args) {
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
    // The portal says it is open - unless it is the portal that decides
    // (a following embedding), and has.
    if (Element* portal = host.find(e->portal); portal && !e->follows) portal->params.set(keys::open, true);
    e->open = true;
    hook(guest, "on_enter", [&] { guest.on_enter(args); });
    if (e->focus) focus_.push_back(e->name);
    if (trace_)
        std::cout << "[sg] open  " << e->host.str() << "." << e->portal.str() << " <= "
                  << e->guest.str() << "\n";
}

void Engine::close_embed(Key name, bool commit) {
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
    if (Element* portal = host.find(e->portal); portal && !e->follows) portal->params.set(keys::open, false);
    focus_.erase(std::remove(focus_.begin(), focus_.end(), e->name), focus_.end());
    if (trace_)
        std::cout << "[sg] close " << e->host.str() << "." << e->portal.str()
                  << (commit ? " (commit)" : " (discard)") << "\n";
}

void Engine::sync_embed(Key name) {
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

bool Engine::embed_open(Key name) {
    const Embedding* e = graph_.embedding(name);
    return e && e->open;
}

void Engine::focus_embed(Key name, bool on) {
    if (detail::observing() > 0) detail::refused_to_observer(std::string("moved focus: ") + name.str());
    Embedding* e = graph_.embedding_rw(name);
    if (!e) return;
    const bool listed = std::find(focus_.begin(), focus_.end(), name) != focus_.end();
    if (e->focus == on && listed == (on && e->open)) return;  // as it is already: nothing moves
    e->focus = on;
    focus_.erase(std::remove(focus_.begin(), focus_.end(), name), focus_.end());
    if (on && e->open) focus_.push_back(name);
}

std::vector<std::string> Engine::check_graph() {
    watched_ = graph_.revision();
    last_watch_ = elapsed();
    std::vector<std::string> now = graph_.validate();
    for (const std::string& p : now) report(p);
    if (strict_ && !now.empty()) throw std::runtime_error("stategine: the graph broke: " + now.front());
    return now;
}

const State* Engine::focused() const {
    const Embedding* e = innermost();
    return e ? graph_.find(e->guest) : nullptr;
}

int Engine::depth(Key id) const {
    if (stack_.empty()) return -1;
    const Key top = stack_.back()->id();
    if (id == top) return 0;
    std::unordered_set<Key> seen{id};
    std::vector<Key> ring{id}, next;
    for (int d = 1; !ring.empty(); ++d, ring.swap(next), next.clear())
        for (Key k : ring)
            for (std::size_t j : graph_.embeddings_holding(k)) {
                const Embedding& e = graph_.embeddings()[j];
                if (!e.open) continue;
                if (e.host == top) return d;
                if (seen.insert(e.host).second) next.push_back(e.host);
            }
    return -1;
}

void Engine::tick(double dt) {
    if (!running_) return;
    time_ += dt;
    const Tick t{dt, time_, frame_++};
    apply_edits();
    carry_kept();  // after the edits: what a target asked for is done before it follows again
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
        carry_to_hosts(*c);
        look_across(*c);
    }
    keep_time(t);
    stepped_.clear();
    // What any state said this frame - one the engine stepped, or one a
    // listener-free hand (an edit, the host) sent something to - is
    // heard by the end of it.
    graph_.each_state([this](State& s) {
        if (!s.said_out().empty()) heard_from(s);
    });
    follow_portals();
    if (stack_.empty()) running_ = false;
    if (graph_.revision() != watched_ && elapsed() - last_watch_ >= watch_interval_) check_graph();
}

void Engine::run(double target_fps, uint64_t max_frames) {
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

void Engine::run_fixed(double dt, uint64_t frames) {
    if (!running_) start();
    for (uint64_t i = 0; i < frames && running_; ++i) tick(dt);
}

double Engine::elapsed() const {
    return std::chrono::duration<double>(Clock::now() - clock_start_).count();
}

State* Engine::focused_guest() {
    while (!focus_.empty()) {
        const Embedding* e = graph_.embedding(focus_.back());
        if (e && e->open) break;
        focus_.pop_back();
    }
    const Embedding* e = innermost();
    return e ? graph_.find(e->guest) : nullptr;
}

const Embedding* Engine::innermost() const {
    const Embedding* best = nullptr;
    int best_depth = -1;
    for (auto it = focus_.rbegin(); it != focus_.rend(); ++it) {
        const Embedding* e = graph_.embedding(*it);
        if (!e || !e->open) continue;
        const int d = depth(e->host);
        if (d > best_depth) best = e, best_depth = d;
    }
    return best;
}

void Engine::report(const std::string& p) {
    if (std::find(problems_.begin(), problems_.end(), p) != problems_.end()) return;
    problems_.push_back(p);
    if (on_problem) on_problem(p);
    else std::cerr << "[sg] " << p << "\n";
}

void Engine::drive(State& s, double dt) {
    if (drive_revision_ != graph_.topology()) index_drives();
    auto it = drive_index_.find(s.id());
    if (it == drive_index_.end()) return;
    for (const Drive* dp : it->second) {
        const Drive& d = *dp;
        if (d.keeps == Keeps::WhileFocused && focused_guest() != &s) continue;  // it waits
        if (d.keeps == Keeps::WhileEntered && current() != &s) continue;        // shown, not entered: it waits
        State* c = graph_.find(d.clock);
        const Key line = timeline_of(d);
        if (!c || !c->find(line)) continue;  // validate() names it
        c->hear(Event{Temporal::advance_event(line), Params{}.set(keys::dt, dt)});
        c->dispatch_pending();
        s.hear(drive_event(d, *c, dt));
    }
}

void Engine::keep_time(const Tick& t) {
    if (drive_revision_ != graph_.topology()) index_drives();
    for (const Drive* d : always_) {
        State* s = graph_.find(d->state);
        if (!s || stepped_.count(s)) continue;
        if (d->keeps == Keeps::WhileShown && !shown(s->id())) continue;
        stepped_.insert(s);
        drive(*s, t.dt);
        step(*s, t);
        step_embeddings_in(*s, t);  // and what is open in it, as anywhere
        carry_to_hosts(*s);
    }
}

void Engine::carry_kept() {
    for (Key name : graph_.kept()) {
        const Functor* f = graph_.functor(name);
        State* src = f ? graph_.find(f->from()) : nullptr;
        State* dst = f ? graph_.find(f->to()) : nullptr;
        if (src && dst) f->apply(*src, *dst, kept_carries_[name]);
    }
}

void Engine::follow_portals() {
    if (follow_revision_ != graph_.topology() || !followed_) {
        following_.clear();
        for (const Embedding& e : graph_.embeddings())
            if (e.follows) following_.push_back(e.name);
        follow_revision_ = graph_.topology();
        followed_ = true;
    }
    for (Key name : following_) {
        const Embedding* e = graph_.embedding(name);
        const State* host = e ? graph_.find(e->host) : nullptr;
        const Element* portal = host ? host->find(e->portal) : nullptr;
        if (!portal) continue;
        const bool want = portal->params.get_or<bool>(keys::open, false);
        if (want == e->open) continue;
        want ? open_embed(name) : close_embed(name, false);
    }
}

void Engine::look_across(const State& here) {
    looked_.clear();
    for (const Seam& sm : graph_.seams()) {
        const bool from_a = sm.a == here.id();
        if (!from_a && sm.b != here.id()) continue;
        const Key other = from_a ? sm.b : sm.a;
        if (other == here.id() || std::find(looked_.begin(), looked_.end(), other) != looked_.end()) continue;
        looked_.push_back(other);
        const Functor* f = graph_.functor(from_a ? sm.a_to_b : sm.b_to_a);
        if (State* o = graph_.find(other); f && o) f->apply(here, *o);
    }
}

void Engine::carry_to_hosts(State& s) {
    for (std::size_t j : graph_.embeddings_holding(s.id())) {
        const Embedding& e = graph_.embeddings()[j];
        if (!e.open || e.sync != EmbedSync::Live || e.out.empty()) continue;
        const Functor* f = graph_.functor(e.out);
        State* subject = graph_.find(e.subject.empty() ? e.host : e.subject);
        if (f && subject) carry(*f, s, *subject, kept_memos_[e.name], e);
    }
}

bool Engine::shown(Key id) const {
    for (std::size_t j : graph_.embeddings_holding(id))
        if (graph_.embeddings()[j].open) return true;
    return false;
}

void Engine::index_drives() {
    drive_index_.clear();
    always_.clear();
    for (const Drive& d : graph_.drives()) {
        drive_index_[d.state].push_back(&d);
        if ((d.keeps == Keeps::Always || d.keeps == Keeps::WhileShown) &&
            std::none_of(always_.begin(), always_.end(), [&](const Drive* o) { return o->state == d.state; }))
            always_.push_back(&d);
    }
    kept_memos_.clear();
    drive_revision_ = graph_.topology();
}

void Engine::step(State& s, const Tick& t) {
    const bool had = s.wrote_in_update();
    s.step(t, watch_updates_);
    noticed(s, had);
    heard_from(s);
}

void Engine::noticed(State& s, bool had) {
    if (had || !s.wrote_in_update()) return;
    const std::string p = "state " + s.id().str() + " changes its data in " + s.wrote_in() +
                          ", outside any arrow: " +
                          (s.wrote_in() == "on_update" ? "drive it from a clock (graph.drive)"
                                                       : "let an arrow on an event do it");
    report(p);
    if (strict_) throw std::runtime_error("stategine: " + p);
}

void Engine::heard_from(State& s, int depth) {
    if (s.said_out().empty()) return;
    std::vector<Event> said = s.take_said();
    // A host's word about its own portal is done at once.
    said.erase(std::remove_if(said.begin(), said.end(), [this](const Event& e) { return portal(e); }), said.end());
    if (said.empty()) return;
    if (!graph_.edits().empty())
        for (const Event& e : said)
            for (const Edit& ed : graph_.edits())
                if (ed.state == s.id() && ed.event == e.name) asked_.push_back({ed.name, e});
    if (carriers_revision_ != graph_.topology()) index_carriers();
    auto it = carriers_.find(s.id());
    if (it != carriers_.end() && depth < 16) {
        const Key from = s.id();
        for (const Event& e : said)
            for (std::size_t i = 0;; ++i) {
                if (carriers_revision_ != graph_.topology()) index_carriers();  // rewired meanwhile: found again
                auto here = carriers_.find(from);
                if (here == carriers_.end() || i >= here->second.size()) break;
                Carrier& c = here->second[i];
                if ((c.e && !c.e->open) || !c.f->maps_event(e.name)) continue;
                State* to = graph_.find(c.to);
                if (!to) continue;
                // A functor carries its objects with the event, as a
                // transition's does - what moved, and that it moved,
                // arrive together - whole: a transport may read more than
                // its two elements, and it runs only when its source
                // says so. (An embedding's `in` carrying what its host
                // says, not its subject, has nothing of the host's to
                // carry: only the event goes.)
                if (c.f->from() == from) c.f->apply(s, *to);
                to->hear(c.f->carried(e, s));
                to->dispatch_pending();
                heard_from(*to, depth + 1);
            }
    }
    for (Event& e : said) said_.push_back(std::move(e));
}

void Engine::index_carriers() {
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

void Engine::enter(State& s, const Params& args) {
    s.attach(this);
    stack_.push_back(&s);
    hook(s, "on_enter", [&] { s.on_enter(args); });
}

auto Engine::routes_of(State& host) -> std::vector<Route>& {
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

void Engine::carry(const Functor& f, const State& src, State& dst, Functor::Memo& memo, const Embedding& e) {
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

void Engine::sync_live_out(State& host) {
    for (Route& r : routes_of(host)) {
        if (!r.e->open || r.e->sync != EmbedSync::Live || !r.out) continue;
        carry(*r.out, *r.guest, *r.subject, r.out_memo, *r.e);
    }
}

void Engine::step_embeddings(State& host, const Tick& t) {
    stepped_.clear();
    stepped_.insert(&host);
    step_embeddings_in(host, t);
    due_.clear();
}

void Engine::step_embeddings_in(State& host, const Tick& t) {
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

void Engine::apply_edits() {
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

void Engine::take_inputs() {
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

void Engine::process_transitions() {
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
                due_.push_back(innermost()->name);  // it crossed that embedding
            } else {
                from->hear(ev);
            }
            continue;
        }
        take(*t, *from, ev);
        if (!running_) return;
    }
}

bool Engine::portal(const Event& ev) {
    static const Key open{"portal.open"}, close{"portal.close"}, focus{"portal.focus"};
    if (ev.name != open && ev.name != close && ev.name != focus) return false;
    const Key at{ev.args.get_or<std::string>("portal", "")};
    std::vector<Key> names;
    for (std::size_t j : graph_.embeddings_hosted_by(ev.source))
        if (graph_.embeddings()[j].portal == at) names.push_back(graph_.embeddings()[j].name);
    for (Key n : names) {
        if (ev.name == open) open_embed(n);
        else if (ev.name == close) close_embed(n, ev.args.get_or<bool>(keys::commit, false));
        else focus_embed(n, ev.args.get_or<bool>("on", true));
    }
    return true;
}

void Engine::take(const Transition& t, State& from, const Event& ev) {
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

    // Across a seam that joins a state to itself (one that wraps), whoever
    // crossed is carried within it: nothing is left, nothing entered.
    if (t.kind == TransitionKind::Switch && target == &from) return;

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
            leave_top();  // (heard as entered already, by cross)
            break;
    }
}

}  // namespace sg
