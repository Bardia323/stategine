#include "sg/dsl/Apply.hpp"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "sg/core/Temporal.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/domains/Camera.hpp"
#include "sg/domains/Look.hpp"
#include "sg/dsl/Facts.hpp"

namespace sg::dsl {

namespace {

// Whether the plan can be made on this graph, said before anything is touched:
// what can be known from names alone - a name already taken, a state that is
// not there, a native nobody registered. It is the cheap early refusal, not
// the guarantee: whatever it misses is caught by the rollback below.
class Preflight {
public:
    Preflight(const Plan& p, const StateGraph& g, const Natives& n, const Bindings* b, const Kinds& k)
        : plan_(p), g_(g), natives_(n), bindings_(b), kinds_(k) {}

    std::string run() {
        for (const Step& s : plan_.steps) {
            std::visit([&](const auto& st) { check(st); }, s);
            if (!why_.empty()) return why_;
        }
        return why_;
    }

private:
    const Plan& plan_;
    const StateGraph& g_;
    const Natives& natives_;
    const Bindings* bindings_;
    const Kinds& kinds_;
    std::set<std::string> states_, functors_, embeds_, seams_, drives_, edits_, transitions_;
    std::string why_;

    void no(const std::string& m) {
        if (why_.empty()) why_ = m;
    }
    bool have_state(Key id) const { return states_.count(id.str()) || g_.contains(id); }
    bool have_functor(Key id) const { return functors_.count(id.str()) || g_.functor(id); }
    // A name a step makes that must be new: the graph would replace what has it, and a replaced thing cannot be put back.
    void fresh_functor(Key id) {
        if (have_functor(id)) return no("functor " + id.str() + " is already in the graph");
        functors_.insert(id.str());
    }

    void check(const plan::State& s) {
        if (!kinds_.find(s.kind)) return no("no state kind " + s.kind);
        if (have_state(s.id)) return no("state " + s.id.str() + " is already in the graph");
        states_.insert(s.id.str());
    }
    void check(const plan::Extern& s) {
        const State* have = g_.find(s.id);
        if (!have) return no("state " + s.id.str() + " is not in the graph");
        if (!s.kind.empty() && s.kind != "state" && have->kind().str() != s.kind)
            return no("state " + s.id.str() + " is " + have->kind().str() + ", not " + s.kind);
    }
    void check(const plan::Element&) {}
    void check(const plan::Param&) {}
    void check(const plan::Arrow& a) {
        if (a.body == plan::Arrow::Body::Native && !natives_.has_arrow(a.native)) no("no native arrow " + a.native + " is registered");
    }
    void check(const plan::Compose&) {}
    void check(const plan::Says&) {}
    void check(const plan::Functor& f) {
        if (f.built_elsewhere) {
            if (!g_.functor(f.name)) no("functor " + f.name.str() + " is not in the graph");
            return;
        }
        fresh_functor(f.name);
    }
    void check(const plan::Object& o) {
        if (o.transport == plan::Object::Transport::Native && !natives_.has_transport(o.native))
            no("no native transport " + o.native + " is registered");
    }
    void check(const plan::EventMap&) {}
    void check(const plan::ArrowMap&) {}
    void check(const plan::ComposeFunctors& c) { fresh_functor(c.name); }
    void check(const plan::Lens&) {}
    void check(const plan::Connect& c) {
        if (!c.t.name.empty() && (transitions_.count(c.t.name.str()) || g_.transition(c.t.name)))
            return no("transition " + c.t.name.str() + " is already in the graph");
        transitions_.insert(c.t.name.str());
    }
    void check(const plan::Embed& e) {
        if (embeds_.count(e.e.name.str()) || g_.embedding(e.e.name)) return no("embedding " + e.e.name.str() + " is already in the graph");
        embeds_.insert(e.e.name.str());
    }
    void check(const plan::Glue& g) {
        if (seams_.count(g.name.str()) || g_.seam(g.name)) return no("seam " + g.name.str() + " is already in the graph");
        seams_.insert(g.name.str());
        // its four functors are made with it, and would replace ones of the same names
        const Seam s = doorway_seam(g.name, g.a, g.pa, g.b, g.pb, g.also);
        for (Key f : {s.a_to_b, s.b_to_a, s.glue_ab, s.glue_ba}) fresh_functor(f);
    }
    void check(const plan::Drive& d) {
        for (const Drive& have : g_.drives())
            if (have.name == d.d.name) return no("drive " + d.d.name.str() + " is already in the graph");
        if (!drives_.insert(d.d.name.str()).second) return no("drive " + d.d.name.str() + " is declared twice");
        const State* clock = g_.find(d.d.clock);
        if (clock && !dynamic_cast<const Temporal*>(clock)) no("state " + d.d.clock.str() + " keeps no time: a drive names a Temporal");
    }
    void check(const plan::Port&) {}
    void check(const plan::Keep&) {}
    void check(const plan::Wear& w) {
        const Key name = wear_embedding(w.host, w.look).name;
        if (embeds_.count(name.str()) || g_.embedding(name)) return no(w.host.str() + " already wears " + w.look.str());
        embeds_.insert(name.str());
    }
    void check(const plan::Film& f) {
        const Key name = film_name(f.camera, f.world);
        if (embeds_.count(name.str()) || g_.embedding(name)) return no(f.camera.str() + " already films " + f.world.str());
        embeds_.insert(name.str());
        if (!f.rig.empty()) fresh_functor(Key{name.str() + ".rig"});
    }
    void check(const plan::Edit& e) {
        if (!natives_.has_edit(e.native)) return no("no native edit " + e.native + " is registered");
        const std::string name = e.state.str() + ":" + e.event.str();
        for (const Edit& have : g_.edits())
            if (have.name.str() == name) return no("edit " + name + " is already in the graph");
        if (!edits_.insert(name).second) no("edit " + name + " is declared twice");
    }
    void check(const plan::Initial&) {}
    void check(const plan::Bind&) {
        if (!bindings_) no("the plan binds input, and no table was given for it");
    }
};

class Maker {
public:
    Maker(StateGraph& g, const Natives& n, Bindings* b, const Kinds& k) : g_(g), natives_(n), bindings_(b), kinds_(k) {}

