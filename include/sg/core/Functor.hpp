// Stategine - functors between states.
//
// Each State is a small category: elements are objects, morphisms are arrows.
// A Functor F : A -> B is the structure-preserving way to move data across a
// transition or a portal:
//
//   F_obj : Ob(A) -> Ob(B)      which element of B receives which element of A
//   F_mor : Hom(A) -> Hom(B)    which arrow of B corresponds to one of A
//
// F is partial: it is defined on the objects and arrows it maps - a piece of A,
// not necessarily all of it - and an object or arrow it does not map is
// outside its domain (`image_object` / `image_morphism` give an empty key).
// The identity functor is the one that maps everything.
//
// `check_laws` verifies that every mapped arrow f : x -> y has an image
// F(f) : F(x) -> F(y) in the target. Whether F(f) also *does* what f does,
// and whether composites and identities behave on real data, is for the laws
// in Laws.hpp: construction is where a law is kept, not where it is proven.
#pragma once

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/State.hpp"

namespace sg {

// How one object's parameters become another's. Reusable across domains: a
// transport knows nothing about 2D, 3D or text, only about parameter names.
using Transport = std::function<void(const Element& src, Element& dst)>;

namespace transport {

// A transport that says what it does (Declared.hpp): stages, each from what
// the last left through an element made fresh between them. What it runs is
// what it says - its function is made from it.
struct Declared {
    std::shared_ptr<const Stages> stages;
    static Declared of(Affine a) { return Declared{std::make_shared<const Stages>(Stages{std::move(a)})}; }
    inline void operator()(const Element& s, Element& d) const;
};

// Everything crosses unchanged.
inline const Declared copy_all = Declared::of([] {
    Affine a;
    a.copy_all = true;
    return a;
}());

// Only the named parameters cross.
inline Declared only(std::vector<Key> names) {
    Affine a;
    for (Key k : names) a.copy(k, k);
    return Declared::of(std::move(a));
}

// Rename on the way over: {destination, source}. This is the whole content of
// "the map's y axis is the world's z axis", and it works for any pair of
// domains that share the parameter vocabulary.
inline Declared swizzle(std::vector<std::pair<Key, Key>> pairs, bool copy_rest = false) {
    Affine a;
    a.copy_all = copy_rest;
    for (const auto& p : pairs) a.copy(p.first, p.second);
    return Declared::of(std::move(a));
}

// Affine on the way over: what it says (Declared.hpp), declared.
inline Declared affine(Affine a) { return Declared::of(std::move(a)); }

// Rename plus a scalar map on each value, for unit changes (cells <-> metres).
inline Transport swizzle_scaled(std::vector<std::pair<Key, Key>> pairs,
                                std::function<double(double)> fn, bool copy_rest = false) {
    return [pairs = std::move(pairs), fn = std::move(fn), copy_rest](const Element& s,
                                                                    Element& d) {
        if (copy_rest) copy_all(s, d);
        for (const auto& p : pairs)
            if (s.params.has(p.second)) d.params.set(p.first, fn(s.params.num(p.second)));
    };
}

// Run one transport, then another, on the same pair of elements.
inline Transport then(Transport a, Transport b) {
    return [a = std::move(a), b = std::move(b)](const Element& s, Element& d) {
        if (a) a(s, d);
        if (b) b(s, d);
    };
}

}  // namespace transport

// The element halfway along a composite transport: made fresh each time, as
// far as anything can tell - its id and kind, no params, alive - but in a
// buffer that is kept, so carrying through a composite does not allocate once
// warm. A composite made of composites is carrying through several at once,
// so each one mid-carry has a buffer of its own.
class Middle {
public:
    Middle() : slot_(in_use()++) {
        auto& p = pool();
        if (p.size() <= slot_) p.emplace_back(new Element());
    }
    ~Middle() { --in_use(); }
    Middle(const Middle&) = delete;
    Middle& operator=(const Middle&) = delete;

