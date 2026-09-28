// Stategine - algebra: the laws' equations, compiled; and the accelerator.
//
// The laws are equations of two paths run on the same data (sg/core/Laws.hpp),
// and they stay the truth. Some paths can be answered without running them:
// every step on them says what it does (sg/core/Declared.hpp) - an affine
// arrow, a transport that copies, renames or sums - so what the path does to
// the data it reads is an operator on a vector of values (Operator.hpp).
//
//     Equation {lhs, rhs}  --try_compile-->  two Programs, a vector, the slots to compare
//                          (or nothing: a step that says nothing, a transition,
//                          an event, an element to be made, sides that end
//                          apart - the verifier runs it, as before)
//
// A slot is one parameter of one element of one state, as the live data has
// it (or of the element made fresh between two stages of a transport). The
// compiler runs each side over the slots in its head: which parameters are
// there, what kind of value each is - so a row that would not run is not
// written, a copy keeps what a value is, and arithmetic is on numbers. What
// it compares is every slot either side wrote in the state they end in, as
// the verifier compares values (same_value: numbers to a hair, headings on
// the circle, the rest exactly). Anything it cannot say for certain it does
// not compile.
//
// `Accelerated` is a laws::Accelerator: offered each equation, it compiles it
// (or takes it from its cache: compiled programs are kept per graph, and read
// again only when what they were compiled on changed), adds it to a batch,
// and at the end runs the batch on its backend. An equation the batch finds
// broken is checked again by the verifier, by running it, and the verifier's
// counterexample is what is reported: the report is the verifier's.
#pragma once

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/algebra/Backend.hpp"
#include "sg/algebra/Program.hpp"
#include "sg/core/Laws.hpp"

namespace sg::algebra {

// What a value is, as far as comparing it goes.
enum class Kind : uint8_t { Absent, None, Flag, Int, Real, Word };

inline Kind kind_of(const Value& v) {
    switch (v.index()) {
        case 0: return Kind::None;
        case 1: return Kind::Flag;
        case 2: return Kind::Int;
        case 3: return Kind::Real;
        default: return Kind::Word;
    }
}
inline bool numeric(Kind k) { return k == Kind::Int || k == Kind::Real; }

// An equation, compiled against the data as it is laid out now.
struct Compiled {
    Program lhs, rhs;
    std::vector<Program::Slot> compare;
    // Where each slot's starting value is read, each time: an element and a
    // parameter (null for a slot that starts empty - a fresh element's).
    struct Source {
        const Element* element = nullptr;
        Key param;
    };
    std::vector<Source> sources;
    // What it was compiled on: the states it reads, their make-up, and the
    // parameters of the elements it reads (names and kinds).
    struct Read {
        const State* state;
        uint64_t structure;
    };
    std::vector<Read> states;
    std::vector<const Element*> elements;
    uint64_t layout = 0;
    uint64_t revision = 0;
};

namespace detail {

inline uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ULL; }

// What the parameters of these elements are, and what kind: a change in
// either is a different compilation.
inline uint64_t layout_of(const std::vector<const Element*>& es) {
    uint64_t h = 1469598103934665603ULL;
    for (const Element* e : es) {
        h = mix(h, reinterpret_cast<uintptr_t>(e));
        for (const auto& kv : e->params) {
            h = mix(h, std::hash<Key>{}(kv.first));
            h = mix(h, kv.second.index());
        }
    }
    return h;
}

// Each side is run in the head as expressions: a slot's value is a node - a
// value as the data holds it at the start (a leaf), or a sum of nodes times
// numbers, plus a number. Nodes are shared (the same sum of the same nodes is
// one node), so two sides that leave a slot the very same expression leave it
// the same value, bit for bit - the same operations on the same values in the
// same order, as the verifier's runs of the same declared rows would do - and
// it need not be compared. What is left to compare is where they differ, and
// the program is only what those need.
//
// A Workspace holds what equations of one graph share while they are checked
// together: the slots, the nodes, and each functor carried from untouched data
// (most sides carry a functor over data one arrow touched at most: the rest
// of its objects are what they always are, found once).
class Workspace {
public:
    explicit Workspace(StateGraph& g) : g_(g), revision_(g.revision()) {}
    const StateGraph& graph() const { return g_; }
    uint64_t revision() const { return revision_; }

