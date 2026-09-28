// Stategine - Kan extensions: a functor carried on along another.
//
// Given F : A -> B and K : A -> C, a left Kan extension of F along K is a
// functor Lan_K F : C -> B with a 2-cell
//
//     alpha : F => Lan_K F . K          alpha_a : F(a) -> Lan_K F (K(a))
//
// through which every other such pair passes exactly once: for G : C -> B and
// gamma : F => G . K there is one sigma : Lan_K F => G with
//
//     gamma_a  ==  alpha_a ; sigma_K(a)
//
// It is what F does on A, said everywhere on C as well as it can be from
// below. The right one, Ran_K F with beta : Ran_K F . K => F, is the same
// from above: gamma : G . K => F passes through it as gamma_a == sigma_K(a) ; beta_a.
//
// The engine builds one where what it already has builds it. Along a functor
// with a right adjoint, K -| R, the left extension is F after R; along one
// with a left adjoint, L -| K, the right extension is F after L:
//
//     Lan_K F  =  F . R      alpha_a = F(eta_a)     sigma_c = gamma_R(c) ; G(eps_c)
//     Ran_K F  =  F . L      beta_a  = F(eps_a)     sigma_c = G(eta_c) ; gamma_L(c)
//
// That these are universal is a theorem about the adjunction, so an extension
// here is exactly as good as its `Adjunction`, which `check` asks first.
// Nothing else is taken on trust: the 2-cell is typed arrow by arrow in B and
// natural on every arrow F carries, and `factor` hands back the sigma a given
// competitor passes through, with the equations that say it does. An
// adjunction is itself one: K -| R makes R = Lan_K id_A, the unit its 2-cell.
//
// The extension is a functor like any other - partial where F and the adjoint
// are, carrying an object's data by the adjoint's transport and then F's - and
// it reaches the world as any functor does: `declare` puts it in the graph as
// the composite it is, which the laws hold to its chain (`composition`).
// `laws::kan` (Laws.hpp) runs the 2-cell's squares on live data, with the
// adjunction's equations.
//
// Paths are words, compared as `Adjunction::check` compares them: composites
// unfolded into their parts, identities dropped - B's identities declared
// here (`identity`), A's and C's on the adjunction.
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sg/core/Adjunction.hpp"
#include "sg/core/Functor.hpp"
#include "sg/core/State.hpp"
#include "sg/core/StateGraph.hpp"

namespace sg {

class Kan {
public:
    using Word = Adjunction::Word;
    enum class Side { Left, Right };

    // Lan_K F along K = adj.left(), where `adj` is K -| R. F and the
    // adjunction are held, not copied, as an Adjunction holds its functors.
    static Kan left(Key name, const Functor& f, const Adjunction& adj) {
        return Kan(Side::Left, name, f, adj);
    }

    // Ran_K F along K = adj.right(), where `adj` is L -| K.
    static Kan right(Key name, const Functor& f, const Adjunction& adj) {
        return Kan(Side::Right, name, f, adj);
    }

    Key name() const { return name_; }
    Side side() const { return side_; }
    bool is_left() const { return side_ == Side::Left; }
    const Adjunction& adjunction() const { return *adj_; }
    const Functor& extended() const { return *f_; }  // F : A -> B
    const Functor& along() const { return is_left() ? adj_->left() : adj_->right(); }    // K : A -> C
    const Functor& adjoint() const { return is_left() ? adj_->right() : adj_->left(); }  // R or L : C -> A

    // Lan_K F or Ran_K F : C -> B, under this extension's name. Made again
    // whenever F or the adjoint has been mapped anew since.
    const Functor& extension() const {
        if (!built_ || f_stamp_ != f_->stamp() || adjoint_stamp_ != adjoint().stamp()) {
            ext_ = Functor::compose(adjoint(), *f_, name_);
            f_stamp_ = f_->stamp();
            adjoint_stamp_ = adjoint().stamp();
            built_ = true;
        }
        return ext_;
    }

    // An arrow of B that is an identity: a no-op loop standing for id_x.
    Kan& identity(Key arrow) {
        identities_.insert(arrow);
        return *this;
    }