    Element& element(const Element& like) {
        Element& e = *pool()[slot_];
        e.remake(like.id, like.kind);
        e.alive = true;
        e.params.clear();
        return e;
    }

private:
    static std::size_t& in_use() {
        static thread_local std::size_t n = 0;
        return n;
    }
    static std::vector<std::unique_ptr<Element>>& pool() {
        static thread_local std::vector<std::unique_ptr<Element>> p;
        return p;
    }
    std::size_t slot_;
};

namespace transport {
// Stages run in turn, through an element made fresh between each two.
inline void run_stages(const Stages& st, std::size_t i, const Element& s, Element& d) {
    if (i + 1 >= st.size()) {
        if (i < st.size()) sg::run(st[i], s, d);
        return;
    }
    Middle held;
    Element& mid = held.element(s);
    sg::run(st[i], s, mid);
    run_stages(st, i + 1, mid, d);
}
inline void Declared::operator()(const Element& s, Element& d) const {
    if (stages) run_stages(*stages, 0, s, d);
}
}  // namespace transport

class Functor {
public:
    Functor() = default;
    Functor(Key name, Key from, Key to) : name_(name), from_(from), to_(to) {}
    // A copy is the same maps, belonging to no graph until one takes it; one
    // put in place of another keeps the place's graph.
    Functor(const Functor& o)
        : name_(o.name_), from_(o.from_), to_(o.to_), identity_(o.identity_), stamp_(o.stamp_),
          obj_(o.obj_), mor_(o.mor_), evt_(o.evt_) {}
    Functor(Functor&& o) noexcept
        : name_(o.name_), from_(o.from_), to_(o.to_), identity_(o.identity_), stamp_(o.stamp_),
          obj_(std::move(o.obj_)), mor_(std::move(o.mor_)), evt_(std::move(o.evt_)) {}
    Functor& operator=(const Functor& o) {
        if (this != &o) *this = Functor(o);
        return *this;
    }
    // One put in place of a functor a graph holds keeps its name - the graph
    // finds it by that name - and the graph counts it, like any rewiring.
    Functor& operator=(Functor&& o) {
        if (revision_ && o.name_ != name_)
            throw std::runtime_error("functor " + name_.str() + " is held by a graph under that name; " +
                                     "put " + o.name_.str() + " in with StateGraph::set_functor");
        if (revision_) revision_->rewired("assign");
        name_ = o.name_;
        from_ = o.from_;
        to_ = o.to_;
        identity_ = o.identity_;
        stamp_ = o.stamp_;
        obj_ = std::move(o.obj_);
        mor_ = std::move(o.mor_);
        evt_ = std::move(o.evt_);
        return *this;
    }

    Key name() const { return name_; }
    // A functor's name is its identity in the graph that holds it, which
    // finds it by that name: only one no graph holds yet is renamed.
    void rename(Key n) {
        if (revision_ && n != name_)
            throw std::runtime_error("functor " + name_.str() + " is held by a graph, which knows it by that name");
        name_ = n;
        remapped("rename");
    }
    // Which version of its maps this functor is: new whenever an object, an
    // arrow or an event is mapped, carried along when it is copied.
    uint64_t stamp() const { return stamp_; }
    Key from() const { return from_; }  // source state
    Key to() const { return to_; }      // target state

    // --- object map ---------------------------------------------------------
    Functor& on_object(Key src_element, Key dst_element, Transport t = nullptr) {
        refuse_if_identity("on_object");
        // No transport is every parameter, as it is: said so.
        const bool said = !t;
        obj_[src_element] = ObjMap{dst_element, std::move(t), said ? transport::copy_all.stages : nullptr};
        remapped("on_object");
        return *this;
    }
    // A transport that says what it does: kept with what it says.
    Functor& on_object(Key src_element, Key dst_element, transport::Declared t) {
        refuse_if_identity("on_object");
        obj_[src_element] = ObjMap{dst_element, Transport(t), t.stages};
        remapped("on_object");
        return *this;
    }

    // --- arrow map ----------------------------------------------------------
    Functor& on_morphism(Key src_morphism, Key dst_morphism) {
        refuse_if_identity("on_morphism");
        mor_[src_morphism] = dst_morphism;
        remapped("on_morphism");
        return *this;
    }

    // Events emitted in the source are relabelled on the way into the target.
    Functor& on_event(Key src_event, Key dst_event) {
        evt_[src_event] = dst_event;
        remapped("on_event");
        return *this;
    }

