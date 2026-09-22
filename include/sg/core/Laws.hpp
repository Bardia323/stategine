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
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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

// --- trial runs --------------------------------------------------------------
// Takes a snapshot of each state the first time a path touches it and puts
// every one of them back on destruction.
class Trial {
public:
    explicit Trial(StateGraph& g) : g_(g) {}
    Trial(const Trial&) = delete;
    Trial& operator=(const Trial&) = delete;
    ~Trial() {
        for (auto& kv : saved_)
            if (State* s = g_.find(kv.first)) s->restore(std::move(kv.second));
    }

    State& touch(Key id) {
        State& s = g_.state(id);
        if (!saved_.count(id)) saved_.emplace(id, s.snapshot());
        return s;
    }

private:
    StateGraph& g_;
    std::unordered_map<Key, State::Snapshot> saved_;
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
inline Outcome run(StateGraph& g, const Path& p, const Params& args) {
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
    out.data = g.state(here).snapshot();
    return out;
}

// Two values agree if they are the same value, where numbers - ints and
// doubles alike - are compared with a tolerance, and headings on the circle.
inline bool same_value(Key k, const Value& a, const Value& b) {
    if (to_string(a) == to_string(b)) return true;
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

    const auto find = [](const State::Snapshot& s, Key id) -> const Element* {
        for (const auto& e : s.elements)
            if (e.id == id) return &e;
        return nullptr;
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

// Run both sides of one equation and report where they disagree.
inline std::vector<Violation> check(StateGraph& g, const Equation& eq) {
    const Outcome l = run(g, eq.lhs, eq.args);
    const Outcome r = run(g, eq.rhs, eq.args);
    State::Snapshot before;
    if (!l.state.empty() && g.find(l.state)) before = g.state(l.state).snapshot();
    return diff(eq, l, r, before);
}

inline void append(std::vector<Violation>& to, std::vector<Violation> from) {
    for (auto& v : from) to.push_back(std::move(v));
}

// --- the laws ----------------------------------------------------------------

// id ; f == f == f ; id, for every arrow of every state and every functor.
// The identities are the engine's own; a unit that fails here means
// composition with it is broken, which is exactly what used to happen when
// the identity functor was a snapshot of an object list.
inline std::vector<Violation> identity(StateGraph& g, const LawOptions& o = {}) {
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
            append(out, check(g, {"identity", where,
                                  Path(sid, dom(m)).arrow(State::composite(
                                      Key{m.name.str() + ".id"}, id_dom, m, m.trigger)),
                                  f, args}));
            append(out, check(g, {"identity", where,
                                  Path(sid, dom(m)).arrow(State::composite(
                                      Key{"id." + m.name.str()}, m, id_cod, m.trigger)),
                                  f, args}));
        }
    }
    for (const auto& kv : g.functors()) {
        const Functor& f = kv.second;
        if (!g.find(f.from()) || !g.find(f.to())) continue;
        const Path plain = Path(f.from()).functor(f.name());
        append(out, check(g, {"identity", "functor " + f.name().str(),
                              Path(f.from()).functor(Functor::compose(
                                  Functor::identity(f.from()), f, Key{f.name().str() + ".id"})),
                              plain, o.args}));
        append(out, check(g, {"identity", "functor " + f.name().str(),
                              Path(f.from()).functor(Functor::compose(
                                  f, Functor::identity(f.to()), Key{"id." + f.name().str()})),
                              plain, o.args}));
    }
    return out;
}

// (f ; g) ; h == f ; (g ; h) == f ; g ; h, for composable triples of arrows in
// each state and of registered functors.
inline std::vector<Violation> associativity(StateGraph& g, const LawOptions& o = {}) {
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
            append(out, check(g, {"associativity", where, Path(sid, dom(f)).arrow(left),
                                  Path(sid, dom(f)).arrow(right), args}));
            append(out, check(g, {"associativity", where, Path(sid, dom(f)).arrow(left),
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
                append(out, check(g, {"associativity",
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
inline std::vector<Violation> composition(StateGraph& g, const LawOptions& o = {}) {
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
            append(out, check(g, {"composition", sid.str() + "." + m.name.str(),
                                  Path(sid, dom(m)).arrow(m.name), chain,
                                  args_for(o, m.trigger)}));
        }
    }
    for (const auto& kv : g.functors()) {
        const std::vector<Key>* chain = g.composite_chain(kv.first);
        if (!chain) continue;
        Path steps(kv.second.from());
        for (Key k : *chain) steps.functor(k);
        append(out, check(g, {"composition", "functor " + kv.first.str(),
                              Path(kv.second.from()).functor(kv.first), steps, o.args}));
    }
    return out;
}

// f then F == F then F(f): a functor maps an arrow to one that does the same
// thing on the other side. `check_laws` already holds the endpoints to this;
// here it is the data.
inline std::vector<Violation> functoriality(StateGraph& g, const LawOptions& o = {}) {
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
            append(out, check(g, {"functoriality",
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
inline std::vector<Violation> lenses(StateGraph& g, const LawOptions& o = {}) {
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

        append(out, check(g, {"put-get", where,
                              Path(subject).functor(e.in).functor(e.out).functor(e.in),
                              Path(subject).functor(e.in), o.args}));
        if (e.open)
            append(out, check(g, {"put-get", where + " (live edits)",
                                  Path(e.guest).functor(e.out).functor(e.in), Path(e.guest),
                                  o.args}));
        append(out, check(g, {"settles", where,
                              Path(subject).functor(e.in).functor(e.out).functor(e.in).functor(
                                  e.out),
                              Path(subject).functor(e.in).functor(e.out), o.args}));

        // put ; put against put, from the same guest: not a path, since the
        // second write starts from the guest again, so it is run by hand.
        Equation eq{"put-put", where, Path(e.guest).functor(e.out).functor(e.out),
                    Path(e.guest).functor(e.out), o.args};
        Outcome twice, once;
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
        append(out, diff(eq, twice, once, g.state(subject).snapshot()));
    }
    return out;
}

// Caller-declared diagrams, against the data as it stands.
inline std::vector<Violation> diagram(StateGraph& g, const Diagram& d) {
    std::vector<Violation> out;
    for (const Equation& eq : d.equations()) append(out, check(g, eq));
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
    for (const Diagram& d : diagrams) laws::append(r.violations, laws::diagram(g, d));
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
