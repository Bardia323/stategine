// Stategine - typed handles: composition that does not compile when it
// would not type.
//
// Underneath, states and elements are named at run time, so `Functor::compose`
// and `State::compose` can only throw when the ends do not meet. This header
// lifts the names into C++ types, so the same mistakes are refused by the
// compiler instead:
//
//   struct Battle { static constexpr const char* name = "battle"; };   // a state
//   struct Menu   { static constexpr const char* name = "menu"; };
//   struct Hero   { using state = Battle; static constexpr const char* name = "hero"; };
//   struct Slime  { using state = Battle; static constexpr const char* name = "slime"; };
//
//   auto strike = sg::typed::arrow<Hero, Slime>(battle, "strike", "attack", fn);
//   auto recoil = sg::typed::arrow<Slime, Hero>(battle, "recoil", "attack", fn);
//   auto both   = recoil * strike;          // Arrow<Hero, Hero>
//   auto bad    = strike * strike;          // does not compile: Slime is not Hero
//
//   auto carry  = sg::typed::functor<Battle, Menu>(graph, "carry");
//   sg::typed::connect(graph, "victory", carry);          // Battle -> Menu, always
//   sg::typed::embed<Portal>(graph, "map", view, edit);    // in/out must mirror
//
// Nothing here is a new primitive. A typed handle is a name plus the static
// type that name was registered under, checked once where the two meet (a
// registration, or binding a name that already exists), and an `Arrow<X, Y>`
// is a `Path` whose ends the compiler knows - so the runtime law checker in
// Laws.hpp runs typed and untyped diagrams alike.
#pragma once

#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "sg/core/Laws.hpp"
#include "sg/core/StateGraph.hpp"

namespace sg {
namespace typed {

// --- tags ---------------------------------------------------------------------
// A tag is any type with a static `name`. One that also names a `state` is an
// object of that state; one that does not is a state.
template <class T, class = void>
struct is_object : std::false_type {};
template <class T>
struct is_object<T, std::void_t<typename T::state>> : std::true_type {};

template <class T, bool = is_object<T>::value>
struct state_of_impl {
    using type = T;
};
template <class T>
struct state_of_impl<T, true> {
    using type = typename T::state;
};
template <class T>
using state_of = typename state_of_impl<T>::type;

template <class T>
Key key() {
    return Key{T::name};
}

// Where a path starting at tag T begins.
template <class T>
Path start() {
    return is_object<T>::value ? Path(key<state_of<T>>(), key<T>()) : Path(key<T>());
}

// --- arrows ---------------------------------------------------------------------
// An arrow from X to Y: a path the compiler has already typed. X and Y are
// both objects, or both states; which one is fixed by how it was made.
template <class X, class Y>
class Arrow {
public:
    const Path& path() const { return path_; }

    // Reading order: f.then(g) is g * f.
    template <class Y2, class Z>
    Arrow<X, Z> then(const Arrow<Y2, Z>& g) const;

protected:
    explicit Arrow(Path p) : path_(std::move(p)) {}

private:
    template <class, class>
    friend class Arrow;
    template <class T>
    friend Arrow<T, T> id();
    template <class X2, class Y2>
    friend Arrow<X2, Y2> unchecked(Path p);

