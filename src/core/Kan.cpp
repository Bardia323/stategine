// Stategine - Kan extensions: what they do. What they are is in sg/core/Kan.hpp.
#include "sg/core/Kan.hpp"

namespace sg {
namespace kan {

Options& Options::supply(Key b, Transport t) {
    supplied[b] = Supplied{std::move(t), nullptr};
    return *this;
}

Options& Options::supply(Key b, transport::Declared t) {
    supplied[b] = Supplied{Transport(t), t.stages};
    return *this;
}

const char* Hole::name(Kind k) {
    switch (k) {
        case Kind::Arrow: return "arrow";
        case Kind::Transport: return "transport";
        case Kind::Budget: return "budget";
        case Kind::Unsupported: return "unsupported";
    }
    return "?";
}

std::size_t Result::holes_of(Hole::Kind k) const {
    return static_cast<std::size_t>(
        std::count_if(holes.begin(), holes.end(), [k](const Hole& h) { return h.kind == k; }));
}

std::string Result::str() const {
    std::string s = std::string(side == Side::Left ? "Lan" : "Ran") + " " + name.str() + ": ";
    s += ok() ? "compiled" : !defects.empty() ? (complete ? "cannot exist" : "cannot exist, as far as searched")
                                              : "not resolved";
    for (const auto& e : defects) s += "\n  defect: " + e;
    for (const auto& h : holes) s += "\n  hole: " + h.str();
    return s;
}

namespace {

// A loop that does nothing and is no composite: an identity, the empty word.
bool is_identity(const Morphism& m) { return !m.handler && m.parts.empty() && dom(m) == cod(m); }

std::string str(const Word& w) { return Adjunction::str(w); }

bool starts_with(const Word& w, const Word& prefix) {
    return prefix.size() <= w.size() && std::equal(prefix.begin(), prefix.end(), w.begin());
}

Word cat(Word x, const Word& y) {
    x.insert(x.end(), y.begin(), y.end());
    return x;
}

// A state read as the category its arrows generate - or its opposite, whose
// words are the same arrows read backwards (a right extension is a left one
// with every arrow turned round).
class View {
public:
    View(const State& s, bool op) : s_(&s), op_(op) {
        for (const Morphism& m : s.morphisms()) {
            if (!m.parts.empty() || is_identity(m)) continue;
            out_[src(m)].push_back({m.name, tgt(m)});
            in_[tgt(m)].push_back(src(m));
        }
    }

    const State& state() const { return *s_; }
    bool op() const { return op_; }
    Key src(const Morphism& m) const { return op_ ? cod(m) : dom(m); }
    Key tgt(const Morphism& m) const { return op_ ? dom(m) : cod(m); }

    // An arrow as the word it is, read the way this view reads.
    Word word(Key arrow) const {
        std::string why;
        Word w = unfold_path(*s_, {arrow},
                             [this](Key k) {
                                 const Morphism* m = s_->morphism(k);
                                 return m && is_identity(*m);
                             },
                             why);
        if (op_) std::reverse(w.begin(), w.end());
        return w;
    }

    // Written as the state writes it.
    Word plain(Word w) const {
        if (op_) std::reverse(w.begin(), w.end());
        return w;
    }

    // Every word from x to y of at most n arrows; `cut` if a longer one may be.
    const std::vector<Word>& paths(Key x, Key y, std::size_t n, bool& cut) const {
        Hom& h = homs_[{x, y}];
        if (!h.built) {
            h.built = true;
            const std::unordered_set<Key>& to_y = reaching(y);
            if (to_y.count(x)) {
                Word w;
                walk(x, y, n, to_y, w, h);
            }
        }
        if (h.cut) cut = true;
        return h.words;
    }

    // Everything reached from `from` (itself included).
    std::unordered_set<Key> reached(const std::vector<Key>& from) const {
        std::unordered_set<Key> seen(from.begin(), from.end());
        std::vector<Key> todo(from.begin(), from.end());
        while (!todo.empty()) {
            const Key at = todo.back();
            todo.pop_back();
            auto it = out_.find(at);
            if (it == out_.end()) continue;
            for (const auto& e : it->second)
                if (seen.insert(e.second).second) todo.push_back(e.second);
        }
        return seen;
    }

