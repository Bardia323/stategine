// The GL view's lamp cells (Quality::lamp_cells): which of the frame's lamps
// can light each part of the view, so each part asks only those.
#include <vector>
#include "sg/gl/World.hpp"

#include <algorithm>
#include <cmath>

#include "WorldHash.hpp"

namespace sg::render {

namespace {
// The shell a distance from the eye falls in (as lights_glsl's lamp_cell):
// the first from the eye to `near`, then out in steps that widen as they go.
int shell_of(float d, float near, float far, int shells) {
    if (d <= near) return 0;
    const int s = static_cast<int>(std::floor(std::log(d / near) / std::log(far / near) * static_cast<float>(shells)));
    return std::clamp(s, 0, shells - 1);
}
gl::Vec3 at_clip(const gl::Mat4& m, float x, float y, float z, float w) {
    return {m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12] * w, m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13] * w,
            m.m[3] * x + m.m[7] * y + m.m[11] * z + m.m[15] * w};  // (x, y, and w)
}
}  // namespace

void GLWorldView::lamp_cells(const std::vector<Light>& lights, const gl::Mat4& view_proj, const gl::Vec3& eye, float zfar, bool images) {
    LampCells& c = lamp_cells_;
    c.on = false;
    // (A copy of a space that wraps is lit where its original is: the cells,
    // which are of where things are seen, would not be of where they are lit.)
    if (!q_.lamp_cells || images) return;
    const std::size_t n = std::min(lights.size(), kAllLights);
    int ranged = 0;
    for (std::size_t i = 0; i < n; ++i) ranged += !lights[i].sun && lights[i].range > 0.0f;
    if (ranged < kLampCellsFrom) return;
    const int cx = std::max(1, static_cast<int>(std::ceil(vp_w_ / static_cast<float>(kLampCell))));
    const int cy = std::max(1, static_cast<int>(std::ceil(vp_h_ / static_cast<float>(kLampCell))));
    const float near = 0.1f, far = std::max(zfar, 0.2f);
    // What they are made from: the view and every lamp's place and range.
    uint64_t key = 1469598103934665603ULL;
    for (float f : view_proj.m) key = mix_bits(key, f);
    for (float f : {eye.x, eye.y, eye.z, near, far, static_cast<float>(cx), static_cast<float>(cy)}) key = mix_bits(key, f);
    for (std::size_t i = 0; i < n; ++i) {
        const Light& l = lights[i];
        for (float f : {l.pos.x, l.pos.y, l.pos.z, l.dir.x, l.dir.y, l.dir.z, l.outer, l.range, l.sun ? 1.0f : 0.0f, l.frame_w, l.frame_h})
            key = mix_bits(key, f);
    }
    c.on = true;
    if (key == c.key && c.tex != 0) return;
    c.key = key, c.cx = cx, c.cy = cy, c.near = near, c.far = far;
    // Every lamp that says no range lights every cell; one that does, those
    // its ball can fall in - the shells its distance from the eye spans, the
    // cells across and up its ball covers on the view (all of them if any of
    // it is behind the eye). (A few cells more than it lights: never fewer.)
    uint32_t always[2] = {0, 0};
    for (std::size_t i = 0; i < n; ++i)
        if (lights[i].sun || lights[i].range <= 0.0f) always[i / 32] |= 1u << (i % 32);
    const std::size_t cells = static_cast<std::size_t>(cx * cy * kLampShells);
    c.bits.resize(cells * 2);
    for (std::size_t k = 0; k < cells; ++k) c.bits[k * 2] = always[0], c.bits[k * 2 + 1] = always[1];
    for (std::size_t i = 0; i < n; ++i) {
        const Light& l = lights[i];
        if (l.sun || l.range <= 0.0f) continue;
        const float r = l.range;
        const gl::Vec3 away = l.pos - eye;
        const float dist = std::sqrt(gl::dot(away, away));
        const int s0 = shell_of(std::max(dist - r, 0.0f), near, far, kLampShells), s1 = shell_of(dist + r, near, far, kLampShells);
        int tx0 = 0, tx1 = cx - 1, ty0 = 0, ty1 = cy - 1;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        bool behind = false;
        // What it can light: a spot its cone (its tip, rings round its axis
        // out to the end of its reach, and the cap there), drawn round wide
        // enough to hold the round of each; a lamp lighting every way, the
        // box round its ball. A downlight lights a pool, not a room: by its
        // ball every cell of the room's view asked it.
        std::vector<gl::Vec3> hull;
        const float dl = std::sqrt(gl::dot(l.dir, l.dir));
        if (l.outer < 1.4f && dl > 1e-6f) {
            const gl::Vec3 ax = l.dir * (1.0f / dl);
            const gl::Vec3 a = std::fabs(ax.y) < 0.9f ? gl::Vec3{0.0f, 1.0f, 0.0f} : gl::Vec3{1.0f, 0.0f, 0.0f};
            gl::Vec3 u = gl::cross(ax, a);
            u = u * (1.0f / std::sqrt(gl::dot(u, u)));
            const gl::Vec3 v = gl::cross(ax, u);
            hull.push_back(l.pos);
            hull.push_back(l.pos + ax * r);
            // (Rings at the cone's edge and half way to its axis, on the ball of
            // its reach; each an octagon round its circle - 1/cos(pi/8) out.)
            for (float frac : {0.5f, 1.0f}) {
                const float th = l.outer * frac, along = r * std::cos(th), across = r * std::sin(th) * 1.0824f;
                for (int k = 0; k < 8; ++k) {
                    const float ph = static_cast<float>(k) * 0.78539816f;
                    hull.push_back(l.pos + ax * along + u * (across * std::cos(ph)) + v * (across * std::sin(ph)));
                }
            }
        } else {
            for (int k = 0; k < 8; ++k) hull.push_back(l.pos + gl::Vec3{(k & 1) ? r : -r, (k & 2) ? r : -r, (k & 4) ? r : -r});
        }
        for (const gl::Vec3& q : hull) {
            const gl::Vec3 h = at_clip(view_proj, q.x, q.y, q.z, 1.0f);
            if (h.z <= 1e-4f) {
                behind = true;
                break;
            }
            x0 = std::min(x0, h.x / h.z), x1 = std::max(x1, h.x / h.z), y0 = std::min(y0, h.y / h.z), y1 = std::max(y1, h.y / h.z);
        }
        if (!behind) {
            if (x1 < -1.0f || x0 > 1.0f || y1 < -1.0f || y0 > 1.0f) continue;  // not in the view at all
            tx0 = std::clamp(static_cast<int>(std::floor((x0 + 1.0f) * 0.5f * static_cast<float>(cx))), 0, cx - 1);
            tx1 = std::clamp(static_cast<int>(std::floor((x1 + 1.0f) * 0.5f * static_cast<float>(cx))), 0, cx - 1);
            ty0 = std::clamp(static_cast<int>(std::floor((y0 + 1.0f) * 0.5f * static_cast<float>(cy))), 0, cy - 1);
            ty1 = std::clamp(static_cast<int>(std::floor((y1 + 1.0f) * 0.5f * static_cast<float>(cy))), 0, cy - 1);
        }
        const uint32_t bit = 1u << (i % 32);
        const std::size_t word = i / 32;
        for (int s = s0; s <= s1; ++s)
            for (int ty = ty0; ty <= ty1; ++ty) {
                uint32_t* row = c.bits.data() + static_cast<std::size_t>((s * cy + ty) * cx) * 2 + word;
                for (int tx = tx0; tx <= tx1; ++tx) row[static_cast<std::size_t>(tx) * 2] |= bit;
            }
    }
    if (c.ring.empty()) {
        c.ring.resize(kLampCellRing);
        gl::glGenTextures(static_cast<gl::GLsizei>(c.ring.size()), c.ring.data());
    }
    c.tex = c.ring[c.next++ % c.ring.size()];
    gl::glActiveTexture(gl::GL_TEXTURE0 + 11u);
    gl::glBindTexture(gl::GL_TEXTURE_2D, c.tex);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MIN_FILTER, gl::GL_NEAREST);
    gl::glTexParameteri(gl::GL_TEXTURE_2D, gl::GL_TEXTURE_MAG_FILTER, gl::GL_NEAREST);
    gl::glTexImage2D(gl::GL_TEXTURE_2D, 0, static_cast<gl::GLint>(gl::GL_RG32UI), cx, cy * kLampShells, 0, gl::GL_RG_INTEGER, gl::GL_UNSIGNED_INT, c.bits.data());
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

void GLWorldView::lamp_cells_to(const gl::Program& p) const {
    const LampCells& c = lamp_cells_;
    p.set("uLampCells", 11);
    if (!c.on) {
        p.set("uLampCellGrid", 0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    gl::glActiveTexture(gl::GL_TEXTURE0 + 11u);
    gl::glBindTexture(gl::GL_TEXTURE_2D, c.tex);
    gl::glActiveTexture(gl::GL_TEXTURE0);
    p.set("uLampCellGrid", static_cast<float>(c.cx), static_cast<float>(c.cy), static_cast<float>(kLampShells), 1.0f);
    p.set("uLampShells", c.near, c.far);
}

}  // namespace sg::render
