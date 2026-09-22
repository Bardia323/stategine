// Stategine - core value / element / morphism primitives.
//
// Parameter and element names are interned into `Key`s once, at setup time, so
// the hot paths (lookup, dispatch, transport) compare and hash pointers instead
// of characters. Everything still takes plain strings at the API surface.
#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace sg {

// ---------------------------------------------------------------------------
// Key: an interned name. Cheap to copy, compare and hash.
// The table is process-wide and never shrinks; intern during setup, not per
// frame. Not thread safe - build your graph on one thread.
// ---------------------------------------------------------------------------
class Key {
public:
    Key() = default;
    Key(const char* s) : text_(intern(std::string(s))) {}          // NOLINT: implicit on purpose
    Key(const std::string& s) : text_(intern(s)) {}                // NOLINT
    Key(std::string&& s) : text_(intern(std::move(s))) {}          // NOLINT

    const std::string& str() const {
        static const std::string empty;
        return text_ ? *text_ : empty;
    }

    const char* c_str() const { return str().c_str(); }
    bool empty() const { return text_ == nullptr || text_->empty(); }

    bool operator==(const Key& o) const { return text_ == o.text_; }
    bool operator!=(const Key& o) const { return text_ != o.text_; }
    bool operator<(const Key& o) const { return str() < o.str(); }

    const void* handle() const { return text_; }

private:
    static const std::string* intern(std::string s) {
        static std::unordered_set<std::string> table;
        return &*table.insert(std::move(s)).first;
    }

    const std::string* text_ = nullptr;
};

inline std::string operator+(const std::string& a, const Key& b) { return a + b.str(); }
inline std::string operator+(const Key& a, const std::string& b) { return a.str() + b; }
inline std::string operator+(const char* a, const Key& b) { return std::string(a) + b.str(); }

}  // namespace sg

template <>
struct std::hash<sg::Key> {
    std::size_t operator()(const sg::Key& k) const noexcept {
        return std::hash<const void*>{}(k.handle());
    }
};

namespace sg {

// ---------------------------------------------------------------------------
// Value: a dynamically typed parameter cell.
// ---------------------------------------------------------------------------
using Value = std::variant<std::monostate, bool, int64_t, double, std::string>;

inline std::string to_string(const Value& v) {
    struct Vis {
        std::string operator()(std::monostate) const { return "nil"; }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(int64_t i) const { return std::to_string(i); }
        std::string operator()(double d) const { return std::to_string(d); }
        std::string operator()(const std::string& s) const { return s; }
    };
    return std::visit(Vis{}, v);
}

// ---------------------------------------------------------------------------
// Params: a small flat map. Elements carry a handful of entries, so a linear
// scan over contiguous memory beats hashing - and there is no per-key node.
// ---------------------------------------------------------------------------
class Params {
public:
    using Entry = std::pair<Key, Value>;

    Params& set(Key key, Value v) {
        if (Value* slot = slot_of(key)) {
            *slot = std::move(v);
        } else {
            entries_.emplace_back(key, std::move(v));
        }
        return *this;
    }

    bool has(Key key) const { return slot_of(key) != nullptr; }

    const Value& get(Key key) const {
        if (const Value* v = slot_of(key)) return *v;
        throw std::out_of_range("Params: no key " + key.str());
    }

    template <typename T>
    T get_or(Key key, T fallback) const {
        if (const Value* v = slot_of(key))
            if (const T* p = std::get_if<T>(v)) return *p;
        return fallback;
    }

    // Convenience for the common numeric case: reads ints as doubles too.
    double num(Key key, double fallback = 0.0) const {
        const Value* v = slot_of(key);
        if (!v) return fallback;
        if (const double* d = std::get_if<double>(v)) return *d;
        if (const int64_t* i = std::get_if<int64_t>(v)) return static_cast<double>(*i);
        return fallback;
    }

    void erase(Key key) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].first == key) {
                entries_[i] = std::move(entries_.back());
                entries_.pop_back();
                return;
            }
        }
    }

    void merge_from(const Params& other) {
        for (const auto& e : other.entries_) set(e.first, e.second);
    }

    std::size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }
    void clear() { entries_.clear(); }

    std::vector<Entry>::const_iterator begin() const { return entries_.begin(); }
    std::vector<Entry>::const_iterator end() const { return entries_.end(); }
    const std::vector<Entry>& all() const { return entries_; }

private:
    Value* slot_of(Key key) {
        for (auto& e : entries_)
            if (e.first == key) return &e.second;
        return nullptr;
    }

    const Value* slot_of(Key key) const {
        for (const auto& e : entries_)
            if (e.first == key) return &e.second;
        return nullptr;
    }

    std::vector<Entry> entries_;
};