    Path path_;
};

// For the handful of places that build a typed path from parts already
// checked. Not for callers: a wrong path here is a lie the compiler repeats.
template <class X, class Y>
Arrow<X, Y> unchecked(Path p) {
    return Arrow<X, Y>(std::move(p));
}

template <class T>
Arrow<T, T> id() {
    return Arrow<T, T>(start<T>());
}

// Whether f : X -> Y then g : Y2 -> Z is defined. Exposed so tests and
// generic code can ask without tripping the static_assert.
template <class F, class G>
struct composable : std::false_type {};
template <class X, class Y, class Y2, class Z>
struct composable<Arrow<X, Y>, Arrow<Y2, Z>> : std::is_same<Y, Y2> {};

template <class X, class Y>
template <class Y2, class Z>
Arrow<X, Z> Arrow<X, Y>::then(const Arrow<Y2, Z>& g) const {
    static_assert(std::is_same<Y, Y2>::value,
                  "sg: f.then(g) needs cod(f) == dom(g); these arrows do not meet");
    Path p = path_;
    p.then(g.path());
    return Arrow<X, Z>(std::move(p));
}

// g * f reads "g after f", as for functors.
template <class Y2, class Z, class X, class Y>
Arrow<X, Z> operator*(const Arrow<Y2, Z>& g, const Arrow<X, Y>& f) {
    static_assert(std::is_same<Y, Y2>::value,
                  "sg: g * f needs cod(f) == dom(g); these arrows do not meet");
    return f.then(g);
}

// --- arrows inside a state --------------------------------------------------------
// A single registered morphism. It is an Arrow, so it composes like one.
template <class X, class Y>
class Hom : public Arrow<X, Y> {
public:
    Key name() const { return name_; }

private:
    explicit Hom(Key name) : Arrow<X, Y>(start<X>().arrow(name)), name_(name) {}

    template <class X2, class Y2>
    friend Hom<X2, Y2> arrow(State&, Key, Key, Morphism::Handler);
    template <class X2, class Y2>
    friend Hom<X2, Y2> bind(const State&, Key);
    template <class X2, class Y2, class Y3, class Z2>
    friend Hom<X2, Z2> compose(State&, Key, const Hom<Y3, Z2>&, const Hom<X2, Y2>&, Key);

    Key name_;
};

namespace detail {
template <class X, class Y>
void require_objects_of_one_state() {
    static_assert(is_object<X>::value && is_object<Y>::value,
                  "sg: an arrow inside a state joins two objects; tag them with `using state`");
    static_assert(std::is_same<state_of<X>, state_of<Y>>::value,
                  "sg: an arrow inside a state cannot leave it; use a functor between states");
}

inline void require_state(const State& s, Key want, const char* what) {
    if (s.id() != want)
        throw std::logic_error(std::string("sg::typed::") + what + ": state is " + s.id().str() +
                               ", but the tag says " + want.str());
}
}  // namespace detail

// Register f : X -> Y in `s`. X == Y registers an endomorphism.
template <class X, class Y>
Hom<X, Y> arrow(State& s, Key name, Key trigger, Morphism::Handler fn) {
    detail::require_objects_of_one_state<X, Y>();
    detail::require_state(s, key<state_of<X>>(), "arrow");
    if (std::is_same<X, Y>::value) {
        s.loop(name, key<X>(), trigger, std::move(fn));
    } else {
        s.arrow(name, key<X>(), key<Y>(), trigger, std::move(fn));
    }
    return Hom<X, Y>(name);
}

// Bind an arrow something else registered - a domain's integrator, say - under
// the type the caller believes it has. The belief is checked here, once.
template <class X, class Y>
Hom<X, Y> bind(const State& s, Key name) {
    detail::require_objects_of_one_state<X, Y>();
    detail::require_state(s, key<state_of<X>>(), "bind");
    const Morphism* m = s.morphism(name);
    if (!m) throw std::logic_error("sg::typed::bind: no arrow " + name.str() + " in " + s.id().str());
    if (dom(*m) != key<X>() || cod(*m) != key<Y>())
        throw std::logic_error("sg::typed::bind: " + name.str() + " is " + dom(*m).str() + " -> " +
                               cod(*m).str() + ", not " + key<X>().str() + " -> " +
                               key<Y>().str());
    return Hom<X, Y>(name);
}

// Register g . f as one arrow of `s`.
template <class X, class Y, class Y2, class Z>
Hom<X, Z> compose(State& s, Key name, const Hom<Y2, Z>& g, const Hom<X, Y>& f, Key trigger) {
    static_assert(std::is_same<Y, Y2>::value,
                  "sg: compose(g, f) needs cod(f) == dom(g); these arrows do not meet");
    detail::require_state(s, key<state_of<X>>(), "compose");
    s.compose(name, f.name(), g.name(), trigger);
    return Hom<X, Z>(name);
}

// --- functors between states --------------------------------------------------------
template <class A, class B>
class Fun : public Arrow<A, B> {
    static_assert(!is_object<A>::value && !is_object<B>::value,
                  "sg: a functor runs between states, not between objects");

public:
    Key name() const { return f_->name(); }
    sg::Functor& raw() const { return *f_; }

