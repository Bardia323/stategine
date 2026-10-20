// A frame of traced light: what each pixel's surface is (the scene drawn
// as surfaces alone), then the rays - the sun's at every pixel; the light
// from all round and what is mirrored at a part of them - each picture kept
// over frames where what it shows held still, smoothed within its surfaces,
// and laid on every pixel; the scene drawn next reads them (uTrace).
#include "Tracer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "TraceShaders.hpp"

namespace sg::render {

void GLWorldView::TracerGone::operator()(trace::Tracer* t) const { delete t; }

bool GLWorldView::tracing() {
    if (root_ || output_ || baking_ || surface_only_ != 0 || !q_.trace) return false;
    if (setting(post_, passes::scene, "trace", 0.0) <= 0.0) return false;
    if (!trace_) trace_.reset(new trace::Tracer(*this));
    trace::Asked a;
    const auto said = [&](const char* name, double fallback) { return setting(post_, passes::scene, name, fallback); };
    a.sun = said("trace.sun", 1.0) > 0.0;
    a.lamps = said("trace.lamps", 1.0) > 0.0;
    a.rough = static_cast<float>(std::clamp(said("trace.mirror", 0.22), 0.0, 1.0));
    a.mirror = a.rough > 0.0;
    const double scale = said("trace.scale", 0.5);
    a.step = scale >= 0.99 ? 1 : (scale >= 0.49 ? 2 : 4);
    a.far = static_cast<float>(std::max(said("trace.far", 1000.0), 1.0));
    a.reach = static_cast<float>(std::clamp(said("trace.reach", 150.0), 1.0, static_cast<double>(a.far)));
    a.sky = static_cast<float>(std::max(said("trace.sky", 1.0), 0.0));
    a.brightest = static_cast<float>(std::max(said("trace.brightest", 4.0), 0.01));
    a.now = said("trace.now", 0.0) > 0.0;
    a.hold = static_cast<float>(std::clamp(said("trace.hold", 24.0), 1.0, 4096.0));
    trace_->asked = a;
    return a.any();
}

void GLWorldView::trace_frame(const std::vector<PlacedRoom>& rooms, const Camera& cam, float aspect) {
    if (trace_) trace_->frame(rooms, cam, aspect);
}

void GLWorldView::trace_after(const std::vector<PlacedRoom>& rooms) {
    if (trace_) trace_->after(rooms);
}

bool GLWorldView::trace_sun_ready() const { return trace_ && trace_->sun_ready(); }

void GLWorldView::trace_to(const gl::Program& p) const {
    if (trace_) trace_->to_scene(p);
    else p.set("uTrace", 0.0f, 0.0f, 0.0f, 0.0f);
}

void GLWorldView::remember_for_trace(const std::vector<Light>& lights, const gl::Vec3& sun_dir, const gl::Vec3& sun_color, const Atmosphere& a,
                                     const Spatial3D* world) {
    if (trace_) trace_->remember(lights, sun_dir, sun_color, a, world);
}

}  // namespace sg::render

