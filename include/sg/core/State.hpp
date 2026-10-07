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

    // Going through a seam is the domain's to say, the taking of it the
    // engine's. `passage` is where whoever crosses this state's boundaries
    // is now (a room's: its eye); `passed` whether, from `before`, they have
    // gone through `boundary` since. The engine asks after every frame's
    // step, and takes the seam's own transition in that frame: no frame is
    // ever seen from past a doorway in the room it leads out of. A state
    // with no boundaries to cross says nothing.
    virtual Params passage() const { return {}; }
    virtual bool passed(const Params& /*before*/, Key /*boundary*/) const { return false; }
    // What whoever crosses meets at `boundary`, from this side: the domain's
    // account of its half of a seam's overlap, in the boundary's own frame
    // facing out through it (x out, y its up, z across). Two sides glued give
    // the same account, the far one turned through the doorway
    // (laws::overlaps): a seam is seamless unless it says what may differ.
    //   walk           1 if it is a doorway walked through
    //   leave          0 if it is only a way in: not walked out of from here
    //   down.x/y/z     which way the pull is, just inside (a unit vector)
    //   floor          how far the ground is below the opening's foot, at
    //                  its threshold - where whoever crosses lands
    //   eye            how high above it whoever crosses carries their eye
    virtual Params overlap(Key /*boundary*/) const { return {}; }

    // The frame order is part of the contract, hence non-virtual. With
    // `watch`, an on_update that changes the state's data - rather than
    // emitting an event for an arrow to act on - is noted (wrote_in_update):
    // behaviour is arrows, and time reaches a state through a drive.
    void step(const Tick& t, bool watch = false);
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
    Element& add_element(Element e);

    Element& add_element(Key id, Key kind) { return add_element(Element{id, kind}); }

    // An element by its name. inline: every arrow, every frame, looks one up.
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

    void remove_element(Key id);

    // Take an element away together with its arrows: an arrow from or to
    // something that is not there is not an arrow. (remove_element alone
    // leaves them dangling, for validate() to name.)
    void remove_with_arrows(Key id);

    ElementRange elements() { return ElementRange{elements_}; }
    const std::deque<Element>& elements() const { return elements_; }

    // --- morphisms (arrows between elements) --------------------------------
    // An arrow, once added, is what it was declared: its ends and its action
    // are read, never rewritten in place - to change an arrow is to take it
    // away and add another, which the structure's stamp (and the graph)
    // notices.
    const Morphism& add_morphism(Morphism m);

    // `native`: which native computation the handler is, if a source said (Morphism::native).
    const Morphism& arrow(Key name, Key from, Key to, Key trigger, Morphism::Handler fn, Key native = Key{});

    const Morphism& loop(Key name, Key on, Key trigger, Morphism::Handler fn, Key native = Key{});

    // An arrow that says what it does (Declared.hpp): its handler is made
    // from what it says, so the two are one. On one element, or from one to
    // another.
    const Morphism& affine(Key name, Key on, Key trigger, Affine a) { return affine(name, on, Key{}, trigger, std::move(a)); }
    const Morphism& affine(Key name, Key from, Key to, Key trigger, Affine a);

    const std::deque<Morphism>& morphisms() const { return morphisms_; }

    const Morphism* morphism(Key name) const;

    // Composition: g . f as one arrow, valid only when cod(f) == dom(g).
    const Morphism& compose(Key name, Key f_name, Key g_name, Key trigger);

    // The composite arrow itself, unregistered. Each part runs exactly as
    // dispatch would run it on its own - a loop still sees no codomain - so a
    // composite and its parts in order are the same action, which the laws
    // then check rather than assume.
    static Morphism composite(Key name, const Morphism& f, const Morphism& g, Key trigger);

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
        // The arrows themselves, when the snapshot is a start (start()).
        std::shared_ptr<const std::deque<Morphism>> arrows;
    };

    Snapshot snapshot() const;
    // A start to come back to (StateGraph::keep_default): the snapshot and its
    // arrows. A trial only ever adds arrows, so how many there were is enough
    // to undo it; a run may take some away and add others, and only the
    // arrows kept bring the state back to what it was.
    Snapshot start() const;

    // Restores in place wherever it can: callers hold `Element&` across frames,
    // and checking a law must not leave those pointing at freed memory. Only
    // if a trial removed an element is the list rebuilt.
    //
    // Content comes back with its stamps, so whatever was worked out from it
    // before the trial still stands after. So does the structure's stamp, when
    // nothing was taken away since the snapshot: what was added is taken off
    // the end again, and every element left is the very one that was there -
    // the list, and every pointer into it, as it was.
    void restore(Snapshot s);

    // Put back as it was (restore): whatever a state keeps that follows from
    // its data - a picture, a cache - made to follow it again.
    virtual void on_restored() {}

    // What is wrong with it as it is now, by its own account: data that
    // disagrees with itself (a body whose meant pose does not fit its own
    // bones). Each state answers for itself; validate() only collects the
    // answers, so the engine's watch says them at start and `verify` refuses
    // them in a project's tests.
    virtual std::vector<std::string> faults() const { return {}; }

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
    uint64_t content_version() const;

    // The same, leaving out what is queued: what the state holds, not what it
    // has set in motion.
    uint64_t data_version() const;

    // --- events -------------------------------------------------------------
    // An event the state sends itself - from an arrow or a hook. If it is
    // one the state says outward (says), it is also put out for the engine
    // to hand to the graph's transitions.
    void emit(Event e);

    void emit(Key name) { emit(Event{name}); }

    // An event from outside - routed by the engine, carried by a drive: for
    // the state's arrows, never taken for something the state said.
    // Sent by no one in particular, it is sent to the state, as before.
    void hear(Event e);

    EventBus& bus() { return bus_; }

    // What it says outward: the events it puts on its own bus for whoever
    // holds it to hand on - a game's door out, a machine's alarm. Declared,
    // so the graph can tell where a transition on one of them comes from
    // (StateGraph::sources) and any view of the graph can draw it.
    void says(Key event);
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
    void dispatch_pending(int max_rounds = 16);

    void apply_morphisms(const Event& ev);

    // Dangling arrows: morphism endpoints with no matching element.
    std::vector<std::string> validate() const;

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
    void index_arrows();

    void reindex();

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