    void make(const plan::State& s) { g_.add(kinds_.find(s.kind)->make(s.id)); }
    void make(const plan::Extern& s) { expect_state(g_, s.id.str(), s.kind); }
    void make(const plan::Element& e) {
        State& s = g_.state(e.state);
        if (e.own) expect_element(s, e.id.str(), e.kind.str());
        else s.add_element(e.id, e.kind);
    }
    void make(const plan::Param& p) {
        State& s = g_.state(p.state);
        (p.element.empty() ? s.params() : s.element(p.element).params).set(p.key, p.value);
    }
    void make(const plan::Arrow& a) {
        State& s = g_.state(a.state);
        if (a.own) {
            expect_arrow(s, a.name.str(), a.from.str(), a.to.str(), a.trigger.str());
            return;
        }
        if (a.body == plan::Arrow::Body::Affine) {
            if (a.to.empty()) s.affine(a.name, a.from, a.trigger, a.affine);
            else s.affine(a.name, a.from, a.to, a.trigger, a.affine);
            return;
        }
        const bool native = a.body == plan::Arrow::Body::Native;
        Morphism::Handler h = native ? natives_.arrow(a.native) : nullptr;
        const Key id = native ? Key{a.native} : Key{};
        if (a.to.empty()) s.loop(a.name, a.from, a.trigger, std::move(h), id);
        else s.arrow(a.name, a.from, a.to, a.trigger, std::move(h), id);
    }
    void make(const plan::Compose& c) { g_.state(c.state).compose(c.name, c.f, c.g, c.trigger); }
    void make(const plan::Says& s) { g_.state(s.state).says(s.event); }
    void make(const plan::Functor& f) {
        if (f.built_elsewhere) expect_functor(g_, f.name.str(), f.from.str(), f.to.str());
        else g_.add_functor(f.name, f.from, f.to);
    }
    void make(const plan::Object& o) {
        Functor& f = *g_.functor(o.functor);
        switch (o.transport) {
            case plan::Object::Transport::Copy: f.on_object(o.src, o.dst); break;
            case plan::Object::Transport::Only: f.on_object(o.src, o.dst, transport::only(o.names)); break;
            case plan::Object::Transport::Swizzle: f.on_object(o.src, o.dst, transport::swizzle(o.pairs, o.rest)); break;
            case plan::Object::Transport::Affine: f.on_object(o.src, o.dst, transport::affine(o.affine)); break;
            case plan::Object::Transport::Native: f.on_object(o.src, o.dst, natives_.transport(o.native), Key{o.native}); break;
        }
    }
    void make(const plan::EventMap& m) { g_.functor(m.functor)->on_event(m.src, m.dst); }
    void make(const plan::ArrowMap& m) { g_.functor(m.functor)->on_morphism(m.src, m.dst); }
    void make(const plan::ComposeFunctors& c) { g_.compose_functors(c.name, c.chain); }
    void make(const plan::Lens& l) { g_.lens(l.get, l.put); }
    void make(const plan::Connect& c) { g_.connect(c.t); }
    void make(const plan::Embed& e) { g_.embed(e.e); }
    void make(const plan::Glue& gl) { glue_doorway(g_, gl.name, gl.a, gl.pa, gl.b, gl.pb, gl.also, gl.wraps); }
    void make(const plan::Drive& d) {
        sg::drive(g_, clock(g_, d.d.clock.str()), d.d.state, d.d.trigger, d.d.additive, d.d.keeps);
    }
    void make(const plan::Port& p) { g_.port(p.state, p.event); }
    void make(const plan::Keep& k) { g_.keep(k.functor); }
    void make(const plan::Wear& w) { wear(g_, w.host, w.look); }
    void make(const plan::Film& f) { film(g_, f.camera, f.world, f.rig); }
    void make(const plan::Initial& i) { g_.set_initial(i.state); }
    void make(const plan::Edit& e) {
        sg::Edit ed;
        ed.state = e.state;
        ed.event = e.event;
        ed.reply = e.reply;
        ed.apply = natives_.edit(e.native);
        ed.native = Key{e.native};
        g_.edit(std::move(ed));
    }
    void make(const plan::Bind& b) {
        for (const plan::Bind::Entry& e : b.entries) bindings_->add(b.device, e.key, e.event, e.args);
    }

private:
    StateGraph& g_;
    const Natives& natives_;
    Bindings* bindings_;
    const Kinds& kinds_;
};

// The states that were there and that the plan changes inside: a host that
// gains a look slot, a clock that gains a timeline. Kept as they were, to be
// put back if the plan is not made.
std::vector<std::pair<Key, State::Snapshot>> touched(const Plan& plan, StateGraph& g) {
    std::vector<std::pair<Key, State::Snapshot>> out;
    std::set<Key> seen, made;
    const auto note = [&](Key id) {
        const State* s = g.find(id);
        if (s && !made.count(id) && seen.insert(id).second) out.emplace_back(id, s->snapshot());
    };
    for (const Step& s : plan.steps) {
        if (const auto* st = std::get_if<plan::State>(&s)) made.insert(st->id);
        else if (const auto* w = std::get_if<plan::Wear>(&s)) note(w->host);
        else if (const auto* d = std::get_if<plan::Drive>(&s)) note(d->d.clock);
    }
    return out;
}

}  // namespace

Applied apply(const Plan& plan, StateGraph& g, const Natives& natives, Bindings* bindings, const Kinds& kinds) {
    if (g.sealed()) return {false, "the graph is being checked: it cannot be changed now"};
    if (const std::string why = Preflight(plan, g, natives, bindings, kinds).run(); !why.empty()) return {false, why};

    // All or nothing. What the graph is made of is checkpointed, and the states
    // the plan touches inside are kept; the plan is made; and unless it is made
    // whole and the graph it made is as valid as the graph it found, all of it is
    // put back. Nothing runs in between (an edit is applied at the start of a
    // frame, and no listener or arrow is called while it is), so no one sees the
    // half. The graph is the same graph throughout.
    const std::vector<std::string> before = g.validate();
    const Checkpoint mark = g.checkpoint();
    auto inside = touched(plan, g);
    Maker maker(g, natives, bindings, kinds);
    std::string why;
    try {
        for (const Step& s : plan.steps) std::visit([&](const auto& st) { maker.make(st); }, s);
    } catch (const std::exception& e) {
        why = e.what();
    }
    if (why.empty()) {
        // What the plan made must not make the graph less valid than it was.
        std::multiset<std::string> known(before.begin(), before.end());
        for (const std::string& problem : g.validate()) {
            auto old = known.find(problem);
            if (old != known.end()) known.erase(old);
            else why += (why.empty() ? "the construction does not validate: " : "; ") + problem;
        }
    }
    if (!why.empty()) {
        g.rollback(mark);
        for (auto& kept : inside)
            if (State* s = g.find(kept.first)) s->restore(std::move(kept.second));
        return {false, "nothing was changed: " + why};
    }
    for (const Step& s : plan.steps)
        if (const auto* st = std::get_if<plan::State>(&s)) g.keep_default(st->id);  // what was made has its start, as every state has
    return {};
}

// --- reload ---------------------------------------------------------------------------------

namespace {

// The name a transition is known by in the graph: its own, or the one connect gives it.
Key transition_name(const Transition& t) {
    return t.name.empty() ? Key{t.from.str() + "-" + t.trigger.str() + "->" + t.to.str()} : t.name;
}

// Which declaration a step is part of: what is made again whole when any of it changes.
std::string unit_of(const Step& step) {
    return std::visit(
        [](const auto& st) -> std::string {
            using S = std::decay_t<decltype(st)>;
            if constexpr (std::is_same_v<S, plan::State>) return "state " + st.id.str();
            else if constexpr (std::is_same_v<S, plan::Extern>) return "extern " + st.id.str();
            else if constexpr (std::is_same_v<S, plan::Element> || std::is_same_v<S, plan::Param> || std::is_same_v<S, plan::Arrow> ||
                               std::is_same_v<S, plan::Compose> || std::is_same_v<S, plan::Says>)
                return "state " + st.state.str();
            else if constexpr (std::is_same_v<S, plan::Functor>) return "functor " + st.name.str();
            else if constexpr (std::is_same_v<S, plan::Object> || std::is_same_v<S, plan::EventMap> || std::is_same_v<S, plan::ArrowMap>)
                return "functor " + st.functor.str();
            else if constexpr (std::is_same_v<S, plan::ComposeFunctors>) return "composite " + st.name.str();
            else if constexpr (std::is_same_v<S, plan::Lens>) return "lens " + st.get.str() + " " + st.put.str();
            else if constexpr (std::is_same_v<S, plan::Connect>) return "transition " + transition_name(st.t).str();
            else if constexpr (std::is_same_v<S, plan::Embed>) return "embed " + st.e.name.str();
            else if constexpr (std::is_same_v<S, plan::Glue>) return "seam " + st.name.str();
            else if constexpr (std::is_same_v<S, plan::Drive>) return "drive " + st.d.name.str();
            else if constexpr (std::is_same_v<S, plan::Port>) return "port " + st.state.str() + " " + st.event.str();
            else if constexpr (std::is_same_v<S, plan::Keep>) return "keep " + st.functor.str();
            else if constexpr (std::is_same_v<S, plan::Wear>) return "wear " + st.host.str() + " " + st.look.str();
            else if constexpr (std::is_same_v<S, plan::Film>) return "film " + st.camera.str() + " " + st.world.str();
            else if constexpr (std::is_same_v<S, plan::Edit>) return "edit " + st.state.str() + ":" + st.event.str();
            else if constexpr (std::is_same_v<S, plan::Initial>) return "initial";
            else return "bind " + st.device;
        },
        step);
}

// One declaration: its steps, in the plan's order, and what they declare.
struct Unit {
    std::vector<const Step*> steps;
    std::vector<std::string> facts;

