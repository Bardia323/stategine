// Stategine - the laws, checked on live data.
//
// The engine has two primitives: states, and the transitions between them.
// Everything with a law attached - an arrow inside a state, a functor across
// a transition, a lens behind a portal - is one of those two seen from a
// particular angle, and every law it owes has the same shape:
//
//     these two paths, run on the same data, leave the same result.
//
// So there is one checker, and the laws are equations it is handed:
//
//   identity        id ; f  ==  f  ==  f ; id
//   associativity   (f ; g) ; h  ==  f ; (g ; h)
//   composition     a registered composite does what its parts do, in order
//   functoriality   f then F  ==  F then F(f)    - the functor carries the
//                                                  arrow's *action*, not just
//                                                  its endpoints
//   put-get         write the view back, read it again: you see what you wrote
//   put-put         writing the same view twice is writing it once
//   settles         (get ; put) ; (get ; put)  ==  get ; put
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
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/Adjunction.hpp"
#include "sg/core/StateGraph.hpp"

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

    std::string str() const {
        std::string s = law + " @ " + where + ": ";
        if (!detail.empty()) return s + detail + "  [" + lhs + "  vs  " + rhs + "]";
        s += state.str() + "." + element.str() + "." + key + " was " + before + "; " + lhs +
             " leaves " + left + ", " + rhs + " leaves " + right;
        if (!args.empty()) s += "  (event args " + args + ")";
        return s;
    }
};

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
// One step: an arrow inside the current state, or a functor / transition out
// of it. Steps normally name registered arrows; a trial composite that must
// not be registered (associativity builds both bracketings) is carried here.
struct Step {
    enum class Kind { Arrow, Functor, Transition };
    Kind kind = Kind::Arrow;
    Key name;
    std::shared_ptr<const Morphism> arrow;
    std::shared_ptr<const Functor> functor;
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
    Path& arrow(Morphism m) {
        const Key n = m.name;
        return push(Step{Step::Kind::Arrow, n, std::make_shared<const Morphism>(std::move(m)),
                         nullptr});
    }
    Path& functor(Key name) { return push(Step{Step::Kind::Functor, name, nullptr, nullptr}); }
    Path& functor(Functor f) {
        const Key n = f.name();
        return push(Step{Step::Kind::Functor, n, nullptr,
                         std::make_shared<const Functor>(std::move(f))});
    }
    Path& transition(Key name) {
        return push(Step{Step::Kind::Transition, name, nullptr, nullptr});
    }

    // Concatenation; the seam is checked when the path runs.
    Path& then(const Path& p) {
        for (const Step& s : p.steps_) steps_.push_back(s);
        return *this;
    }

    Key state() const { return state_; }
    Key element() const { return element_; }
    const std::vector<Step>& steps() const { return steps_; }

    std::string str() const {
        std::string s = element_.empty() ? state_.str() : state_.str() + "." + element_.str();
        if (steps_.empty()) return s + " [id]";
        s += " [";
        for (std::size_t i = 0; i < steps_.size(); ++i) {
            if (i) s += " ; ";
            s += steps_[i].name.str();
        }
        return s + "]";
    }

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

    Diagram& commutes(Path lhs, Path rhs, Params args = {}) {
        equations_.push_back(Equation{"commutes", name_, std::move(lhs), std::move(rhs),
                                      std::move(args)});
        return *this;
    }

    const std::string& name() const { return name_; }
    const std::vector<Equation>& equations() const { return equations_; }

private:
    std::string name_;
    std::vector<Equation> equations_;
};

// How the automatic laws probe arrows that read event arguments. An integrator
// that returns early when dt is zero satisfies every law vacuously; give it a
// dt and the laws are about something.
struct LawOptions {
    Params args;                                 // handed to every trial event
    std::unordered_map<Key, Params> args_for;    // per trigger, overriding `args`
    std::size_t max_triples = 256;               // associativity budget, per state
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
    std::size_t memory() const {
        std::size_t b = 0;
        for (const auto& kv : entries_)
            b += sizeof(Entry) + kv.first.size() + kv.second.deps.size() * 8 +
                 kv.second.result.size() * sizeof(Violation);
        for (const auto& kv : sides_) b += sizeof(Side) + kv.first.size() + kv.second.deps.size() * 8;
        return b + side_elements_ * (sizeof(Element) + 64);
    }

    // --- for the laws -----------------------------------------------------------
    // A new round of checks: versions are read afresh.
    void begin(StateGraph& g) {
        g_ = &g;
        versions_.clear();
        functor_versions_.clear();
        for (const auto& kv : g.functors()) {
            const uint64_t v = kv.second.stamp();
            functor_versions_[kv.second.from()] = mix_stamp(functor_versions_[kv.second.from()], v);
            functor_versions_[kv.second.to()] = mix_stamp(functor_versions_[kv.second.to()], v);
        }
        ++round_;
    }

