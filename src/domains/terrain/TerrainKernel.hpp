// The terrain's insides: noise, curves, and the passes that make a Land. Not
// the engine's interface - `sg/domains/Terrain.hpp` is.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "sg/domains/Terrain.hpp"

namespace sg::terrain::kernel {

constexpr double kPi = 3.14159265358979323846;

inline Vec3d cross(const Vec3d& a, const Vec3d& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// A seeded stream of numbers in [0, 1).
struct Dice {
    uint64_t s;
    explicit Dice(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull) {}
    double next();
    double spread(double half) { return (next() * 2 - 1) * half; }
};

// Value noise on the plane, smooth, 0..1; and its octaves.
double noise(double x, double z, uint32_t seed);
struct Octaves {
    int oct = 5;
    double gain = 0.5, lac = 2.0;
    bool ridged = false, billow = false;
    uint32_t seed = 1;
};
double fbm(double x, double z, const Octaves& o);  // about 0..1

double smoothstep(double a, double b, double x);
// How far x is within [lo, hi], softened `soft` either side: 1 inside, 0 well out.
double within(double x, double lo, double hi, double soft);

// A curve through points (Catmull-Rom), as points `step` metres apart, each
// with how far along it is.
struct Curve {
    std::vector<Vec3d> p;  // y unused until heights are given
    std::vector<double> s;
};
Curve curve(const std::vector<Vec3d>& through, double step);

// What a recipe asks for that is resolved once the land's heights are all
// made: water, and where things are strewn.
struct WaterAsk {
    enum Kind { Lake, Sea, Swamp, River } kind = Lake;
    std::string name;
    double x = 0, z = 0, r = 0, level = 0;
    bool level_given = false;
    Vec3d shallow{0.30, 0.34, 0.26}, deep{0.03, 0.07, 0.08};
    // A river: its curve, width and the level of its water along it.
    Curve along;
    std::vector<double> surface;
    double width = 0;
};
struct Range {
    bool given = false;
    double lo = -1e30, hi = 1e30;
};
struct Rules {
    Range height, slope, wet, road, water;
    // How ragged its edges are (0..1), over patches `scale` metres across;
    // and how much of where it may lie it covers, in patches (cover < 1).
    double noise = 0, scale = 30, soft = -1, cover = 1;
};
struct LayerAsk {
    Layer layer;
    Rules rules;
};
struct ScatterAsk {
    std::string thing;
    Rules rules;
    double density = 1, spacing = 0, lo = 1, hi = 1, sink = 0.2;
    int count = -1, most = 20000;
    uint32_t seed = 1;
};

// The passes, each on the land as it is.
void erode(Land& land, std::vector<float>& flow, int drops, double strength, uint32_t seed);
void thermal(Land& land, int n, double talus_deg);
void smooth(Land& land, int n);
// A way along a curve: the land levelled to its smoothed profile across
// `width`, blended back over `bank`, and each point's distance from its edge
// kept in land.road. A river cuts a bed `depth` deep, falling downhill, and
// says the level of its water along the curve.
void level_way(Land& land, Curve& c, double width, double bank, double sink, double smooth_m);
void cut_river(Land& land, Curve& c, double width, double depth, std::vector<double>& surface);
// Once the heights are made: the water (meshes, depth maps and the water
// distance map), the wetness, the covering, the road ribbons, the things.
void settle_water(Land& land, const std::vector<WaterAsk>& asks, std::vector<float>& swamp);
void wetness(Land& land, const std::vector<float>& flow, const std::vector<float>& swamp);
void cover(Land& land, const std::vector<LayerAsk>& layers, uint32_t seed);
Road ribbon(const Land& land, const Curve& c, double width);
void strew(Land& land, const std::vector<ScatterAsk>& asks);

// Bytes of a land, and back (for the cache).
std::string to_bytes(const Land& land);
bool from_bytes(const std::string& bytes, Land& land);

}  // namespace sg::terrain::kernel
