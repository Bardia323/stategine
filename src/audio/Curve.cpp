#include "sg/audio/Curve.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "sg/audio/Ramp.hpp"
#include "sg/audio/Sound.hpp"

namespace sg::audio {

namespace {

// A column: numbers apart by spaces. False on a word that is not a number.
bool numbers(const std::string& text, std::vector<double>& out) {
    std::istringstream in(text);
    std::string w;
    while (in >> w) {
        char* end = nullptr;
        const double v = std::strtod(w.c_str(), &end);
        if (end == w.c_str() || *end != '\0') return false;
        out.push_back(v);
    }
    return true;
}

}  // namespace

Heard Curve::at(double d) const {
    Heard h;
    if (rows.empty()) return h;
    // The first row further than `d`: `d` lies between it and the one before.
    const auto it = std::upper_bound(rows.begin(), rows.end(), d, [](double v, const Row& r) { return v < r.d; });
    Row r;
    if (it == rows.begin()) {
        r = rows.front();
    } else if (it == rows.end()) {
        r = rows.back();
    } else {
        const Row& a = *(it - 1);
        const Row& b = *it;
        const double span = b.d - a.d;
        const double u = span > 0 ? (d - a.d) / span : 0.0;
        r.db = a.db + (b.db - a.db) * u;
        r.send = a.send + (b.send - a.send) * u;
        r.cut = a.cut + (b.cut - a.cut) * u;
        r.spread = a.spread + (b.spread - a.spread) * u;
    }
    h.gain = db_to_gain(r.db);
    h.send = r.send;
    h.cut = r.cut;
    h.spread = r.spread;
    return h;
}

bool curve_of(const Element& e, Curve& out, std::string* why) {
    const char* names[5] = {"d", "db", "send", "cut", "spread"};
    std::vector<double> cols[5];
    for (int c = 0; c < 5; ++c) {
        const std::string* t = e.params.text(Key{names[c]});
        if (!t) {
            if (why) *why = std::string("no column ") + names[c];
            return false;
        }
        if (!numbers(*t, cols[c])) {
            if (why) *why = std::string("column ") + names[c] + " has a word that is not a number";
            return false;
        }
        if (cols[c].size() != cols[0].size() || cols[c].empty()) {
            if (why) *why = std::string("column ") + names[c] + " is not as long as d";
            return false;
        }
        for (double v : cols[c])
            if (!std::isfinite(v)) {
                if (why) *why = std::string("column ") + names[c] + " has a number that is not finite";
                return false;
            }
    }
    Curve made;
    for (std::size_t i = 0; i < cols[0].size(); ++i) {
        if (i > 0 && cols[0][i] < cols[0][i - 1]) {
            if (why) *why = "its distances go backwards at row " + std::to_string(i);
            return false;
        }
        made.rows.push_back(Curve::Row{cols[0][i], cols[1][i], cols[2][i], cols[3][i], cols[4][i]});
    }
    out = std::move(made);
    return true;
}

const Curve& standard_curve() {
    // -19 log10(max(0.6, d)) dB, 18000 / (1 + 0.04 d) Hz, 0.35 + 0.3 min(1, d / 5)
    // sent, and from both sides within half a metre - at enough distances that
    // a straight line between two is within a fraction of a dB of the formula.
    static const Curve c{{
        {0.0, 4.2151, 0.35, 18000.0, 1.0},
        {0.5, 4.2151, 0.38, 17647.1, 0.0},
        {0.6, 4.2151, 0.386, 17578.1, 0.0},
        {0.8, 1.8413, 0.398, 17441.9, 0.0},
        {1.0, 0.0, 0.41, 17307.7, 0.0},
        {1.5, -3.3457, 0.44, 16981.1, 0.0},
        {2.0, -5.7196, 0.47, 16666.7, 0.0},
        {3.0, -9.0653, 0.53, 16071.4, 0.0},
        {4.0, -11.4391, 0.59, 15517.2, 0.0},
        {5.0, -13.2804, 0.65, 15000.0, 0.0},
        {6.0, -14.7849, 0.65, 14516.1, 0.0},
        {8.0, -17.1587, 0.65, 13636.4, 0.0},
        {12.0, -20.5044, 0.65, 12162.2, 0.0},
        {16.0, -22.8783, 0.65, 10975.6, 0.0},
        {24.0, -26.2240, 0.65, 9183.7, 0.0},
        {32.0, -28.5979, 0.65, 7894.7, 0.0},
        {48.0, -31.9436, 0.65, 6164.4, 0.0},
        {64.0, -34.3174, 0.65, 5056.2, 0.0},
        {96.0, -37.6631, 0.65, 3719.0, 0.0},
        {128.0, -40.0370, 0.65, 2941.2, 0.0},
    }};
    return c;
}

void Curves::read(const StateGraph& g) {
    if (g.revision() != revision_) {
        revision_ = g.revision();
        where_.clear();
        curves_.clear();
        for (Key id : g.ids()) {
            const State* s = g.find(id);
            if (!s) continue;
            for (const Element& e : s->elements())
                if (e.kind == kinds::curve) where_.push_back(Where{id, e.id, 0});
        }
    }
    for (Where& w : where_) {
        const State* s = g.find(w.state);
        const Element* e = s ? s->find(w.element) : nullptr;
        if (!e) continue;
        if (w.stamp != 0 && e->params.stamp() == w.stamp) continue;
        w.stamp = e->params.stamp();
        Curve c;
        if (curve_of(*e, c)) curves_[w.element] = std::move(c);
        else curves_.erase(w.element);
    }
}

const Curve& Curves::of(Key name) const {
    auto it = curves_.find(name);
    return it == curves_.end() ? standard_curve() : it->second;
}

std::vector<std::string> curve_defects(const StateGraph& g) {
    std::vector<std::string> out;
    for (Key id : g.ids()) {
        const State* s = g.find(id);
        if (!s) continue;
        for (const Element& e : s->elements()) {
            if (e.kind != kinds::curve) continue;
            Curve c;
            std::string why;
            if (!curve_of(e, c, &why)) out.push_back("curve " + id.str() + "." + e.id.str() + ": " + why);
        }
    }
    return out;
}

}  // namespace sg::audio
