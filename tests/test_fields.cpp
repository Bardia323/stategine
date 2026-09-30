#include "sg/physics/Rigid.hpp"
#include <cstdio>

namespace f=sg::field;
using namespace sg::spatial;
using sg::rigid::World;
using sg::rigid::Body;
using sg::rigid::Hull;

// A specialized surface/temperature backend. A grid/PDE solver can expose the
// same interface; these tests do not prescribe its numerical implementation.
class SurfaceTemperature : public f::Backend {
public:
    f::Value sample(V3 p,double time) const override { return {20+time+p.x, {0,-p.y,0}}; }
};
class TimedVector : public f::Backend {
public:
    f::Value sample(V3,double time) const override { return {0,{time,0,0}}; }
};
static Body body(const char* name,V3 at,double mass) {
    Body b; b.id=name; b.x=at; b.hulls={Hull::box({}, {0.1,0.1,0.1})}; b.set_mass(mass); return b;
}
int main() {
    int failed=0;
    const auto check=[&](bool ok,const char* name) { std::printf("%s %s\n",ok?"ok":"FAIL",name); failed+=!ok; };
    f::Solver fields;
    const std::vector<f::Receiver> gravity{{"gravity",f::Response::Acceleration,1}};
    fields.rebuild({f::Source::directional("gravity",{0,-9.81,0}),f::Source::directional("wind",{2,0,0})});
    check(length(fields.evaluate({9,8,7},42,gravity).acceleration-V3{0,-9.81,0})<1e-12,"directional gravity is a named vector field");
    auto radial=f::Source::radial("gravity",{},-4,2);
    fields.rebuild({radial});
    check(length(fields.evaluate({2,0,0},0,gravity).acceleration-V3{-1,0,0})<1e-12 &&
          length(fields.evaluate({},0,gravity).acceleration)==0,"inverse-square radial attraction is finite at its centre");
    auto plane=f::Source::plane("gravity",{}, {0,1,0},-2);
    fields.rebuild({plane});
    check(fields.evaluate({0,3,0},0,gravity).acceleration.y==-2 && fields.evaluate({0,-3,0},0,gravity).acceleration.y==2,"plane gravity attracts from both sides");
    auto local=f::Source::directional("gravity",{1,0,0});
    local.bounds=Aabb{{-1,-1,-1},{1,1,1}}; local.pose.translation={10,0,0};
    local.volume.planes={{{1,0,0},0}};
    fields.rebuild({local});
    check(fields.evaluate({10.5,0,0},0,gravity).acceleration.x==1 && fields.evaluate({9.5,0,0},0,gravity).acceleration.x==0 &&
          fields.evaluate({},0,gravity).acceleration.x==0,"bounded fields obey local convex support and source pose");
    f::Source temperature; temperature.channel="temperature"; temperature.shape=f::Source::Specialized;
    temperature.backend=std::make_shared<SurfaceTemperature>();
    fields.rebuild({temperature});
    const auto result=fields.evaluate({3,2,0},7,{{"temperature",f::Response::Scalar,1},{"temperature",f::Response::Vector,2}});
    check(result.readings.size()==2 && result.readings[0].value.scalar==30 && result.readings[1].value.vector.y==-4 &&
          length(result.acceleration)==0,"specialized scalar/vector samples use supplied time without causing motion");
    fields.rebuild({f::Source::directional("gravity",{0,-3,0}),f::Source::radial("gravity",{},-4),plane});
    check(length(fields.evaluate({3,4,0},0,gravity).acceleration-V3{-2.4,-8.2,0})<1e-12,"directional, radial and planar gravity compose by channel");
    World motion; motion.fields={f::Source::directional("push",{2,0,0})}; motion.substeps=1;
    Body a=body("a",{0,5,0},1), b=body("b",{0,8,0},2), c=body("c",{0,11,0},1);
    a.receives=b.receives={{"push",f::Response::Force,1}}; c.receives.clear();
    motion.add(a); motion.add(b); motion.add(c); motion.step(0.02,5);
    check(std::fabs(motion.find("a")->v.x-2*motion.find("b")->v.x)<1e-12 && motion.find("c")->v.x==0,"forces respect mass and a body can receive no fields");
    World emitted; emitted.fields.clear(); emitted.substeps=1;
    Body source=body("source",{0,0,0},0); source.receives.clear(); source.fields={f::Source::radial("gravity",{},-1)};
    emitted.add(source); emitted.add(body("receiver",{5,0,0},1)); emitted.step(0.02);
    check(emitted.find("receiver")->v.x<0,"a physical object may emit without receiving");
    emitted.teleport(*emitted.find("source"),{10,0,0},{}); emitted.step(0.02);
    check(emitted.find("receiver")->v.x> -1e-6,"moving an emitter moves its field, derived from its pose");
    World self; self.fields.clear(); self.substeps=1;
    auto both=body("both",{4,0,0},1); both.fields={f::Source::directional("gravity",{9,0,0})}; self.add(both); self.step(0.02);
    check(self.find("both")->v.x==0,"an emitter does not respond to its own field");
    World torque; torque.fields={f::Source::directional("spin",{0,0,1})}; torque.substeps=1;
    auto spinning=body("spin",{0,5,0},1); spinning.receives={{"spin",f::Response::Torque,1}};
    torque.add(spinning); torque.step(0.02);
    check(torque.find("spin")->w.z>0 && length(torque.find("spin")->v)==0,"declared torque changes spin without translating the body");
    f::Source timed; timed.channel="push"; timed.shape=f::Source::Specialized; timed.backend=std::make_shared<TimedVector>();
    World timed_motion; timed_motion.fields={timed}; timed_motion.substeps=1;
    auto receiver=body("timed",{0,5,0},1); receiver.receives={{"push",f::Response::Acceleration,1}};
    timed_motion.add(receiver); timed_motion.step(0.02,7);
    check(std::fabs(timed_motion.find("timed")->v.x-0.14/(1+0.02*0.02))<1e-12,"rigid field sampling uses the supplied drive time");
    World default_world, explicit_world;
    explicit_world.fields={f::Source::directional("gravity",{0,-9.81,0})};
    default_world.add(body("fall",{0,20,0},1)); explicit_world.add(body("fall",{0,20,0},1));
    for(int i=0;i<40;++i) { default_world.step(1.0/60); explicit_world.step(1.0/60); }
    check(length(default_world.find("fall")->x-explicit_world.find("fall")->x)==0,"default and explicit directional gravity agree exactly");
    // Restoring the inputs, including body motion, repeats the same derived solve.
    const auto snapshot=default_world.bodies;
    default_world.step(0.02,4); const auto expect=default_world.bodies;
    default_world.bodies=snapshot; default_world.set_contacts({}); default_world.step(0.02,4);
    check(length(default_world.bodies[0].x-expect[0].x)==0,"field machinery has no private clock or unrecoverable motion");
    return failed?1:0;
}
