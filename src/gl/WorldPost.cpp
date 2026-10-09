// The GL view's passes after the scene: ambient occlusion, bloom, the
// eye's exposure, the composite and the finish.
#include "sg/gl/World.hpp"

namespace sg::render {

void GLWorldView::run_taa() {
    if (!taa_prog_) taa_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::taa_fs(), "taa");
    const int w = scene_src_->width(), h = scene_src_->height();
    bool fresh = !taa_held_;
    for (gl::RenderTarget& t : taa_history_)
        if (!t.valid() || t.width() != w || t.height() != h) t.create(w, h, gl::GL_RGBA16F, 0, false), fresh = true;
    // How the view moved since the last frame - in one world; carried across
    // a doorway, the picture goes on as it stood. A jump (put somewhere else)
    // starts again.
    gl::Mat4 reproject = gl::Mat4::identity();
    if (taa_world_ == taa_last_world_) {
        const gl::Vec3 d = taa_eye_ - taa_last_eye_;
        if (gl::dot(d, d) > 4.0f) fresh = true;
        reproject = taa_last_vp_ * taa_vp_.inverse();
    }
    gl::RenderTarget& out = taa_history_[1 - taa_front_];
    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glDisable(gl::GL_BLEND);
    out.bind();
    gl::glViewport(0, 0, w, h);
    const gl::Program& p = *taa_prog_;
    p.use();
    p.set("uCurrent", 0);
    p.set("uHistory", 1);
    p.set("uDepth", 2);
    scene_src_->bind_color(0);
    taa_history_[taa_front_].bind_color(1);
    depth_.bind_depth(2);
    p.set("uReproject", reproject);
    p.set("uJitter", 2.0f * jitter_x_ / static_cast<float>(w), 2.0f * jitter_y_ / static_cast<float>(h));
    p.set("uTexel", 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
    p.set("uFresh", fresh ? 1.0f : 0.0f);
    p.set("uReversedZ", gl::reversed_depth() ? 1.0f : 0.0f);
    screen_.draw();
    gl::glActiveTexture(gl::GL_TEXTURE0);
    taa_front_ = 1 - taa_front_;
    taa_held_ = true;
    scene_src_ = &out;
    taa_last_vp_ = taa_vp_;
    taa_last_world_ = taa_world_;
    taa_last_eye_ = taa_eye_;
}

void GLWorldView::run_ao(float strength, float radius) {
    if (!ao_prog_) {
        ao_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_fs(), "ao");
        ao_blur_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_blur_fs(), "ao blur");
        ao_apply_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_apply_fs(), "ao apply");
    }
    gl::glDisable(gl::GL_DEPTH_TEST);
    scene_ms().blit_depth_to(depth_);
    const float tan_half = std::tan(view_.fov * 0.5f);

    ao_a_.bind();
    ao_prog_->use();
    ao_prog_->set("uDepth", 0);
    depth_.bind_depth(0);
    ao_prog_->set("uTexel", 1.0f / static_cast<float>(ao_a_.width()), 1.0f / static_cast<float>(ao_a_.height()));
    ao_prog_->set("uNear", view_.znear);
    ao_prog_->set("uReversedZ", gl::reversed_depth() ? 1.0f : 0.0f);
    ao_prog_->set("uFar", view_.zfar);
    ao_prog_->set("uTanHalf", tan_half);
    ao_prog_->set("uAspect", view_.aspect);
    ao_prog_->set("uRadius", radius);
    screen_.draw();

    ao_b_.bind();
    ao_blur_prog_->use();
    ao_blur_prog_->set("uAO", 0);
    ao_blur_prog_->set("uDepth", 1);
    ao_a_.bind_color(0);
    depth_.bind_depth(1);
    ao_blur_prog_->set("uTexel", 1.0f / static_cast<float>(ao_b_.width()), 1.0f / static_cast<float>(ao_b_.height()));
    ao_blur_prog_->set("uNear", view_.znear);
    ao_blur_prog_->set("uReversedZ", gl::reversed_depth() ? 1.0f : 0.0f);
    ao_blur_prog_->set("uFar", view_.zfar);
    screen_.draw();

    lit_.bind();
    ao_apply_prog_->use();
    ao_apply_prog_->set("uScene", 0);
    ao_apply_prog_->set("uAO", 1);
    ao_apply_prog_->set("uStrength", strength);
    ao_apply_prog_->set("uDepth", 2);
    ao_apply_prog_->set("uNear", view_.znear);
    ao_apply_prog_->set("uReversedZ", gl::reversed_depth() ? 1.0f : 0.0f);
    ao_apply_prog_->set("uFar", view_.zfar);
    ao_apply_prog_->set("uTexel", 1.0f / static_cast<float>(lit_.width()), 1.0f / static_cast<float>(lit_.height()));
    resolve_.bind_color(0);
    ao_b_.bind_color(1);
    depth_.bind_depth(2);
    screen_.draw();
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