    // What a path reads, as numbers: each state it passes through (its data,
    // arrows included), and each functor it crosses - a registered one by its
    // own stamp, one made on the spot (a composite made to check a law) by
    // every functor at its two ends, since it was made from those.
    void deps_of(const Path& p, std::vector<uint64_t>& out) {
        Key here = p.state();
        out.push_back(state_version(here));
        for (const Step& s : p.steps()) {
            if (s.kind == Step::Kind::Arrow) continue;
            if (s.kind == Step::Kind::Transition) {
                const Transition* t = g_->transition(s.name);
                if (!t) {
                    out.push_back(0);
                    continue;
                }
                const Functor* f = t->functor.empty() ? nullptr : g_->functor(t->functor);
                out.push_back(f ? f->stamp() : 0);
                here = t->to;
            } else if (s.functor) {
                out.push_back(functor_version(s.functor->from()));
                out.push_back(functor_version(s.functor->to()));
                here = s.functor->to();
            } else {
                const Functor* f = g_->functor(s.name);
                out.push_back(f ? f->stamp() : 0);
                if (!f) continue;
                here = f->to();
            }
            out.push_back(state_version(here));
        }
    }

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
    bool direct(const Entry& e) const {
        if (strategy_ == Strategy::Direct) return true;
        if (strategy_ == Strategy::Incremental) return false;
        return round_ < e.direct_until;
    }
    // After a check: should this equation be checked directly for a while?
    void judge(Entry& e, bool hit) {
        if (strategy_ != Strategy::Adaptive) return;
        e.misses = hit ? 0 : e.misses + 1;
        // Cheaper to run than to keep, or never the same twice: stop keeping
        // for a while, then look again - the data may have settled.
        if (e.cost_ns < e.keep_ns || e.misses >= 4) {
            e.direct_until = round_ + 16;
            e.misses = 0;
            e.kept = false;
            e.result.clear();
            e.deps.clear();
        }
    }

    const Side* side(const std::string& key, const std::vector<uint64_t>& deps) const {
        auto it = sides_.find(key);
        if (it == sides_.end() || it->second.deps != deps) return nullptr;
        return &it->second;
    }
    void keep_side(const std::string& key, std::vector<uint64_t> deps,
                   std::shared_ptr<const laws::Outcome> o, std::size_t elements) {
        auto it = sides_.find(key);
        if (it != sides_.end()) {
            side_elements_ -= it->second.elements;
            sides_.erase(it);
        }
        if (side_elements_ + elements > side_budget_) return;
        side_elements_ += elements;
        sides_.emplace(key, Side{std::move(deps), std::move(o), elements});
    }

    Stats& tally() { return stats_; }

private:
    uint64_t state_version(Key id) {
        const State* s = g_->find(id);
        if (!s) return 0;
        auto it = versions_.find(s);
        if (it != versions_.end()) return it->second;
        const uint64_t v = s->content_version();
        versions_.emplace(s, v);
        return v;
    }
    uint64_t functor_version(Key state) {
        auto it = functor_versions_.find(state);
        return it == functor_versions_.end() ? 0 : it->second;
    }

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
// every one of them back on destruction. While it lives the graph's
// interfaces are sealed (StateGraph::Sealed): data can be undone, a rewritten
// graph could not, so a path that tries is stopped and reported instead.
class Trial {
public:
    explicit Trial(StateGraph& g) : g_(g), sealed_(g) {}
    Trial(const Trial&) = delete;
    Trial& operator=(const Trial&) = delete;
    ~Trial() {
        for (auto& kv : saved_)
            if (State* s = g_.find(kv.first)) s->restore(std::move(kv.second));
    }

    bool touched(Key id) const { return saved_.count(id) != 0; }

