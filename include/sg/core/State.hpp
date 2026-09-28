// Stategine - State: an object of the state-graph, itself a small category.
//
// Lookup and dispatch are indexed: elements by interned id, morphisms bucketed
// by trigger. Firing an event touches only the arrows that listen for it.
#pragma once

#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/Core.hpp"
#include "sg/core/Declared.hpp"

namespace sg {

class Engine;

// A state's elements, each to act on - never the list they are kept in:
// what a state is made of changes only by add_element / remove_element (and
// their like), which its structure and its graph count.
class ElementRange {
public:
    using iterator = std::deque<Element>::iterator;
    explicit ElementRange(std::deque<Element>& list) : list_(&list) {}
    iterator begin() const { return list_->begin(); }
    iterator end() const { return list_->end(); }
    std::size_t size() const { return list_->size(); }
    bool empty() const { return list_->empty(); }
    Element& operator[](std::size_t i) const { return (*list_)[i]; }
    Element& front() const { return list_->front(); }
    Element& back() const { return list_->back(); }

private:
    std::deque<Element>* list_;
};

// Per-tick context handed to every state.
struct Tick {
    double dt = 0.0;    // seconds since the previous frame
    double time = 0.0;  // simulated seconds: the sum of every dt so far, this one included
    uint64_t frame = 0;
};

class State {
public:
    explicit State(Key id) : id_(id) {}
    virtual ~State() = default;

    State(const State&) = delete;
    State& operator=(const State&) = delete;

    Key id() const { return id_; }
    virtual Key kind() const { return Key{"state"}; }

    Params& params() { return params_; }
    const Params& params() const { return params_; }

    // The engine running it, seen as everyone outside it sees it: const. A
    // state acts on nothing but itself; what it has to tell the world it
    // says (says, emit), and the graph's transitions take it from there.
    const Engine* engine() const { return engine_; }
    void attach(Engine* e) { engine_ = e; }

    // --- lifecycle ----------------------------------------------------------
    virtual void on_enter(const Params& /*args*/) {}
    virtual void on_exit() {}
    virtual void on_pause() {}
    virtual void on_resume() {}
    virtual void on_update(const Tick&) {}
    virtual void on_render(const Tick&) {}
    // Return true to consume the event before any morphism sees it.
    virtual bool on_event(const Event&) { return false; }

    // The frame order is part of the contract, hence non-virtual. With
    // `watch`, an on_update that changes the state's data - rather than
    // emitting an event for an arrow to act on - is noted (wrote_in_update):
    // behaviour is arrows, and time reaches a state through a drive.
    void step(const Tick& t, bool watch = false) {
        watching_ = watch;
        watched("on_update", [&] { on_update(t); });
        dispatch_pending();
        watched("on_render", [&] { on_render(t); });
        watching_ = false;
    }
    // Every hook - on_update, on_event, on_render, and the lifecycle the
    // engine runs (on_enter, on_exit, on_pause, on_resume) - may look, and
    // emit; one that changes the state's data is behaviour outside any arrow.
    // While watched, the first hook to do so is named here.
    bool wrote_in_update() const { return !wrote_in_.empty(); }
    const std::string& wrote_in() const { return wrote_in_; }
    // Run a hook, watching it if asked (the engine does, for the lifecycle).
    template <typename F>
    void watched(const char* hook, F&& run, bool watch) {
        const bool was = watching_;
        watching_ = watch;
        watched(hook, std::forward<F>(run));
        watching_ = was;
    }

    // --- elements (objects) -------------------------------------------------
    Element& add_element(Element e) {
        if (index_.count(e.id)) throw std::runtime_error("duplicate element " + e.id.str());
        restructured("add_element");
        index_.emplace(e.id, elements_.size());
        elements_.push_back(std::move(e));
        return elements_.back();
    }

    Element& add_element(Key id, Key kind) { return add_element(Element{id, kind}); }

    Element* find(Key id) {
        auto it = index_.find(id);
        return it == index_.end() ? nullptr : &elements_[it->second];
    }