void GLWorldView::run_bloom() {
    gl::glDisable(gl::GL_DEPTH_TEST);
    bloom_a_.bind();
    gl::glClear(gl::GL_COLOR_BUFFER_BIT);
    const gl::Program& bright = *program_for(post_.shown(), passes::bright);
    bright.use();
    apply_uniforms(bright, post_, passes::bright);
    bright.set("uScene", 0);
    scene_src_->bind_color(0);
    screen_.draw();

    const gl::Program& blur = *program_for(post_.shown(), passes::blur);
    blur.use();
    apply_uniforms(blur, post_, passes::blur);
    blur.set("uSource", 0);
    const float tx = 1.0f / static_cast<float>(bloom_a_.width());
    const float ty = 1.0f / static_cast<float>(bloom_a_.height());
    const long rounds = std::lround(setting(post_, passes::blur, "passes", q_.bloom_passes));
    for (long i = 0; i < rounds; ++i) {
        bloom_b_.bind();
        blur.set("uDirection", tx, 0.0f);
        bloom_a_.bind_color(0);
        screen_.draw();

        bloom_a_.bind();
        blur.set("uDirection", 0.0f, ty);
        bloom_b_.bind_color(0);
        screen_.draw();
    }
    run_wide_bloom(static_cast<float>(setting(post_, passes::blur, "wide", 0.5)));
}

void GLWorldView::run_wide_bloom(float wide) {
    if (wide <= 0.0f || bloom_levels_ == 0) return;
    if (!bloom_down_) {
        bloom_down_ = std::make_unique<gl::Program>(gl::post_vs(), gl::bloom_down_fs(), "bloom down");
        bloom_up_ = std::make_unique<gl::Program>(gl::post_vs(), gl::bloom_up_fs(), "bloom up");
    }
    const auto texel = [](const gl::RenderTarget& t) {
        return std::pair{1.0f / static_cast<float>(t.width()), 1.0f / static_cast<float>(t.height())};
    };
    bloom_down_->use();
    bloom_down_->set("uSource", 0);
    const gl::RenderTarget* from = &bloom_a_;
    for (int i = 0; i < bloom_levels_; ++i) {
        bloom_chain_[i].bind();
        from->bind_color(0);
        const auto [tx, ty] = texel(*from);
        bloom_down_->set("uTexel", tx, ty);
        bloom_down_->set("uFirst", i == 0 ? 1.0f : 0.0f);
        screen_.draw();
        from = &bloom_chain_[i];
    }
    bloom_up_->use();
    bloom_up_->set("uSource", 0);
    bloom_up_->set("uWeight", 1.0f);
    gl::glEnable(gl::GL_BLEND);
    gl::glBlendFunc(gl::GL_ONE, gl::GL_ONE);
    for (int i = bloom_levels_ - 1; i > 0; --i) {
        bloom_chain_[i - 1].bind();
        bloom_chain_[i].bind_color(0);
        const auto [tx, ty] = texel(bloom_chain_[i]);
        bloom_up_->set("uTexel", tx, ty);
        screen_.draw();
    }
    // Every level has added the whole of the light once: their sum,
    // divided among them, is as bright as the tight bloom it came from.
    bloom_a_.bind();
    bloom_chain_[0].bind_color(0);
    const auto [tx, ty] = texel(bloom_chain_[0]);
    bloom_up_->set("uTexel", tx, ty);
    bloom_up_->set("uWeight", 1.0f / static_cast<float>(bloom_levels_));
    gl::glBlendColor(0.0f, 0.0f, 0.0f, std::min(wide, 1.0f));
    gl::glBlendFunc(gl::GL_CONSTANT_ALPHA, gl::GL_ONE_MINUS_CONSTANT_ALPHA);
    screen_.draw();
    gl::glDisable(gl::GL_BLEND);
}

