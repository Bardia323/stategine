#include "Signed.hpp"
#include <iostream>
int main(int argc, char** argv) {
    try {
        if (argc != 6) throw std::invalid_argument("usage: sg_net_keys folder count quorum faults region");
        sg::examples::make_credentials(argv[1],std::stoi(argv[2]),std::stoi(argv[3]),std::stoi(argv[4]),argv[5]);
        std::cout << "created regional Ed25519 identities; committee " << sg::net::hex(sg::examples::read_committee(argv[1]).id()) << '\n'; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