    bool is_identity(Key arrow) const {
        return arrow.empty() || identities_.count(arrow) > 0 || adj_->is_identity(arrow);
    }

    // --- the 2-cell ---------------------------------------------------------------
    // Its component at an object a of A, as the path in B it runs:
    // F(a) -> Lan(K(a)) for a left extension, Ran(K(a)) -> F(a) for a right.
    Word cell(Key a) const {
        return map(*f_, word(is_left() ? adj_->unit_at(a) : adj_->counit_at(a)));
    }

    // --- is it one ----------------------------------------------------------------
    // Everything that keeps this from being the Kan extension of F along K.
    // Empty means it is: the adjunction holds, the functors keep their
    // arrows' ends, and the 2-cell is typed and natural.
    std::vector<std::string> check(const State& a, const State& b, const State& c) const {
        std::vector<std::string> out;
        const std::string tag = "kan " + name_.str() + ": ";
        const Functor& F = *f_;
        const Functor& K = along();
        if (a.id() != F.from() || b.id() != F.to() || c.id() != K.to()) {
            out.push_back(tag + "checked on " + a.id().str() + ", " + b.id().str() + ", " +
                          c.id().str() + " but extends " + F.from().str() + " -> " + F.to().str() +
                          " along " + K.from().str() + " -> " + K.to().str());
            return out;
        }
        // What it rests on.
        for (auto& e : is_left() ? adj_->check(a, c) : adj_->check(c, a)) out.push_back(tag + e);
        for (auto& e : F.check_laws(a, b)) out.push_back(tag + e);
        const Functor& E = extension();
        for (auto& e : E.check_laws(c, b)) out.push_back(tag + e);

        // The 2-cell's components run between the right ends, in B.
        for (const auto& e : a.elements()) {
            const Key fa = F.image_object(e.id);
            if (fa.empty()) continue;  // outside F: nothing there to extend
            const Key ka = K.image_object(e.id);
            const Key eka = ka.empty() ? Key{} : E.image_object(ka);
            if (eka.empty()) {
                out.push_back(tag + name_.str() + "(" + K.name().str() + "(" + e.id.str() +
                              ")) is not defined");
                continue;
            }
            const std::string what = "2-cell at " + e.id.str();
            if (is_left()) {
                typed(out, tag, what, b, cell(e.id), fa, eka);
            } else {
                typed(out, tag, what, b, cell(e.id), eka, fa);
            }
        }
        if (!out.empty()) return out;  // the squares below assume the types

        for (const Square& sq : equations(a, b)) compare(out, tag, b, sq);
        return out;
    }

    bool holds(const State& a, const State& b, const State& c) const { return check(a, b, c).empty(); }

    // --- the squares ----------------------------------------------------------------
    // Naturality of the 2-cell, one square in B for each arrow f : a -> a'
    // of A that F carries (a partial F is extended from what it carries):
    //
    //     left     F(f) ; alpha_a'   ==  alpha_a ; Lan(K(f))
    //     right    Ran(K(f)) ; beta_a'  ==  beta_a ; F(f)
    //
    // `at` is the object of B both sides start from.
    struct Square {
        std::string law;
        Key at;
        Word lhs;
        Word rhs;
    };

    std::vector<Square> equations(const State& a, const State& /*b*/) const {
        std::vector<Square> out;
        const Functor& F = *f_;
        const Functor& K = along();
        const Functor& E = extension();
        for (const auto& f : a.morphisms()) {
            if (is_identity(f.name)) continue;
            if (!F.is_identity() && F.image_morphism(f.name).empty()) continue;
            if (F.image_object(dom(f)).empty() || F.image_object(cod(f)).empty()) continue;
            const Word Ff = map(F, {f.name});
            const Word EKf = map(E, map(K, {f.name}));
            if (is_left()) {
                out.push_back({"naturality at " + f.name.str(), F.image_object(dom(f)),
                               cat(Ff, cell(cod(f))), cat(cell(dom(f)), EKf)});
            } else {
                const Key ka = K.image_object(dom(f));
                out.push_back({"naturality at " + f.name.str(), ka.empty() ? Key{} : E.image_object(ka),
                               cat(EKf, cell(cod(f))), cat(cell(dom(f)), Ff)});
            }
        }
        return out;
    }