    std::optional<Compiled> compile(const Equation& eq) {
        eq_ = &eq;
        elements_.clear();
        read_.clear();
        states_.clear();
        Side l, r;
        const bool ok = side(eq.lhs, l) && side(eq.rhs, r) && l.here == r.here && l.at == r.at;  // ends apart: the verifier says where
        eq_ = nullptr;
        if (!ok) return std::nullopt;
        // Every slot either side wrote in the state they end in.
        std::vector<uint32_t> written = l.written;
        written.insert(written.end(), r.written.begin(), r.written.end());
        std::sort(written.begin(), written.end());
        written.erase(std::unique(written.begin(), written.end()), written.end());
        struct Pair {
            uint32_t left, right;
            Compare how;
        };
        std::vector<Pair> differ;
        for (uint32_t i : written) {
            if (slots_[i].middle || slots_[i].state != l.here) continue;
            const Kind a = l.kind(*this, i), b = r.kind(*this, i);
            if (a == Kind::Absent && b == Kind::Absent) continue;
            if (a == Kind::Absent || b == Kind::Absent) return std::nullopt;  // there on one side only
            Compare how;
            if (numeric(a) && numeric(b)) {
                how = a == Kind::Int && b == Kind::Int ? Compare::Exact
                      : angular_key(slots_[i].param)   ? Compare::Angle
                                                       : Compare::Number;
            } else if (a == b) {
                how = Compare::Exact;
            } else {
                return std::nullopt;  // a number one side, a word the other
            }
            const uint32_t x = l.value(*this, i), y = r.value(*this, i);
            if (x == y) continue;  // the very same expression: the same value
            differ.push_back(Pair{x, y, how});
        }
        // The program: the leaves read, then each node either side needs, in
        // order; a result slot for each pair.
        Compiled c;
        std::unordered_map<uint32_t, uint32_t> at;  // node -> vector slot
        std::vector<uint32_t> order;
        const auto need = [&](auto&& self, uint32_t n) -> void {
            if (at.count(n)) return;
            const Node& nd = nodes_[n];
            if (nd.leaf) {
                at.emplace(n, static_cast<uint32_t>(c.sources.size()));
                const SlotInfo& s = slots_[nd.slot];
                c.sources.push_back(Compiled::Source{s.read_from, s.param});
                return;
            }
            for (const auto& t : nd.terms) self(self, t.second);
            at.emplace(n, 0u);  // placed below, after every leaf
            order.push_back(n);
        };
        for (const Pair& p : differ) need(need, p.left), need(need, p.right);
        uint32_t next = static_cast<uint32_t>(c.sources.size());
        for (uint32_t n : order) at[n] = next++;
        const uint32_t results = next;
        const uint32_t slots = results + static_cast<uint32_t>(differ.size());
        // Each side computes what its own results need.
        const auto program = [&](bool left) {
            Program p;
            p.slots = slots;
            std::unordered_map<uint32_t, bool> done;
            const auto emit = [&](auto&& self, uint32_t n) -> void {
                const Node& nd = nodes_[n];
                if (nd.leaf || done.count(n)) return;
                for (const auto& t : nd.terms) self(self, t.second);
                std::vector<Term> terms;
                for (const auto& t : nd.terms) terms.push_back(Term{at[t.second], t.first});
                p.op.set(at[n], terms, nd.bias);
                done.emplace(n, true);
            };
            for (std::size_t i = 0; i < differ.size(); ++i) {
                const uint32_t n = left ? differ[i].left : differ[i].right;
                emit(emit, n);
                p.op.copy(at[n], results + static_cast<uint32_t>(i));
            }
            return p;
        };
        c.lhs = program(true);
        c.rhs = program(false);
        for (std::size_t i = 0; i < differ.size(); ++i)
            c.compare.push_back(Program::Slot{results + static_cast<uint32_t>(i), differ[i].how});
        for (const auto& kv : states_) c.states.push_back(Compiled::Read{kv.second, kv.second->structure()});
        c.elements = elements_;
        c.layout = layout_of(elements_);
        c.revision = g_.revision();
        return c;
    }

private:
    static constexpr uint32_t kNone = 0xffffffffu;
    struct SlotInfo {
        Key state, element, param;
        bool middle = false;
        const Element* read_from = nullptr;  // where its starting value is read
        Kind start = Kind::Absent;
    };
    struct Node {
        bool leaf = false;
        uint32_t slot = 0;                               // a leaf's
        double bias = 0.0;
        std::vector<std::pair<double, uint32_t>> terms;  // (k, node)
    };
    struct NodeKey {
        double bias;
        std::vector<std::pair<double, uint32_t>> terms;
        bool operator==(const NodeKey& o) const { return bias == o.bias && terms == o.terms; }
    };
    struct NodeHash {
        std::size_t operator()(const NodeKey& k) const {
            uint64_t h = 1469598103934665603ULL, b = 0;
            std::memcpy(&b, &k.bias, sizeof b);
            h = mix(h, b);
            for (const auto& t : k.terms) {
                uint64_t q = 0;
                std::memcpy(&q, &t.first, sizeof q);
                h = mix(mix(h, q), t.second);
            }
            return static_cast<std::size_t>(h);
        }
    };
    struct PlaceKey {
        Key state, element;
        bool operator==(const PlaceKey& o) const { return state == o.state && element == o.element; }
    };
    struct PlaceHash {
        std::size_t operator()(const PlaceKey& k) const {
            return static_cast<std::size_t>(mix(mix(1469598103934665603ULL, std::hash<Key>{}(k.state)), std::hash<Key>{}(k.element)));
        }
    };
    // An element, as a side sees it: a live one, or one made fresh.
    struct Place {
        Key state, element;
        const Element* live = nullptr;  // null: made fresh between two stages
        uint32_t id = kNone;            // its place in the workspace
    };
    // Every parameter slot of a place, by name, found once.
    struct PlaceSlots {
        std::unordered_map<Key, uint32_t> by_param;
        std::vector<Key> start_params;  // the parameters it has at the start, in order
    };
    // One side: what each slot it wrote holds now (a node, and what kind of
    // value), where it is on its path, which places it has touched, and what
    // it has put where there was nothing.
    struct Side {
        Key here, at;
        std::vector<uint32_t> values;  // slot -> node (kNone: as at the start)
        std::vector<uint8_t> kinds;    // slot -> kind + 1 (0: as at the start)
        std::vector<uint32_t> written;
        std::vector<uint8_t> dirty;    // place -> touched
        std::unordered_map<uint32_t, std::vector<Key>> added;  // place -> parameters it did not have
        Kind kind(const Workspace& w, uint32_t i) const {
            return i < kinds.size() && kinds[i] ? static_cast<Kind>(kinds[i] - 1) : w.slots_[i].start;
        }
        uint32_t value(Workspace& w, uint32_t i) const {
            return i < values.size() && values[i] != kNone ? values[i] : w.leaf(i);
        }
        bool touched(uint32_t place) const { return place < dirty.size() && dirty[place]; }
    };
    // A functor carried from untouched data: for each object, what it writes
    // where (on the element it lands on), and what it puts there new.
    struct Write {
        uint32_t slot, node;
        Kind kind;
    };
    struct Carried {
        uint32_t src, dst;  // places
        Key src_id, dst_id; // their elements
        std::vector<Write> writes;
        std::vector<Key> added;
        bool reads_target = false;
    };
    struct FunctorMemo {
        bool ok = true;
        std::vector<Carried> objects;
    };

