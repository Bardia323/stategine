#include "sg/render/Pack.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

#include "SG_PACK_CODE.hpp"
#include "sg/core/Cache.hpp"

namespace sg::render {

namespace {

// How far along from one end of a block's line to the other each index is,
// in 64ths: BC7's own tables, for two, three and four bits of index.
constexpr int kW2[4] = {0, 21, 43, 64};
constexpr int kW3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
constexpr int kW4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

int between(int a, int b, int w) { return ((64 - w) * a + w * b + 32) >> 6; }

// The nearest whole number, for the numbers packing rounds (never far below
// zero): without a call into the C library, which costs more than the rest.
int nearest(float v) { return v >= 0.0f ? static_cast<int>(v + 0.5f) : -static_cast<int>(0.5f - v); }

struct Writer {
    unsigned char* out;
    int at = 0;
    void put(uint32_t v, int n) {
        for (int i = 0; i < n; ++i, ++at)
            if ((v >> i) & 1u) out[at >> 3] = static_cast<unsigned char>(out[at >> 3] | (1u << (at & 7)));
    }
};

struct Reader {
    const unsigned char* in;
    int at = 0;
    uint32_t get(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i, ++at) v |= static_cast<uint32_t>((in[at >> 3] >> (at & 7)) & 1u) << i;
        return v;
    }
};

// The line through C channels of the sixteen pixels along which they spread
// most, and where on it they begin and end: the best two ends a block can
// have before they are rounded to the mode's bits.
template <int C>
void spread(const float x[16][C], float lo[C], float hi[C]) {
    float mean[C] = {};
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < C; ++c) mean[c] += x[i][c];
    for (int c = 0; c < C; ++c) mean[c] /= 16.0f;
    float cov[C][C] = {};
    for (int i = 0; i < 16; ++i)
        for (int a = 0; a < C; ++a)
            for (int b = 0; b < C; ++b) cov[a][b] += (x[i][a] - mean[a]) * (x[i][b] - mean[b]);
    // From the channel that spreads most, turned towards the principal axis.
    int most = 0;
    for (int c = 1; c < C; ++c)
        if (cov[c][c] > cov[most][most]) most = c;
    float axis[C];
    for (int c = 0; c < C; ++c) axis[c] = cov[most][c];
    float len = 0;
    for (int it = 0; it < 8; ++it) {
        float next[C] = {};
        for (int a = 0; a < C; ++a)
            for (int b = 0; b < C; ++b) next[a] += cov[a][b] * axis[b];
        len = 0;
        for (int c = 0; c < C; ++c) len += next[c] * next[c];
        if (len < 1e-12f) break;
        len = std::sqrt(len);
        for (int c = 0; c < C; ++c) axis[c] = next[c] / len;
    }
    if (len < 1e-12f) {
        for (int c = 0; c < C; ++c) lo[c] = hi[c] = mean[c];
        return;
    }
    float tlo = 1e30f, thi = -1e30f;
    for (int i = 0; i < 16; ++i) {
        float t = 0;
        for (int c = 0; c < C; ++c) t += (x[i][c] - mean[c]) * axis[c];
        tlo = std::min(tlo, t), thi = std::max(thi, t);
    }
    for (int c = 0; c < C; ++c) {
        lo[c] = std::clamp(mean[c] + tlo * axis[c], 0.0f, 255.0f);
        hi[c] = std::clamp(mean[c] + thi * axis[c], 0.0f, 255.0f);
    }
}

// Each pixel's index: the step along the line from e0 to e1 nearest it. What
// it leaves, summed and squared, comes back. With many steps, only the three
// round where the pixel falls along the line are weighed: the steps are all
// on that line, nearly evenly, so the nearest is among them.
template <int C, int L>
long assign(const int px[16][C], const int e0[C], const int e1[C], const int (&w)[L], uint8_t idx[16]) {
    int step[L][C];
    for (int l = 0; l < L; ++l)
        for (int c = 0; c < C; ++c) step[l][c] = between(e0[c], e1[c], w[l]);
    const auto off = [&](int i, int l) {
        int e = 0;
        for (int c = 0; c < C; ++c) {
            const int d = step[l][c] - px[i][c];
            e += d * d;
        }
        return e;
    };
    int d[C];
    int dd = 0;
    for (int c = 0; c < C; ++c) d[c] = e1[c] - e0[c], dd += d[c] * d[c];
    const float per = dd > 0 ? static_cast<float>(L - 1) / static_cast<float>(dd) : 0.0f;
    long all = 0;
    for (int i = 0; i < 16; ++i) {
        int lo = 0, hi = L - 1;
        if (L > 4 && dd > 0) {
            int t = 0;
            for (int c = 0; c < C; ++c) t += (px[i][c] - e0[c]) * d[c];
            const int guess = std::clamp(nearest(static_cast<float>(t) * per), 0, L - 1);
            lo = std::max(0, guess - 1), hi = std::min(L - 1, guess + 1);
        } else if (dd == 0) {
            hi = 0;
        }
        int best = INT_MAX, at = lo;
        for (int l = lo; l <= hi; ++l)
            if (const int e = off(i, l); e < best) best = e, at = l;
        idx[i] = static_cast<uint8_t>(at);
        all += best;
    }
    return all;
}

