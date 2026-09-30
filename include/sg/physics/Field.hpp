// Fields are data/computations inside a state, never independent worlds.
// Sources -> value(position, supplied time) -> declared receiver response.
#pragma once
#include "sg/spatial/Index.hpp"
#include <memory>
#include <optional>
#include <string>

namespace sg::field {
using spatial::V3;

struct Value { double scalar=0; V3 vector; };

// Specialized analytic, grid, fluid or PDE machinery implements this interface.
// It must be const and pure in state-derived data and the supplied time.
// Its coordinates and vector result are local to its source's transform.
class Backend {
public:
    virtual ~Backend() = default;
    virtual Value sample(V3 position, double time) const = 0;
};

struct Source {
    enum Shape { Directional, Radial, Plane, Specialized };
    std::string channel;
    Shape shape=Directional;
    spatial::Transform pose;
    Value value;
    V3 normal{0,1,0};
    double strength=1, exponent=0, softening=0.01;
    // Local support. An empty optional means unbounded, never a global object.
    std::optional<spatial::Aabb> bounds;
    spatial::ConvexVolume volume;
    std::shared_ptr<const Backend> backend;
    std::size_t emitter=std::numeric_limits<std::size_t>::max();
    bool enabled=true;
    static Source directional(std::string channel, V3 vector);
    static Source radial(std::string channel, V3 centre, double strength, double exponent=0);
    static Source plane(std::string channel, V3 point, V3 normal, double strength);
    Value sample(V3 position, double time) const;
};

enum class Response { Acceleration, Force, Torque, Scalar, Vector };
struct Receiver {
    std::string channel;
    Response response=Response::Acceleration;
    double gain=1;
    bool operator==(const Receiver& r) const { return channel==r.channel && response==r.response && gain==r.gain; }
};
struct Reading { std::string channel; Response response; Value value; };
struct Result {
    V3 acceleration, force, torque;
    std::vector<Reading> readings; // scalar/vector consumers belong to their state
};

// Derived query cache. No integration, clock or response policy lives here.
class Solver {
public:
    void rebuild(std::vector<Source> sources);
    bool uniform() const { return uniform_result_; }
    Result evaluate(V3 position, double time, const std::vector<Receiver>& receivers,
                    std::size_t self=std::numeric_limits<std::size_t>::max()) const;
private:
    std::vector<Source> sources_;
    std::vector<std::optional<Value>> uniform_;
    spatial::Index local_;
    std::vector<std::size_t> unbounded_;
    bool uniform_result_=false;
};
} // namespace sg::field