    State& touch(Key id) {
        State& s = g_.state(id);
        if (!saved_.count(id)) saved_.emplace(id, s.snapshot());
        return s;
    }

private:
    StateGraph& g_;
    std::unordered_map<Key, State::Snapshot> saved_;
    StateGraph::Sealed sealed_;
};

struct Outcome {
    std::string error;  // non-empty when the path does not type or cannot run
    Key state;          // where it ended
    Key element;
    State::Snapshot data;
};

inline Params args_for(const LawOptions& o, Key trigger) {
    auto it = o.args_for.find(trigger);
    return it == o.args_for.end() ? o.args : it->second;
}

// Run `p` on the live data, record where it ended and what it left there,
// and undo it.
inline Outcome run_steps(StateGraph& g, const Path& p, const Params& args) {
    Outcome out;
    Trial trial(g);
    Key here = p.state();
    Key at = p.element();
    if (!g.find(here)) {
        out.error = "starts in unknown state " + here.str();
        return out;
    }
    if (!at.empty() && !g.state(here).find(at)) {
        out.error = "starts at " + at.str() + ", which " + here.str() + " does not have";
        return out;
    }

    for (const Step& step : p.steps()) {
        const std::string tag = "step " + step.name.str() + ": ";
        if (step.kind == Step::Kind::Arrow) {
            State& s = trial.touch(here);
            const Morphism* m = step.arrow ? step.arrow.get() : s.morphism(step.name);
            if (!m) {
                out.error = tag + "no such arrow in " + here.str();
                return out;
            }
            if (!at.empty() && dom(*m) != at) {
                out.error = tag + "starts at " + dom(*m).str() + ", but the path is at " + at.str();
                return out;
            }
            Element* src = s.find(dom(*m));
            Element* dst = m->to.empty() ? nullptr : s.find(m->to);
            if (!src || (!m->to.empty() && !dst)) {
                out.error = tag + "an endpoint is missing from " + here.str();
                return out;
            }
            Event ev{m->trigger, args};
            ev.source = here;
            if (m->handler) m->handler(s, *src, dst, ev);
            at = cod(*m);
            continue;
        }

        const Functor* f = nullptr;
        Key target;
        if (step.kind == Step::Kind::Transition) {
            const Transition* t = g.transition(step.name);
            if (!t) {
                out.error = tag + "no such transition";
                return out;
            }
            if (t->kind == TransitionKind::Pop) {
                out.error = tag + "a pop has no fixed target, so it cannot be a step";
                return out;
            }
            if (t->from != StateGraph::any() && t->from != here) {
                out.error = tag + "leaves " + t->from.str() + ", but the path is in " + here.str();
                return out;
            }
            target = t->to;
            if (!t->functor.empty()) {
                f = g.functor(t->functor);
                if (!f) {
                    out.error = tag + "unknown functor " + t->functor.str();
                    return out;
                }
            }
        } else {
            f = step.functor ? step.functor.get() : g.functor(step.name);
            if (!f) {
                out.error = tag + "no such functor";
                return out;
            }
            target = f->to();
        }

        if (f && (f->from() != here || f->to() != target)) {
            out.error = tag + "is " + f->from().str() + " -> " + f->to().str() +
                        ", but the path is in " + here.str();
            return out;
        }
        if (!g.find(target)) {
            out.error = tag + "lands in unknown state " + target.str();
            return out;
        }
        Key image;
        if (!at.empty()) {
            image = f ? f->image_object(at) : Key{};
            if (f && image.empty()) {
                out.error = tag + "does not map " + at.str();
                return out;
            }
        }
        if (f) f->apply(g.state(here), trial.touch(target));
        here = target;
        at = image;
    }

    out.state = here;
    out.element = at;
    // What the path left, taken rather than copied where the trial is about
    // to put the state back anyway.
    State& end = g.state(here);
    if (trial.touched(here)) {
        out.data.params = end.params();
        out.data.queue = end.bus().queued();
        for (Element& e : end.elements()) out.data.elements.push_back(std::move(e));
    } else {
        out.data = end.snapshot();
    }
    return out;
}

// The same, and a path that tries to rewrite the graph - add a functor, an
// embedding - is stopped there, undone, and said not to run.
inline Outcome run(StateGraph& g, const Path& p, const Params& args) {
    try {
        return run_steps(g, p, args);
    } catch (const RewriteRefused& refused) {
        Outcome out;
        out.error = refused.what();
        return out;
    }
}

// Two values agree if they are the same value, where numbers - ints and
// doubles alike - are compared with a tolerance, and headings on the circle.
inline bool same_value(Key k, const Value& a, const Value& b) {
    // Same kind of value: compare it as itself. Formatting both as text, as
    // this once did, made checking a world of a few hundred elements slow.
    if (a.index() == b.index()) {
        if (const double* x = std::get_if<double>(&a)) {
            const double y = std::get<double>(b);
            return *x == y || same_number(k, *x, y);
        }
        return a == b;
    }
    const auto numeric = [](const Value& v, double& out) {
        if (const double* d = std::get_if<double>(&v)) return out = *d, true;
        if (const int64_t* i = std::get_if<int64_t>(&v))
            return out = static_cast<double>(*i), true;
        return false;
    };
    double x = 0, y = 0;
    return numeric(a, x) && numeric(b, y) && same_number(k, x, y);
}

inline std::string args_str(const Params& p) {
    std::string s;
    for (const auto& kv : p) s += (s.empty() ? "" : ", ") + kv.first.str() + "=" + to_string(kv.second);
    return s.empty() ? s : "{" + s + "}";
}

// Every place where two outcomes disagree, as counterexamples.
inline std::vector<Violation> diff(const Equation& eq, const Outcome& l, const Outcome& r,
                                   const State::Snapshot& before) {
    std::vector<Violation> out;
    Violation base;
    base.law = eq.law;
    base.where = eq.where;
    base.lhs = eq.lhs.str();
    base.rhs = eq.rhs.str();
    base.args = args_str(eq.args);

    const auto fail = [&](std::string why) {
        Violation v = base;
        v.detail = std::move(why);
        out.push_back(std::move(v));
    };
    if (!l.error.empty()) fail("left side does not run: " + l.error);
    if (!r.error.empty()) fail("right side does not run: " + r.error);
    if (!out.empty()) return out;
    if (l.state != r.state) {
        fail("the sides end in different states, " + l.state.str() + " and " + r.state.str());
        return out;
    }
    if (!l.element.empty() && !r.element.empty() && l.element != r.element) {
        fail("the sides end at different objects, " + l.element.str() + " and " +
             r.element.str());
        return out;
    }

    // Most equations hold, and most of the data neither side touched: when the
    // two sides left the same elements, in the same order, each with the same
    // stamp (the same content), and queued the same, they agree - found with
    // no index built and no value compared.
    if (l.data.elements.size() == r.data.elements.size() && l.data.queue.size() == r.data.queue.size()) {
        bool same = true;
        for (std::size_t i = 0; same && i < l.data.elements.size(); ++i) {
            const Element& a = l.data.elements[i];
            const Element& b = r.data.elements[i];
            same = a.id == b.id && a.alive == b.alive && a.params.stamp() == b.params.stamp();
        }
        for (std::size_t i = 0; same && i < l.data.queue.size(); ++i)
            same = l.data.queue[i].name == r.data.queue[i].name;
        if (same) return out;
    }

    // Elements by id, looked up rather than searched for: every element of
    // every snapshot is visited, so a scan per lookup would be quadratic.
    const auto index = [](const State::Snapshot& s) {
        std::unordered_map<Key, const Element*> m;
        m.reserve(s.elements.size());
        for (const auto& e : s.elements) m.emplace(e.id, &e);
        return m;
    };
    const auto in_l = index(l.data), in_r = index(r.data), in_before = index(before);
    const auto find = [&](const State::Snapshot& s, Key id) -> const Element* {
        const auto& m = &s == &l.data ? in_l : (&s == &r.data ? in_r : in_before);
        const auto it = m.find(id);
        return it == m.end() ? nullptr : it->second;
    };
    const auto value = [](const Element* e, Key k) {
        return e && e->params.has(k) ? to_string(e->params.get(k)) : std::string("<unset>");
    };
    const auto at = [&](Key id, std::string key, std::string was, std::string a, std::string b) {
        Violation v = base;
        v.state = l.state;
        v.element = id;
        v.key = std::move(key);
        v.before = std::move(was);
        v.left = std::move(a);
        v.right = std::move(b);
        out.push_back(std::move(v));
    };

    std::vector<Key> ids;
    for (const auto& e : l.data.elements) ids.push_back(e.id);
    for (const auto& e : r.data.elements)
        if (!find(l.data, e.id)) ids.push_back(e.id);

    for (Key id : ids) {
        const Element* a = find(l.data, id);
        const Element* b = find(r.data, id);
        const Element* was = find(before, id);
        if (!a || !b) {
            at(id, "<element>", was ? "present" : "absent", a ? "present" : "absent",
               b ? "present" : "absent");
            continue;
        }
        if (a->alive != b->alive)
            at(id, "<alive>", was ? (was->alive ? "true" : "false") : "absent",
               a->alive ? "true" : "false", b->alive ? "true" : "false");
        // The same stamp is the same content (see Stamps in Core.hpp): an
        // element neither side wrote to needs no comparing.
        if (a->params.stamp() == b->params.stamp()) continue;
        std::vector<Key> keys;
        for (const auto& kv : a->params) keys.push_back(kv.first);
        for (const auto& kv : b->params)
            if (!a->params.has(kv.first)) keys.push_back(kv.first);
        for (Key k : keys) {
            const bool ha = a->params.has(k), hb = b->params.has(k);
            if (ha && hb && same_value(k, a->params.get(k), b->params.get(k))) continue;
            at(id, k.str(), value(was, k), value(a, k), value(b, k));
        }
    }

    // What the paths set in motion is part of what they did.
    const auto names = [](const State::Snapshot& s) {
        std::vector<std::string> n;
        for (const auto& e : s.queue) n.push_back(e.name.str());
        std::sort(n.begin(), n.end());
        return n;
    };
    const auto joined = [](const std::vector<std::string>& n) {
        std::string s;
        for (const auto& x : n) s += (s.empty() ? "" : ", ") + x;
        return "{" + s + "}";
    };
    if (names(l.data) != names(r.data))
        at(Key{}, "<emitted>", joined(names(before)), joined(names(l.data)),
           joined(names(r.data)));
    return out;
}

// Where two outcomes disagree. What the data held before is needed only to
// say so, so it is copied only when there is something to say.
inline std::vector<Violation> settle(StateGraph& g, const Equation& eq, const Outcome& l,
                                     const Outcome& r) {
    static const State::Snapshot nothing;
    std::vector<Violation> out = diff(eq, l, r, nothing);
    if (out.empty() || l.state.empty() || !g.find(l.state)) return out;
    return diff(eq, l, r, g.state(l.state).snapshot());
}

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

inline std::vector<Violation> check_direct(StateGraph& g, const Equation& eq) {
    const Outcome l = run(g, eq.lhs, eq.args);
    const Outcome r = run(g, eq.rhs, eq.args);
    return settle(g, eq, l, r);
}

// Run both sides of one equation and report where they disagree - or, with a
// cache, say what was found before, if what it was found on still holds.
inline std::vector<Violation> check(StateGraph& g, LawCache* cache, const Equation& eq) {
    if (!cache || cache->strategy() == LawCache::Strategy::Direct) return check_direct(g, eq);
    using clock = std::chrono::steady_clock;
    const auto asked = clock::now();
    LawCache::Stats& st = cache->tally();
    ++st.equations;
    const std::string args = args_str(eq.args);
    const std::string lhs = eq.lhs.str(), rhs = eq.rhs.str();
    const std::string key = eq.law + "|" + eq.where + "|" + lhs + "|" + rhs + "|" + args;
    if (cache->direct(cache->entry(key))) {
        ++st.direct;
        const auto t = clock::now();
        std::vector<Violation> out = check_direct(g, eq);
        st.run_ns += std::chrono::duration<double, std::nano>(clock::now() - t).count();
        return out;
    }
    std::vector<uint64_t> ldeps, rdeps;
    cache->deps_of(eq.lhs, ldeps);
    cache->deps_of(eq.rhs, rdeps);
    std::vector<uint64_t> deps = ldeps;
    deps.insert(deps.end(), rdeps.begin(), rdeps.end());
    return kept(*cache, key, asked, std::move(deps), [&] {
        // One side may be what it was - the same path, on the same data -
        // even though the equation as a whole is not: run only the other.
        const auto side = [&](const Path& p, const std::string& text, std::vector<uint64_t>& d) {
            const std::string k = text + "|" + args;
            if (const LawCache::Side* s = cache->side(k, d)) {
                ++st.sides_reused;
                return s->outcome;
            }
            auto o = std::make_shared<const Outcome>(run(g, p, eq.args));
            cache->keep_side(k, std::move(d), o, o->data.elements.size());
            return std::shared_ptr<const Outcome>(o);
        };
        const std::shared_ptr<const Outcome> l = side(eq.lhs, lhs, ldeps);
        const std::shared_ptr<const Outcome> r = side(eq.rhs, rhs, rdeps);
        return settle(g, eq, *l, *r);
    });
}

inline std::vector<Violation> check(StateGraph& g, const Equation& eq) { return check_direct(g, eq); }

inline void append(std::vector<Violation>& to, std::vector<Violation> from) {
    for (auto& v : from) to.push_back(std::move(v));
}

// --- the laws ----------------------------------------------------------------

// id ; f == f == f ; id, for every arrow of every state and every functor.
// The identities are the engine's own; a unit that fails here means
// composition with it is broken, which is exactly what used to happen when
// the identity functor was a snapshot of an object list.
inline std::vector<Violation> identity(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (Key sid : g.ids()) {
        const State& s = g.state(sid);
        for (const Morphism& m : s.morphisms()) {
            if (!s.find(dom(m)) || !s.find(cod(m))) continue;  // validate() names it
            const Morphism id_dom{Key{"id." + dom(m).str()}, dom(m), Key{}, m.trigger, nullptr, {}};
            const Morphism id_cod{Key{"id." + cod(m).str()}, cod(m), Key{}, m.trigger, nullptr, {}};
            const Params args = args_for(o, m.trigger);
            const std::string where = sid.str() + "." + m.name.str();
            const Path f = Path(sid, dom(m)).arrow(m.name);
            append(out, check(g, cache, {"identity", where,
                                  Path(sid, dom(m)).arrow(State::composite(
                                      Key{m.name.str() + ".id"}, id_dom, m, m.trigger)),
                                  f, args}));
            append(out, check(g, cache, {"identity", where,
                                  Path(sid, dom(m)).arrow(State::composite(
                                      Key{"id." + m.name.str()}, m, id_cod, m.trigger)),
                                  f, args}));
        }
    }
    for (const auto& kv : g.functors()) {
        const Functor& f = kv.second;
        if (!g.find(f.from()) || !g.find(f.to())) continue;
        const Path plain = Path(f.from()).functor(f.name());
        append(out, check(g, cache, {"identity", "functor " + f.name().str(),
                              Path(f.from()).functor(Functor::compose(
                                  Functor::identity(f.from()), f, Key{f.name().str() + ".id"})),
                              plain, o.args}));
        append(out, check(g, cache, {"identity", "functor " + f.name().str(),
                              Path(f.from()).functor(Functor::compose(
                                  f, Functor::identity(f.to()), Key{"id." + f.name().str()})),
                              plain, o.args}));
    }
    return out;
}

// (f ; g) ; h == f ; (g ; h) == f ; g ; h, for composable triples of arrows in
// each state and of registered functors.
inline std::vector<Violation> associativity(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (Key sid : g.ids()) {
        const State& s = g.state(sid);
        std::unordered_map<Key, std::vector<const Morphism*>> leaving;
        for (const Morphism& m : s.morphisms())
            if (s.find(dom(m)) && s.find(cod(m))) leaving[dom(m)].push_back(&m);
        std::size_t budget = o.max_triples;
        const auto triple = [&](const Morphism& f, const Morphism& gm, const Morphism& h) {
            const Key t = f.trigger;
            const Morphism fg =
                State::composite(Key{"(" + f.name.str() + ";" + gm.name.str() + ")"}, f, gm, t);
            const Morphism gh =
                State::composite(Key{"(" + gm.name.str() + ";" + h.name.str() + ")"}, gm, h, t);
            const Morphism left =
                State::composite(Key{fg.name.str() + ";" + h.name.str()}, fg, h, t);
            const Morphism right =
                State::composite(Key{f.name.str() + ";" + gh.name.str()}, f, gh, t);
            const std::string where =
                sid.str() + ": " + f.name.str() + ", " + gm.name.str() + ", " + h.name.str();
            const Params args = args_for(o, t);
            append(out, check(g, cache, {"associativity", where, Path(sid, dom(f)).arrow(left),
                                  Path(sid, dom(f)).arrow(right), args}));
            append(out, check(g, cache, {"associativity", where, Path(sid, dom(f)).arrow(left),
                                  Path(sid, dom(f)).arrow(f.name).arrow(gm.name).arrow(h.name),
                                  args}));
        };
        for (const Morphism& f : s.morphisms()) {
            if (!s.find(dom(f)) || !s.find(cod(f))) continue;
            for (const Morphism* gm : leaving[cod(f)])
                for (const Morphism* h : leaving[cod(*gm)]) {
                    if (budget == 0) break;
                    --budget;
                    triple(f, *gm, *h);
                }
        }
    }

    std::size_t budget = o.max_triples;
    for (const auto& a : g.functors())
        for (const auto& b : g.functors()) {
            if (a.second.to() != b.second.from()) continue;
            for (const auto& c : g.functors()) {
                if (b.second.to() != c.second.from()) continue;
                if (budget-- == 0) return out;
                const Functor& f = a.second;
                const Functor& gf = b.second;
                const Functor& h = c.second;
                const Functor left = Functor::compose(Functor::compose(f, gf), h,
                                                      Key{"(" + f.name().str() + ";" +
                                                          gf.name().str() + ");" + h.name().str()});
                const Functor right = Functor::compose(f, Functor::compose(gf, h),
                                                       Key{f.name().str() + ";(" +
                                                           gf.name().str() + ";" + h.name().str() +
                                                           ")"});
                append(out, check(g, cache, {"associativity",
                                      "functors " + f.name().str() + ", " + gf.name().str() +
                                          ", " + h.name().str(),
                                      Path(f.from()).functor(left), Path(f.from()).functor(right),
                                      o.args}));
            }
        }
    return out;
}

// A registered composite is a claim that one arrow does what a chain does.
// For arrows the chain is recorded by State::compose, for functors by
// StateGraph::compose_functors; either way the claim is run and checked.
inline std::vector<Violation> composition(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (Key sid : g.ids()) {
        const State& s = g.state(sid);
        for (const Morphism& m : s.morphisms()) {
            if (m.parts.empty() || !s.find(dom(m))) continue;
            // Flatten nested composites into the arrows actually declared.
            std::vector<Key> flat;
            std::vector<Key> todo(m.parts.rbegin(), m.parts.rend());
            while (!todo.empty()) {
                const Key k = todo.back();
                todo.pop_back();
                const Morphism* part = s.morphism(k);
                if (part && !part->parts.empty()) {
                    for (auto it = part->parts.rbegin(); it != part->parts.rend(); ++it)
                        todo.push_back(*it);
                } else {
                    flat.push_back(k);
                }
            }
            Path chain(sid, dom(m));
            for (Key k : flat) chain.arrow(k);
            append(out, check(g, cache, {"composition", sid.str() + "." + m.name.str(),
                                  Path(sid, dom(m)).arrow(m.name), chain,
                                  args_for(o, m.trigger)}));
        }
    }
    for (const auto& kv : g.functors()) {
        const std::vector<Key>* chain = g.composite_chain(kv.first);
        if (!chain) continue;
        Path steps(kv.second.from());
        for (Key k : *chain) steps.functor(k);
        append(out, check(g, cache, {"composition", "functor " + kv.first.str(),
                              Path(kv.second.from()).functor(kv.first), steps, o.args}));
    }
    return out;
}

// f then F == F then F(f): a functor maps an arrow to one that does the same
// thing on the other side. `check_laws` already holds the endpoints to this;
// here it is the data.
inline std::vector<Violation> functoriality(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (const auto& kv : g.functors()) {
        const Functor& F = kv.second;
        const State* a = g.find(F.from());
        const State* b = g.find(F.to());
        if (!a || !b || F.is_identity()) continue;
        F.for_each_morphism([&](Key src, Key dst) {
            const Morphism* f = a->morphism(src);
            const Morphism* Ff = b->morphism(dst);
            if (!f || !Ff) return;  // validate() names it
            if (!a->find(dom(*f)) || !a->find(cod(*f))) return;
            if (F.image_object(dom(*f)) != dom(*Ff) || F.image_object(cod(*f)) != cod(*Ff))
                return;  // so does check_laws
            append(out, check(g, cache, {"functoriality",
                                  "functor " + F.name().str() + " on " + src.str(),
                                  Path(F.from(), dom(*f)).arrow(src).functor(F.name()),
                                  Path(F.from(), dom(*f)).functor(F.name()).arrow(dst),
                                  args_for(o, f->trigger)}));
        });
    }
    return out;
}

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
inline std::vector<Violation> lenses(StateGraph& g, const LawOptions& o = {},
                                    LawCache* cache = nullptr) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (const Embedding& e : g.embeddings()) {
        if (e.in.empty() || e.out.empty()) continue;
        const Functor* in = g.functor(e.in);
        const Functor* put = g.functor(e.out);
        const Key subject = e.subject.empty() ? e.host : e.subject;
        if (!in || !put || !g.find(subject) || !g.find(e.guest)) continue;
        if (in->from() != subject || in->to() != e.guest || put->from() != e.guest ||
            put->to() != subject)
            continue;  // validate() names it
        const std::string where = "embedding " + e.name.str();

        append(out, check(g, cache, {"put-get", where,
                              Path(subject).functor(e.in).functor(e.out).functor(e.in),
                              Path(subject).functor(e.in), o.args}));
        if (e.open)
            append(out, check(g, cache, {"put-get", where + " (live edits)",
                                  Path(e.guest).functor(e.out).functor(e.in), Path(e.guest),
                                  o.args}));
        append(out, check(g, cache, {"settles", where,
                              Path(subject).functor(e.in).functor(e.out).functor(e.in).functor(
                                  e.out),
                              Path(subject).functor(e.in).functor(e.out), o.args}));

        // put ; put against put, from the same guest: not a path, since the
        // second write starts from the guest again, so it is run by hand.
        Equation eq{"put-put", where, Path(e.guest).functor(e.out).functor(e.out),
                    Path(e.guest).functor(e.out), o.args};
        const auto put_put = [&] {
        Outcome twice, once;
        try {
        {
            Trial t(g);
            State& guest = g.state(e.guest);
            State& subj = t.touch(subject);
            put->apply(guest, subj);
            put->apply(guest, subj);
            twice.state = subject;
            twice.data = subj.snapshot();
        }
        {
            Trial t(g);
            State& subj = t.touch(subject);
            put->apply(g.state(e.guest), subj);
            once.state = subject;
            once.data = subj.snapshot();
        }
        } catch (const RewriteRefused& refused) {
            twice.error = refused.what();
        }
        return settle(g, eq, twice, once);
        };
        if (!cache) {
            append(out, put_put());
            continue;
        }
        if (cache->strategy() == LawCache::Strategy::Direct) {
            append(out, put_put());
            continue;
        }
        const auto asked = std::chrono::steady_clock::now();
        ++cache->tally().equations;
        const std::string key = eq.law + "|" + where + "|" + eq.lhs.str();
        if (cache->direct(cache->entry(key))) {
            ++cache->tally().direct;
            append(out, put_put());
            continue;
        }
        std::vector<uint64_t> deps;
        cache->deps_of(eq.lhs, deps);
        append(out, kept(*cache, key, asked, std::move(deps), put_put));
    }
    return out;
}