    Key image_object(Key id) const {
        if (identity_) return id;
        auto it = obj_.find(id);
        return it == obj_.end() ? Key{} : it->second.dst;
    }

    Key image_morphism(Key id) const {
        if (identity_) return id;
        auto it = mor_.find(id);
        return it == mor_.end() ? Key{} : it->second;
    }

    // The identity is a law, not a table: it fixes every object the state has
    // or will have. A copy of the object list taken when it was built would
    // stop being the identity the moment the state grew.
    bool is_identity() const { return identity_; }

    // Walk the arrow map.
    template <typename Fn>
    void for_each_morphism(Fn&& fn) const {
        for (const auto& kv : mor_) fn(kv.first, kv.second);
    }

    // Whether it names this event - declares where it goes - rather than
    // letting it pass as itself.
    bool maps_event(Key name) const { return evt_.count(name) != 0; }
    bool maps_events() const { return !evt_.empty(); }

    // An event, carried across: relabelled, and sent by the functor from its
    // source (the sender a transition's carried event has too).
    Event carried(const Event& e, const State& src) const {
        Event out = e;
        out.name = image_event(e.name);
        out.source = Key{src.id().str() + "/" + name_.str()};
        return out;
    }

    Key image_event(Key name) const {
        auto it = evt_.find(name);
        return it == evt_.end() ? name : it->second;
    }

    // Walk the object map. Whoever applies a functor to live state wants to
    // know what it will write before it writes it.
    template <typename Fn>
    void for_each_object(Fn&& fn) const {
        for (const auto& kv : obj_) fn(kv.first, kv.second.dst);
    }
    // And with what each says it does (Declared.hpp) - null, where it says
    // nothing - in the order `apply` carries them.
    template <typename Fn>
    void for_each_declared(Fn&& fn) const {
        for (const auto& kv : obj_) fn(kv.first, kv.second.dst, kv.second.declared.get());
    }

    // --- application --------------------------------------------------------
    // Push every mapped object of `src` into `dst`, creating targets as needed.
    void apply(const State& src, State& dst) const {
        if (identity_) {
            if (&src == &dst) return;
            for (const auto& s : src.elements()) {
                Element* d = dst.find(s.id);
                if (!d) d = &dst.add_element(s.id, s.kind);
                transport::copy_all(s, *d);
            }
            return;
        }
        for (const auto& kv : obj_) {
            const Element* s = src.find(kv.first);
            if (!s) continue;
            Element* d = dst.find(kv.second.dst);
            if (!d) d = &dst.add_element(kv.second.dst, s->kind);
            if (kv.second.transport) {
                kv.second.transport(*s, *d);
            } else {
                transport::copy_all(*s, *d);
            }
        }
    }

    // --- application, only where something changed -------------------------------
    // What `apply` would do, done only for the objects whose source or target
    // has changed since the last time - which is all `apply` would change,
    // for a transport that is what a transport should be: a function of the
    // two elements' params, the same one each time (`put-put` checks it).
    //
    // A Memo is how the last time is remembered: which element went to which,
    // found once, and the stamps both held after the transport ran (see
    // Stamps in Core.hpp). It owns nothing and means nothing; thrown away, it
    // is built again from the functor and the two states, by one full apply.
    // It is rebuilt whenever the functor, or either state's list of elements,
    // is not what it was built on.
    struct Memo {
        struct Pair {
            const Element* src;
            Element* dst;
            const Transport* transport;
            uint64_t src_stamp, dst_stamp;
        };
        const Functor* functor = nullptr;
        const State* src = nullptr;
        const State* dst = nullptr;
        uint64_t functor_stamp = 0, src_structure = 0, dst_structure = 0;
        uint64_t seen = 0;  // last_stamp() after the last pass: nothing since, nothing to do
        std::vector<Pair> pairs;
        bool built = false;
        void clear() { built = false; }
    };

