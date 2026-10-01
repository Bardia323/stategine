// Credentials are execution metadata; private seeds never enter a State.
#pragma once
#include "sg/net/Protocol.hpp"
#include <filesystem>

namespace sg::examples {
net::Committee read_committee(const std::filesystem::path& folder);
std::unique_ptr<net::SigningKey> read_signing_key(const std::filesystem::path& folder, const std::string& peer);
void make_credentials(const std::filesystem::path& folder, int count, int quorum, int faults, const std::string& region);
// Durable vote safety for examples. A used identity requires verified recovery
// or a new session; it must not silently start voting from tick zero again.
class Journal {
public:
    Journal(const std::filesystem::path& folder, const std::string& peer);
    ~Journal();
    void store(const net::Bytes& snapshot);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
