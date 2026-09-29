// Stategine - core value / element / morphism primitives.
//
// Parameter and element names are interned into `Key`s once, at setup time, so
// the hot paths (lookup, dispatch, transport) compare and hash pointers instead
// of characters. Everything still takes plain strings at the API surface.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "sg/Version.hpp"

namespace sg {

// ---------------------------------------------------------------------------
// Key: an interned name. Cheap to copy, compare and hash.
// The table is process-wide and never shrinks; intern during setup, not per
// frame. Not thread safe - build your graph on one thread.
// ---------------------------------------------------------------------------
class Key {
public:
    Key() = default;
    Key(const char* s) : text_(intern_literal(s)) {}               // NOLINT: implicit on purpose
    Key(const std::string& s) : text_(intern_ref(s)) {}            // NOLINT
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
    static std::unordered_set<std::string>& table() {
        static std::unordered_set<std::string> t;
        return t;
    }
    static const std::string* intern(std::string s) { return &*table().insert(std::move(s)).first; }
    // Looked up without copying when it is there already - the usual case.
    // inline: every name made in a frame goes through it.
    static const std::string* intern_ref(const std::string& s) {
        auto& t = table();
        const auto it = t.find(s);
        return it != t.end() ? &*it : &*t.insert(s).first;
    }
    // Most names in code are string literals, met over and over in a frame
    // (`params.num("x")`): each is remembered by where its characters are, in
    // a small table keyed by that address, and taken from there after one
    // comparison of the characters - a buffer reused for another name is
    // simply looked up again. inline: every Key{"..."} in a frame is this.
    static const std::string* intern_literal(const char* s) {
        struct Slot {
            const char* at = nullptr;
            const std::string* key = nullptr;
        };
        static Slot slots[4096];
        Slot& slot = slots[(reinterpret_cast<std::uintptr_t>(s) >> 2) & 4095];
        if (slot.at == s && std::strcmp(slot.key->c_str(), s) == 0) return slot.key;
        const std::string* key = intern_ref(std::string(s));
        slot.at = s;
        slot.key = key;
        return key;
    }

    const std::string* text_ = nullptr;
};

inline std::string operator+(const std::string& a, const Key& b) { return a + b.str(); }
inline std::string operator+(const Key& a, const std::string& b) { return a.str() + b; }
inline std::string operator+(const char* a, const Key& b) { return std::string(a) + b.str(); }

}  // namespace sg

template <>
struct std::hash<sg::Key> {
    std::size_t operator()(const sg::Key& k) const noexcept;
};

