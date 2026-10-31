// MotionBricks (sg/domains/Bricks.hpp): what a pose is to the model and what
// it gives back, held to MotionBricks' own Python engine on one request
// (tests/data/bricks_reference.txt) - the encoding always; the model's motion
// where the model can run (ONNX Runtime and the .onnx: SG_MOTIONBRICKS) -
// and a body walked on by bricks joined one after another.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "sg/domains/Bricks.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}
std::map<std::string, std::vector<double>> read_reference(const std::string& path) {
    std::map<std::string, std::vector<double>> out;
    std::ifstream in(path);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "frame" || key == "contacts") {
            int f = 0;
            ls >> f;
            key += " " + std::to_string(f);
        }
        for (double v; ls >> v;) out[key].push_back(v);
    }
    return out;
}
double most_apart(const float* a, const std::vector<double>& b, std::size_t n) {
    double m = 0.0;
    for (std::size_t i = 0; i < n && i < b.size(); ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return n == b.size() ? m : 1e9;
}
}  // namespace

int main() {
    using namespace sg::bricks;
    const Skeleton& sk = Skeleton::soma();
    check(sk.names.size() == 77 && sk.names[0] == "Hips", "the SOMA skeleton: 77 joints, Hips first");
    bool all = true;
    for (const std::string& n : model_joints()) all = all && sk.index(n) >= 0;
    check(all && model_joints().size() == 30, "every one of the model's 30 joints is in it");

    const auto ref = read_reference(std::string(SG_SOURCE_DIR) + "/tests/data/bricks_reference.txt");
    check(!ref.empty(), "the reference is read");
    std::array<Pose, 4> context{sk.reference, sk.reference, sk.reference, sk.reference};
    Pose ahead = sk.reference;
    ahead[0].p.x += 1.6;
    const std::vector<Pose> targets{ahead};
    Inputs in{};
    encode(sk, context, targets, Settings{}, in);
    const auto apart = [&](const char* key, const float* v, std::size_t n) {
        const auto it = ref.find(key);
        return it == ref.end() ? 1e9 : most_apart(v, it->second, n);
    };
    std::vector<float> masks;
    for (int f = 0; f < 8; ++f) masks.push_back(static_cast<float>(in.has_global_root[f]));
    const double d_root = std::max(apart("global_root_values", &in.global_root[0][0], 40), apart("local_root_values", &in.local_root[0][0], 32));
    const double d_poses = apart("local_poses", &in.local_poses[0][0], 8 * 267);
    check(d_root < 1e-4, "encoded: the root's place, heading and rates as the reference's (" + std::to_string(d_root) + " apart at most)");
    check(d_poses < 1e-4, "encoded: every joint's place and turn as the reference's (" + std::to_string(d_poses) + ")");
    check(apart("has_global_root_values", masks.data(), 8) == 0.0, "which poses are given, as the reference says");

    std::string why;
    auto model = Model::open({}, &why);
    if (!model) {
        std::printf("(no model to run: %s - its motion not checked)\n", why.c_str());
        std::printf("%s\n", failures ? "FAILED" : "all ok");
        return failures ? 1 : 0;
    }
    Motion m;
    const bool made = model->generate(sk, context, targets, Settings{}, m, &why);
    check(made, "the model makes the motion" + (made ? " (" + std::to_string(model->last_ms()) + " ms)" : ": " + why));
    if (made) {
        const auto frames = ref.find("frames");
        check(frames != ref.end() && static_cast<std::size_t>(frames->second[0]) == m.frames.size(),
              "as many frames as the reference: " + std::to_string(m.frames.size()));
        double worst = 0.0;
        for (const auto& [key, v] : ref) {
            if (key.rfind("frame ", 0) != 0) continue;
            const int f = std::stoi(key.substr(6));
            if (f >= static_cast<int>(m.frames.size())) continue;
            const Pose& p = m.frames[static_cast<std::size_t>(f)];
            for (std::size_t j = 0; j < p.size() && j * 7 + 6 < v.size(); ++j) {
                const double* r = &v[j * 7];
                worst = std::max({worst, std::fabs(p[j].p.x - r[0]), std::fabs(p[j].p.y - r[1]), std::fabs(p[j].p.z - r[2])});
                // (A turn and its opposite are one turn.)
                const double d = std::fabs(p[j].q.x * r[3] + p[j].q.y * r[4] + p[j].q.z * r[5] + p[j].q.w * r[6]);
                worst = std::max(worst, 1.0 - std::min(1.0, d));
            }
        }
        check(worst < 2e-3, "decoded and corrected: its first, middle and last frames as the reference's (" + std::to_string(worst) + " apart at most)");
        check(std::fabs(m.frames.back()[0].p.x - ahead[0].p.x) < 1e-6 && std::fabs(m.frames.front()[0].p.x) < 1e-6,
              "it starts where the body stood and ends at the target");

        // Walked on: bricks joined, each asked from what was played, toward a
        // target placed 1.4 m on from where the body is, facing the way.
        Player player(sk);
        player.seed(sk.reference);
        Pose pose;
        double t = 0.0;
        int asked = 0;
        bool ok = true;
        for (int frame = 0; frame < 6 * 60 && ok; ++frame) {
            if (player.time_to_transition() <= 1.0 / 60.0) {
                const sg::Vec3d here = player.context()[3][0].p;
                std::vector<Pose> next{sk.reference};
                place(next, {here.x + 1.4, 0.0, here.z}, {1.0, 0.0, 0.0});
                ok = player.generate(*model, next, 6, Settings{}, &why);
                ++asked;
            }
            player.sample(1.0 / 60.0, pose);
            player.record(1.0 / 60.0, pose);
            t += 1.0 / 60.0;
        }
        const double went = pose.empty() ? 0.0 : pose[0].p.x;
        check(ok && asked >= 3, "six seconds walked by " + std::to_string(asked) + " bricks joined" + (ok ? std::string() : ": " + why));
        check(went > 3.0 && std::fabs(pose[0].p.z) < 1.0, "it went on along +x: " + std::to_string(went) + " m, " + std::to_string(pose[0].p.z) + " m aside");
        check(pose[0].p.y > 0.7 && pose[0].p.y < 1.2, "on its feet: its hips " + std::to_string(pose[0].p.y) + " m up");
    }
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
