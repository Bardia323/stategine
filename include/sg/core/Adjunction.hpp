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

class Adjunction {
public:
    // A path inside one state, as the arrows it runs, first applied first.
    using Word = std::vector<Key>;

    Adjunction(Key name, const Functor* left, const Functor* right)
        : name_(name), left_(left), right_(right) {
        if (left_->to() != right_->from() || right_->to() != left_->from())
            throw std::runtime_error("adjunction " + name_.str() + ": endpoints do not pair up");
    }

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

    Key unit_at(Key a) const {
        auto it = unit_.find(a);
        return it == unit_.end() ? Key{} : it->second;
    }

    Key counit_at(Key b) const {
        auto it = counit_.find(b);
        return it == counit_.end() ? Key{} : it->second;
    }

    bool is_identity(Key arrow) const { return arrow.empty() || identities_.count(arrow) > 0; }

    // --- F -| G ---------------------------------------------------------------
    // Everything that keeps `left` and `right` from being adjoint through the
    // declared unit and counit. Empty means F -| G.
    std::vector<std::string> check(const State& a, const State& b) const {
        std::vector<std::string> out;
        const std::string tag = "adjunction " + name_.str() + ": ";
        if (a.id() != left_->from() || b.id() != left_->to()) {
            out.push_back(tag + "checked on " + a.id().str() + ", " + b.id().str() +
                          " but runs " + left_->from().str() + " -> " + left_->to().str());
            return out;
        }
        for (auto& e : left_->check_laws(a, b)) out.push_back(tag + e);
        for (auto& e : right_->check_laws(b, a)) out.push_back(tag + e);

        // The components have the right types.
        for (const auto& e : a.elements()) {
            const Key fa = left_->image_object(e.id);
            const Key gfa = fa.empty() ? Key{} : right_->image_object(fa);
            if (gfa.empty()) {
                out.push_back(tag + "G(F(" + e.id.str() + ")) is not defined");
                continue;
            }
            typed(out, tag, "unit at " + e.id.str(), a, unit_at(e.id), e.id, gfa);
        }
        for (const auto& e : b.elements()) {
            const Key gb = right_->image_object(e.id);
            const Key fgb = gb.empty() ? Key{} : left_->image_object(gb);
            if (fgb.empty()) {
                out.push_back(tag + "F(G(" + e.id.str() + ")) is not defined");
                continue;
            }
            typed(out, tag, "counit at " + e.id.str(), b, counit_at(e.id), fgb, e.id);
        }
        if (!out.empty()) return out;  // the equations below assume the types

        for (const Equation& eq : equations(a, b)) {
            const State& s = eq.in_a ? a : b;
            std::string why;
            const Word l = unfold(s, eq.lhs, why);
            const Word r = unfold(s, eq.rhs, why);
            if (!why.empty()) {
                out.push_back(tag + eq.law + ": " + why);
            } else if (l != r) {
                out.push_back(tag + eq.law + ": " + str(eq.lhs) + " is " + str(l) + " but " +
                              str(eq.rhs) + " is " + str(r));
            }
        }
        return out;
    }

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

    std::vector<Equation> equations(const State& a, const State& b) const {
        std::vector<Equation> out;
        const auto word = [](Key k) { return k.empty() ? Word{} : Word{k}; };
        const auto cat = [](Word x, const Word& y) {
            x.insert(x.end(), y.begin(), y.end());
            return x;
        };
        for (const auto& f : a.morphisms()) {
            if (is_identity(f.name)) continue;
            out.push_back({"unit naturality at " + f.name.str(), true, dom(f),
                           cat(word(f.name), word(unit_at(cod(f)))),
                           cat(word(unit_at(dom(f))), map(*right_, map(*left_, {f.name})))});
        }
        for (const auto& g : b.morphisms()) {
            if (is_identity(g.name)) continue;
            out.push_back({"counit naturality at " + g.name.str(), false,
                           left_->image_object(right_->image_object(dom(g))),
                           cat(map(*left_, map(*right_, {g.name})), word(counit_at(cod(g)))),
                           cat(word(counit_at(dom(g))), word(g.name))});
        }
        for (const auto& e : a.elements()) {
            const Key fa = left_->image_object(e.id);
            out.push_back({"left triangle at " + e.id.str(), false, fa,
                           cat(map(*left_, word(unit_at(e.id))), word(counit_at(fa))), {}});
        }
        for (const auto& e : b.elements()) {
            const Key gb = right_->image_object(e.id);
            out.push_back({"right triangle at " + e.id.str(), true, gb,
                           cat(word(unit_at(gb)), map(*right_, word(counit_at(e.id)))), {}});
        }
        return out;
    }

