#include "../examples/Fields.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/sg.hpp"
#include <cstdio>
#ifdef SG_PROJECTED_GL
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#endif
namespace sgen { void build_fields(sg::StateGraph&,const sg::dsl::Natives&,sg::dsl::Bindings&); }

int main() {
    int failed=0;
    const auto check=[&](bool ok,const char* name) { std::printf("%s %s\n",ok?"ok":"FAIL",name); failed+=!ok; };
    sg::StateGraph graph; sg::dsl::Bindings bindings;
    sgen::build_fields(graph,sg::examples::field_natives(),bindings);
    check(graph.validate().empty(),"projector, physics and nested worlds are graph reachable");
    auto report=sg::verify(graph);
    for(const auto& issue:report.structure) std::printf("structure: %s\n",issue.c_str());
    check(report.ok(),"projector/field construction obeys the laws");
    sg::Engine engine(graph); engine.set_strict(true); engine.start();
    auto& host=graph.state("host");
    engine.fire(sg::Key{"host.project"}); engine.tick(1.0/60);
    check(graph.embedding("projected")->open && host.element("projection").params.get_or<bool>("open",false),"host arrow opens the existing embedding through its portal");
    check(std::fabs(host.element("projection").params.num("w")-2.8)<1e-10 &&
          std::fabs(host.element("projection").params.num("h")-1.6)<1e-10,"projector geometry owns the declared aperture");
    check(graph.seams().empty() && graph.transitions().empty(),"projection creates neither seam nor traversal permission");
    check(graph.state("physics").element("ball").params.num("x")<2,"Physics arrow moves its own body by the radial field");
    const auto before=host.element("projection").params.stamp();
    engine.fire({"host.aim",sg::Params{}.set("target_x",100.0)});
    check(host.element("projection").params.stamp()==before,"aim changes only the projector until the portal arrow runs");
    engine.fire(sg::Key{"host.project"}); engine.tick(1.0/60);
    check(!graph.embedding("projected")->open,"a cone missing its wall closes through the host arrow");
    engine.fire({"host.aim",sg::Params{}.set("target_x",0.0)}); engine.fire(sg::Key{"host.project"}); engine.tick(1.0/60);
#ifdef SG_PROJECTED_GL
    sg::gl::Window window(320,180,"projected portal and embedded worlds");
    sg::render::GLWorldView view; check(view.prepare(graph).empty(),"all declared views prepare without look problems");
    const auto snapshot=sg::to_text(host);
    const auto realm=sg::to_text(graph.state("realm"));
    const auto inside=sg::to_text(graph.state("inside"));
    const auto facts=sg::dsl::facts(graph);
    for(int i=0;i<3;++i) view.render(dynamic_cast<const sg::Spatial3D&>(host),320,180);
    check(view.times().feed_views==2,"projected portal and its nested 3D feed both render");
    check(snapshot==sg::to_text(host) && realm==sg::to_text(graph.state("realm")) && inside==sg::to_text(graph.state("inside")) && facts==sg::dsl::facts(graph),"rendering changes no state or declared graph structure");
    engine.fire({"host.aim",sg::Params{}.set("target_x",100.0)}); engine.fire(sg::Key{"host.project"}); engine.tick(1.0/60);
    view.bind_feed("projection",dynamic_cast<const sg::Spatial3D*>(graph.find("realm")),320,180);
    view.render(dynamic_cast<const sg::Spatial3D&>(host),320,180);
    check(view.times().feed_views==0,"presentation binding cannot reopen a closed embedding");
    const auto draws=view.times().draws;
    sg::Surface2D unrelated("unrelated",2,2);
    view.bind_surface("projection",&unrelated);
    view.render(dynamic_cast<const sg::Spatial3D&>(host),320,180);
    check(view.times().feed_views==0 && view.times().draws==draws,"an unrelated raster binding cannot draw a portal or expose a stale feed");
#endif
    engine.stop();
    check(sg::verify(graph).ok(),"laws hold after field motion and projector changes");
    return failed?1:0;
}
