#include "sg/algebra/Compile.hpp"

namespace sg::algebra {

Kind kind_of(const Value& v) {
    switch (v.index()) {
        case 0: return Kind::None;
        case 1: return Kind::Flag;
        case 2: return Kind::Int;
        case 3: return Kind::Real;
        default: return Kind::Word;
    }
}

}  // namespace sg::algebra

namespace sg::algebra::detail {

uint64_t layout_of(const std::vector<const Element*>& es) {
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

std::optional<Compiled> Workspace::compile(const Equation& eq) {
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

std::size_t Workspace::NodeHash::operator()(const NodeKey& k) const {
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

std::size_t Workspace::PlaceHash::operator()(const PlaceKey& k) const {
    return static_cast<std::size_t>(mix(mix(1469598103934665603ULL, std::hash<Key>{}(k.state)), std::hash<Key>{}(k.element)));
}

Kind Workspace::Side::kind(const Workspace& w, uint32_t i) const {
    return i < kinds.size() && kinds[i] ? static_cast<Kind>(kinds[i] - 1) : w.slots_[i].start;
}

uint32_t Workspace::Side::value(Workspace& w, uint32_t i) const {
    return i < values.size() && values[i] != kNone ? values[i] : w.leaf(i);
}

uint32_t Workspace::place(Key state, Key element, const Element* live) {
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

auto Workspace::fresh() -> Place {
    const Key element{"~" + std::to_string(fresh_++)};
    return Place{Key{"~"}, element, nullptr, place(Key{"~"}, element, nullptr)};
}

uint32_t Workspace::slot(const Place& p, Key param) {
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

uint32_t Workspace::leaf(uint32_t slot) {
    if (leaves_[slot] != kNone) return leaves_[slot];
    Node n;
    n.leaf = true;
    n.slot = slot;
    nodes_.push_back(n);
    return leaves_[slot] = static_cast<uint32_t>(nodes_.size() - 1);
}

uint32_t Workspace::sum(double bias, std::vector<std::pair<double, uint32_t>> terms) {
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

void Workspace::reads(const State& s, const Element* e) {
    states_.emplace(s.id(), &s);
    if (e && read_.emplace(e, true).second) elements_.push_back(e);
}

std::vector<Key> Workspace::params_of(const Side& side, const Place& p) {
    std::vector<Key> out;
    for (Key k : place_slots_[p.id].start_params)
        if (side.kind(*this, slot(p, k)) != Kind::Absent) out.push_back(k);
    auto it = side.added.find(p.id);
    if (it != side.added.end())
        for (Key k : it->second)
            if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
    return out;
}

void Workspace::write(Side& side, const Place& dst, Key param, uint32_t to, uint32_t node, Kind k) {
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

void Workspace::affine(Side& side, const Affine& a, const Place& src, const Place& dst, const Params& args, bool* reads_target) {
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

void Workspace::stages(Side& side, const Stages& st, const Place& src, const Place& dst, const Params& args, bool* reads_target) {
    Place from = src;
    for (std::size_t i = 0; i < st.size(); ++i) {
        Place to = i + 1 < st.size() ? fresh() : dst;
        affine(side, st[i], from, to, args, i + 1 == st.size() ? reads_target : nullptr);
        from = to;
    }
}

auto Workspace::carried(const Functor& f, const State& from, const State& to, const Params& args) -> const FunctorMemo& {
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

bool Workspace::side(const Path& p, Side& s) {
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

}  // namespace sg::algebra::detail

namespace sg::algebra {

bool still_holds(const StateGraph& g, const Compiled& c) {
    if (c.revision != g.revision()) return false;
    for (const Compiled::Read& r : c.states)
        if (r.state->structure() != r.structure) return false;
    return detail::layout_of(c.elements) == c.layout;
}

std::vector<double> start_of(const Compiled& c, std::unordered_map<std::string, double>& words) {
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

bool Accelerated::take(StateGraph& g, const Equation& eq) {
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

std::vector<Violation> Accelerated::finish(StateGraph& g) {
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

void Accelerated::forget(const StateGraph* g) {
    if (g) cache_.erase(g), refused_at_.erase(g);
    else cache_.clear(), refused_at_.clear();
}

}  // namespace sg::algebra
