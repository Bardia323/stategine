// The land worn: rain running off it (hydraulic erosion, a drop at a time,
// each carrying what its speed lets it and leaving it where it slows - Beyer's
// droplets), slopes slumping past where they stand (thermal), and how wet each
// place is from where its water comes from.
#include <algorithm>
#include <cmath>
#include <numeric>

#include "TerrainKernel.hpp"

namespace sg::terrain::kernel {

void smooth(Land& L, int n) {
    std::vector<float> next(L.h.size());
    for (int pass = 0; pass < n; ++pass) {
        for (int j = 0; j <= L.nz; ++j)
            for (int i = 0; i <= L.nx; ++i) {
                double s = 0;
                int c = 0;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        const int a = i + di, b = j + dj;
                        if (a < 0 || b < 0 || a > L.nx || b > L.nz) continue;
                        s += L.h[L.index(a, b)], ++c;
                    }
                next[L.index(i, j)] = float(s / c);
            }
        L.h.swap(next);
    }
}

void thermal(Land& L, int n, double talus_deg) {
    const double most = std::tan(talus_deg * kPi / 180) * L.cell;
    std::vector<float> delta(L.h.size());
    for (int pass = 0; pass < n; ++pass) {
        std::fill(delta.begin(), delta.end(), 0.0f);
        for (int j = 0; j <= L.nz; ++j)
            for (int i = 0; i <= L.nx; ++i) {
                const float h = L.h[L.index(i, j)];
                static const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int a = i + di[k], b = j + dj[k];
                    if (a < 0 || b < 0 || a > L.nx || b > L.nz) continue;
                    const double drop = h - L.h[L.index(a, b)];
                    if (drop <= most) continue;
                    const float move = float((drop - most) * 0.12);
                    delta[L.index(i, j)] -= move, delta[L.index(a, b)] += move;
                }
            }
        for (std::size_t k = 0; k < L.h.size(); ++k) L.h[k] += delta[k];
    }
}

void erode(Land& L, std::vector<float>& flow, int drops, double strength, uint32_t seed) {
    // Each drop starts somewhere at random, runs downhill with some inertia,
    // and carries earth up to what its speed, its water and the slope allow;
    // past that it lays it down. What it takes is taken from a small brush
    // round it, so it carves channels, not pits.
    const double inertia = 0.05, capacity = 4.0, min_slope = 0.01, deposit = 0.3, evaporate = 0.015, gravity = 4.0;
    const double take = 0.3 * strength;
    const int life = 40, radius = 2;
    // The brush: weights of the points round a point, by nearness.
    std::vector<std::pair<int, int>> offs;
    std::vector<double> wts;
    double wsum = 0;
    for (int dj = -radius; dj <= radius; ++dj)
        for (int di = -radius; di <= radius; ++di) {
            const double r = std::sqrt(double(di * di + dj * dj));
            if (r > radius) continue;
            offs.push_back({di, dj}), wts.push_back(radius - r), wsum += radius - r;
        }
    for (double& w : wts) w /= wsum;
    const auto H = [&](int i, int j) { return double(L.h[L.index(std::clamp(i, 0, L.nx), std::clamp(j, 0, L.nz))]); };
    // The height and its slope at a point between the grid's (in cells).
    const auto sample = [&](double x, double z, double& gx, double& gz) {
        const int i = int(x), j = int(z);
        const double u = x - i, v = z - j;
        const double a = H(i, j), b = H(i + 1, j), c = H(i, j + 1), d = H(i + 1, j + 1);
        gx = (b - a) * (1 - v) + (d - c) * v;
        gz = (c - a) * (1 - u) + (d - b) * u;
        return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
    };
    Dice dice(seed * 2654435761u + 1);
    for (int n = 0; n < drops; ++n) {
        double x = dice.next() * (L.nx - 1), z = dice.next() * (L.nz - 1);
        double dx = 0, dz = 0, speed = 1, water = 1, sediment = 0;
        for (int step = 0; step < life; ++step) {
            const int i = int(x), j = int(z);
            if (i < 0 || j < 0 || i >= L.nx || j >= L.nz) break;
            double gx, gz;
            const double h = sample(x, z, gx, gz);
            flow[L.index(i, j)] += float(water);
            dx = dx * inertia - gx * (1 - inertia), dz = dz * inertia - gz * (1 - inertia);
            const double l = std::hypot(dx, dz);
            if (l < 1e-12) break;
            dx /= l, dz /= l;
            const double nx = x + dx, nz = z + dz;
            if (nx < 0 || nz < 0 || nx >= L.nx || nz >= L.nz) break;
            double gx2, gz2;
            const double dh = sample(nx, nz, gx2, gz2) - h;
            const double cap = std::max(-dh, min_slope) * speed * water * capacity;
            const double u = x - i, v = z - j;
            if (sediment > cap || dh > 0) {
                // Laid down: up a rise, only enough to fill it; else what it cannot carry.
                const double put = dh > 0 ? std::min(dh, sediment) : (sediment - cap) * deposit;
                sediment -= put;
                L.h[L.index(i, j)] += float(put * (1 - u) * (1 - v));
                L.h[L.index(i + 1, j)] += float(put * u * (1 - v));
                L.h[L.index(i, j + 1)] += float(put * (1 - u) * v);
                L.h[L.index(i + 1, j + 1)] += float(put * u * v);
            } else {
                const double got = std::min((cap - sediment) * take, -dh);
                for (std::size_t k = 0; k < offs.size(); ++k) {
                    const int a = i + offs[k].first, b = j + offs[k].second;
                    if (a < 0 || b < 0 || a > L.nx || b > L.nz) continue;
                    float& hh = L.h[L.index(a, b)];
                    const float bit = float(got * wts[k]);
                    hh -= bit;
                    sediment += bit;
                }
            }
            speed = std::sqrt(std::max(0.0, speed * speed - dh * gravity));
            water *= 1 - evaporate;
            x = nx, z = nz;
        }
    }
}

