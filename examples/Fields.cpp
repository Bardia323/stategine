#include "Fields.hpp"
#include "sg/physics/Rigid.hpp"

namespace sg::examples {
namespace {
void aim(State&,Element& projector,Element*,const Event& event) {
    for(const auto key:{"target_x","target_y","target_z"})
        if(event.args.has(key)) projector.params.set(key,event.args.num(key));
}
void projection(State& host,Element& portal,Element*,const Event&) {
    namespace s=spatial;
    const auto& p=host.element("projector").params;
    const s::V3 origin{p.num("x"),p.num("y"),p.num("z")};
    const s::V3 target{p.num("target_x"),p.num("target_y"),p.num("target_z")};
    if(s::length(target-origin)<1e-12) { portal.params.set("open",false); return; }
    const s::V3 forward=s::normalize(target-origin);
    s::V3 right=s::cross({0,1,0},forward);
    if(s::length(right)<1e-12) right=s::cross({1,0,0},forward);
    right=s::normalize(right); const auto up=s::cross(forward,right);
    s::M3 rotation; rotation.a={right.x,up.x,forward.x,right.y,up.y,forward.y,right.z,up.z,forward.z};
    s::Projector projector{{rotation,origin},p.num("half_x"),p.num("half_y"),0.01,100};
    const auto& wall=host.element("wall").params;
    s::Surface surface{{wall.num("x"),wall.num("y"),wall.num("z")+wall.num("sz")/2},{1,0,0},{0,1,0},wall.num("sx")/2,wall.num("sy")/2};
    const auto image=s::project(projector,surface);
    portal.params.set("open",!image.polygon.empty());
    if(image.polygon.empty()) return;
    const double u=(image.u0+image.u1)*0.5,v=(image.v0+image.v1)*0.5;
    const auto centre=surface.centre+surface.u*((2*u-1)*surface.half_u)+surface.v*((2*v-1)*surface.half_v);
    portal.params.set("x",centre.x).set("y",centre.y).set("z",centre.z+0.01)
        .set("w",2*surface.half_u*(image.u1-image.u0)).set("h",2*surface.half_v*(image.v1-image.v0))
        .set("crop_u0",image.u0).set("crop_v0",image.v0).set("crop_u1",image.u1).set("crop_v1",image.v1);
}
void motion(State& state,Element& e,Element*,const Event& event) {
    const double dt=event.args.num("dt"); if(dt<=0) return;
    rigid::World world; const auto& p=state.element("field").params;
    const spatial::V3 centre{p.num("x"),p.num("y"),p.num("z")};
    if(p.get_or<std::string>("kind","")=="radial") world.fields={field::Source::radial("gravity",centre,p.num("strength"))};
    else world.fields={field::Source::plane("gravity",centre,{0,1,0},p.num("strength"))};
    rigid::Body b; b.id=e.id.str(); b.hulls={rigid::Hull::box({}, {0.1,0.1,0.1})};
    b.x={e.params.num("x"),e.params.num("y"),e.params.num("z")};
    b.v={e.params.num("vx"),e.params.num("vy"),e.params.num("vz")}; b.set_mass(e.params.num("mass",1));
    world.add(b); world.step(dt,event.args.num("time")-dt); const auto& result=world.bodies.front();
    e.params.set("x",result.x.x).set("y",result.x.y).set("z",result.x.z)
        .set("vx",result.v.x).set("vy",result.v.y).set("vz",result.v.z);
}
}
dsl::Natives field_natives() {
    dsl::Natives n; n.arrow("aim_projector",aim).arrow("projected_portal",projection).arrow("field_motion",motion); return n;
}
} // namespace sg::examples