    template <typename S>
    std::vector<const S*> all() const {
        std::vector<const S*> out;
        for (const Step* s : steps)
            if (const S* x = std::get_if<S>(s)) out.push_back(x);
        return out;
    }
    template <typename S>
    const S* first() const {
        for (const Step* s : steps)
            if (const S* x = std::get_if<S>(s)) return x;
        return nullptr;
    }
};

std::map<std::string, Unit> units_of(const Plan& p) {
    std::map<std::string, Unit> out;
    for (const Step& s : p.steps) out[unit_of(s)].steps.push_back(&s);
    for (auto& kv : out) {
        Plan one;
        for (const Step* s : kv.second.steps) one.steps.push_back(*s);
        kv.second.facts = facts(one);
    }
    return out;
}

bool starts(const std::string& s, const char* head) { return s.rfind(head, 0) == 0; }

// A value, not structure: what a state holds now is its own, and a running
// world's differs from its source's start by being played.
bool value_fact(const std::string& f) { return starts(f, "param ") || starts(f, "eparam "); }

// Arrows taken from a state: the state kept, its list of arrows made again
// without them (State::restore, with a start made for it), its structure
// counted as changed so whatever was worked out from it is worked out again.
void drop_arrows(State& x, const std::set<Key>& names) {
    bool any = false;
    for (const Morphism& m : x.morphisms()) any = any || names.count(m.name) != 0;
    if (!any) return;
    State::Snapshot now = x.start();
    auto kept = std::make_shared<std::deque<Morphism>>();
    for (const Morphism& m : x.morphisms())
        if (!names.count(m.name)) kept->push_back(m);
    now.morphisms = kept->size();
    now.arrows = kept;
    ++now.removals;  // not the structure it was: no stamp is carried over
    x.restore(std::move(now));
}

class Reloader {
public:
    Reloader(StateGraph& g, const Natives& n, const Kinds& k) : g_(g), maker_(g, n, nullptr, k) {}

