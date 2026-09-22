// Stategine - covers, descent, and gluing.
//
// An atlas of rooms is one instance of a much older operation: you have data
// defined locally, in pieces, and you want one global thing. The pieces are
// states; a `Cover` says which pieces overlap and gives the transition between
// them as a pair of functors. Gluing is then only allowed when the pieces
// actually agree, and "agree" has a precise, checkable meaning:
//
//   separatedness   on each overlap, going across and back is the identity.
//                   A doorway seen from either side is the same doorway.
//
//   cocycle         around any loop of overlaps, the composite is the
//                   identity. Otherwise the loop has holonomy: walk the ring
//                   of rooms and you come back somewhere else, and there is no
//                   global object to glue to, only a seam.
//
// Both conditions are statements that some composite *is the identity*, and
// measuring the gap between a composite and the identity is exactly what
// `Adjunction` already does - its unit and counit defects are the descent
// failures, reported per object and per parameter. So this header adds no new
// notion of correctness; it applies the one already in the engine to the
// arrows of a cover.
//
// What it buys, concretely: you cannot quietly build a space that does not
// close up. `descent_defects` names the seam before anything is drawn.
#pragma once

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/Adjunction.hpp"
#include "sg/core/Functor.hpp"
#include "sg/core/StateGraph.hpp"

