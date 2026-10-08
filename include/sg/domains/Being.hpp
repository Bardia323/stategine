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
//              `layer`s, each over the joints its `mask` names, or added to
//              what the others play when it is `additive`), a joint held
//              (`<id>.turn`), a hand reaching for a point or a head looking
//              at one (`goal`, by inverse kinematics) - and its joints *go*
//              there, each at its own `stiffness`: the intention says what
//              must hold, the body's own dynamics, in time, get there.
//   led        its joints given over, by how much it gives itself (`lead`), to
//              a pose something else holds out to it in its own frame
//              (`lead_q` on a joint, `lead_t` where one travels): another
//              being's motion carried across (`retarget`), or a ragdoll's
//              bones (Ragdoll.hpp). Under it the being still means what it
//              means (`aq`, `ax ay az`: its pose as its clips and holds say,
//              and `awx awy awz`: how fast its clips turn each joint then, in
//              radians a second of its line), for whatever leads it to aim at.
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
    // {clip, fade=0.3, speed=1, loop=1, weight=1, alone=1, mask, additive}: `mask` names
    // joints and their weights (`spine 1, hips 0`: a joint not named takes its
    // parent's, a root 1); `additive` ("rest" or "first") adds the clip's turn
    // away from that pose to what the other layers play.
    Key play_event() const { return Key{id().str() + ".play"}; }
    Key stop_event() const { return Key{id().str() + ".stop"}; }    // {clip (empty: all), fade}
    // {goal, x, y, z, fade, pole_x, pole_y, pole_z, soften, twist}: a point in its own
    // frame. A chain of two is solved exactly: its middle bends toward the
    // pole if one is given (else as it bends now), turned `twist` degrees
    // about the line to the point; past `soften` (0..1) of its full length it
    // straightens slowly, never snapping.
    Key reach_event() const { return Key{id().str() + ".reach"}; }
    // {goal, x, y, z, fade, pole_x, pole_y, pole_z}: the goal's tip looks at a point, the
    // turn shared down its chain (the goal's `shares`, from the top to the
    // tip; the tip's whole), its up turned toward the pole if one is given.
    Key look_event() const { return Key{id().str() + ".look"}; }
    Key release_event() const { return Key{id().str() + ".release"}; }  // {goal, fade}
    Key turn_event() const { return Key{id().str() + ".turn"}; }    // {joint, yaw, pitch, roll (degrees), weight=1}: held so
    Key free_event() const { return Key{id().str() + ".free"}; }    // {joint (empty: all)}: let go
    Key scale_event() const { return Key{id().str() + ".scale"}; }  // {scale}
    Key steer_event() const { return Key{id().str() + ".steer"}; }  // {blend, x, y}: where in a blend, eased there
    Key lead_event() const { return Key{id().str() + ".lead"}; }    // {weight=1, fade=0.3}: how far it follows what leads it

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
    // A blend: clips played together, each by how near a point (`x`, `y`)
    // is to its own - `points` a line a clip, `clip x [y]` - gone round in
    // step (one phase for all). Played by its name like a clip (`play`),
    // steered by `steer`, and eased there at `rate`: idle 0 / walk 1.4 /
    // run 3.5 on speed, or walks forward, back and aside on a plane.
    Element& blend(const std::string& name, const std::string& points, double x = 0, double y = 0, double rate = 6.0);
    // A goal: the end of a chain of `links` joints ending at `tip`, reached
    // for by inverse kinematics when asked (`reach`).
    // Looked with (`look`), the tip's own turn and `links` above it share the
    // turn: its forward is `fwd_x/y/z` and its up `up_x/y/z`, both in the
    // being's frame as it is bound (default +x and +y).
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
        // A picture's bytes (PNG, JPEG ...) as pixels, RGBA, rows top first -
        // for a skin's own picture; none set, skins are worn plain.
        std::function<bool(const std::string& bytes, int& w, int& h, std::vector<unsigned char>& rgba)> decode;
    };
    static Files& files();  // the program's (as Texture's reader)
    bool import_gltf(const std::string& path, const std::string& prefix = {}, std::string* why = nullptr);
    // A skin posed as the skeleton is now (linear blend skinning), in the
    // being's frame: triangles, 8 floats a corner.
    std::vector<float> skinned(Key skin) const;
    // The picture a skin wears by its uvs (its material's base colour), as
    // read with it; null if it wears none.
    const std::vector<unsigned char>* skin_picture(Key skin, int& w, int& h) const;

    // --- read ---------------------------------------------------------------------
    // A joint's or part's pose in the being's own frame, as last resolved.
    Pose pose_of(Key element) const;
    // A clip's turn of one joint (w x y z) at `t` seconds of its own time, as
    // it is played - keys found by halving, a cubic one by its tangents.
    // False if the clip does not turn the joint.
    bool sample(const std::string& clip, Key joint, double t, double out[4]) const;
    double tempo() const;
    std::vector<Key> parts() const;
    std::vector<Key> joints() const;

    // --- its size, its facing, its floor (BeingFit.cpp) --------------------------------
    // How far it reaches up and down as it is posed now - its skins, else its
    // parts, else its joints - and how tall that is.
    void extent(double& low, double& high) const;
    double extent_low() const;
    double height() const;
    // Which way it faces across the floor: from its ankles to its toes.
    Vec3d facing() const;
    // Made `k` times its size (every bone, part, travel and skin); turned
    // `yaw` about the up (its roots, and every key that moves them); raised
    // `dy` (its roots). Before it starts: they change what it is made of.
    void resize(double k);
    void face(double yaw);
    void lift(double dy);

    // The spirit's step: `dt` of the line's time, at the being's tempo.
    void live(double dt);

    // Its own account: a joint whose meant place (`ax`) is not where its
    // parent's meant place and turn put it by its own bone - a meant pose
    // taken from a body it no longer is.
    std::vector<std::string> faults() const override;

