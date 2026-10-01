// Signed regional messages and redundant execution outside Stategine.
#pragma once
#include "sg/net/Agreement.hpp"
#include <set>

namespace sg::net {
enum class MessageKind : std::uint8_t { Inputs, Proposal, Attestation, Finalization };
struct Message {
    MessageKind kind = MessageKind::Inputs;
    std::string from, to;
    std::vector<Observation> inputs;
    Proposal proposal;
    Attestation attestation;
    Finalization finalization;
};
Bytes encode(const Message& message);
Message decode_message(const Bytes& bytes);
struct InputSlot {
    std::string peer, object, parameter;
    std::uint64_t sequence;
    bool discrete;
};
class Protocol {
public:
    using Builder = std::function<Problem(const std::vector<Observation>&)>;
    Protocol(Committee committee, std::string peer, const SigningKey& key,
             const Backend& backend, EpochContext context,
             std::vector<InputSlot> slots, Builder builder, Verify verify,
             Agreement& agreement, Integrity& integrity);
    void submit(std::vector<Observation> local_inputs);
    void receive(const Message& message);
    std::vector<Message> outgoing();
    const Finalization* finalized() const;
    const VerifiedResult* accepted() const;
    const Problem* problem() const;
    const std::vector<Observation>& observations() const;
    std::size_t rejected() const;
private:
    Committee committee_;
    std::string peer_;
    const SigningKey& key_;
    const Backend& backend_;
    EpochContext context_;
    std::vector<InputSlot> slots_;
    Builder builder_;
    Verify verify_;
    Agreement& agreement_;
    Integrity& integrity_;
    std::vector<Observation> inputs_;
    std::optional<Problem> problem_;
    std::optional<Proposal> proposal_;
    std::optional<VerifiedResult> verified_, accepted_;
    std::optional<Finalization> finalized_;
    std::vector<Message> outgoing_;
    std::set<Digest> received_;
    std::size_t rejected_ = 0;
    bool add(const Observation& observation);
    void solve();
    void finish();
    void broadcast(Message message);
};
}