void GLWorldView::adapt_exposure() {
    if (root_) return;  // a screen's picture, or a doorway's: the eye is the view on the screen's
    Exposure& x = exposure_;
    x.weight = static_cast<float>(std::clamp(setting(post_, passes::scene, "exposure.auto", 0.0), 0.0, 1.0));
    if (x.weight <= 0.0f) {
        // Not adjusting: the next time it does, it starts where it measures.
        x.known = false, x.ev = 0.0;
        x.asked[0] = x.asked[1] = false;
        return;
    }
    // What was asked for last frame is on the card's side by now: read
    // without waiting for this frame's drawing.
    const int last = x.next ^ 1;
    if (x.asked[last]) {
        float rg[2] = {0, 0};
        gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, x.pbo[last]);
        gl::glGetBufferSubData(gl::GL_PIXEL_PACK_BUFFER, 0, sizeof rg, rg);
        gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, 0);
        x.asked[last] = false;
        if (rg[1] > 0.0f && std::isfinite(rg[0])) x.target = exposure_target(rg);
    }
    if (!x.known || x.settle) return;  // measured at once this frame (meter_exposure)
    const double rate = std::max(0.0, setting(post_, passes::scene, "exposure.rate", 1.5));
    const double step = rate * dt_;
    x.ev += std::clamp(x.target - x.ev, -step, step);
}

void GLWorldView::meter_exposure() {
    if (root_ || exposure_.weight <= 0.0f || !scene_src_->valid()) return;
    Exposure& x = exposure_;
    if (!meter_prog_) {
        meter_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::exposure_meter_fs(), "exposure meter");
        x.meter.create(kMeterW, kMeterH, gl::GL_RG16F, 0, false);
        gl::glGenBuffers(2, x.pbo);
        for (gl::GLuint b : x.pbo) {
            gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, b);
            gl::glBufferData(gl::GL_PIXEL_PACK_BUFFER, 2 * sizeof(float), nullptr, gl::GL_STREAM_READ);
        }
        gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, 0);
    }
    gl::glDisable(gl::GL_DEPTH_TEST);
    x.meter.bind();
    meter_prog_->use();
    meter_prog_->set("uScene", 0);
    meter_prog_->set("uCell", 1.0f / kMeterW, 1.0f / kMeterH);
    scene_src_->bind_color(0);
    screen_.draw();
    x.meter.mipmap();  // (leaves the meter's picture bound to unit 0)
    if (!x.known || x.settle) {
        // Settling: read now, and the eye is where it measures this frame.
        float rg[2] = {0, 0};
        gl::glGetTexImage(gl::GL_TEXTURE_2D, kMeterLevels - 1, gl::GL_RG, gl::GL_FLOAT, rg);
        x.target = rg[1] > 0.0f && std::isfinite(rg[0]) ? exposure_target(rg) : 0.0;
        x.ev = x.target;
        x.known = true, x.settle = false;
        x.asked[0] = x.asked[1] = false;
        return;
    }
    gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, x.pbo[x.next]);
    gl::glGetTexImage(gl::GL_TEXTURE_2D, kMeterLevels - 1, gl::GL_RG, gl::GL_FLOAT, nullptr);
    gl::glBindBuffer(gl::GL_PIXEL_PACK_BUFFER, 0);
    x.asked[x.next] = true;
    x.next ^= 1;
}

double GLWorldView::exposure_target(const float rg[2]) const {
    const double mean_log = static_cast<double>(rg[0]) / static_cast<double>(rg[1]);
    const double key = std::max(1e-4, setting(post_, passes::scene, "exposure.key", 0.18));
    const double lo = setting(post_, passes::scene, "exposure.min", -8.0);
    const double hi = setting(post_, passes::scene, "exposure.max", 8.0);
    return std::clamp(std::log2(key) - mean_log, std::min(lo, hi), std::max(lo, hi));
}

