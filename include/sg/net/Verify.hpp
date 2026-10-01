// Independent numerical and discrete validation. No world-write capability.
#pragma once
#include "sg/net/Epoch.hpp"
#include "sg/net/Backend.hpp"
#include <functional>
#include <optional>

namespace sg::net {
struct ResultCertificate {
    Digest epoch{}, inputs{}, topology{}, constraints{}, solver{}, result{}, checkpoint{};
    Residual residual;
    std::string signer;
    Signature signature{};
    Bytes statement() const;
};
struct Proposal {
    ResultCertificate certificate;
    std::vector<double> values;
};
class VerifiedResult {
public:
    const Epoch& epoch() const;
    const std::vector<double>& values() const;
    const Bytes& checkpoint() const;
    Digest result() const;
    Digest decision() const;
    Residual residual() const;
private:
    Epoch epoch_;
    std::vector<double> values_;
    Bytes checkpoint_;
    Digest result_{}, decision_;
    Residual residual_;
    friend class Verify;
};
class Verify {
public:
    using Rule = std::function<bool(const Observation&)>;
    using Checkpoint = std::function<Bytes(const Problem&, const std::vector<double>&)>;
    explicit Verify(Rule discrete_rule = {}, Checkpoint checkpoint = {});
    Proposal propose(const Problem& problem, const Committee& committee,
                     const std::string& peer, const SigningKey& key, const Backend& backend) const;
    std::optional<VerifiedResult> proposal(const Problem& problem, const Committee& committee,
                                         const Proposal& proposal, std::string* reason = nullptr) const;
private:
    Rule rule_;
    Checkpoint checkpoint_;
    void inputs(const Problem& problem, const Committee& committee) const;
};
}
