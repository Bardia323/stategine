// Every shader that holds a room's light-from-all-round boxes holds as many
// as the engine hands it (gl::kShaderProbes, GLWorldView::kMaxProbes): the
// scene's, the relight's, the tracer's. (The relight's once held eight while
// the engine handed it twenty-four: its second bounce saw a third of them.)
#include <cstdio>
#include <string>

#include "sg/gl/Shaders.hpp"
#include "../src/gl/trace/TraceShaders.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
bool has(const std::string& s, const std::string& w) { return s.find(w) != std::string::npos; }
}  // namespace

int main() {
    using namespace sg;
    const std::string n = std::to_string(gl::kShaderProbes);
    const std::string scene = gl::scene_fs(), relight = gl::relight_fs(), traced = render::trace::gi_fs();
    check(has(scene, "const int MAX_PROBES = " + n + ";"), "the scene shader holds " + n + " boxes");
    check(has(traced, "const int MAX_PROBES = " + n + ";"), "the tracer holds " + n);
    for (const char* a : {"uBoxAt[", "uBoxHalf[", "uBoxLo[", "uBoxHi["})
        check(has(relight, std::string(a) + n + "]"), std::string("the relight's ") + a + n + "]");
    check(has(relight, "i < " + n + "; ++i"), "and its loop over them runs to " + n);
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