namespace sg::render::trace {

namespace {
// The units the tracing programs read from: past every one the scene uses,
// and past what the sky's pictures are bound to (2, 3, 14, 15: sky_to).
constexpr int kSurfUnit = 20, kNodesUnit = 21, kTrisUnit = 22, kInstsUnit = 23, kMasksUnit = 24;
constexpr int kPoint0Unit = 25, kMax0Unit = 26, kPoint1Unit = 27, kMax1Unit = 28, kSkyLightUnit = 29;
constexpr int kSunSeenUnit = 11;
constexpr int kNewUnit = 5, kKeptUnit = 6, kLastSurfUnit = 7, kPictureUnit = 9, kHitsUnit = 10;
// And the units the scene reads the traced pictures from.
constexpr int kSceneSurf = 20, kSceneSun = 21, kSceneLamps = 22, kSceneMirror = 23;

void ensure(gl::RenderTarget& t, int w, int h, gl::GLenum format, bool depth = false) {
    if (t.valid() && t.width() == w && t.height() == h) return;
    t.create(w, h, format, 0, depth);
    // (Made: nothing in it - a card may give it whatever was there before.)
    t.bind();
    gl::glDisable(gl::GL_SCISSOR_TEST);
    gl::glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | (depth ? gl::GL_DEPTH_BUFFER_BIT : 0u));
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
}
// (SG_TRACE_TIMES=1: where a frame's time goes, said every 60 frames - each
// part waited for, so only for looking. SG_TRACE_DUMP=<folder>: at frame
// SG_TRACE_DUMP_AT (60), each picture written there as a .pfm.)
struct Clock {
    bool on = std::getenv("SG_TRACE_TIMES") != nullptr;
    std::chrono::steady_clock::time_point at;
    void start() {
        if (on) gl::glFinish(), at = std::chrono::steady_clock::now();
    }
    double lap() {
        if (!on) return 0.0;
        gl::glFinish();
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - at).count();
        at = now;
        return ms;
    }
};
void dump(const gl::RenderTarget& t, const std::string& path) {
    std::vector<float> px(static_cast<std::size_t>(t.width()) * t.height() * 4);
    gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, t.framebuffer());
    gl::glReadPixels(0, 0, t.width(), t.height(), gl::GL_RGBA, gl::GL_FLOAT, px.data());
    std::ofstream out(path, std::ios::binary);
    out << "PF\n" << t.width() << " " << t.height() << "\n-1.0\n";
    for (std::size_t i = 0; i < px.size(); i += 4) out.write(reinterpret_cast<const char*>(&px[i]), 12);
}
uint64_t fnv(uint64_t h, const void* p, std::size_t n) {
    const unsigned char* c = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < n; ++i) h = (h ^ c[i]) * 1099511628211ULL;
    return h;
}
}  // namespace

void Tracer::remember(const std::vector<GLWorldView::Light>& lights, const gl::Vec3& sun_dir, const gl::Vec3& sun_color,
                      const GLWorldView::Atmosphere& a, const Spatial3D* world) {
    Seen s;
    s.known = true;
    s.world = world;
    s.open = world && world->params().num(Key{"sky"}, 0.0) > 0.5;
    s.atmosphere = a;
    s.sun_color = sun_color;
    s.sun_dir = sun_dir;
    // The sun the scene lights by (its first not let in through a doorway -
    // or, in a room shut round, the first let in at a window: the rays find
    // the window's frame and bars as they find a branch), as bright as it
    // lights a surface facing it; and the strongest lamps.
    std::vector<const GLWorldView::Light*> lamps;
    const GLWorldView::Light* let_in = nullptr;
    const auto take_sun = [&](const GLWorldView::Light& l) {
        s.sun = true;
        s.sun_dir = gl::normalize(gl::Vec3{l.dir.x, l.dir.y, l.dir.z} * -1.0f);
        const gl::Vec3 air = v_.sun_air_;
        s.sun_light = gl::Vec3{l.color.x * air.x, l.color.y * air.y, l.color.z * air.z} * l.power;
        s.sun_cone = std::clamp(l.size, 0.0f, 0.2f);
    };
    for (const GLWorldView::Light& l : lights) {
        if (l.sun && !l.gated && !s.sun) {
            take_sun(l);
        } else if (l.sun && l.gated && !l.hung_only && !let_in && l.power > 0.0f) {
            let_in = &l;
        } else if (!l.sun && !l.indirect && !l.gated && l.power > 0.0f) {
            lamps.push_back(&l);
        }
    }
    if (!s.sun && let_in) take_sun(*let_in);
    // (The strongest where the eye is: by their power over how far off they are.)
    const gl::Vec3 eye = v_.cam_eye_;
    const auto near = [&](const GLWorldView::Light* l) {
        const gl::Vec3 d = gl::Vec3{l->pos.x, l->pos.y, l->pos.z} - eye;
        return l->power / (1.0f + gl::dot(d, d));
    };
    std::sort(lamps.begin(), lamps.end(), [&](const GLWorldView::Light* x, const GLWorldView::Light* y) { return near(x) > near(y); });
    s.lamps = static_cast<int>(std::min<std::size_t>(lamps.size(), 8));
    for (int k = 0; k < s.lamps; ++k) {
        const GLWorldView::Light& l = *lamps[static_cast<std::size_t>(k)];
        s.lamp_at[static_cast<std::size_t>(k) * 4 + 0] = l.pos.x, s.lamp_at[static_cast<std::size_t>(k) * 4 + 1] = l.pos.y;
        s.lamp_at[static_cast<std::size_t>(k) * 4 + 2] = l.pos.z, s.lamp_at[static_cast<std::size_t>(k) * 4 + 3] = l.range;
        s.lamp_light[static_cast<std::size_t>(k) * 3 + 0] = l.color.x * l.power, s.lamp_light[static_cast<std::size_t>(k) * 3 + 1] = l.color.y * l.power;
        s.lamp_light[static_cast<std::size_t>(k) * 3 + 2] = l.color.z * l.power;
        const gl::Vec3 way = gl::normalize(gl::Vec3{l.dir.x, l.dir.y, l.dir.z});
        float* w = &s.lamp_way[static_cast<std::size_t>(k) * 6];
        w[0] = way.x, w[1] = way.y, w[2] = way.z, w[3] = l.falloff, w[4] = std::cos(l.inner), w[5] = std::cos(l.outer);
    }
    // The nearest few whose shadows are traced: to a point of the bulb each
    // ray, so a shadow is as soft as the bulb is big - a cage round a bulb
    // throws a blur, not a net. (No bulb said: a small one, a centimetre and
    // a half.)
    s.shadow_lamps = std::min(s.lamps, kShadowLamps);
    for (int k = 0; k < s.shadow_lamps; ++k) {
        const GLWorldView::Light& l = *lamps[static_cast<std::size_t>(k)];
        float* a = &s.shadow_lamp[static_cast<std::size_t>(k) * 4];
        a[0] = l.pos.x, a[1] = l.pos.y, a[2] = l.pos.z, a[3] = std::max(l.size, 0.015f);
    }
    seen_ = s;
}