namespace sg {

// ---------------------------------------------------------------------------
// Value: a dynamically typed parameter cell.
// ---------------------------------------------------------------------------
using Value = std::variant<std::monostate, bool, int64_t, double, std::string>;

std::string to_string(const Value& v);

// ---------------------------------------------------------------------------
// Stamps: what version of its content a thing holds.
//
// Every change to a thing's content - a parameter set to a new value, one
// taken away - is given the next number of one process-wide count, and the
// thing carries it. Content copied carries its stamp with it: the stamp names
// the content, not the place it is kept, so two things with the same stamp
// hold the same content, and a state put back from a snapshot is, stamp for
// stamp, what it was. That is all anything derived needs to know whether to
// look again - a portal's sync, a law already checked, a cached path - and
// it is never part of what a state *is*: nothing reads a stamp to decide what
// the world does, only whether work already done still stands.
// ---------------------------------------------------------------------------
inline uint64_t& stamp_count() {
    static uint64_t n = 0;
    return n;
}
inline uint64_t next_stamp() { return ++stamp_count(); }
// The newest stamp given out: if it has not moved, nothing anywhere changed.
inline uint64_t last_stamp() { return stamp_count(); }

// ---------------------------------------------------------------------------
// A change to what the graph is made of, refused: the graph was being checked
// (a law's trial run), and a check must not leave the world rewritten. The
// trial is undone; the check that tried it reports it.
// ---------------------------------------------------------------------------
struct RewriteRefused : std::logic_error {
    using std::logic_error::logic_error;
};

// ---------------------------------------------------------------------------
// Observers. A listener on a state's bus (EventBus::subscribe) observes: it
// reads the event and does what is outside the world - draws, prints, plays a
// sound, logs. Only the world causes changes to the world: a listener that
// fires the engine, sends a state an event, or rewrites the graph is a way
// between states the graph does not declare. If an observer must change the
// world, it is part of the world - a state, and what it does arrows, functors
// and transitions.
//
// So while a listener runs, those are refused: with an ObserverError
// (Observers::Strict, the default), or reported once each and let through
// (Observers::Report - for a project still moving its listeners into the
// graph). What a listener writes into a state's params directly is not seen
// here (it would cost every write); the engine's watch (set_watch_hooks) and
// the laws see what arrows do.
// ---------------------------------------------------------------------------
struct ObserverError : std::logic_error {
    using std::logic_error::logic_error;
};

enum class Observers { Strict, Report };

namespace detail {

// How many listeners are running on this thread, one inside another.
inline int& observing() {
    static thread_local int n = 0;
    return n;
}

struct ObserverRules {
    Observers policy = Observers::Strict;
    std::function<void(const std::string&)> report;
    std::unordered_set<std::string> said;
};
inline ObserverRules& observer_rules() {
    static ObserverRules r;
    return r;
}

void refused_to_observer(const std::string& what);

// Where the world is changed from outside an arrow, each asks
// `observing() > 0` first: free unless a listener is running.

}  // namespace detail

// How listeners that try to cause something are met, process-wide; `report`
// hears each once under Observers::Report (stderr if unset).
void set_observers(Observers policy, std::function<void(const std::string&)> report = nullptr);

namespace detail {

// Where a graph counts changes to its structure. The graph owns one and hands
// its address to every state and functor it takes in, so a change made
// through any of them - an element added, an object mapped - is counted where
// it belongs, with an add. Changing a value is not a change of structure and
// never comes here. While a law's trial runs, no change of structure is let
// through at all - not of what joins the states, and not of what is in one:
// a trial undoes data, and structure is not data.
struct Revision {
    uint64_t all = 0;       // anything structural: elements and arrows too
    uint64_t topology = 0;  // the interfaces: states, functors, embeddings, seams, transitions
    int sealed = 0;         // > 0 while a law's trial runs

    void element(const char* what);
    void rewired(const char* what);

private:
    void refuse(const char* what) const {
        if (sealed)
            throw RewriteRefused(std::string("the graph was rewritten while it was being checked: ") + what);
    }
};

}  // namespace detail

// Folds a stamp into a running version of many (a state's, a law's).
uint64_t mix_stamp(uint64_t h, uint64_t v);

// ---------------------------------------------------------------------------
// Params: a small flat map. Elements carry a handful of entries, so a linear
// scan over contiguous memory beats hashing - and there is no per-key node.
//
// Copies share their entries until one of them writes: a copy of a state (a
// snapshot, a law's trial, a default kept) costs a pointer per element, and
// only what is then changed is copied for real.
//
// inline: every arrow reads and writes through it, every frame.
// ---------------------------------------------------------------------------
class Params {
public:
    using Entry = std::pair<Key, Value>;

    // Setting a value it already holds is no change, and is not stamped as
    // one: whatever follows from these params need not look again.
    Params& set(Key key, Value v) {
        if (const Value* held = slot_of(key)) {
            if (*held == v) return *this;
            *slot_of_mine(key) = std::move(v);
        } else {
            mine().emplace_back(key, std::move(v));
        }
        stamp_ = next_stamp();
        return *this;
    }

    // Which version of its content this is (see Stamps above). Zero is the
    // empty content nobody has written.
    uint64_t stamp() const { return stamp_; }

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

    // The text under `key`, read where it is (no copy); null if there is none.
    const std::string* text(Key key) const {
        const Value* v = slot_of(key);
        return v ? std::get_if<std::string>(v) : nullptr;
    }
    // Whether the text under `key` is `s`.
    bool is(Key key, const char* s) const {
        const std::string* t = text(key);
        return t && *t == s;
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
        if (!slot_of(key)) return;
        std::vector<Entry>& es = mine();
        for (std::size_t i = 0; i < es.size(); ++i) {
            if (es[i].first == key) {
                es[i] = std::move(es.back());
                es.pop_back();
                stamp_ = next_stamp();
                return;
            }
        }
    }

    std::size_t size() const { return all().size(); }
    bool empty() const { return all().empty(); }
    void clear() {
        if (empty()) return;
        entries_.reset();
        stamp_ = next_stamp();
    }

