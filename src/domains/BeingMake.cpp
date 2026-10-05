// A being made: a person to start from, and motion read from BVH.
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

#include "sg/domains/Being.hpp"

namespace sg {

namespace {
std::string n(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}
}  // namespace

void humanoid(Being& b, double h) {
    // It faces +x (a pose faces along its x), its left to +z, standing on y = 0.
    // A joint turned by pitch swings what hangs from it forward; by roll, out
    // to its right (a left limb out: roll negative).
    const Vec3d skin{0.76, 0.66, 0.58}, cloth{0.32, 0.36, 0.44}, dark{0.22, 0.2, 0.2};
    b.joint("hips", "", {0, 0.53 * h, 0}, {}, 18);
    b.joint("spine", "hips", {0, 0.06 * h, 0});
    b.joint("chest", "spine", {0, 0.12 * h, 0});
    b.joint("neck", "chest", {0, 0.13 * h, 0});
    b.joint("head", "neck", {0, 0.05 * h, 0});
    for (const auto& [side, s] : {std::pair{std::string("_l"), 1.0}, std::pair{std::string("_r"), -1.0}}) {
        b.joint("arm" + side, "chest", {0, 0.115 * h, s * 0.12 * h}, {0, 0, -s * 4}, 26);
        b.joint("forearm" + side, "arm" + side, {0, -0.17 * h, 0}, {0, 6, 0}, 30);
        b.joint("hand" + side, "forearm" + side, {0, -0.15 * h, 0}, {}, 34);
        b.joint("thigh" + side, "hips", {0, -0.03 * h, s * 0.055 * h}, {}, 22);
        b.joint("shin" + side, "thigh" + side, {0, -0.245 * h, 0}, {}, 26);
        b.joint("foot" + side, "shin" + side, {0, -0.235 * h, 0}, {}, 30);
        b.part("upperarm" + side, "arm" + side, "cylinder", {0.055 * h, 0.17 * h, 0.055 * h}, {0, -0.085 * h, 0}, cloth);
        b.part("lowerarm" + side, "forearm" + side, "cylinder", {0.045 * h, 0.15 * h, 0.045 * h}, {0, -0.075 * h, 0}, skin);
        b.part("palm" + side, "hand" + side, "box", {0.035 * h, 0.06 * h, 0.02 * h}, {0, -0.03 * h, 0}, skin);
        b.part("upperleg" + side, "thigh" + side, "cylinder", {0.075 * h, 0.245 * h, 0.075 * h}, {0, -0.1225 * h, 0}, cloth);
        b.part("lowerleg" + side, "shin" + side, "cylinder", {0.06 * h, 0.235 * h, 0.06 * h}, {0, -0.1175 * h, 0}, cloth);
        b.part("shoe" + side, "foot" + side, "box", {0.13 * h, 0.035 * h, 0.05 * h}, {0.035 * h, -0.0175 * h, 0}, dark);
        b.goal("hand" + side, "hand" + side, 2);
        b.goal("foot" + side, "foot" + side, 2);
    }
    b.part("pelvis", "hips", "box", {0.11 * h, 0.07 * h, 0.17 * h}, {0, 0.0, 0}, cloth);
    b.part("belly", "spine", "box", {0.1 * h, 0.12 * h, 0.15 * h}, {0, 0.06 * h, 0}, cloth);
    b.part("torso", "chest", "box", {0.12 * h, 0.14 * h, 0.21 * h}, {0, 0.065 * h, 0}, cloth);
    b.part("throat", "neck", "cylinder", {0.04 * h, 0.05 * h, 0.04 * h}, {0, 0.025 * h, 0}, skin);
    b.part("skull", "head", "sphere", {0.12 * h, 0.13 * h, 0.11 * h}, {0.005 * h, 0.06 * h, 0}, skin);

    // To start with: breathing, walking, waving, nodding (seconds; degrees).
    b.clip("idle",
           "0 chest 0 0 0\n1.4 chest 0 1.5 0\n2.8 chest 0 0 0\n"
           "0 head 0 0 0\n1.4 head 4 -1 0\n2.8 head 0 0 0\n"
           "0 arm_l 0 0 -4\n1.4 arm_l 0 2 -5\n2.8 arm_l 0 0 -4\n0 arm_r 0 0 4\n1.4 arm_r 0 2 5\n2.8 arm_r 0 0 4\n");
    std::string walk;
    const auto key = [&](double t, const std::string& j, double y, double p, double r) { walk += n(t) + " " + j + " " + n(y) + " " + n(p) + " " + n(r) + "\n"; };
    for (int i = 0; i <= 4; ++i) {
        const double t = i * 0.25, s = std::cos(i * 3.14159265358979 * 0.5);  // 1, 0, -1, 0, 1
        key(t, "thigh_l", 0, 26 * s, 0), key(t, "thigh_r", 0, -26 * s, 0);
        key(t, "shin_l", 0, i % 2 ? -42 * (s > 0 ? 1 : 0.15) - 10 : -6, 0);
        key(t, "shin_r", 0, i % 2 ? -42 * (s < 0 ? 1 : 0.15) - 10 : -6, 0);
        key(t, "foot_l", 0, 8 * s, 0), key(t, "foot_r", 0, -8 * s, 0);
        key(t, "arm_l", 0, -22 * s, -5), key(t, "arm_r", 0, 22 * s, 5);
        key(t, "forearm_l", 0, 14, 0), key(t, "forearm_r", 0, 14, 0);
        key(t, "hips", 4 * s, 0, 0), key(t, "chest", -6 * s, 2, 0);
    }
    b.clip("walk", walk);
    b.clip("wave",
           "0 arm_r 0 0 155\n0 forearm_r 0 0 -30\n0.4 forearm_r 0 0 25\n0.8 forearm_r 0 0 -30\n1.2 forearm_r 0 0 25\n1.6 forearm_r 0 0 -30\n"
           "0 head 8 0 0\n1.6 head 8 0 0\n");
    b.clip("nod", "0 neck 0 0 0\n0.3 neck 0 -16 0\n0.6 neck 0 0 0\n0.9 neck 0 -16 0\n1.2 neck 0 0 0\n", false);
}

bool Being::import_bvh(const std::string& text, const std::string& name, std::string* why) {
    // HIERARCHY: joints, their offsets and the channels each frame gives them;
    // MOTION: the frames. Lengths in centimetres if they look like it.
    std::istringstream in(text);
    std::string w;
    struct J {
        std::string name, parent;
        Vec3d off;
        std::vector<std::string> ch;
    };
    std::vector<J> js;
    std::vector<std::string> stack;
    std::string last;
    bool site = false;
    double frame_time = 1.0 / 30;
    int frames = 0;
    while (in >> w) {
        if (w == "ROOT" || w == "JOINT") {
            J j;
            in >> j.name;
            j.parent = stack.empty() ? "" : stack.back();
            js.push_back(j);
            last = j.name;
        } else if (w == "End") {
            in >> w;  // Site
            site = true;
        } else if (w == "{") {
            stack.push_back(site ? std::string("~site") : last);
        } else if (w == "}") {
            if (!stack.empty()) stack.pop_back();
            site = false;
        } else if (w == "OFFSET") {
            Vec3d o;
            in >> o.x >> o.y >> o.z;
            if (!site && !js.empty() && stack.size() && stack.back() == js.back().name && js.back().ch.empty()) js.back().off = o;
            else if (!site)
                for (J& j : js)
                    if (j.name == stack.back()) j.off = o;
        } else if (w == "CHANNELS") {
            int c = 0;
            in >> c;
            for (int i = 0; i < c; ++i) {
                in >> w;
                for (J& j : js)
                    if (j.name == stack.back()) j.ch.push_back(w);
            }
        } else if (w == "MOTION") {
            break;
        }
    }
    while (in >> w) {
        if (w == "Frames:") in >> frames;
        else if (w == "Time:") {
            in >> frame_time;
            break;
        }
    }
    if (js.empty() || frames <= 0) {
        if (why) *why = "no joints, or no frames";
        return false;
    }
    double big = 0;
    for (const J& j : js) big = std::max({big, std::fabs(j.off.x), std::fabs(j.off.y), std::fabs(j.off.z)});
    const double unit = big > 5.0 ? 0.01 : 1.0;
    for (const J& j : js)
        if (!find(Key{j.name})) joint(j.name, j.parent, j.off * unit);
    std::string keys;
    const double d2r = 3.14159265358979323846 / 180.0;
    for (int f = 0; f < frames; ++f)
        for (const J& j : js) {
            // Its rotations in the order given, each about its own axis.
            double rot[3] = {0, 0, 0};
            char order[3] = {0, 0, 0};
            int k = 0;
            for (const std::string& c : j.ch) {
                double v = 0;
                in >> v;
                if (c.find("rotation") != std::string::npos && k < 3) order[k] = c[0], rot[k++] = v;
            }
            // Composed as a turn: about X, Y, Z in the order the file says.
            double qw = 1, qx = 0, qy = 0, qz = 0;
            for (int i = 0; i < k; ++i) {
                const double a = rot[i] * d2r * 0.5, s = std::sin(a), c = std::cos(a);
                const double bx = order[i] == 'X' ? s : 0, by = order[i] == 'Y' ? s : 0, bz = order[i] == 'Z' ? s : 0;
                const double nw = qw * c - qx * bx - qy * by - qz * bz, nx = qw * bx + qx * c + qy * bz - qz * by,
                             ny = qw * by - qx * bz + qy * c + qz * bx, nz = qw * bz + qx * by - qy * bx + qz * c;
                qw = nw, qx = nx, qy = ny, qz = nz;
            }
            // Kept as the turn itself (a clip's `q` key), exactly.
            keys +=n(f * frame_time) + " " + j.name + " q " + n(qw) + " " + n(qx) + " " + n(qy) + " " + n(qz) + "\n";
        }
    clip(name, keys, true);
    return true;
}

}  // namespace sg
