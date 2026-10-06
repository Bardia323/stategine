// Terrain: land from a recipe, the same every time, of any size; its roads
// level, its lakes lying in their hollows, its things where its rules say;
// kept as bytes and read back the same; laid in a world whose walker stands
// on it and whose rays stop at it. And plants grown from L-systems: leaves at
// the tips, the same seed the same tree.
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/core/Laws.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/domains/Modeler.hpp"
#include "sg/domains/Terrain.hpp"
#include "sg/domains/Walk.hpp"

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

namespace sg::terrain::kernel {
std::string to_bytes(const Land& land);
bool from_bytes(const std::string& bytes, Land& land);
}  // namespace sg::terrain::kernel

namespace {

std::size_t faces(const sg::sculpt::Model& m, const std::string& material) {
    std::size_t n = 0;
    for (const auto& p : m.parts)
        if (p.material == material) n += p.corners.size() / 24;
    return n;
}

}  // namespace

int main() {
    using namespace sg;
    using namespace sg::terrain;
    // --- plants ---------------------------------------------------------------------
    {
        const auto a = sculpt::build("grow FA A:F[&(30)'A]/(137)[&(30)'A] n=5 height=6 seed=3");
        const auto b = sculpt::build("grow FA A:F[&(30)'A]/(137)[&(30)'A] n=5 height=6 seed=3");
        const auto c = sculpt::build("grow FA A:F[&(30)'A]/(137)[&(30)'A] n=5 height=6 seed=4 jitter=10");
        check(a.errors.empty() && faces(a, "bark") > 0 && faces(a, "leaf") > 0, "a plant grows: wood, and leaves");
        check(std::fabs(a.size().y - 6) < 0.6, "as tall as it was asked (" + std::to_string(a.size().y) + " m)");
        check(a.parts.size() == b.parts.size() && a.triangles == b.triangles && a.parts[0].corners == b.parts[0].corners, "the same seed, the same plant");
        check(a.parts[0].corners != c.parts[0].corners, "another seed, another plant");
        const auto bare = sculpt::build("grow FA A:F[&(30)'A]/(137)[&(30)'A] n=5 leaves=0");
        check(faces(bare, "leaf") == 0, "leaves=0: bare wood");
        // Leaves only at the tips: every leaf is up in the crown, none on the trunk below the first fork.
        double lowest_leaf = 1e9;
        for (const auto& p : a.parts)
            if (p.material == "leaf")
                for (std::size_t k = 1; k < p.corners.size(); k += 8) lowest_leaf = std::min(lowest_leaf, double(p.corners[k]));
        check(lowest_leaf > 6 * 0.2, "leaves at the tips, not down the trunk");
        const auto roots = sculpt::build("grow [!&(110)R]FA R:FF A:F[&(30)A] n=3");
        double low_leaf = 1e9;
        for (const auto& p : roots.parts)
            if (p.material == "leaf")
                for (std::size_t k = 1; k < p.corners.size(); k += 8) low_leaf = std::min(low_leaf, double(p.corners[k]));
        check(low_leaf > -0.01 && roots.lo.y < -0.1, "a root (!) reaches down, and bears no leaves");
        const auto many = sculpt::build("grow F F:FF n=30");
        check(!many.errors.empty(), "a word that grows past all bounds is refused, with the reason");
        const auto full = sculpt::build("use trees\ntree.oak h=9 seed=2"), low = sculpt::build("use trees\ntree.oak h=9 seed=2 low=1");
        check(full.errors.empty() && low.errors.empty() && low.triangles * 3 < full.triangles, "a tree's low-poly self is a third the faces or fewer");
        bool all = true;
        for (const char* t : {"tree.oak", "tree.pine", "tree.birch", "tree.dead", "tree.willow", "tree.swamp", "tree.palm", "bush", "plant.grass", "plant.reeds"}) {
            const auto m = sculpt::build(std::string("use trees\n") + t);
            if (!m.errors.empty() || m.triangles == 0) all = false, std::printf("  %s: %s\n", t, m.errors.c_str());
        }
        check(all, "every plant of the trees library grows");
    }

    // --- land --------------------------------------------------------------------------
    const std::string recipe = R"(size 120 80 cell=0.5
seed 7
noise 6 60 oct=4
hill 30 0 25 -8
flatten -30 10 8 h=2
road 5 -60,-30 -20,-20 20,-25 60,-35 name=main
lake 30 0
layer grass grass
layer earth earth wet=0.6,1
layer rock rock slope=35,90
thing rock variants=2 : rock r=0.5 flat=0.6
scatter rock density=2 road=2, water=1,
)";
    const Land L = make(recipe);
    check(L.errors.empty(), "a recipe makes land" + (L.errors.empty() ? std::string() : ": " + L.errors));
    check(std::fabs(L.w - 120) < 1e-9 && std::fabs(L.d - 80) < 1e-9 && L.nx == 240 && L.nz == 160, "as big as it says, at the fineness it says");
    {
        const Land again = make(recipe);
        check(again.h == L.h && again.splat == L.splat && again.placed.size() == L.placed.size(), "the same recipe, the same land");
    }
    {
        const Land tiny = make("size 3 2 cell=0.1\nnoise 0.2 1"), big = make("size 6000 4000 cell=10\nnoise 300 2000");
        check(tiny.errors.empty() && tiny.nx == 30 && big.errors.empty() && big.nx == 600, "any size: a tabletop and a province");
    }
    check(std::fabs(L.height(-30, 10) - 2) < 0.02 && std::fabs(L.height(-26, 12) - 2) < 0.05, "flatten: a level pad, where it was asked, as high");
    {
        // Across the road, level: its two edges at one height.
        double worst = 0;
        for (double x = -50; x <= 50; x += 5) {
            double zc = 0, best = 1e9;
            for (double z = -40; z <= 0; z += 0.25)
                if (L.at(L.road, x, z) < best) best = L.at(L.road, x, z), zc = z;
            worst = std::max(worst, std::fabs(L.height(x, zc - 1.8) - L.height(x, zc + 1.8)));
        }
        check(worst < 0.15, "a road is level across (" + std::to_string(worst) + " m at worst)");
        check(L.roads.size() == 1 && !L.roads[0].corners.empty() && L.roads[0].name == "main", "and has its surface");
    }
    {
        check(L.waters.size() == 1 && !L.waters[0].corners.empty(), "a lake lies in its hollow");
        const double level = L.waters[0].hi.y;
        check(L.height(30, 0) < level - 1 && L.at(L.water, 30, 0) < 0, "deep at its middle, and that is water");
        // Its shore all round is above it: where water meets dry land, the land is higher.
        bool held = true;
        for (int j = 0; j < L.nz; ++j)
            for (int i = 0; i < L.nx; ++i) {
                const bool wet = L.water[L.index(i, j)] < 0, dry_next = L.water[L.index(i + 1, j)] >= 0;
                if (wet && dry_next && L.h[L.index(i + 1, j)] < level - 0.01) held = false;
            }
        check(held, "its shore is above it all round: it spills nowhere");
    }
    {
        bool ok = !L.placed.empty();
        for (const Placed& p : L.placed) ok = ok && L.at(L.water, p.at.x, p.at.z) >= 0.5 && L.at(L.road, p.at.x, p.at.z) >= 1.5;
        check(ok, "things are strewn, and none in the water or on the road (" + std::to_string(L.placed.size()) + ")");
    }
    {
        double share = 0;
        for (std::size_t q = 0; q < L.h.size(); ++q) share += L.splat[q * 4] + L.splat[q * 4 + 1] + L.splat[q * 4 + 2] + L.splat[q * 4 + 3];
        check(std::fabs(share / 255.0 / double(L.h.size()) - 1) < 0.02, "the layers' shares make the whole of the land");
    }
    {
        Land back;
        check(kernel::from_bytes(kernel::to_bytes(L), back) && back.h == L.h && back.splat == L.splat && back.placed.size() == L.placed.size() &&
                  back.waters.size() == L.waters.size() && back.waters[0].corners == L.waters[0].corners,
              "kept as bytes, read back the same");
        Land none;
        check(!kernel::from_bytes(kernel::to_bytes(L).substr(0, 100), none), "a damaged keep is refused");
    }
    {
        const Land bad = make("size 50 50\nwobble 3\nlayer x lava\nscatter nothing");
        check(bad.errors.find("wobble") != std::string::npos && bad.errors.find("lava") != std::string::npos && bad.errors.find("nothing") != std::string::npos,
              "what a recipe gets wrong is said, a line each");
    }
    for (const std::string& p : presets()) {
        const Land land = make(preset(p));
        check(land.errors.empty(), "the preset `" + p + "` makes its land" + (land.errors.empty() ? std::string() : ": " + land.errors));
    }

    // --- in a world -----------------------------------------------------------------------
    {
        StateGraph g;
        auto& world = g.add<Spatial3D>("world");
        g.set_initial("world");
        world.params().set("sky", 1.0).set("g", 9.8);
        const auto land = std::make_shared<const Land>(L);
        const std::size_t made = lay(world, "land", land);
        check(made > L.placed.size(), "laid in a world: its ground, road, water and things");
        const Spatial3D::Height* ground = world.ground(Key{"land"});
        check(ground && std::fabs((*ground)(-40, 20) - L.height(-40, 20)) < 1e-9, "the world's ground is the land's");
        const std::size_t again = lay(world, "land", land);
        check(again == made && world.elements().size() == made + 1, "laid again, what was laid before is gone first");
        // A walker dropped over it lands on it, and walks up and down it with its feet on it.
        Element& w = world.camera();
        w.params.set(keys::x, -40.0).set(keys::y, L.height(-40, 20) + 6).set(keys::z, 20.0).set(keys::yaw, 0.0);
        field::Solver pull;
        pull.rebuild(fields_of(world));
        double t = 0;
        for (int i = 0; i < 120; ++i) walk(world, pull, w, Stride{}, 1.0 / 60, t += 1.0 / 60);
        const double eye = w.params.num("height", 1.65);
        check(std::fabs(position_of(w).y - (L.height(-40, 20) + eye)) < 0.05 && w.params.num("grounded") > 0.5, "a walker falls onto the land and stands on it");
        double off = 0;
        for (int i = 0; i < 300; ++i) {
            walk(world, pull, w, Stride{1, 0, 0, 0, 3.0, false}, 1.0 / 60, t += 1.0 / 60);
            const Vec3d p = position_of(w);
            off = std::max(off, std::fabs(p.y - eye - L.height(p.x, p.z)));
        }
        check(position_of(w).x > -30 && off < 0.4, "and walks over it, its feet on it all the way (" + std::to_string(off) + " m at most)");
        Vec3d hit, n;
        double dist = 0;
        check(ray(world, {0, 40, 25}, {0, -1, 0}, 100, hit, n, &dist) && std::fabs(hit.y - L.height(0, 25)) < 0.02 && n.y > 0.5, "a ray stops at the land");
        check(g.validate().empty(), "the world is whole");
    }
    std::printf("%s\n", failures ? "FAILED" : "all terrain laws hold");
    return failures ? 1 : 0;
}
