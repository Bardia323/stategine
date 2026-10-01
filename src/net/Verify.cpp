#include "sg/net/Verify.hpp"
#include "Canonical.hpp"
#include <cmath>
#include <set>
#include <stdexcept>

namespace sg::net {
namespace {
void require(bool condition, const char* reason) { if (!condition) throw std::invalid_argument(reason); }
void numerics(const Problem& p, const std::vector<double>& x) {
    require(x.size() == p.observations.size(),"net: proposal coordinate count differs");
    for (std::size_t i = 0; i < x.size(); ++i) require(std::isfinite(x[i]) && (!p.fixed[i] || x[i] == p.pins[i]),"net: proposal breaks a hard pin or contains a nonfinite number");
    const auto r = measure(p.system(),x);
    require(std::isfinite(r.disagreement) && r.disagreement <= p.solver.residual_tolerance,"net: proposal disagreement exceeds the declared tolerance");
    require(std::isfinite(r.equation) && r.equation <= p.solver.equation_tolerance,"net: proposal equation residual exceeds the declared tolerance");
}
Digest decision(const Epoch& e, const Digest& result, const Digest& checkpoint) {
    detail::Writer w("sg.net.decision.v1"); w.digest(e.id()); w.digest(result); w.digest(checkpoint); return hash(w.data());
}
}
Bytes ResultCertificate::statement() const {
    detail::Writer w("sg.net.result-certificate.v1"); w.digest(epoch); w.digest(inputs); w.digest(topology); w.digest(constraints); w.digest(solver); w.digest(result); w.digest(checkpoint);
    w.real(residual.disagreement); w.real(residual.equation); w.text(signer); return w.data();
}
const Epoch& VerifiedResult::epoch() const { return epoch_; }
const std::vector<double>& VerifiedResult::values() const { return values_; }
const Bytes& VerifiedResult::checkpoint() const { return checkpoint_; }
Digest VerifiedResult::result() const { return result_; }
Digest VerifiedResult::decision() const { return decision_; }
Residual VerifiedResult::residual() const { return residual_; }
Verify::Verify(Rule rule, Checkpoint checkpoint) : rule_(std::move(rule)),checkpoint_(std::move(checkpoint)) {}
void Verify::inputs(const Problem& p, const Committee& c) const {
    p.validate(); c.validate();
    require(p.epoch.context.committee == c.id() && p.epoch.context.partition == c.region && p.epoch.context.generation == c.generation,"net: proposal committee differs from the declared region");
    std::set<std::string> conflicts;
    for (const auto& o : p.epoch.ordered_inputs) {
        require(o.context == p.epoch.context.id() && o.tick == p.epoch.context.tick && o.authentic(c),"net: unsigned, forged or wrong-epoch input");
        if (o.discrete) {
            require(rule_ && rule_(o),"net: discrete event fails its declared deterministic rule");
            require(conflicts.insert(o.conflict).second,"net: conflicting discrete events cannot belong to one epoch");
        }
    }
}
Proposal Verify::propose(const Problem& p, const Committee& c, const std::string& peer, const SigningKey& key, const Backend& backend) const {
    inputs(p,c); require(c.key(peer) && *c.key(peer) == key.public_key(),"net: proposal signer is not a committee member");
    require(backend.available(),"net: proposed execution backend unavailable");
    const auto s = p.system(); const auto executed = backend.solve(s); const auto reference = CpuBackend{}.solve(s);
    numerics(p,executed);
    require(executed.size() == reference.size(),"net: invalid execution result size");
    for (std::size_t i = 0; i < reference.size(); ++i) require(std::fabs(executed[i]-reference[i]) <= p.solver.solve_tolerance,"net: execution differs from the declared reference solve");
    // GPU intermediates are not hashed. All peers compute the bounded CPU
    // reference acceptance cells, avoiding a GPU rounding-boundary split.
    Proposal out; out.values = canonical_values(s,reference,p.solver.quantum); numerics(p,out.values);
    auto& cert = out.certificate;
    cert.epoch = p.epoch.id(); cert.inputs = p.epoch.inputs; cert.topology = p.epoch.context.topology; cert.constraints = p.epoch.context.constraints; cert.solver = p.epoch.context.solver;
    cert.result = hash(canonical_result(out.values,p.solver.quantum)); cert.residual = measure(s,out.values); cert.signer = peer;
    const auto checkpoint = checkpoint_ ? checkpoint_(p,out.values) : canonical_result(out.values,p.solver.quantum);
    cert.checkpoint = hash(checkpoint); cert.signature = key.sign(cert.statement()); return out;
}
std::optional<VerifiedResult> Verify::proposal(const Problem& p, const Committee& c, const Proposal& proposal, std::string* reason) const {
    try {
        inputs(p,c); const auto& cert = proposal.certificate; const auto* signer = c.key(cert.signer);
        require(signer && check_signature(*signer,cert.statement(),cert.signature),"net: forged result certificate");
        require(cert.epoch == p.epoch.id() && cert.inputs == p.epoch.inputs && cert.topology == p.epoch.context.topology && cert.constraints == p.epoch.context.constraints && cert.solver == p.epoch.context.solver,"net: proposal identifies a different problem");
        numerics(p,proposal.values);
        const auto s = p.system(); const auto expected = canonical_values(s,CpuBackend{}.solve(s),p.solver.quantum);
        require(proposal.values == expected,"net: proposal differs from the independently recomputed canonical result");
        require(cert.result == hash(canonical_result(proposal.values,p.solver.quantum)),"net: canonical result hash mismatch");
        const auto residual = measure(s,proposal.values);
        require(cert.residual.disagreement >= 0 && cert.residual.equation >= 0 && std::fabs(cert.residual.disagreement-residual.disagreement) <= p.solver.residual_tolerance && std::fabs(cert.residual.equation-residual.equation) <= p.solver.equation_tolerance,"net: certificate residual differs from independent measurement");
        const auto checkpoint = checkpoint_ ? checkpoint_(p,proposal.values) : canonical_result(proposal.values,p.solver.quantum);
        require(cert.checkpoint == hash(checkpoint),"net: finalized checkpoint differs from deterministic execution");
        VerifiedResult out; out.epoch_ = p.epoch; out.values_ = proposal.values; out.result_ = cert.result; out.checkpoint_ = checkpoint; out.residual_ = residual;
        out.decision_ = decision(p.epoch,cert.result,cert.checkpoint); return out;
    } catch (const std::exception& e) { if (reason) *reason = e.what(); return std::nullopt; }
}
}
