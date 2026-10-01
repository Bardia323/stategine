#include "sg/net/Cellular.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace sg::net {
namespace {
double number(const Params& p, Key key, double fallback) {
    if (!p.has(key)) return fallback;
    const auto& value = p.get(key);
    if (!std::holds_alternative<double>(value) && !std::holds_alternative<int64_t>(value))
        throw std::invalid_argument("net: " + key.str() + " must be numeric");
    const double n = p.num(key);
    if (!std::isfinite(n)) throw std::invalid_argument("net: " + key.str() + " must be finite");
    return n;
}
std::uint32_t integer(const Params& p, Key key, double fallback, bool zero = false) {
    const double n = number(p, key, fallback);
    if (n < (zero ? 0 : 1) || n != std::floor(n) || n > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("net: " + key.str() + " must be an unsigned integer");
    return static_cast<std::uint32_t>(n);
}
bool flag(const Params& p, Key key, bool fallback) {
    if (!p.has(key)) return fallback;
    if (!std::holds_alternative<bool>(p.get(key))) throw std::invalid_argument("net: " + key.str() + " must be boolean");
    return p.get_or<bool>(key, fallback);
}
std::uint32_t index(std::size_t n) {
    if (n > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("net: numerical layout exceeds 32-bit offsets");
    return static_cast<std::uint32_t>(n);
}
void append_offset(std::vector<std::uint32_t>& offsets, std::uint32_t dimension) {
    offsets.push_back(index(static_cast<std::size_t>(offsets.back()) + dimension));
}
std::vector<double> restriction(const Params& p, const char* side, std::uint32_t rows, std::uint32_t cols) {
    const double scale = number(p, Key{std::string(side) + "_scale"}, 1);
    std::vector<double> out;
    index(static_cast<std::size_t>(rows) * cols);
    for (std::uint32_t r = 0; r < rows; ++r)
        for (std::uint32_t c = 0; c < cols; ++c)
            out.push_back(number(p, Key{std::string(side) + "_" + std::to_string(r) + "_" + std::to_string(c)}, r == c ? scale : 0));
    return out;
}
Layout compile(const std::vector<Stalk>& stalks, const std::vector<Overlap>& overlaps) {
    Layout l;
    l.stalk_offsets = {0}; l.overlap_offsets = {0}; l.row_offsets = {0};
    for (const auto& stalk : stalks) append_offset(l.stalk_offsets, stalk.dimension);
    for (const auto& overlap : overlaps) {
        append_offset(l.overlap_offsets, overlap.dimension);
        const auto left_dim = stalks[overlap.left].dimension, right_dim = stalks[overlap.right].dimension;
        for (std::uint32_t r = 0; r < overlap.dimension; ++r) {
            std::map<std::uint32_t, double> row;
            for (std::uint32_t c = 0; c < left_dim; ++c) row[l.stalk_offsets[overlap.left] + c] -= overlap.left_restriction[r * left_dim + c];
            for (std::uint32_t c = 0; c < right_dim; ++c) row[l.stalk_offsets[overlap.right] + c] += overlap.right_restriction[r * right_dim + c];
            for (const auto& entry : row) {
                if (!std::isfinite(entry.second)) throw std::overflow_error("net: restriction difference overflow");
                if (entry.second == 0) continue;
                l.columns.push_back(entry.first); l.restrictions.push_back(entry.second);
            }
            l.row_offsets.push_back(index(l.columns.size()));
        }
    }
    l.column_offsets.assign(static_cast<std::size_t>(l.stalk_offsets.back()) + 1, 0);
    for (const auto c : l.columns) ++l.column_offsets[c + 1];
    for (std::size_t c = 1; c < l.column_offsets.size(); ++c) l.column_offsets[c] += l.column_offsets[c - 1];
    l.rows.resize(l.columns.size()); l.transpose_entries.resize(l.columns.size());
    auto next = l.column_offsets;
    for (std::size_t r = 0; r + 1 < l.row_offsets.size(); ++r)
        for (auto j = l.row_offsets[r]; j < l.row_offsets[r + 1]; ++j) {
            const auto dest = next[l.columns[j]]++;
            l.rows[dest] = index(r); l.transpose_entries[dest] = j;
        }
    return l;
}
}
bool Stalk::operator==(const Stalk& s) const { return element == s.element && dimension == s.dimension; }
bool Overlap::operator==(const Overlap& e) const {
    return element == e.element && left == e.left && right == e.right && dimension == e.dimension &&
        left_restriction == e.left_restriction && right_restriction == e.right_restriction;
}
Cellular::Cellular(const State& state) { update(state); }
Key Cellular::coordinate_key(Key base, std::uint32_t dimension, std::uint32_t coordinate) {
    return dimension == 1 ? base : Key{base.str() + "_" + std::to_string(coordinate)};
}
bool Cellular::derive(const State& state) {
    // Check the retained metadata without allocating another candidate layout.
    if (built_) {
        std::size_t vertices = 0, edges = 0;
        for (const auto& e : state.elements()) { vertices += e.kind == Key{"participant"}; edges += e.kind == Key{"constraint"}; }
        bool same = vertices == stalks_.size() && edges == overlaps_.size();
        for (const auto& v : stalks_) {
            const auto* e = state.find(v.element);
            if (!e || e->kind != Key{"participant"} || integer(e->params,"dim",1) != v.dimension) { same = false; break; }
        }
        for (std::size_t i = 0; same && i < overlaps_.size(); ++i) {
            const auto& v = overlaps_[i]; const auto* e = state.find(v.element);
            if (!e || e->kind != Key{"constraint"} || integer(e->params,"dim",1) != v.dimension || !e->params.text("left") || !e->params.text("right") || *e->params.text("left") != stalks_[v.left].element.str() || *e->params.text("right") != stalks_[v.right].element.str()) { same = false; break; }
            const auto check = [&](const char* side,const std::vector<Key>& keys,const std::vector<double>& entries,std::uint32_t dim) {
                const auto scale = number(e->params,Key{std::string(side)+"_scale"},1);
                for (std::size_t j = 0; j < keys.size(); ++j) if (number(e->params,keys[j],j/dim == j%dim ? scale : 0) != entries[j]) return false;
                return true;
            };
            same = check("left",restriction_keys_[i].left,v.left_restriction,stalks_[v.left].dimension) && check("right",restriction_keys_[i].right,v.right_restriction,stalks_[v.right].dimension);
        }
        if (same) return false;
    }
    std::vector<Stalk> stalks;
    for (const auto& e : state.elements())
        if (e.kind == Key{"participant"}) stalks.push_back({e.id, integer(e.params, "dim", 1)});
    std::sort(stalks.begin(), stalks.end(), [](const Stalk& a, const Stalk& b) { return a.element < b.element; });
    std::unordered_map<Key, std::uint32_t> indices;
    for (std::size_t i = 0; i < stalks.size(); ++i) indices.emplace(stalks[i].element, index(i));
    std::vector<Overlap> overlaps;
    for (const auto& e : state.elements()) {
        if (e.kind != Key{"constraint"}) continue;
        const auto endpoint = [&](Key key) {
            const auto* id = e.params.text(key);
            if (!id || indices.find(Key{*id}) == indices.end()) throw std::invalid_argument("net: " + e.id.str() + "." + key.str() + " needs a participant");
            return indices.at(Key{*id});
        };
        const auto left = endpoint("left"), right = endpoint("right"), dim = integer(e.params, "dim", 1);
        overlaps.push_back({e.id, left, right, dim, restriction(e.params, "left", dim, stalks[left].dimension), restriction(e.params, "right", dim, stalks[right].dimension)});
    }
    std::sort(overlaps.begin(), overlaps.end(), [](const Overlap& a, const Overlap& b) { return a.element < b.element; });
    if (built_ && stalks == stalks_ && overlaps == overlaps_) return false;
    stalks_ = std::move(stalks); overlaps_ = std::move(overlaps);
    restriction_keys_.clear(); restriction_keys_.resize(overlaps_.size());
    for (std::size_t i = 0; i < overlaps_.size(); ++i) {
        const auto& e = overlaps_[i];
        const auto keys = [&](const char* side,std::uint32_t dim,std::vector<Key>& out) {
            for (std::uint32_t r = 0; r < e.dimension; ++r) for (std::uint32_t c = 0; c < dim; ++c) out.emplace_back(std::string(side)+"_"+std::to_string(r)+"_"+std::to_string(c));
        };
        keys("left",stalks_[e.left].dimension,restriction_keys_[i].left); keys("right",stalks_[e.right].dimension,restriction_keys_[i].right);
    }
    built_ = true; compiled_ = false; ++revision_;
    return true;
}
bool Cellular::update(const State& state) {
    derive(state);
    if (compiled_) return false;
    layout_ = compile(stalks_, overlaps_); compiled_ = true; ++compilations_;
    return true;
}
const Layout& Cellular::layout() const {
    if (!compiled_) throw std::logic_error("net: metadata-only cellular topology has no compiled numerical layout");
    return layout_;
}
LinearSystem Cellular::gather(const State& state) {
    update(state);
    LinearSystem s{layout_};
    for (const auto& stalk : stalks_) {
        const auto& p = state.element(stalk.element).params;
        for (std::uint32_t c = 0; c < stalk.dimension; ++c) {
            const auto observation = coordinate_key("observation", stalk.dimension, c);
            if (!p.has(observation)) throw std::invalid_argument("net: " + stalk.element.str() + " needs " + observation.str());
            const double y = number(p, observation, 0);
            s.observations.push_back(y);
            s.confidence.push_back(number(p, coordinate_key("weight", stalk.dimension, c), number(p, "weight", 1)));
            const bool fixed = flag(p, coordinate_key("pinned", stalk.dimension, c), flag(p, "pinned", false));
            s.fixed.push_back(fixed);
            s.pins.push_back(number(p, coordinate_key("pin", stalk.dimension, c), fixed ? y : 0));
        }
    }
    for (const auto& overlap : overlaps_) {
        const auto& p = state.element(overlap.element).params;
        for (std::uint32_t r = 0; r < overlap.dimension; ++r)
            s.overlap_weights.push_back(number(p, coordinate_key("weight", overlap.dimension, r), number(p, "weight", 1)));
    }
    s.lambda = number(state.params(), "lambda", 1);
    s.relaxation = number(state.params(), "relaxation", 0.9);
    s.iterations = integer(state.params(), "iterations", 128, true);
    validate(s);
    return s;
}
} // namespace sg::net