// ---------------------------------------------------------------------------
// Element: an object of a state's category.
// ---------------------------------------------------------------------------
struct Element {
    Key id;
    Key kind;  // "sprite", "mesh", "light", "portal", ...
    Params params;
    bool alive = true;

    Element() = default;
    Element(Key id_, Key kind_) : id(id_), kind(kind_) {}
};

// ---------------------------------------------------------------------------
// Event: the payload carried along a morphism.
// ---------------------------------------------------------------------------
struct Event {
    Key name;
    Params args;
    Key source;  // element or state that emitted it

    Event() = default;
    explicit Event(Key name_) : name(name_) {}
    Event(Key name_, Params args_) : name(name_), args(std::move(args_)) {}
};

// ---------------------------------------------------------------------------
// Morphism: a named arrow inside a state, domain element -> codomain element,
// fired by an event name. Empty `to` means an endomorphism on `from`.
// ---------------------------------------------------------------------------
class State;

struct Morphism {
    using Handler = std::function<void(State&, Element& from, Element* to, const Event&)>;

    Key name;
    Key from;
    Key to;
    Key trigger;
    Handler handler;
};

// ---------------------------------------------------------------------------
// EventBus: a double-buffered queue plus direct subscriptions. The buffers are
// reused frame to frame, so steady-state dispatch does not allocate.
// ---------------------------------------------------------------------------
class EventBus {
public:
    using Listener = std::function<void(const Event&)>;

    void emit(Event e) { queue_.push_back(std::move(e)); }

    void subscribe(Key name, Listener fn) { listeners_[name].push_back(std::move(fn)); }

    // Swaps the pending queue into `out` so handlers may emit freely.
    void drain_into(std::vector<Event>& out) {
        out.clear();
        out.swap(queue_);
        if (listeners_.empty()) return;
        for (const auto& e : out) {
            auto it = listeners_.find(e.name);
            if (it == listeners_.end()) continue;
            for (const auto& fn : it->second) fn(e);
        }
    }

    bool empty() const { return queue_.empty(); }
    void clear() { queue_.clear(); }

private:
    std::vector<Event> queue_;
    std::unordered_map<Key, std::vector<Listener>> listeners_;
};

// ---------------------------------------------------------------------------
// Shared parameter vocabulary. Domains that agree on these names get functors,
// renderers and editors for free - that is the whole point of interning them
// in one place rather than spelling "x" in thirty files.
// ---------------------------------------------------------------------------
namespace keys {
inline const Key x{"x"}, y{"y"}, z{"z"};
inline const Key vx{"vx"}, vy{"vy"}, vz{"vz"};
inline const Key sx{"sx"}, sy{"sy"}, sz{"sz"};
inline const Key r{"r"}, g{"g"}, b{"b"};
inline const Key yaw{"yaw"}, pitch{"pitch"}, roll{"roll"};
inline const Key w{"w"}, h{"h"}, fov{"fov"};
inline const Key glyph{"glyph"}, text{"text"}, open{"open"}, intensity{"intensity"};
inline const Key dt{"dt"}, name{"name"}, commit{"commit"};
// An element may be placed relative to another: `parent` names that element,
// and the pose stored here is then local to it.
inline const Key parent{"parent"};
}  // namespace keys

namespace kinds {
inline const Key sprite{"sprite"}, mesh{"mesh"}, light{"light"}, portal{"portal"};
inline const Key wall{"wall"}, anchor{"anchor"};
inline const Key camera{"camera"}, textbuffer{"textbuffer"}, textline{"textline"};
}  // namespace kinds

// Some parameters are not plain numbers: a heading lives on a circle, so two
// values a full turn apart are the same value. Anything comparing transported
// data has to know that, or a round trip that returns you exactly where you
// started reads as a drift of 2*pi.
inline bool angular_key(Key k) {
    return k == keys::yaw || k == keys::pitch || k == keys::roll;
}

inline bool same_number(Key k, double a, double b, double tolerance = 1e-6) {
    double d = a - b;
    if (angular_key(k)) {
        const double turn = 6.283185307179586;
        d = std::fmod(d, turn);
        if (d > turn * 0.5) d -= turn;
        if (d < -turn * 0.5) d += turn;
    }
    return std::fabs(d) < tolerance;
}

// Position helpers shared by every spatial domain.
struct Vec3d {
    double x = 0, y = 0, z = 0;
};

inline Vec3d position_of(const Element& e) {
    return {e.params.num(keys::x), e.params.num(keys::y), e.params.num(keys::z)};
}

inline void set_position(Element& e, const Vec3d& p) {
    e.params.set(keys::x, p.x).set(keys::y, p.y).set(keys::z, p.z);
}

}  // namespace sg
