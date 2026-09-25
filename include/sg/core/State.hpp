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

namespace sg {

class Engine;

// Per-tick context handed to every state.
struct Tick {
    double dt = 0.0;    // seconds since the previous frame
    double time = 0.0;  // seconds since the engine started
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

    Engine* engine() const { return engine_; }
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

    // The frame order is part of the contract, hence non-virtual.
    void step(const Tick& t) {
        on_update(t);
        dispatch_pending();
        on_render(t);
    }

    // --- elements (objects) -------------------------------------------------
    Element& add_element(Element e) {
        if (index_.count(e.id)) throw std::runtime_error("duplicate element " + e.id.str());
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
        elements_.erase(elements_.begin() + static_cast<std::ptrdiff_t>(it->second));
        index_.erase(it);
        reindex();
    }

    // Take an element away together with its arrows: an arrow from or to
    // something that is not there is not an arrow. (remove_element alone
    // leaves them dangling, for validate() to name.)
    void remove_with_arrows(Key id) {
        remove_element(id);
        const std::size_t had = morphisms_.size();
        for (std::size_t i = morphisms_.size(); i-- > 0;)
            if (morphisms_[i].from == id || morphisms_[i].to == id)
                morphisms_.erase(morphisms_.begin() + static_cast<std::ptrdiff_t>(i));
        if (morphisms_.size() != had) {
            by_trigger_.clear();
            for (std::size_t i = 0; i < morphisms_.size(); ++i) by_trigger_[morphisms_[i].trigger].push_back(i);
        }
    }

    std::deque<Element>& elements() { return elements_; }
    const std::deque<Element>& elements() const { return elements_; }

    // --- morphisms (arrows between elements) --------------------------------
    Morphism& add_morphism(Morphism m) {
        if (m.name.empty()) throw std::runtime_error("morphism needs a name");
        by_trigger_[m.trigger].push_back(morphisms_.size());
        morphisms_.push_back(std::move(m));
        return morphisms_.back();
    }

    Morphism& arrow(Key name, Key from, Key to, Key trigger, Morphism::Handler fn) {
        return add_morphism(Morphism{name, from, to, trigger, std::move(fn), {}});
    }

    Morphism& loop(Key name, Key on, Key trigger, Morphism::Handler fn) {
        return add_morphism(Morphism{name, on, Key{}, trigger, std::move(fn), {}});
    }

    const std::deque<Morphism>& morphisms() const { return morphisms_; }

    const Morphism* morphism(Key name) const {
        for (const auto& m : morphisms_)
            if (m.name == name) return &m;
        return nullptr;
    }

    // Composition: g . f as one arrow, valid only when cod(f) == dom(g).
    Morphism& compose(Key name, Key f_name, Key g_name, Key trigger) {
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
        return Morphism{name,
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
                        {f.name, g.name}};
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
        std::size_t morphisms = 0;
    };

    Snapshot snapshot() const {
        return Snapshot{elements_, params_, bus_.queued(), morphisms_.size()};
    }

    // Restores in place wherever it can: callers hold `Element&` across frames,
    // and checking a law must not leave those pointing at freed memory. Only
    // if a trial removed an element is the list rebuilt.
    void restore(Snapshot s) {
        bool in_place = s.elements.size() <= elements_.size();
        for (std::size_t i = 0; in_place && i < s.elements.size(); ++i)
            in_place = elements_[i].id == s.elements[i].id;
        if (in_place) {
            for (std::size_t i = 0; i < s.elements.size(); ++i)
                elements_[i] = std::move(s.elements[i]);
            while (elements_.size() > s.elements.size()) elements_.pop_back();
        } else {
            elements_ = std::move(s.elements);
        }
        params_ = std::move(s.params);
        bus_.requeue(std::move(s.queue));
        reindex();
        if (morphisms_.size() > s.morphisms) {
            morphisms_.erase(morphisms_.begin() + static_cast<std::ptrdiff_t>(s.morphisms),
                             morphisms_.end());
            by_trigger_.clear();
            for (std::size_t i = 0; i < morphisms_.size(); ++i)
                by_trigger_[morphisms_[i].trigger].push_back(i);
        }
        on_restored();
    }

    // Put back as it was (restore): whatever a state keeps that follows from
    // its data - a picture, a cache - made to follow it again.
    virtual void on_restored() {}

    // --- events -------------------------------------------------------------
    void emit(Event e) {
        if (e.source.empty()) e.source = id_;
        bus_.emit(std::move(e));
    }

    void emit(Key name) { emit(Event{name}); }

    EventBus& bus() { return bus_; }

    // Drains the queue, cascading up to `max_rounds` times so a handler may emit.
    void dispatch_pending(int max_rounds = 16) {
        for (int round = 0; round < max_rounds && !bus_.empty(); ++round) {
            bus_.drain_into(inbox_);
            for (const Event& ev : inbox_) {
                if (on_event(ev)) continue;
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
    void reindex() {
        index_.clear();
        for (std::size_t i = 0; i < elements_.size(); ++i) index_.emplace(elements_[i].id, i);
    }

    Key id_;
    Params params_;
    std::deque<Element> elements_;
    std::unordered_map<Key, std::size_t> index_;
    std::deque<Morphism> morphisms_;
    std::unordered_map<Key, std::vector<std::size_t>> by_trigger_;
    std::vector<std::size_t> scratch_;
    std::vector<Event> inbox_;
    EventBus bus_;
    Engine* engine_ = nullptr;
};

using StatePtr = std::unique_ptr<State>;

}  // namespace sg
