// Stategine - the ways a sound takes to the ear, and what it loses on them.
//
// A place is heard from another only through its openings: the seams whose
// `admits` let sound through. Whatever is in another place is heard from the
// last opening on its way, as far off as the whole way is long; each opening
// on it, by how open it is (its doorway's `aperture`, carried to it by a
// functor) and how sharply the way turns there, takes off the top first.
// Air takes a little more, the higher the more (`air`). Inside a place, a
// wall between the sound and the ear lets through what its material does
// (`occlusion`). And a room answers by its own shape and what it is made of
// (`eyring`).
//
// Three bands: low (below ~250 Hz), mid, high (above ~4 kHz).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/StateGraph.hpp"

namespace sg::audio {

struct Bands {
    double low = 1.0, mid = 1.0, high = 1.0;
};

inline Bands operator*(const Bands& a, const Bands& b) { return {a.low * b.low, a.mid * b.mid, a.high * b.high}; }

// What `metres` of air leaves of each band: exp(-a d), a per band 0.0002,
// 0.0017, 0.0182 a metre.
Bands air(double metres);

// What a surface does to sound: how much of each band it takes in (its
// absorption, for a room's reverb), and how much it lets through (a wall
// between a sound and the ear).
struct Acoustic {
    Bands absorb{0.05, 0.06, 0.08};
    Bands transmit{0.1, 0.03, 0.01};
};

// The acoustic numbers of a material, by its name (stone, brick, plaster,
// planks, glass, fabric, sand ...); a generic hard wall for one unknown.
const Acoustic& acoustic(const std::string& material);
bool known_acoustic(const std::string& material);

// How long a room rings, per band: Eyring's T60 = 0.161 V / (-S ln(1 - a)),
// `a` the absorption over all its surfaces, weighted by their areas.
struct Reverberation {
    Bands t60{0.7, 0.7, 0.7};
    double volume = 0.0, surface = 0.0;
};

Reverberation eyring(double volume, const std::vector<std::pair<double, Acoustic>>& surfaces);

// The same for a room (sg::Room): its floor, walls and ceiling from its own
// outline and height, each of the material named. False for a place that is
// not a room.
bool reverberation_of(const State& place, const std::string& floor, const std::string& walls,
                      const std::string& ceiling, Reverberation& out);

// What an opening `aperture` open (0 shut, 1 wide) lets through: a shut door
// still lets some low through, almost none of the top.
Bands through(double aperture);

// What a way turning `angle` (radians, from straight through) at an opening
// keeps: the top bends round a corner worst.
Bands bend(double angle);

// The way from somewhere to the ear.
struct Route {
    bool heard = false;
    double length = 0.0;  // metres, all the way
    Bands bands;          // what its openings and turns leave
    Vec3d toward{1, 0, 0};  // from the ear to the last opening, in the ear's frame (unit)
};

// Every way to the ear through openings that let sound through, worked out
// once for where the ear is: each opening's two sides are the nodes, a way
// across a place between two of them an edge as long as it is straight.
class Paths {
public:
    void build(const StateGraph& g, Key ear_place, const Vec3d& ear);

    // From `at` in `place`. A sound in the ear's own place is not routed.
    Route to(Key place, const Vec3d& at) const;
    // From the nearest opening of `place`: a place as a whole (its bed, its reverb).
    Route to(Key place) const;

    // Every place a way reaches, and how far.
    const std::unordered_map<Key, double>& reached() const { return nearest_; }
    // How long a crossing of the seam between two places fades over; 1 if none.
    double fade_between(Key a, Key b) const;

private:
    struct Opening {
        Key seam;
        std::size_t seam_index = 0;
        Key place[2], portal[2];
        Vec3d pos[2], normal[2];
        double aperture = 1.0, fade = 1.0;
        bool live = false;
    };
    // Arrived at one side of an opening (in that side's place), from the ear.
    struct Landing {
        int opening = -1, side = 0;
        double dist = 0.0;
        Bands bands;
        Vec3d first{1, 0, 0};  // the first opening from the ear, in the ear's frame
    };
    std::vector<Opening> openings_;
    std::unordered_map<Key, std::vector<int>> sides_of_;  // a place's opening sides: 2 * opening + side
    std::unordered_map<Key, std::vector<Landing>> landed_;
    std::unordered_map<Key, double> nearest_;
    Key ear_place_;
    Vec3d ear_;
    uint64_t revision_ = ~uint64_t{0};
};

// What the walls between `from` and `to` in `place` let through: a ray from
// one to the other against the place's own solids, each wall it crosses
// (an element of kind `wall`) letting through what its material does
// (`material` or `mat`, else a generic wall). At most four.
Bands occlusion(const State& place, const Vec3d& from, const Vec3d& to);

}  // namespace sg::audio