    // --- universal ----------------------------------------------------------------
    // Another functor G : C -> B with a 2-cell gamma - F => G . K for a left
    // extension, G . K => F for a right - given as a path in B per object of A
    // (one not given is the identity). `sigma` is the one 2-cell between the
    // extension and G it passes through, per object of C; `defects` is
    // everything that keeps gamma from being a 2-cell of that type, or from
    // passing through sigma. That sigma is the only one is the adjunction's
    // doing; `check` says whether there is one.
    struct Factor {
        std::unordered_map<Key, Word> sigma;
        std::vector<std::string> defects;
        bool ok() const { return defects.empty(); }
    };

    Factor factor(const Functor& g, const std::unordered_map<Key, Word>& gamma, const State& a,
                  const State& b, const State& c) const {
        Factor out;
        const std::string tag = "kan " + name_.str() + " through " + g.name().str() + ": ";
        const Functor& F = *f_;
        const Functor& K = along();
        const Functor& A = adjoint();
        const Functor& E = extension();
        if (g.from() != K.to() || g.to() != F.to()) {
            out.defects.push_back(tag + "runs " + g.from().str() + " -> " + g.to().str() + ", needs " +
                                  K.to().str() + " -> " + F.to().str());
            return out;
        }
        const auto gamma_at = [&](Key x) {
            auto it = gamma.find(x);
            return it == gamma.end() ? Word{} : it->second;
        };

        // gamma is a 2-cell of the right type, natural on what F carries.
        for (const auto& e : a.elements()) {
            const Key fa = F.image_object(e.id);
            const Key ka = K.image_object(e.id);
            const Key gka = ka.empty() ? Key{} : g.image_object(ka);
            if (fa.empty()) continue;
            if (gka.empty()) {
                out.defects.push_back(tag + g.name().str() + "(" + K.name().str() + "(" + e.id.str() +
                                      ")) is not defined");
                continue;
            }
            const std::string what = "gamma at " + e.id.str();
            if (is_left()) {
                typed(out.defects, tag, what, b, gamma_at(e.id), fa, gka);
            } else {
                typed(out.defects, tag, what, b, gamma_at(e.id), gka, fa);
            }
        }
        if (!out.defects.empty()) return out;
        for (const auto& f : a.morphisms()) {
            if (is_identity(f.name)) continue;
            if (!F.is_identity() && F.image_morphism(f.name).empty()) continue;
            if (F.image_object(dom(f)).empty() || F.image_object(cod(f)).empty()) continue;
            const Word Ff = map(F, {f.name});
            const Word GKf = map(g, map(K, {f.name}));
            if (is_left()) {
                compare(out.defects, tag, b,
                        {"gamma natural at " + f.name.str(), F.image_object(dom(f)),
                         cat(Ff, gamma_at(cod(f))), cat(gamma_at(dom(f)), GKf)});
            } else {
                compare(out.defects, tag, b,
                        {"gamma natural at " + f.name.str(), g.image_object(K.image_object(dom(f))),
                         cat(GKf, gamma_at(cod(f))), cat(gamma_at(dom(f)), Ff)});
            }
        }

        // sigma, at every object of C the extension reaches.
        for (const auto& e : c.elements()) {
            const Key ac = A.image_object(e.id);
            const Key ec = E.image_object(e.id);
            const Key gc = g.image_object(e.id);
            if (ac.empty() || ec.empty() || gc.empty()) continue;
            Word s = is_left() ? cat(gamma_at(ac), map(g, word(adj_->counit_at(e.id))))
                               : cat(map(g, word(adj_->unit_at(e.id))), gamma_at(ac));
            if (is_left()) {
                typed(out.defects, tag, "sigma at " + e.id.str(), b, s, ec, gc);
            } else {
                typed(out.defects, tag, "sigma at " + e.id.str(), b, s, gc, ec);
            }
            out.sigma.emplace(e.id, std::move(s));
        }
        if (!out.defects.empty()) return out;

        // And gamma is alpha then sigma (sigma then beta), component by component.
        for (const auto& e : a.elements()) {
            const Key fa = F.image_object(e.id);
            const Key ka = K.image_object(e.id);
            if (fa.empty() || ka.empty()) continue;
            auto it = out.sigma.find(ka);
            if (it == out.sigma.end()) {
                out.defects.push_back(tag + "sigma at " + ka.str() + " is not defined");
                continue;
            }
            const Word through = is_left() ? cat(cell(e.id), it->second) : cat(it->second, cell(e.id));
            compare(out.defects, tag, b,
                    {"factors at " + e.id.str(), is_left() ? fa : g.image_object(ka), gamma_at(e.id), through});
        }
        return out;
    }

