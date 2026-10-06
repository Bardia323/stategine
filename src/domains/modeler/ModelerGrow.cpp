// Growing: a plant from an L-system. Its rules are rewritten from the axiom
// n times, and the word they make is read by a turtle - every F a step of
// wood, every bracket a branch, the turns between them its angles. What the
// turtle walked is a tree of nodes; each node's thickness is the pipe model's
// (Leonardo's rule: a branch is as thick as the twigs it carries, together),
// so the trunk is as thick as everything it holds up and the twigs thinnest.
// Twigs thinner than `min` are pruned, nearly straight runs made one span;
// each branch is then a tube round its own path, tapering node to node, and
// every tip carries leaves. What is grown is a function of the rules and the
// seed alone.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

#include "ModelerKernel.hpp"

namespace sg::sculpt::kernel {

namespace {

constexpr double kPi = 3.14159265358979323846;

// A seeded stream of numbers in [0, 1): the same seed, the same tree.
struct Dice {
    uint64_t s;
    explicit Dice(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    double next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return double((z ^ (z >> 31)) >> 11) / double(1ull << 53);
    }
    double spread(double half) { return (next() * 2 - 1) * half; }
};

// v turned about the unit axis a by `deg` degrees (Rodrigues).
V3 turn(V3 v, V3 a, double deg) {
    const double t = deg * kPi / 180, c = std::cos(t), s = std::sin(t);
    return v * c + cross(a, v) * s + a * (dot(a, v) * (1 - c));
}

struct Rule {
    double weight;
    std::string rhs;
};

struct Node {
    V3 p;
    int parent = -1;
    std::vector<int> kids;
    double r = 0;
    bool leafy = false;  // a leaf asked for here (L), or a tip
    bool bare = false;   // grown after a `!`: no leaves at its tip
};

struct Turtle {
    V3 p, h{0, 1, 0}, l{-1, 0, 0}, u{0, 0, 1};
    double len = 1;
    int node = 0;
    bool bare = false;
};

// The angle written after a turn, `+(30)`, if there is one.
double angle_at(const std::string& w, std::size_t& i, double fallback) {
    if (i + 1 >= w.size() || w[i + 1] != '(') return fallback;
    const std::size_t close = w.find(')', i + 1);
    if (close == std::string::npos) return fallback;
    const double v = std::atof(w.substr(i + 2, close - i - 2).c_str());
    i = close;
    return v;
}

int ring(Geom& g, V3 c, V3 a, V3 b, double r, int sides) {
    const int first = int(g.p.size());
    for (int j = 0; j < sides; ++j) {
        const double t = 2 * kPi * j / sides;
        g.p.push_back(c + (a * std::cos(t) + b * std::sin(t)) * r);
    }
    return first;
}

void tri(Geom& g, int a, int b, int c, int mat) {
    g.t.insert(g.t.end(), {a, b, c});
    g.mat.push_back(mat);
}

// A low ball of leaves: an octahedron's faces each raised once - twenty-odd
// faces, lumpy - flattened a little and turned at random.
void clump(Geom& g, V3 c, double r, Dice& dice, int mat, int detail) {
    std::vector<V3> v{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    std::vector<int> f{0, 2, 4, 4, 2, 1, 1, 2, 5, 5, 2, 0, 4, 3, 0, 1, 3, 4, 5, 3, 1, 0, 3, 5};
    for (int round = 0; round < detail; ++round) {
        std::vector<int> nf;
        std::map<std::pair<int, int>, int> mid;
        const auto half = [&](int a, int b) {
            const auto k = std::minmax(a, b);
            auto it = mid.find(k);
            if (it != mid.end()) return it->second;
            v.push_back(unit((v[std::size_t(a)] + v[std::size_t(b)]) * 0.5));
            return mid[k] = int(v.size()) - 1;
        };
        for (std::size_t i = 0; i < f.size(); i += 3) {
            const int a = f[i], b = f[i + 1], c2 = f[i + 2], ab = half(a, b), bc = half(b, c2), ca = half(c2, a);
            nf.insert(nf.end(), {a, ab, ca, ab, b, bc, ca, bc, c2, ab, bc, ca});
        }
        f.swap(nf);
    }
    const V3 axis = unit({dice.spread(1), dice.spread(1), dice.spread(1)});
    const double spin = dice.spread(180);
    const int first = int(g.p.size());
    for (const V3& q : v) {
        const double bump = 0.75 + 0.5 * dice.next();
        V3 w = turn(q, axis, spin) * (r * bump);
        w.y *= 0.72;
        g.p.push_back(c + w);
    }
    for (std::size_t i = 0; i < f.size(); i += 3) tri(g, first + f[i], first + f[i + 1], first + f[i + 2], mat);
}

// A leaf: a blade on a short stalk, pointed, widest a third of the way up,
// folded a little along its midrib and curling down at its tip - six faces a
// side, seen from both. `out` is the way it grows, `len` how long.
void blade(Geom& g, V3 base, V3 out, double length, double width, Dice& dice, int mat) {
    const V3 f = unit(out);
    // Its face turned about its own length at random, never edge-on to the sky.
    V3 side = unit(cross(f, V3{0, 1, 0}));
    if (len(side) < 0.5) side = perpendicular(f);
    side = unit(turn(side, f, dice.spread(35)));
    const V3 up = unit(cross(side, f));
    const double w = width * length * (0.85 + 0.3 * dice.next()), fold = w * 0.35, droop = length * 0.18;
    const V3 stalk = base + f * (length * 0.12);
    const auto at = [&](double t, double s) {
        // Along it t (0 at the stalk, 1 the tip), across it s (-1..1).
        return stalk + f * (length * 0.88 * t) + side * (w * s) + up * (fold * (1 - std::fabs(s)) - droop * t * t);
    };
    const int b = int(g.p.size());
    g.p.push_back(base);                    // 0 the stalk's foot
    g.p.push_back(at(0.0, 0.0));            // 1 the blade's foot
    g.p.push_back(at(0.33, -1.0));          // 2 widest, one side
    g.p.push_back(at(0.33, 1.0));           // 3 widest, the other
    g.p.push_back(at(0.45, 0.0));           // 4 the midrib
    g.p.push_back(at(0.75, -0.55));         // 5
    g.p.push_back(at(0.75, 0.55));          // 6
    g.p.push_back(at(1.0, 0.0));            // 7 the tip
    const int tris[] = {1, 2, 4, 1, 4, 3, 2, 5, 4, 4, 6, 3, 4, 5, 7, 4, 7, 6};
    for (int k = 0; k < 18; k += 3) {
        tri(g, b + tris[k], b + tris[k + 1], b + tris[k + 2], mat);
        tri(g, b + tris[k], b + tris[k + 2], b + tris[k + 1], mat);
    }
    // The stalk: a sliver.
    const int s0 = int(g.p.size());
    g.p.push_back(base + side * (w * 0.06));
    tri(g, b, s0, b + 1, mat), tri(g, b, b + 1, s0, mat);
}

void tuft(Geom& g, V3 base, V3 along, double length, int n, Dice& dice, int mat) {
    const V3 a = unit(along);
    const V3 p0 = perpendicular(a);
    for (int k = 0; k < n; ++k) {
        const V3 out = unit(turn(turn(a, p0, 30 + dice.spread(20)), a, 360.0 * k / n + dice.spread(15)));
        const V3 side = unit(cross(out, a)) * (length * 0.035);
        const V3 tip = base + out * (length * (0.8 + 0.4 * dice.next()));
        const int b = int(g.p.size());
        g.p.push_back(base - side), g.p.push_back(base + side), g.p.push_back(tip);
        tri(g, b, b + 1, b + 2, mat), tri(g, b, b + 2, b + 1, mat);
    }
}

// Two crossed cards of leaves, each seen from both sides.
void cards(Geom& g, V3 c, V3 along, double r, Dice& dice, int mat) {
    const V3 a = unit(along);
    for (int k = 0; k < 2; ++k) {
        const V3 side = unit(turn(perpendicular(a), a, 90.0 * k + dice.spread(20)));
        const int b = int(g.p.size());
        g.p.push_back(c - side * r);
        g.p.push_back(c + side * r);
        g.p.push_back(c + side * r + a * (2 * r));
        g.p.push_back(c - side * r + a * (2 * r));
        tri(g, b, b + 1, b + 2, mat), tri(g, b, b + 2, b + 3, mat);
        tri(g, b, b + 2, b + 1, mat), tri(g, b, b + 3, b + 2, mat);
    }
}

}  // namespace

Geom grow(const Growth& gr, int wood, int leafmat, std::string& errors) {
    Geom g;
    // --- the word: the rules, rewritten n times ---------------------------------
    std::map<char, std::vector<Rule>> rules;
    for (const std::string& r : gr.rules) {
        // `A:rhs`, or `A:0.4:rhs` - one of several for A, chosen by weight.
        const std::size_t c1 = r.find(':');
        if (c1 != 1) {
            errors += "grow: a rule is a symbol, `:` and what it becomes, not " + r + "\n";
            continue;
        }
        const std::size_t c2 = r.find(':', 2);
        double w = 1;
        std::string rhs = r.substr(2);
        if (c2 != std::string::npos) {
            char* end = nullptr;
            const std::string ws = r.substr(2, c2 - 2);
            const double v = std::strtod(ws.c_str(), &end);
            if (end && *end == '\0' && !ws.empty()) w = v, rhs = r.substr(c2 + 1);
        }
        rules[r[0]].push_back({w, rhs});
    }
    Dice dice(gr.seed);
    std::string word = gr.axiom;
    constexpr std::size_t kMost = 400000;
    for (int i = 0; i < gr.n; ++i) {
        std::string next;
        for (char ch : word) {
            auto it = rules.find(ch);
            if (it == rules.end()) {
                next += ch;
                continue;
            }
            double total = 0;
            for (const Rule& r : it->second) total += r.weight;
            double pick = dice.next() * total;
            const Rule* chosen = &it->second.back();
            for (const Rule& r : it->second)
                if ((pick -= r.weight) <= 0) {
                    chosen = &r;
                    break;
                }
            next += chosen->rhs;
        }
        word.swap(next);
        if (word.size() > kMost) {
            errors += "grow: the word grew past " + std::to_string(kMost) + " symbols at step " + std::to_string(i + 1) + "; fewer steps\n";
            return g;
        }
    }

    // --- the turtle: what the word walks ---------------------------------------
    std::vector<Node> nodes(1);
    std::vector<int> roots{0};
    std::vector<Turtle> stack;
    Turtle t;
    t.len = gr.len;
    const V3 trop = unit(gr.tropism);
    for (std::size_t i = 0; i < word.size(); ++i) {
        const char ch = word[i];
        const double jit = gr.jitter > 0 ? dice.spread(gr.jitter) : 0.0;
        switch (ch) {
            case 'F':
            case 'G': {
                // A wandering heading, bent towards the tropism as it goes.
                if (gr.jitter > 0) t.h = unit(turn(t.h, t.u, dice.spread(gr.jitter) * 0.5)), t.h = unit(turn(t.h, t.l, dice.spread(gr.jitter) * 0.5));
                if (gr.bend != 0) {
                    const V3 ax = cross(t.h, trop);
                    const double s = len(ax);
                    if (s > 1e-9) {
                        const V3 a = ax * (1 / s);
                        t.h = unit(turn(t.h, a, gr.bend * s * 180 / kPi));
                        t.l = unit(turn(t.l, a, gr.bend * s * 180 / kPi));
                        t.u = cross(t.h, t.l);
                    }
                }
                t.p = t.p + t.h * t.len;
                Node n;
                n.p = t.p;
                n.parent = t.node;
                n.bare = t.bare;
                nodes.push_back(n);
                const int k = int(nodes.size()) - 1;
                nodes[std::size_t(t.node)].kids.push_back(k);
                t.node = k;
                if (nodes.size() > std::size_t(gr.most)) {
                    errors += "grow: more than " + std::to_string(gr.most) + " steps of wood; fewer steps\n";
                    i = word.size();
                }
                break;
            }
            case 'f': {
                t.p = t.p + t.h * t.len;
                Node n;
                n.p = t.p;
                nodes.push_back(n);
                t.node = int(nodes.size()) - 1;
                roots.push_back(t.node);
                break;
            }
            case '+': {
                const double a = angle_at(word, i, gr.angle) + jit;
                t.h = turn(t.h, t.u, a), t.l = turn(t.l, t.u, a);
                break;
            }
            case '-': {
                const double a = angle_at(word, i, gr.angle) + jit;
                t.h = turn(t.h, t.u, -a), t.l = turn(t.l, t.u, -a);
                break;
            }
            case '&': {
                const double a = angle_at(word, i, gr.angle) + jit;
                t.h = turn(t.h, t.l, a), t.u = turn(t.u, t.l, a);
                break;
            }
            case '^': {
                const double a = angle_at(word, i, gr.angle) + jit;
                t.h = turn(t.h, t.l, -a), t.u = turn(t.u, t.l, -a);
                break;
            }
            case '\\': {
                const double a = angle_at(word, i, gr.angle) + jit;
                t.l = turn(t.l, t.h, a), t.u = turn(t.u, t.h, a);
                break;
            }
            case '/': {
                const double a = angle_at(word, i, gr.angle) + jit;
                t.l = turn(t.l, t.h, -a), t.u = turn(t.u, t.h, -a);
                break;
            }
            case '|': t.h = t.h * -1, t.l = t.l * -1; break;
            case '\'': t.len *= gr.shorten; break;
            case '[':
                stack.push_back(t);
                t.len *= gr.branch;
                break;
            case ']':
                if (!stack.empty()) t = stack.back(), stack.pop_back();
                break;
            case 'L': nodes[std::size_t(t.node)].leafy = true; break;
            case '!': t.bare = true; break;
            default: break;
        }
        t.h = unit(t.h), t.l = unit(t.l), t.u = unit(cross(t.h, t.l));
    }
    if (nodes.size() < 2) {
        errors += "grow: the word has no F: nothing grew\n";
        return g;
    }

    // --- thickness by the pipe model, and the height asked for ------------------
    for (std::size_t k = nodes.size(); k-- > 0;) {
        Node& n = nodes[k];
        if (n.kids.empty()) {
            n.r = n.bare ? 4 : 1, n.leafy = n.leafy || !n.bare;  // a root ends blunt, a twig fine
            continue;
        }
        double s = 0;
        for (int c : n.kids) s += std::pow(nodes[std::size_t(c)].r, gr.pipe);
        n.r = std::pow(s, 1 / gr.pipe);
    }
    double rmax = 0;
    for (int r : roots) rmax = std::max(rmax, nodes[std::size_t(r)].r);
    double lo = 1e30, hi = -1e30;
    for (const Node& n : nodes) lo = std::min(lo, n.p.y), hi = std::max(hi, n.p.y);
    const double scale = gr.height > 0 && hi > lo ? gr.height / (hi - lo) : 1.0;
    const double rk = gr.width / std::max(rmax, 1e-9);
    for (Node& n : nodes) n.p = n.p * scale, n.r *= rk;
    const double leaf = gr.leaf * (gr.height > 0 ? 1.0 : scale);

    // --- branches, as runs of nodes ------------------------------------------------
    // Each node's thickest child goes on with it; the others start runs of
    // their own from it (a side branch begins inside the wood it grows out
    // of). A run stops where its wood would be thinner than `min`: what it
    // carried is pruned, and its leaves are kept at the cut.
    std::vector<char> leaves_at(nodes.size(), 0);
    struct Run {
        std::vector<int> nodes;
        bool side = false;
    };
    std::vector<Run> runs;
    std::vector<std::pair<int, int>> todo;  // (the node it grows from, or -1; its first node)
    for (int r : roots) todo.push_back({-1, r});
    const auto by_thickness = [&](std::vector<int> kids) {
        std::sort(kids.begin(), kids.end(), [&](int a, int b) { return nodes[std::size_t(a)].r > nodes[std::size_t(b)].r; });
        return kids;
    };
    for (std::size_t s = 0; s < todo.size(); ++s) {
        Run run;
        run.side = todo[s].first >= 0;
        if (run.side) run.nodes.push_back(todo[s].first);
        int at = todo[s].second;
        run.nodes.push_back(at);
        while (true) {
            const Node& n = nodes[std::size_t(at)];
            if (n.leafy) leaves_at[std::size_t(at)] = 1;
            if (n.kids.empty()) break;
            const std::vector<int> kids = by_thickness(n.kids);
            for (std::size_t c = 1; c < kids.size(); ++c) {
                if (nodes[std::size_t(kids[c])].r < gr.min) leaves_at[std::size_t(at)] = 1;
                else todo.push_back({at, kids[c]});
            }
            if (nodes[std::size_t(kids[0])].r < gr.min) {
                leaves_at[std::size_t(at)] = 1;
                break;
            }
            at = kids[0];
            run.nodes.push_back(at);
        }
        if (run.nodes.size() >= 2) runs.push_back(std::move(run));
    }

    // --- each run a tapering tube, nearly straight spans made one -------------------
    const double cos_merge = std::cos(gr.merge * kPi / 180);
    for (std::size_t ri = 0; ri < runs.size(); ++ri) {
        const std::vector<int>& all = runs[ri].nodes;
        std::vector<int> run{all.front()};
        for (std::size_t i = 1; i + 1 < all.size(); ++i) {
            const V3 a = unit(nodes[std::size_t(all[i])].p - nodes[std::size_t(run.back())].p);
            const V3 b = unit(nodes[std::size_t(all[i + 1])].p - nodes[std::size_t(all[i])].p);
            const double thin = nodes[std::size_t(all[i])].r / std::max(nodes[std::size_t(run.back())].r, 1e-9);
            if (dot(a, b) < cos_merge || thin < 0.8) run.push_back(all[i]);
        }
        run.push_back(all.back());
        // How thick each ring is: a side branch's first, at its parent's
        // middle, is its own; its tip, a third of its last.
        std::vector<double> radius;
        for (std::size_t i = 0; i < run.size(); ++i) radius.push_back(nodes[std::size_t(run[i])].r);
        if (runs[ri].side) radius[0] = radius[1];
        const bool tip = nodes[std::size_t(run.back())].kids.empty();
        if (tip) radius.back() *= 0.35;
        // Fewer faces round a thinner branch.
        const int sides = std::max(3, std::min(gr.sides, int(std::lround(gr.sides * std::sqrt(radius[0] / std::max(gr.width, 1e-9))))));
        std::vector<int> rings;
        V3 across = perpendicular(unit(nodes[std::size_t(run[1])].p - nodes[std::size_t(run[0])].p));
        for (std::size_t i = 0; i < run.size(); ++i) {
            const V3 p = nodes[std::size_t(run[i])].p;
            const V3 in = i > 0 ? unit(p - nodes[std::size_t(run[i - 1])].p) : unit(nodes[std::size_t(run[1])].p - p);
            const V3 out = i + 1 < run.size() ? unit(nodes[std::size_t(run[i + 1])].p - p) : in;
            const V3 dir = unit(in + out);
            // Carried along untwisted: the last ring's across, squared to the way on.
            across = unit(across - dir * dot(across, dir));
            rings.push_back(ring(g, p, across, cross(dir, across), radius[i], sides));
        }
        for (std::size_t i = 0; i + 1 < rings.size(); ++i)
            for (int j = 0; j < sides; ++j) {
                const int j2 = (j + 1) % sides;
                const int A = rings[i] + j, B = rings[i] + j2, C = rings[i + 1] + j2, D = rings[i + 1] + j;
                tri(g, A, B, C, wood), tri(g, A, C, D, wood);
            }
        // Its end closed to a point; the foot of a trunk closed flat.
        {
            const V3 e = nodes[std::size_t(run.back())].p, before = nodes[std::size_t(run[run.size() - 2])].p;
            const int c = int(g.p.size());
            g.p.push_back(e + unit(e - before) * (radius.back() * 1.5));
            for (int j = 0; j < sides; ++j) tri(g, rings.back() + j, rings.back() + (j + 1) % sides, c, wood);
        }
        if (!runs[ri].side) {
            const int c = int(g.p.size());
            g.p.push_back(nodes[std::size_t(run.front())].p);
            for (int j = 0; j < sides; ++j) tri(g, rings.front() + (j + 1) % sides, rings.front() + j, c, wood);
        }
    }

    // --- the leaves, at the tips -----------------------------------------------------
    if (gr.leaves > 0 && leaf > 0)
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            if (!leaves_at[k]) continue;
            const Node& n = nodes[k];
            const V3 along = n.parent >= 0 ? unit(n.p - nodes[std::size_t(n.parent)].p) : V3{0, 1, 0};
            for (int c = 0; c < std::max(1, gr.leafy); ++c) {
                const V3 at = n.p + V3{dice.spread(leaf), dice.spread(leaf * 0.6), dice.spread(leaf)} * (c == 0 ? 0.0 : 0.8);
                if (gr.leaves == 2) cards(g, at, along, leaf * (0.8 + 0.4 * dice.next()), dice, leafmat);
                else if (gr.leaves == 3) {
                    // Leaves: each on its stalk, spread round the tip and
                    // out from it, the first along the twig.
                    const V3 out = c == 0 ? along
                                          : unit(turn(turn(along, perpendicular(along), 35 + dice.spread(30)), along, 360.0 * c / gr.leafy + dice.spread(25)));
                    blade(g, n.p, out, leaf * (0.75 + 0.5 * dice.next()), gr.leaf_width, dice, leafmat);
                } else if (gr.leaves == 4) {
                    tuft(g, n.p - along * (leaf * 0.35 * c), along, leaf, 11, dice, leafmat);
                } else
                    clump(g, at, leaf * (0.8 + 0.4 * dice.next()), dice, leafmat, gr.leaf_detail);
            }
        }
    return g;
}

}  // namespace sg::sculpt::kernel
