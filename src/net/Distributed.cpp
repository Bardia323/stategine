#include "sg/net/Distributed.hpp"
#include "DistributedData.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace sg::net {
namespace {
struct Gathered {
    LinearSystem system;
    std::vector<double> y, confidence, weights, current;
};
Gathered gather(const State& state, const Cellular& cellular, const Partition& partition) {
    Gathered g{LinearSystem{partition.layout()}, {}, {}, {}, {}};
    const bool diffuse = !partition.boundaries().empty();
    for (auto v : partition.stalks()) {
        const auto& stalk = cellular.stalks()[v];
        const auto& p = state.element(stalk.element).params;
        for (std::uint32_t c = 0; c < stalk.dimension; ++c) {
            const auto key = detail::field("observation", stalk.dimension, c);
            if (!p.has(key)) throw std::invalid_argument("net: local participant needs an observation");
            const auto y = detail::number(p, key, 0), m = detail::number(p, detail::field("weight", stalk.dimension, c), detail::number(p, "weight", 1));
            const bool pinned = detail::flag(p, detail::field("pinned", stalk.dimension, c), detail::flag(p, "pinned", false));
            const double pin = detail::number(p, detail::field("pin", stalk.dimension, c), pinned ? y : 0);
            const double current = pinned ? pin : detail::number(p, detail::field("result", stalk.dimension, c), y);
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
    return g;
}
void fixed(LinearSystem& s, double value) {
    s.observations.push_back(value); s.confidence.push_back(0); s.fixed.push_back(1); s.pins.push_back(value);
}
void prepare(const State& state, const Cellular& cellular, const Partition& partition, Gathered& g) {
    if (partition.boundaries().empty()) return;
    for (const auto& b : partition.boundaries())
        for (auto value : detail::projection(state.element(b.overlap).params, "remote", b.dimension)) fixed(g.system, value);
    for (std::size_t i = 0; i < g.y.size(); ++i) { fixed(g.system, g.y[i]); g.system.overlap_weights.push_back(g.confidence[i]); }
    const double alpha = detail::number(state.params(), "diffusion_step", 0.05);
    if (alpha <= 0) throw std::invalid_argument("net: diffusion_step must be positive");
    // Bound the true neighborhood operator, including the remote restriction,
    // independently of the identity used to hold its received projection.
    std::map<std::uint32_t, std::size_t> local;
    for (std::size_t i = 0; i < partition.stalks().size(); ++i) local[partition.stalks()[i]] = partition.layout().stalk_offsets[i];
    auto bounds = g.confidence;
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
                const auto found = local.find(v); if (found == local.end()) return;
                for (std::uint32_t c = 0; c < dim; ++c) bounds[found->second+c] += lambda*g.weights[w]*std::fabs(restriction[r*dim+c])*sum;
            };
            side(overlap.left, ld, overlap.left_restriction); side(overlap.right, rd, overlap.right_restriction);
        }
    }
    for (std::size_t i = 0; i < bounds.size(); ++i)
        if (!g.system.fixed[i] && (!std::isfinite(bounds[i]) || alpha*bounds[i] > 1)) throw std::invalid_argument("net: diffusion_step exceeds the neighborhood bound");
    g.system.relaxation = 1; g.system.iterations = 1;
    validate(g.system);
    const double inverse_bound = step_size(g.system);
    g.system.relaxation = inverse_bound == 0 ? 1 : alpha/inverse_bound;
    if (g.system.relaxation > 1 || !std::isfinite(g.system.relaxation)) throw std::invalid_argument("net: diffusion_step exceeds the local backend bound");
}
std::vector<double> project(const Cellular& c, const Partition& p, const Boundary& b, const std::vector<double>& x) {
    const auto local = std::find(p.stalks().begin(), p.stalks().end(), b.stalk) - p.stalks().begin();
    const auto offset = p.layout().stalk_offsets[local], dim = c.stalks()[b.stalk].dimension;
    const auto& e = c.overlaps()[b.edge]; const auto& restriction = b.local_left ? e.left_restriction : e.right_restriction;
    std::vector<double> values(b.dimension, 0);
    for (std::uint32_t r = 0; r < b.dimension; ++r)
        for (std::uint32_t col = 0; col < dim; ++col) values[r] += restriction[r*dim+col]*x[offset+col];
    for (auto value : values) if (!std::isfinite(value)) throw std::overflow_error("net: boundary projection overflow");
    return values;
}
}
std::vector<BoundaryChange> Distributed::receive(const State& s, const Packet& p) {
    cellular_.derive(s); partition_.update(s, cellular_); return Exchange::receive(s, partition_, p);
}
DistributedResult Distributed::evaluate(const State& state, const Backend* backend) {
    cellular_.derive(state); partition_.update(state, cellular_);
    auto g = gather(state, cellular_, partition_);
    auto step = Exchange::step(state);
    bool ready = true, published = true;
    for (const auto& b : partition_.boundaries()) {
        const auto& p = state.element(b.overlap).params;
        ready &= detail::same_step(p, "remote", step) && detail::integer(p, "remote_tick", -1) == step.tick && detail::flag(p, "remote_ready", false);
        published &= detail::same_step(p, "sent", step) && detail::integer(p, "sent_tick", -1) == step.tick;
    }
    DistributedResult out; out.params = state.params();
    auto x = g.current;
    if (ready && published) {
        const CpuBackend cpu;
        const auto& executor = backend ? *backend : static_cast<const Backend&>(cpu);
        if (!executor.available()) throw std::runtime_error("net: requested distributed backend is unavailable");
        prepare(state, cellular_, partition_, g);
        validate(g.system);
        auto solved = executor.solve(g.system);
        if (solved.size() != g.system.observations.size()) throw std::runtime_error("net: wrong distributed backend coordinate count");
        for (std::size_t i = 0; i < solved.size(); ++i)
            if (!std::isfinite(solved[i]) || (g.system.fixed[i] && solved[i] != g.system.pins[i])) throw std::runtime_error("net: invalid distributed solution or boundary");
        const auto residual = executor.residual(g.system, solved);
        if (!std::isfinite(residual.equation) || !std::isfinite(residual.disagreement) || residual.equation < 0 || residual.disagreement < 0)
            throw std::runtime_error("net: invalid distributed backend residual");
        out.section.equation_residual = residual.equation;
        std::vector<double> delta; coboundary(partition_.layout(), solved, delta);
        for (std::size_t r = 0; r < g.weights.size(); ++r) out.section.residual = std::hypot(out.section.residual, std::sqrt(g.weights[r])*delta[r]);
        if (!std::isfinite(out.section.residual)) throw std::overflow_error("net: distributed disagreement overflow");
        x.assign(solved.begin(), solved.begin() + partition_.coordinates());
        for (std::size_t v = 0; v < partition_.stalks().size(); ++v) {
            const auto& stalk = cellular_.stalks()[partition_.stalks()[v]];
            for (std::uint32_t c = 0; c < stalk.dimension; ++c) {
                const auto i = partition_.layout().stalk_offsets[v]+c;
                const auto correction = x[i]-g.y[i];
                if (!std::isfinite(correction)) throw std::overflow_error("net: distributed correction overflow");
                out.section.readings.push_back({stalk.element, c, stalk.dimension, x[i], correction});
            }
        }
        if (step.tick == std::numeric_limits<std::int64_t>::max()) throw std::overflow_error("net: distributed round overflow");
        ++step.tick; out.params.set("round", step.tick); out.advanced = true;
    }
    std::map<std::string, std::size_t> packets;
    for (const auto& b : partition_.boundaries()) {
        auto p = state.element(b.overlap).params;
        if (out.advanced && detail::same_step(p, "remote", step) && detail::integer(p, "next_tick", -1) == step.tick && detail::flag(p, "next_ready", false)) {
            detail::projection(p, "remote", detail::projection(p, "next_remote", b.dimension));
            p.set("remote_tick", step.tick).set("remote_basis", detail::integer(p, "next_basis", -1)).set("remote_ready", true).set("next_tick", std::int64_t{-1}).set("next_ready", false);
        }
        if (!detail::same_step(p, "sent", step) || detail::integer(p, "sent_tick", -1) != step.tick) {
            const auto values = project(cellular_, partition_, b, x);
            const bool changed = !detail::same_step(p, "sent", step) || values != detail::projection(p, "sent", b.dimension);
            const auto basis = changed ? step.tick : detail::integer(p, "sent_basis", -1);
            if (!packets.count(b.neighbor)) {
                packets[b.neighbor] = out.outgoing.size(); out.outgoing.push_back({partition_.peer().id, b.neighbor, step, {}});
            }
            out.outgoing[packets.at(b.neighbor)].boundaries.push_back({b.overlap.str(), basis, changed ? values : std::vector<double>{}});
            p.set("sent_epoch", step.epoch).set("sent_generation", step.generation).set("sent_tick", step.tick).set("sent_basis", basis);
            detail::projection(p, "sent", values);
        }
        out.boundaries.push_back({b.overlap, std::move(p)});
    }
    return out;
}
} // namespace sg::net