    // --- in the graph ---------------------------------------------------------------
    // The extension put in the graph as what it is: the composite of the
    // adjoint and then F, whose claim the laws check (`composition`). Where
    // either is not the graph's own (an identity made on the spot), it is put
    // in as the functor it makes.
    Functor& declare(StateGraph& g) const {
        const Functor& A = adjoint();
        if (g.functor(A.name()) == &A && g.functor(f_->name()) == f_)
            return g.compose_functors(name_, {A.name(), f_->name()});
        return g.add_functor(extension());
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

    Word unfold(const State& s, const Word& w, std::string& why) const {
        return unfold_path(s, w, [this](Key k) { return is_identity(k); }, why);
    }

private:
    Kan(Side side, Key name, const Functor& f, const Adjunction& adj)
        : side_(side), name_(name), f_(&f), adj_(&adj) {
        const Functor& K = along();
        if (f.from() != K.from())
            throw std::runtime_error("kan " + name_.str() + ": " + f.name().str() + " starts at " +
                                     f.from().str() + ", but " + K.name().str() + " at " + K.from().str());
    }

    static Word word(Key k) { return k.empty() ? Word{} : Word{k}; }
    static Word cat(Word x, const Word& y) {
        x.insert(x.end(), y.begin(), y.end());
        return x;
    }

    // `w` is a path of `s` from `from` to `to`: each arrow there, each
    // starting where the last ended. The empty path only where from is to.
    void typed(std::vector<std::string>& out, const std::string& tag, const std::string& what,
               const State& s, const Word& w, Key from, Key to) const {
        Key at = from;
        for (Key k : w) {
            if (is_identity(k)) continue;
            const Morphism* m = s.morphism(k);
            if (!m) {
                out.push_back(tag + what + ": no arrow " + k.str() + " in " + s.id().str());
                return;
            }
            if (dom(*m) != at) {
                out.push_back(tag + what + ": " + k.str() + " starts at " + dom(*m).str() + ", not " +
                              at.str());
                return;
            }
            at = cod(*m);
        }
        if (at != to)
            out.push_back(tag + what + ": " + Adjunction::str(w) + " runs " + from.str() + " -> " +
                          at.str() + ", needs " + from.str() + " -> " + to.str());
    }

    void compare(std::vector<std::string>& out, const std::string& tag, const State& s,
                 const Square& sq) const {
        std::string why;
        const Word l = unfold(s, sq.lhs, why);
        const Word r = unfold(s, sq.rhs, why);
        if (!why.empty()) {
            out.push_back(tag + sq.law + ": " + why);
        } else if (l != r) {
            out.push_back(tag + sq.law + ": " + Adjunction::str(sq.lhs) + " is " + Adjunction::str(l) +
                          " but " + Adjunction::str(sq.rhs) + " is " + Adjunction::str(r));
        }
    }

    Side side_;
    Key name_;
    const Functor* f_;
    const Adjunction* adj_;
    std::unordered_set<Key> identities_;
    mutable Functor ext_;
    mutable uint64_t f_stamp_ = 0, adjoint_stamp_ = 0;
    mutable bool built_ = false;
};

}  // namespace sg
