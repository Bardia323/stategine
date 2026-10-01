#include "sg/net/Placement.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace sg::net {
namespace {
void nonnegative(double x) { if (!std::isfinite(x) || x < 0) throw std::invalid_argument("net: invalid placement cost"); }
struct Planner {
    const Cellular& c;
    const PlacementMetrics& m;
    std::vector<PeerCapacity> peers;
    std::vector<double> costs, migrations, cuts;
    std::vector<int> current;
    std::vector<std::vector<std::size_t>> adjacent;
    std::vector<double> loads(const std::vector<int>& a) const {
        std::vector<double> out(peers.size(),0);
        for (std::size_t v = 0; v < a.size(); ++v) if (a[v] >= 0) out[a[v]] += costs[v];
        return out;
    }
    bool connected(const std::vector<int>& a, int peer) const {
        const auto start = std::find(a.begin(),a.end(),peer); if (start == a.end()) return true;
        std::set<std::size_t> seen{static_cast<std::size_t>(start-a.begin())};
        std::vector<std::size_t> queue{*seen.begin()};
        for (std::size_t i = 0; i < queue.size(); ++i) for (auto v : adjacent[queue[i]])
            if (a[v] == peer && seen.insert(v).second) queue.push_back(v);
        return seen.size() == static_cast<std::size_t>(std::count(a.begin(),a.end(),peer));
    }
    bool feasible(const std::vector<int>& a) const {
        if (std::find(a.begin(),a.end(),-1) != a.end()) return false;
        const auto l = loads(a);
        for (std::size_t p = 0; p < peers.size(); ++p) if (l[p] > peers[p].capacity || !connected(a,static_cast<int>(p))) return false;
        return true;
    }
    double cut(const std::vector<int>& a) const {
        double out = 0;
        for (std::size_t e = 0; e < c.overlaps().size(); ++e) {
            const auto& edge = c.overlaps()[e];
            if (a[edge.left] >= 0 && a[edge.right] >= 0 && a[edge.left] != a[edge.right]) out += cuts[e];
        }
        return out;
    }
    double objective(const std::vector<int>& a) const {
        const auto l = loads(a); double maximum = 0, migration = 0;
        for (std::size_t p = 0; p < peers.size(); ++p) maximum = std::max(maximum,l[p]/peers[p].capacity);
        for (std::size_t v = 0; v < a.size(); ++v) if (a[v] >= 0 && a[v] != current[v]) migration += migrations[v];
        const double out = maximum + m.cut_penalty*cut(a) + m.migration_penalty*migration;
        if (!std::isfinite(out)) throw std::overflow_error("net: placement objective overflow");
        return out;
    }
    std::vector<int> greedy() const {
        std::vector<int> a(current.size(),-1);
        for (std::size_t count = 0; count < a.size(); ++count) {
            double best = std::numeric_limits<double>::infinity(); std::size_t vertex = 0; int peer = -1;
            const auto l = loads(a);
            // Stable vertex/peer order breaks ties. Grow connected regions;
            // an empty peer may seed a region at any unassigned vertex.
            for (std::size_t v = 0; v < a.size(); ++v) if (a[v] < 0)
                for (std::size_t p = 0; p < peers.size(); ++p) {
                    if (l[p]+costs[v] > peers[p].capacity) continue;
                    const bool empty = std::find(a.begin(),a.end(),static_cast<int>(p)) == a.end();
                    if (!empty && std::none_of(adjacent[v].begin(),adjacent[v].end(),[&](auto n){return a[n] == static_cast<int>(p);})) continue;
                    a[v] = static_cast<int>(p); const auto score = objective(a); a[v] = -1;
                    if (score < best) { best = score; vertex = v; peer = static_cast<int>(p); }
                }
            if (peer < 0) return a;
            a[vertex] = peer;
        }
        return a;
    }
    std::vector<int> improve(std::vector<int> a) const {
        const auto limit = a.size()*std::max<std::size_t>(1,peers.size())*4;
        for (std::size_t pass = 0; pass < limit; ++pass) {
            auto best = a; double score = objective(a);
            for (std::size_t v = 0; v < a.size(); ++v) for (std::size_t p = 0; p < peers.size(); ++p) {
                if (a[v] == static_cast<int>(p)) continue;
                auto trial = a; trial[v] = static_cast<int>(p);
                if (!feasible(trial)) continue;
                const auto value = objective(trial);
                if (value < score) { score = value; best = std::move(trial); }
            }
            for (std::size_t from = 0; from < peers.size(); ++from) for (std::size_t to = 0; to < peers.size(); ++to) {
                if (from == to) continue;
                auto trial = a; for (auto& owner : trial) if (owner == static_cast<int>(from)) owner = static_cast<int>(to);
                if (!feasible(trial)) continue;
                const auto value = objective(trial);
                if (value < score) { score = value; best = std::move(trial); }
            }
            if (best == a) break;
            a = std::move(best);
        }
        return a;
    }
};
}
PlacementPlan Placement::plan(const FinalizedPlacementView& view, const Cellular& c, const PlacementMetrics& m, const InterestPolicy& policy) {
    if (view.receipt == Digest{} || view.last_placement_epoch > view.epoch) throw std::invalid_argument("net: placement needs a verified finalized checkpoint and epoch history");
    Cellular declared; declared.derive(view.state);
    if (declared.stalks() != c.stalks() || declared.overlaps() != c.overlaps()) throw std::invalid_argument("net: placement topology differs from its finalized state");
    nonnegative(m.cut_penalty); nonnegative(m.migration_penalty); nonnegative(m.minimum_improvement);
    Planner p{c,m,{},{},{},{},{},{}};
    std::set<std::string> names;
    for (const auto& peer : m.peers) {
        if (peer.peer.id.empty() || !names.insert(peer.peer.id).second) throw std::invalid_argument("net: duplicate or empty placement peer");
        nonnegative(peer.capacity);
        if (peer.available && peer.capacity > 0) p.peers.push_back(peer);
    }
    std::sort(p.peers.begin(),p.peers.end(),[](const auto& a,const auto& b){return a.peer.id < b.peer.id;});
    p.costs.assign(c.stalks().size(),1); p.migrations.assign(c.stalks().size(),1); p.cuts.assign(c.overlaps().size(),1);
    std::set<Key> ids;
    for (const auto& cost : m.stalks) {
        nonnegative(cost.execution); nonnegative(cost.migration);
        const auto v = std::find_if(c.stalks().begin(),c.stalks().end(),[&](const auto& s){return s.element == cost.stalk;});
        if (v == c.stalks().end() || !ids.insert(cost.stalk).second) throw std::invalid_argument("net: unknown or duplicate stalk cost");
        const auto i = v-c.stalks().begin(); p.costs[i] = cost.execution; p.migrations[i] = cost.migration;
    }
    ids.clear();
    for (const auto& cost : m.overlaps) {
        nonnegative(cost.traffic);
        const auto e = std::find_if(c.overlaps().begin(),c.overlaps().end(),[&](const auto& s){return s.element == cost.overlap;});
        if (e == c.overlaps().end() || !ids.insert(cost.overlap).second) throw std::invalid_argument("net: unknown or duplicate overlap cost");
        p.cuts[e-c.overlaps().begin()] = cost.traffic;
    }
    std::vector<ParticipantView> participants;
    for (const auto& s : c.stalks()) {
        const auto& data = view.state.element(s.element).params; participants.push_back({s.element,&data});
        const auto solver = data.get_or<std::string>("solver",view.state.params().get_or<std::string>("peer",{}));
        const auto peer = std::find_if(p.peers.begin(),p.peers.end(),[&](const auto& x){return x.peer.id == solver;});
        p.current.push_back(peer == p.peers.end() ? -1 : static_cast<int>(peer-p.peers.begin()));
    }
    p.adjacent.resize(c.stalks().size());
    for (const auto& e : c.overlaps()) { p.adjacent[e.left].push_back(e.right); p.adjacent[e.right].push_back(e.left); }
    PlacementPlan out; out.generation = view.generation;
    if (policy) out.interest = policy(participants,c.overlaps());
    std::sort(out.interest.begin(),out.interest.end(),[](const auto& a,const auto& b){return a.overlap < b.overlap;});
    ids.clear();
    for (const auto& change : out.interest) {
        if (change.overlap.str().empty() || !ids.insert(change.overlap).second) throw std::invalid_argument("net: duplicate or empty interest proposal");
        nonnegative(change.traffic_cost);
        const bool exists = std::any_of(c.overlaps().begin(),c.overlaps().end(),[&](const auto& e){return e.element == change.overlap;});
        if (change.action == InterestAction::Add && view.state.find(change.overlap)) throw std::invalid_argument("net: an interest edge cannot replace an existing world element");
        if ((change.action == InterestAction::Add) == exists) throw std::invalid_argument("net: interest proposal conflicts with declared topology");
    }
    auto a = p.current;
    const bool valid = p.feasible(a);
    if (!p.peers.empty()) {
        auto greedy = p.greedy();
        if (p.feasible(greedy) && (!valid || p.objective(greedy) < p.objective(a))) a = std::move(greedy);
        if (p.feasible(a)) a = p.improve(std::move(a));
    }
    if (valid && (view.epoch-view.last_placement_epoch < m.hold_epochs || p.objective(p.current)-p.objective(a) < m.minimum_improvement)) a = p.current;
    out.feasible = p.feasible(a);
    if (!out.feasible) return out; // no partial assignment may be applied
    for (std::size_t v = 0; v < a.size(); ++v) {
        const auto& stalk = c.stalks()[v]; const auto& to = p.peers[a[v]].peer.id;
        out.assignments.push_back({stalk.element,to});
        const auto from = view.state.element(stalk.element).params.get_or<std::string>("solver",view.state.params().get_or<std::string>("peer",{}));
        if (from != to) out.migrations.push_back({stalk.element,from,to});
    }
    const auto l = p.loads(a);
    for (std::size_t peer = 0; peer < p.peers.size(); ++peer) out.estimated_load[p.peers[peer].peer.id] = l[peer];
    out.cut_cost = p.cut(a); out.objective = p.objective(a);
    bool interest_changed = false;
    for (const auto& i : out.interest) {
        if (i.action != InterestAction::Keep) interest_changed = true;
        else for (const auto& [key,value] : i.constraint)
            if (!view.state.element(i.overlap).params.has(key) || view.state.element(i.overlap).params.get(key) != value) interest_changed = true;
    }
    out.changed = !out.migrations.empty() || interest_changed;
    if (out.changed) { if (view.generation == UINT64_MAX) throw std::overflow_error("net: placement generation overflow"); ++out.generation; }
    return out;
}
}
