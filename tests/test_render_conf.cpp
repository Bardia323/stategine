// Stategine - a project's render settings, as the file it ships with.
//
// Every line is optional: what a file does not say keeps the default, what it
// cannot take is said and left as it was, and the file the engine writes for
// a quality reads back to that quality. And the settings as a state
// (render::Graphics): made from the file, changed by its own arrow, kept to
// what each may be.
#include <cstdio>
#include <string>
#include <vector>

#include "sg/core/Laws.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/render/Defaults.hpp"
#include "sg/render/Graphics.hpp"

namespace {
bool ok = true;
void check(bool c, const char* what) {
    std::printf("[%s] %s\n", c ? "ok" : "FAIL", what);
    ok = ok && c;
}
}  // namespace

int main() {
    using sg::render::Quality;
    const Quality defaults;

    Quality q;
    std::vector<std::string> problems;
    sg::render::read_quality("", q, &problems);
    check(problems.empty() && q.msaa == defaults.msaa && q.shadow_size == defaults.shadow_size, "an empty file keeps every default");

    sg::render::read_quality("# a cheap project\nmsaa = 0\n  shadow_size=1024   # softer\npack = off\nexposure = 1.4\n", q, &problems);
    check(problems.empty(), "settings said are taken, with remarks and spaces anywhere");
    check(q.msaa == 0 && q.shadow_size == 1024 && !q.pack && q.exposure == 1.4f, "and each is what the file says");
    check(q.shadow_budget_mb == defaults.shadow_budget_mb && q.bloom_passes == defaults.bloom_passes, "what it does not say is as it was");

    Quality bad = defaults;
    problems.clear();
    sg::render::read_quality("msaa = lots\nshadows = 7\njust words\nmsaa = 2\n", bad, &problems);
    check(problems.size() == 3, "an unknown key, a value of the wrong kind and a line that is not a setting are each said");
    check(bad.msaa == 2 && bad.shadow_size == defaults.shadow_size, "and the rest of the file is taken all the same");

    Quality odd;
    odd.msaa = 8, odd.shadow_budget_mb = -1, odd.instancing = false, odd.bloom_strength = 0.25f;
    Quality back;
    problems.clear();
    sg::render::read_quality(sg::render::quality_text(odd), back, &problems);
    check(problems.empty() && back.msaa == 8 && back.shadow_budget_mb == -1 && !back.instancing && back.bloom_strength == 0.25f &&
              back.shadow_size == odd.shadow_size && back.pack == odd.pack,
          "the file written for a quality reads back to it");

    // The settings as a state.
    {
        using sg::Key;
        sg::StateGraph g;
        Quality start;
        start.msaa = 0, start.taa = true, start.reflection_scale = 0.5f;
        auto& gfx = g.add<sg::render::Graphics>(Key{"graphics"}, start);
        g.port(gfx.id(), sg::render::Graphics::set_event());
        g.set_initial(gfx.id());
        for (const auto& why : g.validate()) std::printf("  %s\n", why.c_str());
        check(g.validate().empty(), "the settings, a state, are reached by their port");
        check(sg::verify(g).ok(), "and keep the laws: the one arrow sets what it is given");
        const Quality now = gfx.quality();
        check(now.msaa == 0 && now.taa && now.reflection_scale == 0.5f && now.shadow_size == start.shadow_size,
              "made from the quality the file gave");

        const auto set = [&](sg::Params p) {
            gfx.hear(sg::Event{sg::render::Graphics::set_event(), std::move(p)});
            gfx.dispatch_pending();
        };
        const uint64_t s0 = gfx.stamp();
        sg::Params p;
        p.set(Key{"msaa"}, 4.0);
        p.set(Key{"taa"}, std::string("off"));
        p.set(Key{"exposure"}, std::string("1.25"));
        set(std::move(p));
        const Quality changed = gfx.quality();
        check(gfx.stamp() != s0, "a setting changed moves its stamp, for whoever draws by it");
        check(changed.msaa == 4 && !changed.taa && changed.exposure == 1.25f, "what the event names is set, said as numbers or words");
        check(changed.shadow_size == start.shadow_size && changed.reflections == start.reflections && changed.reflection_scale == 0.5f,
              "what it does not name is left");

        sg::Params wild;
        wild.set(Key{"shadow_size"}, 3000.0);
        wild.set(Key{"msaa"}, 5.0);
        wild.set(Key{"reflection_scale"}, 7.0);
        wild.set(Key{"nonsense"}, 1.0);
        wild.set(Key{"bloom_passes"}, std::string("many"));
        set(std::move(wild));
        const Quality kept = gfx.quality();
        check(kept.shadow_size == 4096 && kept.msaa == 4 && kept.reflection_scale == 1.0f && kept.bloom_passes == start.bloom_passes,
              "each kept to what it may be; a word that is no number left as it was");
        check(!gfx.element(sg::render::Graphics::quality_id()).params.has(Key{"nonsense"}), "and what is no setting is not kept");

        sg::Params round;
        sg::render::quality_to_params(odd, round);
        const Quality r = sg::render::quality_from_params(round);
        check(r.msaa == 8 && r.shadow_budget_mb == -1 && !r.instancing && r.bloom_strength == 0.25f, "a quality as params reads back to it");
    }
    return ok ? 0 : 1;
}