    uint32_t place(Key state, Key element, const Element* live) {
        const PlaceKey k{state, element};
        auto it = places_.find(k);
        if (it != places_.end()) return it->second;
        const uint32_t id = static_cast<uint32_t>(place_slots_.size());
        place_slots_.emplace_back();
        if (live)
            for (const auto& kv : live->params) place_slots_.back().start_params.push_back(kv.first);
        places_.emplace(k, id);
        return id;
    }
    Place live(const State& s, Key element, const Element* e) { return Place{s.id(), element, e, place(s.id(), element, e)}; }
    Place fresh() {
        const Key element{"~" + std::to_string(fresh_++)};
        return Place{Key{"~"}, element, nullptr, place(Key{"~"}, element, nullptr)};
    }

    uint32_t slot(const Place& p, Key param) {
        PlaceSlots& ps = place_slots_[p.id];
        auto it = ps.by_param.find(param);
        if (it != ps.by_param.end()) return it->second;
        SlotInfo s{p.state, p.element, param, !p.live, nullptr, Kind::Absent};
        if (p.live && p.live->params.has(param)) {
            s.start = kind_of(p.live->params.get(param));
            s.read_from = p.live;
        }
        slots_.push_back(s);
        leaves_.push_back(kNone);
        const uint32_t id = static_cast<uint32_t>(slots_.size() - 1);
        ps.by_param.emplace(param, id);
        return id;
    }
    uint32_t leaf(uint32_t slot) {
        if (leaves_[slot] != kNone) return leaves_[slot];
        Node n;
        n.leaf = true;
        n.slot = slot;
        nodes_.push_back(n);
        return leaves_[slot] = static_cast<uint32_t>(nodes_.size() - 1);
    }
    uint32_t sum(double bias, std::vector<std::pair<double, uint32_t>> terms) {
        NodeKey k{bias, std::move(terms)};
        auto it = sums_.find(k);
        if (it != sums_.end()) return it->second;
        Node n;
        n.bias = k.bias;
        n.terms = k.terms;
        nodes_.push_back(std::move(n));
        const uint32_t id = static_cast<uint32_t>(nodes_.size() - 1);
        sums_.emplace(std::move(k), id);
        return id;
    }
    void reads(const State& s, const Element* e) {
        states_.emplace(s.id(), &s);
        if (e && read_.emplace(e, true).second) elements_.push_back(e);
    }