void Tracer::sized(int w, int h) {
    if (w == w_ && h == h_) return;
    w_ = w, h_ = h;
    history_ = false;
    const int hw = (w + asked.step - 1) / asked.step, hh = (h + asked.step - 1) / asked.step;
    for (gl::RenderTarget& t : surf_) ensure(t, w, h, gl::GL_RGBA32F, true);
    for (gl::RenderTarget* t : {&sun_raw_, &sun_kept_[0], &sun_kept_[1], &sun_a_, &sun_b_, &mirror_full_}) ensure(*t, w, h, gl::GL_RGBA16F);
    if (asked.lamps)
        for (gl::RenderTarget* t : {&lamps_raw_, &lamps_kept_[0], &lamps_kept_[1], &lamps_a_, &lamps_b_}) ensure(*t, w, h, gl::GL_RGBA16F);
    // (The light from all round and mirrors at a part of the pixels: made at
    // half each way whatever is asked, used whole when every pixel is traced.)
    for (gl::RenderTarget* t : {&mirror_raw_, &mirror_kept_[0], &mirror_kept_[1], &mirror_a_, &mirror_b_})
        ensure(*t, hw, hh, gl::GL_RGBA16F);
}

void Tracer::common(const gl::Program& p, int step) const {
    surf_[now_].bind_color(kSurfUnit);
    p.set("uSurf", kSurfUnit);
    p.set("uSurfSize", static_cast<float>(w_), static_cast<float>(h_));
    p.set("uEye", eye_);
    p.set("uFwd", fwd_);
    p.set("uRight", right_);
    p.set("uUp", up_);
    p.set("uJitter", jitter_[0], jitter_[1]);
    p.set("uStep", step);
    p.set("uFrame", frame_);
}

