// Stategine - a project's render settings, as the file it ships with.
//
// Every line is optional: what a file does not say keeps the default, what it
// cannot take is said and left as it was, and the file the engine writes for
// a quality reads back to that quality.
#include <cstdio>
#include <string>
#include <vector>

#include "sg/render/Defaults.hpp"

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
    return ok ? 0 : 1;
}