    std::vector<std::function<void()>> undo;  // what puts back what was done, last first
    std::vector<std::pair<Key, State::Snapshot>> inside;  // states that were there, as they were (with their arrows)
    std::vector<Key> made;  // states made new

    void note(Key id) {
        const State* s = g_.find(id);
        if (!s || noted_.count(id)) return;
        noted_.insert(id);
        inside.emplace_back(id, s->start());
    }

    // A transition taken away, to be declared again or not at all.
    void take_transition(Key name) {
        const Transition* t = g_.transition(name);
        if (!t) return;
        Transition saved = *t;
        g_.disconnect(name);
        undo.push_back([this, saved] { g_.connect(saved); });
    }
    void connect(const plan::Connect& c) {
        const Key name = g_.connect(c.t).name;
        undo.push_back([this, name] { g_.disconnect(name); });
    }

    void take_embedding(Key name) {
        const Embedding* e = g_.embedding(name);
        if (!e) return;
        Embedding saved = *e;
        g_.drop_embedding(name);
        undo.push_back([this, saved] { g_.embed(saved); });
    }
    void added_embedding(Key name) {
        undo.push_back([this, name] { g_.drop_embedding(name); });
    }

    // A functor about to be replaced or taken away: put back as it was.
    void save_functor(Key name) {
        const Functor* f = g_.functor(name);
        if (!f) return;
        Functor saved = *f;
        const std::vector<Key>* chain = g_.composite_chain(name);
        std::vector<Key> parts = chain ? *chain : std::vector<Key>{};
        undo.push_back([this, saved, parts, name] {
            if (parts.empty()) {
                g_.set_functor(saved);
            } else {
                if (g_.functor(name)) g_.drop_functor(name);
                g_.compose_functors(name, parts);
            }
        });
    }
    void take_functor(Key name) {
        if (!g_.functor(name)) return;
        save_functor(name);
        g_.drop_functor(name);
    }