    const Element* find(Key id) const {
        auto it = index_.find(id);
        return it == index_.end() ? nullptr : &elements_[it->second];
    }

    Element& element(Key id) {
        if (Element* e = find(id)) return *e;
        throw std::out_of_range("no element " + id.str() + " in state " + id_.str());
    }

    const Element& element(Key id) const {
        if (const Element* e = find(id)) return *e;
        throw std::out_of_range("no element " + id.str() + " in state " + id_.str());
    }

    void remove_element(Key id) {
        auto it = index_.find(id);
        if (it == index_.end()) return;
        restructured("remove_element");
        const std::size_t at = it->second;
        elements_.erase(elements_.begin() + static_cast<std::ptrdiff_t>(at));
        index_.erase(it);
        // Only those after it moved: their places are updated, the rest stand.
        for (std::size_t i = at; i < elements_.size(); ++i) index_[elements_[i].id] = i;
        ++removals_;
    }

    // Take an element away together with its arrows: an arrow from or to
    // something that is not there is not an arrow. (remove_element alone
    // leaves them dangling, for validate() to name.)
    void remove_with_arrows(Key id) {
        remove_element(id);
        bool arrows = false;
        for (const Morphism& m : morphisms_) arrows = arrows || m.from == id || m.to == id;
        if (arrows) restructured("remove_with_arrows");
        const std::size_t had = morphisms_.size();
        for (std::size_t i = morphisms_.size(); i-- > 0;)
            if (morphisms_[i].from == id || morphisms_[i].to == id)
                morphisms_.erase(morphisms_.begin() + static_cast<std::ptrdiff_t>(i));
        if (morphisms_.size() != had) {
            ++removals_;
            index_arrows();
        }
    }

    ElementRange elements() { return ElementRange{elements_}; }
    const std::deque<Element>& elements() const { return elements_; }

    // --- morphisms (arrows between elements) --------------------------------
    // An arrow, once added, is what it was declared: its ends and its action
    // are read, never rewritten in place - to change an arrow is to take it
    // away and add another, which the structure's stamp (and the graph)
    // notices.
    const Morphism& add_morphism(Morphism m) {
        if (m.name.empty()) throw std::runtime_error("morphism needs a name");
        // A name is what a law, a functor and a path call the arrow by: two
        // arrows under one name would be one arrow to them, two to dispatch.
        if (by_name_.count(m.name))
            throw std::runtime_error("duplicate arrow " + m.name.str() + " in state " + id_.str());
        restructured("add_morphism");
        by_trigger_[m.trigger].push_back(morphisms_.size());
        by_name_.emplace(m.name, morphisms_.size());
        morphisms_.push_back(std::move(m));
        return morphisms_.back();
    }

    const Morphism& arrow(Key name, Key from, Key to, Key trigger, Morphism::Handler fn) {
        return add_morphism(Morphism{name, from, to, trigger, std::move(fn), {}, nullptr});
    }

    const Morphism& loop(Key name, Key on, Key trigger, Morphism::Handler fn) {
        return add_morphism(Morphism{name, on, Key{}, trigger, std::move(fn), {}, nullptr});
    }

    // An arrow that says what it does (Declared.hpp): its handler is made
    // from what it says, so the two are one. On one element, or from one to
    // another.
    const Morphism& affine(Key name, Key on, Key trigger, Affine a) { return affine(name, on, Key{}, trigger, std::move(a)); }
    const Morphism& affine(Key name, Key from, Key to, Key trigger, Affine a) {
        auto steps = std::make_shared<const DeclaredSteps>(DeclaredSteps{DeclaredStep{from, to, std::move(a)}});
        Morphism m{name, from, to, trigger,
                   [steps](State&, Element& src, Element* dst, const Event& ev) {
                       sg::run((*steps)[0].does, src, dst ? *dst : src, &ev.args);
                   },
                   {},
                   nullptr};
        m.declared = steps;
        return add_morphism(std::move(m));
    }

    const std::deque<Morphism>& morphisms() const { return morphisms_; }