float GLWorldView::exposure_gain() const {
    // A doorway drawn in its own look is seen by the eye on the screen: with
    // its adjustment. A screen's picture is a picture: its look's own.
    const Exposure& x = root_ ? (film_of_viewer_ ? root_->exposure_ : exposure_) : exposure_;
    if (root_ && !film_of_viewer_) return 1.0f;
    if (x.weight <= 0.0f || !x.known) return 1.0f;
    return static_cast<float>(std::exp2(static_cast<double>(x.weight) * x.ev));
}

void GLWorldView::composite(int fb_w, int fb_h) {
    // A look that finishes its picture (deband, smear) has it composited
    // aside first; one that does not, straight to the output, as ever.
    Finish fin = finish_of();
    // (A finish that does not build - prepare() says so - is left off.)
    if (fin.on() && !finish_program()) fin = Finish{};
    if (fin.on()) {
        if (!post_frame_.valid() || post_frame_.width() != fb_w || post_frame_.height() != fb_h)
            post_frame_.create(fb_w, fb_h, gl::GL_RGBA16F, 0, false);
        post_frame_.bind();
    } else if (output_) {
        output_->bind();
    } else {
        gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
        gl::glViewport(0, 0, fb_w, fb_h);
    }
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    scene_src_->bind_color(0);
    bloom_a_.bind_color(1);
    if (fog_depth_) depth_.bind_depth(2);
    const float air_density = static_cast<float>(setting(post_, passes::scene, "uFogDensity", 0.0));
    const float air_start = static_cast<float>(setting(post_, passes::scene, "uFogStart", 0.0));
    // The eye's adjustment, on whatever exposure the looks set: the room's,
    // or the attended state's over it.
    const float gain = exposure_gain();
    float exposure = 1.0f;
    if (gain != 1.0f) {
        exposure = static_cast<float>(setting(post_, passes::composite, "uExposure", 1.0));
        if (attend_) {
            const Mix& am = mix(Key{"attend:" + attend_->id().str()}, look_of(*attend_));
            for (const Mix::Part& part : am.parts)
                if (const Element* e = part.look->find(passes::composite); e && e->params.has(Key{"uExposure"}))
                    exposure = static_cast<float>(fader_.value(am, passes::composite, Key{"uExposure"}, 0.0));
        }
        exposure *= gain;
    }

    const auto draw = [&](const gl::Program& p) {
        p.use();
        apply_uniforms(p, post_, passes::composite);
        apply_attended(p, passes::composite);
        if (gain != 1.0f) p.set("uExposure", exposure);
        if (film_of_viewer_) p.set("uGrain", 0.0f);
        if (rays_on_) {
            p.set("uRayDir", to_vec3(rays_dir_));
            p.set("uCamFwd", to_vec3(rays_fwd_));
            p.set("uTanHalf", static_cast<float>(rays_tan_));
            p.set("uRays", static_cast<float>(std::max(0.0, rays_strength_)));
            p.set("uRayColor", gl::Vec3{static_cast<float>(rays_colour_.r), static_cast<float>(rays_colour_.g), static_cast<float>(rays_colour_.b)});
        }
        p.set("uScene", 0);
        p.set("uBloom", 1);
        p.set("uDepth", 2);
        if (fog_depth_) {
            p.set("uDepthView", view_.znear, view_.zfar, std::tan(view_.fov * 0.5f), view_.aspect);
            p.set("uFogReversed", gl::reversed_depth() ? 1.0f : 0.0f);
            p.set("uAirThick", air_density, air_start);
        }
        p.set("uTime", static_cast<float>(world_time_));
        p.set("uTexel", 1.0f / static_cast<float>(fb_w), 1.0f / static_cast<float>(fb_h));
        p.set("uEdgesSmooth", taa_on() ? 1.0f : 0.0f);
        screen_.draw();
    };
    // Looks that share a program share its draw.
    programs_in_mix_.clear();
    for (const Mix::Part& part : post_.parts) {
        const gl::Program* p = program_for(*part.look, passes::composite);
        auto it = std::find_if(programs_in_mix_.begin(), programs_in_mix_.end(),
                               [p](const auto& e) { return e.first == p; });
        if (it == programs_in_mix_.end()) {
            programs_in_mix_.emplace_back(p, part.weight);
        } else {
            it->second += part.weight;
        }
    }
    // A running weighted mean: the k-th program is blended over the ones
    // before it with alpha = its weight / the weight drawn so far.
    float drawn = 0.0f;
    for (const auto& e : programs_in_mix_) {
        drawn += e.second;
        if (drawn == e.second) {
            draw(*e.first);
            continue;
        }
        gl::glEnable(gl::GL_BLEND);
        gl::glBlendColor(0.0f, 0.0f, 0.0f, e.second / drawn);
        gl::glBlendFunc(gl::GL_CONSTANT_ALPHA, gl::GL_ONE_MINUS_CONSTANT_ALPHA);
        draw(*e.first);
        gl::glDisable(gl::GL_BLEND);
    }
    if (fin.on()) finish(fb_w, fb_h, fin);
    gl::glEnable(gl::GL_DEPTH_TEST);
}

