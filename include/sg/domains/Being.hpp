// Stategine - a being: a body, its skeleton, and its spirit, as a state.
//
// Every creature is a state of its own - a person, a dog, a bird - and could
// exist with no room to stand in. What it is, it holds as its elements:
//
//   skeleton   `joint`s, each riding its `parent` joint (none: the root) at
//              an offset `x y z` in the parent's frame, turned there by its own
//              rotation (a unit quaternion `qw qx qy qz`), at rest by `rest_*`.
//   body       `part`s - what is seen and touched - each riding a joint at an
//              offset, with a shape (`box`, `cylinder`, `sphere`, or a `model`
//              by name) and a size. Shown in a room by a declared functor
//              (`show`), as any state's things are carried into another's.
//   spirit     its behaviour: one arrow on its line of time (`<id>.live`,
//              driven), and its intentions as events. What it intends is a
//              target - a clip playing (`clip`, sampled and blended across its
//              `layer`s), a joint held (`<id>.turn`), a hand reaching for a
//              point (`goal`, by inverse kinematics) - and its joints *go*
//              there, each at its own `stiffness`: the intention says what
//              must hold, the body's own dynamics, in time, get there.
//   (soul)     what wants - talk, choice - is another state, sending this one
//              intentions (`play`, `reach`, `turn`) by the graph.
//
// Its own clock: the spirit runs at `tempo` = `scale` ^ -`tempo_exp` of the
// time its line is given - a small creature lives faster than a large one -
// and how long it has lived, by its own measure, is `age`.
//
// The spirit's arrow is a function of the being's params and the step's dt,
// as the laws require: clips are text it reads (memoised on the text), layers
// and goals are a fixed set of elements given when it is made, and every
// joint's motion is its own data. After each step every joint's and part's
// pose in the being's frame is resolved (`px py pz pyaw ppitch proll`), for
// whatever shows it.
#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/State.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg {

class StateGraph;

class Being : public State {
public:
    static constexpr int kLayers = 4;
    explicit Being(Key id, double scale = 1.0);
    Key kind() const override { return Key{"being"}; }

    static Key self_id() { return Key{"self"}; }
    // Its intentions, each its own (so many beings never share one).
    Key live_event() const { return Key{id().str() + ".live"}; }    // driven: {dt}
    Key play_event() const { return Key{id().str() + ".play"}; }    // {clip, fade=0.3, speed=1, loop=1, weight=1, alone=1}
    Key stop_event() const { return Key{id().str() + ".stop"}; }    // {clip (empty: all), fade}
    Key reach_event() const { return Key{id().str() + ".reach"}; }  // {goal, x, y, z, fade}: a point in its own frame
    Key release_event() const { return Key{id().str() + ".release"}; }  // {goal, fade}
    Key turn_event() const { return Key{id().str() + ".turn"}; }    // {joint, yaw, pitch, roll (degrees), weight=1}: held so
    Key free_event() const { return Key{id().str() + ".free"}; }    // {joint (empty: all)}: let go
    Key scale_event() const { return Key{id().str() + ".scale"}; }  // {scale}

    // --- what it is made of (before it starts) ------------------------------------
    // A joint riding `parent` (empty: the root) at `offset`, at rest turned by
    // `rest` (degrees, yaw pitch roll), going to its targets at `stiffness`.
    Element& joint(const std::string& name, const std::string& parent, const Vec3d& offset, const Vec3d& rest_deg = {},
                   double stiffness = 24.0);
    // A part riding `joint`: its shape, size, and offset (its middle) in the joint's frame.
    Element& part(const std::string& name, const std::string& joint, const std::string& shape, const Vec3d& size, const Vec3d& offset,
                  const Vec3d& rgb = {0.7, 0.68, 0.64});
    // A clip: keyframes, a line each - `t joint yaw pitch roll` (seconds of
    // its own time, degrees), or `t joint q w x y z` (the turn itself) -
    // looping or not.
    Element& clip(const std::string& name, const std::string& keys, bool loop = true);
    // A goal: the end of a chain of `links` joints ending at `tip`, reached
    // for by inverse kinematics when asked (`reach`).
    Element& goal(const std::string& name, const std::string& tip, int links = 2);
    // A clip read from BVH (motion capture): its joints made where it names
    // ones not there yet, its motion a clip `name`. False, with why, if it
    // cannot be read.
    bool import_bvh(const std::string& text, const std::string& name, std::string* why = nullptr);
    // A model made elsewhere, rigged, read from glTF 2.0 (.gltf or .glb) - the
    // way a being is mostly made: its skin's joints become the skeleton (each
    // at its own translation, at rest by its own rotation), its skinned mesh a
    // `skin` (the file is its content, read by the program's files and
    // memoised on their stamp), its animations clips by their names (rotation
    // and translation keys), to be played, blended, layered, reached over and
    // retimed like any clip. `prefix` before every name it makes, so two
    // models can share a being. False, with why, if it cannot be read.
    struct Files {
        std::function<bool(const std::string& path, std::string& bytes)> read;
        std::function<long long(const std::string& path)> stamp;
    };
    static Files& files();  // the program's (as Texture's reader)
    bool import_gltf(const std::string& path, const std::string& prefix = {}, std::string* why = nullptr);
    // A skin posed as the skeleton is now (linear blend skinning), in the
    // being's frame: triangles, 8 floats a corner. Its bind pose's bounds, if asked.
    std::vector<float> skinned(Key skin, Vec3d* bind_lo = nullptr, Vec3d* bind_hi = nullptr) const;

    // --- read ---------------------------------------------------------------------
    // A joint's or part's pose in the being's own frame, as last resolved.
    Pose pose_of(Key element) const;
    double tempo() const;
    std::vector<Key> parts() const;
    std::vector<Key> joints() const;

    // The spirit's step: `dt` of the line's time, at the being's tempo.
    void live(double dt);

private:
    struct Key3 {
        double t;
        double q[4];
    };
    struct Track {
        std::vector<Key3> keys;
        std::vector<Key3> moves;  // `p` keys: where it stands on its parent (q[0..2])
    };
    struct Parsed {
        uint64_t stamp = ~uint64_t{0};
        double length = 0;
        std::unordered_map<Key, Track> tracks;
    };
    const Parsed& parsed(const Element& clip) const;
    mutable std::unordered_map<Key, Parsed> clips_;
    void resolve();
};

// Its body shown in `host` (a room): one element there for each part, riding
// `anchor` (the host's element where the being stands - it moves them all),
// carried from the being's resolved parts by a declared functor
// (`<being>.body`) that runs whenever the being moves. Returns the functor's
// name, for whoever embeds or applies it.
Key show(StateGraph& g, const Being& being, Key host, Key anchor);
// Its skins shown in `host` as it is posed now: each a model there (`<being>.<skin>`)
// on an element riding `anchor`. A skin is a mesh made again as the being
// moves, so whoever shows it calls this when it has.
void show_skins(Spatial3D& host, const Being& being, Key anchor);

// A person, roughly: hips, spine, chest, neck, head; shoulders, arms,
// forearms, hands; thighs, shins, feet - parts of a mannequin on them; goals
// for both hands and both feet; and clips to start with: `idle`, `walk`,
// `wave`, `nod`. `height` metres tall.
void humanoid(Being& b, double height = 1.75);

}  // namespace sg
