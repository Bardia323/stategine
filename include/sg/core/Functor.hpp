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
// F(f) : F(x) -> F(y) in the target; composites built with `compose` / `*`
// preserve composition and identities by construction.
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
        obj_[src_element] = ObjMap{dst_element, std::move(t)};
        return *this;
    }

    // Map a whole family at once: ids in the source, ids in the target derived
    // by a naming rule. Typical for "one token per box".
    Functor& on_objects(const std::vector<Key>& src_elements,
                        const std::function<Key(Key)>& rename_fn, Transport t = nullptr) {
        for (Key s : src_elements) obj_[s] = ObjMap{rename_fn(s), t};
        return *this;
    }

    // --- arrow map ----------------------------------------------------------
    Functor& on_morphism(Key src_morphism, Key dst_morphism) {
        mor_[src_morphism] = dst_morphism;
        return *this;
    }

    // Events emitted in the source are relabelled on the way into the target.
    Functor& on_event(Key src_event, Key dst_event) {
        evt_[src_event] = dst_event;
        return *this;
    }

    bool maps_object(Key id) const { return obj_.count(id) != 0; }

    Key image_object(Key id) const {
        auto it = obj_.find(id);
        return it == obj_.end() ? Key{} : it->second.dst;
    }

    Key image_morphism(Key name) const {
        auto it = mor_.find(name);
        return it == mor_.end() ? Key{} : it->second;
    }

    Key image_event(Key name) const {
        auto it = evt_.find(name);
        return it == evt_.end() ? name : it->second;
    }

    std::size_t object_count() const { return obj_.size(); }

    // Walk the object map. Whoever applies a functor to live state wants to
    // know what it will write before it writes it.
    template <typename Fn>
    void for_each_object(Fn&& fn) const {
        for (const auto& kv : obj_) fn(kv.first, kv.second.dst);
    }

    // --- application --------------------------------------------------------
    // Push every mapped object of `src` into `dst`, creating targets as needed.
    void apply(const State& src, State& dst) const {
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

    // Identity functor on a state.
    static Functor identity(const State& s, Key name = Key{}) {
        if (name.empty()) name = Key{"id_" + s.id().str()};
        Functor id(name, s.id(), s.id());
        for (const auto& e : s.elements()) id.obj_[e.id] = ObjMap{e.id, nullptr};
        for (const auto& m : s.morphisms()) id.mor_[m.name] = m.name;
        return id;
    }

    // --- law check ----------------------------------------------------------
    std::vector<std::string> check_laws(const State& src, const State& dst) const {
        std::vector<std::string> errors;
        const std::string tag = "functor " + name_.str() + ": ";
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
            const Key want_dom = image_object(f->from);
            const Key want_cod = f->to.empty() ? Key{} : image_object(f->to);
            if (want_dom.empty()) {
                errors.push_back(tag + "domain " + f->from.str() + " of " + kv.first.str() +
                                 " unmapped");
            } else if (img->from != want_dom) {
                errors.push_back(tag + "F(" + kv.first.str() + ") has domain " + img->from.str() +
                                 ", expected " + want_dom.str());
            }
            if (f->to.empty()) {
                if (!img->to.empty())
                    errors.push_back(tag + "F(" + kv.first.str() + ") must stay an endomorphism");
            } else if (want_cod.empty()) {
                errors.push_back(tag + "codomain " + f->to.str() + " of " + kv.first.str() +
                                 " unmapped");
            } else if (img->to != want_cod) {
                errors.push_back(tag + "F(" + kv.first.str() + ") has codomain " + img->to.str() +
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

    Key name_;
    Key from_;
    Key to_;
    std::unordered_map<Key, ObjMap> obj_;
    std::unordered_map<Key, Key> mor_;
    std::unordered_map<Key, Key> evt_;
};

// A natural transformation eta : F => G, given componentwise on objects.
class NaturalTransformation {
public:
    using Component = std::function<void(const Element& fx, Element& gx)>;

    NaturalTransformation(Key name, const Functor* f, const Functor* g)
        : name_(name), f_(f), g_(g) {}

    NaturalTransformation& at(Key object, Component c) {
        comps_[object] = std::move(c);
        return *this;
    }

    void apply(State& cod) const {
        for (const auto& kv : comps_) {
            const Element* fx = cod.find(f_->image_object(kv.first));
            if (!fx) continue;
            const Key gx_id = g_->image_object(kv.first);
            if (gx_id.empty()) continue;
            Element* gx = cod.find(gx_id);
            if (!gx) gx = &cod.add_element(gx_id, fx->kind);
            kv.second(*fx, *gx);
        }
    }

    Key name() const { return name_; }

private:
    Key name_;
    const Functor* f_;
    const Functor* g_;
    std::unordered_map<Key, Component> comps_;
};

}  // namespace sg
