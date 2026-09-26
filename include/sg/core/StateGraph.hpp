// Stategine - StateGraph: states as objects, transitions as arrows, functors
// as the typed data paths between them, embeddings as nested interfaces.
#pragma once

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

    State& add(StatePtr s) {
        State* raw = s.get();
        insert(std::move(s));
        return *raw;
    }

    State* find(Key id) {
        auto it = states_.find(id);
        return it == states_.end() ? nullptr : it->second.get();
    }
    const State* find(Key id) const {
        auto it = states_.find(id);
        return it == states_.end() ? nullptr : it->second.get();
    }

    State& state(Key id) {
        if (State* s = find(id)) return *s;
        throw std::out_of_range("no state " + id.str());
    }
    const State& state(Key id) const {
        if (const State* s = find(id)) return *s;
        throw std::out_of_range("no state " + id.str());
    }

    bool contains(Key id) const { return states_.count(id) != 0; }
    std::size_t size() const { return states_.size(); }

    std::vector<Key> ids() const {
        std::vector<Key> out;
        out.reserve(states_.size());
        for (const auto& kv : states_) out.push_back(kv.first);
        return out;
    }

    // --- transitions ---------------------------------------------------------
    // A transition is declared whole - fill in a Transition, guard and all, and
    // connect it - and is read, not rewritten, once it is in the graph.
    const Transition& connect(Transition t) {
        rev_.rewired("connect");
        if (t.name.empty())
            t.name = Key{t.from.str() + "-" + t.trigger.str() + "->" + t.to.str()};
        transitions_.push_back(std::move(t));
        const Transition& ref = transitions_.back();
        by_trigger_[ref.trigger].push_back(transitions_.size() - 1);
        return ref;
    }

    const Transition& connect(Key from, Key trigger, Key to,
                              TransitionKind kind = TransitionKind::Switch) {
        Transition t;
        t.from = from;
        t.to = to;
        t.trigger = trigger;
        t.kind = kind;
        return connect(std::move(t));
    }

    // A switch that carries the source's data into the target by `functor`.
    const Transition& connect(Key from, Key trigger, Key to, Key functor) {
        Transition t;
        t.from = from;
        t.to = to;
        t.trigger = trigger;
        t.functor = functor;
        return connect(std::move(t));
    }

    // What a transition carries across, changed - a doorway rebuilt, a way
    // through unglued (empty: nothing carried). Counted like any rewiring.
    bool set_carry(Key transition, Key functor) {
        for (Transition& t : transitions_)
            if (t.name == transition) {
                rev_.rewired("set_carry");
                t.functor = functor;
                return true;
            }
        return false;
    }

    const Transition& push(Key from, Key trigger, Key to) {
        return connect(from, trigger, to, TransitionKind::Push);
    }

    const Transition& pop(Key from, Key trigger) {
        return connect(from, trigger, Key{}, TransitionKind::Pop);
    }

    const std::deque<Transition>& transitions() const { return transitions_; }

    const Transition* transition(Key name) const {
        for (const auto& t : transitions_)
            if (t.name == name) return &t;
        return nullptr;
    }

    // First transition out of `from` for this event whose guard passes.
    // Concrete sources win over "*".
    const Transition* resolve(const State& from, const Event& ev) const {
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

    static Key any() { return Key{"*"}; }

    // --- functors -------------------------------------------------------------
    Functor& add_functor(Functor f) {
        rev_.rewired("add_functor");
        const Key name = f.name();
        if (name.empty()) throw std::runtime_error("functor needs a name");
        if (functors_.count(name)) throw std::runtime_error("duplicate functor " + name.str());
        Functor& held = functors_.emplace(name, std::move(f)).first->second;
        held.revision_ = &rev_;
        return held;
    }

    Functor& add_functor(Key name, Key from, Key to) { return add_functor(Functor{name, from, to}); }

    // Replace a functor, or add it if new. Transitions that are *derived* from
    // something else - a doorway derived from the two portal elements it joins -
    // are rebuilt rather than declared, so that they cannot drift out of step
    // with what they describe.
    Functor& set_functor(Functor f) {
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

    const Functor* functor(Key name) const {
        auto it = functors_.find(name);
        return it == functors_.end() ? nullptr : &it->second;
    }

    Functor* functor(Key name) {
        auto it = functors_.find(name);
        return it == functors_.end() ? nullptr : &it->second;
    }

    const std::map<Key, Functor>& functors() const { return functors_; }

    // Composite of a chain of registered functors, registered under `name`.
    Functor& compose_functors(Key name, const std::vector<Key>& chain) {
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

    // What a registered composite was built from, first applied first. A
    // composite is a claim - "this one arrow does what that chain does" - and
    // keeping the chain is what lets the claim be checked.
    const std::vector<Key>* composite_chain(Key name) const {
        auto it = composites_.find(name);
        return it == composites_.end() ? nullptr : &it->second;
    }

    // Declare F and G together, with the round trip checked on the spot: the
    // usual way one domain gets an editable view in another.
    struct Lens {
        Functor& in;   // host -> guest
        Functor& out;  // guest -> host
    };

    Lens add_lens(Key in_name, Key out_name, Key host, Key guest,
                  const std::vector<std::pair<Key, Key>>& objects,  // {host id, guest id}
                  Transport to_guest, Transport to_host) {
        Functor& in = add_functor(in_name, host, guest);
        Functor& out = add_functor(out_name, guest, host);
        for (const auto& pair : objects) {
            in.on_object(pair.first, pair.second, to_guest);
            out.on_object(pair.second, pair.first, to_host);
        }
        return Lens{in, out};
    }

    // --- embeddings ------------------------------------------------------------
    // An embedding is declared whole - fill in an Embedding, or name its parts
    // - and is read, not rewritten, once it is in the graph: what it joins
    // changes only through `set_sync`, `set_propagation` or dropping it and
    // embedding another, each counted. Whether it takes focus when opened is
    // not structure (`set_focus`); whether it is open is the engine's.
    const Embedding& embed(Embedding e) {
        rev_.rewired("embed");
        if (e.name.empty())
            e.name = Key{e.host.str() + "/" + e.portal.str() + ":" + e.guest.str()};
        if (by_name_.count(e.name)) throw std::runtime_error("duplicate embedding " + e.name.str());
        embeddings_.push_back(std::move(e));
        const Embedding& ref = embeddings_.back();
        index_embedding(embeddings_.size() - 1);
        return ref;
    }

    const Embedding& embed(Key name, Key host, Key portal, Key guest, Key in, Key out,
                           EmbedSync sync = EmbedSync::Commit, Key subject = Key{}) {
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

    const Embedding& embed(Key host, Key portal, Key guest, Key in = Key{}, Key out = Key{},
                           EmbedSync sync = EmbedSync::Commit) {
        return embed(Key{}, host, portal, guest, in, out, sync);
    }

    // Whether it takes input when it is opened. Returns the embedding's name,
    // so a declaration can say it in one line.
    Key set_focus(Key name, bool on) {
        if (Embedding* e = embedding_rw(name)) e->focus = on;
        return name;
    }
    // When its every-frame direction runs (see Propagation).
    bool set_propagation(Key name, Propagation p) {
        Embedding* e = embedding_rw(name);
        if (!e) return false;
        rev_.rewired("set_propagation");
        e->propagate = p;
        return true;
    }
    // Which way data runs through it (see EmbedSync).
    bool set_sync(Key name, EmbedSync s) {
        Embedding* e = embedding_rw(name);
        if (!e) return false;
        rev_.rewired("set_sync");
        e->sync = s;
        return true;
    }

    const Embedding* embedding(Key name) const {
        auto it = by_name_.find(name);
        return it == by_name_.end() ? nullptr : &embeddings_[it->second];
    }

    // Taken away: the guest no longer lives in that portal. (Close it first
    // if it is open; a closed one leaves nothing behind.)
    bool drop_embedding(Key name) {
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

    const std::deque<Embedding>& embeddings() const { return embeddings_; }
    // Where the embeddings of a host, or of a guest, sit in `embeddings()` -
    // no list built. Good until the graph's revision moves.
    const std::vector<std::size_t>& embeddings_hosted_by(Key host_id) const {
        auto it = by_host_.find(host_id);
        return it == by_host_.end() ? none() : it->second;
    }
    const std::vector<std::size_t>& embeddings_holding(Key guest_id) const {
        auto it = by_guest_.find(guest_id);
        return it == by_guest_.end() ? none() : it->second;
    }

    // --- seams ------------------------------------------------------------------
    // Registered by name; registering the same name again replaces it (a seam
    // is rebuilt whenever its doorways move).
    const Seam& add_seam(Seam s) {
        rev_.rewired("add_seam");
        for (Seam& have : seams_)
            if (have.name == s.name) return have = std::move(s);
        seams_.push_back(std::move(s));
        return seams_.back();
    }
    // Unglued: the two sides are no longer one place, and nothing holds them
    // to agree.
    void drop_seam(Key name) {
        rev_.rewired("drop_seam");
        for (auto it = seams_.begin(); it != seams_.end(); ++it)
            if (it->name == name) {
                seams_.erase(it);
                return;
            }
    }
    const std::deque<Seam>& seams() const { return seams_; }
    const Seam* seam(Key name) const {
        for (const Seam& s : seams_)
            if (s.name == name) return &s;
        return nullptr;
    }

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

    // While a Sealed lives, the interfaces cannot change: a law's trial runs
    // arrows and functors on the live graph, and one that tried to rewrite
    // what joins the states would leave the world changed after the check
    // undid its data. Such a change throws RewriteRefused, the trial is
    // undone, and the check reports it. Elements and arrows added inside a
    // state on trial are allowed - the trial takes them away again.
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
    void keep_defaults() {
        for (const auto& kv : states_)
            if (!defaults_.count(kv.first)) defaults_.emplace(kv.first, kv.second->snapshot());
    }
    void keep_default(Key id) {
        if (State* s = find(id)) defaults_[id] = s->snapshot();
    }
    bool has_default(Key id) const { return defaults_.count(id) != 0; }
    // Put `id` back as it started; with `guests`, whatever lives in its
    // portals too, and theirs. False if it has no default.
    bool restore_default(Key id, bool guests = false) {
        std::set<Key> done;
        return restore_default(id, guests, done);
    }
    bool restore_default(Key id, bool guests, std::set<Key>& done) {
        auto it = defaults_.find(id);
        State* s = find(id);
        if (it == defaults_.end() || !s || !done.insert(id).second) return false;
        s->restore(it->second);
        if (guests)
            for (const auto& e : embeddings_)
                if (e.host == id) restore_default(e.guest, true, done);
        return true;
    }
    Key initial() const { return initial_; }

    // --- analysis -------------------------------------------------------------
    // With `reuse` off, everything is checked from scratch - the plain way,
    // kept to measure against; the answer is the same.
    std::vector<std::string> validate(bool reuse = true) const {
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
            if (!c.valid || c.structure != st.structure()) c = StateCheck{true, st.structure(), st.validate()};
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
            if (sm.a == sm.b) errors.push_back("seam " + sm.name.str() + ": a state cannot be glued to itself");
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

    std::set<Key> reachable() const {
        const std::unordered_set<Key> seen = reach();
        return std::set<Key>(seen.begin(), seen.end());
    }

    // Graphviz: states (optionally with their elements and internal arrows),
    // transitions, functors, and the portals that nest one state in another.
    std::string to_dot(bool with_internals = true) const {
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
            if (t.from == any()) os << "  \"*\" [shape=diamond, label=\"any\"];\n";
            const std::string src = t.from.str();
            const std::string dst = t.kind == TransitionKind::Pop ? src : t.to.str();
            const char* style = t.kind == TransitionKind::Push
                                    ? "bold"
                                    : (t.kind == TransitionKind::Pop ? "dotted" : "solid");
            os << "  \"" << src << "\" -> \"" << dst << "\" [label=\"" << t.trigger.str()
               << (t.functor.empty() ? "" : " / " + t.functor.str()) << "\", style=" << style
               << "];\n";
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

private:
    friend class Engine;  // opens, closes and focuses embeddings

    Embedding* embedding_rw(Key name) {
        auto it = by_name_.find(name);
        return it == by_name_.end() ? nullptr : &embeddings_[it->second];
    }

    static const std::vector<std::size_t>& none() {
        static const std::vector<std::size_t> empty;
        return empty;
    }

    void index_embedding(std::size_t i) {
        const Embedding& e = embeddings_[i];
        by_name_[e.name] = i;
        by_host_[e.host].push_back(i);
        by_guest_[e.guest].push_back(i);
    }

    // Everything reachable from the initial state: through a transition, a
    // host's portal (a guest is reached through it), or a seam (a doorway goes
    // both ways). Each interface is followed once, from an index of where it
    // leaves - not found by scanning all of them at every state reached.
    std::unordered_set<Key> reach() const {
        std::unordered_set<Key> seen;
        if (initial_.empty() || !contains(initial_)) return seen;
        std::unordered_map<Key, std::vector<Key>> next;
        std::vector<Key> from_anywhere;
        for (const auto& t : transitions_) {
            if (t.kind == TransitionKind::Pop || t.to.empty()) continue;
            if (t.from == any()) from_anywhere.push_back(t.to);
            else next[t.from].push_back(t.to);
        }
        for (const auto& e : embeddings_) next[e.host].push_back(e.guest);
        for (const auto& sm : seams_) {
            next[sm.a].push_back(sm.b);
            next[sm.b].push_back(sm.a);
        }
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

    void insert(StatePtr s) {
        const Key id = s->id();
        if (states_.count(id)) throw std::runtime_error("duplicate state " + id.str());
        rev_.rewired("add state");
        s->revision_ = &rev_;
        states_.emplace(id, std::move(s));
        if (initial_.empty()) initial_ = id;
    }

    void check_portal_functor(std::vector<std::string>& errors, const Embedding& e, Key fname,
                              Key want_from, Key want_to) const {
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

    std::map<Key, StatePtr> states_;  // ordered: deterministic dot output
    std::deque<Transition> transitions_;
    std::unordered_map<Key, std::vector<std::size_t>> by_trigger_;
    std::map<Key, Functor> functors_;
    std::map<Key, std::vector<Key>> composites_;
    std::deque<Embedding> embeddings_;
    detail::Revision rev_;
    std::unordered_map<Key, State::Snapshot> defaults_;
    std::deque<Seam> seams_;
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