    // A state the source makes again in place: its elements and arrows as the
    // source now says, its params carried.
    void reshape(State& x, const Unit* before, const Unit& after, const std::set<std::string>& have) {
        // What each arrow and composite is, by the source, now and before.
        std::map<Key, const plan::Arrow*> arrows_now, arrows_was;
        std::map<Key, const plan::Compose*> composes_now, composes_was;
        for (const plan::Arrow* a : after.all<plan::Arrow>()) arrows_now[a->name] = a;
        for (const plan::Compose* c : after.all<plan::Compose>()) composes_now[c->name] = c;
        if (before) {
            for (const plan::Arrow* a : before->all<plan::Arrow>()) arrows_was[a->name] = a;
            for (const plan::Compose* c : before->all<plan::Compose>()) composes_was[c->name] = c;
        }
        // Taken away: each arrow the source no longer has, or has otherwise
        // than the graph does; and whatever is composed of one.
        std::set<Key> gone;
        const auto fact_of = [](const Step& st) {
            Plan one;
            one.steps.push_back(st);
            const std::vector<std::string> f = facts(one);
            return f.empty() ? std::string{} : f.front();
        };
        for (const auto& kv : arrows_was)
            if (!kv.second->own && !arrows_now.count(kv.first)) gone.insert(kv.first);
        for (const auto& kv : composes_was)
            if (!composes_now.count(kv.first)) gone.insert(kv.first);
        for (const auto& kv : arrows_now)
            if (!kv.second->own && !have.count(fact_of(Step{*kv.second}))) gone.insert(kv.first);
        for (const auto& kv : composes_now)
            if (!have.count(fact_of(Step{*kv.second}))) gone.insert(kv.first);
        for (bool more = true; more;) {
            more = false;
            for (const Morphism& m : x.morphisms())
                if (!m.parts.empty() && !gone.count(m.name))
                    for (Key part : m.parts)
                        if (gone.count(part)) {
                            gone.insert(m.name);
                            more = true;
                            break;
                        }
        }
        drop_arrows(x, gone);

        // Elements: those taken out of the source go (with their arrows);
        // one of another kind is another element.
        std::set<Key> now_elements, fresh;
        for (const plan::Element* e : after.all<plan::Element>()) now_elements.insert(e->id);
        if (before)
            for (const plan::Element* e : before->all<plan::Element>())
                if (!e->own && !now_elements.count(e->id) && x.find(e->id)) x.remove_with_arrows(e->id);
        for (const plan::Element* e : after.all<plan::Element>()) {
            if (e->own) {
                maker_.make(*e);  // made by its kind: it must be there, of that kind
                continue;
            }
            Element* have_e = x.find(e->id);
            if (have_e && have_e->kind != e->kind) {
                x.remove_with_arrows(e->id);
                have_e = nullptr;
            }
            if (!have_e) {
                x.add_element(e->id, e->kind);
                fresh.insert(e->id);
            }
        }

        // Params: what is there stays; what the source newly says is added.
        for (const plan::Param* p : after.all<plan::Param>()) {
            if (p->element.empty()) {
                if (!x.params().has(p->key)) x.params().set(p->key, p->value);
                continue;
            }
            Element* e = x.find(p->element);
            if (e && (fresh.count(p->element) || !e->params.has(p->key))) e->params.set(p->key, p->value);
        }

        // Arrows, then what is composed of them: each the source has and the state now lacks.
        for (const plan::Arrow* a : after.all<plan::Arrow>())
            if (!a->own && !x.morphism(a->name)) maker_.make(*a);
        for (const plan::Compose* c : after.all<plan::Compose>())
            if (!x.morphism(c->name)) maker_.make(*c);
        for (const plan::Says* s : after.all<plan::Says>()) {
            const auto& said = x.said();
            if (std::find(said.begin(), said.end(), s->event) == said.end()) maker_.make(*s);
        }
    }

