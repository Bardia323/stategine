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

class StateGraph {
public:
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

    State* find(Key id) const {
        auto it = states_.find(id);
        return it == states_.end() ? nullptr : it->second.get();
    }

    State& state(Key id) const {
        if (State* s = find(id)) return *s;
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
    Transition& connect(Transition t) {
        if (t.name.empty())
            t.name = Key{t.from.str() + "-" + t.trigger.str() + "->" + t.to.str()};
        transitions_.push_back(std::move(t));
        Transition& ref = transitions_.back();
        by_trigger_[ref.trigger].push_back(transitions_.size() - 1);
        return ref;
    }

    Transition& connect(Key from, Key trigger, Key to,
                        TransitionKind kind = TransitionKind::Switch) {
        Transition t;
        t.from = from;
        t.to = to;
        t.trigger = trigger;
        t.kind = kind;
        return connect(std::move(t));
    }

    Transition& push(Key from, Key trigger, Key to) {
        return connect(from, trigger, to, TransitionKind::Push);
    }

    Transition& pop(Key from, Key trigger) {
        return connect(from, trigger, Key{}, TransitionKind::Pop);
    }

    const std::deque<Transition>& transitions() const { return transitions_; }

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

    std::vector<const Transition*> outgoing(Key state_id) const {
        std::vector<const Transition*> out;
        for (const auto& t : transitions_)
            if (t.from == state_id || t.from == any()) out.push_back(&t);
        return out;
    }

    static Key any() { return Key{"*"}; }

    // --- functors -------------------------------------------------------------
    Functor& add_functor(Functor f) {
        const Key name = f.name();
        if (name.empty()) throw std::runtime_error("functor needs a name");
        if (functors_.count(name)) throw std::runtime_error("duplicate functor " + name.str());
        return functors_.emplace(name, std::move(f)).first->second;
    }

    Functor& add_functor(Key name, Key from, Key to) { return add_functor(Functor{name, from, to}); }

    const Functor* functor(Key name) const {
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
        return add_functor(std::move(acc));
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
    Embedding& embed(Embedding e) {
        if (e.name.empty())
            e.name = Key{e.host.str() + "/" + e.portal.str() + ":" + e.guest.str()};
        for (const auto& x : embeddings_)
            if (x.name == e.name) throw std::runtime_error("duplicate embedding " + e.name.str());
        embeddings_.push_back(std::move(e));
        Embedding& ref = embeddings_.back();
        by_host_[ref.host].push_back(embeddings_.size() - 1);
        return ref;
    }

    Embedding& embed(Key name, Key host, Key portal, Key guest, Key in, Key out,
                     EmbedSync sync = EmbedSync::Commit) {
        Embedding e;
        e.name = name;
        e.host = host;
        e.portal = portal;
        e.guest = guest;
        e.in = in;
        e.out = out;
        e.sync = sync;
        return embed(std::move(e));
    }

    Embedding& embed(Key host, Key portal, Key guest, Key in = Key{}, Key out = Key{},
                     EmbedSync sync = EmbedSync::Commit) {
        return embed(Key{}, host, portal, guest, in, out, sync);
    }

    Embedding* embedding(Key name) {
        for (auto& e : embeddings_)
            if (e.name == name) return &e;
        return nullptr;
    }

    std::deque<Embedding>& embeddings() { return embeddings_; }
    const std::deque<Embedding>& embeddings() const { return embeddings_; }

    // Indexed: the engine asks this every frame.
    std::vector<Embedding*> embeddings_of(Key host_id) {
        std::vector<Embedding*> out;
        auto it = by_host_.find(host_id);
        if (it == by_host_.end()) return out;
        out.reserve(it->second.size());
        for (std::size_t i : it->second) out.push_back(&embeddings_[i]);
        return out;
    }

    void set_initial(Key id) { initial_ = id; }
    Key initial() const { return initial_; }

    // --- analysis -------------------------------------------------------------
    std::vector<std::string> validate() const {
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

        for (const auto& kv : states_)
            for (const auto& e : kv.second->validate()) errors.push_back(e);

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
            if (e.host == e.guest)
                errors.push_back("embedding " + e.name.str() + ": a state cannot embed itself");
            check_portal_functor(errors, e, e.in, e.host, e.guest);
            check_portal_functor(errors, e, e.out, e.guest, e.host);
        }

        for (const auto& kv : functors_) {
            const State* a = find(kv.second.from());
            const State* b = find(kv.second.to());
            if (!a || !b) {
                errors.push_back("functor " + kv.first.str() + ": unknown endpoint state");
                continue;
            }
            for (const auto& e : kv.second.check_laws(*a, *b)) errors.push_back(e);
        }

        if (!initial_.empty()) {
            if (!contains(initial_)) {
                errors.push_back("initial state " + initial_.str() + " does not exist");
            } else {
                const std::set<Key> seen = reachable();
                for (const auto& kv : states_)
                    if (!seen.count(kv.first))
                        errors.push_back("state " + kv.first.str() + " unreachable from " +
                                         initial_.str());
            }
        }
        return errors;
    }

    std::set<Key> reachable() const {
        std::set<Key> seen;
        if (initial_.empty() || !contains(initial_)) return seen;
        std::vector<Key> stack{initial_};
        seen.insert(initial_);
        while (!stack.empty()) {
            const Key cur = stack.back();
            stack.pop_back();
            for (const auto& t : transitions_) {
                if (t.from != cur && t.from != any()) continue;
                if (t.kind == TransitionKind::Pop || t.to.empty()) continue;
                if (seen.insert(t.to).second) stack.push_back(t.to);
            }
            // A guest is reachable through its host's portal.
            for (const auto& e : embeddings_) {
                if (e.host != cur) continue;
                if (seen.insert(e.guest).second) stack.push_back(e.guest);
            }
        }
        return seen;
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
    void insert(StatePtr s) {
        const Key id = s->id();
        if (states_.count(id)) throw std::runtime_error("duplicate state " + id.str());
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
    std::deque<Embedding> embeddings_;
    std::unordered_map<Key, std::vector<std::size_t>> by_host_;
    Key initial_;
};

}  // namespace sg