    const Morphism* morphism(Key name) const {
        auto it = by_name_.find(name);
        return it == by_name_.end() ? nullptr : &morphisms_[it->second];
    }

    // Composition: g . f as one arrow, valid only when cod(f) == dom(g).
    const Morphism& compose(Key name, Key f_name, Key g_name, Key trigger) {
        const Morphism* f = morphism(f_name);
        const Morphism* g = morphism(g_name);
        if (!f || !g) throw std::runtime_error("compose: unknown morphism");
        return add_morphism(composite(name, *f, *g, trigger));
    }

    // The composite arrow itself, unregistered. Each part runs exactly as
    // dispatch would run it on its own - a loop still sees no codomain - so a
    // composite and its parts in order are the same action, which the laws
    // then check rather than assume.
    static Morphism composite(Key name, const Morphism& f, const Morphism& g, Key trigger) {
        if (cod(f) != dom(g))
            throw std::runtime_error("compose: cod(" + f.name.str() + ")=" + cod(f).str() +
                                     " != dom(" + g.name.str() + ")=" + dom(g).str());
        auto fh = f.handler;
        auto gh = g.handler;
        const Key f_to = f.to;
        const Key mid_id = cod(f);
        const Key g_to = g.to;
        const Key end_id = cod(g);
        Morphism m{name,
                   f.from,
                   end_id == f.from ? Key{} : end_id,
                   trigger,
                   [fh, gh, f_to, mid_id, g_to](State& s, Element& from, Element*,
                                                const Event& ev) {
                       if (fh) fh(s, from, f_to.empty() ? nullptr : s.find(f_to), ev);
                       Element* mid = s.find(mid_id);
                       if (!mid) return;
                       if (gh) gh(s, *mid, g_to.empty() ? nullptr : s.find(g_to), ev);
                   },
                   {f.name, g.name},
                   nullptr};
        // Declared if both parts are: what each says, one after the other (an
        // arrow with no handler - an identity - says it does nothing).
        const auto says = [](const Morphism& x) -> const DeclaredSteps* {
            static const DeclaredSteps nothing;
            return x.declared ? x.declared.get() : x.handler ? nullptr : &nothing;
        };
        if (const DeclaredSteps* a = says(f); a) {
            if (const DeclaredSteps* b = says(g); b) {
                auto both = std::make_shared<DeclaredSteps>(*a);
                both->insert(both->end(), b->begin(), b->end());
                m.declared = std::move(both);
            }
        }
        return m;
    }

    // --- trial runs -----------------------------------------------------------
    // A state's data is its elements, its own parameters and what it has queued.
    // Taking and restoring that lets an arrow be run to see what it does, and
    // then un-run. Anything a subclass keeps outside its elements is not data
    // in this sense and is not restored.
    struct Snapshot {
        std::deque<Element> elements;
        Params params;
        std::vector<Event> queue;
        std::vector<Event> said;  // said outward, not yet taken
        std::size_t morphisms = 0;
        uint64_t structure = 0;
        uint64_t removals = 0;
    };

    Snapshot snapshot() const {
        return Snapshot{elements_, params_, bus_.queued(), said_out_, morphisms_.size(), structure_, removals_};
    }

    // Restores in place wherever it can: callers hold `Element&` across frames,
    // and checking a law must not leave those pointing at freed memory. Only
    // if a trial removed an element is the list rebuilt.
    //
    // Content comes back with its stamps, so whatever was worked out from it
    // before the trial still stands after. So does the structure's stamp, when
    // nothing was taken away since the snapshot: what was added is taken off
    // the end again, and every element left is the very one that was there -
    // the list, and every pointer into it, as it was.
    void restore(Snapshot s) {
        bool in_place = s.elements.size() <= elements_.size();
        for (std::size_t i = 0; in_place && i < s.elements.size(); ++i)
            in_place = elements_[i].id == s.elements[i].id;
        const bool same_structure =
            in_place && removals_ == s.removals && morphisms_.size() >= s.morphisms;
        if (in_place) {
            for (std::size_t i = 0; i < s.elements.size(); ++i)
                elements_[i] = std::move(s.elements[i]);
            while (elements_.size() > s.elements.size()) {
                index_.erase(elements_.back().id);
                elements_.pop_back();
            }
        } else {
            elements_ = std::move(s.elements);
            reindex();
            ++removals_;
        }
        params_ = std::move(s.params);
        bus_.requeue(std::move(s.queue));
        said_out_ = std::move(s.said);
        if (morphisms_.size() > s.morphisms) {
            morphisms_.erase(morphisms_.begin() + static_cast<std::ptrdiff_t>(s.morphisms),
                             morphisms_.end());
            index_arrows();
        }
        if (same_structure) structure_ = s.structure;
        else restructured("restore");
        on_restored();
    }

