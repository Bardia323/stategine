// Stategine - adjunctions and isomorphisms between states.
//
// Given F : A -> B (left/free) and G : B -> A (right/forgetful):
//     unit    eta : Id_A => G.F
//     counit  eps : F.G => Id_B
// States here are finite categories of named elements, so both witnesses are
// checkable by round-tripping every object and its parameters:
//
//   * eta trivial  <=>  G(F(x)) == x for all x in A   (F is a section)
//   * eps trivial  <=>  F(G(y)) == y for all y in B
//
// Both holding means the two states are isomorphic and switching between them
// loses nothing. Only the first is the usual free/forgetful situation: A -> B
// -> A is lossless, B -> A -> B forgets whatever A has no room for.
#pragma once

#include <string>
#include <vector>

#include "sg/core/Functor.hpp"
#include "sg/core/State.hpp"

namespace sg {

class Adjunction {
public:
    Adjunction(Key name, const Functor* left, const Functor* right)
        : name_(name), left_(left), right_(right) {
        if (left_->to() != right_->from() || right_->to() != left_->from())
            throw std::runtime_error("adjunction " + name_.str() + ": endpoints do not pair up");
    }

    Key name() const { return name_; }
    const Functor& left() const { return *left_; }
    const Functor& right() const { return *right_; }

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

private:
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
};

}  // namespace sg
