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
    Digest checkpoint_hash() const;
    Digest result() const;
    Digest decision() const;
    Residual residual() const;
private:
    Epoch epoch_;
    std::vector<double> values_;
    Bytes checkpoint_;
    Digest result_{}, decision_, checkpoint_hash_;
    Residual residual_;
    friend class Verify;
};
struct VerifyStats { std::uint64_t reference_solves = 0, authenticated_input_sets = 0, cache_hits = 0; };
class Verify {
public:
    using Rule = std::function<bool(const Observation&)>;
    using Checkpoint = std::function<Bytes(const Problem&, const std::vector<double>&)>;
    explicit Verify(Rule discrete_rule = {}, Checkpoint checkpoint = {});
    ~Verify();
    Verify(const Verify& other);
    Verify& operator=(const Verify& other);
    Verify(Verify&& other) noexcept;
    Verify& operator=(Verify&& other) noexcept;
    Proposal propose(const Problem& problem, const Committee& committee,
                     const std::string& peer, const SigningKey& key, const Backend& backend) const;
    std::optional<VerifiedResult> proposal(const Problem& problem, const Committee& committee,
                                         const Proposal& proposal, std::string* reason = nullptr) const;
    std::optional<VerifiedResult> certificate(const Problem& problem, const Committee& committee,
                                             const ResultCertificate& certificate, std::string* reason = nullptr) const;
    VerifyStats diagnostics() const;
private:
    Rule rule_;
    Checkpoint checkpoint_;
    void inputs(const Problem& problem, const Committee& committee) const;
    struct Reference;
    struct Cache;
    std::unique_ptr<Cache> cache_;
    std::shared_ptr<const Reference> reference(const Problem& problem, const Committee& committee) const;
    bool matches(const Problem& problem, const Committee& committee, const ResultCertificate& certificate,
                 const std::vector<double>* values, const VerifiedResult& local, std::string* reason = nullptr,
                 bool authenticated = false) const;
    friend class Protocol;
};
}
