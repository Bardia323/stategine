#include "sg/net/Protocol.hpp"
#include "Canonical.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace sg::net {
namespace {
void certificate(detail::Writer& w, const ResultCertificate& c) {
    w.digest(c.epoch); w.digest(c.inputs); w.digest(c.topology); w.digest(c.constraints); w.digest(c.solver); w.digest(c.result); w.digest(c.checkpoint);
    w.real(c.residual.disagreement); w.real(c.residual.equation); w.text(c.signer); w.signature(c.signature);
}
ResultCertificate certificate(detail::Reader& r) {
    ResultCertificate c; c.epoch = r.digest(); c.inputs = r.digest(); c.topology = r.digest(); c.constraints = r.digest(); c.solver = r.digest(); c.result = r.digest(); c.checkpoint = r.digest();
    c.residual = {r.real(),r.real()}; c.signer = r.text(); c.signature = r.signature(); return c;
}
void proposal(detail::Writer& w, const Proposal& p) { certificate(w,p.certificate); w.integer(p.values.size()); for (const auto x : p.values) w.real(x); }
Proposal proposal(detail::Reader& r) { Proposal p; p.certificate = certificate(r); const auto n = r.integer(); if (n > 1048576) throw std::length_error("net: excessive proposal coordinates"); p.values.reserve(n); for (std::uint64_t i = 0; i < n; ++i) p.values.push_back(r.real()); return p; }
void attestation(detail::Writer& w, const Attestation& a) { w.digest(a.epoch); w.digest(a.decision); w.text(a.signer); w.signature(a.signature); }
Attestation attestation(detail::Reader& r) { Attestation a; a.epoch = r.digest(); a.decision = r.digest(); a.signer = r.text(); a.signature = r.signature(); return a; }
void finalized(detail::Writer& w, const Finalization& f) { certificate(w,f.proposal.certificate); w.integer(f.attestations.size()); for (const auto& a : f.attestations) attestation(w,a); }
Finalization finalized(detail::Reader& r) { Finalization f; f.proposal.certificate = certificate(r); const auto n = r.integer(); if (n > 1024) throw std::length_error("net: excessive finalization signatures"); for (std::uint64_t i = 0; i < n; ++i) f.attestations.push_back(attestation(r)); return f; }
Bytes request_statement(const Attestation& a,const std::string& to) {
    detail::Writer w("sg.net.data-request.v1"); w.digest(a.epoch); w.digest(a.decision); w.text(a.signer); w.text(to); return w.data();
}
Digest decision(const ResultCertificate& c) { detail::Writer w("sg.net.decision.v1"); w.digest(c.epoch); w.digest(c.result); w.digest(c.checkpoint); return hash(w.data()); }
}
Bytes encode(const Message& m) {
    if (m.inputs.size() > 65536 || m.from.empty() || m.to.empty()) throw std::invalid_argument("net: invalid signed message routing");
    detail::Writer w("sg.net.protocol.v2"); w.integer(static_cast<unsigned>(m.kind)); w.text(m.from); w.text(m.to);
    const bool data = m.kind == MessageKind::Inputs || m.kind == MessageKind::Proposal; w.integer(data ? m.inputs.size() : 0);
    if (data) for (const auto& o : m.inputs) detail::observation(w,o);
    if (m.kind == MessageKind::Proposal) proposal(w,m.proposal);
    else if (m.kind == MessageKind::Attestation) { certificate(w,m.proposal.certificate); attestation(w,m.attestation); }
    else if (m.kind == MessageKind::Finalization) finalized(w,m.finalization);
    else if (m.kind == MessageKind::Request) attestation(w,m.attestation);
    else if (m.kind != MessageKind::Inputs) throw std::invalid_argument("net: unknown signed message kind");
    if (w.data().size() > 16777216) throw std::length_error("net: oversized signed message");
    return w.data();
}
Message decode_message(const Bytes& bytes) {
    detail::Reader r(bytes,"sg.net.protocol.v2"); Message m; const auto kind = r.integer(); if (kind > 4) throw std::invalid_argument("net: invalid signed message kind"); m.kind = static_cast<MessageKind>(kind);
    m.from = r.text(); m.to = r.text(); const auto n = r.integer(); if (n > 65536 || m.from.empty() || m.to.empty()) throw std::invalid_argument("net: invalid signed input count or routing");
    for (std::uint64_t i = 0; i < n; ++i) m.inputs.push_back(detail::observation(r));
    if (m.kind == MessageKind::Proposal) m.proposal = proposal(r);
    else if (m.kind == MessageKind::Attestation) { m.proposal.certificate = certificate(r); m.attestation = attestation(r); }
    else if (m.kind == MessageKind::Finalization) m.finalization = finalized(r);
    else if (m.kind == MessageKind::Request) m.attestation = attestation(r);
    if (m.kind != MessageKind::Inputs && m.kind != MessageKind::Proposal && !m.inputs.empty()) throw std::invalid_argument("net: control references cannot carry epoch input data");
    r.end(); return m;
}
Protocol::Protocol(Committee c, std::string peer, const SigningKey& key, const Backend& backend, EpochContext context,
                   std::vector<InputSlot> slots, Builder builder, Verify verify, Agreement& agreement, Integrity& integrity)
    : committee_(std::move(c)),peer_(std::move(peer)),key_(key),backend_(backend),context_(std::move(context)),slots_(std::move(slots)),builder_(std::move(builder)),verify_(std::move(verify)),agreement_(agreement),integrity_(integrity) {
    committee_id_ = committee_.id(); context_id_ = context_.id();
    if (!committee_.key(peer_) || *committee_.key(peer_) != key_.public_key() || context_.committee != committee_id_ || context_.partition != committee_.region || context_.generation != committee_.generation || agreement_.next_tick() != context_.tick || agreement_.previous() != context_.previous || !builder_ || slots_.empty()) throw std::invalid_argument("net: regional protocol configuration differs from its epoch");
    std::set<std::pair<std::string,std::uint64_t>> sequences;
    for (const auto& s : slots_) if (!committee_.key(s.peer) || s.object.empty() || s.parameter.empty() || !sequences.emplace(s.peer,s.sequence).second) throw std::invalid_argument("net: invalid declared input manifest");
}
void Protocol::broadcast(Message m) { m.from = peer_; for (const auto& member : committee_.members) if (member.peer != peer_) { m.to = member.peer; outgoing_.push_back(m); } }
bool Protocol::add(const Observation& o) {
    const auto sequence = std::find_if(slots_.begin(),slots_.end(),[&](const InputSlot& s){ return s.peer == o.peer && s.sequence == o.sequence; });
    if (sequence == slots_.end()) { ++rejected_; return false; }
    const auto seen = std::find_if(inputs_.begin(),inputs_.end(),[&](const Observation& x){ return x.peer == o.peer && x.sequence == o.sequence; });
    // Reuse only the exact signed statement already authenticated locally.
    // A different statement or signature still goes through Integrity below.
    if (seen != inputs_.end() && seen->signature == o.signature && seen->id() == o.id()) return true;
    // Inspect signed changes to object/parameter as equivocation too; matching
    // only the full manifest first would discard this conflicting evidence.
    const auto delivery = integrity_.receive(o,context_id_,context_.tick);
    if (delivery != Delivery::Accepted && delivery != Delivery::Duplicate) { ++rejected_; return false; }
    const auto slot = std::find_if(slots_.begin(),slots_.end(),[&](const InputSlot& s){ return s.peer == o.peer && s.object == o.object && s.parameter == o.parameter && s.sequence == o.sequence && s.discrete == o.discrete; });
    if (slot == slots_.end()) { ++rejected_; return false; }
    if (seen == inputs_.end()) inputs_.push_back(o);
    else if (seen->id() != o.id()) { ++rejected_; return false; }
    return true;
}
void Protocol::submit(std::vector<Observation> local) {
    if (accepted_) return;
    for (auto& o : local) {
        if (o.peer != peer_) throw std::invalid_argument("net: cannot sign another participant's observation");
        o.context = context_id_; o.tick = context_.tick; o.sign(key_); if (!add(o)) throw std::invalid_argument("net: local observation does not match the epoch manifest");
    }
    Message m; m.inputs = std::move(local); broadcast(std::move(m)); solve(); finish(); drain_pending();
}
void Protocol::solve() {
    if (problem_ || inputs_.size() != slots_.size()) return;
    try {
        auto problem = builder_(order_inputs(inputs_));
        if (problem.epoch.context.id() != context_id_) throw std::invalid_argument("net: builder changed the declared epoch");
        auto proposal = verify_.propose(problem,committee_,peer_,key_,backend_);
        auto verified = verify_.proposal(problem,committee_,proposal);
        if (!verified) throw std::invalid_argument("net: local result failed independent verification");
        const auto vote = agreement_.attest(*verified,peer_,key_);
        problem_ = std::move(problem); proposal_ = std::move(proposal); verified_ = std::move(verified);
        Message result; result.kind = MessageKind::Proposal; result.inputs = inputs_; result.proposal = *proposal_; broadcast(result);
        result.kind = MessageKind::Attestation; result.inputs.clear(); result.proposal.values.clear(); result.attestation = vote; broadcast(std::move(result));
    } catch (const std::invalid_argument&) { ++rejected_; }
}
void Protocol::finish() {
    if (accepted_ || !verified_ || !proposal_) return;
    auto finalization = agreement_.finalize(*verified_,*proposal_);
    if (!finalization) return;
    const auto accepted = agreement_.accept_verified(*verified_,*finalization);
    if (!accepted) { ++rejected_; return; }
    accepted_ = accepted; finalized_ = std::move(finalization); integrity_.retire_before(context_.tick+1);
    Message m; m.kind = MessageKind::Finalization; m.finalization = *finalized_; m.finalization.proposal.values.clear(); broadcast(std::move(m));
}
void Protocol::request_data(const Message& m) {
    const auto& cert = m.kind == MessageKind::Finalization ? m.finalization.proposal.certificate : m.proposal.certificate;
    const auto* signer = committee_.key(cert.signer);
    if (!signer || cert.topology != context_.topology || cert.constraints != context_.constraints || cert.solver != context_.solver || !check_signature(*signer,cert.statement(),cert.signature)) { ++rejected_; return; }
    const auto bytes = encode(m); const auto frame = hash(bytes);
    if (pending_frames_.count(frame)) return;
    if (pending_.size() >= 64 || bytes.size() > 4*1024*1024-pending_bytes_) { ++rejected_; return; }
    pending_frames_.insert(frame); pending_bytes_ += bytes.size(); pending_.push_back(decode_message(bytes));
    Attestation request{cert.epoch,decision(cert),peer_,{}};
    const auto identity = hash(request_statement(request,m.from));
    if (requests_.size() < 64 && requests_.insert(identity).second) {
        request.signature = key_.sign(request_statement(request,m.from));
        Message out; out.kind = MessageKind::Request; out.from = peer_; out.to = m.from; out.attestation = request; outgoing_.push_back(std::move(out));
    }
}
void Protocol::drain_pending() {
    if (!problem_ || pending_.empty()) return;
    std::vector<Message> pending; pending.swap(pending_); pending_frames_.clear(); pending_bytes_ = 0;
    for (const auto& m : pending) receive(m);
}
void Protocol::receive(const Message& m) {
    if (m.to != peer_ || !committee_.key(m.from)) return;
    const auto frame = hash(encode(m));
    if (received_.count(frame)) return;
    if (m.kind == MessageKind::Request) {
        const auto& request = m.attestation; const auto* key = committee_.key(request.signer);
        if (request.signer != m.from || !key || !check_signature(*key,request_statement(request,peer_),request.signature)) { ++rejected_; return; }
        if (!proposal_ || !verified_ || request.epoch != verified_->epoch().id() || request.decision != verified_->decision() || requests_.size() >= 64) return;
        if (requests_.insert(frame).second) { Message data; data.kind = MessageKind::Proposal; data.from = peer_; data.to = m.from; data.inputs = inputs_; data.proposal = *proposal_; outgoing_.push_back(std::move(data)); }
        if (received_.size() < 256) received_.insert(frame);
        return;
    }
    const bool data = m.kind == MessageKind::Inputs || m.kind == MessageKind::Proposal;
    if (data) for (const auto& o : m.inputs) if (o.tick < context_.tick || accepted_) integrity_.receive(o,context_id_,context_.tick);
    if (accepted_) return;
    if (data) for (const auto& o : m.inputs) if (!add(o)) return;
    solve();
    if (m.kind == MessageKind::Inputs && received_.size() < 256) received_.insert(frame);
    if (!problem_ || !verified_) { if (m.kind != MessageKind::Inputs) request_data(m); return; }
    if (m.kind == MessageKind::Proposal || m.kind == MessageKind::Attestation) {
        if (!verify_.matches(*problem_,committee_,m.proposal.certificate,m.kind == MessageKind::Proposal ? &m.proposal.values : nullptr,*verified_)) { ++rejected_; return; }
        if (m.kind == MessageKind::Attestation && !agreement_.receive(*verified_,m.attestation)) ++rejected_;
    } else if (m.kind == MessageKind::Finalization) {
        if (!verify_.matches(*problem_,committee_,m.finalization.proposal.certificate,nullptr,*verified_)) { ++rejected_; return; }
        auto accepted = agreement_.accept_verified(*verified_,m.finalization);
        if (!accepted) { ++rejected_; return; }
        accepted_ = std::move(accepted); finalized_ = m.finalization;
        finalized_->proposal.values = verified_->values(); // local reconstruction, never unverified wire data
        integrity_.retire_before(context_.tick+1); Message echo = m; echo.inputs.clear(); echo.finalization.proposal.values.clear(); broadcast(std::move(echo));
    }
    if (received_.size() < 256) received_.insert(frame);
    finish(); drain_pending();
}
std::vector<Message> Protocol::outgoing() { std::vector<Message> out; out.swap(outgoing_); return out; }
const Finalization* Protocol::finalized() const { return finalized_ ? &*finalized_ : nullptr; }
const VerifiedResult* Protocol::accepted() const { return accepted_ ? &*accepted_ : nullptr; }
const Problem* Protocol::problem() const { return problem_ ? &*problem_ : nullptr; }
const std::vector<Observation>& Protocol::observations() const { return inputs_; }
std::size_t Protocol::rejected() const { return rejected_; }
VerifyStats Protocol::verification_diagnostics() const { return verify_.diagnostics(); }
}
