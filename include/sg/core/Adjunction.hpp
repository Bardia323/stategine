// Stategine - adjunctions and isomorphisms between states.
//
// Given F : A -> B (left) and G : B -> A (right), F -| G is witnessed by two
// families of arrows, not by the round trips coming home:
//
//     unit    eta_a : a -> G(F(a))       for every object a of A
//     counit  eps_b : F(G(b)) -> b       for every object b of B
//
// subject to four equations (paths written first-step-first):
//
//     unit naturality     f ; eta_a'           ==  eta_a ; G(F(f))     f : a -> a'
//     counit naturality   F(G(g)) ; eps_b'     ==  eps_b ; g           g : b -> b'
//     left triangle       F(eta_a) ; eps_F(a)  ==  id_F(a)
//     right triangle      eta_G(b) ; G(eps_b)  ==  id_G(b)
//
// Nothing asks G(F(a)) to *be* a: eta_a only has to be an arrow there. That
// is the whole difference between an adjunction and an equivalence. When
// every component can be an identity the pair is an isomorphism of states,
// which is what `is_isomorphism` and the round-trip `*_defects` measure.
//
// `check` decides the four equations structurally: a path is the word of the
// arrows it runs, composites are unfolded into their parts and identities
// drop out. Two paths are equal when they are the same word, so a state is
// read as the category its arrows generate, with the relations its own
// composites record. Equations that only hold on data - two different
// arrows that happen to do the same thing - are for `laws::adjunction`
// (Laws.hpp), which runs the same four equations on live state.
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sg/core/Functor.hpp"
#include "sg/core/State.hpp"

namespace sg {

// A path as the generating arrows it runs: composites into their parts,
// identities - whatever `is_identity` says is one - into nothing. An arrow
// the state does not have is named in `why` (the first one only).
template <typename IsIdentity>
std::vector<Key> unfold_path(const State& s, const std::vector<Key>& w, const IsIdentity& is_identity,
                             std::string& why, int depth = 0) {
    std::vector<Key> out;
    for (Key k : w) {
        if (is_identity(k)) continue;
        const Morphism* m = s.morphism(k);
        if (!m) {
            if (why.empty()) why = "no arrow " + k.str() + " in " + s.id().str();
            continue;
        }
        if (m->parts.empty() || depth > 32) {
            out.push_back(k);
        } else {
            const std::vector<Key> inner = unfold_path(s, m->parts, is_identity, why, depth + 1);
            out.insert(out.end(), inner.begin(), inner.end());
        }
    }
    return out;
}

class Adjunction {
public:
    // A path inside one state, as the arrows it runs, first applied first.
    using Word = std::vector<Key>;

    Adjunction(Key name, const Functor* left, const Functor* right);

    Key name() const { return name_; }
    const Functor& left() const { return *left_; }
    const Functor& right() const { return *right_; }

    // --- the witnesses --------------------------------------------------------
    // eta_a is `arrow`, an arrow of A from a to G(F(a)). An empty arrow is the
    // identity, which is only an arrow when G(F(a)) is a. An object with no
    // component declared gets the identity.
    Adjunction& unit(Key a, Key arrow = Key{}) {
        unit_[a] = arrow;
        return *this;
    }

    // eps_b is `arrow`, an arrow of B from F(G(b)) to b.
    Adjunction& counit(Key b, Key arrow = Key{}) {
        counit_[b] = arrow;
        return *this;
    }

    // An arrow that is an identity: a no-op loop standing for id_x, so that a
    // functor has somewhere to send an arrow it collapses. Identities drop out
    // of every path.
    Adjunction& identity(Key arrow) {
        identities_.insert(arrow);
        return *this;
    }

    Key unit_at(Key a) const;

    Key counit_at(Key b) const;

    bool is_identity(Key arrow) const { return arrow.empty() || identities_.count(arrow) > 0; }

    // --- F -| G ---------------------------------------------------------------
    // Everything that keeps `left` and `right` from being adjoint through the
    // declared unit and counit. Empty means F -| G.
    std::vector<std::string> check(const State& a, const State& b) const;

    bool holds(const State& a, const State& b) const { return check(a, b).empty(); }

    // --- the equations ----------------------------------------------------------
    // All four families, with F and G already applied to the arrows. `in_a`
    // says which state the paths run in and `at` the object they start from.
    struct Equation {
        std::string law;
        bool in_a = true;
        Key at;
        Word lhs;
        Word rhs;
    };

    std::vector<Equation> equations(const State& a, const State& b) const;

    // A path through a functor, arrow by arrow; identities go to nothing. An
    // arrow the functor does not carry maps to `F(f)?`, which no state has.
    Word map(const Functor& f, const Word& w) const;

    // --- round trips ------------------------------------------------------------
    // Whether the unit and counit can be identities at all: the objects where
    // G(F(x)) is not x, or F(G(y)) is not y. A defect here is not a failure of
    // F -| G - it is the reason F -| G is not an isomorphism.

    // Objects of A where x -> F(x) -> G(F(x)) does not return to x.
    std::vector<std::string> unit_defects(const State& a) const {
        return defects(a, *left_, *right_);
    }

    // Objects of B where y -> G(y) -> F(G(y)) does not return to y.
    std::vector<std::string> counit_defects(const State& b) const {
        return defects(b, *right_, *left_);
    }

    bool is_isomorphism(const State& a, const State& b) const;

    // Parameter-level check: run A -> B -> A on scratch states and report the
    // keys that came back changed. Catches transports that drop data even when
    // the object maps line up.
    std::vector<std::string> data_defects(const State& a, State& b_scratch,
                                          State& a_scratch) const;

    static std::string str(const Word& w);

private:
    // A component names an arrow of `s` from `from` to `to`, or is an
    // identity and `from` is `to`.
    void typed(std::vector<std::string>& out, const std::string& tag, const std::string& what,
               const State& s, Key arrow, Key from, Key to) const;

    // A path as the generating arrows it runs: composites into their parts,
    // identities into nothing.
    Word unfold(const State& s, const Word& w, std::string& why) const;

    static std::vector<std::string> defects(const State& s, const Functor& out_f,
                                            const Functor& back_f);

    Key name_;
    const Functor* left_;
    const Functor* right_;
    std::unordered_map<Key, Key> unit_;
    std::unordered_map<Key, Key> counit_;
    std::unordered_set<Key> identities_;
};

}  // namespace sg