// The two ends that, with these indices, leave the least: least squares.
// False when the indices do not pin them (all at one step).
template <int C, int L>
bool refit(const float x[16][C], const uint8_t idx[16], const int (&w)[L], float lo[C], float hi[C]) {
    float a = 0, b = 0, c2 = 0, r0[C] = {}, r1[C] = {};
    for (int i = 0; i < 16; ++i) {
        const float t = w[idx[i]] / 64.0f, s = 1.0f - t;
        a += s * s, b += s * t, c2 += t * t;
        for (int c = 0; c < C; ++c) r0[c] += s * x[i][c], r1[c] += t * x[i][c];
    }
    const float det = a * c2 - b * b;
    if (std::fabs(det) < 1e-6f) return false;
    for (int c = 0; c < C; ++c) {
        lo[c] = std::clamp((c2 * r0[c] - b * r1[c]) / det, 0.0f, 255.0f);
        hi[c] = std::clamp((a * r1[c] - b * r0[c]) / det, 0.0f, 255.0f);
    }
    return true;
}

// Mode 6: one line through colour and height together - seven bits an end and
// channel, a bit each end shares across its channels, sixteen steps.
struct Six {
    int q[2][4] = {};
    int p[2] = {};
    uint8_t idx[16] = {};
    long err = LONG_MAX;
};

void round6(const float e[4], int q[4], int& p) {
    long best = LONG_MAX;
    for (int pb = 0; pb < 2; ++pb) {
        int t[4];
        long err = 0;
        for (int c = 0; c < 4; ++c) {
            t[c] = std::clamp(nearest((e[c] - pb) * 0.5f), 0, 127);
            const float d = static_cast<float>(t[c] * 2 + pb) - e[c];
            err += static_cast<long>(d * d * 16.0f);
        }
        if (err < best) {
            best = err, p = pb;
            for (int c = 0; c < 4; ++c) q[c] = t[c];
        }
    }
}

Six fit6(const int px[16][4]) {
    float x[16][4];
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c) x[i][c] = static_cast<float>(px[i][c]);
    float lo[4], hi[4];
    spread<4>(x, lo, hi);
    Six best;
    // Ends with the given shared bits, each channel rounded to its nearest.
    const auto with = [&](int pa, int pb) {
        Six g;
        for (int c = 0; c < 4; ++c) {
            g.q[0][c] = std::clamp(nearest((lo[c] - pa) * 0.5f), 0, 127);
            g.q[1][c] = std::clamp(nearest((hi[c] - pb) * 0.5f), 0, 127);
        }
        g.p[0] = pa, g.p[1] = pb;
        int e0[4], e1[4];
        for (int c = 0; c < 4; ++c) e0[c] = g.q[0][c] * 2 + pa, e1[c] = g.q[1][c] * 2 + pb;
        g.err = assign<4, 16>(px, e0, e1, kW4, g.idx);
        if (g.err < best.err) best = g;
    };
    for (int round = 0; round < 2; ++round) {
        int q[4], pa = 0, pb = 0;
        round6(lo, q, pa);
        round6(hi, q, pb);
        with(pa, pb);
        if (best.err == 0 || !refit<4, 16>(x, best.idx, kW4, lo, hi)) break;
    }
    return best;
}

