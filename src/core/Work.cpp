#include "sg/core/Work.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace sg::work {

namespace {

thread_local bool g_in_hand = false;

// Work given out at once: n pieces, taken one by one by whoever is free.
struct Job {
    const std::function<void(std::size_t)>* fn = nullptr;
    std::size_t n = 0;
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::mutex m;
    std::condition_variable all_done;
    std::exception_ptr failed;

    // Takes and does pieces until none is left to take.
    void help() {
        for (std::size_t i; (i = next.fetch_add(1)) < n;) {
            try {
                (*fn)(i);
            } catch (...) {
                std::lock_guard<std::mutex> g(m);
                if (!failed) failed = std::current_exception();
            }
            if (done.fetch_add(1) + 1 == n) {
                std::lock_guard<std::mutex> g(m);
                all_done.notify_all();
            }
        }
    }
    void wait() {
        // A piece is short as often as not: a moment's spin before sleeping.
        for (int k = 0; k < 256 && done.load() < n; ++k) std::this_thread::yield();
        std::unique_lock<std::mutex> g(m);
        all_done.wait(g, [this] { return done.load() >= n; });
    }
};

// The hands: started when first asked for, then waiting for work. Never
// stopped - a thread joined as the program ends can hang it; the end takes
// them.
class Hands {
public:
    unsigned count() {
        std::lock_guard<std::mutex> g(m_);
        return wanted_;
    }
    void want(unsigned n) {
        if (n == 0) n = std::max(1u, std::thread::hardware_concurrency());
        std::lock_guard<std::mutex> g(m_);
        wanted_ = n;
        // The caller is a hand of its own: n - 1 more.
        while (started_ + 1 < wanted_) {
            std::thread([this] { work(); }).detach();
            ++started_;
        }
    }
    void give(const std::shared_ptr<Job>& j) {
        {
            std::lock_guard<std::mutex> g(m_);
            jobs_.push_back(j);
        }
        bell_.notify_all();
    }
    void done_with(const std::shared_ptr<Job>& j) {
        std::lock_guard<std::mutex> g(m_);
        jobs_.erase(std::remove(jobs_.begin(), jobs_.end(), j), jobs_.end());
    }

private:
    void work() {
        g_in_hand = true;
        for (;;) {
            std::shared_ptr<Job> j;
            {
                std::unique_lock<std::mutex> g(m_);
                bell_.wait(g, [this] { return !jobs_.empty(); });
                // The oldest job with a piece left; one taken out is done with.
                while (!jobs_.empty() && jobs_.front()->next.load() >= jobs_.front()->n) jobs_.pop_front();
                if (jobs_.empty()) continue;
                j = jobs_.front();
            }
            j->help();
        }
    }

    std::mutex m_;
    std::condition_variable bell_;
    std::deque<std::shared_ptr<Job>> jobs_;
    unsigned wanted_ = 0, started_ = 0;
};

Hands& the_hands() {
    static Hands* h = [] {
        auto* made = new Hands;
        made->want(0);
        return made;
    }();
    return *h;
}

void run(const std::function<void()>* mine, std::size_t n, const std::function<void(std::size_t)>& fn) {
    Hands& h = the_hands();
    if (n == 0 || h.count() <= 1 || (n == 1 && !mine)) {
        if (mine) (*mine)();
        for (std::size_t i = 0; i < n; ++i) fn(i);
        return;
    }
    auto j = std::make_shared<Job>();
    j->fn = &fn;
    j->n = n;
    h.give(j);
    std::exception_ptr own;
    if (mine) {
        try {
            (*mine)();
        } catch (...) {
            own = std::current_exception();
        }
    }
    j->help();
    j->wait();
    h.done_with(j);
    if (own) std::rethrow_exception(own);
    if (j->failed) std::rethrow_exception(j->failed);
}

}  // namespace

unsigned hands() { return the_hands().count(); }
void set_hands(unsigned n) { the_hands().want(n); }
bool in_hand() { return g_in_hand; }

void each(std::size_t n, const std::function<void(std::size_t)>& fn) { run(nullptr, n, fn); }

void beside(const std::function<void()>& mine, std::size_t n, const std::function<void(std::size_t)>& fn) {
    run(&mine, n, fn);
}

void bands(int n, const std::function<void(int, int)>& fn, int per_hand) {
    if (n <= 0) return;
    const int pieces = std::min(n, static_cast<int>(hands()) * std::max(1, per_hand));
    if (pieces <= 1) return fn(0, n);
    each(static_cast<std::size_t>(pieces), [&](std::size_t i) {
        const int a = static_cast<int>(static_cast<long long>(n) * static_cast<long long>(i) / pieces);
        const int b = static_cast<int>(static_cast<long long>(n) * static_cast<long long>(i + 1) / pieces);
        if (a < b) fn(a, b);
    });
}

}  // namespace sg::work
