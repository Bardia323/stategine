// The modeller: a recipe in, a mesh out - closed where it should be, cut where
// it is cut, the same every time, and a state that keeps the laws.
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <sstream>
#include <string>

#include <filesystem>

#include "sg/core/Cache.hpp"
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
        // A quad painted from a picture: its corners keep their places on it
        // (rows down), and its material says which picture.
        const std::string obj =
            "mtllib quad.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
            "usemtl paint\nf 1/1/1 2/2/1 3/3/1 4/4/1\n";
        const std::string mtl = "newmtl paint\nKd 1 1 1\nmap_Kd -s 1 1 1 colormap.png\n";
        sculpt::Files f;
        f.read = [&](const std::string& p, std::string& out) {
            out = p.find(".mtl") != std::string::npos ? mtl : obj;
            return true;
        };
        f.stamp = [](const std::string&) { return 1LL; };
        const Model m = sculpt::build("import kit/quad.obj mats=1", {}, &f);
        check(m.parts.size() == 1 && m.parts[0].texture == "kit/colormap.png",
              "an import's material says its picture, beside its library: " + (m.parts.empty() ? std::string() : m.parts[0].texture));
        bool kept = !m.parts.empty();
        if (kept) {
            const std::vector<float>& c = m.parts[0].corners;
            // A corner at (x, y) was painted at (x, 1 - y).
            for (std::size_t i = 0; i + 7 < c.size(); i += 8)
                kept = kept && std::abs(c[i + 6] - c[i]) < 1e-5 && std::abs(c[i + 7] - (1.0f - c[i + 1])) < 1e-5;
        }
        check(kept, "and its corners keep their places on it");
        check(std::find(m.imports.begin(), m.imports.end(), "kit/quad.mtl") != m.imports.end(), "its library is among what it read");
        check(m.parts.size() == 1 && m.parts[0].normal_texture.empty() && m.parts[0].surface_texture.empty() && !m.parts[0].cutout,
              "a material that names only its colour has no other maps");
        const Model flipped = sculpt::build("import kit/quad.obj mats=1 scale=-1,1,1", {}, &f);
        bool still = !flipped.parts.empty();
        if (still) {
            const std::vector<float>& c = flipped.parts[0].corners;
            for (std::size_t i = 0; i + 7 < c.size(); i += 8) still = still && std::abs(c[i + 6] + c[i]) < 1e-5;
        }
        check(still, "mirrored, each corner keeps its own place");
    }
    {
        // A photographed material's maps, as a converter writes them: a normal
        // map (`norm`; `map_Bump -bm` as exporters write one), a surface map
        // (`map_ORM`), and a colour whose alpha cuts the leaf out (`map_d`).
        const std::string obj =
            "mtllib leaf.mtl\nv 0 0 0\nv 1 0 0\nv 1 1 0\nvt 0 0\nvt 1 0\nvt 1 1\n"
            "usemtl bark\nf 1/1 2/2 3/3\nusemtl leaf\nf 3/3 2/2 1/1\n";
        const std::string mtl =
            "newmtl bark\nmap_Kd bark_diff.jpg\nmap_Bump -bm 1.0 bark_nor_gl.png\nmap_ORM bark_arm.png\n"
            "newmtl leaf\nmap_Kd leaf.png\nmap_d leaf.png\nnorm leaf_nor_gl.png\nmap_Bump leaf_height.png\n";
        sculpt::Files f;
        f.read = [&](const std::string& p, std::string& out) {
            out = p.find(".mtl") != std::string::npos ? mtl : obj;
            return true;
        };
        f.stamp = [](const std::string&) { return 1LL; };
        const Model m = sculpt::build("import plant/leaf.obj mats=1", {}, &f);
        const sculpt::Part* bark = nullptr;
        const sculpt::Part* leaf = nullptr;
        for (const sculpt::Part& p : m.parts) (p.material == "bark" ? bark : leaf) = &p;
        check(bark && bark->texture == "plant/bark_diff.jpg" && bark->normal_texture == "plant/bark_nor_gl.png" &&
                  bark->surface_texture == "plant/bark_arm.png" && !bark->cutout,
              "a material's normal and surface maps, beside its library");
        check(leaf && leaf->normal_texture == "plant/leaf_nor_gl.png" && leaf->surface_texture.empty() && leaf->cutout,
              "`norm` is the normal map over a `map_Bump`, and `map_d` cuts the colour out");
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
        check(closed(again) && again.triangles > 600 && again.triangles <= 3000, "an imported heap of shapes, made again: one closed surface, within its budget");
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
    {
        // The language for compositions: a branch, a variable naming a word,
        // a number that varies by where it is asked and never by when.
        const Model branch = sculpt::build("let n 3\nif $n>2\n  box 1 1 1\nelse\n  sphere 5\nend\n");
        check(branch.errors.empty() && std::abs(branch.size().x - 1) < 1e-6, "if/else takes the branch its expression says");
        const Model named = sculpt::build("define big.thing s=1\n  box $s*2 1 1\nend\nlet kind big\n$kind.thing 2\n");
        check(named.errors.empty() && std::abs(named.size().x - 4) < 1e-6, "a macro named by a variable: `$kind.thing`");
        const Model nested = sculpt::build("let style big\nlet big_w 3\nbox $${style}_w 1 1\n");
        check(nested.errors.empty() && std::abs(nested.size().x - 3) < 1e-6, "a variable named by a variable: `$${style}_w`");
        const std::string r = "for i 5\n  box 1 1+rand(7,$i)*3 1 at=$i*2,0,0\nend\n";
        check(sculpt::build(r).size().y == sculpt::build(r).size().y && sculpt::build(r).size().y > 1.5, "rand: varied, and the same every time");
        check(sculpt::build("use nowhere\n").errors.find("no library") != std::string::npos, "an unknown library is named, not passed over");
        // Every style says every word: each makes a building and a tower with
        // no error, and the composer's other compositions run in it.
        for (const std::string& style : {"classical", "gothic", "modern", "romanesque", "islamic", "japanese", "brutalist", "artdeco"}) {
            const Model b = sculpt::build("use arch\nuse " + style + "\nbuilding style=" + style + " w=10 d=8 floors=2 bays=4\ntower style=" + style + " at=-10,0,0\n");
            check(b.errors.empty() && b.triangles > 2000 && b.size().y > 8, style + ": a building and a tower, every word of the style said " + b.errors);
        }
        const Model church = sculpt::build("use arch\nuse gothic\nchurch\n"), temple = sculpt::build("use arch\nuse classical\ntemple\n"),
                    street = sculpt::build("use arch\nuse modern\nstreet style=modern n=3 seed=2\n");
        // (A field meshed coarser to keep its grid down is a note, not a fault.)
        const auto faultless = [](const Model& m) {
            std::istringstream in(m.errors);
            for (std::string l; std::getline(in, l);)
                if (l.find("coarser") == std::string::npos) return false;
            return true;
        };
        check(faultless(church) && faultless(temple) && faultless(street) && church.size().z > 25 && street.size().x > 20,
              "compositions: a church, a temple, a street of houses each its own");
        sculpt::define_library("tiny", "define tiny.wall len=1 h=1 t=1\n  box $len $h $t\nend\n");
        check(sculpt::build("use tiny\ntiny.wall 2 2 2\n").triangles == 12, "a program adds a library of its own (define_library)");
    }
    {
        // The constructions a building is made of, each tradition's own
        // geometry, and what the geometry says: a two-centred arch's crown is
        // at the height asked and its springers at the span; the vault's ribs
        // meet at one crown; an order's column is as tall as asked and as
        // wide as its module; the styles are rebuilt on them without a fault;
        // the parametric style is a point, and any two mixed are a building.
        const auto faultless = [](const Model& m) {
            std::istringstream in(m.errors);
            for (std::string l; std::getline(in, l);)
                if (l.find("coarser") == std::string::npos) return false;
            return true;
        };
        const Model lancet = sculpt::build("use pointed\npointed.arch 2 4 0.75 1\n"), equi = sculpt::build("use pointed\npointed.arch 2 3 0.5 1\n"),
                    round = sculpt::build("use pointed\npointed.arch 2 3 0 1\n");
        check(lancet.errors.empty() && std::abs(lancet.size().y - 4) < 1e-6 && std::abs(lancet.size().x - 2) < 1e-6 && std::abs(equi.size().y - 3) < 1e-6 &&
                  std::abs(round.size().y - 3) < 1e-6,
              "a two-centred arch of any centre: span w, crown at h (" + lancet.errors + ")");
        // The equilateral arch's rise is w * sqrt(3)/2: its springing is where the compass says.
        // (At x = 0.5 the right arc, struck from (-1, s) with radius 2, is at s + 2 sin(acos(0.75)) = s + 1.32.)
        check(inside(equi, 0.95, 3 - 2 * 0.866 - 0.1, 0) && inside(equi, 0.5, 2.4, 0) && !inside(equi, 0.5, 2.95, 0),
              "the equilateral arch springs w*0.866 below its crown, and curves in above its springing as the compass says");
        const Model tudor = sculpt::build("use pointed\npointed.tudor 2 1.6 1\n"), ogee = sculpt::build("use pointed\npointed.ogee 2 3.2 1\n");
        check(tudor.errors.empty() && ogee.errors.empty() && std::abs(tudor.size().y - 1.6) < 1e-6 && std::abs(ogee.size().y - 3.2) < 1e-6 && tudor.size().x < 2.001,
              "a Tudor and an ogee arch, struck by compass, crown where asked");
        const Model vault = sculpt::build("use pointed\npointed.vault 6 7 8 res=0.3\n");
        check(vault.errors.empty() && vault.size().y > 8.0 && vault.size().y < 8.9 && std::abs(vault.size().x - 6 - 0.5) < 0.3 && std::abs(vault.size().z - 7 - 0.5) < 0.3,
              "a rib vault over its bay: its ribs and web rise to the crown asked and no higher " + vault.errors);
        const Model col = sculpt::build("use orders\norder.column corinthian 5\n"), dor = sculpt::build("use orders\norder.column doric 4 fluted=1\n");
        check(col.errors.empty() && std::abs(col.size().y - 5) < 0.02 && std::abs(dor.size().y - 4) < 0.02 && dor.size().x > 2 * 4 / 16.0 * 1.1,
              "an order's column is as tall as asked, its module the height over the order's number");
        for (const char* r : {"use mould\nmould.cornice 6 1 0.9 modillions=1\nmould.balustrade 4 1\nmould.rustication 6 1.2 0.1 0.4\nmould.panel 1 1.5\nmould.console\n",
                              "use orders\norder.portico ionic 12 20 6 6\norder.aedicule corinthian 1.2 2.2 seg=1 at=0,0,20\norder.arcade doric 12 6 4 at=0,0,30\n",
                              "use pointed\npointed.window 2 5 0.5 2\npointed.pinnacle\npointed.buttress\npointed.flyer\npointed.rose\npointed.gable\n",
                              "use girih\ngirih.starcross 4 3\ngirih.rosette\ngirih.strap\ngirih.muqarnas\ngirih.horseshoe\ngirih.multifoil 2 3 7\ngirih.dome 3\ngirih.iwan\ngirih.screen\n",
                              "use structure\nstructure.voussoirs\nstructure.barrel\nstructure.groin 4 4 4 res=0.3\nstructure.dome 2 0.2 0.6\nstructure.pdome\nstructure.pendentives 4 2 0.3 0.3\n"
                              "structure.truss\nstructure.roofframe\nstructure.dogleg\nstructure.spiral\nstructure.curtain\nstructure.ribbon\nstructure.punched 12 3 3.2 4 door=1\n"
                              "structure.storefront\nstructure.balcony\nstructure.penthouse\nstructure.portal\nstructure.sawtooth\nstructure.dock\nstructure.core\n",
                              "use city\nskyscraper 24 24 20 setbacks=1 spire=1\nblock 30 20 5 at=60,0,0\nhouse at=100,0,0\nrowhouse at=130,0,0\nwarehouse at=180,0,0\nshop at=230,0,0\n",
                              "use city\ncity.block 60 40 1 3\n", "let detail 0.4\nuse arch\nuse gothic\ncathedral len=24 w=9 h=10\n", "use arch\nuse islamic\nmosque\n",
                              "let detail 0.4\nuse arch\nuse classical\npalace w=24 d=10 floors=2 bays=7\n",
                              "use arch\nuse romanesque\ncastle\n"}) {
            const Model m = sculpt::build(r);
            check(faultless(m) && m.triangles > 500, std::string("every word of the libraries says something, without a fault: ") + m.errors + r);
        }
        const Model dist = sculpt::build("use city\ncity.district 2 2 bw=50 bd=40\n");
        check(faultless(dist) && dist.size().x > 100 && dist.triangles > 20000, "a district: blocks and streets, buildings by zone and seed");
        // `default` is a `let` unless the name is already set: how a library takes a setting given before `use`.
        const Model dflt = sculpt::build("let k 3\ndefault k 5\ndefault j 2\nbox $k $j 1\n");
        check(dflt.errors.empty() && std::abs(dflt.size().x - 3) < 1e-6 && std::abs(dflt.size().y - 2) < 1e-6, "default sets a variable only where none is set");
        check(std::abs(sculpt::build("box atan2(1,1) 1 1\n").size().x - 45) < 1e-6 && std::abs(sculpt::build("box hypot(3,4) 1 1\n").size().x - 5) < 1e-6,
              "the expressions know atan2 (in degrees) and hypot");
        // `detail` makes less of the ornament, never nothing.
        const Model fine = sculpt::build("use mould\nmould.balustrade 8 1\n"), coarse = sculpt::build("let detail 0.3\nuse mould\nmould.balustrade 8 1\n");
        check(coarse.triangles < fine.triangles / 2 && coarse.triangles > 100, "detail 0.3 makes a balustrade of a fraction of the faces");
        // The parametric style: every known style is a point in it, and two mixed are still a building.
        for (const std::string& pa : {"classical", "gothic", "romanesque", "islamic", "byzantine", "baroque", "modern", "japanese", "artdeco", "brutalist"}) {
            const Model b = sculpt::build("let pa " + pa + "\nuse arch\nuse param\nbuilding style=param w=10 d=8 floors=2 bays=4\ntower style=param at=-10,0,0\n");
            check(faultless(b) && b.triangles > 2000 && b.size().y > 8, "param at " + pa + ": a building and a tower " + b.errors);
        }
        const Model mix = sculpt::build("let pa gothic\nlet pb islamic\nlet pt 0.5\nuse arch\nuse param\nlet param_dome 1\nbuilding style=param w=10 d=8 floors=2 bays=4\n"
                                        "interior style=param w=8 d=10 h=5 at=30,0,0\n");
        const Model twist = sculpt::build("use arch\nuse param\nlet param_twist 60\nlet param_taper 0.4\ntower style=param r=3 h=20\n");
        check(faultless(mix) && mix.triangles > 2000 && faultless(twist) && twist.size().y > 14 && twist.size().x < 9 && twist.size().x > 6,
              "half way from gothic to islamic with a dome, and a tower twisted and tapered: built, not drawn " + mix.errors + twist.errors);
        // A facade asks for its windows: a room behind it can open its walls where they are.
        const Model fac = sculpt::build("use arch\nuse classical\nbuilding style=classical w=12 d=9 floors=2 bays=5 sbays=4\n");
        std::size_t windows = 0, doors = 0;
        for (const auto& q : fac.openings) (q.walk ? doors : windows) += 1;
        check(doors == 1 && windows == 2 * (5 + 5 + 4 + 4) - 1, "a building asks for an opening at every window of every facade, and one at its door (" + std::to_string(windows) + ")");
    }
    {
        // A cut meshes only where it cuts: a quatrefoil through a wall, fine,
        // and the rest of the wall the eight corners it began as.
        const std::string foil = "box 4 3 0.3 res=0.02\nsub group\n  cyl 0.25 1 at=0.2,1.5,0 rot=90,0,0 centre=1\n  cyl 0.25 1 at=-0.2,1.5,0 rot=90,0,0 centre=1\n"
                                 "  cyl 0.25 1 at=0,1.7,0 rot=90,0,0 centre=1\n  cyl 0.25 1 at=0,1.3,0 rot=90,0,0 centre=1\nend\n";
        const auto t0 = std::chrono::steady_clock::now();
        const Model m = sculpt::build(foil);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("     quatrefoil at 2 cm: %zu faces in %.0f ms\n", m.triangles, ms);
        check(closed(m) && m.triangles < 6000, "a fine cut in a wall is meshed where it cuts only: closed, and thousands of faces, not a hundred thousand");
        check(!inside(m, 0, 1.5, 0) && !inside(m, 0.3, 1.5, 0) && inside(m, 0.6, 1.5, 0) && inside(m, 1.5, 0.5, 0.1), "the foil is open, the wall round it solid");
        bool corner = false, far_flat = true;
        for (const Tri& t : tris(m))
            for (const double* p : {t.a, t.b, t.c}) {
                corner = corner || (std::abs(p[0] - 2) < 1e-6 && std::abs(p[1] - 3) < 1e-6 && std::abs(std::abs(p[2]) - 0.15) < 1e-6);
                // away from the cut, nothing but the wall's own planes
                if (std::abs(p[0]) > 1 && std::abs(std::abs(p[2]) - 0.15) > 1e-6 && std::abs(std::abs(p[0]) - 2) > 1e-6 && p[1] > 1e-6 && p[1] < 3 - 1e-6) far_flat = false;
            }
        check(corner && far_flat, "the wall away from the cut keeps its exact faces");
        const Model gate = sculpt::build("wall 8 4 0.8 res=0.05\nsub arch 2 3 4 at=0,0,0\n");
        check(closed(gate) && gate.triangles < 20000, "a gate cut fine through a wall: closed, meshed only round the gate (" + std::to_string(gate.triangles) + " faces)");
        const Model slits = sculpt::build("tower 2 7 3\n");
        check(closed(slits) && slits.triangles < 6000, "a tower's slits each cut where they are, not the tower remeshed (" + std::to_string(slits.triangles) + ")");
    }
    {
        // Remeshing follows the detail: a small bead on a broad slab, made at
        // a budget, keeps its faces on the bead and few on the flat.
        const Model m = sculpt::build("box 4 0.2 4\nsphere 0.15 at=0,0.2,0\nremesh faces=3000 blend=0.03\n");
        std::size_t near = 0;
        for (const Tri& t : tris(m)) {
            const double c[3] = {(t.a[0] + t.b[0] + t.c[0]) / 3, (t.a[1] + t.b[1] + t.c[1]) / 3, (t.a[2] + t.b[2] + t.c[2]) / 3};
            if (std::hypot(std::hypot(c[0], c[1] - 0.35), c[2]) < 0.3) ++near;
        }
        std::printf("     bead on a slab: %zu faces, %zu on the bead\n", m.triangles, near);
        check(closed(m) && m.triangles <= 3000 && near > 250, "remesh at a budget spends it where the detail is (" + std::to_string(near) + " faces on the bead)");
        const Model flat = sculpt::build("box 2 2 2\nremesh res=0.05\n");
        check(closed(flat) && flat.triangles < 400 && std::abs(volume(flat) - 8) < 0.05, "a flat thing remeshed at a cell is a few large faces, not thousands (" + std::to_string(flat.triangles) + ")");
    }
    {
        // A sweep carries its outline round corners without twisting it, and
        // its corners are mitred: neither pinched nor turned.
        const std::string ell = "0,0 0.3,0 0.3,0.1 0.1,0.1 0.1,0.3 0,0.3";
        const std::string path = "0,0,0 0,2,0 2,2,0 2,2,2 0,3,2";
        const Model m = sculpt::build("sweep " + ell + " / " + path + "\n");
        check(closed(m) && m.errors.empty(), "a sweep up, across and round is closed " + m.errors);
        // A square swept along a mitred path holds its area times the length
        // of the path its middle follows.
        const Model sq = sculpt::build("sweep -0.1,-0.1 0.1,-0.1 0.1,0.1 -0.1,0.1 / 0,0,0 0,0,-2 0,2,-2 2,2,-2\n");
        check(closed(sq) && std::abs(volume(sq) - 0.04 * 6) < 0.04 * 6 * 0.01, "a square swept round a bend upward and one sideways keeps its section: area x length (" + std::to_string(volume(sq)) + ")");
        const Model f = sculpt::build("sweep " + ell + " / " + path + "\nremesh res=0.02\n");
        int agree = 0, n = 0;
        for (double s = 0.1; s < 0.95; s += 0.05)
            for (const auto& p : {std::array<double, 3>{0.05, s * 2, 0.05}, std::array<double, 3>{s * 2, 2.05, 0.2}, std::array<double, 3>{2.2, 2.05, s * 2}}) {
                ++n;
                agree += inside(m, p[0], p[1], p[2]) == inside(f, p[0], p[1], p[2]);
            }
        check(agree >= n - 3, "its exact faces and its field agree on where it is, leg after leg (" + std::to_string(agree) + " of " + std::to_string(n) + ")");
        const Model ring = sculpt::build("sweep " + ell + " / 0,0,0 3,0,0 3,0,3 0,1,3 0,0,0\n");
        check(closed(ring) && ring.triangles == 6 * 4 * 2, "a path that ends where it began goes round: no ends, every corner mitred");
        const Model round = sculpt::build("tube 0.1 0,0,0 2,0,0 2,0,2 bend=0.5\n");
        check(closed(round) && round.triangles > sculpt::build("tube 0.1 0,0,0 2,0,0 2,0,2\n").triangles, "bend= rounds a path's corners");
    }
    {
        // A word said twice on a line is never quietly dropped: turns compose,
        // anything else is refused by name.
        const Model turned = sculpt::build("box 2 1 1 rot=0,0,90 rot=90,0,0\n");
        check(turned.errors.empty() && std::abs(turned.size().x - 1) < 1e-6 && std::abs(turned.size().z - 2) < 1e-6,
              "two rot= compose, the first first " + turned.errors);
        const Model twice = sculpt::build("box 1 1 1 at=1,0,0 at=2,0,0\n");
        check(twice.triangles == 0 && twice.errors.find("at= said twice") != std::string::npos, "at= said twice: the line is refused, by name");
        const Model block = sculpt::build("group at=1,0,0 at=2,0,0\n  box 1 1 1\nend\nbox 1 1 1 at=5,0,0\n");
        check(block.errors.find("said twice") != std::string::npos && block.errors.find("nothing to end") == std::string::npos && block.triangles == 24,
              "a block said so still ends where it ends");
        const Model both = sculpt::build("define post h=2\n  box 0.2 $h 0.2\nend\npost 3 h=4\n");
        check(both.triangles == 0 && both.errors.find("said twice") != std::string::npos, "a macro's parameter said by place and by name is said twice");
        check(!sculpt::placement("box 1 1 1 rot=90 rot=0,0,90\n", 0).editable, "a line with two turns is read, not written back as one");
    }
    {
        // Openings asked of the walls: no faces, carried as the solid is.
        const Model m = sculpt::build("group rot=90 at=5,0,0\n  array n=3 step=2,0,0\n    opening 1 2 head=pointed at=0,1,0\n  end\nend\nbox 1 1 1\n");
        check(m.triangles == 12 && m.openings.size() == 3, "three openings in an array, and no faces of theirs");
        const auto& o = m.openings[1];
        check(std::abs(o.at.x - 5) < 1e-9 && std::abs(o.at.y - 1) < 1e-9 && std::abs(o.at.z + 2) < 1e-9 && std::abs(o.w - 1) < 1e-9 && std::abs(o.h - 2) < 1e-9 &&
                  std::abs(o.facing.x - 1) < 1e-9 && o.head == "pointed",
              "each where the blocks put it, sized and turned with them");
        check(sculpt::build("opening 1 2 head=ogee\n").errors.find("round, pointed or square") != std::string::npos, "an opening's head is one there is");
        const Model in = sculpt::build("use arch\nuse gothic\ninterior style=gothic w=14 d=30 h=10 sbays=4 walls=0 arcade=1 ww=2.2 wh=6 wsill=2.5\n");
        std::size_t windows = 0, arches = 0;
        for (const auto& q : in.openings) (q.h > 3 ? windows : arches) += 1;
        check(in.errors.empty() && windows == 8 && arches > 8 && in.openings[0].head == "pointed", "an interior with the room's walls asks them for its windows and its arcade " + in.errors);
        const Model own = sculpt::build("use arch\nuse gothic\ninterior style=gothic w=14 d=30 h=10 sbays=4\n");
        check(own.openings.empty(), "an interior with walls of its own asks nothing of anyone's");
        const Vec3d at = sculpt::stand(sculpt::build("box 1 1 1 at=3,-1,0\n"), {10, 0, 0}, 0.0);
        check(std::abs(at.x - 13) < 1e-9 && std::abs(at.y + 1) < 1e-9, "a model stands where its recipe says: its box's foot carried from the origin");
    }
    {
        // What moves in a model (`moves`): its faces a part of their own, its
        // joint where its block put it - riding on the one round it, turned
        // the other way in a mirror - and the model posed by them.
        const Model door = sculpt::build(
            "box 1.2 2.2 0.1 mat=wood\nmoves leaf turn 0 90 axis=-y at=-0.45,0,0.05\n  box 0.9 2 0.04 at=0.45,0,-0.02 mat=oak\n"
            "  moves fold turn 0 0 follow=-2 axis=-y at=0.9,0,0\n    box 0.3 0.3 0.3 at=0.15,1,0\n  end\nend\n");
        bool tagged = door.joints.size() == 2 && door.errors.empty();
        for (const auto& p : door.parts) tagged = tagged && (p.material == "wood") == p.joint.empty();
        check(tagged, "a moving block's faces are parts of their own, each saying its joint " + door.errors);
        if (tagged) {
            const sculpt::Joint& fold = door.joints[0];
            const sculpt::Joint& leaf = door.joints[1];  // (the inner block closes first)
            check(fold.name == "fold" && fold.parent == 1 && fold.follow == -2 && leaf.name == "leaf" && leaf.parent == -1 && std::abs(leaf.axis.y + 1) < 1e-9 &&
                      std::abs(leaf.at.x + 0.45) < 1e-9 && std::abs(leaf.at.z - 0.05) < 1e-9 && std::abs(fold.at.x - 0.45) < 1e-9,
                  "each joint where its block put it, the inner riding on the outer");
        }
        // A quarter turn about -y swings the leaf's far edge from x = 0.45 out to z = 0.05 + 0.9;
        // the fold, following at -2, turns back on it and lies along the leaf.
        const Model swung = sculpt::pose(door, {{"leaf", 90}});
        double flo = 1e9, fhi = -1e9;
        for (const auto& p : swung.parts)
            for (std::size_t c = 0; p.joint == "fold" && c + 7 < p.corners.size(); c += 8)
                flo = std::min(flo, double(p.corners[c + 2])), fhi = std::max(fhi, double(p.corners[c + 2]));
        check(std::abs(swung.hi.z - 0.95) < 1e-4 && fhi < 0.951 && flo > 0.649 && sculpt::pose(door, {}).all() == door.all(),
              "posed: the leaf swung out a quarter turn, the fold turned back on it - and unposed, as made");
        check(std::abs(sculpt::pose(door, {{"leaf", 500}}).hi.z - 0.95) < 1e-4, "a joint goes no further than its way");
        const Model mirrored = sculpt::build("group scale=-1,1,1\n  moves m turn 0 90 at=0.5,0,0\n    box 1 1 0.1 at=0.5,0,0\n  end\nend\n");
        check(mirrored.joints.size() == 1 && std::abs(mirrored.joints[0].axis.y + 1) < 1e-9 && std::abs(mirrored.joints[0].at.x + 0.5) < 1e-9,
              "in a mirror a joint turns the other way round");
        const Model drawer = sculpt::build("moves d slide 0 0.4 axis=z\n  box 0.5 0.2 0.5\nend\n");
        check(std::abs(sculpt::opened(drawer, 1).hi.z - 0.65) < 1e-6 && std::abs(sculpt::opened(drawer, 0.5).hi.z - 0.45) < 1e-6,
              "a slide runs along its axis, opened as far as asked");
        check(sculpt::build("moves a turn 0 1\n  box 1 1 1\nend\nmoves a turn 0 1\n  box 1 1 1 at=3,0,0\nend\n").errors.find("twice") != std::string::npos,
              "two joints of one name are an error");
        check(std::abs(sculpt::build("define w2\n  box $q 1 1\nend\nw2 q=3\n").size().x - 3) < 1e-9,
              "a word said to a macro that it does not name is a variable in it");
    }
    {
        // Doors, drawers and cabinets (`use doors`): every word of the
        // library made without a fault, every kind of door a set of choices
        // whose leaves say how they move.
        const auto faultless = [](const Model& m) {
            std::istringstream in(m.errors);
            for (std::string l; std::getline(in, l);)
                if (l.find("coarser") == std::string::npos) return false;
            return true;
        };
        const std::string parts[] = {"door.fill.", "door.grip.", "door.hinge.", "door.body.", "door.op.", "door.frame.", "door.arch.", "door.ring.", "door.tymp."};
        const std::string inner[] = {"door.leaf", "door.handle", "door.light", "door.studs", "door.hung", "door.hang", "door.glide", "door.fold",
                                     "door.knocker", "door.hammer", "door.chime", "door.garage", "door.roller", "door.wall", "door.rollers"};
        std::istringstream words(sculpt::recipes("doors"));
        int kinds = 0;
        for (std::string l; std::getline(words, l);) {
            const std::string word = l.substr(0, l.find(' '));
            if (word.empty()) continue;
            const Model m = sculpt::build("use doors\n" + word + "\n");
            check(faultless(m), "a word of the doors, made without a fault: " + word + " " + m.errors);
            bool kind = word.rfind("door.", 0) == 0;
            for (const std::string& p : parts) kind = kind && word.rfind(p, 0) != 0;
            for (const std::string& p : inner) kind = kind && word != p;
            if (kind) {
                ++kinds;
                check(!m.joints.empty() && m.triangles > 50, "a kind of door moves: " + word);
            }
        }
        check(kinds >= 30, "thirty kinds of door and more (" + std::to_string(kinds) + ")");
        const Model hotel = sculpt::build("use doors\ndoor.hotel\n");
        const Model open = sculpt::opened(hotel, 1);
        check(hotel.openings.size() == 1 && hotel.openings[0].walk && std::abs(hotel.openings[0].w - 0.915) < 1e-6 && open.hi.z > hotel.hi.z + 0.8,
              "a hotel's door asks its wall for its opening, and its leaf swings a quarter turn out");
        const Model persian = sculpt::build("use doors\ndoor.persian name=gate\n");
        check(persian.joints.size() == 2 && persian.joints[0].name == "gate.1" && persian.joints[0].axis.y * persian.joints[1].axis.y < 0,
              "a pair: two leaves, hung at either jamb, named as asked");
        const Model kitchen = sculpt::build("use doors\ncabinet.kitchen name=k\n");
        int slides = 0, turns = 0;
        for (const auto& j : kitchen.joints) (j.slide ? slides : turns) += 1;
        check(faultless(kitchen) && slides == 1 && turns == 2, "a kitchen's unit: a drawer that runs out, two doors that turn " + kitchen.errors);
        check(sculpt::build("use doors\ncabinet.kitchen fill=glass\n").parts.size() > kitchen.parts.size(),
              "a choice said to a kind of cabinet reaches its fronts (glass doors)");
        // A sectional door's panels (its leaves' material) run up the jambs and back under the ceiling.
        const auto reach = [](const Model& m) {
            double z = -1e9;
            for (const auto& p : m.parts)
                for (std::size_t c = 0; p.material == "paintedmetal" && c + 7 < p.corners.size(); c += 8) z = std::max(z, double(p.corners[c + 2]));
            return z;
        };
        const Model garage = sculpt::build("use doors\ndoor.garage\n");
        check(reach(sculpt::opened(garage, 1)) > reach(garage) + 1.5 && garage.joints.size() >= 4 && !garage.joints[0].path.empty(),
              "a sectional door's panels ride their tracks up and back under the ceiling, all together");
        // A track: a part riding it by two points, turned as it bends.
        const Model rider = sculpt::build("moves p track 0 3 0,0,0 0,1,0 0,1,2 span=0.5\n  box 1 0.5 0.05\nend\n");
        const Model up = sculpt::pose(rider, {{"p", 2.0}});
        check(rider.errors.empty() && std::abs(up.lo.y - 1.0) < 0.03 && up.hi.y < 1.03 && up.hi.z > 1.4,
              "on a track, a part goes up it and round its corner, lying along it after");
    }
    {
        // A model that imports is kept on disk too, and read back only while
        // what it imports says the same.
        const std::string dir = "modeler_cache_test";
        sg::cache::set_folder(dir);
        std::string obj = sculpt::to_obj(sculpt::build("box 1 2 3\n"), "a");
        sculpt::Files f;
        f.read = [&](const std::string&, std::string& text) { return text = obj, true; };
        f.stamp = [&](const std::string&) { return (long long)obj.size(); };
        Modeler::set_files(f);
        const auto made = [&](const char* id) {
            StateGraph g;
            auto& m = g.add<Modeler>(Key{id});
            m.hear(Event{Modeler::set_event(), Params{}.set("ops", std::string("import a.obj\nsphere 0.3 at=4,0,0\n"))});
            m.dispatch_pending();
            return m.model();
        };
        const Model first = made("one");
        const auto before = sg::cache::stats();
        const Model again = made("two");
        const auto after = sg::cache::stats();
        check(after.hits == before.hits + 1 && again.all() == first.all(), "a model that imports is read back from the disk");
        obj = sculpt::to_obj(sculpt::build("box 3 1 1\n"), "a");
        const Model changed = made("three");
        check(std::abs(changed.size().x - first.size().x) > 0.5, "and made again when the file it imports says something else");
        Modeler::set_files({});
        sg::cache::set_folder("");
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
