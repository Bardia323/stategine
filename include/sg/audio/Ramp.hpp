// Stategine - a change of gain, as a ramp in dB with a shape.
//
// Every change of a voice's gain goes from where it is to where it is wanted
// over a time, along a shape: linear, an S (3t^2 - 2t^3: no corner at either
// end), log (quick at first) or exp (slow at first). It moves in dB, so a
// fade sounds even all the way down, and is turned into a gain only where
// it is heard (10^(dB/20)). What moves it on is time the caller hands it -
// the sounding state's own, so a paused world's ramps wait with it.
#pragma once

#include <cmath>
#include <string>

namespace sg::audio {

enum class Shape { Linear, S, Log, Exp };

// The shape a word names ("linear", "s", "log", "exp"); S otherwise.
Shape shape_of(const std::string& word);

// How far along, 0..1, a ramp of `shape` is at `t` of its way.
double ease(Shape shape, double t);

// Below this, nothing is heard.
inline constexpr double kSilent = -96.0;

inline double db_to_gain(double db) { return db <= kSilent ? 0.0 : std::pow(10.0, db / 20.0); }
inline double gain_to_db(double g) { return g <= 1.6e-5 ? kSilent : std::fmax(kSilent, 20.0 * std::log10(g)); }

struct Ramp {
    double from = kSilent, to = kSilent;
    double t = 1.0;        // how far along, 0..1
    double seconds = 0.12; // how long the whole of it takes
    Shape shape = Shape::S;

    // Where it is now, in dB.
    double now() const;
    // Head for `db` from where it is now, over `secs`. The same target again
    // changes nothing: a ramp under way keeps going.
    void toward(double db, double secs, Shape s);
    // Put it straight there.
    void set(double db);
    // `dt` seconds of the time it follows.
    void advance(double dt);
    bool settled() const { return t >= 1.0; }
};

}  // namespace sg::audio