GLWorldView::Finish GLWorldView::finish_of() {
    Finish f;
    const Spatial3D* world = last_world_;
    // Debanding is for what nothing was drawn over: a world with a sky has
    // none of that, and pays nothing.
    if (world && world->params().num(Key{"sky"}, 0.0) <= 0.5)
        f.deband = static_cast<float>(std::clamp(setting(post_, passes::composite, "deband", 0.0), 0.0, 1.0));
    // Only the view on the screen smears: a feed's picture, or a view drawn
    // for another, is this frame's alone.
    const double smear = root_ ? 0.0 : std::clamp(setting(post_, passes::composite, "smear", 0.0), 0.0, 1.0);
    f.smear = smear > 0.0;
    if (!f.smear) {
        // Nothing kept while nothing is smeared: no picture held, and the
        // next smear starts from its own first frame.
        if (smear_hist_[0].valid()) smear_hist_[0].destroy(), smear_hist_[1].destroy();
        smear_valid_ = false;
        smear_world_ = Key{};
        smear_look_ = Key{};
        return f;
    }
    // Let go on a cut: a look cut to or from (fade 0), or a world reached by
    // anything but a seam that lets the picture through.
    const LookState* look = world ? &look_of(*world) : nullptr;
    const Key world_id = world ? world->id() : Key{};
    const Key look_id = look ? look->id() : Key{};
    const bool look_cuts = look && look->fade_seconds() <= 0.0;
    if (smear_valid_ && look_id != smear_look_ && (look_cuts || smear_look_cuts_)) smear_valid_ = false;
    if (smear_valid_ && world_id != smear_world_ && !view_carried(smear_world_, world_id)) smear_valid_ = false;
    smear_world_ = world_id;
    smear_look_ = look_id;
    smear_look_cuts_ = look_cuts;
    // `smear` stays of each thirtieth of a second, so a trail is as long at
    // any frame rate. The shell's transient interval, as look fades use: a
    // frame with none (a still, drawn on its own) is shown as it is.
    f.keep = smear_valid_ && dt_ > 0.0 ? static_cast<float>(std::pow(smear, dt_ * 30.0)) : 0.0f;
    const double blur = setting(post_, passes::composite, "smear.blur", 0.0);
    f.blur = blur > 0.0 ? static_cast<float>(blur) : 1.0f;
    return f;
}

