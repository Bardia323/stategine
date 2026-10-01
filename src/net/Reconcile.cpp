#include "sg/net/Reconcile.hpp"
#include <cmath>
#include <stdexcept>

namespace sg::net {
Result Reconcile::evaluate(const State& state, const Backend* backend) {
    const auto system = cellular_.gather(state);
    const CpuBackend cpu;
    const auto& executor = backend ? *backend : static_cast<const Backend&>(cpu);
    if (!executor.available()) throw std::runtime_error("net: requested numerical backend is unavailable");
    const auto values = executor.solve(system);
    if (values.size() != system.observations.size()) throw std::runtime_error("net: backend returned the wrong number of coordinates");
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) throw std::runtime_error("net: backend returned a nonfinite value");
        if (system.fixed[i] && values[i] != system.pins[i]) throw std::runtime_error("net: backend changed a fixed variable");
    }
    const auto residual = executor.residual(system, values);
    if (!std::isfinite(residual.disagreement) || !std::isfinite(residual.equation) || residual.disagreement < 0 || residual.equation < 0)
        throw std::runtime_error("net: backend returned invalid residuals");
    Result result;
    result.residual = residual.disagreement; result.equation_residual = residual.equation;
    for (std::size_t v = 0; v < cellular_.stalks().size(); ++v) {
        const auto& stalk = cellular_.stalks()[v];
        for (std::uint32_t c = 0; c < stalk.dimension; ++c) {
            const auto i = system.layout.stalk_offsets[v] + c;
            const double correction = values[i] - system.observations[i];
            if (!std::isfinite(correction)) throw std::overflow_error("net: correction overflow");
            result.readings.push_back({stalk.element, c, stalk.dimension, values[i], correction});
        }
    }
    return result;
}
} // namespace sg::net
