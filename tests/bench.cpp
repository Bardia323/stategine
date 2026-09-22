// Stategine - throughput of the hot paths: element lookup, event dispatch,
// functor transport, portal sync.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/sg.hpp"

namespace {

using Clock = std::chrono::steady_clock;

template <typename Fn>
double per_second(const char* what, long iterations, Fn&& fn) {
    const auto t0 = Clock::now();
    fn();
    const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
    const double rate = iterations / secs;
    std::printf("%-34s %9.2f ms  %10.1f k/s\n", what, secs * 1000.0, rate / 1000.0);
    return rate;
}

}  // namespace

int main() {
    constexpr int kBodies = 512;
    constexpr long kFrames = 2000;

    sg::StateGraph graph;
    auto& world = graph.add<sg::Spatial3D>("world");
    std::vector<sg::Key> ids;
    ids.reserve(kBodies);
    for (int i = 0; i < kBodies; ++i) {
        const sg::Key id{"body" + std::to_string(i)};
        ids.push_back(id);
        world.mesh(id, i % 32, 0, i / 32).params.set(sg::keys::vx, 1.0);
    }
    graph.set_initial("world");

    std::printf("elements: %d, morphisms: %zu\n\n", kBodies, world.morphisms().size());

    double sink = 0;
    per_second("element lookup", static_cast<long>(kBodies) * 200, [&] {
        for (int rep = 0; rep < 200; ++rep)
            for (sg::Key id : ids) sink += world.element(id).params.num(sg::keys::x);
    });
    if (sink < 0) std::printf(" ");  // keep the loop from being optimised away

    sg::Engine engine(graph);
    engine.start();
    per_second("frames (512 integrator arrows)", kFrames, [&] {
        engine.run_fixed(1.0 / 60.0, kFrames);
    });

    // Functor transport: a full 3D -> 2D collapse of every body.
    auto& map = graph.add<sg::Spatial2D>("map", 64, 64);
    std::vector<std::pair<sg::Key, sg::Key>> objects;
    for (sg::Key id : ids) {
        const sg::Key tok{id.str() + "_tok"};
        map.sprite(tok, 0, 0);
        objects.emplace_back(id, tok);
    }
    graph.add_lens("collapse", "stamp", "world", "map", objects,
                   sg::transport::swizzle({{sg::keys::x, sg::keys::x},
                                           {sg::keys::y, sg::keys::z}}),
                   sg::transport::swizzle({{sg::keys::x, sg::keys::x},
                                           {sg::keys::z, sg::keys::y}}));
    const sg::Functor* collapse = graph.functor("collapse");

    constexpr long kApplies = 2000;
    per_second("functor apply (512 objects)", kApplies * kBodies, [&] {
        for (long i = 0; i < kApplies; ++i) collapse->apply(world, map);
    });

    // A Live portal: the same transport, driven by the engine every frame.
    world.portal("panel", {0, 2, 0}, 2, 2);
    graph.embed("portal", "world", "panel", "map", "collapse", "stamp", sg::EmbedSync::Live);
    engine.open_embed("portal");
    per_second("frames with a live portal", kFrames, [&] {
        engine.run_fixed(1.0 / 60.0, kFrames);
    });

    return 0;
}