bool GLWorldView::view_carried(Key from, Key to) const {
    if (!graph_ || from == Key{} || to == Key{}) return false;
    const auto admits_view = [&](Key state, Key doorway) {
        const State* st = graph_->find(state);
        const Element* e = st ? st->find(doorway) : nullptr;
        if (!e || !e->params.has(Key{"admits"})) return true;
        const auto* words = std::get_if<std::string>(&e->params.get(Key{"admits"}));
        if (!words) return true;
        for (std::size_t at = 0; at < words->size();) {
            const std::size_t end = std::min(words->find(' ', at), words->size());
            if (words->compare(at, end - at, "view") == 0) return true;
            at = end + 1;
        }
        return false;
    };
    for (const Seam& s : graph_->seams()) {
        if (!((s.a == from && s.b == to) || (s.a == to && s.b == from))) continue;
        for (std::size_t i = 0; i < s.boundary_a.size() && i < s.boundary_b.size(); ++i)
            if (admits_view(s.a, s.boundary_a[i]) && admits_view(s.b, s.boundary_b[i])) return true;
    }
    return false;
}

const gl::Program* GLWorldView::finish_program(std::string* error) {
    if (!finish_tried_) {
        finish_tried_ = true;
        try {
            finish_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::finish_fs(), "finish");
            present_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::present_fs(), "present");
        } catch (const std::exception& e) {
            finish_error_ = e.what();
            finish_prog_.reset();
            present_prog_.reset();
        }
    }
    if (error) *error = finish_error_;
    return finish_prog_ && present_prog_ ? finish_prog_.get() : nullptr;
}

void GLWorldView::finish(int fb_w, int fb_h, const Finish& f) {
    const auto to_output = [&] {
        if (output_) {
            output_->bind();
        } else {
            gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
            gl::glViewport(0, 0, fb_w, fb_h);
        }
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    };
    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glDisable(gl::GL_BLEND);
    const gl::Program* p = finish_program();  // (built: composite asked)
    // Where nothing was drawn is read from the view's depth: resolved already
    // if the occlusion or the air's glow read it this frame, else now.
    if (f.deband > 0.0f && !fog_depth_ && setting(post_, passes::composite, "ao", 0.0) <= 0.0)
        scene_ms().blit_depth_to(depth_);
    if (f.smear)
        for (gl::RenderTarget& h : smear_hist_)
            if (!h.valid() || h.width() != fb_w || h.height() != fb_h) {
                h.create(fb_w, fb_h, gl::GL_RGBA16F, 0, false);
                smear_valid_ = false;
            }
    const bool keep = f.smear && smear_valid_ && f.keep > 0.0f;
    // Smearing, the picture is drawn into the history first (it is the last
    // frame shown, next frame) and then put on the output as it is.
    if (f.smear) smear_hist_[1 - smear_front_].bind();
    else to_output();
    // The textures are put back as the composite left them (its picture,
    // its bloom, the depth if it read it, that unit last): what comes after
    // finds the units as it always did, finished or not.
    const auto as_composite_left = [&] {
        scene_src_->bind_color(0);
        bloom_a_.bind_color(1);
        if (fog_depth_) {
            depth_.bind_depth(2);
        } else if (f.smear) {
            // (Nor is the history left on a unit: it is drawn into next frame.)
            gl::glActiveTexture(gl::GL_TEXTURE0 + 2u);
            gl::glBindTexture(gl::GL_TEXTURE_2D, 0);
            gl::glActiveTexture(gl::GL_TEXTURE0 + 1u);
        }
    };
    p->use();
    p->set("uFrame", 0);
    p->set("uDepth", 1);
    // (Not smearing, the history is never read: it shares the frame's unit.)
    p->set("uHistory", f.smear ? 2 : 0);
    post_frame_.bind_color(0);
    depth_.bind_depth(1);
    if (f.smear) smear_hist_[smear_front_].bind_color(2);
    p->set("uTexel", 1.0f / static_cast<float>(fb_w), 1.0f / static_cast<float>(fb_h));
    p->set("uDeband", f.deband);
    p->set("uDebandReversed", gl::reversed_depth() ? 1.0f : 0.0f);
    p->set("uSmearKeep", keep ? f.keep : 0.0f);
    p->set("uSmearBlur", f.blur);
    screen_.draw();
    if (f.smear) {
        smear_front_ = 1 - smear_front_;
        smear_valid_ = true;
        to_output();
        present_prog_->use();
        present_prog_->set("uFrame", 0);
        smear_hist_[smear_front_].bind_color(0);
        screen_.draw();
    }
    as_composite_left();
}

}  // namespace sg::render
