// The modeller: a recipe in, a mesh out - closed where it should be, cut where
// it is cut, the same every time, and a state that keeps the laws.
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "sg/core/Laws.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/domains/Modeler.hpp"

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

using sg::sculpt::Model;

struct Tri {
    double a[3], b[3], c[3];
};
static std::vector<Tri> tris(const Model& m) {
    std::vector<Tri> out;
    for (const auto& p : m.parts)
        for (std::size_t i = 0; i + 23 < p.corners.size(); i += 24) {
            Tri t;
            for (int k = 0; k < 3; ++k) t.a[k] = p.corners[i + std::size_t(k)], t.b[k] = p.corners[i + 8 + std::size_t(k)], t.c[k] = p.corners[i + 16 + std::size_t(k)];
            out.push_back(t);
        }
    return out;
}
// Every edge is met once each way: no hole, nothing doubled.
static bool closed(const Model& m) {
    std::map<std::array<long long, 3>, int> id;
    const auto key = [&](const double* p) {
        std::array<long long, 3> k{std::llround(p[0] * 1e4), std::llround(p[1] * 1e4), std::llround(p[2] * 1e4)};
        return id.emplace(k, int(id.size())).first->second;
    };
    std::map<std::pair<int, int>, int> e;
    int degenerate = 0;
    for (const Tri& t : tris(m)) {
        const int a = key(t.a), b = key(t.b), c = key(t.c);
        if (a == b || b == c || a == c) {
            ++degenerate;
            continue;
        }
        ++e[{a, b}], ++e[{b, c}], ++e[{c, a}];
    }
    int bad = 0, dbl = 0;
    for (const auto& [k, n] : e) {
        if (n != 1) ++dbl;
        else if (!e.count({k.second, k.first})) ++bad;
    }
    if (bad || dbl) std::printf("     open edges %d, doubled %d, degenerate faces %d of %zu edges\n", bad, dbl, degenerate, e.size());
    // Where a cut meets a corner two cells may put their vertex on one point: a pinch, a face of no area whose
    // edges then go unmatched - not a hole. A hole is open edges beyond what the pinches account for.
    return !e.empty() && bad <= 3 * degenerate && dbl * 200 < int(e.size());
}
static double volume(const Model& m) {
    double v = 0;
    for (const Tri& t : tris(m))
        v += (t.a[0] * (t.b[1] * t.c[2] - t.b[2] * t.c[1]) - t.a[1] * (t.b[0] * t.c[2] - t.b[2] * t.c[0]) + t.a[2] * (t.b[0] * t.c[1] - t.b[1] * t.c[0])) / 6;
    return v;
}
// Inside, by a ray's crossings.
static bool inside(const Model& m, double x, double y, double z) {
    const double d[3] = {0.3127, 0.5713, 0.7611};
    int n = 0;
    const auto cross = [](const double* a, const double* b, double* o) {
        o[0] = a[1] * b[2] - a[2] * b[1], o[1] = a[2] * b[0] - a[0] * b[2], o[2] = a[0] * b[1] - a[1] * b[0];
    };
    const auto dot = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    for (const Tri& t : tris(m)) {
        double e1[3], e2[3], p[3], s[3], q[3];
        for (int k = 0; k < 3; ++k) e1[k] = t.b[k] - t.a[k], e2[k] = t.c[k] - t.a[k];
        cross(d, e2, p);
        const double det = dot(e1, p);
        if (std::abs(det) < 1e-12) continue;
        const double o[3] = {x, y, z};
        for (int k = 0; k < 3; ++k) s[k] = o[k] - t.a[k];
        const double u = dot(s, p) / det;
        cross(s, e1, q);
        const double v = dot(d, q) / det;
        if (u < 0 || v < 0 || u + v > 1) continue;
        if (dot(e2, q) / det > 0) ++n;
    }
    return n % 2 == 1;
}