void wetness(Land& L, const std::vector<float>& flow, const std::vector<float>& swamp) {
    // Where water gathers: each point passes what reaches it (its own rain and
    // all that runs to it) to its lowest neighbour - so valleys and the feet
    // of slopes are wetter than ridges - plus nearness to standing water, a
    // swamp, and the paths the eroding rain took.
    const std::size_t n = L.h.size();
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return L.h[a] > L.h[b]; });
    std::vector<float> acc(n, 1.0f);
    for (std::size_t k : order) {
        const int i = int(k % std::size_t(L.nx + 1)), j = int(k / std::size_t(L.nx + 1));
        std::size_t low = k;
        float lh = L.h[k];
        for (int dj = -1; dj <= 1; ++dj)
            for (int di = -1; di <= 1; ++di) {
                const int a = i + di, b = j + dj;
                if ((di == 0 && dj == 0) || a < 0 || b < 0 || a > L.nx || b > L.nz) continue;
                const std::size_t q = L.index(a, b);
                if (L.h[q] < lh) lh = L.h[q], low = q;
            }
        if (low != k) acc[low] += acc[k];
    }
    double most_flow = 1;
    for (float f : flow) most_flow = std::max(most_flow, double(f));
    // What runs together over a hundred square metres begins to show.
    const double cells100 = 100.0 / (L.cell * L.cell);
    for (std::size_t k = 0; k < n; ++k) {
        const double gathered = std::clamp(std::log(acc[k] / cells100 + 1.0) / std::log(60.0), 0.0, 1.0);
        const double worn = std::clamp(std::sqrt(flow[k] / most_flow) * 1.5, 0.0, 1.0);
        const double near = L.water[k] < 0 ? 1.0 : std::exp(-L.water[k] / 4.0);
        L.wet[k] = float(std::clamp(std::max({gathered * 0.8, worn, near * 0.95, double(swamp[k])}), 0.0, 1.0));
    }
}

}  // namespace sg::terrain::kernel