void write6(Six f, unsigned char out[16]) {
    if (f.idx[0] >= 8) {  // the first pixel's index has no top bit: the line runs the other way
        for (int c = 0; c < 4; ++c) std::swap(f.q[0][c], f.q[1][c]);
        std::swap(f.p[0], f.p[1]);
        for (uint8_t& i : f.idx) i = static_cast<uint8_t>(15 - i);
    }
    std::memset(out, 0, 16);
    Writer w{out};
    w.put(1u << 6, 7);
    for (int c = 0; c < 4; ++c) w.put(static_cast<uint32_t>(f.q[0][c]), 7), w.put(static_cast<uint32_t>(f.q[1][c]), 7);
    w.put(static_cast<uint32_t>(f.p[0]), 1), w.put(static_cast<uint32_t>(f.p[1]), 1);
    w.put(f.idx[0], 3);
    for (int i = 1; i < 16; ++i) w.put(f.idx[i], 4);
}

// Mode 5: colour on a line of its own and height on another - seven bits an
// end and colour, eight for height, four steps each. Where height goes its own
// way across a block, this keeps both.
struct Five {
    int qc[2][3] = {};
    int qa[2] = {};
    uint8_t ci[16] = {}, ai[16] = {};
    long err = LONG_MAX;
};

int expand7(int q) { return (q << 1) | (q >> 6); }

int round7(float e) {
    const int q0 = std::clamp(nearest(e * 127.0f / 255.0f), 0, 127);
    int best = q0;
    float d = std::fabs(static_cast<float>(expand7(q0)) - e);
    for (int q = std::max(0, q0 - 1); q <= std::min(127, q0 + 1); ++q)
        if (const float dq = std::fabs(static_cast<float>(expand7(q)) - e); dq < d) d = dq, best = q;
    return best;
}

Five fit5(const int px[16][4]) {
    Five best;
    // Colour.
    float x[16][3];
    int col[16][3];
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c) col[i][c] = px[i][c], x[i][c] = static_cast<float>(px[i][c]);
    float lo[3], hi[3];
    spread<3>(x, lo, hi);
    long cerr = LONG_MAX;
    for (int round = 0; round < 2; ++round) {
        int q0[3], q1[3], e0[3], e1[3];
        uint8_t idx[16];
        for (int c = 0; c < 3; ++c) q0[c] = round7(lo[c]), q1[c] = round7(hi[c]), e0[c] = expand7(q0[c]), e1[c] = expand7(q1[c]);
        const long err = assign<3, 4>(col, e0, e1, kW2, idx);
        if (err < cerr) {
            cerr = err;
            for (int c = 0; c < 3; ++c) best.qc[0][c] = q0[c], best.qc[1][c] = q1[c];
            std::memcpy(best.ci, idx, 16);
        }
        if (cerr == 0 || !refit<3, 4>(x, best.ci, kW2, lo, hi)) break;
    }
    // Height.
    float h[16][1];
    int ha[16][1];
    float alo[1] = {255.0f}, ahi[1] = {0.0f};
    for (int i = 0; i < 16; ++i) {
        ha[i][0] = px[i][3], h[i][0] = static_cast<float>(px[i][3]);
        alo[0] = std::min(alo[0], h[i][0]), ahi[0] = std::max(ahi[0], h[i][0]);
    }
    long aerr = LONG_MAX;
    for (int round = 0; round < 2; ++round) {
        const int e0[1] = {std::clamp(nearest(alo[0]), 0, 255)};
        const int e1[1] = {std::clamp(nearest(ahi[0]), 0, 255)};
        uint8_t idx[16];
        const long err = assign<1, 4>(ha, e0, e1, kW2, idx);
        if (err < aerr) {
            aerr = err;
            best.qa[0] = e0[0], best.qa[1] = e1[0];
            std::memcpy(best.ai, idx, 16);
        }
        if (aerr == 0 || !refit<1, 4>(h, best.ai, kW2, alo, ahi)) break;
    }
    best.err = cerr + aerr;
    return best;
}