    // The parameters an element has on this side, now.
    std::vector<Key> params_of(const Side& side, const Place& p) {
        std::vector<Key> out;
        for (Key k : place_slots_[p.id].start_params)
            if (side.kind(*this, slot(p, k)) != Kind::Absent) out.push_back(k);
        auto it = side.added.find(p.id);
        if (it != side.added.end())
            for (Key k : it->second)
                if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
        return out;
    }

    void write(Side& side, const Place& dst, Key param, uint32_t to, uint32_t node, Kind k) {
        if (side.kind(*this, to) == Kind::Absent) {
            auto& a = side.added[dst.id];
            if (std::find(a.begin(), a.end(), param) == a.end()) a.push_back(param);
        }
        if (side.values.size() <= to) side.values.resize(slots_.size(), kNone), side.kinds.resize(slots_.size(), 0);
        side.values[to] = node;
        side.kinds[to] = static_cast<uint8_t>(static_cast<uint8_t>(k) + 1);
        side.written.push_back(to);
        if (side.dirty.size() <= dst.id) side.dirty.resize(place_slots_.size(), 0);
        side.dirty[dst.id] = 1;
    }

    // What `run(Affine)` does (sg/core/Declared.hpp), as expressions.
    void affine(Side& side, const Affine& a, const Place& src, const Place& dst, const Params& args, bool* reads_target = nullptr) {
        const bool same = src.state == dst.state && src.element == dst.element;
        if (a.copy_all && !same)
            for (Key p : params_of(side, src)) {
                const uint32_t from = slot(src, p), to = slot(dst, p);
                write(side, dst, p, to, side.value(*this, from), side.kind(*this, from));
            }
        for (const Affine::Row& r : a.rows) {
            bool there = true;
            for (const Affine::Term& t : r.terms) {
                if (t.of_target && reads_target) *reads_target = true;
                if (side.kind(*this, slot(t.of_target ? dst : src, t.param)) == Kind::Absent) there = false;
            }
            if (!there) continue;
            const uint32_t to = slot(dst, r.param);
            if (Affine::is_copy(r)) {
                const uint32_t from = slot(r.terms[0].of_target ? dst : src, r.terms[0].param);
                write(side, dst, r.param, to, side.value(*this, from), side.kind(*this, from));
                continue;
            }
            std::vector<std::pair<double, uint32_t>> terms;
            for (const Affine::Term& t : r.terms) {
                const uint32_t from = slot(t.of_target ? dst : src, t.param);
                double k = t.k;
                if (!t.arg.empty()) k *= args.num(t.arg);
                // Not a number: it counts as 0, as `num` has it - a term
                // times 0, kept, so the sum is taken as the run takes it.
                if (!numeric(side.kind(*this, from))) {
                    terms.emplace_back(0.0, sum(0.0, {}));
                    continue;
                }
                terms.emplace_back(k, side.value(*this, from));
            }
            write(side, dst, r.param, to, sum(r.bias, std::move(terms)), Kind::Real);
        }
    }