    // A path through a functor, arrow by arrow; identities go to nothing. An
    // arrow the functor does not carry maps to `F(f)?`, which no state has.
    Word map(const Functor& f, const Word& w) const {
        Word out;
        for (Key k : w) {
            if (is_identity(k)) continue;
            const Key img = f.is_identity() ? k : f.image_morphism(k);
            out.push_back(img.empty() ? Key{f.name().str() + "(" + k.str() + ")?"} : img);
        }
        return out;
    }

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

    bool is_isomorphism(const State& a, const State& b) const {
        return unit_defects(a).empty() && counit_defects(b).empty();
    }

    // Parameter-level check: run A -> B -> A on scratch states and report the
    // keys that came back changed. Catches transports that drop data even when
    // the object maps line up.
    std::vector<std::string> data_defects(const State& a, State& b_scratch,
                                          State& a_scratch) const {
        std::vector<std::string> out;
        left_->apply(a, b_scratch);
        right_->apply(b_scratch, a_scratch);
        for (const auto& e : a.elements()) {
            const Element* back = a_scratch.find(e.id);
            if (!back) {
                out.push_back(e.id.str() + ": lost on the round trip");
                continue;
            }
            for (const auto& kv : e.params) {
                if (!back->params.has(kv.first)) {
                    out.push_back(e.id.str() + "." + kv.first.str() + ": dropped");
                } else if (to_string(back->params.get(kv.first)) != to_string(kv.second)) {
                    out.push_back(e.id.str() + "." + kv.first.str() + ": " + to_string(kv.second) +
                                  " -> " + to_string(back->params.get(kv.first)));
                }
            }
        }
        return out;
    }

    static std::string str(const Word& w) {
        if (w.empty()) return "id";
        std::string s;
        for (std::size_t i = 0; i < w.size(); ++i) s += (i ? " ; " : "") + w[i].str();
        return s;
    }

private:
    // A component names an arrow of `s` from `from` to `to`, or is an
    // identity and `from` is `to`.
    void typed(std::vector<std::string>& out, const std::string& tag, const std::string& what,
               const State& s, Key arrow, Key from, Key to) const {
        if (arrow.empty()) {
            if (from != to)
                out.push_back(tag + what + ": needs an arrow " + from.str() + " -> " + to.str() +
                              ", has the identity");
            return;
        }
        const Morphism* m = s.morphism(arrow);
        if (!m) {
            out.push_back(tag + what + ": no arrow " + arrow.str() + " in " + s.id().str());
        } else if (dom(*m) != from || cod(*m) != to) {
            out.push_back(tag + what + ": " + arrow.str() + " is " + dom(*m).str() + " -> " +
                          cod(*m).str() + ", needs " + from.str() + " -> " + to.str());
        }
    }

    // A path as the generating arrows it runs: composites into their parts,
    // identities into nothing.
    Word unfold(const State& s, const Word& w, std::string& why, int depth = 0) const {
        Word out;
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
                const Word inner = unfold(s, m->parts, why, depth + 1);
                out.insert(out.end(), inner.begin(), inner.end());
            }
        }
        return out;
    }

    static std::vector<std::string> defects(const State& s, const Functor& out_f,
                                            const Functor& back_f) {
        std::vector<std::string> out;
        for (const auto& e : s.elements()) {
            const Key image = out_f.image_object(e.id);
            if (image.empty()) {
                out.push_back(e.id.str() + ": not in the domain of " + out_f.name().str());
                continue;
            }
            const Key back = back_f.image_object(image);
            if (back != e.id)
                out.push_back(e.id.str() + ": round trip landed on " +
                              (back.empty() ? std::string("<none>") : back.str()));
        }
        return out;
    }

    Key name_;
    const Functor* left_;
    const Functor* right_;
    std::unordered_map<Key, Key> unit_;
    std::unordered_map<Key, Key> counit_;
    std::unordered_set<Key> identities_;
};

}  // namespace sg
