// The core, compiled entirely on its own: no domain, no renderer, no GL.
// If anything in core/ had grown a dependency on rooms or on pixels, this
// translation unit would not build.
#include "sg/core/Adjunction.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Sheaf.hpp"
#include "sg/core/Typed.hpp"
int main() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("only");
    s.add_element("thing", "kind").params.set("v", 1.0);
    g.set_initial("only");
    sg::Engine e(g);
    e.start();
    e.tick(0.0);
    return sg::verify(g).ok() && sg::interface_defects(g).empty() ? 0 : 1;
}