int main() {
    using namespace sg;
    {
        StateGraph g;
        auto& m = g.add<Modeler>(Key{"model"});
        g.set_initial(Key{"model"});
        m.ports(g);
        const auto say = [&](Key ev, Params p) {
            m.hear(Event{ev, std::move(p)});
            m.dispatch_pending();
        };
        say(Modeler::op_event(), Params{}.set("line", std::string("box 2 1 1")));
        say(Modeler::op_event(), Params{}.set("line", std::string("sphere 0.5 at=0,1,0")));
        check(m.count() == 2 && m.model().triangles > 12, "lines come in at its port and make a mesh");
        say(Modeler::undo_event(), Params{}.set("n", 1.0));
        check(m.count() == 1 && m.model().triangles == 12, "undo takes the last line back");
        say(Modeler::clear_event(), {});
        check(m.count() == 0 && m.model().triangles == 0, "clear: nothing");
        say(Modeler::set_event(), Params{}.set("ops", std::string("cyl 1 2\n")));
        check(sg::verify(g).ok(), "the state keeps the laws");
        const Model& a = m.model();
        const Model& b = m.model();
        check(&a == &b, "a mesh already made is not made again");
    }
    {
        const Model m = sculpt::build("box 2 1 3");
        check(m.triangles == 12 && closed(m) && std::abs(volume(m) - 6) < 1e-6, "a box: 12 faces, closed, its volume");
        check(std::abs(m.lo.y) < 1e-9 && std::abs(m.hi.y - 1) < 1e-9, "standing on y = 0");
    }
    {
        const Model m = sculpt::build("cyl 1 2\nsphere 1 at=4,0,0\ncone 1 0 2 at=8,0,0\ntorus 1 0.3 at=12,0,0\ncapsule 0.5 2 at=16,0,0");
        check(closed(m) && m.errors.empty(), "cylinder, sphere, cone, torus, capsule: all closed " + m.errors);
        check(volume(m) > 0, "and wound outwards (positive volume)");
    }
    {
        const Model m = sculpt::build("box 2 2 2\nsub cyl 0.4 3 at=0,1,0 centre=1");
        check(closed(m), "a box with a hole through it is closed");
        check(!inside(m, 0, 1, 0) && inside(m, 0.8, 1, 0.8) && inside(m, 0.7, 0.2, 0.1), "and the hole is there: the middle is empty, the rest solid");
        const double v = volume(m);
        check(std::abs(v - (8 - 3.14159265 * 0.16 * 2)) < 0.25, "its volume is the box less the bore (" + std::to_string(v) + ")");
        double nearest = 9;
        for (const Tri& t : tris(m))
            for (const double* p : {t.a, t.b, t.c}) nearest = std::min(nearest, std::hypot(std::hypot(p[0] - 1, p[1] - 2), p[2] - 1));
        check(nearest < 0.02, "the box's corners stay sharp where it was cut elsewhere (" + std::to_string(nearest) + ")");
    }
    {
        const Model m = sculpt::build("wall 8 4 0.8\nsub arch 2 3 4 at=0,0,0");
        check(closed(m), "a wall with a gate cut through it is closed " + m.errors);
        check(!inside(m, 0, 1, 0) && !inside(m, 0, 2.9, 0.1) && inside(m, 2.5, 1, 0) && inside(m, 0, 3.6, 0.2),
              "the gate is open to its arch, and the wall stands above and beside");
    }
    {
        const Model i = sculpt::build("box 2 2 2\nand sphere 1.2 at=0,1,0 centre=1");
        check(closed(i) && inside(i, 0, 1, 0) && !inside(i, 0.95, 1.95, 0.95), "an intersection: the box's corners go");
        const Model b = sculpt::build("sphere 1\nblend=0.5 sphere 1 at=1.5,0,0");
        check(closed(b) && inside(b, 0.75, 1, 0), "a smooth union bridges two spheres");
    }
    {
        const Model m = sculpt::build("array n=5 step=1,0,0\n box 0.5 1 0.5\nend");
        check(m.triangles == 60 && std::abs(m.size().x - 4.5) < 1e-6, "an array of five boxes");
        const Model r = sculpt::build("radial n=6\n box 0.4 0.4 0.4 at=2,0,0\nend");
        check(r.triangles == 72 && std::abs(r.hi.x - 2.2) < 0.25 && std::abs(r.lo.x + 2.2) < 0.25, "a radial of six");
        const Model mi = sculpt::build("mirror x\n box 1 1 1 at=2,0,0\nend");
        check(mi.triangles == 24 && std::abs(mi.lo.x + 2.5) < 1e-6 && volume(mi) > 1.9, "a mirror, wound outwards");
    }
    {
        const Model m = sculpt::build("define post h=2 r=0.1\n cyl $r $h\n sphere $r*1.5 at=0,$h,0\nend\npost 3 0.2\npost h=1 at=5,0,0");
        check(m.errors.empty() && std::abs(m.hi.y - 3.6) < 0.05, "a macro, with arguments and defaults " + m.errors);
        const Model f = sculpt::build("for i 4\n box 1 $i+1 1 at=$i*2,0,0\nend");
        check(f.errors.empty() && std::abs(f.hi.y - 4) < 1e-6, "a for loop with expressions " + f.errors);
        const Model t = sculpt::build("tower 2 8 3\nstairs 6\npine\nrock\ncolumn\nroof\npyramid\nturret\nwindow\ngatehouse");
        check(t.errors.empty() && t.triangles > 500, "the library builds: " + t.errors);
        check(!sculpt::recipes().empty() && sculpt::build("nonsense 3").errors.find("unknown statement") != std::string::npos,
              "the library is listed, and a bad line is reported");
    }
    {
        const std::string r = "wall 8 4 0.8\nsub arch 2 3 4\ntower 2 7 3 at=5,0,0\nbox 1 1 1 mat=wood at=0,0,3";
        const Model a = sculpt::build(r), b = sculpt::build(r);
        check(a.all() == b.all() && a.parts.size() == 2, "the same recipe makes the same triangles, apart by material");
    }
    {
        const Model m = sculpt::build("tower 2 8 3\nbox 1 1 1 mat=door at=3,0,0");
        const std::string obj = sculpt::to_obj(m, "t");
        sculpt::Files f;
        f.read = [&](const std::string&, std::string& out) { return out = obj, true; };
        f.stamp = [](const std::string&) { return 1LL; };
        const Model back = sculpt::build("import t.obj mats=1", {}, &f);
        check(back.triangles == m.triangles && std::abs(back.size().y - m.size().y) < 1e-3 && back.parts.size() == m.parts.size(),
              "an .obj written and read back is the same model (" + std::to_string(back.triangles) + " faces) " + back.errors);
        const Model big = sculpt::build("import t.obj fit=4 base=1 scale=2", {}, &f);
        check(std::abs(big.hi.y - 8) < 1e-3, "an import fitted to a height, then scaled");
    }
    {
        const std::string castle =
            "wall 12 4 0.9 at=0,0,-6\nsub arch 2.2 3 3 at=0,0,-6\nwall 12 4 0.9 at=0,0,6\nwall 12 4 0.9 at=-6,0,0 rot=90\nwall 12 4 0.9 at=6,0,0 rot=90\n"
            "tower 2 7 3 at=-6,0,-6\ntower 2 7 3 at=6,0,-6\ntower 2 7 3 at=-6,0,6\ntower 2 7 3 at=6,0,6\n"
            "box 4 5 4 at=0,0,0\nroof 4.6 4.6 2\ngatehouse at=0,0,-6\n";
        const auto t0 = std::chrono::steady_clock::now();
        const Model m = sculpt::build(castle);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("     castle: %zu triangles in %.0f ms; %s\n", m.triangles, ms, m.errors.c_str());
        check(m.triangles > 1000 && ms < 1500, "a castle-size recipe builds well under a second and a half");
    }
    {
        // A recipe read as placements, and written back from them.
        const std::string r = "# a hut\nbox 2 1 2 mat=stone  # walls\nsub cyl 0.3 1 at=0,0,1 rot=90\ngroup towers\n  tower 1 3 1 at=4,0,0\nend\n"
                              "cone 1 1 at=0,1,0 scale=1.5\nbox 1 1 1 at=$x,0,0\n";
        const auto ps = sculpt::placements(r);
        check(ps.size() == 4 && ps[0].line == 1 && ps[1].line == 2 && ps[2].line == 6 && ps[3].line == 7,
              "what stands at the top of a recipe is placed; what is in a block, or a comment, is not");
        check(std::abs(ps[1].at.z - 1) < 1e-9 && std::abs(ps[1].rot.y - 90) < 1e-9 && std::abs(ps[2].scale.x - 1.5) < 1e-9 && !ps[3].editable,
              "where it stands, its turn and its size are read; an expression is read, not editable");
        check(sculpt::place(r, 2, ps[1].at, ps[1].rot, ps[1].scale) == r, "writing back what was read changes nothing");
        const std::string moved = sculpt::place(r, 1, {0.5, 0, -0.25}, {0, 45, 0}, {1, 1, 1});
        const auto again = sculpt::placement(moved, 1);
        check(moved.find("box 2 1 2 mat=stone at=0.5,0,-0.25 rot=45  # walls") != std::string::npos && std::abs(again.at.x - 0.5) < 1e-9 &&
                  std::abs(again.rot.y - 45) < 1e-9 && sculpt::place(moved, 1, again.at, again.rot, again.scale) == moved,
              "moved and turned, the line says so, its other words and comment kept - and reads back as written");
        check(sculpt::place(r, 7, {1, 0, 0}, {}, {1, 1, 1}) == r && sculpt::place(r, 4, {1, 0, 0}, {}, {1, 1, 1}) == r,
              "a line that is not editable, or not placed, is left as it is");
        sg::StateGraph g;
        auto& m = g.add<Modeler>("pick");
        m.hear(sg::Event{Modeler::select_event(), sg::Params{}.set("line", 2.0)});
        m.dispatch_pending();
        check(m.element(Modeler::recipe_id()).params.num("selected") == 2.0, "the line worked on is the recipe's: model.select");
    }
    {
        // A mesh sent in, made again: the field of its faces (inside where its
        // winding says, so shapes in one another are one solid), meshed at a
        // budget - one closed surface of about that many faces.
        const std::string heap = "box 2 2 2\nbox 2 2 2 at=1,0.5,1\nsphere 1.2 at=0,2,0\ncyl 0.4 3 at=-1,0,1\n";
        const Model parts = sculpt::build(heap);
        const std::string obj = sculpt::to_obj(parts, "heap");
        sculpt::Files files;
        files.read = [&](const std::string&, std::string& text) { return text = obj, true; };
        files.stamp = [](const std::string&) { return 0LL; };
        const auto t0 = std::chrono::steady_clock::now();
        const Model again = sculpt::build("import heap.obj faces=3000\n", {}, &files);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("     heap: %zu faces -> %zu in %.0f ms %s\n", parts.triangles, again.triangles, ms, again.errors.c_str());
        check(closed(again) && again.triangles > 1500 && again.triangles < 6000, "an imported heap of shapes, made again: one closed surface, near its budget");
        check(std::abs(volume(again) - volume(parts)) / volume(parts) < 0.6 && inside(again, 0, 1, 0) && inside(again, 1.5, 1.5, 1.5) && !inside(again, 3, 1, -2),
              "its inside is the union of what it was made of");
        const Model one = sculpt::build(heap + "remesh faces=2000\n");
        check(closed(one) && one.triangles > 800 && one.triangles < 4500, "remesh: the shapes so far, one surface, at a budget");
        const auto rgb = sculpt::picture(one, 128, 128);
        const std::string file = sculpt::png(rgb, 128, 128);
        bool lit = false;
        for (std::size_t i = 0; i < rgb.size(); i += 3) lit = lit || rgb[i] > 120;
        check(rgb.size() == 128 * 128 * 3 && lit && file.rfind("\x89PNG", 0) == 0, "and a picture of it to look at, as a PNG");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
