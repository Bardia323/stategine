#include "sg/dsl/Apply.hpp"

#include <set>

#include "sg/core/Temporal.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/domains/Camera.hpp"
#include "sg/domains/Look.hpp"

namespace sg::dsl {

namespace {

// Whether the plan can be made on this graph, said before anything is made.
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
            const Functor* have = g_.functor(f.name);
            if (!have) no("functor " + f.name.str() + " is not in the graph");
            return;
        }
        if (have_functor(f.name)) return no("functor " + f.name.str() + " is already in the graph");
        functors_.insert(f.name.str());
    }
    void check(const plan::Object& o) {
        if (o.transport == plan::Object::Transport::Native && !natives_.has_transport(o.native))
            no("no native transport " + o.native + " is registered");
    }
    void check(const plan::EventMap&) {}
    void check(const plan::ArrowMap&) {}
    void check(const plan::ComposeFunctors& c) {
        if (have_functor(c.name)) return no("functor " + c.name.str() + " is already in the graph");
        functors_.insert(c.name.str());
    }
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
    }
    void check(const plan::Initial&) {}
    void check(const plan::Edit& e) {
        if (!natives_.has_edit(e.native)) return no("no native edit " + e.native + " is registered");
        const std::string name = e.state.str() + ":" + e.event.str();
        for (const Edit& have : g_.edits())
            if (have.name.str() == name) return no("edit " + name + " is already in the graph");
        if (!edits_.insert(name).second) no("edit " + name + " is declared twice");
    }
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
        Morphism::Handler h = a.body == plan::Arrow::Body::Native ? natives_.arrow(a.native) : nullptr;
        if (a.to.empty()) s.loop(a.name, a.from, a.trigger, std::move(h));
        else s.arrow(a.name, a.from, a.to, a.trigger, std::move(h));
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
            case plan::Object::Transport::Native: f.on_object(o.src, o.dst, natives_.transport(o.native)); break;
        }
    }
    void make(const plan::EventMap& m) { g_.functor(m.functor)->on_event(m.src, m.dst); }
    void make(const plan::ArrowMap& m) { g_.functor(m.functor)->on_morphism(m.src, m.dst); }
    void make(const plan::ComposeFunctors& c) { g_.compose_functors(c.name, c.chain); }
    void make(const plan::Lens& l) { g_.lens(l.get, l.put); }
    void make(const plan::Connect& c) {
        sg::Transition t = c.t;
        if (!c.enter.empty()) t.action = [enter = c.enter](State&, const Event&, Params& args) {
            for (const auto& kv : enter) args.set(kv.first, kv.second);
        };
        g_.connect(std::move(t));
    }
    void make(const plan::Embed& e) { g_.embed(e.e); }
    void make(const plan::Glue& gl) { glue_doorway(g_, gl.name, gl.a, gl.pa, gl.b, gl.pb, gl.also); }
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

}  // namespace

Applied apply(const Plan& plan, StateGraph& g, const Natives& natives, Bindings* bindings, const Kinds& kinds) {
    if (const std::string why = Preflight(plan, g, natives, bindings, kinds).run(); !why.empty()) return {false, why};
    Maker maker(g, natives, bindings, kinds);
    try {
        for (const Step& s : plan.steps) std::visit([&](const auto& st) { maker.make(st); }, s);
    } catch (const std::exception& e) {
        return {false, std::string("the graph may be partly changed: ") + e.what()};
    }
    g.keep_defaults();  // what was made has its start, as what the engine started with has
    return {};
}

}  // namespace sg::dsl
