#include "sg/net/Distributed.hpp"
#include "DistributedData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <stdexcept>

namespace sg::net {
namespace {
struct AsyncPolicy { bool enabled = false; std::int64_t staleness = 0; double bound = 0; };
AsyncPolicy asynchronous(const State& state, const Cellular& c, const Partition& p, const std::vector<double>& constants, const std::vector<std::size_t>& offsets, std::vector<double>& diagonal, std::vector<double>& off, std::vector<double>& entries, std::vector<std::size_t>& touched) {
    const auto b = detail::integer(state.params(), "max_staleness", 0);
    if (b < 0 || b > 65536) throw std::invalid_argument("net: invalid maximum boundary staleness");
    if (!detail::flag(state.params(), "async", b > 0) || p.boundaries().empty()) return {};
    // A sufficient, deliberately narrow certificate: the free operator is
    // strictly diagonally dominant. No global matrix or remote y is needed.
    const auto n = offsets.back();
    diagonal.resize(n); off.assign(n,0);
    for (std::size_t i = 0; i < n; ++i) diagonal[i] = constants[i*3];
    const double lambda = detail::number(state.params(), "lambda", 1);
    if (lambda < 0) throw std::invalid_argument("net: negative penalty");
    for (const auto& e : c.overlaps()) {
        const auto& data = state.element(e.element).params;
        for (std::uint32_t row = 0; row < e.dimension; ++row) {
            const auto w = lambda*detail::number(data, detail::field("weight", e.dimension, row), detail::number(data, "weight", 1));
            if (w < 0 || !std::isfinite(w)) throw std::invalid_argument("net: invalid async overlap weight");
            touched.clear();
            for (std::uint32_t col = 0; col < c.stalks()[e.left].dimension; ++col) { const auto i = offsets[e.left]+col; if (std::find(touched.begin(),touched.end(),i) == touched.end()) { touched.push_back(i); entries[i] = 0; } entries[i] -= e.left_restriction[row*c.stalks()[e.left].dimension+col]; }
            for (std::uint32_t col = 0; col < c.stalks()[e.right].dimension; ++col) { const auto i = offsets[e.right]+col; if (std::find(touched.begin(),touched.end(),i) == touched.end()) { touched.push_back(i); entries[i] = 0; } entries[i] += e.right_restriction[row*c.stalks()[e.right].dimension+col]; }
            std::sort(touched.begin(),touched.end());
            double sum = 0; for (const auto i : touched) if (!constants[i*3+1]) sum += std::fabs(entries[i]);
            for (const auto i : touched) if (!constants[i*3+1]) {
                const double a = entries[i];
                diagonal[i] += w*a*a;
                off[i] += w*std::fabs(a)*std::max(0.0,sum-std::fabs(a));
            }
        }
    }
    for (std::size_t i = 0; i < diagonal.size(); ++i)
        if (!constants[i*3+1] && (!std::isfinite(diagonal[i]+off[i]) || !(diagonal[i]-off[i] > 64*std::numeric_limits<double>::epsilon()*std::max(1.0,diagonal[i]+off[i])))) return {};
    double bound = 0;
    for (auto v : p.stalks()) for (auto i = offsets[v]; i < offsets[v+1]; ++i)
        if (!constants[i*3+1]) bound = std::max(bound, diagonal[i]+off[i]);
    const double maximum = bound == 0 ? 0 : 1/(static_cast<double>(b+1)*bound);
    const double alpha = detail::number(state.params(), "diffusion_step", 0.05);
    if (alpha <= 0 || (bound != 0 && alpha > maximum)) throw std::invalid_argument("net: diffusion_step exceeds the bounded asynchronous contraction bound");
    return {true,b,maximum};
}
struct Gathered {
    LinearSystem system;
    std::vector<double> y, confidence, weights, current;
};
void gather(const State& state, const Cellular& cellular, const Partition& partition, Gathered& g, bool dynamic) {
    g.y.clear(); g.confidence.clear(); g.weights.clear(); g.current.clear();
    g.system.observations.clear(); g.system.confidence.clear(); g.system.overlap_weights.clear(); g.system.fixed.clear(); g.system.pins.clear();
    const bool diffuse = !partition.boundaries().empty();
    for (auto v : partition.stalks()) {
        const auto& stalk = cellular.stalks()[v];
        const auto& p = state.element(stalk.element).params;
        for (std::uint32_t c = 0; c < stalk.dimension; ++c) {
            const auto key = detail::field("observation", stalk.dimension, c);
            if (dynamic && !p.has(key)) throw std::invalid_argument("net: local participant needs an observation");
            const auto y = detail::number(p, key, 0), m = detail::number(p, detail::field("weight", stalk.dimension, c), detail::number(p, "weight", 1));
            const bool pinned = detail::flag(p, detail::field("pinned", stalk.dimension, c), detail::flag(p, "pinned", false));
            const double pin = detail::number(p, detail::field("pin", stalk.dimension, c), pinned ? y : 0);
            const double current = pinned ? pin : (dynamic ? detail::number(p, detail::field("result", stalk.dimension, c), y) : 0);
            if (m < 0) throw std::invalid_argument("net: negative local confidence");
            g.y.push_back(y); g.confidence.push_back(m); g.current.push_back(current);
            g.system.observations.push_back(diffuse ? current : y); g.system.confidence.push_back(diffuse ? 0 : m);
            g.system.fixed.push_back(pinned); g.system.pins.push_back(pin);
        }
    }
    const auto lambda = detail::number(state.params(), "lambda", 1);
    if (lambda < 0) throw std::invalid_argument("net: negative penalty");
    for (auto e : partition.overlaps()) {
        const auto& overlap = cellular.overlaps()[e];
        const auto& p = state.element(overlap.element).params;
        for (std::uint32_t r = 0; r < overlap.dimension; ++r) {
            const auto w = detail::number(p, detail::field("weight", overlap.dimension, r), detail::number(p, "weight", 1));
            if (w < 0) throw std::invalid_argument("net: negative overlap weight");
            g.weights.push_back(w); g.system.overlap_weights.push_back(diffuse ? lambda*w : w);
        }
    }
    g.system.lambda = diffuse ? 1 : lambda;
    g.system.relaxation = detail::number(state.params(), "relaxation", 0.9);
    const auto iterations = detail::integer(state.params(), "iterations", 128);
    if (iterations < 0 || iterations > 65536) throw std::invalid_argument("net: invalid local iteration count");
    g.system.iterations = static_cast<std::uint32_t>(iterations);

}
void fixed(LinearSystem& s, double value) {
    s.observations.push_back(value); s.confidence.push_back(0); s.fixed.push_back(1); s.pins.push_back(value);
}
double plan(const State& state, const Cellular& cellular, const Partition& partition, Gathered& g, const std::vector<std::size_t>& local, std::vector<double>& bounds) {
    if (partition.boundaries().empty()) { validate_constants(g.system); return step_size(g.system); }
    for (const auto& b : partition.boundaries())
        for (std::uint32_t r = 0; r < b.dimension; ++r) fixed(g.system, 0);
    for (std::size_t i = 0; i < g.y.size(); ++i) { fixed(g.system, g.y[i]); g.system.overlap_weights.push_back(g.confidence[i]); }
    const double alpha = detail::number(state.params(), "diffusion_step", 0.05);
    if (alpha <= 0) throw std::invalid_argument("net: diffusion_step must be positive");
    // Bound the true neighborhood operator, including the remote restriction,
    // independently of the identity used to hold its received projection.
    bounds = g.confidence;
    std::size_t w = 0;
    const double lambda = detail::number(state.params(), "lambda", 1);
    for (auto e : partition.overlaps()) {
        const auto& overlap = cellular.overlaps()[e];
        for (std::uint32_t r = 0; r < overlap.dimension; ++r, ++w) {
            const auto ld = cellular.stalks()[overlap.left].dimension, rd = cellular.stalks()[overlap.right].dimension;
            double sum = 0;
            for (std::uint32_t c = 0; c < ld; ++c) sum += std::fabs(overlap.left_restriction[r*ld+c]);
            for (std::uint32_t c = 0; c < rd; ++c) sum += std::fabs(overlap.right_restriction[r*rd+c]);
            const auto side = [&](std::uint32_t v, std::uint32_t dim, const std::vector<double>& restriction) {
                const auto offset = local[v]; if (offset == std::numeric_limits<std::size_t>::max()) return;
                for (std::uint32_t c = 0; c < dim; ++c) bounds[offset+c] += lambda*g.weights[w]*std::fabs(restriction[r*dim+c])*sum;
            };
            side(overlap.left, ld, overlap.left_restriction); side(overlap.right, rd, overlap.right_restriction);
        }
    }
    for (std::size_t i = 0; i < bounds.size(); ++i)
        if (!g.system.fixed[i] && (!std::isfinite(bounds[i]) || alpha*bounds[i] > 1)) throw std::invalid_argument("net: diffusion_step exceeds the neighborhood bound");
    g.system.relaxation = 1; g.system.iterations = 1;
    validate_constants(g.system);
    const double bound = operator_bound(g.system);
    const double inverse_bound = bound == 0 ? 0 : 1/bound;
    g.system.relaxation = inverse_bound == 0 ? 1 : alpha/inverse_bound;
    if (g.system.relaxation > 1 || !std::isfinite(g.system.relaxation)) throw std::invalid_argument("net: diffusion_step exceeds the local backend bound");
    return bound == 0 ? 0 : g.system.relaxation/bound;
}
void project(const Cellular& c, const Partition& p, const Boundary& b, const std::vector<double>& x, std::size_t offset, std::vector<double>& values) {
    (void)p; const auto dim = c.stalks()[b.stalk].dimension;
    const auto& e = c.overlaps()[b.edge]; const auto& restriction = b.local_left ? e.left_restriction : e.right_restriction;
    values.assign(b.dimension,0);
    for (std::uint32_t r = 0; r < b.dimension; ++r)
        for (std::uint32_t col = 0; col < dim; ++col) values[r] += restriction[r*dim+col]*x[offset+col];
    for (auto value : values) if (!std::isfinite(value)) throw std::overflow_error("net: boundary projection overflow");

}
}
struct Distributed::Workspace {
    const State* owner = nullptr;
    std::int64_t epoch = -1, generation = -1;
    std::uint64_t cellular = 0, partition = 0;
    Gathered gathered;
    SolveWorkspace numeric;
    std::vector<double> constants, observed_constants, diagonal, off, entries, bounds, solved, projection, sent;
    std::vector<std::size_t> offsets, local, touched, boundary_groups;
    std::vector<std::string> neighbors;
    std::vector<Packet> groups;
    std::vector<std::size_t> group_sizes;
    std::array<double,5> controls{};
    AsyncPolicy policy;
    double alpha = 0, relaxation = 0;
    DistributedStats stats;
    explicit Workspace(const Partition& p) : gathered{LinearSystem{p.layout()},{},{},{},{}} {}
};
Distributed::Distributed() = default;
Distributed::~Distributed() = default;
Distributed::Distributed(const Distributed& other) : cellular_(other.cellular_),partition_(other.partition_) {}
Distributed& Distributed::operator=(const Distributed& other) {
    if (this != &other) { cellular_ = other.cellular_; partition_ = other.partition_; workspace_.reset(); } return *this;
}
void Distributed::prepare(const State& state) {
    cellular_.derive(state); partition_.update(state,cellular_);
    if (!workspace_) workspace_ = std::make_unique<Workspace>(partition_);
    auto& w = *workspace_; const auto step = Exchange::step(state);
    const bool changed = w.owner != &state || w.epoch != step.epoch || w.generation != step.generation;
    if (!changed && (w.cellular != cellular_.revision() || w.partition != partition_.compilations())) throw std::invalid_argument("net: topology or assignment changed inside a numerical epoch");
    if (changed) {
        w.numeric.resize(w.offsets,cellular_.stalks().size()+1); w.offsets[0] = 0;
        for (std::size_t i = 0; i < cellular_.stalks().size(); ++i) w.offsets[i+1] = w.offsets[i]+cellular_.stalks()[i].dimension;
    }
    auto& constants = w.observed_constants;
    std::size_t size = w.offsets.back()*3+1; for (const auto& e : cellular_.overlaps()) size += e.dimension;
    w.numeric.resize(constants,size); std::size_t next = 0;
    for (const auto& v : cellular_.stalks()) {
        const auto& p = state.element(v.element).params;
        for (std::uint32_t c = 0; c < v.dimension; ++c) {
            const auto confidence = detail::number(p,detail::field("weight",v.dimension,c),detail::number(p,"weight",1));
            const auto pinned = detail::flag(p,detail::field("pinned",v.dimension,c),detail::flag(p,"pinned",false));
            const auto pin = detail::number(p,detail::field("pin",v.dimension,c),pinned ? detail::number(p,detail::field("observation",v.dimension,c),0) : 0);
            if (confidence < 0) throw std::invalid_argument("net: negative confidence in numerical plan");
            constants[next++] = confidence; constants[next++] = pinned; constants[next++] = pin;
        }
    }
    for (const auto& e : cellular_.overlaps()) for (std::uint32_t r = 0; r < e.dimension; ++r) {
        const auto& p = state.element(e.element).params;
        const auto weight = detail::number(p,detail::field("weight",e.dimension,r),detail::number(p,"weight",1));
        if (weight < 0) throw std::invalid_argument("net: negative overlap weight in numerical plan");
        constants[next++] = weight;
    }
    constants[next] = detail::number(state.params(),"lambda",1);
    if (!changed && constants != w.constants) throw std::invalid_argument("net: numerical constraints changed inside an epoch");
    const auto b = detail::integer(state.params(),"max_staleness",0);
    const std::array<double,5> controls{static_cast<double>(b),static_cast<double>(detail::flag(state.params(),"async",b > 0)),detail::number(state.params(),"diffusion_step",0.05),detail::number(state.params(),"relaxation",0.9),static_cast<double>(detail::integer(state.params(),"iterations",128))};
    if (!changed && controls != w.controls) throw std::invalid_argument("net: execution controls changed inside an epoch");
    if (!changed) return;
    const auto n = partition_.layout().stalk_offsets.back(), m = partition_.layout().overlap_offsets.back();
    auto& g = w.gathered;
    const auto reserve = [&](auto& v,std::size_t capacity) { if (v.capacity() < capacity) { v.reserve(capacity); ++w.numeric.growths; } };
    reserve(g.y,n); reserve(g.confidence,n); reserve(g.weights,m); reserve(g.current,n);
    reserve(g.system.observations,n); reserve(g.system.confidence,n); reserve(g.system.overlap_weights,m); reserve(g.system.fixed,n); reserve(g.system.pins,n);
    reserve(w.entries,w.offsets.back()); w.entries.resize(w.offsets.back()); reserve(w.touched,w.offsets.back()); reserve(w.diagonal,w.offsets.back()); reserve(w.off,w.offsets.back()); reserve(w.bounds,n);
    reserve(w.solved,n); reserve(w.numeric.edge,m); reserve(w.numeric.delta,m); reserve(w.numeric.ax,n);
    reserve(w.projection,m); reserve(w.sent,m);
    w.numeric.resize(w.local,cellular_.stalks().size()); std::fill(w.local.begin(),w.local.end(),std::numeric_limits<std::size_t>::max());
    for (std::size_t i = 0; i < partition_.stalks().size(); ++i) w.local[partition_.stalks()[i]] = partition_.layout().stalk_offsets[i];
    if (!w.stats.layout_validations || w.cellular != cellular_.revision() || w.partition != partition_.compilations()) {
        validate_layout(partition_.layout()); ++w.stats.layout_validations;
    }
    gather(state,cellular_,partition_,g,false);
    w.alpha = plan(state,cellular_,partition_,g,w.local,w.bounds); w.relaxation = g.system.relaxation;
    w.policy = asynchronous(state,cellular_,partition_,constants,w.offsets,w.diagonal,w.off,w.entries,w.touched); ++w.stats.certificates;
    w.neighbors.clear(); w.boundary_groups.clear();
    reserve(w.boundary_groups,partition_.boundaries().size());
    for (const auto& boundary : partition_.boundaries()) {
        auto it = std::find(w.neighbors.begin(),w.neighbors.end(),boundary.neighbor);
        if (it == w.neighbors.end()) { w.neighbors.push_back(boundary.neighbor); it = w.neighbors.end()-1; }
        w.boundary_groups.push_back(static_cast<std::size_t>(it-w.neighbors.begin()));
    }
    w.groups.resize(w.neighbors.size()); w.group_sizes.resize(w.neighbors.size());
    for (std::size_t i = 0; i < w.groups.size(); ++i) { w.groups[i].from = partition_.peer().id; w.groups[i].to = w.neighbors[i]; }
    // Discard any packet metadata from a prior execution selection.
    for (std::size_t i = 0; i < w.groups.size(); ++i) {
        auto& packet = w.groups[i]; packet.boundaries.clear();
        for (std::size_t j = 0; j < partition_.boundaries().size(); ++j) if (w.boundary_groups[j] == i) { const auto& bnd = partition_.boundaries()[j]; packet.boundaries.push_back({bnd.overlap.str(),0,{}}); packet.boundaries.back().values.reserve(bnd.dimension); }
    }
    w.constants = constants; w.controls = controls; w.owner = &state; w.epoch = step.epoch; w.generation = step.generation; w.cellular = cellular_.revision(); w.partition = partition_.compilations(); ++w.stats.plans;
}
std::vector<BoundaryChange> Distributed::receive(const State& s,const Packet& packet) { prepare(s); return Exchange::receive(s,partition_,packet,workspace_->policy.enabled); }
DistributedResult Distributed::evaluate(const State& state,const Backend* backend) { DistributedResult out; evaluate(state,out,backend); return out; }
void Distributed::evaluate(const State& state,DistributedResult& out,const Backend* backend) {
    prepare(state); auto& w = *workspace_; auto& g = w.gathered; gather(state,cellular_,partition_,g,true);
    auto step = Exchange::step(state); const auto policy = w.policy; bool ready = true,published = true;
    for (const auto& b : partition_.boundaries()) {
        const auto& p = state.element(b.overlap).params; const auto sequence = detail::integer(p,"remote_tick",-1);
        ready &= detail::same_step(p,"remote",step) && detail::flag(p,"remote_ready",false) && (policy.enabled ? (sequence >= 0 && (sequence >= step.tick || step.tick-sequence <= policy.staleness)) : sequence == step.tick);
        published &= detail::same_step(p,"sent",step) && detail::integer(p,"sent_tick",-1) == step.tick;
    }
    out.params = state.params(); out.params.set("async_active",policy.enabled).set("async_step_bound",policy.bound);
    out.advanced = false; out.section.residual = 0; out.section.equation_residual = 0; out.section.readings.clear();
    auto& x = g.current;
    if (ready && published) {
        const CpuBackend cpu; const auto& executor = backend ? *backend : static_cast<const Backend&>(cpu);
        if (!executor.available()) throw std::runtime_error("net: requested distributed backend is unavailable");
        if (!partition_.boundaries().empty()) {
            for (const auto& b : partition_.boundaries()) for (std::uint32_t r = 0; r < b.dimension; ++r) { const auto& params = state.element(b.overlap).params; const auto key = detail::field("remote",b.dimension,r); if (!params.has(key)) throw std::invalid_argument("net: missing boundary projection"); fixed(g.system,detail::number(params,key,0)); }
            for (std::size_t i = 0; i < g.y.size(); ++i) { fixed(g.system,g.y[i]); g.system.overlap_weights.push_back(g.confidence[i]); }
            g.system.relaxation = w.relaxation; g.system.iterations = 1;
        }
        validate_dynamic(g.system); const PreparedSystem prepared(g.system,w.alpha);
        executor.solve_prepared(prepared,w.numeric,w.solved);
        if (w.solved.size() != g.system.observations.size()) throw std::runtime_error("net: wrong distributed backend coordinate count");
        for (std::size_t i = 0; i < w.solved.size(); ++i) if (!std::isfinite(w.solved[i]) || (g.system.fixed[i] && w.solved[i] != g.system.pins[i])) throw std::runtime_error("net: invalid distributed solution or boundary");
        const auto residual = executor.residual_prepared(prepared,w.solved,w.numeric);
        if (!std::isfinite(residual.equation) || !std::isfinite(residual.disagreement) || residual.equation < 0 || residual.disagreement < 0) throw std::runtime_error("net: invalid distributed backend residual");
        out.section.equation_residual = residual.equation; coboundary(partition_.layout(),w.solved,w.numeric.delta);
        for (std::size_t r = 0; r < g.weights.size(); ++r) out.section.residual = std::hypot(out.section.residual,std::sqrt(g.weights[r])*w.numeric.delta[r]);
        if (!std::isfinite(out.section.residual)) throw std::overflow_error("net: distributed disagreement overflow");
        x.assign(w.solved.begin(),w.solved.begin()+partition_.coordinates());
        w.numeric.resize(out.section.readings,x.size()); std::size_t r = 0;
        for (std::size_t v = 0; v < partition_.stalks().size(); ++v) { const auto& stalk = cellular_.stalks()[partition_.stalks()[v]];
            for (std::uint32_t c = 0; c < stalk.dimension; ++c) { const auto i = partition_.layout().stalk_offsets[v]+c; const auto correction = x[i]-g.y[i]; if (!std::isfinite(correction)) throw std::overflow_error("net: distributed correction overflow"); out.section.readings[r++] = {stalk.element,c,stalk.dimension,x[i],correction}; }
        }
        if (step.tick == std::numeric_limits<std::int64_t>::max()) throw std::overflow_error("net: distributed round overflow");
        ++step.tick; out.params.set("round",step.tick); out.advanced = true;
    }
    w.numeric.resize(out.boundaries,partition_.boundaries().size()); std::fill(w.group_sizes.begin(),w.group_sizes.end(),0);
    for (std::size_t i = 0; i < partition_.boundaries().size(); ++i) {
        const auto& b = partition_.boundaries()[i]; auto p = state.element(b.overlap).params;
        if (!policy.enabled && out.advanced && detail::same_step(p,"remote",step) && detail::integer(p,"next_tick",-1) == step.tick && detail::flag(p,"next_ready",false)) {
            w.projection.resize(b.dimension); for (std::uint32_t c = 0; c < b.dimension; ++c) { const auto key = detail::field("next_remote",b.dimension,c); if (!p.has(key)) throw std::invalid_argument("net: missing boundary projection"); w.projection[c] = detail::number(p,key,0); }
            detail::projection(p,"remote",w.projection); p.set("remote_tick",step.tick).set("remote_basis",detail::integer(p,"next_basis",-1)).set("remote_ready",true).set("next_tick",std::int64_t{-1}).set("next_ready",false);
        }
        if (!detail::same_step(p,"sent",step) || detail::integer(p,"sent_tick",-1) != step.tick) {
            project(cellular_,partition_,b,x,w.local[b.stalk],w.projection);
            bool changed = !detail::same_step(p,"sent",step);
            if (!changed) for (std::uint32_t c = 0; c < b.dimension; ++c) { const auto key = detail::field("sent",b.dimension,c); if (!p.has(key)) throw std::invalid_argument("net: missing boundary projection"); changed |= w.projection[c] != detail::number(p,key,0); }
            const auto basis = changed ? step.tick : detail::integer(p,"sent_basis",-1); const auto group = w.boundary_groups[i]; auto& packet = w.groups[group]; packet.step = step;
            auto& value = packet.boundaries[w.group_sizes[group]++]; value.overlap = b.overlap.str(); value.basis = basis;
            if (policy.enabled || changed) value.values = w.projection; else value.values.clear();
            p.set("sent_epoch",step.epoch).set("sent_generation",step.generation).set("sent_tick",step.tick).set("sent_basis",basis); detail::projection(p,"sent",w.projection);
        }
        out.boundaries[i] = {b.overlap,std::move(p)};
    }
    std::size_t count = 0; for (auto size : w.group_sizes) count += size != 0;
    w.numeric.resize(out.outgoing,count); count = 0;
    for (std::size_t i = 0; i < w.groups.size(); ++i) if (w.group_sizes[i]) { auto& packet = out.outgoing[count++]; packet.from = w.groups[i].from; packet.to = w.groups[i].to; packet.step = step; packet.boundaries.resize(w.group_sizes[i]); for (std::size_t j = 0; j < w.group_sizes[i]; ++j) packet.boundaries[j] = w.groups[i].boundaries[j]; }
}
DistributedStats Distributed::diagnostics() const { if (!workspace_) return {}; auto stats = workspace_->stats; stats.workspace_growths = workspace_->numeric.growths; return stats; }
} // namespace sg::net
