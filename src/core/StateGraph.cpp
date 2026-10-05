#include "sg/core/StateGraph.hpp"

namespace sg {

State& StateGraph::add(StatePtr s) {
    State* raw = s.get();
    insert(std::move(s));
    return *raw;
}

State* StateGraph::find(Key id) {
    auto it = state_index_.find(id);
    return it == state_index_.end() ? nullptr : it->second;
}

const State* StateGraph::find(Key id) const {
    auto it = state_index_.find(id);
    return it == state_index_.end() ? nullptr : it->second;
}

State& StateGraph::state(Key id) {
    if (State* s = find(id)) return *s;
    throw std::out_of_range("no state " + id.str());
}

const State& StateGraph::state(Key id) const {
    if (const State* s = find(id)) return *s;
    throw std::out_of_range("no state " + id.str());
}

std::vector<Key> StateGraph::ids() const {
    std::vector<Key> out;
    out.reserve(states_.size());
    for (const auto& kv : states_) out.push_back(kv.first);
    return out;
}

const Transition& StateGraph::connect(Transition t) {
    if (t.name.empty()) {
        const std::string base = t.from.str() + "-" + t.trigger.str() + "->" + t.to.str();
        t.name = Key{base};
        for (int n = 2; transition_by_name_.count(t.name); ++n) t.name = Key{base + "#" + std::to_string(n)};
    } else if (transition_by_name_.count(t.name)) {
        throw std::runtime_error("duplicate transition " + t.name.str());
    }
    rev_.rewired("connect");
    transitions_.push_back(std::move(t));
    const Transition& ref = transitions_.back();
    by_trigger_[ref.trigger].push_back(transitions_.size() - 1);
    transition_by_name_.emplace(ref.name, transitions_.size() - 1);
    return ref;
}

const Transition& StateGraph::connect(Key from, Key trigger, Key to, TransitionKind kind) {
    Transition t;
    t.from = from;
    t.to = to;
    t.trigger = trigger;
    t.kind = kind;
    return connect(std::move(t));
}

const Transition& StateGraph::connect(Key from, Key trigger, Key to, Key functor) {
    Transition t;
    t.from = from;
    t.to = to;
    t.trigger = trigger;
    t.functor = functor;
    return connect(std::move(t));
}

bool StateGraph::set_carry(Key transition, Key functor) {
    for (Transition& t : transitions_)
        if (t.name == transition) {
            rev_.rewired("set_carry");
            t.functor = functor;
            return true;
        }
    return false;
}

std::vector<Key> StateGraph::sources(const Transition& t) const {
    if (t.from != any()) return {t.from};
    std::vector<Key> out;
    for (const auto& kv : states_) {
        const auto& said = kv.second->said();
        if (std::find(said.begin(), said.end(), t.trigger) != said.end()) out.push_back(kv.first);
    }
    std::sort(out.begin(), out.end(), [](Key a, Key b) { return a.str() < b.str(); });
    return out;
}

const Transition* StateGraph::transition(Key name) const {
    auto it = transition_by_name_.find(name);
    return it == transition_by_name_.end() ? nullptr : &transitions_[it->second];
}

const Transition* StateGraph::resolve(const State& from, const Event& ev) const {
    auto it = by_trigger_.find(ev.name);
    if (it == by_trigger_.end()) return nullptr;
    const Transition* wildcard = nullptr;
    for (std::size_t i : it->second) {
        const Transition& t = transitions_[i];
        if (t.guard && !t.guard(from, ev)) continue;
        if (t.from == from.id()) return &t;
        if (t.from == any() && !wildcard) wildcard = &t;
    }
    return wildcard;
}

void StateGraph::cross(const Transition& t, State& from, State* target, const Event& ev, Params& args) const {
    for (const auto& kv : t.enter) args.set(kv.first, kv.second);
    if (t.action) t.action(from, ev, args);
    if (!target) return;
    if (!t.functor.empty()) {
        const Functor* f = functor(t.functor);
        if (!f) throw std::runtime_error("transition " + t.name.str() + ": no functor " + t.functor.str());
        f->apply(from, *target, ev);
    }
    // Arriving is an event of the state arrived at: it hears where from,
    // and by which transition, for its own arrows to answer.
    target->hear(Event{entered_event(), Params{}.set("from", from.id().str()).set("by", t.name.str())});
}

Functor& StateGraph::add_functor(Functor f) {
    rev_.rewired("add_functor");
    const Key name = f.name();
    if (name.empty()) throw std::runtime_error("functor needs a name");
    if (functors_.count(name)) throw std::runtime_error("duplicate functor " + name.str());
    Functor& held = functors_.emplace(name, std::move(f)).first->second;
    held.revision_ = &rev_;
    return held;
}

Functor& StateGraph::set_functor(Functor f) {
    rev_.rewired("set_functor");
    const Key name = f.name();
    if (name.empty()) throw std::runtime_error("functor needs a name");
    composites_.erase(name);  // whatever it was composed from, it is not now
    auto it = functors_.find(name);
    if (it == functors_.end()) it = functors_.emplace(name, std::move(f)).first;
    else it->second = std::move(f);
    it->second.revision_ = &rev_;
    return it->second;
}

bool StateGraph::drop_functor(Key name) {
    auto it = functors_.find(name);
    if (it == functors_.end()) return false;
    for (const Embedding& e : embeddings_)
        if (e.in == name || e.out == name)
            throw std::runtime_error("functor " + name.str() + " is embedding " + e.name.str() + "'s: drop that first");
    for (const Transition& t : transitions_)
        if (t.functor == name)
            throw std::runtime_error("functor " + name.str() + " is carried by transition " + t.name.str() + ": unglue it first");
    rev_.rewired("drop_functor");
    functors_.erase(it);
    composites_.erase(name);
    lenses_.erase(std::remove_if(lenses_.begin(), lenses_.end(), [&](const LensPair& l) { return l.get == name || l.put == name; }),
                  lenses_.end());
    kept_.erase(std::remove(kept_.begin(), kept_.end(), name), kept_.end());
    return true;
}

const Functor* StateGraph::functor(Key name) const {
    auto it = functors_.find(name);
    return it == functors_.end() ? nullptr : &it->second;
}

Functor* StateGraph::functor(Key name) {
    auto it = functors_.find(name);
    return it == functors_.end() ? nullptr : &it->second;
}

Functor& StateGraph::compose_functors(Key name, const std::vector<Key>& chain) {
    if (chain.empty()) throw std::runtime_error("compose_functors: empty chain");
    const Functor* first = functor(chain.front());
    if (!first) throw std::runtime_error("compose_functors: unknown " + chain.front().str());
    Functor acc = *first;
    for (std::size_t i = 1; i < chain.size(); ++i) {
        const Functor* next = functor(chain[i]);
        if (!next) throw std::runtime_error("compose_functors: unknown " + chain[i].str());
        acc = Functor::compose(acc, *next);
    }
    acc.rename(name);
    Functor& out = add_functor(std::move(acc));
    composites_[name] = chain;
    return out;
}

const std::vector<Key>* StateGraph::composite_chain(Key name) const {
    auto it = composites_.find(name);
    return it == composites_.end() ? nullptr : &it->second;
}

void StateGraph::lens(Key get, Key put) {
    for (const LensPair& l : lenses_)
        if (l.get == get && l.put == put) return;  // declared already: nothing changed
    rev_.rewired("lens");
    lenses_.push_back({get, put});
}

auto StateGraph::add_lens(Key in_name, Key out_name, Key host, Key guest, const std::vector<std::pair<Key, Key>>& objects, // {host id, guest id}
                  Transport to_guest, Transport to_host) -> Lens {
    Functor& in = add_functor(in_name, host, guest);
    Functor& out = add_functor(out_name, guest, host);
    for (const auto& pair : objects) {
        in.on_object(pair.first, pair.second, to_guest);
        out.on_object(pair.second, pair.first, to_host);
    }
    return Lens{in, out};
}

const Embedding& StateGraph::embed(Embedding e) {
    rev_.rewired("embed");
    if (e.name.empty())
        e.name = Key{e.host.str() + "/" + e.portal.str() + ":" + e.guest.str()};
    if (by_name_.count(e.name)) throw std::runtime_error("duplicate embedding " + e.name.str());
    embeddings_.push_back(std::move(e));
    const Embedding& ref = embeddings_.back();
    index_embedding(embeddings_.size() - 1);
    return ref;
}

const Embedding& StateGraph::embed(Key name, Key host, Key portal, Key guest, Key in, Key out, EmbedSync sync, Key subject) {
    Embedding e;
    e.name = name;
    e.host = host;
    e.portal = portal;
    e.guest = guest;
    e.subject = subject;
    e.in = in;
    e.out = out;
    e.sync = sync;
    return embed(std::move(e));
}

const Embedding& StateGraph::embed(Key host, Key portal, Key guest, Key in, Key out, EmbedSync sync) {
    return embed(Key{}, host, portal, guest, in, out, sync);
}

Key StateGraph::set_follows(Key name, bool on) {
    if (Embedding* e = embedding_rw(name)) {
        rev_.rewired("set_follows");
        e->follows = on;
    }
    return name;
}

Key StateGraph::set_focus(Key name, bool on) {
    if (Embedding* e = embedding_rw(name)) e->focus = on;
    return name;
}

bool StateGraph::set_propagation(Key name, Propagation p) {
    Embedding* e = embedding_rw(name);
    if (!e) return false;
    rev_.rewired("set_propagation");
    e->propagate = p;
    return true;
}

bool StateGraph::set_sync(Key name, EmbedSync s) {
    Embedding* e = embedding_rw(name);
    if (!e) return false;
    rev_.rewired("set_sync");
    e->sync = s;
    return true;
}

const Embedding* StateGraph::embedding(Key name) const {
    auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : &embeddings_[it->second];
}

bool StateGraph::drop_embedding(Key name) {
    for (auto it = embeddings_.begin(); it != embeddings_.end(); ++it)
        if (it->name == name) {
            embeddings_.erase(it);
            by_host_.clear();
            by_guest_.clear();
            by_name_.clear();
            for (std::size_t i = 0; i < embeddings_.size(); ++i) index_embedding(i);
            rev_.rewired("drop_embedding");
            return true;
        }
    return false;
}

const std::vector<std::size_t>& StateGraph::embeddings_hosted_by(Key host_id) const {
    auto it = by_host_.find(host_id);
    return it == by_host_.end() ? none() : it->second;
}

const std::vector<std::size_t>& StateGraph::embeddings_holding(Key guest_id) const {
    auto it = by_guest_.find(guest_id);
    return it == by_guest_.end() ? none() : it->second;
}

const Drive& StateGraph::drive(Drive d) {
    rev_.rewired("drive");
    if (d.name.empty()) d.name = Key{d.clock.str() + ">" + d.state.str()};
    for (Drive& have : drives_)
        if (have.name == d.name) return have = std::move(d);
    drives_.push_back(std::move(d));
    return drives_.back();
}

const Drive& StateGraph::drive(Key name, Key clock, Key state, Key trigger, bool additive) {
    return drive(Drive{name, clock, state, trigger, additive, Key{}, Keeps::WhileActive});
}

void StateGraph::drop_drive(Key name) {
    rev_.rewired("drop_drive");
    for (auto it = drives_.begin(); it != drives_.end(); ++it)
        if (it->name == name) {
            drives_.erase(it);
            return;
        }
}

void StateGraph::port(Key state, Key event) {
    rev_.rewired("port");
    if (!has_port(state, event)) ports_.push_back({state, event});
}

bool StateGraph::has_port(Key state, Key event) const {
    for (const auto& p : ports_)
        if (p.first == state && p.second == event) return true;
    return false;
}

void StateGraph::keep(Key functor, bool on) {
    const auto it = std::find(kept_.begin(), kept_.end(), functor);
    if (on == (it != kept_.end())) return;
    rev_.rewired("keep");
    if (on) kept_.push_back(functor);
    else kept_.erase(it);
}

const Edit& StateGraph::edit(Edit e) {
    rev_.rewired("edit");
    if (e.name.empty()) e.name = Key{e.state.str() + ":" + e.event.str()};
    if (e.reply.empty()) e.reply = Key{e.event.str() + ".done"};
    for (Edit& have : edits_)
        if (have.name == e.name) return have = std::move(e);
    edits_.push_back(std::move(e));
    return edits_.back();
}

const Edit& StateGraph::edit(Key state, Key event, Edit::Apply apply) {
    return edit(Edit{Key{}, state, event, std::move(apply), Key{}});
}

void StateGraph::drop_edit(Key name) {
    rev_.rewired("drop_edit");
    for (auto it = edits_.begin(); it != edits_.end(); ++it)
        if (it->name == name) {
            edits_.erase(it);
            return;
        }
}

const Seam& StateGraph::add_seam(Seam s) {
    rev_.rewired("add_seam");
    for (Seam& have : seams_)
        if (have.name == s.name) return have = std::move(s);
    seams_.push_back(std::move(s));
    return seams_.back();
}

void StateGraph::drop_seam(Key name) {
    rev_.rewired("drop_seam");
    for (auto it = seams_.begin(); it != seams_.end(); ++it)
        if (it->name == name) {
            seams_.erase(it);
            return;
        }
}

const Seam* StateGraph::seam(Key name) const {
    for (const Seam& s : seams_)
        if (s.name == name) return &s;
    return nullptr;
}

Checkpoint StateGraph::checkpoint() const {
    Checkpoint c;
    for (const auto& kv : states_) c.states.insert(kv.first);
    for (const auto& kv : functors_) c.functors.insert(kv.first);
    for (const auto& kv : composites_) c.composites.insert(kv.first);
    for (const auto& kv : defaults_) c.defaults.insert(kv.first);
    c.transitions = transitions_.size();
    c.embeddings = embeddings_.size();
    c.lenses = lenses_.size();
    c.ports = ports_.size();
    c.kept = kept_.size();
    c.seams = seams_;
    c.drives = drives_;
    c.edits = edits_;
    c.initial = initial_;
    return c;
}

void StateGraph::rollback(const Checkpoint& c) {
    rev_.rewired("rollback");  // refused while sealed, before anything is touched
    // What joins states first, then what they join.
    transitions_.erase(transitions_.begin() + static_cast<std::ptrdiff_t>(std::min(c.transitions, transitions_.size())), transitions_.end());
    by_trigger_.clear();
    transition_by_name_.clear();
    for (std::size_t i = 0; i < transitions_.size(); ++i) {
        by_trigger_[transitions_[i].trigger].push_back(i);
        transition_by_name_.emplace(transitions_[i].name, i);
    }
    embeddings_.erase(embeddings_.begin() + static_cast<std::ptrdiff_t>(std::min(c.embeddings, embeddings_.size())), embeddings_.end());
    by_host_.clear();
    by_guest_.clear();
    by_name_.clear();
    for (std::size_t i = 0; i < embeddings_.size(); ++i) index_embedding(i);
    seams_ = c.seams;
    drives_ = c.drives;
    edits_ = c.edits;
    lenses_.resize(std::min(c.lenses, lenses_.size()));
    ports_.resize(std::min(c.ports, ports_.size()));
    kept_.resize(std::min(c.kept, kept_.size()));
    for (auto it = composites_.begin(); it != composites_.end();) it = c.composites.count(it->first) ? std::next(it) : composites_.erase(it);
    for (auto it = functors_.begin(); it != functors_.end();) it = c.functors.count(it->first) ? std::next(it) : functors_.erase(it);
    for (auto it = defaults_.begin(); it != defaults_.end();) it = c.defaults.count(it->first) ? std::next(it) : defaults_.erase(it);
    for (auto it = states_.begin(); it != states_.end();) {
        if (c.states.count(it->first)) {
            ++it;
            continue;
        }
        state_checks_.erase(it->second.get());
        state_index_.erase(it->first);
        it = states_.erase(it);
    }
    initial_ = c.initial;
}

void StateGraph::keep_defaults() {
    for (const auto& kv : states_)
        if (!defaults_.count(kv.first)) defaults_.emplace(kv.first, kv.second->snapshot());
}

void StateGraph::keep_default(Key id) {
    if (State* s = find(id)) defaults_[id] = s->snapshot();
}

bool StateGraph::restore_default(Key id, bool guests, std::set<Key>& done) {
    auto it = defaults_.find(id);
    State* s = find(id);
    if (it == defaults_.end() || !s || !done.insert(id).second) return false;
    s->restore(it->second);
    if (guests)
        for (const auto& e : embeddings_)
            if (e.host == id) restore_default(e.guest, true, done);
    return true;
}

int walked_dims(const State& s) {
    if (s.params().has(Key{"walk_dims"})) return static_cast<int>(s.params().num(Key{"walk_dims"}));
    const Key k = s.kind();
    return k == Key{"space3d"} ? 3 : k == Key{"space2d"} ? 2 : 0;
}

std::vector<std::string> StateGraph::validate(bool reuse) const {
    std::vector<std::string> errors;
    for (const auto& t : transitions_) {
        if (t.from != any() && !contains(t.from))
            errors.push_back("transition " + t.name.str() + ": unknown source " +
                             t.from.str());
        if (t.kind != TransitionKind::Pop && !contains(t.to))
            errors.push_back("transition " + t.name.str() + ": unknown target " + t.to.str());
        if (t.kind == TransitionKind::Pop && !t.to.empty())
            errors.push_back("transition " + t.name.str() + ": Pop must not name a target");
        if (!t.functor.empty()) {
            const Functor* f = functor(t.functor);
            if (!f) {
                errors.push_back("transition " + t.name.str() + ": unknown functor " +
                                 t.functor.str());
            } else {
                if (t.from != any() && f->from() != t.from)
                    errors.push_back("functor " + f->name().str() + " starts at " +
                                     f->from().str() + ", transition " + t.name.str() +
                                     " leaves " + t.from.str());
                if (!t.to.empty() && f->to() != t.to)
                    errors.push_back("functor " + f->name().str() + " lands in " +
                                     f->to().str() + ", transition " + t.name.str() +
                                     " enters " + t.to.str());
            }
        }
    }

    // A state's own arrows, and a functor's endpoints, are checked again
    // only when the state's structure (or the functor) is not what it was
    // when they were last checked - otherwise the answer then is the answer.
    for (const auto& kv : states_) {
        const State& st = *kv.second;
        if (!reuse) {
            for (const auto& e : st.validate()) errors.push_back(e);
            continue;
        }
        StateCheck& c = state_checks_[&st];
        if (!c.valid || c.structure != st.structure()) c = StateCheck{true, st.structure(), st.validate(), false, {}};
        for (const auto& e : c.errors) errors.push_back(e);
    }

    for (const auto& e : embeddings_) {
        const State* h = find(e.host);
        if (!h) {
            errors.push_back("embedding " + e.name.str() + ": unknown host " + e.host.str());
        } else if (!h->find(e.portal)) {
            errors.push_back("embedding " + e.name.str() + ": host has no portal element " +
                             e.portal.str());
        }
        if (!contains(e.guest))
            errors.push_back("embedding " + e.name.str() + ": unknown guest " + e.guest.str());
        const Key subject = e.subject.empty() ? e.host : e.subject;
        if (!e.subject.empty() && !contains(e.subject))
            errors.push_back("embedding " + e.name.str() + ": unknown subject " +
                             e.subject.str());
        if (subject == e.guest)
            errors.push_back("embedding " + e.name.str() + ": a state cannot embed itself");
        if (e.sync == EmbedSync::View && e.in.empty())
            errors.push_back("embedding " + e.name.str() +
                             ": a View portal needs an `in` functor to refresh the guest");
        if (e.sync == EmbedSync::View && !e.out.empty())
            errors.push_back("embedding " + e.name.str() +
                             ": a View portal is read-only, so `out` never runs");
        check_portal_functor(errors, e, e.in, subject, e.guest);
        check_portal_functor(errors, e, e.out, e.guest, subject);
    }

    for (const Seam& sm : seams_) {
        const State* a = find(sm.a);
        const State* b = find(sm.b);
        if (!a || !b) {
            errors.push_back("seam " + sm.name.str() + ": unknown side " + (a ? sm.b : sm.a).str());
            continue;
        }
        if (sm.a == sm.b && !sm.wraps)
            errors.push_back("seam " + sm.name.str() + ": a state glued to itself is the shape of its space - say the seam wraps");
        // A seam identifies like with like: two spaces walked in as many
        // dimensions. A world of fewer is not glued to one of more - it is
        // seen in it as a picture (an embedding) and gone into by a
        // transition; one of more is come out into by a transition only.
        if (const int da = walked_dims(*a), db = walked_dims(*b); da && db && da != db)
            errors.push_back("seam " + sm.name.str() + ": " + sm.a.str() + " is walked in " + std::to_string(da) + " dimensions and " + sm.b.str() + " in " +
                             std::to_string(db) + " - a seam glues like to like; see the fewer as a picture (an embedding) and go in by a transition");
        for (Key x : sm.boundary_a)
            if (!a->find(x)) errors.push_back("seam " + sm.name.str() + ": " + sm.a.str() + " has no " + x.str());
        for (Key y : sm.boundary_b)
            if (!b->find(y)) errors.push_back("seam " + sm.name.str() + ": " + sm.b.str() + " has no " + y.str());
        if (sm.boundary_a.size() != sm.boundary_b.size())
            errors.push_back("seam " + sm.name.str() + ": its boundaries differ in size, so no gluing matches them");
    }

    for (const auto& kv : functors_) {
        const State* a = find(kv.second.from());
        const State* b = find(kv.second.to());
        if (!a || !b) {
            errors.push_back("functor " + kv.first.str() + ": unknown endpoint state");
            continue;
        }
        if (!reuse) {
            for (const auto& e : kv.second.check_laws(*a, *b)) errors.push_back(e);
            continue;
        }
        FunctorCheck& c = functor_checks_[&kv.second];
        if (!c.valid || c.functor != kv.second.stamp() || c.a != a || c.b != b ||
            c.a_structure != a->structure() || c.b_structure != b->structure())
            c = FunctorCheck{true, kv.second.stamp(), a, b, a->structure(), b->structure(),
                             kv.second.check_laws(*a, *b)};
        for (const auto& e : c.errors) errors.push_back(e);
    }

    // Drives are checked against two indices, not each against all the rest:
    // the first two drives on each (clock, line), in declaration order (the
    // first that is not this one is the one named), made here once; and, for
    // each driven state, the triggers its arrows answer to (kept with the
    // state's own check while its structure is what it was).
    struct LineKey {
        Key clock, line;
        bool operator==(const LineKey& o) const { return clock == o.clock && line == o.line; }
    };
    struct LineHash {
        std::size_t operator()(const LineKey& k) const {
            const std::size_t a = std::hash<Key>{}(k.clock), b = std::hash<Key>{}(k.line);
            return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
        }
    };
    struct FirstTwo {
        const Drive* at[2] = {nullptr, nullptr};
    };
    std::unordered_map<LineKey, FirstTwo, LineHash> on_line;
    on_line.reserve(drives_.size());
    for (const Drive& d : drives_) {
        FirstTwo& two = on_line[LineKey{d.clock, d.line.empty() ? d.state : d.line}];
        if (!two.at[0]) two.at[0] = &d;
        else if (!two.at[1]) two.at[1] = &d;
    }
    std::unordered_map<const State*, std::unordered_set<Key>> answers;  // when nothing is kept
    const auto answers_of = [&](const State& st) -> const std::unordered_set<Key>& {
        const auto fill = [&st](std::unordered_set<Key>& into) {
            for (const Morphism& m : st.morphisms()) into.insert(m.trigger);
        };
        if (reuse) {
            StateCheck& c = state_checks_[&st];  // made above, for every state
            if (!c.triggers_made) {
                c.triggers.clear();
                fill(c.triggers);
                c.triggers_made = true;
            }
            return c.triggers;
        }
        auto an = answers.find(&st);
        if (an == answers.end()) {
            an = answers.emplace(&st, std::unordered_set<Key>{}).first;
            fill(an->second);
        }
        return an->second;
    };
    for (const Drive& d : drives_) {
        const State* c = find(d.clock);
        const State* s = find(d.state);
        const Key line = d.line.empty() ? d.state : d.line;
        if (!c) errors.push_back("drive " + d.name.str() + ": unknown clock " + d.clock.str());
        else if (c->kind() != Key{"temporal"})
            errors.push_back("drive " + d.name.str() + ": " + d.clock.str() + " is not a clock");
        else if (const Element* tl = c->find(line); !tl || tl->kind != Key{"timeline"})
            errors.push_back("drive " + d.name.str() + ": clock " + d.clock.str() +
                             " has no timeline " + line.str());
        if (!s) {
            errors.push_back("drive " + d.name.str() + ": unknown state " + d.state.str());
            continue;
        }
        const FirstTwo& two = on_line.find(LineKey{d.clock, line})->second;
        if (const Drive* other = two.at[0] != &d ? two.at[0] : two.at[1])
            errors.push_back("drive " + d.name.str() + ": timeline " + line.str() + " of " +
                             d.clock.str() + " also keeps time for drive " + other->name.str() +
                             " - a line keeps one state's time; give each its own");
        if (!answers_of(*s).count(d.trigger))
            errors.push_back("drive " + d.name.str() + ": no arrow of " + d.state.str() +
                             " is fired by " + d.trigger.str());
    }

    for (const auto& p : ports_)
        if (!find(p.first)) errors.push_back("port " + p.second.str() + ": unknown state " + p.first.str());
    for (Key k : kept_)
        if (!functor(k)) errors.push_back("kept functor " + k.str() + " is not declared");

    for (const Edit& e : edits_) {
        const State* s = find(e.state);
        if (!s) {
            errors.push_back("edit " + e.name.str() + ": unknown state " + e.state.str());
            continue;
        }
        const auto& said = s->said();
        if (std::find(said.begin(), said.end(), e.event) == said.end())
            errors.push_back("edit " + e.name.str() + ": " + e.state.str() + " does not say " + e.event.str() +
                             " - an edit is asked for by what a state says (State::says)");
        if (!e.apply) errors.push_back("edit " + e.name.str() + ": does nothing");
    }

    if (!initial_.empty()) {
        if (!contains(initial_)) {
            errors.push_back("initial state " + initial_.str() + " does not exist");
        } else {
            if (!reuse || reach_revision_ != rev_.topology) {
                reach_ = reach();
                reach_revision_ = rev_.topology;
            }
            const std::unordered_set<Key>& seen = reach_;
            for (const auto& kv : states_)
                if (!seen.count(kv.first))
                    errors.push_back("state " + kv.first.str() + " unreachable from " +
                                     initial_.str());
        }
    }
    return errors;
}

std::set<Key> StateGraph::reachable() const {
    const std::unordered_set<Key> seen = reach();
    return std::set<Key>(seen.begin(), seen.end());
}

std::string StateGraph::to_dot(bool with_internals) const {
    std::ostringstream os;
    os << "digraph stategraph {\n";
    os << "  compound=true;\n  rankdir=LR;\n  node [shape=box, style=rounded];\n";
    for (const auto& kv : states_) {
        const State& s = *kv.second;
        if (!with_internals) {
            os << "  \"" << s.id().str() << "\" [label=\"" << s.id().str() << "\\n<"
               << s.kind().str() << ">\"];\n";
            continue;
        }
        os << "  subgraph \"cluster_" << s.id().str() << "\" {\n";
        os << "    label=\"" << s.id().str() << " <" << s.kind().str() << ">\";\n";
        os << "    \"" << s.id().str() << "\" [shape=point, width=0.05];\n";
        for (const auto& e : s.elements())
            os << "    \"" << s.id().str() << "::" << e.id.str() << "\" [label=\""
               << e.id.str() << "\\n:" << e.kind.str() << "\", shape=ellipse];\n";
        for (const auto& m : s.morphisms()) {
            const Key dst = m.to.empty() ? m.from : m.to;
            os << "    \"" << s.id().str() << "::" << m.from.str() << "\" -> \""
               << s.id().str() << "::" << dst.str() << "\" [label=\"" << m.name.str() << " ("
               << m.trigger.str() << ")\", style=dashed];\n";
        }
        os << "  }\n";
    }
    for (const auto& t : transitions_) {
        // From the states that say its trigger, if it is taken from any
        // and some do; from "any" otherwise.
        std::vector<Key> from = sources(t);
        if (from.empty()) {
            os << "  \"*\" [shape=diamond, label=\"any\"];\n";
            from.push_back(any());
        }
        const char* style = t.kind == TransitionKind::Push
                                ? "bold"
                                : (t.kind == TransitionKind::Pop ? "dotted" : "solid");
        for (Key f : from) {
            const std::string src = f.str();
            const std::string dst = t.kind == TransitionKind::Pop ? src : t.to.str();
            os << "  \"" << src << "\" -> \"" << dst << "\" [label=\"" << t.trigger.str()
               << (t.functor.empty() ? "" : " / " + t.functor.str()) << "\", style=" << style
               << "];\n";
        }
    }
    for (const auto& e : embeddings_)
        os << "  \"" << e.host.str() << "\" -> \"" << e.guest.str() << "\" [label=\"embed "
           << e.portal.str() << "\", color=darkgreen, style=bold, arrowhead=odiamond];\n";
    for (const auto& kv : functors_)
        os << "  \"" << kv.second.from().str() << "\" -> \"" << kv.second.to().str()
           << "\" [label=\"" << kv.first.str() << "\", color=blue, arrowhead=vee];\n";
    os << "}\n";
    return os.str();
}

Embedding* StateGraph::embedding_rw(Key name) {
    auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : &embeddings_[it->second];
}

void StateGraph::index_embedding(std::size_t i) {
    const Embedding& e = embeddings_[i];
    by_name_[e.name] = i;
    by_host_[e.host].push_back(i);
    by_guest_[e.guest].push_back(i);
}

std::unordered_set<Key> StateGraph::reach() const {
    std::unordered_set<Key> seen;
    if (initial_.empty() || !contains(initial_)) return seen;
    std::unordered_map<Key, std::vector<Key>> next;
    std::vector<Key> from_anywhere;
    for (const auto& t : transitions_) {
        if (t.kind == TransitionKind::Pop || t.to.empty()) continue;
        if (t.from == any()) from_anywhere.push_back(t.to);
        else next[t.from].push_back(t.to);
    }
    // An embedding joins the two states it is between, as a seam does:
    // a camera that films a room is reached from the room as much as the
    // room from the camera.
    for (const auto& e : embeddings_) {
        next[e.host].push_back(e.guest);
        next[e.guest].push_back(e.host);
    }
    for (const auto& sm : seams_) {
        next[sm.a].push_back(sm.b);
        next[sm.b].push_back(sm.a);
    }
    // A functor that carries what a state says reaches where it goes.
    for (const auto& kv : functors_)
        if (kv.second.maps_events()) next[kv.second.from()].push_back(kv.second.to());
    // And a kept one, which carries into it whenever its source changes.
    for (Key k : kept_)
        if (const Functor* f = functor(k)) next[f->from()].push_back(f->to());
    // A driven state brings its clock with it; a clock alone reaches
    // nothing - being driven is not being reachable.
    for (const auto& d : drives_) next[d.state].push_back(d.clock);
    std::vector<Key> stack{initial_};
    seen.insert(initial_);
    for (Key to : from_anywhere)
        if (seen.insert(to).second) stack.push_back(to);
    while (!stack.empty()) {
        const Key cur = stack.back();
        stack.pop_back();
        auto it = next.find(cur);
        if (it == next.end()) continue;
        for (Key to : it->second)
            if (seen.insert(to).second) stack.push_back(to);
    }
    return seen;
}

void StateGraph::insert(StatePtr s) {
    const Key id = s->id();
    if (state_index_.count(id)) throw std::runtime_error("duplicate state " + id.str());
    rev_.rewired("add state");
    s->revision_ = &rev_;
    State* raw = s.get();
    states_.emplace(id, std::move(s));
    state_index_.emplace(id, raw);
    if (initial_.empty()) initial_ = id;
}

void StateGraph::check_portal_functor(std::vector<std::string>& errors, const Embedding& e, Key fname, Key want_from, Key want_to) const {
    if (fname.empty()) return;
    const Functor* f = functor(fname);
    if (!f) {
        errors.push_back("embedding " + e.name.str() + ": unknown functor " + fname.str());
        return;
    }
    if (f->from() != want_from || f->to() != want_to)
        errors.push_back("embedding " + e.name.str() + ": functor " + fname.str() + " is " +
                         f->from().str() + " -> " + f->to().str() + ", expected " +
                         want_from.str() + " -> " + want_to.str());
}

}  // namespace sg
