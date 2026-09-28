#include "sg/core/Declared.hpp"

namespace sg {

bool Affine::is_copy(const Row& r) {
    return r.terms.size() == 1 && r.terms[0].k == 1.0 && r.terms[0].arg.empty() && r.bias == 0.0;
}

auto Affine::copy(Key to, Key from) -> Affine& {
    rows.push_back(Row{to, {Term{from, 1.0, Key{}, false}}, 0.0});
    return *this;
}

auto Affine::set(Key to, std::vector<Term> terms, double bias) -> Affine& {
    rows.push_back(Row{to, std::move(terms), bias});
    return *this;
}

void run(const Affine& a, const Element& src, Element& dst, const Params* args) {
    if (a.copy_all && &src != &dst)
        for (const auto& kv : src.params) dst.params.set(kv.first, kv.second);
    for (const Affine::Row& r : a.rows) {
        bool there = true;
        for (const Affine::Term& t : r.terms)
            if (!(t.of_target ? dst : src).params.has(t.param)) there = false;
        if (!there) continue;
        if (Affine::is_copy(r)) {
            const Affine::Term& t = r.terms[0];
            dst.params.set(r.param, (t.of_target ? dst : src).params.get(t.param));
            continue;
        }
        double v = r.bias;
        for (const Affine::Term& t : r.terms) {
            double k = t.k;
            if (!t.arg.empty()) k *= args ? args->num(t.arg) : 0.0;
            v += k * (t.of_target ? dst : src).params.num(t.param);
        }
        dst.params.set(r.param, v);
    }
}

}  // namespace sg