    // Put back as it was (restore): whatever a state keeps that follows from
    // its data - a picture, a cache - made to follow it again.
    virtual void on_restored() {}

    // --- versions -------------------------------------------------------------
    // What the state is made of - its elements and arrows, as a list - stamped
    // like content (see Stamps in Core.hpp): a new stamp whenever an element
    // or an arrow comes or goes. While it holds, a pointer to one of its
    // elements found once is the same element.
    uint64_t structure() const { return structure_; }

    // A version of everything a trial run would read or leave: the structure,
    // the state's own params, every element's params and whether it is alive,
    // what is queued (each event's name, sender and arguments, in order).
    // Equal versions, equal data - so a law that held on this data holds on it
    // still. It costs a pass over the elements and the queue (no copy, no
    // allocation), which is what makes it worth asking before running arrows.
    uint64_t content_version() const {
        uint64_t h = data_version();
        const std::vector<Event>& q = bus_.queued();
        h = mix_stamp(h, q.size());
        for (const Event& e : q) {
            h = mix_stamp(h, std::hash<Key>{}(e.name));
            h = mix_stamp(h, std::hash<Key>{}(e.source));
            h = mix_stamp(h, e.args.stamp());
        }
        h = mix_stamp(h, said_out_.size());
        for (const Event& e : said_out_) {
            h = mix_stamp(h, std::hash<Key>{}(e.name));
            h = mix_stamp(h, e.args.stamp());
        }
        return h;
    }

    // The same, leaving out what is queued: what the state holds, not what it
    // has set in motion.
    uint64_t data_version() const {
        uint64_t h = mix_stamp(0x9e3779b97f4a7c15ull, structure_);
        h = mix_stamp(h, params_.stamp());
        for (const Element& e : elements_) h = mix_stamp(h, (e.params.stamp() << 1) | (e.alive ? 1u : 0u));
        return h;
    }

