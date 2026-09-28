#include "sg/core/Adjunction.hpp"

namespace sg {

Adjunction::Adjunction(Key name, const Functor* left, const Functor* right) : name_(name), left_(left), right_(right) {
    if (left_->to() != right_->from() || right_->to() != left_->from())
        throw std::runtime_error("adjunction " + name_.str() + ": endpoints do not pair up");
}

Key Adjunction::unit_at(Key a) const {
    auto it = unit_.find(a);
    return it == unit_.end() ? Key{} : it->second;
}

Key Adjunction::counit_at(Key b) const {
    auto it = counit_.find(b);
    return it == counit_.end() ? Key{} : it->second;
}

std::vector<std::string> Adjunction::check(const State& a, const State& b) const {
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

auto Adjunction::equations(const State& a, const State& b) const -> std::vector<Equation> {
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

auto Adjunction::map(const Functor& f, const Word& w) const -> Word {
    Word out;
    for (Key k : w) {
        if (is_identity(k)) continue;
        const Key img = f.is_identity() ? k : f.image_morphism(k);
        out.push_back(img.empty() ? Key{f.name().str() + "(" + k.str() + ")?"} : img);
    }
    return out;
}

bool Adjunction::is_isomorphism(const State& a, const State& b) const {
    return unit_defects(a).empty() && counit_defects(b).empty();
}

std::vector<std::string> Adjunction::data_defects(const State& a, State& b_scratch, State& a_scratch) const {
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

std::string Adjunction::str(const Word& w) {
    if (w.empty()) return "id";
    std::string s;
    for (std::size_t i = 0; i < w.size(); ++i) s += (i ? " ; " : "") + w[i].str();
    return s;
}

void Adjunction::typed(std::vector<std::string>& out, const std::string& tag, const std::string& what, const State& s, Key arrow, Key from, Key to) const {
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

auto Adjunction::unfold(const State& s, const Word& w, std::string& why) const -> Word {
    return unfold_path(s, w, [this](Key k) { return is_identity(k); }, why);
}

std::vector<std::string> Adjunction::defects(const State& s, const Functor& out_f, const Functor& back_f) {
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

}  // namespace sg
