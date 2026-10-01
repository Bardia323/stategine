#include "sg/net/Partition.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace sg::net {
namespace {
std::uint32_t index(std::size_t n) {
    if (n > std::numeric_limits<std::uint32_t>::max()) throw std::length_error("net: partition exceeds numerical offsets");
    return static_cast<std::uint32_t>(n);
}
void row(Layout& l, const std::map<std::uint32_t, double>& entries) {
    for (const auto& entry : entries) {
        if (!std::isfinite(entry.second)) throw std::overflow_error("net: partition restriction overflow");
        if (entry.second == 0) continue;
        l.columns.push_back(entry.first); l.restrictions.push_back(entry.second);
    }
    l.row_offsets.push_back(index(l.columns.size()));
}
}
bool Boundary::operator==(const Boundary& b) const {
    return overlap == b.overlap && neighbor == b.neighbor && edge == b.edge && stalk == b.stalk && offset == b.offset && dimension == b.dimension && local_left == b.local_left;
}
bool Partition::update(const State& state, const Cellular& c) {
    const auto* name = state.params().text("peer");
    if (!name || name->empty()) throw std::invalid_argument("net: partition needs a peer id");
    if (built_ && topology_ == c.revision() && peer_.id == *name && owners_.size() == c.stalks().size()) {
        bool same = true;
        for (std::size_t i = 0; i < owners_.size(); ++i) {
            const auto& p = state.element(c.stalks()[i].element).params; const auto* owner = p.text("solver");
            if (p.has("solver") && (!owner || owner->empty())) throw std::invalid_argument("net: solver must be a peer id");
            same &= owners_[i] == (owner ? *owner : *name);
        }
        if (same) { peer_.endpoint = state.params().get_or<std::string>("endpoint",{}); return false; }
    }
    Peer peer{*name, state.params().get_or<std::string>("endpoint", {})};
    std::vector<std::string> owners;
    std::vector<std::uint32_t> stalks, edges, offsets(c.stalks().size(), std::numeric_limits<std::uint32_t>::max());
    Layout l; l.stalk_offsets = {0}; l.overlap_offsets = {0}; l.row_offsets = {0};
    for (std::size_t v = 0; v < c.stalks().size(); ++v) {
        const auto& p = state.element(c.stalks()[v].element).params;
        const auto* owner = p.text("solver");
        if (p.has("solver") && (!owner || owner->empty())) throw std::invalid_argument("net: solver must be a peer id");
        owners.push_back(owner ? *owner : peer.id);
        if (owners.back() != peer.id) continue;
        stalks.push_back(index(v)); offsets[v] = l.stalk_offsets.back();
        l.stalk_offsets.push_back(index(static_cast<std::size_t>(offsets[v]) + c.stalks()[v].dimension));
    }
    const auto coordinates = l.stalk_offsets.back();
    std::vector<Boundary> boundaries;
    for (std::size_t e = 0; e < c.overlaps().size(); ++e) {
        const auto& overlap = c.overlaps()[e];
        const bool left = owners[overlap.left] == peer.id, right = owners[overlap.right] == peer.id;
        if (!left && !right) continue;
        edges.push_back(index(e));
        if (left == right) continue;
        boundaries.push_back({overlap.element, owners[left ? overlap.right : overlap.left], index(e), left ? overlap.left : overlap.right,
            l.stalk_offsets.back(), overlap.dimension, left});
        l.stalk_offsets.push_back(index(static_cast<std::size_t>(l.stalk_offsets.back()) + overlap.dimension));
    }
    peer_.endpoint = peer.endpoint;
    owners_ = owners;
    if (built_ && topology_ == c.revision() && peer_.id == peer.id && stalks_ == stalks && overlaps_ == edges && boundaries_ == boundaries) return false;
    topology_ = c.revision();
    for (const auto e : edges) {
        const auto& overlap = c.overlaps()[e];
        const auto boundary = std::find_if(boundaries.begin(), boundaries.end(), [e](const Boundary& b) { return b.edge == e; });
        l.overlap_offsets.push_back(index(static_cast<std::size_t>(l.overlap_offsets.back()) + overlap.dimension));
        for (std::uint32_t r = 0; r < overlap.dimension; ++r) {
            std::map<std::uint32_t, double> entries;
            const auto side = [&](std::uint32_t v, const std::vector<double>& restriction, double sign) {
                if (owners[v] != peer.id) { entries[boundary->offset + r] += sign; return; }
                const auto dim = c.stalks()[v].dimension;
                for (std::uint32_t col = 0; col < dim; ++col) entries[offsets[v] + col] += sign * restriction[r * dim + col];
            };
            side(overlap.left, overlap.left_restriction, -1); side(overlap.right, overlap.right_restriction, 1);
            row(l, entries);
        }
    }
    // Confidence becomes a restriction to a fixed observation. This permits
    // the unchanged backend to take a gradient step starting at the state's x.
    if (!boundaries.empty())
        for (std::uint32_t col = 0; col < coordinates; ++col) {
            const auto ghost = l.stalk_offsets.back();
            l.stalk_offsets.push_back(index(static_cast<std::size_t>(ghost) + 1));
            l.overlap_offsets.push_back(index(static_cast<std::size_t>(l.overlap_offsets.back()) + 1));
            row(l, {{col, -1}, {ghost, 1}});
        }
    l.column_offsets.assign(static_cast<std::size_t>(l.stalk_offsets.back()) + 1, 0);
    for (auto col : l.columns) ++l.column_offsets[col + 1];
    for (std::size_t col = 1; col < l.column_offsets.size(); ++col) l.column_offsets[col] += l.column_offsets[col - 1];
    auto next = l.column_offsets;
    l.rows.resize(l.columns.size()); l.transpose_entries.resize(l.columns.size());
    for (std::size_t r = 0; r + 1 < l.row_offsets.size(); ++r)
        for (auto j = l.row_offsets[r]; j < l.row_offsets[r + 1]; ++j) {
            const auto dest = next[l.columns[j]]++; l.rows[dest] = index(r); l.transpose_entries[dest] = j;
        }
    if (built_ && peer_.id == peer.id && stalks_ == stalks && overlaps_ == edges && boundaries_ == boundaries && layout_ == l) return false;
    peer_ = std::move(peer); stalks_ = std::move(stalks); overlaps_ = std::move(edges); boundaries_ = std::move(boundaries);
    coordinates_ = coordinates; layout_ = std::move(l); built_ = true; ++compilations_;
    return true;
}
} // namespace sg::net
