#include "sg/algebra/Backend.hpp"

namespace sg::algebra {

std::vector<Verdict> CpuBackend::run(const Batch& b) {
    std::vector<Verdict> out(b.size());
    const auto share = [&](std::size_t from, std::size_t to) {
        std::vector<double> l, r;
        for (std::size_t j = from; j < to; ++j) {
            const uint32_t n = b.jobs[j].slots;
            if (l.size() < n) l.resize(n), r.resize(n);
            out[j] = run_job(b, j, l.data(), r.data());
        }
    };
    const std::size_t n = b.size();
    const unsigned t = n < 2048 ? 1u : std::min<unsigned>(threads_, static_cast<unsigned>(n / 1024));
    if (t <= 1) {
        share(0, n);
        return out;
    }
    std::vector<std::thread> pool;
    for (unsigned i = 0; i < t; ++i) pool.emplace_back(share, n * i / t, n * (i + 1) / t);
    for (std::thread& th : pool) th.join();
    return out;
}

}  // namespace sg::algebra