    Maker& maker() { return maker_; }

private:
    StateGraph& g_;
    Maker maker_;
    std::set<Key> noted_;
};

// What a reload would refuse, said before anything is touched.
std::string refused(const std::map<std::string, Unit>& before, const std::map<std::string, Unit>& after,
                    const std::set<std::string>& changed, const std::set<std::string>& removed, const StateGraph& g,
                    const Natives& natives, const Kinds& kinds) {
    for (const std::string& key : removed) {
        const Unit& u = before.at(key);
        if (const plan::State* s = u.first<plan::State>(); s && g.contains(s->id))
            return "state " + s->id.str() + " is no longer in the source: a state is not taken from a running graph";
        if (starts(key, "port ")) return key + " is no longer in the source: a port is not closed in a running graph";
        if (starts(key, "lens ")) return key + " is no longer in the source: a lens is not undeclared in a running graph";
    }
    for (const std::string& key : changed) {
        const Unit& u = after.at(key);
        if (const plan::State* s = u.first<plan::State>()) {
            const KindInfo* k = kinds.find(s->kind);
            if (!k) return "no state kind " + s->kind;
            if (const State* have = g.find(s->id)) {
                const Key kind = k->make(s->id)->kind();
                if (have->kind() != kind)
                    return "state " + s->id.str() + " is a " + have->kind().str() + ", not a " + kind.str() +
                           ": a state's kind is what it is, and another kind is another state";
            }
        }
        for (const plan::Arrow* a : u.all<plan::Arrow>())
            if (a->body == plan::Arrow::Body::Native && !natives.has_arrow(a->native)) return "no native arrow " + a->native + " is registered";
        for (const plan::Object* o : u.all<plan::Object>())
            if (o->transport == plan::Object::Transport::Native && !natives.has_transport(o->native))
                return "no native transport " + o->native + " is registered";
        for (const plan::Edit* e : u.all<plan::Edit>())
            if (!natives.has_edit(e->native)) return "no native edit " + e->native + " is registered";
        for (const plan::Drive* d : u.all<plan::Drive>()) {
            const State* clock = g.find(d->d.clock);
            if (clock && !dynamic_cast<const Temporal*>(clock)) return "state " + d->d.clock.str() + " keeps no time: a drive names a Temporal";
        }
    }
    return {};
}

}  // namespace

Reloaded reload(const Plan& before, const Plan& after, StateGraph& g, const Natives& natives, Bindings* bindings, const Kinds& kinds) {
    if (g.sealed()) return {false, "the graph is being checked: it cannot be changed now", {}};
    const std::map<std::string, Unit> was = units_of(before), now = units_of(after);

    // What changed: a declaration the source says otherwise than it did, or
    // otherwise than the graph has it. Values are the world's own once it
    // runs: they are held to the source alone.
    const std::vector<std::string> graph_facts = facts(g, Scope::Whole);
    const std::set<std::string> have(graph_facts.begin(), graph_facts.end());
    std::set<std::string> changed, removed;
    for (const auto& [key, u] : now) {
        if (starts(key, "extern ") || starts(key, "bind ")) continue;
        auto old = was.find(key);
        bool differs = old != was.end() && old->second.facts != u.facts;
        for (std::size_t i = 0; !differs && i < u.facts.size(); ++i)
            differs = !value_fact(u.facts[i]) && !have.count(u.facts[i]);
        if (differs) changed.insert(key);
    }
    for (const auto& [key, u] : was)
        if (!now.count(key) && !starts(key, "extern ") && !starts(key, "bind ")) removed.insert(key);
    // A composite is what its chain was when it was made: a link of it made again, it is made again.
    for (const auto& [key, u] : now) {
        if (!starts(key, "composite ") || changed.count(key)) continue;
        if (const plan::ComposeFunctors* c = u.first<plan::ComposeFunctors>())
            for (Key part : c->chain)
                if (changed.count("functor " + part.str()) || removed.count("functor " + part.str())) changed.insert(key);
    }

    Reloaded out;
    // Binds are a table outside the graph, made as the source says.
    const auto bind = [&] {
        if (!bindings) return;
        for (const auto& [key, u] : now)
            for (const plan::Bind* b : u.all<plan::Bind>())
                for (const plan::Bind::Entry& e : b->entries) bindings->add(b->device, e.key, e.event, e.args);
    };
    if (changed.empty() && removed.empty()) {
        bind();
        return out;  // the same source: nothing touched
    }
    if (const std::string why = refused(was, now, changed, removed, g, natives, kinds); !why.empty())
        return {false, "nothing was changed: " + why, {}};

    const auto is = [&](const std::string& key, const char* head) { return starts(key, head); };
    const auto each = [&](const std::set<std::string>& keys, const char* head, const std::map<std::string, Unit>& units,
                          const std::function<void(const std::string&, const Unit&)>& f) {
        for (const std::string& key : keys)
            if (is(key, head)) f(key, units.at(key));
    };

    const std::vector<std::string> problems = g.validate();
    const Checkpoint mark = g.checkpoint();
    Reloader r(g, natives, kinds);
    std::string why;
    try {
        // 1. What is made again or taken away is first taken away: what joins
        //    states, then what they are joined by.
        const auto take = [&](const char* head, const std::function<void(const std::string&, bool, const Unit&)>& f) {
            for (const std::set<std::string>* keys : {&changed, &removed})
                for (const std::string& key : *keys)
                    if (starts(key, head)) f(key, keys == &removed, was.count(key) ? was.at(key) : now.at(key));
        };
        take("transition ", [&](const std::string& key, bool, const Unit&) { r.take_transition(Key{key.substr(11)}); });
        take("embed ", [&](const std::string& key, bool, const Unit&) { r.take_embedding(Key{key.substr(6)}); });
        take("wear ", [&](const std::string&, bool gone, const Unit& old) {
            if (const plan::Wear* w = old.first<plan::Wear>(); w && gone) r.take_embedding(wear_embedding(w->host, w->look).name);
        });
        take("film ", [&](const std::string&, bool, const Unit& old) {
            if (const plan::Film* f = old.first<plan::Film>()) {
                const Key name = film_name(f->camera, f->world);
                r.take_embedding(name);
                r.take_functor(Key{name.str() + ".rig"});
            }
        });
        take("drive ", [&](const std::string& key, bool gone, const Unit&) {
            if (gone) g.drop_drive(Key{key.substr(6)});  // its line stays on the clock
        });
        take("edit ", [&](const std::string& key, bool gone, const Unit&) {
            if (gone) g.drop_edit(Key{key.substr(5)});
        });
        take("keep ", [&](const std::string& key, bool gone, const Unit&) {
            if (!gone) return;
            const Key f{key.substr(5)};
            g.keep(f, false);
            r.undo.push_back([&g, f] { g.keep(f); });
        });
        take("seam ", [&](const std::string&, bool gone, const Unit& old) {
            const plan::Glue* gl = old.first<plan::Glue>();
            if (!gl) return;
            const Seam seam = doorway_seam(gl->name, gl->a, gl->pa, gl->b, gl->pb, gl->also, gl->wraps);
            if (gone) g.drop_seam(gl->name);
            for (Key f : {seam.a_to_b, seam.b_to_a, seam.glue_ab, seam.glue_ba}) {
                if (gone) r.take_functor(f);
                else r.save_functor(f);
            }
        });
        take("composite ", [&](const std::string& key, bool, const Unit&) { r.take_functor(Key{key.substr(10)}); });
        take("functor ", [&](const std::string&, bool gone, const Unit& old) {
            const plan::Functor* f = old.first<plan::Functor>();
            if (gone && f && !f->built_elsewhere) r.take_functor(f->name);
        });

        // 2. States: made, or made again in place.
        each(changed, "state ", now, [&](const std::string& key, const Unit& u) {
            const plan::State* s = u.first<plan::State>();
            if (!s) return;
            State* x = g.find(s->id);
            if (!x) {
                for (const Step* st : u.steps) std::visit([&](const auto& step) { r.maker().make(step); }, *st);
                r.made.push_back(s->id);
            } else {
                r.note(s->id);
                auto old = was.find(key);
                r.reshape(*x, old == was.end() ? nullptr : &old->second, u, have);
            }
            out.changed.push_back(key);
        });

        // 3. Functors, their maps made again from nothing under the same name; then composites.
        each(changed, "functor ", now, [&](const std::string& key, const Unit& u) {
            const plan::Functor* f = u.first<plan::Functor>();
            if (!f || f->built_elsewhere) return;
            if (g.functor(f->name)) {
                r.save_functor(f->name);
                g.set_functor(Functor(f->name, f->from, f->to));
                for (const Step* st : u.steps)
                    if (!std::holds_alternative<plan::Functor>(*st)) std::visit([&](const auto& step) { r.maker().make(step); }, *st);
            } else {
                for (const Step* st : u.steps) std::visit([&](const auto& step) { r.maker().make(step); }, *st);
            }
            out.changed.push_back(key);
        });
        each(changed, "composite ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::ComposeFunctors* c : u.all<plan::ComposeFunctors>()) {
                r.maker().make(*c);
                const Key name = c->name;
                r.undo.push_back([&g, name] {
                    if (g.functor(name)) g.drop_functor(name);
                });
            }
            out.changed.push_back(key);
        });