// Caller-declared diagrams, against the data as it stands.
inline std::vector<Violation> diagram(StateGraph& g, const Diagram& d, LawCache* cache = nullptr) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (const Equation& eq : d.equations()) append(out, check(g, cache, eq));
    return out;
}

// --- adjunctions -----------------------------------------------------------------
// F -| G on live data: the four equations `Adjunction::check` decides on the
// arrows (unit and counit naturality, both triangles), each run from its
// object on the states' current data. The structural check says the paths
// are the same word; this says they do the same thing, which is all a pair
// of different words can be asked for.
inline std::vector<Violation> adjunction(StateGraph& g, const Adjunction& adj,
                                         const LawOptions& o = {}, LawCache* cache = nullptr) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    const State* a = g.find(adj.left().from());
    const State* b = g.find(adj.left().to());
    if (!a || !b) {
        out.push_back(Violation{"adjunction", adj.name().str(), "", "", {}, {}, "", "", "", "",
                                "", "a state it joins is not in the graph"});
        return out;
    }
    const auto path = [&](Key state, Key at, const Adjunction::Word& w) {
        Path p(state, at);
        for (Key k : w)
            if (!adj.is_identity(k)) p.arrow(k);
        return p;
    };
    for (const auto& e : adj.equations(*a, *b)) {
        const Key sid = e.in_a ? a->id() : b->id();
        append(out, check(g, cache, Equation{"adjunction", adj.name().str() + ", " + e.law,
                                      path(sid, e.at, e.lhs), path(sid, e.at, e.rhs), o.args}));
    }
    return out;
}


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

