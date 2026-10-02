#include "IceSignals.hpp"
#include "../src/net/Canonical.hpp"
namespace sg::examples {
net::Bytes sign_signal(const net::Committee &committee, const std::string &from, const net::IceSignal &s,
                       const net::SigningKey &key) {
    net::detail::Writer w("example.ice.signal.v1");
    w.digest(committee.id());
    w.text(from);
    w.text(s.peer);
    w.integer(s.lane == net::DeliveryClass::Reliable ? 0 : 1);
    w.integer(s.session);
    w.integer(static_cast<unsigned>(s.kind));
    w.text(s.value);
    w.text(s.type);
    net::detail::Writer envelope("example.ice.envelope.v1");
    envelope.bytes(w.data());
    envelope.signature(key.sign(w.data()));
    return envelope.data();
}
std::optional<AuthenticatedSignal> verify_signal(const net::Committee &committee, const std::string &recipient,
                                                 const net::Bytes &bytes) {
    net::detail::Reader envelope(bytes, "example.ice.envelope.v1");
    const auto body = envelope.bytes(70000);
    const auto signature = envelope.signature();
    envelope.end();
    net::detail::Reader r(body, "example.ice.signal.v1");
    if (r.digest() != committee.id())
        return {};
    const auto from = r.text(), to = r.text();
    const auto lane = r.integer(), session = r.integer(), kind = r.integer();
    net::IceSignal s{to,       lane == 0 ? net::DeliveryClass::Reliable : net::DeliveryClass::Latest,
                     session,  static_cast<net::IceSignalKind>(kind),
                     r.text(), r.text()};
    r.end();
    const auto *identity = committee.key(from);
    if (!identity || to != recipient || from == recipient || lane > 1 || kind > 2 ||
        !net::check_signature(*identity, body, signature))
        return {};
    return AuthenticatedSignal{from, std::move(s)};
}
} // namespace sg::examples
