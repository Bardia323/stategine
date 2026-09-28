// Stategine - algebra: where a batch is run.
//
// A backend takes a batch of jobs (Program.hpp) and gives back a verdict for
// each: the same verdicts, whatever runs them - this CPU, a GPU (sg/gpu). It
// knows nothing of states or laws; it runs `run_job` over every job.
//
// The CPU backend is the reference, and always there: every other backend is
// held to what it says (tests), and whatever cannot run - no device, no
// driver - hands its batch to it.
#pragma once

#include <algorithm>
#include <string>
#include <thread>
#include <vector>

#include "sg/algebra/Program.hpp"

namespace sg::algebra {

class Backend {
public:
    virtual ~Backend() = default;
    virtual std::string name() const = 0;
    // Whether it can run here: its device and driver found.
    virtual bool available() const = 0;
    // One verdict per job, in order.
    virtual std::vector<Verdict> run(const Batch& b) = 0;
};

// Every job on this machine's cores: a thread to each share of the batch,
// once it is big enough to be worth it.
class CpuBackend : public Backend {
public:
    explicit CpuBackend(unsigned threads = 0) : threads_(threads ? threads : std::max(1u, std::thread::hardware_concurrency())) {}
    std::string name() const override { return "cpu"; }
    bool available() const override { return true; }
    std::vector<Verdict> run(const Batch& b) override {
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

private:
    unsigned threads_;
};

}  // namespace sg::algebra