inline void report(std::vector<Violation>& out, const std::string& where, const std::string& lhs,
                   const std::string& rhs, const std::string& detail) {
    Violation v;
    v.law = "seam";
    v.where = where;
    v.lhs = lhs;
    v.rhs = rhs;
    v.detail = detail;
    out.push_back(std::move(v));
}

// Every value `image` holds, held the same by `have`.
inline void agree(std::vector<Violation>& out, const std::string& where, const std::string& lhs,
                  const std::string& rhs, const Element& image, const Element* have, const std::string& what) {
    if (!have) {
        report(out, where, lhs, rhs, what + " is missing on the far side");
        return;
    }
    for (const auto& kv : image.params) {
        if (!have->params.has(kv.first)) {
            report(out, where, lhs, rhs, what + "." + kv.first.str() + " is " + to_string(kv.second) +
                                             " carried across, and not there at all");
            continue;
        }
        if (same_value(kv.first, kv.second, have->params.get(kv.first))) continue;
        report(out, where, lhs, rhs,
               what + "." + kv.first.str() + " is " + to_string(kv.second) + " carried across, but " +
                   to_string(have->params.get(kv.first)) + " there");
    }
}

// Across by `there`, back by `back`: everything mapped comes home unchanged.
inline void round_trip(std::vector<Violation>& out, const std::string& where, const Functor& there,
                       const Functor& back, const State& side) {
    const Functor loop = Functor::compose(there, back);
    State scratch(Key{"seam.scratch"});
    loop.apply(side, scratch);
    const std::string lhs = there.name().str() + " ; " + back.name().str(), rhs = "id";
    for (const auto& e : side.elements()) {
        const Key image = loop.image_object(e.id);
        if (image.empty()) continue;
        if (image != e.id) {
            report(out, where, lhs, rhs, e.id.str() + " goes across and comes back as " + image.str());
            continue;
        }
        if (const Element* r = scratch.find(image)) agree(out, where, lhs, rhs, *r, &e, side.id().str() + "." + e.id.str());
    }
}

