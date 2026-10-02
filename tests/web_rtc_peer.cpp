// Real native browser interoperability, using the existing signed example IO.
#include "../examples/NetIo.hpp"
#include "../examples/Signed.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("credentials signaling-url");
        const auto committee = sg::examples::read_committee(argv[1]);
        const auto key = sg::examples::read_signing_key(argv[1], "p0");
        sg::examples::NetIo io({0, 49390, {{"p0", "127.0.0.1"}, {"p1", "127.0.0.1"}}, argv[2], {}, {}, false},
                               committee, *key);
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        std::cout << "READY\n" << std::flush;
        while (std::chrono::steady_clock::now() < until) {
            io.poll();
            sg::net::Inbound in;
            while (io.try_receive(in)) {
                if (in.channel == "stop")
                    return 0;
                if (in.channel == "signed")
                    continue;
                const auto delivery =
                    in.channel == "probe.latest" ? sg::net::DeliveryClass::Latest : sg::net::DeliveryClass::Reliable;
                if (io.send({in.peer, in.channel, delivery, "echo", std::move(in.bytes)}) !=
                    sg::net::SendResult::Accepted)
                    throw std::runtime_error("echo backpressure");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("interop timed out");
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
