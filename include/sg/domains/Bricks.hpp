// Stategine - MotionBricks: a body's motion made as it goes.
//
// NVIDIA's MotionBricks (Wang et al. 2026, github.com/NVlabs/motionbricks)
// makes the motion between where a body is and where it is to be: given its
// last four poses (30 a second) and one to four poses to reach (placed in the
// world - ahead along a path, at a chair, crouched by a box), its model says
// the frames between, 18 to 58 of them, and how its feet meet the ground.
// Played, and asked again a little before the end with what was played as
// the new context, the bricks join into a body's motion of any length: it
// walks where it is steered, stops, turns, at the pace it is asked.
//
// Here is all of it but the model: the SOMA skeleton the model knows
// (`Skeleton::soma`), what a pose is to it (`encode`), what it gives back
// (`decode`), the ends made to meet what was asked (`correct`), the targets
// placed (`place`, `at_speed`) and the playing (`Player`). The model itself
// (`Model`) is MotionBricks' SOMA ONNX file, run by ONNX Runtime on the
// processor - its library opened at run time (cmake/onnxruntime.cmake); with
// none, `Model::open` says so and nothing is made. Where the file is:
// `SG_MOTIONBRICKS` (the .onnx), else the build's own copy
// (cmake/motionbricks.cmake). The model is NVIDIA's (the NVIDIA Open Model
// Agreement: commercial use allowed); this code follows MotionBricks'
// Apache-2.0 runtime.
//
// Model space is the engine's: Y up, metres, the ground XZ. A pose is each
// joint's place and turn in its parent's frame - the root's in the world's.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "sg/core/Core.hpp"