void Tracer::world_to(const gl::Program& p) const {
    nodes_tex_.bind(kNodesUnit), tris_tex_.bind(kTrisUnit), insts_tex_.bind(kInstsUnit), masks_tex_.bind(kMasksUnit);
    p.set("uNodes", kNodesUnit), p.set("uTris", kTrisUnit), p.set("uInsts", kInstsUnit), p.set("uMasks", kMasksUnit);
    p.set("uTlas", tlas_root_);
    const char* at[2] = {"uHfAt0", "uHfAt1"};
    const char* top[2] = {"uHfTop0", "uHfTop1"};
    for (int k = 0; k < 2; ++k) {
        const Square& s = squares_[k];
        if (s.n > 0) s.points.bind_color(k == 0 ? kPoint0Unit : kPoint1Unit), s.atlas.bind_color(k == 0 ? kMax0Unit : kMax1Unit);
        p.set(at[k], s.x0, s.z0, s.cell, static_cast<float>(s.n));
        p.set(top[k], s.n > 0 ? static_cast<int>(std::lround(std::log2(static_cast<double>(s.n)))) : 0);
    }
    p.set("uHfPoint0", kPoint0Unit), p.set("uHfMax0", kMax0Unit), p.set("uHfPoint1", kPoint1Unit), p.set("uHfMax1", kMax1Unit);
    p.set("uHfAlbedo", ground_albedo_);
    // The sky as the scene drew it last.
    const auto sky = seen_.world ? v_.skies_.find(seen_.world) : v_.skies_.end();
    v_.sky_to(p, sky != v_.skies_.end() ? sky->second.get() : nullptr, seen_.atmosphere);
    const auto colour = [&](const char* name, float x, float y, float z) {
        const std::string n = name;
        p.set(name, gl::Vec3{static_cast<float>(v_.setting(v_.post_, passes::scene, (n + ".x").c_str(), x)),
                             static_cast<float>(v_.setting(v_.post_, passes::scene, (n + ".y").c_str(), y)),
                             static_cast<float>(v_.setting(v_.post_, passes::scene, (n + ".z").c_str(), z))});
    };
    colour("uSkyTop", 0.20f, 0.40f, 0.75f);
    colour("uSkyHorizon", 0.72f, 0.78f, 0.84f);
    p.set("uSunDir", seen_.sun_dir);
    p.set("uSunColor", seen_.sun_color);
    p.set("uSunLight", seen_.sun ? seen_.sun_light : gl::Vec3{0, 0, 0});
    p.set("uSunCone", seen_.sun_cone);
    p.set("uSkyEyeY", eye_.y);
    p.set("uSkyGain", asked.sky);
    sky_light_.bind_color(kSkyLightUnit);
    p.set("uSkyLight", kSkyLightUnit);
    p.set("uFar", asked.far);
    static const int skip = std::getenv("SG_TRACE_SKIP") ? std::atoi(std::getenv("SG_TRACE_SKIP")) : 0;
    p.set("uSkip", skip);
    p.set("uReach", asked.reach);
    // (The sun as the eye's pixels have it, once its pass is made this frame.)
    const bool sun_seen = sun_out_ && asked.sun && seen_.sun && sun_made_ == frame_;
    if (sun_seen) sun_out_->bind_color(kSunSeenUnit);
    p.set("uSunSeen", kSunSeenUnit);
    p.set("uSunSeenOn", sun_seen ? 1 : 0);
    p.set("uVP", vp_);
    p.set("uLampCount", seen_.lamps);
    for (int k = 0; k < seen_.lamps; ++k) {
        const std::string i = std::to_string(k);
        const float* a = &seen_.lamp_at[static_cast<std::size_t>(k) * 4];
        const float* l = &seen_.lamp_light[static_cast<std::size_t>(k) * 3];
        p.set(("uLampAt[" + i + "]").c_str(), a[0], a[1], a[2], a[3]);
        p.set(("uLampLight[" + i + "]").c_str(), gl::Vec3{l[0], l[1], l[2]});
        const float* w = &seen_.lamp_way[static_cast<std::size_t>(k) * 6];
        p.set(("uLampWay[" + i + "]").c_str(), w[0], w[1], w[2], w[3]);
        p.set(("uLampCone[" + i + "]").c_str(), w[4], w[5]);
    }
    p.set("uOpenSky", seen_.open ? 1.0f : 0.0f);
    p.set("uBrightest", asked.brightest);
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

void Tracer::pass(const gl::Program& p, const gl::RenderTarget& into) const {
    (void)p;
    into.bind();
    gl::glViewport(0, 0, into.width(), into.height());
    v_.screen_.draw();
}

void Tracer::make_sky_light() {
    // Made again when the sun, its sky or the clouds over it have changed -
    // the clouds drifting, every half second or so.
    uint64_t of = 1469598103934665603ULL;
    for (float f : {seen_.sun_dir.x, seen_.sun_dir.y, seen_.sun_dir.z, seen_.sun_color.x, seen_.sun_color.y, seen_.sun_color.z, seen_.atmosphere.weight,
                    seen_.atmosphere.haze, seen_.atmosphere.sun, asked.sky, std::floor(eye_.y / 50.0f), static_cast<float>(frame_ / 15)})
        of = fnv(of, &f, sizeof f);
    of = fnv(of, &seen_.world, sizeof seen_.world);
    if (of == sky_of_ && sky_light_.valid()) return;
    sky_of_ = of;
    ensure(sky_light_, 32, 32, gl::GL_RGBA16F);
    if (!sky_prog_) sky_prog_ = std::make_unique<gl::Program>(gl::post_vs(), sky_fs().c_str(), "trace sky");
    const gl::Program& p = *sky_prog_;
    p.use();
    world_to(p);
    pass(p, sky_light_);
}

const gl::RenderTarget& Tracer::carry(const gl::RenderTarget& now, gl::RenderTarget kept[2], int step, bool mirror, float clamp) {
    if (!carry_prog_) carry_prog_ = std::make_unique<gl::Program>(gl::post_vs(), carry_fs().c_str(), "trace carry");
    const gl::Program& p = *carry_prog_;
    p.use();
    common(p, step);
    now.bind_color(kNewUnit);
    kept[now_ ^ 1].bind_color(kKeptUnit);
    surf_[now_ ^ 1].bind_color(kLastSurfUnit);
    p.set("uNew", kNewUnit), p.set("uKept", kKeptUnit), p.set("uLastSurf", kLastSurfUnit);
    p.set("uLastVP", last_vp_);
    p.set("uLastEye", last_eye_);
    p.set("uMost", history_ ? asked.hold : 1.0f);
    p.set("uMirror", mirror ? 1.0f : 0.0f);
    p.set("uClamp", clamp);
    p.set("uKeptSize", static_cast<float>(kept[now_].width()), static_cast<float>(kept[now_].height()));
    pass(p, kept[now_]);
    return kept[now_];
}

const gl::RenderTarget& Tracer::smooth(const gl::RenderTarget& from, gl::RenderTarget& a, gl::RenderTarget& b, int step, int passes, float penumbra,
                                       float sharp) {
    if (!smooth_prog_) smooth_prog_ = std::make_unique<gl::Program>(gl::post_vs(), smooth_fs().c_str(), "trace smooth");
    const gl::Program& p = *smooth_prog_;
    p.use();
    common(p, step);
    sun_raw_.bind_color(kHitsUnit);
    p.set("uHits", kHitsUnit);
    p.set("uPicture", kPictureUnit);
    p.set("uPenumbra", penumbra);
    p.set("uPixel", pixel_);
    p.set("uSharp", sharp);
    const gl::RenderTarget* in = &from;
    for (int i = 0; i < passes; ++i) {
        gl::RenderTarget& out = (i & 1) ? b : a;
        in->bind_color(kPictureUnit);
        p.set("uSpread", 1 << i);
        pass(p, out);
        in = &out;
    }
    return *in;
}

void Tracer::widen(const gl::RenderTarget& from, gl::RenderTarget& into, int step, float rough) {
    if (!widen_prog_) widen_prog_ = std::make_unique<gl::Program>(gl::post_vs(), widen_fs().c_str(), "trace widen");
    const gl::Program& p = *widen_prog_;
    p.use();
    common(p, step);
    from.bind_color(kPictureUnit);
    p.set("uPicture", kPictureUnit);
    p.set("uRough", rough);
    pass(p, into);
}

void Tracer::frame(const std::vector<PlacedRoom>& rooms, const GLWorldView::Camera& cam, float aspect) {
    v_.trace_beside_ = false;
    if (cam.ortho > 0.0f || rooms.empty() || !rooms.front().room) {
        made_ = false;
        return;
    }
    gl::RenderTarget& scene = v_.scene_ms();
    // (A setting that changes the pictures' sizes starts them again.)
    if (!(sized_for_ == asked)) w_ = h_ = 0, sized_for_ = asked, made_ = false;
    sized(scene.width(), scene.height());
    now_ ^= 1;
    ++frame_;

    // The eye as the surfaces are drawn from.
    const float zfar = static_cast<float>(rooms.front().room->params().num(Key{"far"}, 120.0));
    const gl::Mat4 lens = GLWorldView::projection_of(cam, aspect, GLWorldView::kNear, zfar);
    const gl::Mat4 looking = gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, cam.up);
    jitter_[0] = 2.0f * v_.jitter_x_ / static_cast<float>(w_), jitter_[1] = 2.0f * v_.jitter_y_ / static_cast<float>(h_);
    vp_ = gl::Mat4::translate({jitter_[0], jitter_[1], 0.0f}) * lens * looking;
    eye_ = cam.eye;
    fwd_ = gl::normalize(cam.forward);
    const gl::Vec3 r = gl::normalize(gl::cross(fwd_, cam.up)), u = gl::cross(r, fwd_);
    const float tan_y = std::tan(cam.fov * 0.5f);
    right_ = r * (tan_y * aspect), up_ = u * tan_y;
    pixel_ = 2.0f * tan_y / static_cast<float>(h_);

    // What each pixel's surface is: drawn beside the scene, and traced after
    // it - the scene reading the last frame's light, carried to where each
    // pixel now is - or (`trace.now`) drawn alone first, and traced before
    // the scene, which reads this frame's.
    if (!asked.now) {
        v_.trace_beside_ = true;
        return;
    }
    Clock clock;
    clock.start();
    v_.surface_only_ = GLWorldView::kTraceSurface;
    v_.draw_world(rooms, cam, aspect, surf_[now_], 0, GLWorldView::kNear, v_.own_skip_, v_.own_clips_);
    v_.surface_only_ = 0;
    surfaces_ms_ = clock.lap();
    trace(rooms);
}

