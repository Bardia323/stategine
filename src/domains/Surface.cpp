#include "sg/domains/Surface.hpp"

namespace sg {

// Its picture is made when it is first painted (pixels()): a world of many
// surfaces is not made to hold them all, blank, before any is painted.
Surface2D::Surface2D(Key id, int cols, int rows, int cell_px) : Spatial2D(id, cols, rows), cell_(cell_px) {}

std::vector<unsigned char>& Surface2D::blank() {
    pixels_.assign(static_cast<std::size_t>(px_w()) * static_cast<std::size_t>(px_h()) * 4, 0);
    return pixels_;
}

const std::vector<unsigned char>& Surface2D::raster() {
    // layout_changed() also refreshes the signature, so it runs either way.
    const bool moved = layout_changed();
    if (stale()) dirty_ = true;
    if (dirty_ || moved) redraw();
    return pixels_;
}

const unsigned char* Surface2D::pixel(int x, int y) const {
    // Not painted yet: blank, as every pixel of it is.
    static const unsigned char none[4] = {0, 0, 0, 0};
    if (pixels_.empty()) return none;
    return &pixels_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(px_w()) +
                     static_cast<std::size_t>(x)) *
                    4];
}

void Surface2D::resize(int cols, int rows) {
    if (cols == this->cols() && rows == this->rows()) return;
    params().set(keys::w, static_cast<int64_t>(std::max(1, cols)));
    params().set(keys::h, static_cast<int64_t>(std::max(1, rows)));
    std::vector<unsigned char>().swap(pixels_);  // made again, at the new size, when painted
    dirty_ = true;
}

bool Surface2D::layout_changed() {
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

void Surface2D::paint_board() {
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
}

void Surface2D::put(int x, int y, int r, int g, int b) {
    if (x < 0 || y < 0 || x >= px_w() || y >= px_h()) return;
    const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(px_w()) +
                           static_cast<std::size_t>(x)) *
                          4;
    std::vector<unsigned char>& px = pixels();
    px[i] = static_cast<unsigned char>(r);
    px[i + 1] = static_cast<unsigned char>(g);
    px[i + 2] = static_cast<unsigned char>(b);
    px[i + 3] = 255;
}

void Surface2D::fill(int r, int g, int b) {
    for (int y = 0; y < px_h(); ++y)
        for (int x = 0; x < px_w(); ++x) put(x, y, r, g, b);
}

void Surface2D::hline(int y, int r, int g, int b) {
    for (int x = 0; x < px_w(); ++x) put(x, y, r, g, b);
}

void Surface2D::vline(int x, int r, int g, int b) {
    for (int y = 0; y < px_h(); ++y) put(x, y, r, g, b);
}

void Surface2D::box(int x0, int y0, int w, int h, int r, int g, int b) {
    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x) put(x, y, r, g, b);
}

void Surface2D::outline(int x0, int y0, int w, int h, int r, int g, int b) {
    for (int x = x0; x < x0 + w; ++x) {
        put(x, y0, r, g, b);
        put(x, y0 + h - 1, r, g, b);
    }
    for (int y = y0; y < y0 + h; ++y) {
        put(x0, y, r, g, b);
        put(x0 + w - 1, y, r, g, b);
    }
}

}  // namespace sg
