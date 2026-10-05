// A model looked at: four views of it in one picture - three-quarters from
// above, front, side and top - each fitted to its square, flat-shaded by one
// light, each material its own colour; and that picture as a PNG. For whoever
// writes recipes to see what they made, as they make it: not a renderer, and
// not of any world.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "sg/domains/Modeler.hpp"

namespace sg::sculpt {

namespace {
struct C3 {
    double r, g, b;
};

C3 colour_of(const std::string& material) {
    static const std::pair<const char*, C3> known[] = {
        {"stone", {0.62, 0.6, 0.56}}, {"rubble", {0.55, 0.53, 0.5}}, {"brick", {0.66, 0.3, 0.22}},  {"slate", {0.3, 0.33, 0.4}},
        {"rooftiles", {0.6, 0.25, 0.18}}, {"snow", {0.93, 0.95, 0.98}}, {"ice", {0.7, 0.85, 0.95}}, {"wood", {0.55, 0.38, 0.22}},
        {"planks", {0.6, 0.42, 0.25}},  {"bark", {0.35, 0.25, 0.16}},  {"iron", {0.45, 0.46, 0.48}}, {"metal", {0.7, 0.71, 0.73}},
        {"gold", {0.85, 0.68, 0.25}},   {"grass", {0.35, 0.55, 0.25}}, {"earth", {0.42, 0.3, 0.2}},  {"sand", {0.85, 0.76, 0.55}},
        {"glass", {0.6, 0.8, 0.85}},    {"plaster", {0.85, 0.82, 0.76}}, {"fabric", {0.55, 0.25, 0.3}}};
    for (const auto& [n, c] : known)
        if (material == n) return c;
    if (material.empty()) return {0.72, 0.72, 0.7};
    uint32_t h = 2166136261u;
    for (char ch : material) h = (h ^ uint8_t(ch)) * 16777619u;
    return {0.35 + 0.5 * ((h >> 0) & 255) / 255.0, 0.35 + 0.5 * ((h >> 8) & 255) / 255.0, 0.35 + 0.5 * ((h >> 16) & 255) / 255.0};
}

struct View {
    double yaw, pitch;  // the eye's way round the model, and up
};

// One view into a square of the picture: every face turned to the eye,
// fitted, filled by its depth.
void draw(const Model& m, const View& v, int x0, int y0, int size, int stride, std::vector<unsigned char>& rgb, std::vector<float>& depth) {
    const double cy = std::cos(v.yaw), sy = std::sin(v.yaw), cp = std::cos(v.pitch), sp = std::sin(v.pitch);
    // To the eye's frame: turned about y by yaw, then tipped by pitch.
    const auto eye = [&](double x, double y, double z, double& ex, double& ey, double& ez) {
        const double rx = cy * x + sy * z, rz = -sy * x + cy * z;
        ex = rx;
        ey = cp * y - sp * rz;
        ez = sp * y + cp * rz;  // toward the eye
    };
    double lo[2] = {1e30, 1e30}, hi[2] = {-1e30, -1e30};
    for (const Part& p : m.parts)
        for (std::size_t i = 0; i + 7 < p.corners.size(); i += 8) {
            double ex, ey, ez;
            eye(p.corners[i], p.corners[i + 1], p.corners[i + 2], ex, ey, ez);
            lo[0] = std::min(lo[0], ex), hi[0] = std::max(hi[0], ex), lo[1] = std::min(lo[1], ey), hi[1] = std::max(hi[1], ey);
        }
    if (lo[0] > hi[0]) return;
    const double span = std::max(hi[0] - lo[0], hi[1] - lo[1]) * 1.1 + 1e-9, k = size / span;
    const double mx = (lo[0] + hi[0]) * 0.5, my = (lo[1] + hi[1]) * 0.5;
    // The light: from over the eye's left shoulder.
    double lx, ly, lz;
    eye(-0.4, 0.8, 0.45, lx, ly, lz);
    const double ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= ll, ly /= ll, lz /= ll;
    for (const Part& p : m.parts) {
        const C3 base = colour_of(p.material);
        for (std::size_t i = 0; i + 23 < p.corners.size(); i += 24) {
            double X[3], Y[3], Z[3];
            for (int c = 0; c < 3; ++c) {
                double ex, ey, ez;
                eye(p.corners[i + c * 8], p.corners[i + c * 8 + 1], p.corners[i + c * 8 + 2], ex, ey, ez);
                X[c] = x0 + size * 0.5 + (ex - mx) * k;
                Y[c] = y0 + size * 0.5 - (ey - my) * k;
                Z[c] = ez;
            }
            // Its face's normal, as the eye sees it: shaded by the light, and
            // drawn from either side (a face seen from behind is dimmer).
            const double ax = X[1] - X[0], ay = Y[1] - Y[0], bx = X[2] - X[0], by = Y[2] - Y[0];
            const double area = ax * by - ay * bx;
            if (std::fabs(area) < 1e-12) continue;
            double nx, ny, nz;
            {
                double e[3][3];
                for (int c = 0; c < 3; ++c) eye(p.corners[i + c * 8], p.corners[i + c * 8 + 1], p.corners[i + c * 8 + 2], e[c][0], e[c][1], e[c][2]);
                const double u[3] = {e[1][0] - e[0][0], e[1][1] - e[0][1], e[1][2] - e[0][2]}, w[3] = {e[2][0] - e[0][0], e[2][1] - e[0][1], e[2][2] - e[0][2]};
                nx = u[1] * w[2] - u[2] * w[1], ny = u[2] * w[0] - u[0] * w[2], nz = u[0] * w[1] - u[1] * w[0];
                const double nl = std::sqrt(nx * nx + ny * ny + nz * nz) + 1e-30;
                nx /= nl, ny /= nl, nz /= nl;
            }
            const bool back = nz < 0;
            if (back) nx = -nx, ny = -ny, nz = -nz;
            const double lit = (0.28 + 0.72 * std::max(0.0, nx * lx + ny * ly + nz * lz)) * (back ? 0.55 : 1.0);
            const int bx0 = std::max(x0, int(std::floor(std::min({X[0], X[1], X[2]})))), bx1 = std::min(x0 + size - 1, int(std::ceil(std::max({X[0], X[1], X[2]}))));
            const int by0 = std::max(y0, int(std::floor(std::min({Y[0], Y[1], Y[2]})))), by1 = std::min(y0 + size - 1, int(std::ceil(std::max({Y[0], Y[1], Y[2]}))));
            for (int py = by0; py <= by1; ++py)
                for (int px = bx0; px <= bx1; ++px) {
                    const double sx = px + 0.5, sy2 = py + 0.5;
                    const double w0 = ((X[1] - sx) * (Y[2] - sy2) - (Y[1] - sy2) * (X[2] - sx)) / area;
                    const double w1 = ((X[2] - sx) * (Y[0] - sy2) - (Y[2] - sy2) * (X[0] - sx)) / area;
                    const double w2 = 1 - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const float z = float(w0 * Z[0] + w1 * Z[1] + w2 * Z[2]);
                    const std::size_t at = std::size_t(py) * std::size_t(stride) + std::size_t(px);
                    if (z <= depth[at]) continue;
                    depth[at] = z;
                    rgb[at * 3] = (unsigned char)std::min(255.0, 255 * std::pow(base.r * lit, 1 / 2.2));
                    rgb[at * 3 + 1] = (unsigned char)std::min(255.0, 255 * std::pow(base.g * lit, 1 / 2.2));
                    rgb[at * 3 + 2] = (unsigned char)std::min(255.0, 255 * std::pow(base.b * lit, 1 / 2.2));
                }
        }
    }
}

uint32_t crc(const unsigned char* d, std::size_t n, uint32_t c = 0xffffffffu) {
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
            table[i] = v;
        }
        made = true;
    }
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 255] ^ (c >> 8);
    return c;
}
void be32(std::string& s, uint32_t v) {
    for (int k = 3; k >= 0; --k) s += char((v >> (k * 8)) & 255);
}
void chunk(std::string& out, const char* type, const std::string& data) {
    be32(out, uint32_t(data.size()));
    std::string body = std::string(type, 4) + data;
    out += body;
    be32(out, crc(reinterpret_cast<const unsigned char*>(body.data()), body.size()) ^ 0xffffffffu);
}
}  // namespace

