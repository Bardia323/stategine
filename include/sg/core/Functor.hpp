// Stategine - functors between states.
//
// Each State is a small category: elements are objects, morphisms are arrows.
// A Functor F : A -> B is the structure-preserving way to move data across a
// transition or a portal:
//
//   F_obj : Ob(A) -> Ob(B)      which element of B receives which element of A
//   F_mor : Hom(A) -> Hom(B)    which arrow of B corresponds to one of A
//
// `check_laws` verifies that every mapped arrow f : x -> y has an image
// F(f) : F(x) -> F(y) in the target. Whether F(f) also *does* what f does,
// and whether composites and identities behave on real data, is for the laws
// in Laws.hpp: construction is where a law is kept, not where it is proven.
#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/State.hpp"

namespace sg {

// How one object's parameters become another's. Reusable across domains: a
// transport knows nothing about 2D, 3D or text, only about parameter names.
using Transport = std::function<void(const Element& src, Element& dst)>;

namespace transport {

// Everything crosses unchanged.
inline void copy_all(const Element& s, Element& d) {
    for (const auto& kv : s.params) d.params.set(kv.first, kv.second);
}

// Only the named parameters cross.
inline Transport only(std::vector<Key> names) {
    return [names = std::move(names)](const Element& s, Element& d) {
        for (Key k : names)
            if (s.params.has(k)) d.params.set(k, s.params.get(k));
    };
}

// Rename on the way over: {destination, source}. This is the whole content of
// "the map's y axis is the world's z axis", and it works for any pair of
// domains that share the parameter vocabulary.
inline Transport swizzle(std::vector<std::pair<Key, Key>> pairs, bool copy_rest = false) {
    return [pairs = std::move(pairs), copy_rest](const Element& s, Element& d) {
        if (copy_rest) copy_all(s, d);
        for (const auto& p : pairs)
            if (s.params.has(p.second)) d.params.set(p.first, s.params.get(p.second));
    };
}

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

class Functor {
public:
    Functor() = default;
    Functor(Key name, Key from, Key to) : name_(name), from_(from), to_(to) {}

    Key name() const { return name_; }
    void rename(Key n) { name_ = n; }
    Key from() const { return from_; }  // source state
    Key to() const { return to_; }      // target state

    // --- object map ---------------------------------------------------------
    Functor& on_object(Key src_element, Key dst_element, Transport t = nullptr) {
        refuse_if_identity("on_object");
        obj_[src_element] = ObjMap{dst_element, std::move(t)};
        return *this;
    }

    // --- arrow map ----------------------------------------------------------
    Functor& on_morphism(Key src_morphism, Key dst_morphism) {
        refuse_if_identity("on_morphism");
        mor_[src_morphism] = dst_morphism;
        return *this;
    }

    // Events emitted in the source are relabelled on the way into the target.
    Functor& on_event(Key src_event, Key dst_event) {
        evt_[src_event] = dst_event;
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
            return h;
        }
        Functor h(name, f.from_, g.to_);
        for (const auto& kv : f.obj_) {
            auto mid = g.obj_.find(kv.second.dst);
            if (mid == g.obj_.end()) continue;  // outside G's image: dropped
            Transport tf = kv.second.transport;
            Transport tg = mid->second.transport;
            h.obj_[kv.first] = ObjMap{mid->second.dst,
                                      [tf, tg](const Element& s, Element& d) {
                                          Element scratch(s.id, s.kind);
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
                                      }};
        }
        for (const auto& kv : f.mor_) {
            auto mid = g.mor_.find(kv.second);
            if (mid != g.mor_.end()) h.mor_[kv.first] = mid->second;
        }
        for (const auto& kv : f.evt_) h.evt_[kv.first] = g.image_event(kv.second);
        return h;
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
    };

    void refuse_if_identity(const char* what) const {
        if (identity_)
            throw std::runtime_error("functor " + name_.str() + ": " + what +
                                     " on an identity would make it something else");
    }

    Key name_;
    Key from_;
    Key to_;
    bool identity_ = false;
    std::unordered_map<Key, ObjMap> obj_;
    std::unordered_map<Key, Key> mor_;
    std::unordered_map<Key, Key> evt_;
};

}  // namespace sg
