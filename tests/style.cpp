// A style laid under a look (sg/gl/Style.hpp): what the look says stays its
// own; what it does not is the style's; a world's own lines go to the world;
// the pixel style changes the composite's shader; what cannot be read is
// said, never a failure.
#include <cstdio>
#include <string>
#include <vector>

#include "sg/core/State.hpp"
#include "sg/gl/Style.hpp"

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}
double value(const sg::LookState& l, sg::Key pass, const char* k, double fallback = -1.0) {
    const sg::Element* e = l.find(pass);
    return e ? e->params.num(sg::Key{k}, fallback) : fallback;
}
}  // namespace

int main() {
    using namespace sg;
    {
        LookState look(Key{"room.air"});
        look.uniform(passes::composite, "uSharpen", 0.2);
        State world(Key{"room"});
        world.params().set("probe_cell", 2.0);
        std::vector<std::string> problems;
        check(gl::apply_style(look, &world, "photoreal", {}, &problems), "the engine's photoreal style is found");
        check(value(look, passes::composite, "uSharpen") == 0.2, "what the look says is its own: its sharpening kept");
        check(value(look, passes::composite, "uBarrel") == 0.06, "what it does not say is the style's: the lens's bow");
        check(value(look, passes::scene, "trace.gi") == 0.0, "a setting of the scene's: nothing traced");
        check(world.params().num(Key{"probe_cell"}) == 2.0, "the world's own probe boxes kept");
        check(problems.empty(), "nothing it could not read");
    }
    {
        LookState look(Key{"room.air"});
        State world(Key{"room"});
        gl::apply_style(look, &world, "photoreal");
        check(world.params().num(Key{"probe_cell"}, 0.0) == 1.3, "a world that says none takes the style's probe boxes");
        LookState again(Key{"room.air2"});
        gl::apply_style(again, nullptr, "photoreal");
        check(value(look, passes::composite, "uSharpen") == value(again, passes::composite, "uSharpen"), "laid twice, the same");
    }
    {
        LookState look(Key{"old.air"});
        gl::apply_style(look, nullptr, "psx");
        const Element* e = look.find(passes::composite);
        check(e && e->params.has(look_keys::fs), "the psx style draws the composite with the pixel shader");
        check(value(look, passes::composite, "uPixels") == 240.0, "at 240 rows");
    }
    {
        LookState look(Key{"x.air"});
        std::vector<std::string> problems;
        check(!gl::apply_style(look, nullptr, "no_such_style", {}, &problems) && !problems.empty(), "a style not found is said, not a failure");
        problems.clear();
        gl::apply_style(look, nullptr, "composite.uVignette = 0.3\nnonsense line =\nscene.trace = x\n", {}, &problems);
        check(value(look, passes::composite, "uVignette") == 0.3 && problems.size() == 2, "a style's text: what it can read taken, the rest said");
    }
    return failures ? 1 : 0;
}