    // The arrow the state names for word w from x to y: a generator, a
    // composite of them, or - for the empty word - an identity loop at x.
    Key name_for(Key x, Key y, const Word& w) const {
        for (const Morphism& m : s_->morphisms())
            if (src(m) == x && tgt(m) == y && word(m.name) == w) return m.name;
        return Key{};
    }

private:
    struct Hom {
        bool built = false, cut = false;
        std::vector<Word> words;
    };

    void walk(Key at, Key y, std::size_t n, const std::unordered_set<Key>& to_y, Word& w, Hom& h) const {
        if (at == y) h.words.push_back(w);
        auto it = out_.find(at);
        if (it == out_.end()) return;
        for (const auto& e : it->second) {
            if (!to_y.count(e.second)) continue;
            if (w.size() >= n) {
                h.cut = true;
                return;
            }
            w.push_back(e.first);
            walk(e.second, y, n, to_y, w, h);
            w.pop_back();
        }
    }

    // Everything with a way to y (y included).
    const std::unordered_set<Key>& reaching(Key y) const {
        auto found = reaching_.find(y);
        if (found != reaching_.end()) return found->second;
        std::unordered_set<Key> seen{y};
        std::vector<Key> todo{y};
        while (!todo.empty()) {
            const Key at = todo.back();
            todo.pop_back();
            auto it = in_.find(at);
            if (it == in_.end()) continue;
            for (Key k : it->second)
                if (seen.insert(k).second) todo.push_back(k);
        }
        return reaching_.emplace(y, std::move(seen)).first->second;
    }

