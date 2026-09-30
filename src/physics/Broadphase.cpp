#include "sg/physics/Rigid.hpp"
namespace sg::rigid {
void World::indexed_pairs() {
    std::vector<spatial::Index::Entry> entries;
    entries.reserve(bodies.size());
    for(std::size_t i=0;i<bodies.size();++i) entries.push_back({i,{bodies[i].lo,bodies[i].hi}});
    broadphase_.refit(std::move(entries));
    for(std::size_t i=0;i<bodies.size();++i) {
        if(!moving(bodies[i])) continue;
        const auto& b=bodies[i];
        for(auto j:broadphase_.query(spatial::Aabb{b.lo,b.hi}.expanded(looked_for(b)))) {
            if(j==i || (moving(bodies[j]) && j<i)) continue;
            consider(i,j);
        }
    }
}
void World::consider(std::size_t i, std::size_t j) {
    const Body& a = bodies[i];
    Body& b = bodies[j];
    // Two things neither of which gives: nothing between them to solve.
    if (!moves(a) && !moves(b) && !a.sensor && !b.sensor && !(b.dynamic() && !b.awake)) return;
    const double r = looked_for(a);
    if (b.hi.x < a.lo.x - r || b.lo.x > a.hi.x + r || b.hi.y < a.lo.y - r || b.lo.y > a.hi.y + r || b.hi.z < a.lo.z - r ||
        b.lo.z > a.hi.z + r)
        return;
    if (b.dynamic() && !b.awake) {
        b.place();
        // Something driven into it, or out from under it: it wakes now,
        // and gives this very step.
        if (a.driven) wake(b);
    }
    pair(std::min(i, j), std::max(i, j), r);
}

void World::every_pair() {
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        if (!moving(bodies[i])) continue;
        for (std::size_t j = 0; j < bodies.size(); ++j) {
            if (j == i || (moving(bodies[j]) && j < i)) continue;  // (the pair is met from the other side)
            consider(i, j);
        }
    }
}

void World::sweep_pairs() {
    const std::size_t n = bodies.size();
    span_lo_.resize(n);
    span_hi_.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        const Body& b = bodies[k];
        const double g = moving(b) ? looked_for(b) : 0.0;
        span_lo_[k] = b.lo.x - g, span_hi_[k] = b.hi.x + g;
    }
    const auto before = [&](std::size_t a, std::size_t b) { return span_lo_[a] < span_lo_[b] || (span_lo_[a] == span_lo_[b] && a < b); };
    if (order_.size() != n) {
        order_.resize(n);
        for (std::size_t k = 0; k < n; ++k) order_[k] = k;
        std::sort(order_.begin(), order_.end(), before);
    } else {
        for (std::size_t i = 1; i < n; ++i) {
            const std::size_t v = order_[i];
            std::size_t j = i;
            for (; j > 0 && before(v, order_[j - 1]); --j) order_[j] = order_[j - 1];
            order_[j] = v;
        }
    }
    const auto prune = [&](std::vector<std::size_t>& open, double at) {
        for (std::size_t i = 0; i < open.size();)
            if (span_hi_[open[i]] < at) open[i] = open.back(), open.pop_back();
            else ++i;
    };
    open_moving_.clear();
    open_still_.clear();
    for (std::size_t k : order_) {
        prune(open_moving_, span_lo_[k]);
        prune(open_still_, span_lo_[k]);
        const bool mk = moving(bodies[k]);
        for (std::size_t m : open_moving_) consider(mk ? std::min(m, k) : m, mk ? std::max(m, k) : k);
        if (mk)
            for (std::size_t st : open_still_) consider(k, st);
        (mk ? open_moving_ : open_still_).push_back(k);
    }
}

} // namespace sg::rigid
