// Stategine - Surface: a 2D state that can hand over its own pixels.
//
// This is what makes any 2D state usable as an interface inside another domain:
// the state stays a plain category of sprites and morphisms, and `raster()` is
// one more view of it - here as RGBA a texture can take. The buffer is reused
// between frames and only redrawn when something actually changed.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "sg/domains/Spatial.hpp"

namespace sg {

class Surface2D : public Spatial2D {
public:
    Surface2D(Key id, int cols, int rows, int cell_px = 24)
        : Spatial2D(id, cols, rows), cell_(cell_px) {
        set_integrating(false);  // an interface is edited, not simulated
        pixels_.assign(static_cast<std::size_t>(px_w()) * static_cast<std::size_t>(px_h()) * 4,
                       0);
    }

    Key kind() const override { return Key{"surface2d"}; }

    int px_w() const { return cols() * cell_; }
    int px_h() const { return rows() * cell_; }
    int cell() const { return cell_; }

    void set_selection(Key id) {
        if (selected_ != id) dirty_ = true;
        selected_ = id;
    }
    Key selection() const { return selected_; }

    void set_background(int r, int g, int b) {
        bg_ = {r, g, b};
        dirty_ = true;
    }

    // Redraw only when a sprite moved, the selection changed, or somebody asked.
    const std::vector<unsigned char>& raster() {
        // layout_changed() also refreshes the signature, so it runs either way.
        const bool moved = layout_changed();
        if (dirty_ || moved) redraw();
        return pixels_;
    }

    void invalidate() { dirty_ = true; }

    // Bumped on every redraw, so a texture upload can be skipped when nothing
    // about the surface changed this frame.
    uint64_t revision() const { return revision_; }

private:
    struct Rgb {
        int r, g, b;
    };

    bool layout_changed() {
        signature_scratch_.clear();
        for (const auto& e : elements()) {
            if ((e.kind != kinds::sprite && e.kind != kinds::tile) || !e.alive) continue;
            signature_scratch_.push_back(e.params.num(keys::x));
            signature_scratch_.push_back(e.params.num(keys::y));
            signature_scratch_.push_back(e.params.num(keys::r, 0.9));
            signature_scratch_.push_back(e.params.num(keys::g, 0.5));
            signature_scratch_.push_back(e.params.num(keys::b, 0.2));
        }
        if (signature_scratch_ != signature_) {
            signature_ = signature_scratch_;
            return true;
        }
        return false;
    }

    void redraw() {
        fill(bg_.r, bg_.g, bg_.b);

        // Tiles first: they are the board, not pieces on it. A surface whose
        // tiles are laid out like the thing it edits reads as a picture of it
        // rather than as an arbitrary grid - and, more usefully, its
        // neighbouring cells are the neighbouring positions.
        for (const auto& e : elements()) {
            if (e.kind != kinds::tile || !e.alive) continue;
            const int cx = clampi(static_cast<int>(std::lround(e.params.num(keys::x))), 0,
                                  cols() - 1);
            const int cy = clampi(static_cast<int>(std::lround(e.params.num(keys::y))), 0,
                                  rows() - 1);
            box(cx * cell_, cy * cell_, cell_, cell_,
                static_cast<int>(e.params.num(keys::r, 0.2) * 255),
                static_cast<int>(e.params.num(keys::g, 0.2) * 255),
                static_cast<int>(e.params.num(keys::b, 0.2) * 255));
        }

        for (int c = 0; c <= cols(); ++c) vline(c * cell_, 60, 70, 90);
        for (int r = 0; r <= rows(); ++r) hline(r * cell_, 60, 70, 90);

        for (const auto& e : elements()) {
            if (e.kind != kinds::sprite || !e.alive) continue;
            const int cx = clampi(static_cast<int>(std::lround(e.params.num(keys::x))), 0,
                                  cols() - 1);
            const int cy = clampi(static_cast<int>(std::lround(e.params.num(keys::y))), 0,
                                  rows() - 1);
            const bool sel = (e.id == selected_);
            const int r = static_cast<int>(e.params.num(keys::r, 0.9) * 255);
            const int g = static_cast<int>(e.params.num(keys::g, 0.5) * 255);
            const int b = static_cast<int>(e.params.num(keys::b, 0.2) * 255);
            const int pad = sel ? 2 : 4;
            box(cx * cell_ + pad, cy * cell_ + pad, cell_ - 2 * pad, cell_ - 2 * pad, r, g, b);
            // A soft drop shadow, so tokens read as objects on a board.
            box(cx * cell_ + pad + 2, cy * cell_ + cell_ - pad, cell_ - 2 * pad - 2, 2,
                r / 4, g / 4, b / 4);
            if (sel) outline(cx * cell_ + 1, cy * cell_ + 1, cell_ - 2, cell_ - 2, 255, 230, 120);
        }
        dirty_ = false;
        ++revision_;
    }

    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void put(int x, int y, int r, int g, int b) {
        if (x < 0 || y < 0 || x >= px_w() || y >= px_h()) return;
        const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(px_w()) +
                               static_cast<std::size_t>(x)) *
                              4;
        pixels_[i] = static_cast<unsigned char>(r);
        pixels_[i + 1] = static_cast<unsigned char>(g);
        pixels_[i + 2] = static_cast<unsigned char>(b);
        pixels_[i + 3] = 255;
    }

    void fill(int r, int g, int b) {
        for (int y = 0; y < px_h(); ++y)
            for (int x = 0; x < px_w(); ++x) put(x, y, r, g, b);
    }

    void hline(int y, int r, int g, int b) {
        for (int x = 0; x < px_w(); ++x) put(x, y, r, g, b);
    }

    void vline(int x, int r, int g, int b) {
        for (int y = 0; y < px_h(); ++y) put(x, y, r, g, b);
    }

    void box(int x0, int y0, int w, int h, int r, int g, int b) {
        for (int y = y0; y < y0 + h; ++y)
            for (int x = x0; x < x0 + w; ++x) put(x, y, r, g, b);
    }

    void outline(int x0, int y0, int w, int h, int r, int g, int b) {
        for (int x = x0; x < x0 + w; ++x) {
            put(x, y0, r, g, b);
            put(x, y0 + h - 1, r, g, b);
        }
        for (int y = y0; y < y0 + h; ++y) {
            put(x0, y, r, g, b);
            put(x0 + w - 1, y, r, g, b);
        }
    }

    int cell_;
    Rgb bg_{18, 22, 32};
    Key selected_;
    bool dirty_ = true;
    uint64_t revision_ = 0;
    std::vector<unsigned char> pixels_;
    std::vector<double> signature_;
    std::vector<double> signature_scratch_;
};

}  // namespace sg
