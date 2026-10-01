// Regional acceptance records. Machines attest to independently verified work.
#pragma once
#include "sg/net/Verify.hpp"

namespace sg::net {
struct Attestation {
    Digest epoch{}, decision{};
    std::string signer;
    Signature signature{};
    Bytes statement() const;
};
struct Finalization {
    Proposal proposal;
    std::vector<Attestation> attestations;
    Digest id() const;
};
struct VoteConflict { Attestation first, second; };
struct Handoff;
class Agreement {
public:
    using Persist = std::function<void(const Bytes&)>;
    explicit Agreement(Committee committee, Digest previous = {}, std::uint64_t next_tick = 0,
                       Persist persist = {}, std::string world = {});
    static Agreement resume(const Problem& last_problem, const Committee& old_committee,
                            const Verify& verify, const Handoff& handoff, Persist persist = {});
    Attestation attest(const VerifiedResult& result, const std::string& peer, const SigningKey& key);
    bool receive(const VerifiedResult& result, const Attestation& attestation);
    std::optional<Finalization> finalize(const VerifiedResult& result, const Proposal& proposal);
    std::optional<VerifiedResult> accept(const Problem& problem, const Verify& verify, const Finalization& finalization);
    Digest previous() const;
    std::uint64_t next_tick() const;
    const std::vector<VoteConflict>& evidence() const;
    Bytes snapshot() const;
    void restore(const Bytes& snapshot);
    Attestation endorse(const Problem& last_problem, const Verify& verify, const Handoff& handoff,
                        const std::string& peer, const SigningKey& key);
private:
    Committee committee_;
    Digest previous_{};
    std::uint64_t next_tick_ = 0;
    std::string world_;
    std::map<std::string,Attestation> locks_, votes_;
    std::vector<VoteConflict> evidence_;
    Persist persist_;
    Digest handoff_{};
    Digest committee_id_{};
    bool current(const VerifiedResult& result) const;
    void persist() const;
    std::optional<VerifiedResult> accept_verified(const VerifiedResult& result, const Finalization& finalization);
    friend class Protocol;
};
struct Handoff {
    Finalization finalized;
    Bytes checkpoint;
    Committee next;
    std::vector<Attestation> endorsements;
    Digest decision() const;
};
bool verify_handoff(const Problem& last_problem, const Committee& old_committee, const Verify& verify, const Handoff& handoff);
}