        // 4. How states meet, declared again.
        each(changed, "lens ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::Lens* l : u.all<plan::Lens>()) r.maker().make(*l);
            out.changed.push_back(key);
        });
        each(changed, "transition ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::Connect* c : u.all<plan::Connect>()) r.connect(*c);
            out.changed.push_back(key);
        });
        each(changed, "embed ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::Embed* e : u.all<plan::Embed>()) {
                r.maker().make(*e);
                r.added_embedding(e->e.name);
            }
            out.changed.push_back(key);
        });
        each(changed, "wear ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::Wear* w : u.all<plan::Wear>()) {
                const Key name = wear_embedding(w->host, w->look).name;
                if (g.embedding(name)) continue;  // worn already, as it is
                r.note(w->host);
                r.maker().make(*w);
                r.added_embedding(name);
            }
            out.changed.push_back(key);
        });
        each(changed, "film ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::Film* f : u.all<plan::Film>()) {
                r.note(f->camera);
                r.maker().make(*f);
                r.added_embedding(film_name(f->camera, f->world));
            }
            out.changed.push_back(key);
        });
        each(changed, "seam ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::Glue* gl : u.all<plan::Glue>()) r.maker().make(*gl);
            out.changed.push_back(key);
        });
        each(changed, "drive ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::Drive* d : u.all<plan::Drive>()) {
                r.note(d->d.clock);
                r.maker().make(*d);  // by its name, in place of the one there; its line is the line there
            }
            out.changed.push_back(key);
        });
        for (const char* head : {"port ", "edit ", "initial"})
            each(changed, head, now, [&](const std::string& key, const Unit& u) {
                for (const Step* st : u.steps) std::visit([&](const auto& step) { r.maker().make(step); }, *st);
                out.changed.push_back(key);
            });
        each(changed, "keep ", now, [&](const std::string& key, const Unit& u) {
            for (const plan::Keep* k : u.all<plan::Keep>()) {
                const bool was_kept = std::find(g.kept().begin(), g.kept().end(), k->functor) != g.kept().end();
                g.keep(k->functor);
                if (!was_kept) {
                    const Key f = k->functor;
                    r.undo.push_back([&g, f] { g.keep(f, false); });
                }
            }
            out.changed.push_back(key);
        });
        for (const std::string& key : removed) out.changed.push_back(key + " (taken away)");

        // What it made must not make the graph less valid than it was.
        std::multiset<std::string> known(problems.begin(), problems.end());
        for (const std::string& problem : g.validate()) {
            auto old = known.find(problem);
            if (old != known.end()) known.erase(old);
            else why += (why.empty() ? "the construction does not validate: " : "; ") + problem;
        }
    } catch (const std::exception& e) {
        why = e.what();
    }
    if (!why.empty()) {
        // Put back, last first: what was declared again is taken away and what
        // was there is declared again; then what was added since the mark goes,
        // and the states that were there are as they were.
        for (auto it = r.undo.rbegin(); it != r.undo.rend(); ++it) {
            try {
                (*it)();
            } catch (const std::exception&) {
                // the rollback below puts back what this could not
            }
        }
        g.rollback(mark);
        for (auto& kept : r.inside)
            if (State* s = g.find(kept.first)) s->restore(std::move(kept.second));
        return {false, "nothing was changed: " + why, {}};
    }
    for (Key id : r.made) g.keep_default(id);  // what was made has its start, as every state has
    bind();
    std::sort(out.changed.begin(), out.changed.end());
    return out;
}

}  // namespace sg::dsl