void Tracer::after(const std::vector<PlacedRoom>& rooms) {
    if (!v_.trace_beside_) return;
    v_.trace_beside_ = false;
    Clock clock;
    clock.start();
    v_.scene_ms().second_to(surf_[now_]);
    surfaces_ms_ = clock.lap();
    trace(rooms);
}

void Tracer::trace(const std::vector<PlacedRoom>& rooms) {
    Clock clock;
    double ms[10] = {};
    ms[8] = surfaces_ms_;
    clock.start();
    // The world as rays meet it.
    gather_things(rooms);
    ms[0] = clock.lap();
    lay_ground(rooms, eye_);
    ms[1] = clock.lap();
    if (ms[1] > 0.2) ++relaid_;
    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glDisable(gl::GL_BLEND);
    gl::glDisable(gl::GL_CULL_FACE);
    gl::glDisable(gl::GL_SCISSOR_TEST);
    make_sky_light();

    const int step = asked.step;
    if (asked.sun && seen_.sun) {
        if (!sun_prog_) sun_prog_ = std::make_unique<gl::Program>(gl::post_vs(), sun_fs().c_str(), "trace sun");
        const gl::Program& p = *sun_prog_;
        p.use();
        common(p, 1);
        world_to(p);
        pass(p, sun_raw_);
        ms[2] = clock.lap();
        const gl::RenderTarget& kept = carry(sun_raw_, sun_kept_, 1, false, -1.0f);
        sun_out_ = &smooth(kept, sun_a_, sun_b_, 1, 3, seen_.sun_cone);
        sun_made_ = frame_;
        ms[3] = clock.lap();
    }
    if (asked.lamps && seen_.shadow_lamps > 0) {
        if (!lamps_prog_) lamps_prog_ = std::make_unique<gl::Program>(gl::post_vs(), lamps_fs().c_str(), "trace lamps");
        const gl::Program& p = *lamps_prog_;
        p.use();
        common(p, 1);
        world_to(p);
        p.set("uShadowLamps", seen_.shadow_lamps);
        for (int k = 0; k < seen_.shadow_lamps; ++k) {
            const float* a = &seen_.shadow_lamp[static_cast<std::size_t>(k) * 4];
            p.set(("uShadowLamp[" + std::to_string(k) + "]").c_str(), a[0], a[1], a[2], a[3]);
        }
        pass(p, lamps_raw_);
        ms[4] = clock.lap();
        const gl::RenderTarget& kept = carry(lamps_raw_, lamps_kept_, 1, false, -1.0f);
        lamps_out_ = &smooth(kept, lamps_a_, lamps_b_, 1, 2, 0.0f);
        lamps_made_ = frame_;
        ms[5] = clock.lap();
    }
    if (asked.mirror) {
        if (!mirror_prog_) mirror_prog_ = std::make_unique<gl::Program>(gl::post_vs(), mirror_fs().c_str(), "trace mirror");
        const gl::Program& p = *mirror_prog_;
        p.use();
        common(p, step);
        world_to(p);
        p.set("uRough", asked.rough);
        pass(p, mirror_raw_);
        ms[6] = clock.lap();
        const gl::RenderTarget& kept = carry(mirror_raw_, mirror_kept_, step, true, 2.0f);
        const gl::RenderTarget& smoothed = smooth(kept, mirror_a_, mirror_b_, step, 2, 0.0f, 0.12f);
        widen(smoothed, mirror_full_, step, asked.rough);
        ms[7] = clock.lap();
    }
    if (clock.on && frame_ % 60 == 0)
        std::printf("trace ms: surfaces %.2f (ground laid %d) | things %.2f ground %.2f | sun %.2f +%.2f | lamps %.2f +%.2f | mirror %.2f +%.2f | %zu shapes, %zu tris, %zu instances (%zu tris placed)\n", ms[8], relaid_, ms[0],
                    ms[1], ms[2], ms[3], ms[4], ms[5], ms[6], ms[7], shapes_.size(), tris_.size() / 12, insts_.size() / 16, placed_tris_);
    if (const char* folder = std::getenv("SG_TRACE_DUMP")) {
        const char* at = std::getenv("SG_TRACE_DUMP_AT");
        if (frame_ == (at ? std::atoi(at) : 60)) {
            const std::string f = folder;
            dump(surf_[now_], f + "/surf.pfm");
            if (sun_out_) dump(*sun_out_, f + "/sun.pfm"), dump(sun_raw_, f + "/sun_raw.pfm");
            if (asked.mirror) dump(mirror_full_, f + "/mirror.pfm"), dump(mirror_raw_, f + "/mirror_raw.pfm");
            if (squares_[0].n) dump(squares_[0].points, f + "/ground0.pfm"), dump(squares_[1].points, f + "/ground1.pfm");
            dump(sky_light_, f + "/sky.pfm");
        }
    }
    gl::glActiveTexture(gl::GL_TEXTURE0);
    gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
    last_vp_ = vp_, last_eye_ = eye_;
    shown_ = now_, shown_vp_ = vp_, shown_eye_ = eye_;
    history_ = true;
    made_ = seen_.known;
    made_at_ = v_.frame_count_;
    // (The scene's target as the scene left it.)
    gl::glViewport(0, 0, w_, h_);
}