std::vector<unsigned char> picture(const Model& m, int w, int h) {
    std::vector<unsigned char> rgb(std::size_t(w) * std::size_t(h) * 3);
    // A dark ground, a little lighter towards the top of each square.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double t = double(y % (h / 2)) / (h / 2);
            const unsigned char g = (unsigned char)(46 - 18 * t);
            const std::size_t at = (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 3;
            rgb[at] = g, rgb[at + 1] = g, rgb[at + 2] = (unsigned char)(g + 6);
        }
    std::vector<float> depth(std::size_t(w) * std::size_t(h), -1e30f);
    const int s = std::min(w, h) / 2;
    const double pi = 3.14159265358979;
    const View views[4] = {{-pi * 0.2, 0.45}, {0.0, 0.0}, {-pi * 0.5, 0.0}, {0.0, pi * 0.5}};
    for (int q = 0; q < 4; ++q) draw(m, views[q], (q % 2) * s, (q / 2) * s, s, w, rgb, depth);
    // The lines between the views.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (x == s || y == s) {
                const std::size_t at = (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 3;
                rgb[at] = rgb[at + 1] = rgb[at + 2] = 90;
            }
    return rgb;
}

std::string png(const std::vector<unsigned char>& rgb, int w, int h) {
    std::string raw;
    raw.reserve(std::size_t(h) * (std::size_t(w) * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw += char(0);
        raw.append(reinterpret_cast<const char*>(rgb.data()) + std::size_t(y) * std::size_t(w) * 3, std::size_t(w) * 3);
    }
    // zlib, stored: blocks of at most 65535 bytes, and the Adler sum.
    std::string z = "\x78\x01";
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
    for (std::size_t at = 0; at < raw.size() || at == 0;) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z += char(last ? 1 : 0);
        z += char(n & 255), z += char(n >> 8), z += char(~n & 255), z += char((~n >> 8) & 255);
        z.append(raw, at, n);
        at += n;
        if (last) break;
    }
    be32(z, (b << 16) | a);
    std::string out = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    be32(ihdr, uint32_t(w)), be32(ihdr, uint32_t(h));
    ihdr += char(8), ihdr += char(2), ihdr += char(0), ihdr += char(0), ihdr += char(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", "");
    return out;
}

}  // namespace sg::sculpt
