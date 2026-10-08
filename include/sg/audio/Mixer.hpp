// Stategine - the mixer: the world's sound, heard from where the ear is.
//
// Like a renderer, it reads the states and owns only what it takes to be
// heard: voices, their delay lines and filters, a reverb for each place that
// can be heard, the buses. What sounds, how loud, how it is heard with
// distance, how a place answers - all of that is the states' (Sound.hpp);
// the mixer changes nothing in the world, and a world with no mixer is the
// same world.
//
// On the game's thread, once a frame:
//
//   mixer.listen(graph);          what states say is heard as it is said
//   mixer.read(engine, dt);       the ear, every sound, every place's look
//
// and on the sound thread, as often as the device asks: `render`. The samples
// come from the program (`Samples`: a fresh take of a one-shot each time it
// is asked for, a loop's long take); what fills itself - a record, a tape -
// is a stream it hands its samples to.
//
// What it keeps from one read to the next is only presentation: each voice's
// ramp (Ramp.hpp, moved on by its own state's time, so a paused world's
// sounds wait with it), each look's fade (moved on by `dt`, the shell's
// interval, which is never the world's time), and the derived ways to the ear.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "sg/audio/Curve.hpp"
#include "sg/audio/Paths.hpp"
#include "sg/audio/Ramp.hpp"
#include "sg/audio/Sound.hpp"

namespace sg {
class Engine;
}

namespace sg::audio {

// Mono samples, at the samples' rate.
using Take = std::shared_ptr<const std::vector<float>>;

// Where the sounds come from.
struct Samples {
    int rate = 48000;
    // A one-shot, a fresh take each time it is asked for; null if there is none.
    std::function<Take(const std::string& name)> shot;
    // A loop's take, closed on itself; null if there is none.
    std::function<Take(const std::string& name)> loop;
};

// What fills itself: `frames` stereo frames, interleaved, -1..1. Called on the sound thread.
using Fill = std::function<void(float* lr, int frames)>;

// Where the ears are: a place, a point in it, and which way they face.
struct Ear {
    Key place;
    Vec3d pos;
    Vec3d facing{1, 0, 0};
};

// A voice as the last read meant it: what plays, how loud and from where.
struct Voice {
    enum class Kind { Shot, Loop, Stream };
    std::string id;
    Kind kind = Kind::Loop;
    uint64_t serial = 0;  // a one-shot's, in the order they were said
    Take take;
    int stream = -1;
    double pitch = 1.0;
    double sync = -1.0;    // a loop on its state's time: seconds into it; -1 free
    double level = kSilent;  // dB: its gain as eased, and its bus
    double gl = 0, gr = 0, dl = 0, dr = 0;  // each ear's gain and delay (samples)
    double cut = 20000.0, send = 0.0, width = 0.0;
    Bands bands;
    int echo = -1;         // which reverb it sends to
    double priority = 1.0;
};

// A place's reverb, as the last read meant it.
struct Echo {
    Key place;
    double size = 1.0, decay = 0.7, damp = 0.5, wet = 0.3;
    double level = 0.0;  // dB, what reaches the ear of it
    Bands bands;
};

class Mixer {
public:
    explicit Mixer(Samples samples, int voices = 32);
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // --- the game's thread ---------------------------------------------------------
    // Hear what every state says: one listener on each said event, once.
    void listen(StateGraph& g);
    // Read the world: the ear is the eye of the place one is in.
    void read(const Engine& engine, double dt);
    void read(const StateGraph& g, const Ear& ear, double dt);
    static Ear ear_of(const Engine& engine);

    // A one-shot told from outside the graph: what an observer of a said
    // event heard. Heard at `at` in `place`'s frame.
    void shot(const std::string& name, Key place, const Vec3d& at, double gain = 1.0, double pitch = 1.0, Bus bus = Bus::Sfx);
    // A stream: its samples, and where it is heard from.
    int stream(Fill fill);
    void place(int stream, Key place, const Vec3d& at, double gain, double width = 0.6, Bus bus = Bus::Music);
    void master(double gain);

    // What the last read meant, to look at.
    const std::vector<Voice>& voices() const;
    const std::vector<Echo>& echoes() const;
    double bus_db(Bus b) const;

    // --- the sound thread ----------------------------------------------------------
    void render(float* lr, int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sg::audio