    // Stages through elements made fresh between them.
    void stages(Side& side, const Stages& st, const Place& src, const Place& dst, const Params& args, bool* reads_target = nullptr) {
        Place from = src;
        for (std::size_t i = 0; i < st.size(); ++i) {
            Place to = i + 1 < st.size() ? fresh() : dst;
            affine(side, st[i], from, to, args, i + 1 == st.size() ? reads_target : nullptr);
            from = to;
        }
    }

    // A functor carried over untouched data, once: what each object writes.
    const FunctorMemo& carried(const Functor& f, const State& from, const State& to, const Params& args) {
        const std::string key = f.name().str() + "|" + std::to_string(f.stamp()) + "|" + f.from().str() + "|" + f.to().str() + "|" +
                                laws::args_str(args);
        auto it = memo_.find(key);
        if (it != memo_.end()) return it->second;
        FunctorMemo m;
        const auto one = [&](const Element& e, const Element& d, const Stages& said) {
            Side scratch;
            Carried c;
            const Place s = live(from, e.id, &e), t = live(to, d.id, &d);
            c.src = s.id;
            c.dst = t.id;
            c.src_id = e.id;
            c.dst_id = d.id;
            stages(scratch, said, s, t, args, &c.reads_target);
            for (uint32_t w : scratch.written)
                if (slots_[w].state == to.id() && slots_[w].element == d.id)
                    c.writes.push_back(Write{w, scratch.values[w], static_cast<Kind>(scratch.kinds[w] - 1)});
            auto a = scratch.added.find(t.id);
            if (a != scratch.added.end()) c.added = a->second;
            m.objects.push_back(std::move(c));
        };
        if (f.is_identity()) {
            if (&from != &to)
                for (const Element& e : from.elements()) {
                    const Element* d = to.find(e.id);
                    if (!d) {
                        m.ok = false;  // it would be made: run it
                        break;
                    }
                    one(e, *d, *transport::copy_all.stages);
                }
        } else {
            f.for_each_declared([&](Key sid, Key did, const Stages* said) {
                if (!m.ok) return;
                const Element* e = from.find(sid);
                if (!e) return;
                const Element* d = to.find(did);
                if (!d || !said) {
                    m.ok = false;  // made, or says nothing: run it
                    return;
                }
                one(*e, *d, *said);
            });
        }
        return memo_.emplace(key, std::move(m)).first->second;
    }