    // Returns how many objects were carried (every one, when the memo was
    // built afresh).
    std::size_t apply(const State& src, State& dst, Memo& m) const {
        if (!m.built || m.functor != this || m.src != &src || m.dst != &dst ||
            m.functor_stamp != stamp_ || m.src_structure != src.structure() ||
            m.dst_structure != dst.structure()) {
            apply(src, dst);
            build(src, dst, m);
            return m.pairs.size();
        }
        if (m.seen == last_stamp()) return 0;
        std::size_t carried = 0;
        for (Memo::Pair& p : m.pairs) {
            if (p.src->params.stamp() == p.src_stamp && p.dst->params.stamp() == p.dst_stamp) continue;
            if (p.transport && *p.transport) {
                (*p.transport)(*p.src, *p.dst);
            } else {
                transport::copy_all(*p.src, *p.dst);
            }
            p.src_stamp = p.src->params.stamp();
            p.dst_stamp = p.dst->params.stamp();
            ++carried;
        }
        // A transport that wrote a target another pair reads makes that pair
        // look again next time, as it must.
        m.seen = last_stamp();
        return carried;
    }

    // Same, plus one event - the one that took a transition - relabelled into
    // the target's queue.
    void apply(const State& src, State& dst, const Event& e) const {
        apply(src, dst);
        dst.hear(carried(e, src));
    }

    // Same, plus relabelled events forwarded into the target's queue.
    void apply(const State& src, State& dst, const std::vector<Event>& carry) const {
        apply(src, dst);
        for (const Event& e : carry) {
            Event out = e;
            out.name = image_event(e.name);
            out.source = Key{src.id().str() + "/" + name_.str()};
            dst.emit(std::move(out));
        }
    }

    // --- composition --------------------------------------------------------
    // G . F : A -> C, defined when cod(F) == dom(G).
    static Functor compose(const Functor& f, const Functor& g, Key name = Key{}) {
        if (f.to_ != g.from_)
            throw std::runtime_error("functor compose: cod(" + f.name_.str() + ")=" +
                                     f.to_.str() + " != dom(" + g.name_.str() + ")=" +
                                     g.from_.str());
        if (name.empty()) name = Key{g.name_.str() + "." + f.name_.str()};
        // Identities are units for composition by construction, not by luck.
        if (f.identity_ || g.identity_) {
            Functor h = f.identity_ ? g : f;
            h.name_ = name;
            h.evt_ = compose_events(f, g);
            return h;
        }
        Functor h(name, f.from_, g.to_);
        for (const auto& kv : f.obj_) {
            auto mid = g.obj_.find(kv.second.dst);
            if (mid == g.obj_.end()) continue;  // outside G's image: dropped
            Transport tf = kv.second.transport;
            Transport tg = mid->second.transport;
            // Declared if both are: the one's stages, then the other's.
            std::shared_ptr<const Stages> said;
            if (kv.second.declared && mid->second.declared) {
                auto both = std::make_shared<Stages>(*kv.second.declared);
                both->insert(both->end(), mid->second.declared->begin(), mid->second.declared->end());
                said = std::move(both);
            }
            h.obj_[kv.first] = ObjMap{mid->second.dst,
                                      [tf, tg](const Element& s, Element& d) {
                                          Middle held;
                                          Element& scratch = held.element(s);
                                          if (tf) {
                                              tf(s, scratch);
                                          } else {
                                              transport::copy_all(s, scratch);
                                          }
                                          if (tg) {
                                              tg(scratch, d);
                                          } else {
                                              transport::copy_all(scratch, d);
                                          }
                                      },
                                      std::move(said)};
        }
        for (const auto& kv : f.mor_) {
            auto mid = g.mor_.find(kv.second);
            if (mid != g.mor_.end()) h.mor_[kv.first] = mid->second;
        }
        h.evt_ = compose_events(f, g);
        return h;
    }

    // (G . F)(e) = G(F(e)), for every event - and an event F does not name it
    // passes as itself, so what G makes of that is the composite's too.
    static std::unordered_map<Key, Key> compose_events(const Functor& f, const Functor& g) {
        std::unordered_map<Key, Key> out;
        for (const auto& kv : f.evt_) out[kv.first] = g.image_event(kv.second);
        for (const auto& kv : g.evt_)
            if (!f.evt_.count(kv.first)) out[kv.first] = kv.second;
        for (auto it = out.begin(); it != out.end();)
            it = it->first == it->second ? out.erase(it) : std::next(it);
        return out;
    }

    // g * f reads "g after f".
    friend Functor operator*(const Functor& g, const Functor& f) { return compose(f, g); }

