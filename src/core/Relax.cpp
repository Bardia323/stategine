#include "sg/core/Relax.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <variant>

namespace sg {

double Gap::norm() const {
    double s = 0;
    for (double d : by) s += d * d;
    return std::sqrt(s);
}

Gap gap(const StateGraph& g, const Functor& f) {
    Gap out;
    const State* src = g.find(f.from());
    const State* dst = g.find(f.to());
    if (!src || !dst) return out;
    // What the functor says, made where nothing else sees it: the relation's
    // answer, not written anywhere.
    State said(Key{"gap.scratch." + f.name().str()});
    f.apply(*src, said);
    std::vector<std::pair<std::string, double>> parts;
    for (const Element& e : said.elements()) {
        const Element* is = dst->find(e.id);
        if (!is) continue;
        for (const auto& kv : e.params) {
            const bool number = std::holds_alternative<double>(kv.second) || std::holds_alternative<int64_t>(kv.second);
            if (!number || !is->params.has(kv.first)) continue;
            parts.emplace_back(e.id.str() + "." + kv.first.str(),
                               e.params.num(kv.first) - is->params.num(kv.first));
        }
    }
    std::sort(parts.begin(), parts.end());
    for (auto& p : parts) {
        out.where.push_back(std::move(p.first));
        out.by.push_back(p.second);
    }
    return out;
}

namespace {

double apart(const Gap& a, const Gap& b) {
    if (a.by.size() != b.by.size()) return INFINITY;
    double most = 0;
    for (std::size_t i = 0; i < a.by.size(); ++i) most = std::max(most, std::fabs(a.by[i] - b.by[i]));
    return most;
}

// The fewest steps after which every gap of the second half is what it was,
// with two whole periods there to see it; 0 when there is none.
int period_of(const std::vector<Gap>& seen, double tol) {
    const std::size_t from = seen.size() / 2;
    const std::size_t tail = seen.size() - from;
    for (int k = 1; k <= Orbit::kMostPeriod && tail >= 2 * static_cast<std::size_t>(k) + 1; ++k) {
        bool back = true;
        for (std::size_t i = from; back && i + k < seen.size(); ++i) back = apart(seen[i], seen[i + k]) <= tol;
        if (back) return k;
    }
    return 0;
}

}  // namespace

Orbit orbit(const std::vector<Gap>& seen, double tol) {
    Orbit o;
    if (seen.empty()) return o;
    std::vector<double> n;
    for (const Gap& g : seen) n.push_back(g.norm());
    o.first = n.front();
    o.last = n.back();
    o.peak = *std::max_element(n.begin(), n.end());
    for (std::size_t i = 1; i < n.size(); ++i)
        if (n[i] > n[i - 1] * (1 + 1e-12) + 1e-15) o.monotone = false;
    if (o.last <= tol) {
        o.kind = Orbit::Kind::Converges;
        return o;
    }
    o.period = period_of(seen, tol);
    if (o.period >= 2) {
        o.kind = Orbit::Kind::Periodic;
        return o;
    }
    // Grown past anything it was in its first half: it is going away.
    const double early = *std::max_element(n.begin(), n.begin() + static_cast<std::ptrdiff_t>(n.size() / 2 + 1));
    o.kind = (o.period == 0 && o.last > early + tol) ? Orbit::Kind::Grows : Orbit::Kind::Persists;
    return o;
}

std::string Orbit::str() const {
    const char* k = kind == Kind::Converges ? "converges" : kind == Kind::Periodic ? "periodic" : kind == Kind::Grows ? "grows" : "persists";
    std::string s = k;
    if (period) s += " (period " + std::to_string(period) + ")";
    s += monotone ? ", monotone" : ", not monotone";
    s += ", gap " + std::to_string(first) + " -> " + std::to_string(last) + ", at most " + std::to_string(peak);
    return s;
}

std::vector<std::string> locality_defects(const StateGraph& g, const Cover& c, Key whole) {
    std::vector<std::string> out;
    const State* w = g.find(whole);
    if (!w) {
        out.push_back("no state " + whole.str());
        return out;
    }
    // What some piece carries away and brings back: object, parameter.
    std::set<std::pair<Key, Key>> held;
    for (const Overlap& o : c.overlaps()) {
        if (o.u != whole && o.v != whole) continue;
        const Key piece = c.other_side(o, whole);
        const Functor* there = c.transition(g, o, whole);
        const Functor* back = c.transition(g, o, piece);
        if (!there || !back) {
            out.push_back("overlap " + o.name.str() + ": a transition is not in the graph");
            continue;
        }
        const Functor round = Functor::compose(*there, *back);
        // What comes back must come back as it was (descent, on this overlap)...
        for (const std::string& d : identity_defects(g, round, whole.str() + " -> " + piece.str() + " -> " + whole.str()))
            out.push_back(d);
        // ...and what comes back at all is what this piece holds of the whole.
        State scratch(Key{"locality.scratch." + o.name.str()});
        round.apply(*w, scratch);
        for (const Element& e : w->elements()) {
            if (round.image_object(e.id) != e.id) continue;
            const Element* b = scratch.find(e.id);
            if (!b) continue;
            for (const auto& kv : e.params)
                if (b->params.has(kv.first)) held.insert({e.id, kv.first});
        }
    }
    for (const Element& e : w->elements())
        for (const auto& kv : e.params)
            if (!held.count({e.id, kv.first}))
                out.push_back(whole.str() + ": " + e.id.str() + "." + kv.first.str() + " is held by no piece");
    return out;
}

}  // namespace sg