    std::vector<Entry>::const_iterator begin() const { return all().begin(); }
    std::vector<Entry>::const_iterator end() const { return all().end(); }
    const std::vector<Entry>& all() const {
        static const std::vector<Entry> none;
        return entries_ ? *entries_ : none;
    }

private:
    // The entries, this copy's own to write: shared ones are copied first.
    std::vector<Entry>& mine() {
        if (!entries_) entries_ = std::make_shared<std::vector<Entry>>();
        else if (entries_.use_count() > 1) entries_ = std::make_shared<std::vector<Entry>>(*entries_);
        return *entries_;
    }
    Value* slot_of_mine(Key key) {
        for (auto& e : mine())
            if (e.first == key) return &e.second;
        return nullptr;
    }

    const Value* slot_of(Key key) const {
        if (!entries_) return nullptr;
        for (const auto& e : *entries_)
            if (e.first == key) return &e.second;
        return nullptr;
    }

    std::shared_ptr<std::vector<Entry>> entries_;
    uint64_t stamp_ = 0;
};

// ---------------------------------------------------------------------------
// Element: an object of a state's category.
//
// What an element is - its id, its kind - is fixed when it is made: it is how
// the state finds it, how its arrows name it, what the renderer takes it for.
// Renaming one in place would leave the state's index, its arrows and every
// functor that maps it pointing at something else. So the two read like any
// Key, and nothing but the element itself can set them; an element with
// another name is another element (take this one away, add that one). Its
// params and whether it is alive are its to change.
// ---------------------------------------------------------------------------
class Middle;
struct Element;

class ElementKey {
public:
    ElementKey(Key k = Key{}) : k_(k) {}  // NOLINT: made from a Key on purpose
    ElementKey(const char* s) : k_(s) {}  // NOLINT
    ElementKey(const ElementKey&) = default;
    operator Key() const { return k_; }  // NOLINT: reads as the Key it is
    Key key() const { return k_; }
    const std::string& str() const { return k_.str(); }
    const char* c_str() const { return k_.c_str(); }
    bool empty() const { return k_.empty(); }
    const void* handle() const { return k_.handle(); }
    friend bool operator==(const ElementKey& a, const ElementKey& b) { return a.k_ == b.k_; }
    friend bool operator!=(const ElementKey& a, const ElementKey& b) { return a.k_ != b.k_; }
    friend bool operator==(const ElementKey& a, const Key& b) { return a.k_ == b; }
    friend bool operator!=(const ElementKey& a, const Key& b) { return a.k_ != b; }
    friend bool operator==(const Key& a, const ElementKey& b) { return a == b.k_; }
    friend bool operator!=(const Key& a, const ElementKey& b) { return a != b.k_; }
    friend bool operator<(const ElementKey& a, const ElementKey& b) { return a.k_ < b.k_; }
    friend std::string operator+(const std::string& a, const ElementKey& b) { return a + b.str(); }
    friend std::string operator+(const ElementKey& a, const std::string& b) { return a.str() + b; }
    friend std::string operator+(const char* a, const ElementKey& b) { return std::string(a) + b.str(); }

private:
    friend struct Element;
    ElementKey& operator=(const ElementKey&) = default;
    Key k_;
};

struct Element {
    ElementKey id;
    ElementKey kind;  // "sprite", "mesh", "light", "portal", ...
    Params params;
    bool alive = true;

    Element() = default;
    Element(Key id_, Key kind_) : id(id_), kind(kind_) {}
    Element(const Element&) = default;
    Element(Element&&) noexcept = default;
    // One element's data put in another's place: a snapshot restored over
    // the element it was taken of (same id), or a list rebuilt whole.
    Element& operator=(const Element&) = default;
    Element& operator=(Element&&) noexcept = default;

private:
    friend class Middle;  // a composite's scratch element, remade for each object
    void remake(Key id_, Key kind_) {
        id.k_ = id_;
        kind.k_ = kind_;
    }
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

struct DeclaredStep;  // Declared.hpp

struct Morphism {
    using Handler = std::function<void(State&, Element& from, Element* to, const Event&)>;

