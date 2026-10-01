#include "Secure.hpp"
#include "sg/net/Cellular.hpp"
#include "../../src/net/Canonical.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace sg::examples::pong {
namespace {
net::Bytes position(double a, double b) { net::detail::Writer w("pong.position.v1"); w.real(a); w.real(b); return w.data(); }
std::vector<double> position(const net::Bytes& bytes) {
    net::detail::Reader r(bytes,"pong.position.v1"); std::vector<double> x{r.real(),r.real()}; r.end(); return x;
}
net::Bytes move(std::int64_t n) {
    if (n < -1 || n > 1) throw std::invalid_argument("Pong commands are ternary discrete events");
    net::detail::Writer w("pong.move.v1"); w.integer(static_cast<std::uint64_t>(n+1)); return w.data();
}
std::int64_t move(const net::Bytes& bytes) {
    net::detail::Reader r(bytes,"pong.move.v1"); const auto n = r.integer(); r.end();
    if (n > 2) throw std::invalid_argument("invalid Pong move");
    return static_cast<std::int64_t>(n)-1;
}
}
net::DiscreteInputs controls(const std::vector<net::Observation>& inputs) {
    net::DiscreteInputs result{0,0};
    for (const auto& o : inputs) if (o.discrete) {
        if (o.peer != "p0" && o.peer != "p1") throw std::invalid_argument("unexpected Pong input producer");
        result[o.peer == "p0" ? 0 : 1] = move(o.payload);
    }
    return result;
}
Work work(const State& network, const net::Cellular& cellular, net::Prediction::Step rules, const net::Committee& committee, const net::Agreement& agreement) {
    const auto tick = static_cast<std::uint64_t>(network.params().get_or<std::int64_t>("epoch",0));
    if (tick > (std::numeric_limits<std::uint64_t>::max()-1)/2) throw std::overflow_error("Pong epoch exhausted");
    const auto peer = network.params().get_or<std::string>("peer",{});
    const auto& published = network.element(Key{peer}).params;
    const auto checkpoint = net::unhex(published.get_or<std::string>("snapshot",{}));
    const auto baseline = read_frame(checkpoint);
    const auto layout = cellular.layout(); const auto topology = net::topology_description(cellular);
    net::LinearSystem base(layout); base.observations = {baseline[4],baseline[6],baseline[4],baseline[6]};
    base.confidence.assign(4,1); base.overlap_weights.assign(2,1); base.fixed.assign(4,0); base.pins.assign(4,0);
    net::SolverSpec spec; spec.rules = network.params().get_or<std::string>("solver_rules",{});
    const auto iterations = network.params().num("iterations");
    if (iterations < 0 || iterations > 65536 || iterations != std::floor(iterations)) throw std::invalid_argument("invalid declared Pong solver iteration count");
    spec.iterations = static_cast<std::uint32_t>(iterations);
    spec.lambda = network.params().num("lambda"); spec.relaxation = network.params().num("relaxation");
    spec.quantum = network.params().num("quantum"); spec.solve_tolerance = network.params().num("solve_tolerance");
    spec.residual_tolerance = network.params().num("residual_tolerance"); spec.equation_tolerance = network.params().num("equation_tolerance");
    base.iterations = spec.iterations; base.lambda = spec.lambda; base.relaxation = spec.relaxation;
    Work w;
    w.context = {network.params().get_or<std::string>("world",{}),committee.region,tick,committee.generation,
                 net::topology_hash(layout,topology),net::constraint_hash(base,checkpoint),spec.id(),committee.id(),agreement.previous()};
    for (const auto* id : {"p0","p1"}) {
        w.slots.push_back({id,id,"position",2*tick,false});
        w.slots.push_back({id,id,"move",2*tick+1,true});
    }
    const auto context = w.context;
    w.build = [layout,spec,baseline,checkpoint,context,topology](const std::vector<net::Observation>& inputs) {
        net::LinearSystem s(layout); s.confidence.assign(4,1); s.overlap_weights.assign(2,1); s.fixed.assign(4,0); s.pins.assign(4,0);
        s.observations.assign(4,0); s.lambda = spec.lambda; s.iterations = spec.iterations; s.relaxation = spec.relaxation;
        if (inputs.size() != 4) throw std::invalid_argument("incomplete Pong epoch");
        for (const auto& o : inputs) {
            if (o.peer != "p0" && o.peer != "p1") throw std::invalid_argument("invalid Pong participant");
            if (o.discrete) { move(o.payload); continue; }
            const auto x = position(o.payload); const auto offset = o.peer == "p0" ? 0 : 2;
            // A position is checked against the previous finalized game's rules.
            // A signed participant cannot teleport a paddle by reporting a value.
            if (std::fabs(x[0]-baseline[4]) > 1e-12 || std::fabs(x[1]-baseline[6]) > 1e-12) throw std::invalid_argument("Pong observation contradicts the finalized checkpoint");
            s.observations[offset] = x[0]; s.observations[offset+1] = x[1];
        }
        return net::Problem::make(context,s,spec,inputs,checkpoint,topology);
    };
    w.verify = net::Verify([](const net::Observation& o) {
        try { return o.parameter == "move" && o.object == o.peer && o.conflict == "paddle:"+o.peer && (o.peer == "p0" || o.peer == "p1") && (move(o.payload),true); }
        catch (const std::exception&) { return false; }
    },[baseline,rules](const net::Problem& p,const std::vector<double>&) { return frame_bytes(rules(baseline,controls(p.epoch.ordered_inputs))); });
    net::Observation p; p.peer = peer; p.object = peer; p.parameter = "position"; p.sequence = 2*tick;
    p.payload = position(published.num("position_0"),published.num("position_1")); w.local.push_back(p);
    p.parameter = "move"; p.sequence = 2*tick+1; p.discrete = true; p.conflict = "paddle:"+peer;
    p.payload = move(static_cast<std::int64_t>(published.num("input"))); w.local.push_back(p);
    return w;
}
Params finalization(const net::Protocol& p) {
    if (!p.accepted() || !p.finalized()) throw std::logic_error("Pong finalization requires a verified quorum");
    const auto& context = p.accepted()->epoch().context; const auto inputs = controls(p.accepted()->epoch().ordered_inputs);
    Params args; args.set("epoch",static_cast<std::int64_t>(context.tick)).set("receipt",net::hex(p.finalized()->id()))
        .set("previous",net::hex(context.previous)).set("committee",net::hex(context.committee)).set("topology",net::hex(context.topology))
        .set("left_input",static_cast<double>(inputs[0])).set("right_input",static_cast<double>(inputs[1]));
    for (std::size_t i = 0; i < p.accepted()->values().size(); ++i) args.set(Key{"value_"+std::to_string(i)},p.accepted()->values()[i]);
    return args;
}
}