    // Object map, typed at both ends.
    template <class X, class Y>
    Fun& on(Transport t = nullptr) {
        static_assert(std::is_same<state_of<X>, A>::value,
                      "sg: F.on<X, Y>() needs X to be an object of F's source");
        static_assert(std::is_same<state_of<Y>, B>::value,
                      "sg: F.on<X, Y>() needs Y to be an object of F's target");
        f_->on_object(key<X>(), key<Y>(), std::move(t));
        return *this;
    }

    // Arrow map. That F(f) runs between F's images of f's ends is a fact about
    // the object map, which is data; `validate` checks it, and the
    // functoriality law checks F(f) does what f does.
    template <class X, class Y, class X2, class Y2>
    Fun& on(const Hom<X, Y>& f, const Hom<X2, Y2>& image) {
        static_assert(std::is_same<state_of<X>, A>::value,
                      "sg: F.on(f, F(f)) needs f to be an arrow of F's source");
        static_assert(std::is_same<state_of<X2>, B>::value,
                      "sg: F.on(f, F(f)) needs F(f) to be an arrow of F's target");
        f_->on_morphism(f.name(), image.name());
        return *this;
    }

    Fun& on_event(Key src, Key dst) {
        f_->on_event(src, dst);
        return *this;
    }

    // F restricted to one object: X -> F(X). Whether F maps X there is data,
    // so this is where it is checked.
    template <class X, class Y>
    Arrow<X, Y> at() const {
        static_assert(std::is_same<state_of<X>, A>::value && is_object<X>::value,
                      "sg: F.at<X, Y>() needs X to be an object of F's source");
        static_assert(std::is_same<state_of<Y>, B>::value && is_object<Y>::value,
                      "sg: F.at<X, Y>() needs Y to be an object of F's target");
        if (f_->image_object(key<X>()) != key<Y>())
            throw std::logic_error("sg::typed::at: " + name().str() + " does not map " +
                                   key<X>().str() + " to " + key<Y>().str());
        return unchecked<X, Y>(start<X>().functor(name()));
    }

private:
    explicit Fun(sg::Functor& f) : Arrow<A, B>(start<A>().functor(f.name())), f_(&f) {}

    template <class A2, class B2>
    friend Fun<A2, B2> functor(StateGraph&, Key);
    template <class A2, class B2>
    friend Fun<A2, B2> bind_functor(StateGraph&, Key);
    template <class A2, class B2, class B3, class C2>
    friend Fun<A2, C2> compose(StateGraph&, Key, const Fun<B3, C2>&, const Fun<A2, B2>&);