    // --- events -------------------------------------------------------------
    // An event the state sends itself - from an arrow or a hook. If it is
    // one the state says outward (says), it is also put out for the engine
    // to hand to the graph's transitions.
    void emit(Event e) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("sent an event to a state: ") + e.name.str() + " to " + id_.str());
        if (e.source.empty()) e.source = id_;
        if (e.source == id_ && !said_.empty() && std::find(said_.begin(), said_.end(), e.name) != said_.end())
            said_out_.push_back(e);
        bus_.emit(std::move(e));
    }

    void emit(Key name) { emit(Event{name}); }

    // An event from outside - routed by the engine, carried by a drive: for
    // the state's arrows, never taken for something the state said.
    // Sent by no one in particular, it is sent to the state, as before.
    void hear(Event e) {
        if (detail::observing() > 0) detail::refused_to_observer(std::string("sent an event to a state: ") + e.name.str() + " to " + id_.str());
        if (e.source.empty()) e.source = id_;
        bus_.emit(std::move(e));
    }

    EventBus& bus() { return bus_; }

    // What it says outward: the events it puts on its own bus for whoever
    // holds it to hand on - a game's door out, a machine's alarm. Declared,
    // so the graph can tell where a transition on one of them comes from
    // (StateGraph::sources) and any view of the graph can draw it.
    void says(Key event) {
        if (std::find(said_.begin(), said_.end(), event) == said_.end()) said_.push_back(event);
    }
    const std::vector<Key>& said() const { return said_; }
    // What it has said and the engine has not yet taken: part of its data,
    // so a law's trial that says something says it only to the trial.
    const std::vector<Event>& said_out() const { return said_out_; }
    std::vector<Event> take_said() {
        std::vector<Event> out;
        out.swap(said_out_);
        return out;
    }

    // Drains the queue, cascading up to `max_rounds` times so a handler may emit.
    void dispatch_pending(int max_rounds = 16) {
        for (int round = 0; round < max_rounds && !bus_.empty(); ++round) {
            bus_.drain_into(inbox_);
            for (const Event& ev : inbox_) {
                bool consumed = false;
                watched("on_event", [&] { consumed = on_event(ev); });
                if (consumed) continue;
                apply_morphisms(ev);
            }
        }
    }

    void apply_morphisms(const Event& ev) {
        auto it = by_trigger_.find(ev.name);
        if (it == by_trigger_.end()) return;
        // Copy the bucket: a handler may add morphisms mid-dispatch.
        scratch_ = it->second;
        for (std::size_t i : scratch_) {
            const Morphism& m = morphisms_[i];
            if (!m.handler) continue;
            Element* src = find(m.from);
            if (!src) continue;
            Element* dst = m.to.empty() ? nullptr : find(m.to);
            if (!m.to.empty() && !dst) continue;  // dangling arrow: skip
            m.handler(*this, *src, dst, ev);
        }
    }

    // Dangling arrows: morphism endpoints with no matching element.
    std::vector<std::string> validate() const {
        std::vector<std::string> errors;
        for (const auto& m : morphisms_) {
            if (!find(m.from))
                errors.push_back(id_.str() + "." + m.name.str() + ": domain " + m.from.str() +
                                 " missing");
            if (!m.to.empty() && !find(m.to))
                errors.push_back(id_.str() + "." + m.name.str() + ": codomain " + m.to.str() +
                                 " missing");
        }
        return errors;
    }

private:
    friend class StateGraph;

    template <typename F>
    void watched(const char* hook, F&& run) {
        if (!watching_) {
            run();
            return;
        }
        const uint64_t before = data_version();
        run();
        if (wrote_in_.empty() && data_version() != before) wrote_in_ = hook;
    }

    // What the state is made of is about to change: counted by the graph
    // that holds it, if any (which refuses it while a law's trial runs, before
    // anything is touched), and a new stamp for it.
    void restructured(const char* what) {
        if (revision_) revision_->element(what);
        structure_ = next_stamp();
    }

    // Arrows by trigger (for dispatch) and by name (for everything that names
    // one), found again when the list changes other than at its end.
    void index_arrows() {
        by_trigger_.clear();
        by_name_.clear();
        for (std::size_t i = 0; i < morphisms_.size(); ++i) {
            by_trigger_[morphisms_[i].trigger].push_back(i);
            by_name_.emplace(morphisms_[i].name, i);
        }
    }

    void reindex() {
        index_.clear();
        for (std::size_t i = 0; i < elements_.size(); ++i) index_.emplace(elements_[i].id, i);
    }

    Key id_;
    std::string wrote_in_;  // the first hook seen writing (step, watched)
    bool watching_ = false;
    std::vector<Event> said_out_;
    Params params_;
    std::deque<Element> elements_;
    std::unordered_map<Key, std::size_t> index_;
    std::deque<Morphism> morphisms_;
    std::unordered_map<Key, std::vector<std::size_t>> by_trigger_;
    std::unordered_map<Key, std::size_t> by_name_;
    std::vector<std::size_t> scratch_;
    std::vector<Event> inbox_;
    EventBus bus_;
    std::vector<Key> said_;
    Engine* engine_ = nullptr;
    uint64_t structure_ = next_stamp();
    uint64_t removals_ = 0;  // how often anything was taken out of the lists
    detail::Revision* revision_ = nullptr;  // the graph's count, once it holds this state
};

using StatePtr = std::unique_ptr<State>;

}  // namespace sg