namespace sg {

// Two states that share a region, and the transition between them. The two
// functors must be mutually inverse on that region - that is the condition,
// not an assumption.
struct Overlap {
    Key name;
    Key u, v;
    Key u_to_v;  // functor registered in the graph
    Key v_to_u;
};

// How far a composite may stray from the identity before it counts as a seam.
inline std::vector<std::string> identity_defects(const StateGraph& g, const Functor& f,
                                                 const std::string& tag) {
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
        if (!back) continue;
        for (const auto& kv : e.params) {
            if (!back->params.has(kv.first)) continue;
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

// --- the law every interface owes, in every domain -----------------------------
// A view and its write-back form a round trip on the thing being viewed. That
// round trip is almost never the identity - a map that shows metres as cells
// quantises, a summary rounds, a form trims whitespace - and demanding that it
// be the identity would be wrong. What it must be is *idempotent*:
//
//     round . round  ==  round
//
// which says that looking at something and writing it back settles, rather
// than nudging it a little further every time. An interface that fails this
// moves your data simply by being opened and closed, and it does so whatever
// the data is: positions, prices, text. This is the check, and it knows
// nothing about any domain.
inline std::vector<std::string> idempotence_defects(const StateGraph& g, const Functor& round,
                                                    const std::string& tag) {
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

// Does the round trip lose anything at all? Not a defect either way: a lossless
// view is an isomorphism onto its image, a lossy one is a projection. Worth
// being able to ask, and worth not confusing with correctness.
inline bool is_lossless(const StateGraph& g, const Functor& round) {
    return identity_defects(g, round, "round").empty();
}

// Every interface registered in a graph, held to that law. An embedding with
// both directions declared is a view of its subject, whatever either of them
// happens to be about.
inline std::vector<std::string> interface_defects(const StateGraph& g) {
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

class Cover {
public:
    Overlap& add(Key name, Key u, Key v, Key u_to_v, Key v_to_u) {
        if (name.empty()) name = Key{u.str() + "^" + v.str()};
        overlaps_.push_back(Overlap{name, u, v, u_to_v, v_to_u});
        Overlap& o = overlaps_.back();
        by_state_[u].push_back(overlaps_.size() - 1);
        by_state_[v].push_back(overlaps_.size() - 1);
        return o;
    }

    const std::vector<Overlap>& overlaps() const { return overlaps_; }

    // The transition out of `here` along `o`, in the direction that leaves it.
    const Functor* transition(const StateGraph& g, const Overlap& o, Key here) const {
        return g.functor(o.u == here ? o.u_to_v : o.v_to_u);
    }

    Key other_side(const Overlap& o, Key here) const { return o.u == here ? o.v : o.u; }

    // --- descent ----------------------------------------------------------------
    // Everything that stops these pieces gluing into one space.
    std::vector<std::string> descent_defects(const StateGraph& g, int max_cycle = 4) const {
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

        for (const auto& d : cocycle_defects(g, max_cycle)) out.push_back(d);
        return out;
    }

    // Loops that do not close: walk the ring and you arrive somewhere else.
    std::vector<std::string> cocycle_defects(const StateGraph& g, int max_cycle = 4) const {
        std::vector<std::string> out;
        std::vector<Key> path;
        std::vector<std::size_t> used;
        for (const auto& kv : by_state_) {
            const Key start = kv.first;
            path.clear();
            used.clear();
            walk(g, start, start, path, used, max_cycle, out);
        }
        return out;
    }

    // --- gluing -------------------------------------------------------------------
    // The composite transition from `root` to each state it can reach: the
    // change of coordinates that expresses that state's local data in the
    // root's terms. This is the glued section - and it is only well defined
    // because descent holds, which is why the check above exists.
    std::vector<std::pair<Key, Functor>> sections(const StateGraph& g, Key root,
                                                  int max_depth = 4) const {
        std::vector<std::pair<Key, Functor>> out;
        const State* root_state = g.find(root);
        if (!root_state) return out;
        out.emplace_back(root, Functor::identity(*root_state, Key{"id." + root.str()}));

        std::unordered_map<Key, std::size_t> seen{{root, 0}};
        std::vector<std::pair<Key, int>> queue{{root, 0}};
        for (std::size_t i = 0; i < queue.size(); ++i) {
            const Key here = queue[i].first;
            const int depth = queue[i].second;
            if (depth >= max_depth) continue;
            const Functor to_here = out[seen[here]].second;

            auto it = by_state_.find(here);
            if (it == by_state_.end()) continue;
            for (std::size_t oi : it->second) {
                const Overlap& o = overlaps_[oi];
                const Key there = other_side(o, here);
                if (seen.count(there)) continue;
                const Functor* step = transition(g, o, here);
                if (!step) continue;
                seen.emplace(there, out.size());
                // root -> here, then here -> there.
                out.emplace_back(there, Functor::compose(to_here, *step,
                                                         Key{root.str() + "->" + there.str()}));
                queue.emplace_back(there, depth + 1);
            }
        }
        return out;
    }

private:
    void walk(const StateGraph& g, Key start, Key here, std::vector<Key>& path,
              std::vector<std::size_t>& used, int budget,
              std::vector<std::string>& out) const {
        if (budget <= 0) return;
        auto it = by_state_.find(here);
        if (it == by_state_.end()) return;
        for (std::size_t oi : it->second) {
            bool already = false;
            for (std::size_t u : used)
                if (u == oi) already = true;
            if (already) continue;
            const Overlap& o = overlaps_[oi];
            const Key there = other_side(o, here);

            used.push_back(oi);
            path.push_back(here);
            if (there == start && path.size() >= 3) {
                // A closed loop of three or more overlaps: compose it.
                Functor loop = *transition(g, overlaps_[used.front()], start);
                bool ok = true;
                Key cursor = other_side(overlaps_[used.front()], start);
                for (std::size_t k = 1; k < used.size(); ++k) {
                    const Functor* step = transition(g, overlaps_[used[k]], cursor);
                    if (!step) {
                        ok = false;
                        break;
                    }
                    loop = Functor::compose(loop, *step);
                    cursor = other_side(overlaps_[used[k]], cursor);
                }
                if (ok) {
                    std::string ring;
                    for (const Key& p : path) ring += p.str() + " -> ";
                    ring += start.str();
                    loop.rename(Key{"loop." + ring});
                    for (const auto& d : identity_defects(g, loop, "cycle " + ring))
                        out.push_back(d);
                }
            } else if (there != start) {
                walk(g, start, there, path, used, budget - 1, out);
            }
            path.pop_back();
            used.pop_back();
        }
    }

    std::vector<Overlap> overlaps_;
    std::unordered_map<Key, std::vector<std::size_t>> by_state_;
};

}  // namespace sg
