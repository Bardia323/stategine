#include "sg/core/Core.hpp"

#include <algorithm>
#include <mutex>

auto std::hash<sg::Key>::operator()(const sg::Key& k) const noexcept -> std::size_t {
    return std::hash<const void*>{}(k.handle());
}

namespace sg {

namespace {
// The names, in shards by their hash, each behind its own lock: any thread
// may name things, and two seldom want one shard at once. A set's nodes never
// move, so a name, once made, is where it is for good.
struct Shard {
    std::mutex lock;
    std::unordered_set<std::string> names;
};
Shard& shard_of(const std::string& s) {
    static Shard shards[64];
    const std::size_t h = std::hash<std::string>{}(s);
    return shards[(h ^ (h >> 29)) & 63];
}
}  // namespace

const std::string* Key::intern(std::string s) {
    Shard& sh = shard_of(s);
    std::lock_guard<std::mutex> held(sh.lock);
    return &*sh.names.insert(std::move(s)).first;
}

const std::string* Key::intern_ref(const std::string& s) {
    Shard& sh = shard_of(s);
    std::lock_guard<std::mutex> held(sh.lock);
    const auto it = sh.names.find(s);
    return it != sh.names.end() ? &*it : &*sh.names.insert(s).first;
}

std::string to_string(const Value& v) {
    struct Vis {
        std::string operator()(std::monostate) const { return "nil"; }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(int64_t i) const { return std::to_string(i); }
        std::string operator()(double d) const { return std::to_string(d); }
        std::string operator()(const std::string& s) const { return s; }
    };
    return std::visit(Vis{}, v);
}

}  // namespace sg

namespace sg::detail {

void refused_to_observer(const std::string& what) {
    const std::string p = "a listener " + what +
                          ": listeners observe; only the world changes the world - declare it in the graph "
                          "(a state says it, and a transition or an embedding's functor carries it)";
    ObserverRules& r = observer_rules();
    if (r.policy == Observers::Strict) throw ObserverError(p);
    if (!r.said.insert(p).second) return;
    if (r.report) r.report(p);
    else std::fprintf(stderr, "[sg] %s\n", p.c_str());
}

}  // namespace sg::detail

namespace sg {

void set_observers(Observers policy, std::function<void(const std::string&)> report) {
    detail::ObserverRules& r = detail::observer_rules();
    r.policy = policy;
    r.report = std::move(report);
}

}  // namespace sg

namespace sg::detail {

void Revision::refuse(const char* what) const {
    if (sealed.load(std::memory_order_relaxed) > 0 && restoring() == 0)
        throw RewriteRefused(std::string("the graph was rewritten while it was being checked: ") + what);
}

void Revision::element(const char* what) {
    refuse(what);
    if (observing() > 0) refused_to_observer(std::string("changed what a state is made of: ") + what);
    ++all;
}

void Revision::rewired(const char* what) {
    refuse(what);
    if (observing() > 0) refused_to_observer(std::string("rewrote the graph: ") + what);
    ++all;
    ++topology;
}

}  // namespace sg::detail

namespace sg {

uint64_t mix_stamp(uint64_t h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdull;
}

void EventBus::drain_into(std::vector<Event>& out) {
    out.clear();
    out.swap(queue_);
    if (listeners_.empty() || detail::trials() > 0) return;
    struct Observing {
        Observing() { ++detail::observing(); }
        ~Observing() { --detail::observing(); }
    };
    for (const auto& e : out) {
        auto it = listeners_.find(e.name);
        if (it == listeners_.end()) continue;
        Observing watching;
        for (const auto& fn : it->second) fn(e);
    }
}

bool angular_key(Key k) {
    return k == keys::yaw || k == keys::pitch || k == keys::roll;
}

bool same_number(Key k, double a, double b, double tolerance) {
    double d = a - b;
    if (angular_key(k)) {
        const double turn = 6.283185307179586;
        d = std::fmod(d, turn);
        if (d > turn * 0.5) d -= turn;
        if (d < -turn * 0.5) d += turn;
    }
    return std::fabs(d) < tolerance;
}

Vec3d unit(const Vec3d& a) {
    const double l = length(a);
    return l > 1e-12 ? Vec3d{a.x / l, a.y / l, a.z / l} : Vec3d{0, 1, 0};
}

double smoothstep(double a, double b, double v) {
    const double t = std::clamp((v - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

Vec3d rotate_xz(const Vec3d& v, double yaw) {
    const double c = std::cos(yaw), s = std::sin(yaw);
    return {v.x * c - v.z * s, v.y, v.x * s + v.z * c};
}

Vec3d position_of(const Element& e) {
    return {e.params.num(keys::x), e.params.num(keys::y), e.params.num(keys::z)};
}

void set_position(Element& e, const Vec3d& p) {
    e.params.set(keys::x, p.x).set(keys::y, p.y).set(keys::z, p.z);
}

}  // namespace sg