    // Identity functor on a state: every object and arrow to itself, including
    // the ones the state does not have yet.
    static Functor identity(Key state, Key name = Key{}) {
        if (name.empty()) name = Key{"id_" + state.str()};
        Functor id(name, state, state);
        id.identity_ = true;
        return id;
    }

    static Functor identity(const State& s, Key name = Key{}) { return identity(s.id(), name); }

    // --- law check ----------------------------------------------------------
    std::vector<std::string> check_laws(const State& src, const State& dst) const {
        std::vector<std::string> errors;
        const std::string tag = "functor " + name_.str() + ": ";
        if (identity_) {
            if (src.id() != from_ || dst.id() != to_ || from_ != to_)
                errors.push_back(tag + "an identity must be on one state");
            return errors;
        }
        for (const auto& kv : obj_)
            if (!src.find(kv.first))
                errors.push_back(tag + "object " + kv.first.str() + " missing in " +
                                 src.id().str());

        for (const auto& kv : mor_) {
            const Morphism* f = src.morphism(kv.first);
            if (!f) {
                errors.push_back(tag + "arrow " + kv.first.str() + " missing in " +
                                 src.id().str());
                continue;
            }
            const Morphism* img = dst.morphism(kv.second);
            if (!img) {
                errors.push_back(tag + "image arrow " + kv.second.str() + " missing in " +
                                 dst.id().str());
                continue;
            }
            // Endomorphisms are arrows x -> x like any other: `to` is left empty
            // only as a storage convenience, so compare codomains, not fields.
            const Key want_dom = image_object(dom(*f));
            const Key want_cod = image_object(cod(*f));
            if (want_dom.empty()) {
                errors.push_back(tag + "domain " + dom(*f).str() + " of " + kv.first.str() +
                                 " unmapped");
            } else if (dom(*img) != want_dom) {
                errors.push_back(tag + "F(" + kv.first.str() + ") has domain " + dom(*img).str() +
                                 ", expected " + want_dom.str());
            }
            if (want_cod.empty()) {
                errors.push_back(tag + "codomain " + cod(*f).str() + " of " + kv.first.str() +
                                 " unmapped");
            } else if (cod(*img) != want_cod) {
                errors.push_back(tag + "F(" + kv.first.str() + ") has codomain " + cod(*img).str() +
                                 ", expected " + want_cod.str());
            }
        }
        return errors;
    }

private:
    struct ObjMap {
        Key dst;
        Transport transport;
        std::shared_ptr<const Stages> declared;  // what it says it does; null: nothing said
    };

    void build(const State& src, State& dst, Memo& m) const {
        m.pairs.clear();
        const auto pair = [&](const Element& s, Key to, const Transport* t) {
            if (Element* d = dst.find(to)) m.pairs.push_back({&s, d, t, s.params.stamp(), d->params.stamp()});
        };
        if (identity_) {
            if (&src != &dst)
                for (const auto& s : src.elements()) pair(s, s.id, nullptr);
        } else {
            for (const auto& kv : obj_)
                if (const Element* s = src.find(kv.first)) pair(*s, kv.second.dst, &kv.second.transport);
        }
        m.functor = this;
        m.src = &src;
        m.dst = &dst;
        m.functor_stamp = stamp_;
        m.src_structure = src.structure();
        m.dst_structure = dst.structure();
        m.seen = last_stamp();
        m.built = true;
    }

    friend class StateGraph;

    // Its maps changed: a new stamp, and a change to the structure of the
    // graph that holds it, if any.
    void remapped(const char* what) {
        if (revision_) revision_->rewired(what);
        stamp_ = next_stamp();
    }

    void refuse_if_identity(const char* what) const {
        if (identity_)
            throw std::runtime_error("functor " + name_.str() + ": " + what +
                                     " on an identity would make it something else");
    }

    Key name_;
    Key from_;
    Key to_;
    bool identity_ = false;
    uint64_t stamp_ = next_stamp();
    std::unordered_map<Key, ObjMap> obj_;
    std::unordered_map<Key, Key> mor_;
    std::unordered_map<Key, Key> evt_;
    detail::Revision* revision_ = nullptr;  // the graph's count, once a graph holds it
};

}  // namespace sg