void write5(Five f, unsigned char out[16]) {
    if (f.ci[0] >= 2) {
        for (int c = 0; c < 3; ++c) std::swap(f.qc[0][c], f.qc[1][c]);
        for (uint8_t& i : f.ci) i = static_cast<uint8_t>(3 - i);
    }
    if (f.ai[0] >= 2) {
        std::swap(f.qa[0], f.qa[1]);
        for (uint8_t& i : f.ai) i = static_cast<uint8_t>(3 - i);
    }
    std::memset(out, 0, 16);
    Writer w{out};
    w.put(1u << 5, 6);
    w.put(0, 2);  // no rotation: height is height
    for (int c = 0; c < 3; ++c) w.put(static_cast<uint32_t>(f.qc[0][c]), 7), w.put(static_cast<uint32_t>(f.qc[1][c]), 7);
    w.put(static_cast<uint32_t>(f.qa[0]), 8), w.put(static_cast<uint32_t>(f.qa[1]), 8);
    w.put(f.ci[0], 1);
    for (int i = 1; i < 16; ++i) w.put(f.ci[i], 2);
    w.put(f.ai[0], 1);
    for (int i = 1; i < 16; ++i) w.put(f.ai[i], 2);
}

// Mode 7: the block in two parts, a line each through colour and height -
// six bits an end (five and a bit the end shares across its channels), four
// steps. Where a stroke's hard edge crosses a block, each side keeps its own.
//
// Which pixels are in which part: BC7's sixty-four ways of cutting a block in
// two, a bit a pixel (set: the second part); and the pixel of the second part
// whose index has no top bit.
constexpr uint16_t kParts[64] = {
    0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80, 0xE800, 0xFFE8, 0xFF00, 0xFFF0, 0xF000,
    0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310, 0x3100, 0x8CCE, 0x088C, 0x3110, 0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C,
    0xAAAA, 0xF0F0, 0x5A5A, 0x33CC, 0x3C3C, 0x55AA, 0x9696, 0xA55A, 0x73CE, 0x13C8, 0x324C, 0x3BDC, 0x6996, 0xC33C, 0x9966, 0x0660,
    0x0272, 0x04E4, 0x4E40, 0x2720, 0xC936, 0x936C, 0x39C6, 0x639C, 0x9336, 0x9CC6, 0x817E, 0xE718, 0xCCF0, 0x0FCC, 0x7744, 0xEE22};
constexpr uint8_t kAnchor2[64] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2,
                                  8,  8,  2,  2,  15, 15, 6,  8,  2,  8,  15, 15, 2,  8,  2,  2,  2,  15, 15, 6, 6,  2, 6,  8, 15, 15,
                                  2,  2,  15, 15, 15, 15, 15, 2,  2,  15};

struct Seven {
    int part = 0;
    int q[4][4] = {};  // ends: part 0's two, then part 1's; five bits a channel
    int p[4] = {};
    uint8_t idx[16] = {};
    long err = LONG_MAX;
};

int expand6(int v) { return (v << 2) | (v >> 4); }

// An end of five bits and a shared bit, nearest `e` over its four channels.
void round7(const float e[4], int q[4], int& p) {
    float best = 1e30f;
    for (int pb = 0; pb < 2; ++pb) {
        int t[4];
        float err = 0;
        for (int c = 0; c < 4; ++c) {
            const int v0 = std::clamp(nearest((e[c] * 63.0f / 255.0f - pb) * 0.5f), 0, 31);
            int bq = v0;
            float bd = 1e30f;
            for (int q2 = std::max(0, v0 - 1); q2 <= std::min(31, v0 + 1); ++q2) {
                const float d = static_cast<float>(expand6(q2 * 2 + pb)) - e[c];
                if (d * d < bd) bd = d * d, bq = q2;
            }
            t[c] = bq, err += bd;
        }
        if (err < best) {
            best = err, p = pb;
            for (int c = 0; c < 4; ++c) q[c] = t[c];
        }
    }
}

