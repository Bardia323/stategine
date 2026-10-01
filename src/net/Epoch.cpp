#include "sg/net/Epoch.hpp"
#include "sg/net/Cellular.hpp"
#include "Canonical.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace sg::net {
namespace {
template<class T> void integers(detail::Writer& w, const std::vector<T>& v) { w.integer(v.size()); for (const auto x : v) w.integer(x); }
void reals(detail::Writer& w, const std::vector<double>& v) { w.integer(v.size()); for (const auto x : v) w.real(x); }
Digest inputs_hash(const std::vector<Observation>& inputs, const std::vector<double>& observations) {
    detail::Writer w("sg.net.inputs.v1"); w.integer(inputs.size());
    // A signature authenticates a statement but is not a unique encoding of it.
    for (const auto& o : inputs) w.bytes(o.statement());
    reals(w,observations); return hash(w.data());
}
std::int64_t quantize(double x, double quantum) {
    if (!std::isfinite(x) || !std::isfinite(quantum) || quantum <= 0) throw std::invalid_argument("net: invalid canonical result number");
    const auto q = std::round(static_cast<long double>(x)/quantum);
    // Below this bound the public double reconstruction preserves its cell.
    if (std::fabs(q) > 281474976710656.0L) throw std::overflow_error("net: canonical result exceeds its quantization range");
    return static_cast<std::int64_t>(q);
}
void require_hash(Digest& given, const Digest& actual) {
    if (given != Digest{} && given != actual) throw std::invalid_argument("net: epoch definition differs from supplied numerical problem");
    given = actual;
}
}
void SolverSpec::validate() const {
    if (method != "bounded-gradient-reference-v1" || rules.empty() || iterations > 65536 || !std::isfinite(lambda) || lambda < 0 || !std::isfinite(relaxation) || relaxation <= 0 || relaxation > 1 || !std::isfinite(quantum) || quantum <= 0 || !std::isfinite(solve_tolerance) || solve_tolerance < 0 || !std::isfinite(residual_tolerance) || residual_tolerance < 0 || !std::isfinite(equation_tolerance) || equation_tolerance < 0) throw std::invalid_argument("net: invalid declared solver specification");
}
Digest SolverSpec::id() const {
    validate(); detail::Writer w("sg.net.solver.v1"); w.text(method); w.text(rules); w.integer(iterations); w.real(lambda); w.real(relaxation);
    w.real(quantum); w.real(solve_tolerance); w.real(residual_tolerance); w.real(equation_tolerance); return hash(w.data());
}
Digest EpochContext::id() const {
    if (world.empty() || partition.empty() || (tick > 0 && previous == Digest{})) throw std::invalid_argument("net: epoch needs a world, partition and finalized predecessor");
    detail::Writer w("sg.net.epoch-context.v1"); w.text(world); w.text(partition); w.integer(tick); w.integer(generation);
    w.digest(topology); w.digest(constraints); w.digest(solver); w.digest(committee); w.digest(previous); return hash(w.data());
}
Digest Epoch::id() const { detail::Writer w("sg.net.epoch.v1"); w.digest(context.id()); w.digest(inputs); return hash(w.data()); }
LinearSystem Problem::system() const {
    LinearSystem s{layout}; s.observations = observations; s.confidence = confidence; s.overlap_weights = overlap_weights;
    s.fixed = fixed; s.pins = pins; s.lambda = solver.lambda; s.relaxation = solver.relaxation; s.iterations = solver.iterations; return s;
}
Problem Problem::make(EpochContext context, const LinearSystem& system, SolverSpec spec, std::vector<Observation> inputs, Bytes declared, Bytes topology) {
    net::validate(system); spec.validate();
    if (spec.lambda != system.lambda || spec.relaxation != system.relaxation || spec.iterations != system.iterations) throw std::invalid_argument("net: solver controls do not describe the supplied problem");
    require_hash(context.topology,topology_hash(system.layout,topology)); require_hash(context.constraints,constraint_hash(system,declared)); require_hash(context.solver,spec.id());
    context.id(); auto ordered = order_inputs(std::move(inputs));
    Problem p; p.epoch = {context,inputs_hash(ordered,system.observations),std::move(ordered)};
    p.layout = system.layout; p.observations = system.observations; p.confidence = system.confidence; p.overlap_weights = system.overlap_weights;
    p.fixed = system.fixed; p.pins = system.pins; p.solver = std::move(spec); p.declared_constraints = std::move(declared); p.declared_topology = std::move(topology); return p;
}
void Problem::validate() const {
    const auto s = system(); net::validate(s); solver.validate(); epoch.context.id();
    const auto ordered = order_inputs(epoch.ordered_inputs);
    if (epoch.context.topology != topology_hash(layout,declared_topology) || epoch.context.constraints != constraint_hash(s,declared_constraints) || epoch.context.solver != solver.id() || epoch.inputs != inputs_hash(ordered,observations)) throw std::invalid_argument("net: modified epoch numerical buffers or inputs");
    for (std::size_t i = 0; i < ordered.size(); ++i) if (ordered[i].id() != epoch.ordered_inputs[i].id()) throw std::invalid_argument("net: epoch inputs are not in canonical order");
}
Digest topology_hash(const Layout& l, const Bytes& declared) {
    detail::Writer w("sg.net.topology.v1"); integers(w,l.stalk_offsets); integers(w,l.overlap_offsets); integers(w,l.row_offsets); integers(w,l.columns); reals(w,l.restrictions);
    integers(w,l.column_offsets); integers(w,l.rows); integers(w,l.transpose_entries); w.bytes(declared); return hash(w.data());
}
Bytes topology_description(const Cellular& c) {
    detail::Writer w("sg.net.cellular-relations.v1"); w.integer(c.stalks().size());
    for (const auto& s : c.stalks()) { w.text(s.element.str()); w.integer(s.dimension); }
    w.integer(c.overlaps().size());
    for (const auto& e : c.overlaps()) { w.text(e.element.str()); w.integer(e.left); w.integer(e.right); w.integer(e.dimension); reals(w,e.left_restriction); reals(w,e.right_restriction); }
    return w.data();
}
Digest constraint_hash(const LinearSystem& s, const Bytes& declared) {
    detail::Writer w("sg.net.constraints.v1"); reals(w,s.confidence); reals(w,s.overlap_weights); reals(w,s.pins); integers(w,s.fixed); w.real(s.lambda); w.bytes(declared); return hash(w.data());
}
std::vector<Observation> order_inputs(std::vector<Observation> inputs) {
    if (inputs.size() > 65536) throw std::length_error("net: too many epoch observations");
    const auto key = [](const Observation& o){ return std::tie(o.tick,o.peer,o.sequence,o.object,o.parameter); };
    std::sort(inputs.begin(),inputs.end(),[&](const Observation& a,const Observation& b){ return key(a) < key(b); });
    for (std::size_t i = 1; i < inputs.size(); ++i)
        if (inputs[i-1].peer == inputs[i].peer && inputs[i-1].context == inputs[i].context && inputs[i-1].sequence == inputs[i].sequence) throw std::invalid_argument("net: duplicate or equivocating epoch input sequence");
    return inputs;
}
Bytes canonical_result(const std::vector<double>& values, double quantum) {
    detail::Writer w("sg.net.result.v1"); w.real(quantum); w.integer(values.size()); for (const auto x : values) w.integer(static_cast<std::uint64_t>(quantize(x,quantum))); return w.data();
}
std::vector<double> canonical_values(const LinearSystem& s, const std::vector<double>& values, double quantum) {
    if (values.size() != s.observations.size()) throw std::invalid_argument("net: wrong canonical coordinate count");
    auto out = values;
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = s.fixed[i] ? s.pins[i] : static_cast<double>(quantize(values[i],quantum))*quantum;
    canonical_result(out,quantum); return out;
}
}
