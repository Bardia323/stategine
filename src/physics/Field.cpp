#include "sg/physics/Field.hpp"
#include <stdexcept>

namespace sg::field {
Source Source::directional(std::string channel,V3 vector) {
    Source s; s.channel=std::move(channel); s.value.vector=vector; return s;
}
Source Source::radial(std::string channel,V3 centre,double strength,double exponent) {
    Source s; s.channel=std::move(channel); s.shape=Radial; s.pose.translation=centre;
    s.strength=strength; s.exponent=exponent; return s;
}
Source Source::plane(std::string channel,V3 point,V3 normal,double strength) {
    if(spatial::length(normal)<1e-12) throw std::invalid_argument("field plane needs a normal");
    Source s; s.channel=std::move(channel); s.shape=Plane; s.pose.translation=point;
    s.normal=spatial::normalize(normal); s.strength=strength; return s;
}
Source Source::box(std::string channel,spatial::Transform pose,V3 half,double strength) {
    Source s; s.channel=std::move(channel); s.shape=Box; s.pose=pose; s.half=half; s.strength=strength; return s;
}
Value Source::sample(V3 position,double time) const {
    if(!enabled) return {};
    if(shape==Directional && !bounds && volume.planes.empty()) {
        const double weight=strength/std::pow(std::max(softening,1e-12),exponent);
        return {value.scalar*weight,pose.vector(value.vector)*weight};
    }
    const V3 p=pose.inverse().point(position);
    if((bounds && !bounds->contains(p)) || !volume.contains(p)) return {};
    Value out=value;
    double distance=0;
    if(shape==Specialized) {
        if(!backend) throw std::invalid_argument("specialized field needs a backend");
        out=backend->sample(p,time);
    } else if(shape==Radial) {
        distance=spatial::length(p);
        out.vector=distance>1e-12?p*(1/distance):V3{};
    } else if(shape==Box) {
        // The nearest point of the box, and the way out from it.
        const V3 q{std::clamp(p.x,-half.x,half.x),std::clamp(p.y,-half.y,half.y),std::clamp(p.z,-half.z,half.z)};
        V3 out_of=p-q;
        distance=spatial::length(out_of);
        if(distance>1e-12) out_of=out_of*(1/distance);
        else {
            // Inside: out by the nearest face.
            const double gaps[3]={half.x-std::fabs(p.x),half.y-std::fabs(p.y),half.z-std::fabs(p.z)};
            const int a=gaps[0]<=gaps[1]&&gaps[0]<=gaps[2]?0:gaps[1]<=gaps[2]?1:2;
            const double c[3]={p.x,p.y,p.z};
            out_of=V3{a==0?(c[0]<0?-1.0:1.0):0.0,a==1?(c[1]<0?-1.0:1.0):0.0,a==2?(c[2]<0?-1.0:1.0):0.0};
            distance=gaps[a];
        }
        out.vector=out_of;
        if(faces) {
            // Each face's strength, as much as the way out is through it.
            const auto& f=*faces;
            const double k=out_of.x*out_of.x*(out_of.x>=0?f[0]:f[1])+out_of.y*out_of.y*(out_of.y>=0?f[2]:f[3])+
                           out_of.z*out_of.z*(out_of.z>=0?f[4]:f[5]);
            out.vector=out_of*k;
        }
    } else if(shape==Plane) {
        const V3 n=spatial::normalize(normal);
        const double d=spatial::dot(p,n); distance=std::fabs(d);
        out.vector=d>1e-12?n:d<-1e-12?-n:V3{};
    }
    const double weight=strength/std::pow(std::max(distance,std::max(softening,1e-12)),exponent);
    out.scalar*=weight; out.vector=pose.vector(out.vector)*weight;
    return out;
}
void Solver::rebuild(std::vector<Source> sources) {
    sources_=std::move(sources); unbounded_.clear();
    uniform_.assign(sources_.size(),std::nullopt);
    uniform_result_=true;
    std::vector<spatial::Index::Entry> entries;
    for(std::size_t i=0;i<sources_.size();++i) {
        const auto& s=sources_[i]; if(!s.enabled) continue;
        if(s.shape==Source::Directional && !s.bounds && s.volume.planes.empty()) uniform_[i]=s.sample({},0);
        if(!uniform_[i] || s.emitter!=std::numeric_limits<std::size_t>::max()) uniform_result_=false;
        if(s.bounds) entries.push_back({i,s.bounds->transformed(s.pose)});
        else unbounded_.push_back(i);
    }
    local_.rebuild(std::move(entries));
}
Result Solver::evaluate(V3 p,double time,const std::vector<Receiver>& receivers,std::size_t self) const {
    Result result;
    std::vector<std::size_t> nearby;
    const auto* candidates=&unbounded_;
    if(local_.size()>0) {
        nearby=local_.query(spatial::Aabb{p,p});
        nearby.insert(nearby.end(),unbounded_.begin(),unbounded_.end());
        std::sort(nearby.begin(),nearby.end()); candidates=&nearby;
    }
    for(const auto& r:receivers) {
        Value sum;
        for(const auto i:*candidates) {
            const auto& s=sources_[i];
            if(s.channel!=r.channel || (self!=std::numeric_limits<std::size_t>::max() && s.emitter==self)) continue;
            const auto v=uniform_[i]?*uniform_[i]:s.sample(p,time); sum.scalar+=v.scalar*r.gain; sum.vector+=v.vector*r.gain;
        }
        switch(r.response) {
        case Response::Acceleration: result.acceleration+=sum.vector; break;
        case Response::Force: result.force+=sum.vector; break;
        case Response::Torque: result.torque+=sum.vector; break;
        case Response::Scalar: case Response::Vector: result.readings.push_back({r.channel,r.response,sum}); break;
        }
    }
    return result;
}
} // namespace sg::field