    bool side(const Path& p, Side& s) {
        s.here = p.state();
        s.at = p.element();
        const State* st = g_.find(s.here);
        if (!st || (!s.at.empty() && !st->find(s.at))) return false;
        for (const Step& step : p.steps()) {
            const Params& args = step.args ? *step.args : eq_->args;
            if (step.kind == Step::Kind::Arrow) {
                const State& here = g_.state(s.here);
                const Morphism* m = step.arrow ? step.arrow.get() : here.morphism(step.name);
                if (!m || (!s.at.empty() && dom(*m) != s.at)) return false;
                const Element* src = here.find(dom(*m));
                if (!src || (!m->to.empty() && !here.find(m->to))) return false;
                if (!m->declared && m->handler) return false;  // says nothing: run it
                if (m->declared)
                    for (const DeclaredStep& d : *m->declared) {
                        const Key to_id = d.to.empty() ? d.from : d.to;
                        const Element* a = here.find(d.from);
                        const Element* b = here.find(to_id);
                        if (!a || !b) return false;
                        reads(here, a);
                        reads(here, b);
                        affine(s, d.does, live(here, d.from, a), live(here, to_id, b), args);
                    }
                s.at = cod(*m);
                continue;
            }
            if (step.kind != Step::Kind::Functor) return false;  // a transition, an event: run it
            const Functor* f = step.functor ? step.functor.get() : g_.functor(step.name);
            if (!f || f->from() != s.here || !g_.find(f->to())) return false;
            const State& from = g_.state(s.here);
            const State& to = g_.state(f->to());
            Key image;
            if (!s.at.empty()) {
                image = f->image_object(s.at);
                if (image.empty()) return false;
            }
            if (!(f->is_identity() && &from == &to)) {
                const FunctorMemo& m = carried(*f, from, to, args);
                if (!m.ok) return false;
                for (const Carried& c : m.objects) {
                    // What it carries from untouched data is what it always
                    // does; from data this side touched, worked out again.
                    if (!s.touched(c.src) && !(c.reads_target && s.touched(c.dst))) {
                        for (const Write& w : c.writes) {
                            if (s.kind(*this, w.slot) == Kind::Absent) {
                                auto& a = s.added[c.dst];
                                if (std::find(a.begin(), a.end(), slots_[w.slot].param) == a.end()) a.push_back(slots_[w.slot].param);
                            }
                            if (s.values.size() <= w.slot) s.values.resize(slots_.size(), kNone), s.kinds.resize(slots_.size(), 0);
                            s.values[w.slot] = w.node;
                            s.kinds[w.slot] = static_cast<uint8_t>(static_cast<uint8_t>(w.kind) + 1);
                            s.written.push_back(w.slot);
                        }
                        if (!c.writes.empty()) {
                            if (s.dirty.size() <= c.dst) s.dirty.resize(place_slots_.size(), 0);
                            s.dirty[c.dst] = 1;
                        }
                    } else {
                        const Element* e = from.find(c.src_id);
                        const Element* d = to.find(c.dst_id);
                        if (!e || !d) return false;
                        const Stages* said = nullptr;
                        if (f->is_identity()) said = transport::copy_all.stages.get();
                        else
                            f->for_each_declared([&](Key sid, Key, const Stages* st2) {
                                if (sid == e->id) said = st2;
                            });
                        if (!said) return false;
                        stages(s, *said, live(from, e->id, e), live(to, d->id, d), args);
                    }
                }
                // The data it read: every object's two elements.
                if (f->is_identity()) {
                    for (const Element& e : from.elements()) reads(from, &e), reads(to, to.find(e.id));
                } else {
                    f->for_each_declared([&](Key sid, Key did, const Stages*) {
                        if (const Element* e = from.find(sid)) reads(from, e), reads(to, to.find(did));
                    });
                }
            }
            reads(from, nullptr);
            reads(to, nullptr);
            s.here = f->to();
            s.at = image;
        }
        return true;
    }

    StateGraph& g_;
    uint64_t revision_;
    const Equation* eq_ = nullptr;
    std::vector<SlotInfo> slots_;
    std::vector<uint32_t> leaves_;
    std::vector<Node> nodes_;
    std::unordered_map<NodeKey, uint32_t, NodeHash> sums_;
    std::unordered_map<PlaceKey, uint32_t, PlaceHash> places_;
    std::vector<PlaceSlots> place_slots_;
    std::unordered_map<std::string, FunctorMemo> memo_;
    uint64_t fresh_ = 0;
    // What the equation being compiled reads.
    std::unordered_map<Key, const State*> states_;
    std::unordered_map<const Element*, bool> read_;
    std::vector<const Element*> elements_;
};

}  // namespace detail

// The equation as two programs on the data as it is laid out now - or
// nothing, if any step of it says nothing of what it does, or the compiler
// cannot say for certain what running it would show.
inline std::optional<Compiled> try_compile(StateGraph& g, const Equation& eq) {
    detail::Workspace w(g);
    return w.compile(eq);
}

// Whether what `c` was compiled on is still how the graph is.
inline bool still_holds(const StateGraph& g, const Compiled& c) {
    if (c.revision != g.revision()) return false;
    for (const Compiled::Read& r : c.states)
        if (r.state->structure() != r.structure) return false;
    return detail::layout_of(c.elements) == c.layout;
}

// The values the slots start from, as the data holds them now. What is not a
// number stands as one: a flag 0 or 1, a word its place among the words
// seen (`words`, shared by a batch), nothing a number of its own.
inline std::vector<double> start_of(const Compiled& c, std::unordered_map<std::string, double>& words) {
    std::vector<double> x(c.sources.size(), 0.0);
    for (std::size_t i = 0; i < x.size(); ++i) {
        const Compiled::Source& s = c.sources[i];
        if (!s.element || !s.element->params.has(s.param)) continue;
        const Value& v = s.element->params.get(s.param);
        if (const double* d = std::get_if<double>(&v)) x[i] = *d;
        else if (const int64_t* n = std::get_if<int64_t>(&v)) x[i] = static_cast<double>(*n);
        else if (const bool* b = std::get_if<bool>(&v)) x[i] = *b ? 1.0 : 0.0;
        else if (const std::string* w = std::get_if<std::string>(&v))
            x[i] = words.emplace(*w, static_cast<double>(words.size())).first->second;
        else x[i] = -1.0;  // nothing: the same as itself
    }
    return x;
}