inline bool travels(const StateGraph& g, Key f) {
    for (const Seam& s : g.seams())
        if (s.a_to_b == f || s.b_to_a == f) return true;
    return false;
}

}  // namespace seam_detail

inline std::vector<Violation> seams(const StateGraph& g) {
    using namespace seam_detail;
    std::vector<Violation> out;

    // Two-way: nothing crosses between like states except along a seam. A
    // state of no particular kind (plain `State`) is not like anything; an
    // embedding with both directions is a lens, and owes the lens laws
    // instead - it already has its way back.
    const Key untyped{"state"};
    const auto crossing = [&](Key fname, const std::string& through) {
        const Functor* f = g.functor(fname);
        if (!f) return;
        const State* a = g.find(f->from());
        const State* b = g.find(f->to());
        if (!a || !b || a == b || a->kind() != b->kind() || a->kind() == untyped || travels(g, fname)) return;
        report(out, through, f->from().str() + " -> " + f->to().str(), "a way back",
               "one way: " + fname.str() + " carries " + f->from().str() + " into " + f->to().str() +
                   ", both " + a->kind().str() + ", and no seam says how to come back - glue them with a seam");
    };
    for (const Embedding& e : g.embeddings()) {
        if (!e.in.empty() && !e.out.empty()) continue;
        if (!e.in.empty()) crossing(e.in, "embedding " + e.name.str());
        if (!e.out.empty()) crossing(e.out, "embedding " + e.name.str());
    }
    for (const Transition& t : g.transitions())
        if (!t.functor.empty()) crossing(t.functor, "transition " + t.name.str());

    for (const Seam& sm : g.seams()) {
        const std::string where = "seam " + sm.name.str();
        const State* a = g.find(sm.a);
        const State* b = g.find(sm.b);
        if (!a || !b) continue;  // validate() says so
        const auto get = [&](Key name, Key from, Key to, const char* role) -> const Functor* {
            const Functor* f = g.functor(name);
            if (!f) {
                report(out, where, role, from.str() + " -> " + to.str(), std::string(role) + " " + name.str() + " is missing");
                return nullptr;
            }
            if (f->from() != from || f->to() != to) {
                report(out, where, role, from.str() + " -> " + to.str(),
                       std::string(role) + " " + name.str() + " runs " + f->from().str() + " -> " + f->to().str());
                return nullptr;
            }
            return f;
        };
        const Functor* ab = get(sm.a_to_b, sm.a, sm.b, "travel");
        const Functor* ba = get(sm.b_to_a, sm.b, sm.a, "travel");
        const Functor* gab = get(sm.glue_ab, sm.a, sm.b, "glue");
        const Functor* gba = get(sm.glue_ba, sm.b, sm.a, "glue");

        const auto in = [](const std::vector<Key>& set, Key k) {
            return std::find(set.begin(), set.end(), k) != set.end();
        };
        if (ab && ba) {
            round_trip(out, where, *ab, *ba, *a);
            round_trip(out, where, *ba, *ab, *b);
            for (Key x : sm.boundary_a)
                if (!ab->image_object(x).empty())
                    report(out, where, ab->name().str(), "travel", "travel writes " + x.str() + ", part of the boundary");
            for (Key y : sm.boundary_b)
                if (!ba->image_object(y).empty())
                    report(out, where, ba->name().str(), "travel", "travel writes " + y.str() + ", part of the boundary");
        }
        // A glue is a map of boundaries: all of one onto all of the other,
        // one to one, and nothing else.
        const auto bijection = [&](const Functor& f, const std::vector<Key>& from, Key from_state,
                                   const std::vector<Key>& to, Key to_state) {
            std::vector<Key> hit;
            for (Key x : from) {
                const Key y = f.image_object(x);
                if (y.empty()) {
                    report(out, where, f.name().str(), "the boundary",
                           "the glue leaves " + from_state.str() + "." + x.str() + " unglued");
                } else if (!in(to, y)) {
                    report(out, where, f.name().str(), "the boundary",
                           "the glue takes " + from_state.str() + "." + x.str() + " off the far boundary, to " + y.str());
                } else if (in(hit, y)) {
                    report(out, where, f.name().str(), "the boundary",
                           "the glue takes two things to " + to_state.str() + "." + y.str());
                } else {
                    hit.push_back(y);
                }
            }
            for (Key y : to)
                if (!in(hit, y)) report(out, where, f.name().str(), "the boundary", "nothing is glued to " + to_state.str() + "." + y.str());
            f.for_each_object([&](Key x, Key) {
                if (!in(from, x))
                    report(out, where, f.name().str(), "the boundary",
                           "the glue reaches past the boundary, to " + from_state.str() + "." + x.str());
            });
        };
        if (gab && gba) {
            bijection(*gab, sm.boundary_a, sm.a, sm.boundary_b, sm.b);
            bijection(*gba, sm.boundary_b, sm.b, sm.boundary_a, sm.a);
            round_trip(out, where, *gab, *gba, *a);
            round_trip(out, where, *gba, *gab, *b);
            // Agreement, from each side.
            State from_a(Key{"seam.from_a"}), from_b(Key{"seam.from_b"});
            gab->apply(*a, from_a);
            gba->apply(*b, from_b);
            for (Key x : sm.boundary_a) {
                const Key y = gab->image_object(x);
                if (const Element* r = y.empty() ? nullptr : from_a.find(y))
                    agree(out, where, gab->name().str(), sm.b.str(), *r, b->find(y), sm.b.str() + "." + y.str());
            }
            for (Key y : sm.boundary_b) {
                const Key x = gba->image_object(y);
                if (const Element* r = x.empty() ? nullptr : from_b.find(x))
                    agree(out, where, gba->name().str(), sm.a.str(), *r, a->find(x), sm.a.str() + "." + x.str());
            }
        }
    }
    return out;
}

}  // namespace laws

