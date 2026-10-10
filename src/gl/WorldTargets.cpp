// The GL view's targets: what a picture is drawn into, made for its size
// and kept, and the part of the screen a view is given.
#include "sg/gl/World.hpp"

namespace sg::render {

void GLWorldView::ensure_resources() {
    if (ready_) return;
    builtin_ = {
        {passes::shadow, {gl::depth_vs(), gl::depth_fs()}},
        {passes::scene, {gl::scene_vs(), gl::scene_fs()}},
        {passes::bright, {gl::post_vs(), gl::bright_fs()}},
        {passes::blur, {gl::post_vs(), gl::blur_fs()}},
        {passes::composite, {gl::post_vs(), gl::composite_fs()}},
    };
    const bool was = preparing_;
    preparing_ = true;  // the built-ins are never late
    for (Key p : passes::all()) program_for(standard_, p);
    std::string cut_error;
    cut_caster_ = shared_program(gl::depth_cut_vs(), gl::depth_cut_fs(), "cut caster", cut_error);
    if (!cut_caster_) std::fprintf(stderr, "! the caster for things cut out of their pictures does not build: %s\n", cut_error.c_str());
    preparing_ = was;
    cube_.create(gl::cube_vertices());
    cylinder_.create(gl::cylinder_vertices());
    sphere_.create(gl::sphere_vertices());
    quad_.create(gl::quad_vertices());
    screen_.create();
    ready_ = true;
}

void GLWorldView::ensure_targets(int w, int h) {
    if (w == target_w_ && h == target_h_) return;
    if (keeps_sizes_) {
        // The set that was drawn into is put by, and the one of this size
        // taken (or made: the first time only).
        if (target_w_ > 0) {
            Targets old;
            swap_targets(old);
            old.w = target_w_, old.h = target_h_;
            parked_.push_back(std::move(old));
        }
        const auto at = std::find_if(parked_.begin(), parked_.end(), [&](const Targets& t) { return t.w == w && t.h == h; });
        if (at != parked_.end()) {
            swap_targets(*at);
            parked_.erase(at);
            target_w_ = w;
            target_h_ = h;
            return;
        }
    }
    make_targets(w, h);
}

void GLWorldView::swap_targets(Targets& t) {
    std::swap(scene_target_, t.scene);
    std::swap(resolve_, t.resolve);
    std::swap(depth_, t.depth);
    std::swap(lit_, t.lit);
    std::swap(ao_a_, t.ao_a);
    std::swap(ao_b_, t.ao_b);
    std::swap(bloom_a_, t.bloom_a);
    std::swap(bloom_b_, t.bloom_b);
    for (int i = 0; i < kBloomLevels; ++i) std::swap(bloom_chain_[i], t.chain[i]);
    std::swap(bloom_levels_, t.levels);
    std::swap(post_frame_, t.post);
    root_pool_.swap(t.roots);  // (the vectors' own storage goes with them: doorways hold their pictures)
    pool_.swap(t.nested);
}

void GLWorldView::keep_sizes(const std::vector<std::pair<int, int>>& sizes) {
    sizes_kept_ = true;
    const auto wanted = [&](int w, int h) { return std::find(sizes.begin(), sizes.end(), std::make_pair(w, h)) != sizes.end(); };
    // What is kept is what the picture may be made at: the sets of sizes it
    // is no longer are given back.
    const auto give_back = [](Targets& t) {
        for (gl::RenderTarget* r : {&t.scene, &t.resolve, &t.depth, &t.lit, &t.ao_a, &t.ao_b, &t.bloom_a, &t.bloom_b, &t.post}) r->destroy();
        for (gl::RenderTarget& r : t.chain) r.destroy();
        for (RootView& v : t.roots) v.target.destroy();
        for (Nested& n : t.nested) n.target.destroy();
    };
    for (auto it = parked_.begin(); it != parked_.end();)
        if (wanted(it->w, it->h)) ++it;
        else give_back(*it), it = parked_.erase(it);
    if (target_w_ > 0 && !wanted(target_w_, target_h_)) {
        Targets old;
        swap_targets(old);
        give_back(old);
        target_w_ = target_h_ = 0;
    }
    if (sizes.empty()) return;
    // Each made now - when the picture is made or sized, not when somebody
    // walks up to a screen - with no pictures for doorways yet: those are
    // made as the world in the picture first shows one (render).
    ensure_resources();
    for (const auto& [w, h] : sizes) ensure_targets(w, h);
}

void GLWorldView::release_targets() {
    keep_sizes({});
    parked_.clear();
}

void GLWorldView::make_targets(int w, int h) {
    target_w_ = w;
    target_h_ = h;
    // As many samples as asked for, if the driver has them.
    gl::GLint most = 0;
    gl::glGetIntegerv(gl::GL_MAX_SAMPLES, &most);
    msaa_ = most > 0 ? std::min(q_.msaa, static_cast<int>(most)) : q_.msaa;
    // (Lent one of the size, it makes none of its own.)
    if (lent_scene_ && lent_scene_->width() == w && lent_scene_->height() == h) scene_target_.destroy();
    else scene_target_.create(w, h, gl::GL_RGBA16F, msaa_, true);
    resolve_.create(w, h, gl::GL_RGBA16F, 0, false);
    depth_.create(w, h, gl::GL_RGBA16F, 0, true, /*depth_texture=*/true);
    // The views through doorways seen through doorways, and the last frame's
    // picture: made now, with the rest, so that no step through a doorway
    // waits on one being made. (A view drawn for another - a feed's, a
    // doorway's own look - makes them as a doorway is first seen in it: most
    // show none, and each is the size of the picture.)
    root_pool_.reserve(kRootViewsMost);  // (never moved: doorways hold their pictures)
    if (!keeps_sizes_ && !root_) {
        root_pool_.resize(std::max(root_pool_.size(), kRootViews));
        for (RootView& v : root_pool_) make_root_view(v, w, h);
        pool_.resize(std::max(pool_.size(), kViewPool));
        for (Nested& n : pool_) {
            fit(n, w / 4, h / 4, true);
            n.frame = 0;
        }
    }
    // (Lent them by the view it is drawn for, it makes none of its own.)
    if (!scratch_lent_) make_scratch(w, h);
}

void GLWorldView::make_scratch(int w, int h) {
    lit_.create(w, h, gl::GL_RGBA16F, 0, false);
    // Full resolution: at half, the occlusion's edges stair-step over the
    // antialiased picture.
    ao_a_.create(w, h, gl::GL_RGBA16F, 0, false);
    ao_b_.create(w, h, gl::GL_RGBA16F, 0, false);
    const int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
    bloom_a_.create(bw, bh, gl::GL_RGBA16F, 0, false);
    bloom_b_.create(bw, bh, gl::GL_RGBA16F, 0, false);
    bloom_levels_ = 0;
    for (int lw = bw / 2, lh = bh / 2; bloom_levels_ < kBloomLevels && lw >= 8 && lh >= 8; lw /= 2, lh /= 2)
        bloom_chain_[bloom_levels_++].create(lw, lh, gl::GL_RGBA16F, 0, false);
}

void GLWorldView::fit(Nested& n, int w, int h, bool anew) {
    // As big as the part of the screen it is given - grown in steps of 128
    // pixels, never past the screen: a few sizes, each made once. Grown ahead
    // of what it is asked for (a quarter more, and half as much again as it
    // was): a doorway coming near grows by a pixel or two a frame, and would
    // otherwise be made again at every step of 128.
    const auto step = [](int v, int most) { return std::min(most, (std::max(v, 1) + 127) / 128 * 128); };
    if (!anew && n.target.valid() && n.target.width() >= w && n.target.height() >= h) return;
    const int was_w = anew || !n.target.valid() ? 0 : n.target.width(), was_h = anew || !n.target.valid() ? 0 : n.target.height();
    const auto ahead = [](int need, int was) { return was > 0 ? std::max(need + need / 4, was + was / 2) : need; };
    n.target.create(step(ahead(w, was_w), target_w_), step(ahead(h, was_h), target_h_), gl::GL_RGBA16F, 0, true);
    n.target.bind();
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
}

void GLWorldView::trade_scratch(GLWorldView& with) {
    std::swap(lit_, with.lit_);
    std::swap(ao_a_, with.ao_a_);
    std::swap(ao_b_, with.ao_b_);
    std::swap(bloom_a_, with.bloom_a_);
    std::swap(bloom_b_, with.bloom_b_);
    for (int i = 0; i < kBloomLevels; ++i) std::swap(bloom_chain_[i], with.bloom_chain_[i]);
    std::swap(bloom_levels_, with.bloom_levels_);
}

gl::RenderTarget& GLWorldView::scene_ms() {
    if (lent_scene_ && lent_scene_->valid() && lent_scene_->width() == target_w_ && lent_scene_->height() == target_h_) return *lent_scene_;
    if (!scene_target_.valid() || scene_target_.width() != target_w_ || scene_target_.height() != target_h_)
        scene_target_.create(target_w_, target_h_, gl::GL_RGBA16F, msaa_, true);
    return scene_target_;
}

void GLWorldView::make_root_view(RootView& v, int w, int h) {
    v.target.create(w, h, gl::GL_RGBA16F, 0, false);
    v.target.bind();
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
}

void GLWorldView::sample_screen(const Rect& r, float fx, float fy) const {
    // Where `r` is in the part of the screen being drawn, 0..1 across it.
    const float w = sub_.x1 - sub_.x0, h = sub_.y1 - sub_.y0;
    scene_->set("uScreenRect", (r.x0 - sub_.x0) / w, (r.y0 - sub_.y0) / h, (r.x1 - sub_.x0) / w, (r.y1 - sub_.y0) / h);
    scene_->set("uViewport", vp_w_, vp_h_);
    if (fx < 1.0f || fy < 1.0f) scene_->set("uUVRect", 0.0f, 0.0f, fx, fy);
}

bool GLWorldView::scissor_to(const Rect& r, int w, int h) {
    if (r.x0 <= -1.0f && r.y0 <= -1.0f && r.x1 >= 1.0f && r.y1 >= 1.0f) return false;
    const int x0 = std::max(static_cast<int>(std::floor((r.x0 * 0.5f + 0.5f) * static_cast<float>(w))) - 2, 0);
    const int y0 = std::max(static_cast<int>(std::floor((r.y0 * 0.5f + 0.5f) * static_cast<float>(h))) - 2, 0);
    const int x1 = std::min(static_cast<int>(std::ceil((r.x1 * 0.5f + 0.5f) * static_cast<float>(w))) + 2, w);
    const int y1 = std::min(static_cast<int>(std::ceil((r.y1 * 0.5f + 0.5f) * static_cast<float>(h))) + 2, h);
    gl::glScissor(x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0));
    gl::glEnable(gl::GL_SCISSOR_TEST);
    return true;
}

}  // namespace sg::render