// The laws' accelerator: equations compiled, kept per graph, run in a batch
// on a backend, and every one found broken checked again by the verifier.
class Accelerated : public laws::Accelerator {
public:
    struct Stats {
        std::size_t offered = 0, taken = 0, compiled = 0, reused = 0, refused = 0;
        std::size_t broken = 0;       // found broken by the batch (then checked by the verifier)
        std::size_t unsure = 0;       // too near the tolerance to say (checked by the verifier)
        std::size_t proved = 0;       // held by what they are: the same expressions, nothing to run
        double compile_ms = 0, run_ms = 0, recheck_ms = 0;
    };

    explicit Accelerated(Backend& backend) : backend_(backend) {}

    bool take(StateGraph& g, const Equation& eq) override {
        ++stats_.offered;
        const auto t0 = std::chrono::steady_clock::now();
        auto& kept = cache_[&g];
        const std::string key = eq.law + "|" + eq.where + "|" + eq.lhs.str() + "|" + eq.rhs.str() + "|" + laws::args_str(eq.args);
        auto it = kept.find(key);
        const Compiled* c = nullptr;
        if (it != kept.end() && it->second && still_holds(g, *it->second)) {
            c = it->second.get();
            ++stats_.reused;
        } else if (it != kept.end() && !it->second && refused_at_[&g][key] == g.revision()) {
            ++stats_.refused;  // refused on this very graph before
        } else {
            if (!ws_ || &ws_->graph() != &g || ws_->revision() != g.revision()) ws_ = std::make_unique<detail::Workspace>(g);
            std::optional<Compiled> made = ws_->compile(eq);
            ++stats_.compiled;
            if (made) {
                auto held = std::make_unique<Compiled>(std::move(*made));
                c = held.get();
                kept[key] = std::move(held);
            } else {
                kept[key] = nullptr;
                refused_at_[&g][key] = g.revision();
                ++stats_.refused;
            }
        }
        stats_.compile_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!c) return false;
        ++stats_.taken;
        // Both sides leave every slot the very same expression: it holds,
        // with nothing to run.
        if (c->compare.empty()) {
            ++stats_.proved;
            return true;
        }
        batch_.add(c->lhs, c->rhs, start_of(*c, words_), c->compare);
        taken_.push_back(eq);
        return true;
    }

    std::vector<Violation> finish(StateGraph& g) override {
        std::vector<Violation> out;
        ws_.reset();  // what it shared held for this check only: the data may be laid out anew
        if (batch_.empty()) return out;
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<Verdict> v = backend_.run(batch_);
        stats_.run_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const auto t1 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < taken_.size(); ++i) {
            if (v[i].agree == 1) continue;
            // Parted, or not sure: the verifier runs it, and says.
            ++(v[i].agree == 0 ? stats_.broken : stats_.unsure);
            laws::append(out, laws::check_direct(g, taken_[i]));
        }
        stats_.recheck_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
        batch_.clear();
        taken_.clear();
        words_.clear();
        return out;
    }

    const Stats& stats() const { return stats_; }
    void reset_stats() { stats_ = Stats{}; }
    // Forget what was compiled (for one graph, or all).
    void forget(const StateGraph* g = nullptr) {
        if (g) cache_.erase(g), refused_at_.erase(g);
        else cache_.clear(), refused_at_.clear();
    }

private:
    Backend& backend_;
    std::unique_ptr<detail::Workspace> ws_;
    Batch batch_;
    std::vector<Equation> taken_;
    std::unordered_map<std::string, double> words_;
    std::unordered_map<const StateGraph*, std::unordered_map<std::string, std::unique_ptr<Compiled>>> cache_;
    std::unordered_map<const StateGraph*, std::unordered_map<std::string, uint64_t>> refused_at_;
    Stats stats_;
};

}  // namespace sg::algebra