// ---------------------------------------------------------------------------
// Everything at once: the structure `validate` checks, then every law above.
// ---------------------------------------------------------------------------
struct LawReport {
    std::vector<std::string> structure;  // StateGraph::validate
    std::vector<Violation> violations;

    bool ok() const { return structure.empty() && violations.empty(); }

    std::string str() const {
        std::string s;
        for (const auto& e : structure) s += "structure: " + e + "\n";
        for (const auto& v : violations) s += v.str() + "\n";
        return s;
    }
};

inline LawReport verify(StateGraph& g, const std::vector<Diagram>& diagrams = {},
                        const LawOptions& o = {}) {
    LawReport r;
    r.structure = g.validate();
    laws::append(r.violations, laws::identity(g, o));
    laws::append(r.violations, laws::associativity(g, o));
    laws::append(r.violations, laws::composition(g, o));
    laws::append(r.violations, laws::functoriality(g, o));
    laws::append(r.violations, laws::lenses(g, o));
    laws::append(r.violations, laws::seams(g));
    for (const Diagram& d : diagrams) laws::append(r.violations, laws::diagram(g, d));
    return r;
}

// The same, keeping what it finds in `cache` and answering from it wherever
// what an answer was found on has not changed: the report is verify's, and a
// graph checked again unchanged costs next to nothing.
inline LawReport verify(StateGraph& g, LawCache& cache, const std::vector<Diagram>& diagrams = {},
                        const LawOptions& o = {}) {
    LawReport r;
    r.structure = g.validate();
    laws::append(r.violations, laws::identity(g, o, &cache));
    laws::append(r.violations, laws::associativity(g, o, &cache));
    laws::append(r.violations, laws::composition(g, o, &cache));
    laws::append(r.violations, laws::functoriality(g, o, &cache));
    laws::append(r.violations, laws::lenses(g, o, &cache));
    laws::append(r.violations, laws::seams(g));
    for (const Diagram& d : diagrams) laws::append(r.violations, laws::diagram(g, d, &cache));
    return r;
}

// For callers that would rather not start at all than start on a lie.
struct LawError : std::runtime_error {
    explicit LawError(LawReport r)
        : std::runtime_error("stategine: the graph breaks its laws\n" + r.str()),
          report(std::move(r)) {}
    LawReport report;
};

inline void enforce(StateGraph& g, const std::vector<Diagram>& diagrams = {},
                    const LawOptions& o = {}) {
    LawReport r = verify(g, diagrams, o);
    if (!r.ok()) throw LawError(std::move(r));
}

}  // namespace sg
