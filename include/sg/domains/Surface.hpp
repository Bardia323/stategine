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
    void on_restored() override { invalidate(); }

    Surface2D(Key id, int cols, int rows, int cell_px = 24);

    Key kind() const override { return Key{"surface2d"}; }

    int px_w() const { return cols() * cell_; }
    int px_h() const { return rows() * cell_; }
    int cell() const { return cell_; }

    void set_selection(Key id) {
        if (selected_ != id) dirty_ = true;
        selected_ = id;
    }
    Key selection() const { return selected_; }

    // Whether the pixels are sRGB-encoded - colours picked by eye, photographs,
    // anything painted to look right on a screen - rather than linear values.
    // A renderer decodes sRGB before lighting it; without that, a painted
    // colour is lit as if it were brighter than it is, and comes out washed
    // out. Off by default, which is how the board has always been drawn.
    void set_srgb(bool on) { srgb_ = on; }
    bool srgb() const { return srgb_; }

    void set_background(int r, int g, int b) {
        bg_ = {r, g, b};
        dirty_ = true;
    }

    // Redraw only when a sprite moved, the selection changed, or somebody asked.
    const std::vector<unsigned char>& raster();

    // One pixel of the last raster: r, g, b, a.
    const unsigned char* pixel(int x, int y) const;

    void invalidate() { dirty_ = true; }

    // A new size, in cells: the raster is made again at that size, and drawn
    // afresh. A renderer showing it makes its texture again to match.
    void resize(int cols, int rows);

    // Bumped on every redraw, so a texture upload can be skipped when nothing
    // about the surface changed this frame.
    uint64_t revision() const { return revision_; }

protected:
    // What the surface looks like. The default is a board: tiles, a grid, and
    // sprites as tokens on it. A surface that is a sheet of text, a photograph
    // or a screen paints itself instead, with the helpers below, and calls
    // invalidate() whenever what it shows changes. Sprites and tiles moving
    // still trigger a repaint on their own.
    virtual void paint() { paint_board(); }

    // Whether what the picture shows has changed since it was painted, as the
    // surface's own data says - a copy of something it shows, carried in by a
    // functor, stamped anew. A picture is memoised on its data, never told.
    virtual bool stale() { return false; }

    // The whole buffer, RGBA rows top to bottom, for painters that write it
    // directly (blending, blitting an image). Made, blank, when first wanted:
    // a surface nobody has painted yet holds no picture, only its size.
    std::vector<unsigned char>& pixels() { return pixels_.empty() ? blank() : pixels_; }

    struct Rgb {
        int r, g, b;
    };

    bool layout_changed();

    void redraw() {
        pixels();
        paint();
        dirty_ = false;
        ++revision_;
    }

    void paint_board();

    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void put(int x, int y, int r, int g, int b);

    void fill(int r, int g, int b);

    void hline(int y, int r, int g, int b);

    void vline(int x, int r, int g, int b);

    void box(int x0, int y0, int w, int h, int r, int g, int b);

    void outline(int x0, int y0, int w, int h, int r, int g, int b);

private:
    std::vector<unsigned char>& blank();

    int cell_;
    Rgb bg_{18, 22, 32};
    Key selected_;
    bool dirty_ = true;
    bool srgb_ = false;
    uint64_t revision_ = 0;
    std::vector<unsigned char> pixels_;
    std::vector<double> signature_;
    std::vector<double> signature_scratch_;
};

}  // namespace sg