namespace sg::bricks {

constexpr double kRate = 30.0;    // frames a second, in and out
constexpr int kPerToken = 4;      // poses a token holds: the context's four, the target's up to four
constexpr int kMinTokens = 6, kMaxTokens = 16;
constexpr int kBoundary = 3;      // frames dropped from each end of what the model says
constexpr int kMaxFrames = kMaxTokens * kPerToken - 2 * kBoundary;  // 58

struct Quat {
    double w = 1.0, x = 0.0, y = 0.0, z = 0.0;
};
Quat mul(const Quat& a, const Quat& b);
Quat conj(const Quat& q);
Quat normalized(const Quat& q);
Vec3d rotate(const Quat& q, const Vec3d& v);
Quat slerp(const Quat& a, const Quat& b, double t);  // the shorter way
Quat about_y(double radians);

// A joint's place and turn: in its parent's frame (the root's in the world's).
struct Xf {
    Vec3d p;
    Quat q;
};
Xf mul(const Xf& a, const Xf& b);
Xf inverse(const Xf& a);
Xf blend(const Xf& a, const Xf& b, double t);

using Pose = std::vector<Xf>;

// A skeleton: its joints' names, parents (-1: the root, which is first) and
// reference pose.
struct Skeleton {
    std::vector<std::string> names;
    std::vector<int> parents;
    Pose reference;
    int index(const std::string& name) const;  // -1: none
    // The SOMA skeleton, 77 joints (MotionBricks' assets/human).
    static const Skeleton& soma();
};
// SOMA standing at ease: its reference (arms held out level) with the arms
// down by its sides, elbows a little bent - what a body is asked to come to
// when it stops, and to pass through on its way (a target in a T stands
// the motion's arms up as it nears it).
const Pose& at_ease();
// Each joint's place and turn in the world, from its pose.
std::vector<Xf> world_of(const Skeleton& sk, const Pose& pose);
// The model's own 30 joints, in its order.
const std::vector<std::string>& model_joints();

// What was made: its frames at kRate, and how each foot meets the ground in
// each (left heel, left toe, right heel, right toe: 0..1).
struct Motion {
    std::vector<Pose> frames;
    std::vector<std::array<double, 4>> contacts;
    double duration() const { return frames.empty() ? 0.0 : (static_cast<double>(frames.size()) - 1.0) / kRate; }
    // The pose `t` seconds in, between its two nearest frames (clamped to it).
    void sample(double t, Pose& out, std::array<double, 4>* contacts = nullptr) const;
};

struct Settings {
    std::vector<int> tokens = {8, 9, 10, 11, 12, 13, 14};  // how long it may make it, in tokens (6..16)
    bool correct = true;       // its ends made to meet the context's last pose and the first target
    bool root_only = false;    // the targets say only where the body is and which way it faces - its limbs as the model makes them (a walk going on)
    double face_x = 1.0, face_z = 0.0;  // the way the last context pose is turned to, in the model's space
};

// The model's inputs, for one request (its tensors' shapes).
struct Inputs {
    float global_root[8][5];
    float local_root[8][4];
    float local_poses[8][267];
    int has_global_root[8], has_local_root[8], has_poses[8];
    int allowed[11];
};
// The poses made the model's inputs; returns how they were turned and moved
// to face the way the settings say (what decoding undoes).
Xf encode(const Skeleton& sk, const std::array<Pose, 4>& context, const std::vector<Pose>& targets, const Settings& s, Inputs& in);
// What the model said (`pred`: 64 frames of 366) made a motion of `sk`'s
// poses: the boundary frames dropped, its root put back where `alignment` took it from.
void decode(const Skeleton& sk, const float* pred, int tokens, const Xf& alignment, Motion& out);
// Its ends made to meet the last context pose and the first target, the
// difference spread over it (`whole`: all of the target; else only where on
// the ground it stands).
void correct(const std::array<Pose, 4>& context, const Pose& target, Motion& m, bool whole = true);

// Poses placed on the ground at `ground` (its y unused), turned to face
// `facing` (in XZ): only the root moves.
void place(std::vector<Pose>& poses, const Vec3d& ground, const Vec3d& facing);
// Their root's ground motion made `speed` metres a second, on the whole
// (the first pose to the last, 30 a second): a target that barely moves is kept.
void at_speed(std::vector<Pose>& poses, double speed);

// The model, run by ONNX Runtime on the processor.
class Model {
public:
    // Opened from the .onnx at `path` (empty: where SG_MOTIONBRICKS or the
    // build says); none, and why, if it cannot be.
    static std::unique_ptr<Model> open(const std::string& path = {}, std::string* why = nullptr);
    // Whether this build can run one at all (ONNX Runtime fetched).
    static bool can();
    ~Model();
    // The motion from `context` (four poses, the last now) to `targets` (one
    // to four poses, the first to be reached), into `out`.
    bool generate(const Skeleton& sk, const std::array<Pose, 4>& context, const std::vector<Pose>& targets, const Settings& s, Motion& out,
                  std::string* why = nullptr);
    // How long the last generation took, milliseconds.
    double last_ms() const { return last_ms_; }

private:
    Model();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    double last_ms_ = 0.0;
};

// Playing: what was made played on, what was played kept as the context of
// the next (its last poses, 30 a second, whatever the frames' rate).
class Player {
public:
    explicit Player(const Skeleton& sk, int history = 32);
    // Every pose of the history this one (standing still in it).
    void seed(const Pose& pose);
    // A pose played, `dt` seconds after the last.
    void record(double dt, const Pose& pose);
    // The context the history makes: four poses at kRate, ending at the last played.
    std::array<Pose, 4> context() const;
    // Made toward `targets` from what was played; played from its start. Its
    // turn to be asked again comes `early` frames before its end.
    bool generate(Model& model, const std::vector<Pose>& targets, int early, const Settings& s = {}, std::string* why = nullptr);
    // On by `dt`, the pose there into `out`.
    void sample(double dt, Pose& out, std::array<double, 4>* contacts = nullptr);
    // Seconds till it should be asked again (0: now).
    double time_to_transition() const;
    double time() const { return time_; }
    const Motion& motion() const { return motion_; }
    const Skeleton& skeleton() const { return *sk_; }

private:
    const Skeleton* sk_;
    std::vector<Pose> history_;  // oldest first
    std::vector<double> dts_;    // from each to the next; the last 0
    Motion motion_;
    double time_ = 0.0;
    int early_ = 0;
};

}  // namespace sg::bricks
