#include "sg/gl/Crossings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sg::render {

namespace {

constexpr int kGrid = 4;

// The mean change of each cell of a kGrid x kGrid grid, from picture a to b.
std::vector<double> cells(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b, int w, int h) {
    std::vector<double> sum(kGrid * kGrid, 0.0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
            sum[(y * kGrid / h) * kGrid + x * kGrid / w] += std::abs(a[i] - b[i]) + std::abs(a[i + 1] - b[i + 1]) + std::abs(a[i + 2] - b[i + 2]);
        }
    const double n = static_cast<double>(w / kGrid) * (h / kGrid) * 3.0;
    for (double& s : sum) s /= n;
    return sum;
}

double mean(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0.0 : s / static_cast<double>(v.size());
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

}  // namespace

std::vector<std::string> check_crossings(GLWorldView& view, StateGraph& g, const CrossingOptions& o) {
    std::vector<std::string> out;
    for (const Seam& s : g.seams())
        for (std::size_t i = 0; i < s.boundary_a.size() && i < s.boundary_b.size(); ++i)
            for (const bool ab : {true, false}) {
                auto* A = dynamic_cast<Spatial3D*>(g.find(ab ? s.a : s.b));
                auto* B = dynamic_cast<Spatial3D*>(g.find(ab ? s.b : s.a));
                const Key pa = ab ? s.boundary_a[i] : s.boundary_b[i], pb = ab ? s.boundary_b[i] : s.boundary_a[i];
                const Element* door = A ? A->find(pa) : nullptr;
                const Element* far = B ? B->find(pb) : nullptr;
                if (!door || !far || door->kind != kinds::portal) continue;
                // Only a doorway walked out through, and one with a face to
                // walk through (a ball is flown into, not walked).
                if (door->params.num(Key{"walk"}, 0.0) < 0.5 || door->params.num(Key{"leave"}, 1.0) < 0.5 || door->params.has(Key{"ball"})) continue;
                const std::string way = s.name.str() + " (" + A->id().str() + " -> " + B->id().str() + ")";
                const Params keep_a = A->camera().params, keep_b = B->camera().params;
                const Pose at = world_pose(*A, *door);
                const Vec3d in = facing(at), up = up_of(at);
                const double hh = door->params.num(keys::h, 2.0) * 0.5;
                // Facing through it, on the doorway's own ground.
                const Pose ground = compose_pose(Pose{{}, at.yaw, at.pitch, at.roll}, Pose{{}, 3.14159265358979, 0.0, 0.0});
                std::vector<std::vector<unsigned char>> frames;
                std::vector<bool> beyond;
                const int n = static_cast<int>(std::ceil((o.before + o.after) / o.step));
                for (int k = 0; k <= n; ++k) {
                    // (Half a step off, so no frame is drawn from the doorway's plane itself.)
                    const double d = o.before - (k + 0.5) * o.step;
                    Element eye = A->camera();
                    eye.params = keep_a;
                    for (const char* lens : {"ortho", "back", "stand_w", "stand_x", "stand_y", "stand_z"}) eye.params.erase(Key{lens});
                    eye.params.set(keys::yaw, 0.0).set(keys::pitch, 0.0).set(keys::roll, 0.0).set(keys::fov, 70.0);
                    set_position(eye, at.position + up * (o.eye - hh) + in * d);
                    set_standing(eye, ground);
                    Spatial3D* world = A;
                    if (d > 0.0) {
                        A->camera().params = eye.params;
                    } else {
                        Element there = B->camera();
                        there.params = keep_b;
                        portal_carry(*door, *far)(eye, there);
                        B->camera().params = there.params;
                        world = B;
                    }
                    view.render(*world, o.w, o.h);
                    std::vector<unsigned char> px(static_cast<std::size_t>(o.w) * o.h * 3);
                    gl::glReadPixels(0, 0, o.w, o.h, gl::GL_RGB, gl::GL_UNSIGNED_BYTE, px.data());
                    if (!o.dump.empty()) {
                        std::string name = o.dump + "/" + s.name.str() + "-" + A->id().str() + "-" + std::to_string(k) + ".ppm";
                        if (FILE* f = std::fopen(name.c_str(), "wb")) {
                            std::fprintf(f, "P6\n%d %d\n255\n", o.w, o.h);
                            for (int y = o.h - 1; y >= 0; --y) std::fwrite(&px[static_cast<std::size_t>(y) * o.w * 3], 1, static_cast<std::size_t>(o.w) * 3, f);
                            std::fclose(f);
                        }
                    }
                    frames.push_back(std::move(px));
                    beyond.push_back(d <= 0.0);
                }
                A->camera().params = keep_a;
                B->camera().params = keep_b;
                // The walk's own pace: how much a frame changes, cell by cell.
                std::vector<std::vector<double>> steps;
                for (std::size_t k = 1; k < frames.size(); ++k) steps.push_back(cells(frames[k - 1], frames[k], o.w, o.h));
                if (steps.size() < 3) continue;
                std::vector<double> means;
                for (const auto& st : steps) means.push_back(mean(st));
                const double pace = median(means);
                std::vector<double> per(kGrid * kGrid);
                for (int c = 0; c < kGrid * kGrid; ++c) {
                    std::vector<double> col;
                    for (const auto& st : steps) col.push_back(st[c]);
                    per[c] = median(col);
                }
                for (std::size_t k = 1; k + 1 < frames.size(); ++k) {
                    const auto skip = cells(frames[k - 1], frames[k + 1], o.w, o.h);
                    int flicker = 0;
                    for (int c = 0; c < kGrid * kGrid; ++c)
                        if (steps[k - 1][c] > 3 * per[c] + 8 && steps[k][c] > 3 * per[c] + 8 && skip[c] < 0.35 * std::min(steps[k - 1][c], steps[k][c]))
                            ++flicker;
                    if (flicker) out.push_back(way + ": frame " + std::to_string(k) + " flickers in " + std::to_string(flicker) + " cell(s)");
                }
                // A seam whose two sides mean to wear different looks says so
                // (`differs look`): its crossing is not held to one picture.
                const std::string differs = " " + door->params.get_or<std::string>(Key{"differs"}, "") + " " +
                                            far->params.get_or<std::string>(Key{"differs"}, "") + " ";
                const bool looks_differ = differs.find(" look ") != std::string::npos;
                for (std::size_t k = 1; k < frames.size() && !looks_differ; ++k)
                    if (beyond[k] != beyond[k - 1] && means[k - 1] > 3 * pace + 4) {
                        char b[160];
                        std::snprintf(b, sizeof b, ": crossing, the picture jumps %.1f (a frame's pace is %.1f)", means[k - 1], pace);
                        out.push_back(way + b);
                    }
            }
    return out;
}

}  // namespace sg::render