    sg::Functor* f_;
};

template <class A, class B>
Fun<A, B> functor(StateGraph& g, Key name) {
    return Fun<A, B>(g.add_functor(name, key<A>(), key<B>()));
}

template <class A, class B>
Fun<A, B> bind_functor(StateGraph& g, Key name) {
    sg::Functor* f = g.functor(name);
    if (!f) throw std::logic_error("sg::typed::bind_functor: no functor " + name.str());
    if (f->from() != key<A>() || f->to() != key<B>())
        throw std::logic_error("sg::typed::bind_functor: " + name.str() + " is " +
                               f->from().str() + " -> " + f->to().str() + ", not " +
                               key<A>().str() + " -> " + key<B>().str());
    return Fun<A, B>(*f);
}

// Register G . F under `name`. The graph remembers the chain, so the
// composition law can hold the composite to it.
template <class A, class B, class B2, class C>
Fun<A, C> compose(StateGraph& g, Key name, const Fun<B2, C>& second, const Fun<A, B>& first) {
    static_assert(std::is_same<B, B2>::value,
                  "sg: compose(G, F) needs cod(F) == dom(G); these functors do not meet");
    return Fun<A, C>(g.compose_functors(name, {first.name(), second.name()}));
}

// --- transitions, embeddings, lenses ------------------------------------------------
// A transition is typed by the functor it carries: there is no way to put a
// Battle -> Menu functor on a transition out of anything but Battle.
template <class A, class B>
const Transition& connect(StateGraph& g, Key trigger, const Fun<A, B>& carry,
                          TransitionKind kind = TransitionKind::Switch) {
    Transition t;
    t.from = key<A>();
    t.to = key<B>();
    t.trigger = trigger;
    t.kind = kind;
    t.functor = carry.name();
    return g.connect(std::move(t));
}

template <class A, class B>
const Transition& connect(StateGraph& g, Key trigger, TransitionKind kind = TransitionKind::Switch) {
    static_assert(!is_object<A>::value && !is_object<B>::value,
                  "sg: a transition runs between states");
    return g.connect(key<A>(), trigger, key<B>(), kind);
}

// A two-way portal. The portal is an object of the host; `in` reads the
// subject into the guest and `out` writes it back, so their types must mirror
// each other - a lens whose halves disagree about what they view does not
// compile. The subject is whatever `in` starts from.
template <class Portal, class S, class G, class G2, class S2>
const Embedding& embed(StateGraph& g, Key name, const Fun<S, G>& in, const Fun<G2, S2>& out,
                       EmbedSync sync = EmbedSync::Commit) {
    static_assert(is_object<Portal>::value, "sg: a portal is an object of its host state");
    static_assert(std::is_same<G, G2>::value && std::is_same<S, S2>::value,
                  "sg: embed(in, out) needs out to run back along in: in : S -> G, out : G -> S");
    const Key host = key<state_of<Portal>>();
    const Key subject = key<S>();
    return g.embed(name, host, key<Portal>(), key<G>(), in.name(), out.name(), sync,
                   subject == host ? Key{} : subject);
}

// A read-only window: `in` only.
template <class Portal, class S, class G>
const Embedding& view(StateGraph& g, Key name, const Fun<S, G>& in) {
    static_assert(is_object<Portal>::value, "sg: a portal is an object of its host state");
    const Key host = key<state_of<Portal>>();
    const Key subject = key<S>();
    return g.embed(name, host, key<Portal>(), key<G>(), in.name(), Key{}, EmbedSync::View,
                   subject == host ? Key{} : subject);
}

// Both halves of a lens, declared together and typed together.
template <class S, class G>
struct Lens {
    Fun<S, G> in;
    Fun<G, S> out;

    // Pair an object of the subject with its image in the view.
    template <class X, class Y>
    Lens& pair(Transport to_view, Transport to_subject) {
        in.template on<X, Y>(std::move(to_view));
        out.template on<Y, X>(std::move(to_subject));
        return *this;
    }
};

template <class S, class G>
Lens<S, G> lens(StateGraph& g, Key in_name, Key out_name) {
    return Lens<S, G>{functor<S, G>(g, in_name), functor<G, S>(g, out_name)};
}

// --- diagrams ------------------------------------------------------------------------
// Only two arrows with the same ends can be claimed equal; anything else is
// refused here rather than reported as a mismatch later.
template <class X, class Y, class X2, class Y2>
Diagram& commutes(Diagram& d, const Arrow<X, Y>& lhs, const Arrow<X2, Y2>& rhs,
                  Params args = {}) {
    static_assert(std::is_same<X, X2>::value && std::is_same<Y, Y2>::value,
                  "sg: only parallel arrows can commute; these do not share both ends");
    return d.commutes(lhs.path(), rhs.path(), std::move(args));
}

}  // namespace typed
}  // namespace sg