    const State* s_;
    bool op_;
    std::unordered_map<Key, std::vector<std::pair<Key, Key>>> out_;  // generators out: name, target
    std::unordered_map<Key, std::vector<Key>> in_;
    mutable std::map<std::pair<Key, Key>, Hom> homs_;
    mutable std::unordered_map<Key, std::unordered_set<Key>> reaching_;
};

std::vector<uint64_t> version(const Functor& k, const Functor& f, const State& a, const State& b,
                                     const State& c) {
    const std::hash<Key> h;
    return {h(k.name()), h(f.name()), k.stamp(), f.stamp(), a.structure(), b.structure(), c.structure()};
}

// The image of an arrow of A under a functor, as a word of the view it lands
// in; false where the functor does not carry it.
bool image_word(const Functor& f, const View& v, Key arrow, Word& out) {
    const Key img = f.is_identity() ? arrow : f.image_morphism(arrow);
    if (img.empty() || !v.state().morphism(img)) return false;
    out = v.word(img);
    return true;
}

// What carries an object across, as the functor it is made for takes it.
struct Carry {
    Transport run;
    std::shared_ptr<const Stages> declared;
};

// The data at b = K(a) made into F(a)'s: F . K^-1 at a, from declared
// stages only. K's transport at a is undone only where its stages prove it
// invertible on what F then reads:
//   it copies a whole       its inverse is the identity;
//   it renames              each parameter it sets copied from one of a's,
//                           no two sources and no two targets alike: undone
//                           by the renaming turned round, on the parameters
//                           it carries - so F may read only those.
// A rename with a collision, a parameter F reads that K drops, arithmetic, a
// copy of everything with renames on top: not provably invertible, a
// Transport hole. A transport that says nothing of what it does (a function,
// opaque) cannot be composed from declared stages at all: Unsupported.
// F the identity is F(a) = a's data as it is: only K carrying a whole makes
// b's data literally a's, and then the transport is the identity; after a
// renaming, a's other parameters are not there to be the identity of.
bool derive(const Functor& K, const Functor& F, Key a, Carry& out, Hole::Kind& kind, std::string& why) {
    // K's inverse at a: none (whole), or a renaming turned round.
    bool whole = K.is_identity();
    std::optional<Affine> back;
    std::unordered_set<Key> carried;
    if (!whole) {
        const std::shared_ptr<const Stages> k = K.declared_of(a);
        if (!k) {
            kind = Hole::Kind::Unsupported;
            why = K.name().str() + "'s transport at " + a.str() + " is opaque: no declared stages to invert";
            return false;
        }
        if (k->size() == 1 && (*k)[0].copy_all && (*k)[0].rows.empty()) {
            whole = true;
        } else {
            kind = Hole::Kind::Transport;
            if (k->size() != 1 || (*k)[0].copy_all || (*k)[0].rows.empty()) {
                why = K.name().str() + "'s transport at " + a.str() + " is not a whole copy or a pure renaming: " +
                      "not provably invertible";
                return false;
            }
            std::unordered_set<Key> targets;
            Affine inv;
            for (const Affine::Row& r : (*k)[0].rows) {
                if (!Affine::is_copy(r) || r.terms[0].of_target) {
                    why = K.name().str() + " sets " + r.param.str() + " of " + a.str() +
                          " by arithmetic, not a copy: not provably invertible";
                    return false;
                }
                if (!carried.insert(r.terms[0].param).second || !targets.insert(r.param).second) {
                    why = K.name().str() + "'s renaming at " + a.str() + " is not one to one (" +
                          r.terms[0].param.str() + " -> " + r.param.str() + " collides): no inverse";
                    return false;
                }
                inv.copy(r.terms[0].param, r.param);
            }
            back = std::move(inv);
        }
    }
    if (F.is_identity()) {
        if (whole) {  // b's data is a's, and F(a) is a: the same representation
            out = Carry{Transport(transport::copy_all), transport::copy_all.stages};
            return true;
        }
        kind = Hole::Kind::Transport;
        why = F.name().str() + " is the identity, so " + a.str() + "'s every parameter is its data; " +
              K.name().str() + " carries only some";
        return false;
    }
    const std::shared_ptr<const Stages> f = F.declared_of(a);
    if (!f) {
        kind = Hole::Kind::Unsupported;
        why = F.name().str() + "'s transport at " + a.str() + " is opaque: no declared stages to compose";
        return false;
    }
    if (whole) {
        out = Carry{Transport(transport::Declared{f}), f};
        return true;
    }
    // F must read only what K carried: its first stage reads a.
    kind = Hole::Kind::Transport;
    const Affine& first = f->front();
    if (first.copy_all) {
        why = F.name().str() + " reads every parameter of " + a.str() + ", and " + K.name().str() +
              " drops all but its renamed ones";
        return false;
    }
    for (const Affine::Row& r : first.rows)
        for (const Affine::Term& t : r.terms)
            if (!t.of_target && !carried.count(t.param)) {
                why = F.name().str() + " reads " + t.param.str() + " of " + a.str() + ", which " + K.name().str() +
                      " drops: nothing to undo it from";
                return false;
            }
    auto stages = std::make_shared<Stages>();
    stages->push_back(std::move(*back));
    stages->insert(stages->end(), f->begin(), f->end());
    std::shared_ptr<const Stages> both = std::move(stages);
    out = Carry{Transport(transport::Declared{both}), both};
    return true;
}

// One object b's (co)limit: the comma category over it, and the apex found.
struct Point {
    struct Obj {
        Key a;
        Word g;   // K(a) -> b, a word of B
        Key fa;   // F(a)
    };
    std::vector<Obj> objs;
    std::map<std::pair<Key, Word>, std::size_t> index;
    Key apex;
    std::vector<Word> legs;  // F(a) -> apex, a word of C, per object
};

}  // namespace

namespace detail {

Result compile(Side side, const StateGraph& g, Key k_name, Key f_name, Key name, const Options& o) {
    Result r;
    r.side = side;
    r.name = name;
    r.along = k_name;
    r.extends = f_name;
    const Functor* K = g.functor(k_name);
    const Functor* F = g.functor(f_name);
    if (!K || !F) {
        r.defects.push_back(std::string("no functor ") + (!K ? k_name.str() : f_name.str()) + " in the graph");
        r.complete = true;
        return r;
    }
    if (K->from() != F->from()) {
        r.defects.push_back(k_name.str() + " starts at " + K->from().str() + ", " + f_name.str() + " at " +
                            F->from().str());
        r.complete = true;
        return r;
    }
    const State* A = g.find(K->from());
    const State* B = g.find(K->to());
    const State* C = g.find(F->to());
    if (!A || !B || !C) {
        r.defects.push_back("a state it joins is not in the graph");
        r.complete = true;
        return r;
    }
    r.from = B->id();
    r.to = C->id();
    r.read = version(*K, *F, *A, *B, *C);
    // Not functors: nothing to extend.
    for (auto& e : K->check_laws(*A, *B)) r.defects.push_back(e);
    for (auto& e : F->check_laws(*A, *C)) r.defects.push_back(e);
    if (!r.defects.empty()) {
        r.complete = true;
        return r;
    }

    // Along an inclusion: one to one, on objects and on arrows.
    if (!K->is_identity()) {
        std::unordered_map<Key, Key> seen;
        K->for_each_object([&](Key a, Key b) {
            auto in = seen.emplace(b, a);
            if (!in.second)
                r.holes.push_back({Hole::Kind::Unsupported, b,
                                   k_name.str() + " is not an inclusion: " + in.first->second.str() + " and " +
                                       a.str() + " both go to " + b.str()});
        });
        seen.clear();
        K->for_each_morphism([&](Key a, Key b) {
            auto in = seen.emplace(b, a);
            if (!in.second)
                r.holes.push_back({Hole::Kind::Unsupported, b,
                                   k_name.str() + " is not an inclusion: arrows " + in.first->second.str() +
                                       " and " + a.str() + " both go to " + b.str()});
        });
    }
    if (!r.holes.empty()) return r;  // not complete: nothing was searched

    const bool op = side == Side::Right;
    const View va(*A, op), vb(*B, op), vc(*C, op);
    const std::string colimit = op ? "limit" : "colimit";
    const std::size_t n = o.max_path;

    // The diagram: the objects of A both functors carry, and the arrows
    // between them both carry.
    struct Arrow {
        Key s, t;
        Word k, f;  // K(h) in B, F(h) in C
    };
    std::vector<Key> as;
    std::vector<Key> images;
    for (const auto& e : A->elements()) {
        const Key ka = K->image_object(e.id), fa = F->image_object(e.id);
        if (ka.empty() || fa.empty()) continue;
        as.push_back(e.id);
        images.push_back(ka);
    }
    std::vector<Arrow> hs;
    for (const Morphism& m : A->morphisms()) {
        if (!m.parts.empty() || is_identity(m)) continue;
        Arrow h{va.src(m), va.tgt(m), {}, {}};
        if (!image_word(*K, vb, m.name, h.k) || !image_word(*F, vc, m.name, h.f)) continue;
        if (F->image_object(dom(m)).empty() || F->image_object(cod(m)).empty()) continue;
        hs.push_back(std::move(h));
    }

    // Each object of B that K's image forces.
    const std::unordered_set<Key> forced = vb.reached(images);
    std::unordered_map<Key, Point> points;
    for (const auto& be : B->elements()) {
        const Key b = be.id;
        if (!forced.count(b)) continue;
        bool cut = false;
        Point p;
        for (Key a : as)
            for (const Word& w : vb.paths(K->image_object(a), b, n, cut)) {
                p.index.emplace(std::make_pair(a, w), p.objs.size());
                p.objs.push_back({a, w, F->image_object(a)});
            }
        // What the cocone must respect: each arrow h of A between them.
        struct Tie {
            std::size_t i, j;  // leg i == F(h) ; leg j
            Word f;
        };
        std::vector<Tie> ties;
        for (const Arrow& h : hs)
            for (std::size_t j = 0; j < p.objs.size(); ++j) {
                if (p.objs[j].a != h.t) continue;
                const Word from = cat(h.k, p.objs[j].g);
                auto it = p.index.find({h.s, from});
                if (it != p.index.end()) ties.push_back({it->second, j, h.f});
                else if (from.size() > n) cut = true;
            }

        // Every cocone on C's own elements, the ties kept.
        struct Cone {
            Key apex;
            std::vector<Word> legs;
        };
        std::vector<Cone> cones;
        bool out_of_budget = false;
        for (const auto& xe : C->elements()) {
            const Key x = xe.id;
            std::vector<const std::vector<Word>*> options;
            bool none = false;
            for (const auto& ob : p.objs) {
                options.push_back(&vc.paths(ob.fa, x, n, cut));
                if (options.back()->empty()) none = true;
            }
            if (none) continue;
            std::vector<Word> legs(p.objs.size());
            std::function<void(std::size_t)> choose = [&](std::size_t i) {
                if (out_of_budget) return;
                if (i == p.objs.size()) {
                    if (cones.size() >= o.max_cones) {
                        out_of_budget = true;
                        return;
                    }
                    cones.push_back({x, legs});
                    return;
                }
                for (const Word& w : *options[i]) {
                    legs[i] = w;
                    bool kept = true;
                    for (const Tie& t : ties) {
                        if (std::max(t.i, t.j) != i) continue;
                        if (legs[t.i] != cat(t.f, legs[t.j])) {
                            kept = false;
                            break;
                        }
                    }
                    if (kept) choose(i + 1);
                }
            };
            choose(0);
        }
        if (out_of_budget) cut = true;

        // The one every other passes through, by exactly one arrow.
        const Cone* found = nullptr;
        for (const Cone& cand : cones) {
            bool universal = true;
            if (p.objs.empty()) {
                for (const auto& xe : C->elements())
                    if (vc.paths(cand.apex, xe.id, n, cut).size() != 1) universal = false;
            } else {
                for (const Cone& mu : cones) {
                    if (!starts_with(mu.legs[0], cand.legs[0])) {
                        universal = false;
                        break;
                    }
                    const Word u(mu.legs[0].begin() + static_cast<std::ptrdiff_t>(cand.legs[0].size()),
                                 mu.legs[0].end());
                    for (std::size_t i = 1; i < p.objs.size() && universal; ++i)
                        universal = mu.legs[i] == cat(cand.legs[i], u);
                    if (!universal) break;
                }
            }
            if (universal) {
                found = &cand;
                break;
            }
        }
        // Cut short, neither a (co)limit found nor one missing is proven:
        // past the budget may lie a cone that does not pass through it, or
        // the one that is universal.
        if (cut)
            r.holes.push_back({Hole::Kind::Budget, b,
                               "words past " + std::to_string(n) + " arrows" +
                                   (out_of_budget ? " and cones past " + std::to_string(o.max_cones) : "") +
                                   " not looked at"});
        if (!found) {
            if (cut) continue;
            // Searched through and through, and none: the pointwise extension
            // cannot exist, whatever else was or was not found. That is the
            // whole answer - searches cut elsewhere are not needed for it.
            r.defects.push_back(b.str() + ": no " + colimit + " in " + C->id().str() + " of " +
                                    std::to_string(p.objs.size()) + " object(s) of " + f_name.str() + " over " +
                                    (op ? b.str() + " -> " + k_name.str() : k_name.str() + " -> " + b.str()) +
                                    (cones.empty() ? " (no cone at all)" : ""));
            r.holes.clear();
            r.objects.clear();
            r.complete = true;
            return r;
        }
        p.apex = found->apex;
        p.legs = found->legs;
        r.objects[b] = p.apex;
        points.emplace(b, std::move(p));
    }

    // The 2-cell: the leg at (a, id).
    for (Key a : as) {
        auto pt = points.find(K->image_object(a));
        if (pt == points.end()) continue;
        auto it = pt->second.index.find({a, Word{}});
        if (it != pt->second.index.end()) r.cells[a] = vc.plain(pt->second.legs[it->second]);
    }

    // Arrows: each goes where the universal property sends it.
    for (const Morphism& m : B->morphisms()) {
        auto ps = points.find(vb.src(m));
        auto pt = points.find(vb.tgt(m));
        if (ps == points.end() || pt == points.end()) continue;
        const Point& s = ps->second;
        const Point& t = pt->second;
        if (is_identity(m)) {
            const Key id = vc.name_for(s.apex, s.apex, {});
            if (!id.empty()) r.arrows[m.name] = id;
            else
                r.holes.push_back({Hole::Kind::Arrow, m.name,
                                   "an identity, and " + C->id().str() + " names none at " + s.apex.str() +
                                       ": give it an identity loop"});
            continue;
        }
        const Word w = vb.word(m.name);
        std::optional<Word> u;
        bool cut = false, apart = false;
        if (s.objs.empty()) {
            const auto& only = vc.paths(s.apex, t.apex, n, cut);
            if (only.size() == 1) u = only[0];
        }
        for (std::size_t i = 0; i < s.objs.size(); ++i) {
            const Word to = cat(s.objs[i].g, w);
            auto it = t.index.find({s.objs[i].a, to});
            if (it == t.index.end()) {
                if (to.size() > n) cut = true;
                continue;
            }
            const Word& lt = t.legs[it->second];
            if (!starts_with(lt, s.legs[i])) {
                apart = true;
                break;
            }
            const Word rest(lt.begin() + static_cast<std::ptrdiff_t>(s.legs[i].size()), lt.end());
            if (u && *u != rest) {
                apart = true;
                break;
            }
            u = rest;
        }
        const Key from = op ? t.apex : s.apex, to = op ? s.apex : t.apex;
        if (apart) {
            r.holes.push_back({Hole::Kind::Arrow, m.name,
                               "no one arrow " + from.str() + " -> " + to.str() + " of " + C->id().str() +
                                   " agrees with every leg"});
            continue;
        }
        if (!u) {
            r.holes.push_back(cut ? Hole{Hole::Kind::Budget, m.name,
                                         "its image lies past " + std::to_string(n) + " arrows"}
                                  : Hole{Hole::Kind::Arrow, m.name, "no leg says where it goes"});
            continue;
        }
        const Key img = vc.name_for(s.apex, t.apex, *u);
        if (img.empty()) {
            r.holes.push_back({Hole::Kind::Arrow, m.name,
                               "goes to " + str(vc.plain(*u)) + " (" + from.str() + " -> " + to.str() + "), which " +
                                   C->id().str() + " does not name: " +
                                   (u->empty() ? "give " + from.str() + " an identity loop" : "compose it")});
            continue;
        }
        r.arrows[m.name] = img;
    }

    // Data: supplied, or composed from what is declared - never made up.
    std::unordered_map<Key, Carry> carry;
    for (const auto& be : B->elements()) {
        const Key b = be.id;
        auto pt = points.find(b);
        if (pt == points.end()) continue;
        auto given = o.supplied.find(b);
        if (given != o.supplied.end()) {
            if (!given->second.run && !given->second.declared) {
                r.holes.push_back({Hole::Kind::Transport, b, "the transport supplied is empty"});
                continue;
            }
            carry[b] = Carry{given->second.run, given->second.declared};
            continue;
        }
        Key a;
        for (Key x : as)
            if (K->image_object(x) == b) a = x;
        auto id_leg = a.empty() ? pt->second.index.end() : pt->second.index.find({a, Word{}});
        const bool at_a = !a.empty() && id_leg != pt->second.index.end() && pt->second.legs[id_leg->second].empty();
        std::string why;
        Hole::Kind kind = Hole::Kind::Transport;
        if (at_a && derive(*K, *F, a, carry[b], kind, why)) continue;
        carry.erase(b);
        r.holes.push_back({kind, b,
                           "to " + pt->second.apex.str() + ": " +
                               (at_a ? why
                                     : std::string("its data is no object's of ") + A->id().str() +
                                           " carried as it is, so no declared transport composes into it") +
                               " - supply one (kan::Options::supply)"});
    }

    r.complete = r.holes_of(Hole::Kind::Budget) == 0;
    if (!r.defects.empty() || !r.holes.empty()) return r;
    Functor out(name, B->id(), C->id());
    for (const auto& be : B->elements()) {
        auto obj = r.objects.find(be.id);
        if (obj == r.objects.end()) continue;
        const Carry& c = carry.at(be.id);
        if (c.declared) out.on_object(be.id, obj->second, transport::Declared{c.declared});
        else out.on_object(be.id, obj->second, c.run);
    }
    for (const Morphism& m : B->morphisms()) {
        auto it = r.arrows.find(m.name);
        if (it != r.arrows.end()) out.on_morphism(m.name, it->second);
    }
    r.functor = std::move(out);
    return r;
}

}  // namespace detail

bool Result::current(const StateGraph& g) const {
    const Functor* k = g.functor(along);
    const Functor* f = g.functor(extends);
    if (!k || !f) return false;
    const State* a = g.find(k->from());
    const State* b = g.find(k->to());
    const State* c = g.find(f->to());
    return a && b && c && read == version(*k, *f, *a, *b, *c);
}

}  // namespace kan
}  // namespace sg
