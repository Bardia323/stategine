// MotionBricks: a body's motion made as it goes (sg/domains/Bricks.hpp) - all
// but running the model (src/domains/BricksModel.cpp). What a pose is to the
// model and back follows MotionBricks' own SOMA engine (its Apache-2.0
// runtime: motionbricks/model/soma/kernels.py), number for number.
#include "sg/domains/Bricks.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sg::bricks {

namespace {
#include "BricksSoma.inc"

Vec3d add(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3d sub(const Vec3d& a, const Vec3d& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3d scale(const Vec3d& a, double k) { return {a.x * k, a.y * k, a.z * k}; }

// A turn from the three axes it takes x, y and z to (the columns of its matrix).
Quat from_axes(const Vec3d& x, const Vec3d& y, const Vec3d& z) {
    const double m00 = x.x, m10 = x.y, m20 = x.z, m01 = y.x, m11 = y.y, m21 = y.z, m02 = z.x, m12 = z.y, m22 = z.z;
    Quat q;
    const double tr = m00 + m11 + m22;
    if (tr > 0.0) {
        const double s = std::sqrt(tr + 1.0) * 2.0;
        q = {0.25 * s, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s};
    } else if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        q = {(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s};
    } else if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        q = {(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s};
    } else {
        const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
        q = {(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s};
    }
    return normalized(q);
}

// A turn from its 6D form: the first two columns of its matrix (Zhou et al.).
Quat from_6d(const float* f) {
    const Vec3d x = unit({f[0], f[1], f[2]});
    const Vec3d z = unit(cross(x, {f[3], f[4], f[5]}));
    return from_axes(x, cross(z, x), z);
}

// Which way a pose faces, from where its legs stand: square to the line
// across its hips, on the ground (as the model was trained: "hips_pos").
double heading(const Vec3d& right_hip, const Vec3d& left_hip) {
    Vec3d across{right_hip.x - left_hip.x, 0.0, right_hip.z - left_hip.z};
    const double l = length(across);
    if (l <= 1e-6) return 0.0;
    across = scale(across, 1.0 / l);
    const Vec3d forward = cross({0.0, 1.0, 0.0}, across);
    return std::atan2(forward.x, forward.z);
}

// Where the model's joints are in `sk` (each by name), and back: a joint
// whose parent the model does not have keeps its reference turn.
struct Layout {
    std::vector<int> model_to_pose, pose_to_model;
    int right_leg = -1, left_leg = -1;
    bool ok = false;
};
const Layout& layout_of(const Skeleton& sk) {
    thread_local std::unordered_map<const Skeleton*, Layout> cache;
    auto it = cache.find(&sk);
    if (it != cache.end() && it->second.pose_to_model.size() == sk.names.size()) return it->second;
    Layout l;
    l.ok = true;
    for (const std::string& n : model_joints()) {
        const int i = sk.index(n);
        l.model_to_pose.push_back(i);
        l.ok = l.ok && i >= 0;
    }
    l.ok = l.ok && !l.model_to_pose.empty() && l.model_to_pose[0] == 0;
    l.pose_to_model.assign(sk.names.size(), -1);
    if (l.ok)
        for (std::size_t m = 0; m < l.model_to_pose.size(); ++m) {
            const int j = l.model_to_pose[m];
            const int parent = sk.parents[static_cast<std::size_t>(j)];
            if (parent == -1 || l.pose_to_model[static_cast<std::size_t>(parent)] >= 0) l.pose_to_model[static_cast<std::size_t>(j)] = static_cast<int>(m);
        }
    l.right_leg = sk.index("RightLeg"), l.left_leg = sk.index("LeftLeg");
    return cache[&sk] = l;
}

Vec3d aligned(const Xf& align, const Vec3d& p) { return rotate(conj(align.q), sub(p, align.p)); }
}  // namespace

Quat mul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
Quat conj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
Quat normalized(const Quat& q) {
    const double l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return l > 1e-12 ? Quat{q.w / l, q.x / l, q.y / l, q.z / l} : Quat{};
}
Vec3d rotate(const Quat& q, const Vec3d& v) {
    const Vec3d u{q.x, q.y, q.z};
    const Vec3d t = scale(cross(u, v), 2.0);
    return add(add(v, scale(t, q.w)), cross(u, t));
}
Quat slerp(const Quat& a, const Quat& b0, double t) {
    Quat b = b0;
    double d = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (d < 0.0) b = {-b.w, -b.x, -b.y, -b.z}, d = -d;
    if (d > 0.9995)
        return normalized({a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
    const double th = std::acos(std::clamp(d, -1.0, 1.0)), s = std::sin(th);
    const double ka = std::sin((1.0 - t) * th) / s, kb = std::sin(t * th) / s;
    return {a.w * ka + b.w * kb, a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb};
}
Quat about_y(double r) { return {std::cos(r * 0.5), 0.0, std::sin(r * 0.5), 0.0}; }

Xf mul(const Xf& a, const Xf& b) { return {add(a.p, rotate(a.q, b.p)), normalized(mul(a.q, b.q))}; }
Xf inverse(const Xf& a) {
    const Quat qi = conj(a.q);
    return {scale(rotate(qi, a.p), -1.0), qi};
}
Xf blend(const Xf& a, const Xf& b, double t) { return {add(a.p, scale(sub(b.p, a.p), t)), slerp(a.q, b.q, t)}; }

int Skeleton::index(const std::string& name) const {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

const Skeleton& Skeleton::soma() {
    static const Skeleton s = [] {
        Skeleton k;
        for (const SomaJoint& j : kSomaJoints) {
            k.names.emplace_back(j.name);
            k.parents.push_back(j.parent);
            k.reference.push_back({{j.p[0], j.p[1], j.p[2]}, normalized({j.q[3], j.q[0], j.q[1], j.q[2]})});
        }
        return k;
    }();
    return s;
}

const std::vector<std::string>& model_joints() {
    static const std::vector<std::string> n = {"Hips", "Spine1", "Spine2", "Chest", "Neck1", "Neck2", "Head", "Jaw",
                                               "LeftEye", "RightEye", "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
                                               "LeftHandThumbEnd", "LeftHandMiddleEnd", "RightShoulder", "RightArm", "RightForeArm",
                                               "RightHand", "RightHandThumbEnd", "RightHandMiddleEnd", "LeftLeg", "LeftShin",
                                               "LeftFoot", "LeftToeBase", "RightLeg", "RightShin", "RightFoot", "RightToeBase"};
    return n;
}

std::vector<Xf> world_of(const Skeleton& sk, const Pose& pose) {
    std::vector<Xf> w(pose.size());
    for (std::size_t i = 0; i < pose.size(); ++i) {
        const int p = i < sk.parents.size() ? sk.parents[i] : -1;
        w[i] = p < 0 ? pose[i] : mul(w[static_cast<std::size_t>(p)], pose[i]);
    }
    return w;
}

void Motion::sample(double t, Pose& out, std::array<double, 4>* c) const {
    if (frames.empty()) return;
    const int last = static_cast<int>(frames.size()) - 1;
    const double f = t * kRate;
    const int i0 = std::clamp(static_cast<int>(std::floor(f)), 0, last);
    const int i1 = std::min(i0 + 1, last);
    const double k = i0 < last ? std::clamp(f - i0, 0.0, 1.0) : 0.0;
    out.resize(frames[static_cast<std::size_t>(i0)].size());
    for (std::size_t j = 0; j < out.size(); ++j) out[j] = blend(frames[static_cast<std::size_t>(i0)][j], frames[static_cast<std::size_t>(i1)][j], k);
    if (c && !contacts.empty())
        for (int n = 0; n < 4; ++n)
            (*c)[static_cast<std::size_t>(n)] = contacts[static_cast<std::size_t>(i0)][static_cast<std::size_t>(n)] * (1.0 - k) +
                                                contacts[static_cast<std::size_t>(i1)][static_cast<std::size_t>(n)] * k;
}

Xf encode(const Skeleton& sk, const std::array<Pose, 4>& context, const std::vector<Pose>& targets, const Settings& s, Inputs& in) {
    const Layout& l = layout_of(sk);
    const int given = std::clamp(static_cast<int>(targets.size()), 1, kPerToken);
    // The eight poses in the world: the context's four, then the targets', the last repeated.
    std::array<std::vector<Xf>, 8> w;
    for (int f = 0; f < 4; ++f) w[static_cast<std::size_t>(f)] = world_of(sk, context[static_cast<std::size_t>(f)]);
    for (int f = 0; f < 4; ++f) w[static_cast<std::size_t>(4 + f)] = world_of(sk, targets.empty() ? context[3] : targets[static_cast<std::size_t>(std::min(f, given - 1))]);
    // Turned so the last context pose faces the settings' way, on the ground under its hips.
    const auto& now = w[3];
    const double current = heading(now[static_cast<std::size_t>(l.right_leg)].p, now[static_cast<std::size_t>(l.left_leg)].p);
    const double desired = std::atan2(s.face_x, s.face_z);
    const Xf align{{now[0].p.x, 0.0, now[0].p.z}, about_y(current - desired)};
    const int joints = static_cast<int>(model_joints().size());
    for (int f = 0; f < 8; ++f) {
        const int next = f == 3 ? f : std::min(f + 1, 7);
        const auto& a = w[static_cast<std::size_t>(f)];
        const auto& b = w[static_cast<std::size_t>(next)];
        const Vec3d hips = aligned(align, a[0].p), hips_next = aligned(align, b[0].p);
        const double h = heading(aligned(align, a[static_cast<std::size_t>(l.right_leg)].p), aligned(align, a[static_cast<std::size_t>(l.left_leg)].p));
        const double h_next = heading(aligned(align, b[static_cast<std::size_t>(l.right_leg)].p), aligned(align, b[static_cast<std::size_t>(l.left_leg)].p));
        const double dh = std::atan2(std::sin(h_next - h), std::cos(h_next - h));
        float* g = in.global_root[f];
        g[0] = static_cast<float>(hips.x), g[1] = static_cast<float>(hips.y), g[2] = static_cast<float>(hips.z);
        g[3] = static_cast<float>(std::cos(h)), g[4] = static_cast<float>(std::sin(h));
        float* r = in.local_root[f];
        r[0] = static_cast<float>(dh * kRate);
        r[1] = static_cast<float>((hips_next.x - hips.x) * kRate), r[2] = static_cast<float>((hips_next.z - hips.z) * kRate);
        r[3] = static_cast<float>(hips.y);
        const Vec3d root{hips.x, 0.0, hips.z};
        float* p = in.local_poses[f];
        for (int m = 0; m < joints; ++m) {
            const Xf& j = a[static_cast<std::size_t>(l.model_to_pose[static_cast<std::size_t>(m)])];
            if (m > 0) {
                const Vec3d c = sub(aligned(align, j.p), root);
                p[(m - 1) * 3] = static_cast<float>(c.x), p[(m - 1) * 3 + 1] = static_cast<float>(c.y), p[(m - 1) * 3 + 2] = static_cast<float>(c.z);
            }
            const Quat q = mul(conj(align.q), j.q);
            const Vec3d x = rotate(q, {1.0, 0.0, 0.0}), y = rotate(q, {0.0, 1.0, 0.0});
            float* o = p + (joints - 1) * 3 + m * 6;
            o[0] = static_cast<float>(x.x), o[1] = static_cast<float>(x.y), o[2] = static_cast<float>(x.z);
            o[3] = static_cast<float>(y.x), o[4] = static_cast<float>(y.y), o[5] = static_cast<float>(y.z);
        }
        // Given: the context, and as many targets as were. Root rates unknown
        // where nothing follows (the last context pose, the last target).
        const int on = f < kPerToken + given ? 1 : 0;
        in.has_global_root[f] = on, in.has_poses[f] = on;
        in.has_local_root[f] = (f == kPerToken - 1 || f == kPerToken + given - 1) ? 0 : on;
    }
    for (int i = 0; i <= kMaxTokens - kMinTokens; ++i)
        in.allowed[i] = std::find(s.tokens.begin(), s.tokens.end(), kMinTokens + i) != s.tokens.end() ? 1 : 0;
    return align;
}

void decode(const Skeleton& sk, const float* pred, int tokens, const Xf& alignment, Motion& out) {
    const Layout& l = layout_of(sk);
    constexpr int kWidth = 366;
    const int raw = std::clamp(tokens * kPerToken, 0, 64);
    const int n = std::max(0, raw - 2 * kBoundary);
    const int joints = static_cast<int>(model_joints().size());
    const int rot0 = 5 + (joints - 1) * 3;
    out.frames.assign(static_cast<std::size_t>(n), Pose{});
    out.contacts.assign(static_cast<std::size_t>(n), {});
    for (int f = 0; f < n; ++f) {
        const float* row = pred + static_cast<std::size_t>(kBoundary + f) * kWidth;
        for (int c = 0; c < 4; ++c) out.contacts[static_cast<std::size_t>(f)][static_cast<std::size_t>(c)] = std::clamp(static_cast<double>(row[kWidth - 4 + c]), 0.0, 1.0);
        Pose& pose = out.frames[static_cast<std::size_t>(f)];
        pose.resize(sk.names.size());
        // (Its global turns: each joint's local is its parent's undone from its own.)
        pose[0] = mul(alignment, Xf{{row[0], row[1], row[2]}, from_6d(row + rot0)});
        for (std::size_t j = 1; j < sk.names.size(); ++j) {
            const int m = l.pose_to_model[j];
            if (m < 0) {
                pose[j] = sk.reference[j];
                continue;
            }
            const int pm = l.pose_to_model[static_cast<std::size_t>(sk.parents[j])];
            const Quat q = from_6d(row + rot0 + m * 6), pq = from_6d(row + rot0 + pm * 6);
            pose[j] = {sk.reference[j].p, normalized(mul(conj(pq), q))};
        }
    }
}

void correct(const std::array<Pose, 4>& context, const Pose& target, Motion& m) {
    const std::size_t n = m.frames.size();
    if (n < 2) return;
    const Pose& first = m.frames.front();
    const Pose& last = m.frames.back();
    const std::size_t joints = first.size();
    std::vector<Xf> start(joints), end(joints);
    // The root, in the world: its place by the difference, its turn after its own.
    start[0] = {sub(context[3][0].p, first[0].p), normalized(mul(conj(normalized(first[0].q)), normalized(context[3][0].q)))};
    end[0] = {sub(target[0].p, last[0].p), normalized(mul(conj(normalized(last[0].q)), normalized(target[0].q)))};
    for (std::size_t j = 1; j < joints; ++j) start[j] = mul(inverse(first[j]), context[3][j]), end[j] = mul(inverse(last[j]), target[j]);
    const Xf id{};
    for (std::size_t f = 0; f < n; ++f) {
        const double we = static_cast<double>(f) / static_cast<double>(n - 1), ws = 1.0 - we;
        Pose& p = m.frames[f];
        const Xf r = p[0];
        p[0] = {add(add(r.p, scale(start[0].p, ws)), scale(end[0].p, we)), normalized(mul(mul(r.q, slerp(Quat{}, start[0].q, ws)), slerp(Quat{}, end[0].q, we)))};
        for (std::size_t j = 1; j < joints; ++j) p[j] = mul(mul(p[j], blend(id, start[j], ws)), blend(id, end[j], we));
    }
}

void place(std::vector<Pose>& poses, const Vec3d& ground, const Vec3d& facing) {
    // (The model's way of facing is +x: a pose turned to `facing` from there.)
    const double yaw = std::atan2(-facing.z, facing.x);
    const Xf at{{ground.x, 0.0, ground.z}, about_y(yaw)};
    for (Pose& p : poses)
        if (!p.empty()) p[0] = mul(at, p[0]);
}

void at_speed(std::vector<Pose>& poses, double speed) {
    if (poses.size() < 2) return;
    const Vec3d start = poses.front()[0].p, end = poses.back()[0].p;
    const double rate = kRate / static_cast<double>(poses.size() - 1);
    const double now = std::hypot(end.x - start.x, end.z - start.z) * rate;
    if (now <= 1e-6) return;
    const double k = speed / now;
    for (std::size_t f = 1; f < poses.size(); ++f) {
        Vec3d& p = poses[f][0].p;
        p = {start.x + (p.x - start.x) * k, p.y, start.z + (p.z - start.z) * k};
    }
}

// --- playing ---------------------------------------------------------------------

Player::Player(const Skeleton& sk, int history) : sk_(&sk), history_(static_cast<std::size_t>(std::max(history, 4)), sk.reference),
                                                  dts_(history_.size(), 0.0) {}

void Player::seed(const Pose& pose) {
    std::fill(history_.begin(), history_.end(), pose);
    std::fill(dts_.begin(), dts_.end(), 0.0);
}

void Player::record(double dt, const Pose& pose) {
    if (!(dt > 0.0)) return;
    dts_.back() = dt;
    std::rotate(dts_.begin(), dts_.begin() + 1, dts_.end());
    dts_.back() = 0.0;
    std::rotate(history_.begin(), history_.begin() + 1, history_.end());
    history_.back() = pose;
}

std::array<Pose, 4> Player::context() const {
    // Four samples 30 a second apart, ending at the last pose played; before
    // what is kept, its oldest.
    const std::size_t n = history_.size();
    std::vector<double> at(n, 0.0);
    for (std::size_t i = n - 1; i-- > 0;) at[i] = at[i + 1] - dts_[i];
    std::array<Pose, 4> out;
    for (int s = 0; s < 4; ++s) {
        const double t = -(3 - s) / kRate;
        const std::size_t upper = std::min<std::size_t>(static_cast<std::size_t>(std::upper_bound(at.begin(), at.end(), t) - at.begin()), n - 1);
        std::size_t lower = upper > 0 ? upper - 1 : 0;
        const double span = at[upper] - at[lower];
        double k = 0.0;
        if (span > 0.0) k = std::clamp((t - at[lower]) / span, 0.0, 1.0);
        else lower = upper;
        Pose& p = out[static_cast<std::size_t>(s)];
        p.resize(history_[upper].size());
        for (std::size_t j = 0; j < p.size(); ++j) p[j] = blend(history_[lower][j], history_[upper][j], k);
    }
    return out;
}

bool Player::generate(Model& model, const std::vector<Pose>& targets, int early, const Settings& s, std::string* why) {
    Motion made;
    if (!model.generate(*sk_, context(), targets, s, made, why)) return false;
    motion_ = std::move(made);
    time_ = 0.0;
    early_ = std::max(0, early);
    return true;
}

void Player::sample(double dt, Pose& out, std::array<double, 4>* contacts) {
    if (motion_.frames.empty()) {
        out = history_.back();
        return;
    }
    time_ += std::max(0.0, dt);
    motion_.sample(time_, out, contacts);
}

double Player::time_to_transition() const {
    const int end = std::max(0, static_cast<int>(motion_.frames.size()) - 1 - early_);
    return std::max(0.0, end / kRate - time_);
}

}  // namespace sg::bricks
