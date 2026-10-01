#include "sg/net/Verify.hpp"
#include "Canonical.hpp"
#include <cmath>
#include <set>
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace sg::net {
namespace {
void require(bool condition, const char* reason) { if (!condition) throw std::invalid_argument(reason); }
void numerics(const Problem& p,const std::vector<double>& x,const Residual& r) {
    require(x.size() == p.observations.size(),"net: proposal coordinate count differs");
    for (std::size_t i = 0; i < x.size(); ++i) require(std::isfinite(x[i]) && (!p.fixed[i] || x[i] == p.pins[i]),"net: proposal breaks a hard pin or contains a nonfinite number");
    require(std::isfinite(r.disagreement) && r.disagreement <= p.solver.residual_tolerance,"net: proposal disagreement exceeds the declared tolerance");
    require(std::isfinite(r.equation) && r.equation <= p.solver.equation_tolerance,"net: proposal equation residual exceeds the declared tolerance");
}
bool same_committee(const Committee& a,const Committee& b) {
    if (a.region != b.region || a.generation != b.generation || a.quorum != b.quorum || a.faults != b.faults || a.members.size() != b.members.size()) return false;
    for (std::size_t i = 0; i < a.members.size(); ++i) if (a.members[i].peer != b.members[i].peer || a.members[i].key != b.members[i].key) return false;
    return true;
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
Digest VerifiedResult::checkpoint_hash() const { return checkpoint_hash_; }
Digest VerifiedResult::result() const { return result_; }
Digest VerifiedResult::decision() const { return decision_; }
Residual VerifiedResult::residual() const { return residual_; }
struct Verify::Reference {
    Digest epoch{}, committee_id{}, result{}, checkpoint_hash{};
    Committee committee;
    std::vector<Signature> signatures;
    std::vector<double> executed, values;
    Residual residual, executed_residual;
    Bytes checkpoint;
};
struct Verify::Cache {
    std::mutex mutex;
    std::vector<std::shared_ptr<const Reference>> entries;
    VerifyStats stats;
};
Verify::Verify(Rule rule,Checkpoint checkpoint) : rule_(std::move(rule)),checkpoint_(std::move(checkpoint)),cache_(std::make_unique<Cache>()) {}
Verify::~Verify() = default;
// A copy is another executor: it must independently compute its reference.
Verify::Verify(const Verify& other) : Verify(other.rule_,other.checkpoint_) {}
Verify& Verify::operator=(const Verify& other) { if (this != &other) { rule_ = other.rule_; checkpoint_ = other.checkpoint_; cache_ = std::make_unique<Cache>(); } return *this; }
Verify::Verify(Verify&&) noexcept = default;
Verify& Verify::operator=(Verify&&) noexcept = default;
void Verify::inputs(const Problem& p,const Committee& c) const {
    require(p.epoch.context.partition == c.region && p.epoch.context.generation == c.generation,"net: proposal committee differs from the declared region");
    std::set<std::string> conflicts;
    for (const auto& o : p.epoch.ordered_inputs) {
        require(o.context == p.epoch.context.id() && o.tick == p.epoch.context.tick && o.authentic(c),"net: unsigned, forged or wrong-epoch input");
        if (o.discrete) { require(rule_ && rule_(o),"net: discrete event fails its declared deterministic rule"); require(conflicts.insert(o.conflict).second,"net: conflicting discrete events cannot belong to one epoch"); }
    }
}
std::shared_ptr<const Verify::Reference> Verify::reference(const Problem& p,const Committee& c) const {
    // A claimed digest is never enough to hit this cache. Mutable public
    // Problems are validated before consulting the bound epoch identity.
    p.validate(); const auto epoch = p.epoch.id();
    std::lock_guard<std::mutex> lock(cache_->mutex);
    auto found = std::find_if(cache_->entries.begin(),cache_->entries.end(),[&](const auto& r){return r->epoch == epoch && same_committee(r->committee,c);});
    Digest committee_id;
    if (found != cache_->entries.end()) committee_id = (*found)->committee_id;
    else committee_id = c.id(); // validates unrecognized committee data
    require(p.epoch.context.committee == committee_id,"net: proposal committee differs from the declared region");
    if (found == cache_->entries.end()) found = std::find_if(cache_->entries.begin(),cache_->entries.end(),[&](const auto& r){return r->epoch == epoch && r->committee_id == committee_id;});
    if (found != cache_->entries.end()) {
        const auto& signatures = (*found)->signatures; bool authenticated = signatures.size() == p.epoch.ordered_inputs.size();
        for (std::size_t i = 0; authenticated && i < signatures.size(); ++i) authenticated &= signatures[i] == p.epoch.ordered_inputs[i].signature;
        if (!authenticated) { inputs(p,c); ++cache_->stats.authenticated_input_sets; }
        ++cache_->stats.cache_hits; return *found;
    }
    inputs(p,c); ++cache_->stats.authenticated_input_sets;
    auto out = std::make_shared<Reference>(); out->epoch = epoch; out->committee = c; out->committee_id = committee_id;
    for (const auto& o : p.epoch.ordered_inputs) out->signatures.push_back(o.signature);
    const auto s = p.system(); const PreparedSystem prepared(s,step_size(s)); SolveWorkspace work;
    CpuBackend{}.solve_prepared(prepared,work,out->executed); ++cache_->stats.reference_solves;
    out->executed_residual = measure(prepared,out->executed,work);
    out->values = canonical_values(s,out->executed,p.solver.quantum);
    out->residual = measure(prepared,out->values,work); numerics(p,out->values,out->residual);
    const auto canonical = canonical_result(out->values,p.solver.quantum); out->result = hash(canonical);
    // Checkpoints are declared deterministic functions of this immutable
    // Problem and canonical values, just like the discrete validation rule.
    out->checkpoint = checkpoint_ ? checkpoint_(p,out->values) : canonical;
    out->checkpoint_hash = hash(out->checkpoint);
    if (cache_->entries.size() == 2) cache_->entries.erase(cache_->entries.begin());
    cache_->entries.push_back(out); return out;
}
Proposal Verify::propose(const Problem& p,const Committee& c,const std::string& peer,const SigningKey& key,const Backend& backend) const {
    const auto ref = reference(p,c);
    require(c.key(peer) && *c.key(peer) == key.public_key(),"net: proposal signer is not a committee member"); require(backend.available(),"net: proposed execution backend unavailable");
    if (dynamic_cast<const CpuBackend*>(&backend)) numerics(p,ref->executed,ref->executed_residual);
    else {
        const auto s = p.system(); const PreparedSystem prepared(s,step_size(s)); SolveWorkspace work; std::vector<double> executed;
        backend.solve_prepared(prepared,work,executed); numerics(p,executed,measure(prepared,executed,work));
        require(executed.size() == ref->executed.size(),"net: invalid execution result size");
        for (std::size_t i = 0; i < executed.size(); ++i) require(std::fabs(executed[i]-ref->executed[i]) <= p.solver.solve_tolerance,"net: execution differs from the declared reference solve");
    }
    Proposal out; out.values = ref->values; auto& cert = out.certificate;
    cert.epoch = ref->epoch; cert.inputs = p.epoch.inputs; cert.topology = p.epoch.context.topology; cert.constraints = p.epoch.context.constraints; cert.solver = p.epoch.context.solver;
    cert.result = ref->result; cert.residual = ref->residual; cert.signer = peer; cert.checkpoint = ref->checkpoint_hash; cert.signature = key.sign(cert.statement()); return out;
}
bool Verify::matches(const Problem& p,const Committee& c,const ResultCertificate& cert,const std::vector<double>* values,const VerifiedResult& local,std::string* reason,bool authenticated) const {
    try {
        if (!authenticated) {
            const auto* signer = c.key(cert.signer);
            require(signer && check_signature(*signer,cert.statement(),cert.signature),"net: forged result certificate");
        }
        require(cert.epoch == local.epoch().id() && cert.inputs == local.epoch().inputs && cert.topology == local.epoch().context.topology && cert.constraints == local.epoch().context.constraints && cert.solver == local.epoch().context.solver,"net: proposal identifies a different problem");
        require(!values || *values == local.values(),"net: proposal differs from the independently recomputed canonical result");
        require(cert.result == local.result(),"net: canonical result hash mismatch"); const auto residual = local.residual();
        require(std::isfinite(cert.residual.disagreement) && std::isfinite(cert.residual.equation) && cert.residual.disagreement >= 0 && cert.residual.equation >= 0 && std::fabs(cert.residual.disagreement-residual.disagreement) <= p.solver.residual_tolerance && std::fabs(cert.residual.equation-residual.equation) <= p.solver.equation_tolerance,"net: certificate residual differs from independent measurement");
        require(cert.checkpoint == local.checkpoint_hash(),"net: finalized checkpoint differs from deterministic execution"); return true;
    } catch (const std::exception& e) { if (reason) *reason = e.what(); return false; }
}
std::optional<VerifiedResult> Verify::certificate(const Problem& p,const Committee& c,const ResultCertificate& cert,std::string* reason) const {
    try {
        const auto* signer = c.key(cert.signer);
        require(signer && check_signature(*signer,cert.statement(),cert.signature),"net: forged result certificate");
        const auto ref = reference(p,c);
        VerifiedResult out; out.epoch_ = p.epoch; out.values_ = ref->values; out.result_ = ref->result; out.checkpoint_ = ref->checkpoint; out.checkpoint_hash_ = ref->checkpoint_hash; out.residual_ = ref->residual; out.decision_ = decision(p.epoch,ref->result,ref->checkpoint_hash);
        if (!matches(p,c,cert,nullptr,out,reason,true)) return std::nullopt;
        return out;
    } catch (const std::exception& e) { if (reason) *reason = e.what(); return std::nullopt; }
}
std::optional<VerifiedResult> Verify::proposal(const Problem& p,const Committee& c,const Proposal& proposal,std::string* reason) const {
    auto result = certificate(p,c,proposal.certificate,reason);
    if (result && proposal.values != result->values()) { if (reason) *reason = "net: proposal differs from the independently recomputed canonical result"; return std::nullopt; }
    return result;
}
VerifyStats Verify::diagnostics() const { std::lock_guard<std::mutex> lock(cache_->mutex); return cache_->stats; }
}
