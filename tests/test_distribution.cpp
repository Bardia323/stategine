// Stategine - Distribution: laws of chance kept as data. Each value law's
// draws have the mean and spread it says; the same law and seed draw the
// same; points keep their process's character - blue noise its spacing,
// clusters their clumping; intensities add, and points drawn by a sum are as
// many as its parts' together (superposition); a field sampled by any
// process follows its intensity; a law carried onto a user by a functor's
// transport; the faults named. And Wear's: a leak that reaches nowhere, an
// age past the most.
#include <cmath>
#include <cstdio>
#include <vector>

#include "sg/domains/Distribution.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/domains/Wear.hpp"

namespace {
int failed = 0;
void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
    failed += !ok;
}
double mean_of(const sg::Law& l, int n) {
    double s = 0;
    for (int i = 0; i < n; ++i) s += sg::draw(l, static_cast<uint64_t>(i));
    return s / n;
}
}  // namespace

int main() {
    sg::Distribution d(sg::Key{"chance"});
    d.law(sg::Key{"size"}, "power", sg::Params{}.set("lo", 0.1).set("hi", 2.0).set("alpha", 1.5));
    d.law(sg::Key{"tall"}, "normal", sg::Params{}.set("mean", 1.7).set("sd", 0.1));
    d.law(sg::Key{"turn"}, "vonmises", sg::Params{}.set("mean", 0.5).set("kappa", 8.0));
    d.law(sg::Key{"share"}, "beta", sg::Params{}.set("a", 2.0).set("b", 6.0));
    d.law(sg::Key{"which"}, "choice", sg::Params{}.set("w0", 1.0).set("w1", 3.0));
    d.law(sg::Key{"trees"}, "disk", sg::Params{}.set("spacing", 3.0).set("seed", 4.0));
    d.law(sg::Key{"moss"}, "thomas", sg::Params{}.set("parents", 0.4).set("children", 12.0).set("sd", 1.5));
    d.law(sg::Key{"rain"}, "poisson", sg::Params{}.set("density", 30.0));
    d.law(sg::Key{"leak"}, "bump", sg::Params{}.set("at_x", 20.0).set("at_z", 20.0).set("sd", 3.0).set("amount", 200.0));
    d.law(sg::Key{"all"}, "sum", sg::Params{}.set("of", std::string("rain leak")));
    check(d.faults().empty(), "laws said rightly have no faults");

    const auto law = [&](const char* n) { return sg::law_of(d.law(sg::Key{n})->params); };
    const sg::Law tall = law("tall");
    double v = 0, m = mean_of(tall, 20000);
    for (int i = 0; i < 20000; ++i) v += std::pow(sg::draw(tall, static_cast<uint64_t>(i)) - m, 2.0);
    check(std::fabs(m - 1.7) < 0.005 && std::fabs(std::sqrt(v / 20000) - 0.1) < 0.005, "a normal draws its mean and its spread");
    check(std::fabs(mean_of(law("share"), 20000) - 0.25) < 0.01, "a beta draws its mean (a / (a + b))");
    const sg::Law size = law("size");
    int small = 0, in = 0;
    for (int i = 0; i < 20000; ++i) {
        const double x = sg::draw(size, static_cast<uint64_t>(i));
        small += x < 0.3, in += x >= 0.1 && x <= 2.0;
    }
    check(in == 20000 && small > 14000, "a power law stays in its range, and most of it is small");
    double c = 0, s = 0;
    for (int i = 0; i < 20000; ++i) c += std::cos(sg::draw(law("turn"), static_cast<uint64_t>(i)) - 0.5), s += std::sin(sg::draw(law("turn"), static_cast<uint64_t>(i)) - 0.5);
    check(c / 20000 > 0.9 && std::fabs(s / 20000) < 0.02, "a von Mises gathers round its way");
    check(std::fabs(mean_of(law("which"), 20000) - 0.75) < 0.02, "a choice picks by its weights");
    check(sg::draw(size, 77) == sg::draw(size, 77), "the same law, seed and index: the same draw");

    const sg::Area area{0, 0, 40, 40};
    const auto trees = sg::points(law("trees"), area);
    double nearest = 1e9;
    for (std::size_t i = 0; i < trees.size(); ++i)
        for (std::size_t j = i + 1; j < trees.size(); ++j) nearest = std::min(nearest, std::hypot(trees[i].x - trees[j].x, trees[i].z - trees[j].z));
    check(trees.size() > 60 && nearest >= 3.0 - 1e-9, "blue noise: many, and no two nearer than its spacing");
    // Clumping: of the cells of a grid, how unevenly a cluster fills them
    // against how rain does (the variance over the mean of the counts).
    const auto clumping = [&](const std::vector<sg::Vec3d>& p) {
        std::vector<double> cells(64, 0.0);
        for (const auto& q : p) cells[static_cast<std::size_t>(std::min(7, static_cast<int>(q.z / 5)) * 8 + std::min(7, static_cast<int>(q.x / 5)))] += 1;
        double mu = 0, var = 0;
        for (double k : cells) mu += k;
        mu /= 64;
        for (double k : cells) var += (k - mu) * (k - mu);
        return var / 64 / std::max(mu, 1e-9);
    };
    const auto moss = sg::points(law("moss"), area), rain = sg::points(law("rain"), area);
    check(clumping(moss) > 3.0 * clumping(rain), "a cluster clumps where rain does not");
    check(std::fabs(sg::intensity(d, sg::Key{"all"}, 20, 20, area) - (sg::intensity(d, sg::Key{"rain"}, 20, 20, area) + sg::intensity(d, sg::Key{"leak"}, 20, 20, area))) < 1e-12,
          "intensities add: a sum's is its parts'");
    const auto both = sg::sample(d, sg::Key{"all"}, area);
    check(both.size() == sg::sample(d, sg::Key{"rain"}, area).size() + sg::sample(d, sg::Key{"leak"}, area, "poisson").size() ||
              both.size() == rain.size() + sg::sample(d, sg::Key{"leak"}, area).size(),
          "a sum drawn as drawn: its parts' points together (superposition)");
    const auto near_leak = [](const std::vector<sg::Vec3d>& p) {
        int k = 0;
        for (const auto& q : p) k += std::hypot(q.x - 20, q.z - 20) < 6;
        return static_cast<double>(k) / std::max<std::size_t>(p.size(), 1);
    };
    for (const char* how : {"poisson", "halton", "disk"}) {
        const auto pts = sg::sample(d, sg::Key{"leak"}, area, how, 0.4);
        std::printf("  %s: %zu points, %.0f%% within 6 m of the leak\n", how, pts.size(), 100 * near_leak(pts));
        check(!pts.empty() && near_leak(pts) > 0.6, "a field sampled follows its intensity (most round its cause)");
    }
    double most = 0;
    const auto pic = sg::field_picture(d, sg::Key{"leak"}, area, 32, &most);
    check(pic.size() == 32 * 32 * 4 && pic[(16 * 32 + 16) * 4 + 3] > 240 && pic[0 + 3] < 10 && most > 0, "a field as a picture: white at its cause, dark away from it");

    sg::Element user(sg::Key{"rubble"}, sg::Key{"fixture"});
    sg::law_to(*d.law(sg::Key{"size"}), user);
    const sg::Law carried = sg::law_of(user.params, "size");
    check(carried.what == "power" && sg::draw(carried, 5) == sg::draw(size, 5), "a law carried onto its user draws as it does");

    d.law(sg::Key{"bad"}, "normal", sg::Params{}.set("sd", -1.0));
    d.law(sg::Key{"worse"}, "sum", sg::Params{}.set("of", std::string("rain nothing")));
    d.law(sg::Key{"none"}, "gaussianish");
    check(d.faults().size() == 3, "a spread less than none, a sum of no law, a law of no name: each named");

    sg::Spatial3D room(sg::Key{"room"});
    sg::add_leak(room, sg::Key{"roof"}, {1, 3, 1}, 1.2);
    check(sg::leaks_of(room).size() == 1 && room.faults().empty(), "a leak in a room, said rightly");
    sg::add_leak(room, sg::Key{"dry"}, {1, 3, 1}, 0.0);
    room.params().set("age", 2.0);
    check(room.faults().size() == 2, "a leak that reaches nowhere, and an age past the most: each named");
    std::printf("%s\n", failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}