// One part of a block, fitted: its pixels' indices, and what it leaves.
long fit_part(const int px[16][4], uint16_t mask, bool second, int q0[4], int q1[4], int& p0, int& p1, uint8_t idx[16]) {
    int sub[16][4], at[16], n = 0;
    float x[16][4];
    for (int i = 0; i < 16; ++i)
        if (((mask >> i) & 1) == (second ? 1 : 0)) {
            for (int c = 0; c < 4; ++c) sub[n][c] = px[i][c], x[n][c] = static_cast<float>(px[i][c]);
            at[n++] = i;
        }
    // Pad to sixteen with the part's own first pixel: it changes no line through it.
    for (int k = n; k < 16; ++k)
        for (int c = 0; c < 4; ++c) sub[k][c] = sub[0][c], x[k][c] = x[0][c];
    float lo[4], hi[4];
    spread<4>(x, lo, hi);
    long best = LONG_MAX;
    uint8_t got[16];
    for (int round = 0; round < 2; ++round) {
        int a[4], b[4], pa = 0, pb = 0, e0[4], e1[4];
        round7(lo, a, pa);
        round7(hi, b, pb);
        for (int c = 0; c < 4; ++c) e0[c] = expand6(a[c] * 2 + pa), e1[c] = expand6(b[c] * 2 + pb);
        long err = assign<4, 4>(sub, e0, e1, kW2, got);
        // Only the part's own pixels count; the padding repeats the first.
        for (int k = n; k < 16; ++k) {
            int d = 0;
            for (int c = 0; c < 4; ++c) {
                const int v = between(e0[c], e1[c], kW2[got[k]]) - sub[k][c];
                d += v * v;
            }
            err -= d;
        }
        if (err < best) {
            best = err, p0 = pa, p1 = pb;
            for (int c = 0; c < 4; ++c) q0[c] = a[c], q1[c] = b[c];
            for (int k = 0; k < n; ++k) idx[at[k]] = got[k];
        }
        if (best == 0 || !refit<4, 4>(x, got, kW2, lo, hi)) break;
    }
    return best;
}

// How well a part's pixels lie on one line: what is left of their spread once
// the line's own is taken away. A guess, to choose which cuts to fit.
float off_line(const float sum[4], const float prod[4][4], int n) {
    if (n < 2) return 0.0f;
    float cov[4][4], mean[4];
    for (int c = 0; c < 4; ++c) mean[c] = sum[c] / static_cast<float>(n);
    float trace = 0;
    for (int a = 0; a < 4; ++a)
        for (int b = 0; b < 4; ++b) cov[a][b] = prod[a][b] - mean[a] * sum[b];
    for (int c = 0; c < 4; ++c) trace += cov[c][c];
    float v[4] = {1.0f, 1.0f, 1.0f, 1.0f}, lambda = 0;
    for (int it = 0; it < 4; ++it) {
        float w[4] = {};
        for (int a = 0; a < 4; ++a)
            for (int b = 0; b < 4; ++b) w[a] += cov[a][b] * v[b];
        float len = 0;
        for (float k : w) len += k * k;
        if (len < 1e-12f) return trace;
        len = std::sqrt(len);
        for (int c = 0; c < 4; ++c) v[c] = w[c] / len;
        lambda = len;
    }
    return trace - lambda;
}

Seven fit7(const int px[16][4]) {
    // Each pixel's own sums, so each cut's parts are summed by its mask.
    float one[16][4], two[16][4][4];
    for (int i = 0; i < 16; ++i)
        for (int a = 0; a < 4; ++a) {
            one[i][a] = static_cast<float>(px[i][a]);
            for (int b = 0; b < 4; ++b) two[i][a][b] = static_cast<float>(px[i][a] * px[i][b]);
        }
    float guess[64];
    for (int k = 0; k < 64; ++k) {
        float s[2][4] = {}, q[2][4][4] = {};
        int n[2] = {};
        for (int i = 0; i < 16; ++i) {
            const int part = (kParts[k] >> i) & 1;
            ++n[part];
            for (int a = 0; a < 4; ++a) {
                s[part][a] += one[i][a];
                for (int b = 0; b < 4; ++b) q[part][a][b] += two[i][a][b];
            }
        }
        guess[k] = off_line(s[0], q[0], n[0]) + off_line(s[1], q[1], n[1]);
    }
    // The two cuts that look best, fitted.
    int order[64];
    for (int k = 0; k < 64; ++k) order[k] = k;
    std::partial_sort(order, order + 2, order + 64, [&](int a, int b) { return guess[a] < guess[b]; });
    Seven best;
    for (int t = 0; t < 2; ++t) {
        Seven f;
        f.part = order[t];
        const long a = fit_part(px, kParts[f.part], false, f.q[0], f.q[1], f.p[0], f.p[1], f.idx);
        const long b = fit_part(px, kParts[f.part], true, f.q[2], f.q[3], f.p[2], f.p[3], f.idx);
        f.err = a + b;
        if (f.err < best.err) best = f;
    }
    return best;
}