private:
    struct Key3 {
        double t;
        double q[4];
        // A cubic key (glTF's CUBICSPLINE): its tangents in and out, in the
        // value's units a second, and the way to the next is Hermite's.
        double in[4] = {0, 0, 0, 0}, out[4] = {0, 0, 0, 0};
        bool cubic = false;
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
    // A track's turn (w x y z) and place (x y z) at phase `ph`.
    static void turn_at(const Track& tr, double ph, bool loop, double length, double out[4]);
    static void move_at(const Track& tr, double ph, double out[3]);
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

// `model` fitted to `reference`: as tall, facing the same way, standing on
// the same floor (as its first clip that carries its root stands it, if one
// does) - a model made anywhere (in centimetres, Z up, facing +z) put where
// the reference body would be. The engine's humanoid is a reference
// any game can make; so is any being it already has.
void fit(Being& model, const Being& reference);

// The joints of `from` that `to` has too: by name, or by name without a
// rig's namespace (`mixamorig:Hips` is `Hips`).
std::vector<std::pair<Key, Key>> same_joints(const Being& from, const Being& to);
// `from`'s motion carried onto `to`: a functor (`<from>.to.<to>`, kept - the
// engine carries it whenever `from` moves) taking each joint of `from` to
// the same joint of `to` as what leads it - the turn away from `from`'s
// bind pose put on `to`'s, and a travelling joint's travel scaled by its
// size. Two bodies of one skeleton but other sizes, bone axes or rest poses
// dance one dance. `to` follows as far as it gives itself (`lead`); one
// source may lead many. Returns the functor's name.
Key retarget(StateGraph& g, const Being& from, const Being& to, Key name = {});

// Which keys of a track can go: `values` is `width` numbers a key - a turn
// (4: w x y z, slerped) or a place (3, straight between) - at `times`. A key
// goes when its neighbours, joined, still put every key between them within
// `tolerance` metres of where it was, measured `reach` metres from the joint
// (its furthest descendant) for a turn; again until none goes. The first and
// last are always kept. Returns the indices kept, in order.
std::vector<std::size_t> kept_keys(const std::vector<double>& times, const std::vector<double>& values, int width, double reach, double tolerance);

// A person, roughly: hips, spine, chest, neck, head; shoulders, arms,
// forearms, hands; thighs, shins, feet - parts of a mannequin on them; goals
// for both hands and both feet; and clips to start with: `idle`, `walk`,
// `wave`, `nod`. `height` metres tall.
void humanoid(Being& b, double height = 1.75);

}  // namespace sg