    Key name;
    Key from;
    Key to;
    Key trigger;
    Handler handler;
    // For a composite, the arrows it was built from, first applied first. The
    // laws check that the composite still does what its parts do in order.
    std::vector<Key> parts;
    // What it does, declared (Declared.hpp) - the steps its handler is made
    // of - or nothing, when it says nothing: it may do anything.
    std::shared_ptr<const std::vector<DeclaredStep>> declared;
    // The native computation - a name a source binds it by - that is the inside
    // of its handler, when a source declared one. Identity for holding two
    // declarations side by side; nothing is claimed about what the C++ does.
    Key native = Key{};
};

// The type of an arrow. An endomorphism leaves `to` empty, but its codomain is
// still its domain - forgetting that is how a composite ending in a loop once
// got registered as a loop on the wrong element.
inline Key dom(const Morphism& m) { return m.from; }
inline Key cod(const Morphism& m) { return m.to.empty() ? m.from : m.to; }

// ---------------------------------------------------------------------------
// EventBus: a double-buffered queue plus direct subscriptions. The buffers are
// reused frame to frame, so steady-state dispatch does not allocate.
// ---------------------------------------------------------------------------
namespace detail {
// How many law trials are running on this thread (Laws.hpp's Trial): while
// any is, nothing outside the graph hears what a state does.
inline int& trials() {
    static thread_local int n = 0;
    return n;
}
}  // namespace detail

class EventBus {
public:
    using Listener = std::function<void(const Event&)>;

    void emit(Event e) { queue_.push_back(std::move(e)); }

    // What is waiting to be dispatched. The law checks run arrows on trial and
    // compare what they emitted, then put the queue back as they found it.
    const std::vector<Event>& queued() const { return queue_; }
    void requeue(std::vector<Event> q) { queue_ = std::move(q); }

    // A listener watches a state from outside the graph - a test, a tool, a
    // log. It is not a way between states: what a state has to tell another
    // it says (State::says), and the graph's transitions carry it. A law's
    // trial runs arrows to see what they do and then un-runs them; a
    // listener could not be un-run, so while a trial runs none is called.
    void subscribe(Key name, Listener fn) { listeners_[name].push_back(std::move(fn)); }

    // Swaps the pending queue into `out` so handlers may emit freely.
    void drain_into(std::vector<Event>& out);

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
inline const Key time{"time"}, frame{"frame"};  // what a clock says (Temporal.hpp)
// An element may be placed relative to another: `parent` names that element,
// and the pose stored here is then local to it.
inline const Key parent{"parent"};
}  // namespace keys

namespace kinds {
inline const Key sprite{"sprite"}, mesh{"mesh"}, light{"light"}, portal{"portal"};
inline const Key wall{"wall"}, anchor{"anchor"}, tile{"tile"};
inline const Key camera{"camera"}, textbuffer{"textbuffer"}, textline{"textline"};
}  // namespace kinds

// Some parameters are not plain numbers: a heading lives on a circle, so two
// values a full turn apart are the same value. Anything comparing transported
// data has to know that, or a round trip that returns you exactly where you
// started reads as a drift of 2*pi.
bool angular_key(Key k);

bool same_number(Key k, double a, double b, double tolerance = 1e-6);

// Position helpers shared by every spatial domain.
struct Vec3d {
    double x = 0, y = 0, z = 0;
};

// The arithmetic every world needs of a point, once, here - so no two of them
// write it out and disagree.
inline Vec3d operator+(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3d operator-(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3d operator*(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }

// --- one rotation, one heading ----------------------------------------------
// Every sign error this engine has shipped came from writing a rotation out by
// hand a second time, or from two places disagreeing about what an angle is
// measured from. There is one answer here and everything else calls it.
//
//   yaw is a heading measured from +x, turning towards +z.
//   heading(yaw) is the way something with that yaw faces.
//   across(yaw)  is ninety degrees to its left.
//
// A renderer that builds a rotation matrix must agree with this; `sg_tests`
// checks that it does, since the two live on opposite sides of a layer
// boundary and cannot share the code itself.
Vec3d rotate_xz(const Vec3d& v, double yaw);

inline Vec3d heading(double yaw) { return {std::cos(yaw), 0.0, std::sin(yaw)}; }

inline Vec3d across(double yaw) { return {-std::sin(yaw), 0.0, std::cos(yaw)}; }

Vec3d position_of(const Element& e);

void set_position(Element& e, const Vec3d& p);

}  // namespace sg