void write7(Seven f, unsigned char out[16]) {
    const uint16_t mask = kParts[f.part];
    // Each part's first index (pixel 0 for the first, its anchor for the
    // second) has no top bit: where it would, that part's line runs the other way.
    const int anchor[2] = {0, kAnchor2[f.part]};
    for (int part = 0; part < 2; ++part) {
        if (f.idx[anchor[part]] < 2) continue;
        for (int c = 0; c < 4; ++c) std::swap(f.q[2 * part][c], f.q[2 * part + 1][c]);
        std::swap(f.p[2 * part], f.p[2 * part + 1]);
        for (int i = 0; i < 16; ++i)
            if (((mask >> i) & 1) == part) f.idx[i] = static_cast<uint8_t>(3 - f.idx[i]);
    }
    std::memset(out, 0, 16);
    Writer w{out};
    w.put(1u << 7, 8);
    w.put(static_cast<uint32_t>(f.part), 6);
    for (int c = 0; c < 4; ++c)
        for (int e = 0; e < 4; ++e) w.put(static_cast<uint32_t>(f.q[e][c]), 5);
    for (int e = 0; e < 4; ++e) w.put(static_cast<uint32_t>(f.p[e]), 1);
    for (int i = 0; i < 16; ++i) w.put(f.idx[i], i == 0 || i == anchor[1] ? 1 : 2);
}

// sRGB numbers to light, and back - the nearest number to a light, as a card
// writes it.
const float* to_light() {
    static const auto table = [] {
        std::vector<float> t(256);
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            t[i] = static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return t;
    }();
    return table.data();
}

unsigned char from_light(float v) {
    static const auto table = [] {
        std::vector<unsigned char> t(16384);
        for (int i = 0; i < 16384; ++i) {
            const double l = i / 16383.0;
            const double c = l <= 0.0031308 ? l * 12.92 : 1.055 * std::pow(l, 1.0 / 2.4) - 0.055;
            t[i] = static_cast<unsigned char>(std::clamp(nearest(c * 255.0), 0, 255));
        }
        return t;
    }();
    return table[static_cast<std::size_t>(std::clamp(nearest(v * 16383.0f), 0, 16383))];
}

std::string flat(const Packed& p) {
    std::string s;
    const auto put = [&s](uint64_t v) { s.append(reinterpret_cast<const char*>(&v), 8); };
    put(static_cast<uint64_t>(p.w)), put(static_cast<uint64_t>(p.h)), put(p.srgb ? 1 : 0), put(p.levels.size());
    for (const Packed::Level& l : p.levels) put(static_cast<uint64_t>(l.w)), put(static_cast<uint64_t>(l.h)), put(l.at), put(l.size);
    s += p.bytes;
    return s;
}

bool unflat(const std::string& s, Packed& p) {
    std::size_t at = 0;
    const auto get = [&](uint64_t& v) {
        if (at + 8 > s.size()) return false;
        std::memcpy(&v, s.data() + at, 8);
        at += 8;
        return true;
    };
    uint64_t w, h, srgb, n;
    if (!get(w) || !get(h) || !get(srgb) || !get(n) || n > 64) return false;
    p.w = static_cast<int>(w), p.h = static_cast<int>(h), p.srgb = srgb != 0;
    p.levels.resize(n);
    for (Packed::Level& l : p.levels) {
        uint64_t lw, lh, la, ls;
        if (!get(lw) || !get(lh) || !get(la) || !get(ls)) return false;
        l.w = static_cast<int>(lw), l.h = static_cast<int>(lh), l.at = la, l.size = ls;
    }
    p.bytes = s.substr(at);
    for (const Packed::Level& l : p.levels)
        if (l.at + l.size > p.bytes.size()) return false;
    return true;
}

}  // namespace

bool packable(int w, int h) { return w >= 16 && h >= 16 && w % 4 == 0 && h % 4 == 0 && w * h >= 128 * 128; }

long bc7_encode(const unsigned char px[64], unsigned char out[16]) {
    int p[16][4];
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 4; ++c) p[i][c] = px[i * 4 + c];
    const Six six = fit6(p);
    // Within a level a channel, as near as any mode keeps it: no other is tried.
    if (six.err <= 16) {
        write6(six, out);
        return six.err;
    }
    const Five five = fit5(p);
    long best = std::min(six.err, five.err);
    // Far from either: a hard edge, most likely - the block cut in two.
    if (best > 576) {
        const Seven seven = fit7(p);
        if (seven.err < best) {
            write7(seven, out);
            return seven.err;
        }
    }
    if (five.err < six.err) {
        write5(five, out);
        return five.err;
    }
    write6(six, out);
    return six.err;
}