void Tracer::to_scene(const gl::Program& p) const {
    if (!fresh()) {
        p.set("uTrace", 0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const bool sun = asked.sun && seen_.sun && sun_out_;
    const int lamps = asked.lamps && lamps_out_ && lamps_made_ + 1 >= frame_ ? seen_.shadow_lamps : 0;
    p.set("uTrace", sun ? 1.0f : 0.0f, static_cast<float>(lamps), asked.mirror ? asked.rough : 0.0f, 0.0f);
    p.set("uTraceSunDir", seen_.sun_dir);
    for (int k = 0; k < kShadowLamps; ++k) {
        const float* a = &seen_.shadow_lamp[static_cast<std::size_t>(k) * 4];
        p.set(("uTraceLamp[" + std::to_string(k) + "]").c_str(), gl::Vec3{a[0], a[1], a[2]});
    }
    if (lamps) lamps_out_->bind_color(kSceneLamps);
    p.set("uTraceLamps", kSceneLamps);
    p.set("uTraceVP", shown_vp_);
    p.set("uTraceEye", shown_eye_);
    surf_[shown_].bind_color(kSceneSurf);
    if (sun) sun_out_->bind_color(kSceneSun);
    mirror_full_.bind_color(kSceneMirror);
    p.set("uTraceSurf", kSceneSurf), p.set("uTraceSun", kSceneSun), p.set("uTraceMirror", kSceneMirror);
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

}  // namespace sg::render::trace
