// Stategine - what passes through a seam: view, light, sound, things
// (Channel, StateGraph::admits / passes / fade / connected).
#include <algorithm>
#include <functional>
#include <string>

#include "sg/core/StateGraph.hpp"

namespace sg {

namespace {

const Key kAdmits{"admits"}, kFade{"fade"}, kOpening{"opening"}, kMuffle{"muffle"};
const Key kFeed{"feed"}, kBallOut{"ball_out"}, kWalk{"walk"};

// How much of a shut door's sound is still heard through it.
constexpr double kMuffled = 0.25;

// A seam's doorway on one side: the first of its boundary there.
const Element* doorway(const StateGraph& g, Key state, const std::vector<Key>& boundary) {
    if (boundary.empty()) return nullptr;
    const State* s = g.find(state);
    return s ? s->find(boundary.front()) : nullptr;
}

bool names(const std::string& words, const char* name) {
    return (" " + words + " ").find(std::string(" ") + name + " ") != std::string::npos;
}

// What one doorway lets through: what it says, or what it is - a picture
// (a screen, a painting) only what is seen in it; the sky of a world in a
// glass, flown out of, all but its sound; any other doorway everything.
bool side_admits(const Element* e, Channel c) {
    if (!e) return true;
    if (e->params.has(kAdmits)) return names(e->params.get_or<std::string>(kAdmits, ""), channel_name(c));
    if (e->params.num(kFeed, 0.0) > 0.5 && e->params.num(kWalk, 0.0) < 0.5) return c == Channel::View;
    if (e->params.num(kBallOut, 0.0) > 0.5) return c != Channel::Sound;
    return true;
}

// The least either doorway says of `key`, or `none` if neither says.
double least(const Element* a, const Element* b, Key key, double none) {
    double v = none;
    bool said = false;
    for (const Element* e : {a, b})
        if (e && e->params.has(key)) v = said ? std::min(v, e->params.num(key)) : e->params.num(key), said = true;
    return v;
}

// A piece of what a channel joins: union by a parent per state.
Key root_of(std::unordered_map<Key, Key>& parent, Key k) {
    Key r = k;
    for (auto it = parent.find(r); it != parent.end() && it->second != r; it = parent.find(r)) r = it->second;
    // Every state passed on the way points at the root now.
    for (auto it = parent.find(k); it != parent.end() && it->second != r;) {
        const Key next = it->second;
        it->second = r;
        it = parent.find(next);
    }
    return r;
}

}  // namespace

const char* channel_name(Channel c) {
    switch (c) {
        case Channel::View: return "view";
        case Channel::Light: return "light";
        case Channel::Sound: return "sound";
        case Channel::Objects: return "objects";
    }
    return "";
}

bool StateGraph::admits(const Seam& s, Channel c) const {
    return side_admits(doorway(*this, s.a, s.boundary_a), c) && side_admits(doorway(*this, s.b, s.boundary_b), c);
}

double StateGraph::passes(const Seam& s, Channel c) const {
    const Element* a = doorway(*this, s.a, s.boundary_a);
    const Element* b = doorway(*this, s.b, s.boundary_b);
    if (!side_admits(a, c) || !side_admits(b, c)) return 0.0;
    const double open = std::clamp(least(a, b, kOpening, 1.0), 0.0, 1.0);
    switch (c) {
        case Channel::Light: return open;
        case Channel::Sound: {
            const double shut = std::clamp(least(a, b, kMuffle, kMuffled), 0.0, 1.0);
            return shut + (1.0 - shut) * open;
        }
        default: return 1.0;
    }
}

double StateGraph::fade(const Seam& s) const {
    double v = 1.0;
    bool said = false;
    for (const Element* e : {doorway(*this, s.a, s.boundary_a), doorway(*this, s.b, s.boundary_b)})
        if (e && e->params.has(kFade)) v = said ? std::max(v, e->params.num(kFade)) : e->params.num(kFade), said = true;
    return std::max(0.0, v);
}

void StateGraph::refresh_channels() const {
    // What decides the pieces: the seams (the revision counts them) and what
    // their doorways say (each doorway's stamp). Nothing moved, nothing is
    // looked at again.
    uint64_t seen = mix_stamp(0x6a09e667f3bcc909ull, rev_.all.load());
    for (const Seam& s : seams_)
        for (const Element* e : {doorway(*this, s.a, s.boundary_a), doorway(*this, s.b, s.boundary_b)})
            seen = mix_stamp(seen, e ? e->params.stamp() : 0);
    if (seen == channels_seen_) return;
    channels_seen_ = seen;
    // Which states each seam joins, in order: a seam glued again under its
    // name to other rooms (a corridor lent) leaves which seams pass as they
    // were, but not what they join.
    uint64_t ends = 0x3c6ef372fe94f82bull;
    for (const Seam& s : seams_) ends = mix_stamp(mix_stamp(ends, std::hash<Key>{}(s.a)), std::hash<Key>{}(s.b));
    for (int i = 0; i < kChannels; ++i) {
        const Channel c = static_cast<Channel>(i);
        ChannelPieces& pieces = channels_[static_cast<std::size_t>(i)];
        std::vector<char> passing;
        passing.reserve(seams_.size());
        for (const Seam& s : seams_) passing.push_back(passes(s, c) > 0.0 ? 1 : 0);
        // Only a channel whose seams now pass otherwise, or join other
        // states, is joined again.
        if (pieces.made && pieces.ends == ends && passing == pieces.passing) continue;
        std::unordered_map<Key, Key> parent;
        std::size_t n = 0;
        for (const Seam& s : seams_) {
            if (!passing[n++] || s.a == s.b) continue;
            parent.emplace(s.a, s.a);
            parent.emplace(s.b, s.b);
            const Key ra = root_of(parent, s.a), rb = root_of(parent, s.b);
            if (ra != rb) parent[rb] = ra;
        }
        pieces.root.clear();
        for (const auto& kv : parent) pieces.root[kv.first] = root_of(parent, kv.first);
        pieces.passing = std::move(passing);
        pieces.ends = ends;
        pieces.made = true;
    }
}

const std::unordered_map<Key, Key>& StateGraph::pieces(Channel c) const {
    refresh_channels();
    return channels_[static_cast<std::size_t>(c)].root;
}

Key StateGraph::component(Channel c, Key state) const {
    const auto& root = pieces(c);
    const auto it = root.find(state);
    return it == root.end() ? state : it->second;
}

bool StateGraph::connected(Channel c, Key a, Key b) const {
    return a == b || component(c, a) == component(c, b);
}

}  // namespace sg