void bc7_decode(const unsigned char in[16], unsigned char px[64]) {
    int mode = 0;
    while (mode < 8 && !((in[0] >> mode) & 1)) ++mode;
    Reader r{in};
    r.get(mode + 1);
    if (mode == 6) {
        int e[2][4];
        for (int c = 0; c < 4; ++c) e[0][c] = static_cast<int>(r.get(7)), e[1][c] = static_cast<int>(r.get(7));
        const int p0 = static_cast<int>(r.get(1)), p1 = static_cast<int>(r.get(1));
        for (int c = 0; c < 4; ++c) e[0][c] = e[0][c] * 2 + p0, e[1][c] = e[1][c] * 2 + p1;
        for (int i = 0; i < 16; ++i) {
            const int idx = static_cast<int>(r.get(i == 0 ? 3 : 4));
            for (int c = 0; c < 4; ++c) px[i * 4 + c] = static_cast<unsigned char>(between(e[0][c], e[1][c], kW4[idx]));
        }
        return;
    }
    if (mode == 4 || mode == 5) {
        const int rot = static_cast<int>(r.get(2));
        const int sel = mode == 4 ? static_cast<int>(r.get(1)) : 0;
        const int cbits = mode == 4 ? 5 : 7, abits = mode == 4 ? 6 : 8;
        int c0[3], c1[3];
        for (int c = 0; c < 3; ++c) c0[c] = static_cast<int>(r.get(cbits)), c1[c] = static_cast<int>(r.get(cbits));
        int a0 = static_cast<int>(r.get(abits)), a1 = static_cast<int>(r.get(abits));
        const auto grow = [](int q, int bits) { return (q << (8 - bits)) | (q >> (2 * bits - 8)); };
        for (int c = 0; c < 3; ++c) c0[c] = grow(c0[c], cbits), c1[c] = grow(c1[c], cbits);
        if (abits < 8) a0 = grow(a0, abits), a1 = grow(a1, abits);
        // The first set of indices, then the second (mode 4: two bits, then three).
        const int b1 = 2, b2 = mode == 4 ? 3 : 2;
        int first[16], second[16];
        for (int i = 0; i < 16; ++i) first[i] = static_cast<int>(r.get(i == 0 ? b1 - 1 : b1));
        for (int i = 0; i < 16; ++i) second[i] = static_cast<int>(r.get(i == 0 ? b2 - 1 : b2));
        for (int i = 0; i < 16; ++i) {
            const int ci = sel ? second[i] : first[i], ai = sel ? first[i] : second[i];
            const int cb = sel ? b2 : b1, ab = sel ? b1 : b2;
            const int cw = cb == 2 ? kW2[ci] : kW3[ci], aw = ab == 2 ? kW2[ai] : kW3[ai];
            int v[4] = {between(c0[0], c1[0], cw), between(c0[1], c1[1], cw), between(c0[2], c1[2], cw), between(a0, a1, aw)};
            if (rot) std::swap(v[3], v[rot - 1]);
            for (int c = 0; c < 4; ++c) px[i * 4 + c] = static_cast<unsigned char>(v[c]);
        }
        return;
    }
    if (mode == 7) {
        const int part = static_cast<int>(r.get(6));
        int e[4][4];
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) e[k][c] = static_cast<int>(r.get(5));
        for (int k = 0; k < 4; ++k) {
            const int pb = static_cast<int>(r.get(1));
            for (int c = 0; c < 4; ++c) e[k][c] = expand6(e[k][c] * 2 + pb);
        }
        const int anchor = kAnchor2[part];
        for (int i = 0; i < 16; ++i) {
            const int idx = static_cast<int>(r.get(i == 0 || i == anchor ? 1 : 2));
            const int k = ((kParts[part] >> i) & 1) * 2;
            for (int c = 0; c < 4; ++c) px[i * 4 + c] = static_cast<unsigned char>(between(e[k][c], e[k + 1][c], kW2[idx]));
        }
        return;
    }
    // A mode this packer never writes: shown as nothing, not guessed at.
    std::memset(px, 0, 64);
}

