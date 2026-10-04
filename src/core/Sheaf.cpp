#include "sg/core/Sheaf.hpp"

namespace sg {

std::vector<std::string> identity_defects(const StateGraph& g, const Functor& f, const std::string& tag, bool whole) {
    std::vector<std::string> out;
    const State* s = g.find(f.from());
    if (!s || f.from() != f.to()) {
        out.push_back(tag + ": composite does not return to its own state");
        return out;
    }

    // Objects: every mapped object must come back to itself.
    for (const auto& e : s->elements()) {
        const Key image = f.image_object(e.id);
        if (image.empty()) continue;  // outside the transition's domain
        if (image != e.id)
            out.push_back(tag + ": " + e.id.str() + " returns as " + image.str());
    }

    // Parameters: and come back carrying the same values.
    State scratch(Key{"descent.scratch." + f.name().str()});
    f.apply(*s, scratch);
    for (const auto& e : s->elements()) {
        const Key image = f.image_object(e.id);
        if (image.empty()) continue;
        const Element* back = scratch.find(image);
        if (!back) {
            if (whole) out.push_back(tag + ": " + e.id.str() + " does not come back");
            continue;
        }
        for (const auto& kv : e.params) {
            if (!back->params.has(kv.first)) {
                if (whole) out.push_back(tag + ": " + e.id.str() + "." + kv.first.str() + " is lost");
                continue;
            }
            const std::string before = to_string(kv.second);
            const std::string after = to_string(back->params.get(kv.first));
            if (before == after) continue;
            // Numbers get a tolerance, since a transition is trigonometry, and
            // headings are compared on the circle they actually live on.
            if (same_number(kv.first, e.params.num(kv.first, 0.0),
                            back->params.num(kv.first, 0.0)))
                continue;
            out.push_back(tag + ": " + e.id.str() + "." + kv.first.str() + " drifts " + before +
                          " -> " + after);
        }
    }
    return out;
}

std::vector<std::string> idempotence_defects(const StateGraph& g, const Functor& round, const std::string& tag) {
    std::vector<std::string> out;
    const State* s = g.find(round.from());
    if (!s || round.from() != round.to()) {
        out.push_back(tag + ": a round trip must return to its own state");
        return out;
    }

    State once(Key{tag + ".once"});
    round.apply(*s, once);
    State twice(Key{tag + ".twice"});
    round.apply(once, twice);

    for (const auto& e : s->elements()) {
        const Key image = round.image_object(e.id);
        if (image.empty()) continue;
        const Element* a = once.find(image);
        const Element* b = twice.find(image);
        if (!a || !b) continue;
        for (const auto& kv : a->params) {
            if (!b->params.has(kv.first)) continue;
            if (to_string(kv.second) == to_string(b->params.get(kv.first))) continue;
            if (same_number(kv.first, a->params.num(kv.first, 0.0),
                            b->params.num(kv.first, 0.0)))
                continue;
            out.push_back(tag + ": " + image.str() + "." + kv.first.str() +
                          " keeps moving - " + to_string(kv.second) + " then " +
                          to_string(b->params.get(kv.first)));
        }
    }
    return out;
}

std::vector<std::string> interface_defects(const StateGraph& g) {
    std::vector<std::string> out;
    for (const Embedding& e : g.embeddings()) {
        if (e.in.empty() || e.out.empty()) continue;  // a one-way window owes nothing
        const Functor* in = g.functor(e.in);
        const Functor* back = g.functor(e.out);
        if (!in || !back) continue;
        const Key subject = e.subject.empty() ? e.host : e.subject;
        if (in->from() != subject || back->to() != subject) continue;  // validate() says so

        // subject -> guest -> subject, which is what opening and closing does.
        for (const auto& d : idempotence_defects(g, Functor::compose(*in, *back),
                                                 "interface " + e.name.str()))
            out.push_back(d);
    }
    return out;
}

std::string Monodromy::str() const {
    std::string s = ring + ": ";
    if (trivial()) return s + "closes";
    s += order ? "comes home after " + std::to_string(order) + " times round"
               : "does not come home within " + std::to_string(kMostOrder) + " times round";
    if (permutes) s += "; the space itself goes round (objects come back as others)";
    if (!moves.empty()) s += "; once round, " + moves.front().substr(moves.front().find(": ") + 2);
    if (moves.size() > 1) s += " (and " + std::to_string(moves.size() - 1) + " more)";
    return s;
}

Overlap& Cover::add(Key name, Key u, Key v, Key u_to_v, Key v_to_u) {
    if (name.empty()) name = Key{u.str() + "^" + v.str()};
    overlaps_.push_back(Overlap{name, u, v, u_to_v, v_to_u});
    Overlap& o = overlaps_.back();
    by_state_[u].push_back(overlaps_.size() - 1);
    by_state_[v].push_back(overlaps_.size() - 1);
    return o;
}

const Functor* Cover::transition(const StateGraph& g, const Overlap& o, Key here) const {
    return g.functor(o.u == here ? o.u_to_v : o.v_to_u);
}

std::vector<std::string> Cover::descent_defects(const StateGraph& g) const {
    std::vector<std::string> out;

    for (const Overlap& o : overlaps_) {
        const Functor* f = g.functor(o.u_to_v);
        const Functor* b = g.functor(o.v_to_u);
        if (!f || !b) {
            out.push_back("overlap " + o.name.str() + ": missing transition functor");
            continue;
        }
        if (f->from() != o.u || f->to() != o.v || b->from() != o.v || b->to() != o.u) {
            out.push_back("overlap " + o.name.str() + ": transitions do not run between " +
                          o.u.str() + " and " + o.v.str());
            continue;
        }
        // Separatedness: across and back, both ways round.
        for (const auto& d : identity_defects(g, Functor::compose(*f, *b),
                                              "overlap " + o.name.str() + " (" + o.u.str() +
                                                  " -> " + o.v.str() + " -> " + o.u.str() + ")"))
            out.push_back(d);
        for (const auto& d : identity_defects(g, Functor::compose(*b, *f),
                                              "overlap " + o.name.str() + " (" + o.v.str() +
                                                  " -> " + o.u.str() + " -> " + o.v.str() + ")"))
            out.push_back(d);
    }

    for (const auto& d : cocycle_defects(g)) out.push_back(d);
    return out;
}

template <typename Fn>
void Cover::each_ring(const StateGraph& g, Fn&& fn) const {
    std::unordered_map<Key, bool> reached;
    for (Key root : states_in_order()) {
        if (reached[root] || !g.find(root)) continue;
        const Tree t = grow(g, root);
        for (const auto& kv : t.to) reached[kv.first] = true;
        // The overlaps that close a loop, and those the cover says wrap: each
        // is one generator of the piece's loops.
        std::vector<std::size_t> rings = t.closing;
        for (std::size_t oi = 0; oi < overlaps_.size(); ++oi)
            if (overlaps_[oi].wraps && t.to.count(overlaps_[oi].u) && t.to.count(overlaps_[oi].v)) rings.push_back(oi);
        for (std::size_t oi : rings) {
            const Overlap& o = overlaps_[oi];
            // (By name: a piece glued to itself has u and v the same.)
            const Functor* across = g.functor(o.u_to_v);
            const Functor* over = g.functor(o.v_to_u);
            if (!across || !over) continue;
            std::string ring;
            for (const Key& p : t.path.at(o.u)) ring += p.str() + " -> ";
            const auto& home = t.path.at(o.v);
            for (auto it = home.rbegin(); it != home.rend(); ++it)
                ring += it->str() + (std::next(it) == home.rend() ? "" : " -> ");
            if (o.wraps) ring += " (by " + o.name.str() + ")";
            // Round it, and round it the other way.
            Functor loop = Functor::compose(Functor::compose(t.to.at(o.u), *across), t.back.at(o.v), Key{"loop." + ring});
            Functor back = Functor::compose(Functor::compose(t.to.at(o.v), *over), t.back.at(o.u), Key{"loop.back." + ring});
            fn(o, ring, loop, back);
        }
    }
}

std::vector<Monodromy> Cover::monodromy(const StateGraph& g) const {
    std::vector<Monodromy> out;
    each_ring(g, [&](const Overlap& o, const std::string& ring, const Functor& loop, const Functor&) {
        Monodromy m;
        m.ring = ring;
        m.wraps = o.wraps;
        m.moves = identity_defects(g, loop, "cycle " + ring);
        loop.for_each_object([&](Key src, Key image) { m.permutes = m.permutes || (!image.empty() && image != src); });
        // Its order: how many times round until every composite is the identity.
        m.order = 0;
        if (m.moves.empty()) {
            m.order = 1;
        } else {
            Functor power = loop;
            for (int n = 2; n <= Monodromy::kMostOrder; ++n) {
                power = Functor::compose(power, loop, Key{"loop." + ring + "^" + std::to_string(n)});
                if (identity_defects(g, power, "power").empty()) {
                    m.order = n;
                    break;
                }
            }
        }
        out.push_back(std::move(m));
    });
    return out;
}

std::vector<std::string> Cover::cocycle_defects(const StateGraph& g) const {
    // A ring is an automorphism when going round it one way and back the other
    // is where you were: then it is the space's shape, however strange. One
    // that is not loses or confuses what goes round it, and glues to nothing.
    std::vector<std::string> out;
    each_ring(g, [&](const Overlap&, const std::string& ring, const Functor& loop, const Functor& back) {
        for (auto& d : identity_defects(g, Functor::compose(loop, back), "cycle " + ring + " and back")) out.push_back(std::move(d));
    });
    return out;
}

std::vector<std::pair<Key, Functor>> Cover::sections(const StateGraph& g, Key root, std::vector<std::string>* seams) const {
    std::vector<std::pair<Key, Functor>> out;
    if (!g.find(root)) return out;
    std::vector<std::string> defects = descent_defects(g);
    // One chart of a space whose rings move what goes round them would have to
    // be in two places at once; a ring the cover says wraps is its shape.
    for (const Monodromy& m : monodromy(g))
        if (!m.wraps && !m.trivial()) defects.push_back("cycle " + m.str());
    if (!defects.empty()) {
        if (seams) *seams = std::move(defects);
        return out;
    }
    const Tree t = grow(g, root);
    for (Key k : t.order) out.emplace_back(k, t.to.at(k));
    return out;
}

auto Cover::grow(const StateGraph& g, Key root) const -> Tree {
    Tree t;
    const State& r = g.state(root);
    t.to.emplace(root, Functor::identity(r, Key{"id." + root.str()}));
    t.back.emplace(root, Functor::identity(r, Key{"id." + root.str()}));
    t.path[root] = {root};
    t.order.push_back(root);
    std::vector<bool> in_tree(overlaps_.size(), false), seen(overlaps_.size(), false);
    for (std::size_t i = 0; i < t.order.size(); ++i) {
        const Key here = t.order[i];
        auto it = by_state_.find(here);
        if (it == by_state_.end()) continue;
        for (std::size_t oi : it->second) {
            if (seen[oi]) continue;
            const Overlap& o = overlaps_[oi];
            if (o.wraps) continue;  // the space's own shape: no tree grows across it
            const Key there = other_side(o, here);
            const Functor* step = transition(g, o, here);
            const Functor* home = transition(g, o, there);
            if (!step || !home) continue;  // descent names a missing transition
            seen[oi] = true;
            if (t.to.count(there)) continue;  // closes a loop: its monodromy
            in_tree[oi] = true;
            t.to.emplace(there, Functor::compose(t.to.at(here), *step,
                                                 Key{root.str() + "->" + there.str()}));
            t.back.emplace(there, Functor::compose(*home, t.back.at(here),
                                                   Key{there.str() + "->" + root.str()}));
            t.path[there] = t.path[here];
            t.path[there].push_back(there);
            t.order.push_back(there);
        }
    }
    for (std::size_t oi = 0; oi < overlaps_.size(); ++oi)
        if (seen[oi] && !in_tree[oi]) t.closing.push_back(oi);
    return t;
}

std::vector<Key> Cover::states_in_order() const {
    std::vector<Key> out;
    std::unordered_map<Key, bool> named;
    for (const Overlap& o : overlaps_)
        for (Key k : {o.u, o.v})
            if (!named[k]) named[k] = true, out.push_back(k);
    return out;
}

}  // namespace sg
