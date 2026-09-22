// Stategine - terminal views.
//
// A view reads a state and draws it. It is not a state and not a subclass of
// one, so any state can have several views at once (or none), and adding a
// backend never touches the domain code.
#pragma once

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sg/domains/Console.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg::render {

// A character grid with a couple of drawing helpers, shared by the views below.
class CharCanvas {
public:
    CharCanvas(int w, int h, char fill = ' ') : w_(w), h_(h), rows_(h, std::string(w, fill)) {}

    void put(int x, int y, char c) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
        rows_[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = c;
    }

    void print(std::ostream& os, const std::string& title = {}, bool framed = true) const {
        if (framed) {
            os << "+" << std::string(static_cast<std::size_t>(w_), '-') << "+";
            if (!title.empty()) os << "  " << title;
            os << "\n";
            for (const auto& r : rows_) os << "|" << r << "|\n";
            os << "+" << std::string(static_cast<std::size_t>(w_), '-') << "+\n";
        } else {
            if (!title.empty()) os << title << "\n";
            for (const auto& r : rows_) os << r << "\n";
        }
    }

    int width() const { return w_; }
    int height() const { return h_; }

private:
    int w_, h_;
    std::vector<std::string> rows_;
};

inline char glyph_of(const Element& e, char fallback) {
    const std::string g = e.params.get_or<std::string>(keys::glyph, std::string{});
    return g.empty() ? fallback : g[0];
}

// A 2D state, straight down.
inline void draw(const Spatial2D& s, std::ostream& os = std::cout, bool framed = true) {
    CharCanvas canvas(s.cols(), s.rows(), '.');
    for (const auto& e : s.elements()) {
        if (e.kind != kinds::sprite || !e.alive) continue;
        canvas.put(static_cast<int>(std::lround(e.params.num(keys::x))),
                   static_cast<int>(std::lround(e.params.num(keys::y))), glyph_of(e, '*'));
    }
    canvas.print(os, s.id().str(), framed);
}

// A 3D state seen from above, with the light baked into the shading - the cheap
// way to see the same world the GL renderer draws.
inline void draw_top_down(const Spatial3D& s, int cols, int rows, std::ostream& os = std::cout) {
    const std::string ramp = " .:-=+*#%@";
    CharCanvas canvas(cols, rows, ' ');

    double lx = cols * 0.5, lz = rows * 0.5, power = 14.0;
    for (const auto& e : s.elements()) {
        if (e.kind != kinds::light) continue;
        lx = e.params.num(keys::x);
        lz = e.params.num(keys::z);
        power = e.params.num(keys::intensity, 1.0) * 14.0;
        break;
    }
    for (int z = 0; z < rows; ++z) {
        for (int x = 0; x < cols; ++x) {
            const double d2 = (x - lx) * (x - lx) + (z - lz) * (z - lz);
            const double lit = std::min(1.0, power / (1.0 + d2));
            canvas.put(x, z, ramp[static_cast<std::size_t>(lit * (ramp.size() - 1))]);
        }
    }
    for (const auto& e : s.elements()) {
        if (!e.alive) continue;
        if (e.kind != kinds::mesh && e.kind != kinds::light && e.kind != kinds::portal) continue;
        canvas.put(static_cast<int>(std::lround(e.params.num(keys::x))),
                   static_cast<int>(std::lround(e.params.num(keys::z))), glyph_of(e, '#'));
    }
    canvas.print(os, s.id().str() + " (top-down, x across, z down)");
}

// A console state's scrollback.
inline void draw(const ConsoleState& c, std::ostream& os = std::cout, std::size_t tail = 12) {
    const auto& lines = c.lines();
    const std::size_t start = lines.size() > tail ? lines.size() - tail : 0;
    for (std::size_t i = start; i < lines.size(); ++i) os << lines[i] << "\n";
}

}  // namespace sg::render