std::vector<unsigned char> mip_down(const unsigned char* rgba, int w, int h, bool srgb, int& next_w, int& next_h) {
    next_w = std::max(1, w / 2), next_h = std::max(1, h / 2);
    std::vector<unsigned char> out(static_cast<std::size_t>(next_w) * next_h * 4);
    const float* light = to_light();
    for (int y = 0; y < next_h; ++y) {
        const int y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
        for (int x = 0; x < next_w; ++x) {
            const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1);
            const unsigned char* q[4] = {rgba + (static_cast<std::size_t>(y0) * w + x0) * 4, rgba + (static_cast<std::size_t>(y0) * w + x1) * 4,
                                         rgba + (static_cast<std::size_t>(y1) * w + x0) * 4, rgba + (static_cast<std::size_t>(y1) * w + x1) * 4};
            unsigned char* o = &out[(static_cast<std::size_t>(y) * next_w + x) * 4];
            for (int c = 0; c < 3; ++c) {
                if (srgb) {
                    o[c] = from_light((light[q[0][c]] + light[q[1][c]] + light[q[2][c]] + light[q[3][c]]) * 0.25f);
                } else {
                    o[c] = static_cast<unsigned char>((q[0][c] + q[1][c] + q[2][c] + q[3][c] + 2) / 4);
                }
            }
            o[3] = static_cast<unsigned char>((q[0][3] + q[1][3] + q[2][3] + q[3][3] + 2) / 4);
        }
    }
    return out;
}

Packed pack(const unsigned char* rgba, int w, int h, bool srgb) {
    Packed p;
    p.w = w, p.h = h, p.srgb = srgb;
    std::vector<unsigned char> held;
    const unsigned char* src = rgba;
    int lw = w, lh = h;
    for (;;) {
        const int bw = (lw + 3) / 4, bh = (lh + 3) / 4;
        Packed::Level level{lw, lh, p.bytes.size(), static_cast<std::size_t>(bw) * bh * 16};
        p.bytes.resize(level.at + level.size);
        auto* out = reinterpret_cast<unsigned char*>(&p.bytes[level.at]);
        unsigned char block[64];
        for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx) {
                // A block past the edge of a small level repeats the edge.
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x) {
                        const int sx = std::min(bx * 4 + x, lw - 1), sy = std::min(by * 4 + y, lh - 1);
                        std::memcpy(block + (y * 4 + x) * 4, src + (static_cast<std::size_t>(sy) * lw + sx) * 4, 4);
                    }
                bc7_encode(block, out + (static_cast<std::size_t>(by) * bw + bx) * 16);
            }
        p.levels.push_back(level);
        if (lw == 1 && lh == 1) break;
        int nw = 0, nh = 0;
        std::vector<unsigned char> next = mip_down(src, lw, lh, srgb, nw, nh);
        held.swap(next);
        src = held.data(), lw = nw, lh = nh;
    }
    return p;
}

namespace {
Packed kept_by(const Digest& key, const unsigned char* rgba, int w, int h, bool srgb) {
    std::string kept;
    Packed p;
    if (cache::load("packed", key, kept) && unflat(kept, p) && p.w == w && p.h == h && p.srgb == srgb) return p;
    p = pack(rgba, w, h, srgb);
    cache::store("packed", key, flat(p));
    return p;
}
}  // namespace

Packed pack_kept(const unsigned char* rgba, int w, int h, bool srgb) {
    const Digest key = Hasher{}
                           .text("packed")
                           .text(SG_PACK_CODE)
                           .integer(w)
                           .integer(h)
                           .integer(srgb ? 1 : 0)
                           .bytes(rgba, static_cast<std::size_t>(w) * h * 4)
                           .digest();
    return kept_by(key, rgba, w, h, srgb);
}

namespace {
Digest key_of(int w, int h, bool srgb, const Digest& made_of) {
    return Hasher{}
        .text("packed.of")
        .text(SG_PACK_CODE)
        .integer(w)
        .integer(h)
        .integer(srgb ? 1 : 0)
        .integer(static_cast<int64_t>(made_of.hi))
        .integer(static_cast<int64_t>(made_of.lo))
        .digest();
}
}  // namespace

Packed pack_kept(const unsigned char* rgba, int w, int h, bool srgb, const Digest& made_of) {
    return kept_by(key_of(w, h, srgb, made_of), rgba, w, h, srgb);
}

bool packed_kept(int w, int h, bool srgb, const Digest& made_of, Packed& out) {
    std::string kept;
    Packed p;
    if (!cache::load("packed", key_of(w, h, srgb, made_of), kept) || !unflat(kept, p) || p.w != w || p.h != h || p.srgb != srgb) return false;
    out = std::move(p);
    return true;
}

}  // namespace sg::render
