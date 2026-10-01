#include "sg/net/Agreement.hpp"
#include "Canonical.hpp"
#include <set>
#include <stdexcept>

namespace sg::net {
Bytes Attestation::statement() const { detail::Writer w("sg.net.attestation.v1"); w.digest(epoch); w.digest(decision); w.text(signer); return w.data(); }
Digest Finalization::id() const {
    // The receipt is independent of proposer, signature bytes and which
    // sufficient subset arrived first. All verified quorums name this decision.
    detail::Writer w("sg.net.finalized.v1"); w.digest(proposal.certificate.epoch); w.digest(proposal.certificate.result); w.digest(proposal.certificate.checkpoint); return hash(w.data());
}
Agreement::Agreement(Committee c, Digest previous, std::uint64_t next_tick, Persist persist, std::string world) : committee_(std::move(c)),previous_(previous),next_tick_(next_tick),world_(std::move(world)),persist_(std::move(persist)) {
    committee_id_ = committee_.id();
    if ((next_tick == 0) != (previous == Digest{}) || (next_tick > 0 && world_.empty())) throw std::invalid_argument("net: regional agreement needs its verified finalized predecessor and world identity");
}
bool Agreement::current(const VerifiedResult& r) const {
    const auto& c = r.epoch().context;
    return handoff_ == Digest{} && (world_.empty() || world_ == c.world) && c.partition == committee_.region && c.generation == committee_.generation && c.committee == committee_id_ && c.tick == next_tick_ && c.previous == previous_;
}
Attestation Agreement::attest(const VerifiedResult& r, const std::string& peer, const SigningKey& key) {
    if (!current(r) || !committee_.key(peer) || *committee_.key(peer) != key.public_key()) throw std::invalid_argument("net: attestation is not for this member's current regional step");
    const auto old = locks_.find(peer);
    if (old != locks_.end()) {
        if (old->second.epoch != r.epoch().id() || old->second.decision != r.decision()) throw std::invalid_argument("net: member already attested to a different problem or result at this world step");
        persist(); return old->second;
    }
    world_ = r.epoch().context.world;
    Attestation out{r.epoch().id(),r.decision(),peer,{}}; out.signature = key.sign(out.statement()); locks_[peer] = out; votes_[peer] = out; persist(); return out;
}
bool Agreement::receive(const VerifiedResult& r, const Attestation& a) {
    const auto* key = committee_.key(a.signer);
    if (!current(r) || !key || !check_signature(*key,a.statement(),a.signature) || a.epoch != r.epoch().id() || a.decision != r.decision()) return false;
    const auto old = votes_.find(a.signer);
    if (old != votes_.end()) {
        if (old->second.epoch == a.epoch && old->second.decision == a.decision) return true;
        evidence_.push_back({old->second,a}); return false;
    }
    world_ = r.epoch().context.world; votes_[a.signer] = a; return true;
}
std::optional<Finalization> Agreement::finalize(const VerifiedResult& r, const Proposal& p) {
    if (!current(r) || p.certificate.epoch != r.epoch().id() || p.certificate.result != r.result() || p.certificate.checkpoint != r.checkpoint_hash()) return std::nullopt;
    Finalization out{p,{}};
    for (const auto& [peer,vote] : votes_) { (void)peer; if (vote.epoch == r.epoch().id() && vote.decision == r.decision()) out.attestations.push_back(vote); }
    if (out.attestations.size() < committee_.quorum) return std::nullopt;
    return out;
}
std::optional<VerifiedResult> Agreement::accept(const Problem& p, const Verify& verify, const Finalization& f) {
    const auto result = f.proposal.values.empty() && !p.observations.empty() ? verify.certificate(p,committee_,f.proposal.certificate) : verify.proposal(p,committee_,f.proposal);
    if (!result) return std::nullopt;
    return accept_verified(*result,f);
}
std::optional<VerifiedResult> Agreement::accept_verified(const VerifiedResult& result,const Finalization& f) {
    if (!current(result) || f.proposal.certificate.epoch != result.epoch().id() || f.proposal.certificate.result != result.result() || f.proposal.certificate.checkpoint != result.checkpoint_hash() || f.attestations.size() > committee_.members.size()) return std::nullopt;
    std::set<std::string> signers;
    for (const auto& a : f.attestations) {
        const auto* key = committee_.key(a.signer);
        if (!key || a.epoch != result.epoch().id() || a.decision != result.decision() || !signers.insert(a.signer).second || !check_signature(*key,a.statement(),a.signature)) return std::nullopt;
    }
    if (signers.size() < committee_.quorum || next_tick_ == UINT64_MAX) return std::nullopt;
    // A failed flush must leave the live predecessor intact so the same
    // verified receipt can be retried, rather than skipping a world step.
    auto next = *this;
    next.world_ = result.epoch().context.world; next.previous_ = f.id(); ++next.next_tick_; next.votes_.clear(); next.locks_.clear(); next.persist();
    world_ = std::move(next.world_); previous_ = next.previous_; next_tick_ = next.next_tick_; votes_.clear(); locks_.clear(); return result;
}
Digest Agreement::previous() const { return previous_; }
std::uint64_t Agreement::next_tick() const { return next_tick_; }
const std::vector<VoteConflict>& Agreement::evidence() const { return evidence_; }
void Agreement::persist() const { if (persist_) persist_(snapshot()); }
Bytes Agreement::snapshot() const {
    detail::Writer w("sg.net.agreement-journal.v1"); w.digest(committee_id_); w.digest(previous_); w.integer(next_tick_); w.text(world_); w.digest(handoff_); w.integer(locks_.size());
    for (const auto& [peer,a] : locks_) { (void)peer; w.digest(a.epoch); w.digest(a.decision); w.text(a.signer); w.signature(a.signature); }
    return w.data();
}
void Agreement::restore(const Bytes& bytes) {
    if (!locks_.empty() || !votes_.empty() || handoff_ != Digest{}) throw std::logic_error("net: restore needs an unused agreement instance");
    detail::Reader r(bytes,"sg.net.agreement-journal.v1");
    if (r.digest() != committee_id_ || r.digest() != previous_ || r.integer() != next_tick_) throw std::invalid_argument("net: journal must match an independently verified predecessor");
    const auto world = r.text(); const auto handoff = r.digest(); const auto n = r.integer();
    if (!world_.empty() && world != world_) throw std::invalid_argument("net: journal belongs to another world");
    if (n > committee_.members.size()) throw std::invalid_argument("net: excessive journal locks");
    std::map<std::string,Attestation> locks;
    for (std::uint64_t i = 0; i < n; ++i) {
        Attestation a; a.epoch = r.digest(); a.decision = r.digest(); a.signer = r.text(); a.signature = r.signature(); const auto* key = committee_.key(a.signer);
        if (!key || !check_signature(*key,a.statement(),a.signature) || !locks.emplace(a.signer,a).second) throw std::invalid_argument("net: invalid journal signature");
    }
    r.end(); world_ = world; handoff_ = handoff; locks_ = std::move(locks); votes_ = locks_;
}
Digest Handoff::decision() const { detail::Writer w("sg.net.handoff.v1"); w.digest(finalized.id()); w.digest(hash(checkpoint)); w.digest(next.id()); return hash(w.data()); }
Attestation Agreement::endorse(const Problem& p, const Verify& verify, const Handoff& h, const std::string& peer, const SigningKey& key) {
    h.next.validate(); const auto* public_key = committee_.key(peer);
    Agreement independent(committee_,p.epoch.context.previous,p.epoch.context.tick,{},p.epoch.context.world);
    const auto result = independent.accept(p,verify,h.finalized);
    if (!result || result->checkpoint() != h.checkpoint || previous_ != h.finalized.id() || next_tick_ != p.epoch.context.tick+1 || world_ != p.epoch.context.world || !public_key || *public_key != key.public_key() || h.next.region != committee_.region || committee_.generation == UINT64_MAX || h.next.generation != committee_.generation+1) throw std::invalid_argument("net: handoff does not extend this finalized regional checkpoint");
    const auto decision = h.decision();
    if (handoff_ != Digest{} && handoff_ != decision) throw std::invalid_argument("net: member already endorsed another handoff");
    // Seal the old generation before exposing an endorsement. It cannot
    // continue voting while a replacement generation begins from this receipt.
    handoff_ = decision; persist();
    Attestation out{p.epoch.id(),decision,peer,{}}; out.signature = key.sign(out.statement()); return out;
}
bool verify_handoff(const Problem& p, const Committee& old, const Verify& verify, const Handoff& h) {
    try {
        h.next.validate();
        if (h.next.region != old.region || h.next.generation != old.generation+1 || old.generation == UINT64_MAX) return false;
        Agreement previous(old,p.epoch.context.previous,p.epoch.context.tick,{},p.epoch.context.world);
        const auto r = previous.accept(p,verify,h.finalized);
        if (!r || r->checkpoint() != h.checkpoint || h.finalized.proposal.certificate.checkpoint != hash(h.checkpoint)) return false;
        std::set<std::string> signers;
        if (h.endorsements.size() > old.members.size()) return false;
        for (const auto& a : h.endorsements) {
            const auto* key = old.key(a.signer);
            if (!key || a.epoch != p.epoch.id() || a.decision != h.decision() || !signers.insert(a.signer).second || !check_signature(*key,a.statement(),a.signature)) return false;
        }
        return signers.size() >= old.quorum;
    } catch (const std::exception&) { return false; }
}
Agreement Agreement::resume(const Problem& p, const Committee& old, const Verify& verify, const Handoff& h, Persist persist) {
    if (!verify_handoff(p,old,verify,h)) throw std::invalid_argument("net: cannot resume an unverified committee handoff");
    return Agreement(h.next,h.finalized.id(),p.epoch.context.tick+1,std::move(persist),p.epoch.context.world);
}
}
