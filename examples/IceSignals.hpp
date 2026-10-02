// The existing example signaling envelope, shared by native and browser shells.
#pragma once
#include "sg/net/IceSession.hpp"
#include "sg/net/Integrity.hpp"
#include <optional>
namespace sg::examples {
struct AuthenticatedSignal {std::string from;net::IceSignal signal;};
net::Bytes sign_signal(const net::Committee& committee,const std::string& from,const net::IceSignal& signal,const net::SigningKey& key);
std::optional<AuthenticatedSignal> verify_signal(const net::Committee& committee,const std::string& recipient,const net::Bytes& bytes);
}
