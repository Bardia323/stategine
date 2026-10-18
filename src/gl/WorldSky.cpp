// The GL view's skies: as a planet's (a look's `atmosphere`) - the three
// pictures the scene reads the sky and the air between from - and its clouds
// as a volume (`clouds.cover`) - the dome of the sky above with them in it.
// Each made again only when what it is made for changes; the dome a part at
// a time, as the clouds drift with world time. And the fog pooled where a
// view looks (sg/domains/Fog.hpp), for its scene and its air.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <cmath>

#include "WorldHash.hpp"
#include "sg/domains/Fog.hpp"

namespace sg::render {

auto GLWorldView::atmosphere_of(const Mix& look) const -> Atmosphere {
    Atmosphere a;
    a.weight = std::clamp(static_cast<float>(setting(look, passes::scene, "atmosphere", 0.0)), 0.0f, 1.0f);
    if (a.weight <= 0.0f) return a;
    a.scale = std::max(static_cast<float>(setting(look, passes::scene, "atmosphere.scale", a.scale)), 1e-6f);
    a.altitude = std::clamp(static_cast<float>(setting(look, passes::scene, "atmosphere.altitude", a.altitude)), 0.0f, 90.0f);
    a.sun = std::max(static_cast<float>(setting(look, passes::scene, "atmosphere.sun", a.sun)), 0.0f);
    a.haze = std::max(static_cast<float>(setting(look, passes::scene, "atmosphere.haze", a.haze)), 0.0f);
    a.ground = std::clamp(static_cast<float>(setting(look, passes::scene, "atmosphere.ground", a.ground)), 0.0f, 1.0f);
    return a;
}

auto GLWorldView::clouds_of(const Mix& look) const -> Clouds {
    Clouds c;
    const auto said = [&](const char* name, float fallback) { return static_cast<float>(setting(look, passes::scene, name, fallback)); };
    c.cover = std::clamp(said("clouds.cover", 0.0f), 0.0f, 1.0f);
    if (c.cover <= 0.0f) return c;
    c.base = std::max(said("clouds.base", c.base), 0.0f);
    c.top = std::max(said("clouds.top", c.top), c.base + 100.0f);
    c.density = std::max(said("clouds.density", c.density), 0.0f);
    c.size = std::max(said("clouds.size", c.size), 100.0f);
    c.fray = std::clamp(said("clouds.fray", c.fray), 0.0f, 1.0f);
    c.bright = std::max(said("clouds.bright", c.bright), 0.0f);
    c.wind = said("clouds.wind", c.wind);
    c.wind_dir = said("clouds.wind_dir", c.wind_dir);
    c.lean = said("clouds.lean", c.lean);
    c.far = std::clamp(said("clouds.far", c.far), 1000.0f, 200000.0f);
    c.steps = std::clamp(static_cast<int>(said("clouds.steps", static_cast<float>(c.steps))), 8, 256);
    c.dome = std::clamp(static_cast<int>(said("clouds.dome", static_cast<float>(c.dome))) / 4 * 4, 64, 4096);
    return c;
}

auto GLWorldView::sky_for(const Spatial3D* world, const Atmosphere& a, const Clouds& clouds, const gl::Vec3& sun_dir, const gl::Vec3& sun_color,
                          const gl::Vec3& eye, double time, const Mix& look) -> const Sky* {
    if (a.weight <= 0.0f && clouds.cover <= 0.0f) return nullptr;
    // (Skies no view has asked for in a while let go.)
    for (auto it = skies_.begin(); it != skies_.end();)
        it = it->second->used + 600 < frame_count_ ? skies_.erase(it) : std::next(it);
    std::unique_ptr<Sky>& slot = skies_[world];
    if (!slot) slot = std::make_unique<Sky>();
    Sky& s = *slot;
    s.used = frame_count_;
    s.atmosphere = a.weight > 0.0f, s.cloudy = clouds.cover > 0.0f;
    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glDisable(gl::GL_CULL_FACE);
    gl::glDisable(gl::GL_BLEND);
    gl::glDisable(gl::GL_SCISSOR_TEST);
    if (s.atmosphere) {
        if (!sky_view_prog_) {
            sky_transmittance_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::atmosphere_transmittance_fs(), "atmosphere transmittance");
            sky_multiple_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::atmosphere_multiple_fs(), "atmosphere multiple");
            sky_view_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::atmosphere_sky_fs(), "atmosphere sky");
        }
        const auto made = [](gl::RenderTarget& t, int w, int h) {
            if (t.valid() && t.width() == w && t.height() == h) return false;
            t.create(w, h, gl::GL_RGBA16F, 0, false);
            return true;
        };
        bool fresh = made(s.transmittance, 256, 64);
        fresh = made(s.multiple, 32, 32) || fresh;
        fresh = made(s.view, 192, 108) || fresh;
        // What the air is made of; then where the eye is in it (to a metre)
        // and how high the sun is.
        uint64_t air = 1469598103934665603ULL;
        for (float f : {a.haze, a.ground}) air = mix_bits(air, f);
        const float up = std::clamp(gl::normalize(sun_dir + gl::Vec3{0.0f, 1e-4f, 0.0f}).y, -1.0f, 1.0f);
        const float eye_km = std::clamp(6360.0f + a.altitude + eye.y * a.scale, 6360.001f, 6459.99f);
        uint64_t seen = air;
        for (float f : {up, std::round(eye_km * 1000.0f)}) seen = mix_bits(seen, f);
        if (fresh || s.air != air || s.seen != seen) {
            const auto air_of = [&](const gl::Program& p, const gl::RenderTarget& t) {
                p.use();
                p.set("uAtmoSize", static_cast<float>(t.width()), static_cast<float>(t.height()));
                p.set("uAtmoHaze", a.haze);
                p.set("uAtmoGround", a.ground);
                p.set("uAtmoTransmittance", 15);
                p.set("uAtmoMultiple", 14);
                t.bind();
            };
            if (fresh || s.air != air) {
                air_of(*sky_transmittance_prog_, s.transmittance);
                screen_.draw();
                s.transmittance.bind_color(15);
                air_of(*sky_multiple_prog_, s.multiple);
                screen_.draw();
                s.air = air;
            }
            s.transmittance.bind_color(15);
            s.multiple.bind_color(14);
            air_of(*sky_view_prog_, s.view);
            sky_view_prog_->set("uAtmoEye", eye_km);
            sky_view_prog_->set("uAtmoSunUp", up);
            screen_.draw();
            s.seen = seen;
            ++times_.skies_made;
        }
    }
    // (Its clouds once a frame, whichever of the frame's views of it asks first.)
    if (s.cloudy && s.dome_frame != frame_count_) make_dome(s, a, clouds, sun_dir, sun_color, eye, time, look);
    gl::glActiveTexture(gl::GL_TEXTURE0);
    return &s;
}

void GLWorldView::make_dome(Sky& s, const Atmosphere& a, const Clouds& clouds, const gl::Vec3& sun_dir, const gl::Vec3& sun_color,
                            const gl::Vec3& eye, double time, const Mix& look) {
    s.dome_frame = frame_count_;
    // The noise, once: 64 texels each way, repeating.
    constexpr int kNoise = 64;
    if (cloud_noise_.ensure(kNoise, kNoise, kNoise, /*volume=*/true, /*tiles=*/true)) {
        const gl::Program noise(gl::post_vs(), gl::cloud_noise_fs(), "cloud noise");
        noise.use();
        noise.set("uNoiseSize", static_cast<float>(kNoise));
        for (int g = 0; g < kNoise / gl::LayerArray::kGroup; ++g) {
            cloud_noise_.bind_group(g);
            noise.set("uNoiseGroup", g * gl::LayerArray::kGroup);
            screen_.draw();
        }
    }
    if (!cloud_dome_prog_) cloud_dome_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::cloud_dome_fs(), "cloud dome");
    bool fresh = false;
    for (gl::RenderTarget& t : s.dome)
        if (!t.valid() || t.width() != clouds.dome) t.create(clouds.dome, clouds.dome, gl::GL_RGBA16F, 0, false), fresh = true;
    // Made whole again when what the rest of it was made for has gone too
    // far for it to stand: the clouds or the air as the look says, the eye
    // moved more than a few tens of metres at once, world time gone back or
    // on by more than a few seconds, the sun turned more than a little.
    uint64_t of = 1469598103934665603ULL;
    for (float f : {clouds.cover, clouds.base, clouds.top, clouds.density, clouds.size, clouds.fray, clouds.bright, clouds.wind, clouds.wind_dir,
                    clouds.lean, clouds.far, static_cast<float>(clouds.steps), a.weight, a.scale, a.altitude, a.sun, a.haze, a.ground})
        of = mix_bits(of, f);
    const gl::Vec3 sun = gl::normalize(sun_dir + gl::Vec3{0.0f, 1e-4f, 0.0f});
    const gl::Vec3 moved = eye - s.dome_eye;
    // Its texels are the clouds at the world time each was last made, a
    // frame to sixteen frames ago: how far the clouds have gone since the
    // last frame, in that time (the wind, and their boiling up), says how far
    // behind the rest may stand - kept half and half with what they were
    // while they hardly move, less the faster they go, and the whole made
    // again once they go half a metre a frame (world time run fast). Its
    // time is only ever the world's: a world whose time stands still has
    // clouds that stand still, however many frames are drawn.
    const double went = std::abs(time - s.dome_time) * (std::abs(static_cast<double>(clouds.wind)) + 0.4);
    if (of != s.clouds || gl::dot(moved, moved) > 50.0f * 50.0f || time < s.dome_time || went > 0.5 ||
        gl::dot(sun, s.dome_sun) < std::cos(0.1f))
        fresh = true;
    const bool same = !fresh && went == 0.0 && gl::dot(moved, moved) == 0.0f && sun.x == s.dome_sun.x && sun.y == s.dome_sun.y && sun.z == s.dome_sun.z;
    s.still = same ? s.still + 1 : 0;
    s.clouds = of, s.dome_eye = eye, s.dome_time = time, s.dome_sun = sun;
    // Nothing it is made for moved - not the world's time, the eye or the sun
    // - for long enough that every texel has been made four times over: it
    // rests, as it is (and its shadow with it), until something does.
    if (s.still > 64) return;
    // Where the wind has carried them by now, and how far they have boiled up
    // (wrapped where every reading of the noise wraps, so it stays exact).
    const double wrap = static_cast<double>(clouds.size) * 100.0, carried = static_cast<double>(clouds.wind) * time;
    const float drift_x = static_cast<float>(std::fmod(-carried * std::cos(clouds.wind_dir), wrap));
    const float drift_z = static_cast<float>(std::fmod(-carried * std::sin(clouds.wind_dir), wrap));
    const float boil = static_cast<float>(std::fmod(-0.4 * time, wrap));
    // Each frame one texel of every four by four, in an order that spreads them.
    static constexpr int kOrder[16] = {0, 10, 2, 8, 5, 15, 7, 13, 1, 11, 3, 9, 4, 14, 6, 12};
    const gl::Program& p = *cloud_dome_prog_;
    p.use();
    p.set("uDomeSize", static_cast<float>(clouds.dome), static_cast<float>(clouds.dome));
    p.set("uDomePart", fresh ? -1 : kOrder[s.part]);
    s.part = (s.part + 1) % 16;
    p.set("uDomeKeep", fresh ? 0.0f : static_cast<float>(0.5 * std::max(0.0, 1.0 - went / 0.5)));
    p.set("uSpin", static_cast<float>(s.spin++ % 64u));
    p.set("uEye", eye);
    p.set("uPlanet", s.atmosphere ? 6360.0f / a.scale : 6.36e6f);
    p.set("uCloudLayer", clouds.base, clouds.top, clouds.cover, clouds.density);
    p.set("uCloudShape", clouds.size, clouds.fray, clouds.bright, clouds.far);
    const gl::Vec3 drift{drift_x, boil, drift_z};
    const float lean_x = clouds.lean * std::cos(clouds.wind_dir), lean_z = clouds.lean * std::sin(clouds.wind_dir);
    p.set("uCloudDrift", drift);
    p.set("uCloudLean", lean_x, lean_z);
    p.set("uCloudSteps", clouds.steps);
    p.set("uSunDir", sun_dir);
    p.set("uSunColor", sun_color);
    const auto colour = [&](const char* name, float x, float y, float z) {
        const std::string n = name;
        p.set(name, gl::Vec3{static_cast<float>(setting(look, passes::scene, (n + ".x").c_str(), x)),
                             static_cast<float>(setting(look, passes::scene, (n + ".y").c_str(), y)),
                             static_cast<float>(setting(look, passes::scene, (n + ".z").c_str(), z))});
    };
    colour("uSkyTop", 0.20f, 0.40f, 0.75f);
    colour("uSkyHorizon", 0.72f, 0.78f, 0.84f);
    p.set("uAtmo", s.atmosphere ? a.weight : 0.0f);
    p.set("uAtmoScale", a.scale);
    p.set("uAtmoAltitude", a.altitude);
    p.set("uAtmoSun", a.sun);
    p.set("uAtmoHaze", a.haze);
    p.set("uAtmoGround", a.ground);
    p.set("uAtmoSky", 14);
    p.set("uAtmoTransmittance", 15);
    p.set("uAtmoMultiple", 14);
    p.set("uDomeLast", 3);
    p.set("uCloudNoise", 4);
    if (s.atmosphere) s.view.bind_color(14), s.transmittance.bind_color(15);
    s.dome[s.dome_now].bind_color(3);
    cloud_noise_.bind_color(4);
    s.dome_now ^= 1;
    s.dome[s.dome_now].bind();
    screen_.draw();
    if (fresh) ++times_.domes_made;
    // And their shadow on the ground round the eye: 512 texels across 16 km,
    // its middle kept to whole texels so it does not crawl as the eye goes.
    if (!cloud_shadow_prog_) cloud_shadow_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::cloud_shadow_fs(), "cloud shadow");
    constexpr int kShadow = 512;
    constexpr float kHalf = 8000.0f, kTexel = 2.0f * kHalf / kShadow;
    if (!s.shadow.valid()) s.shadow.create(kShadow, kShadow, gl::GL_RGBA16F, 0, false);
    s.shadow_x = std::round(eye.x / kTexel) * kTexel, s.shadow_z = std::round(eye.z / kTexel) * kTexel, s.shadow_half = kHalf;
    const gl::Program& m = *cloud_shadow_prog_;
    m.use();
    m.set("uMapSize", static_cast<float>(kShadow), static_cast<float>(kShadow));
    m.set("uCloudShadowAt", s.shadow_x, s.shadow_z, kHalf, 1.0f);
    m.set("uSunDir", sun_dir);
    m.set("uCloudLayer", clouds.base, clouds.top, clouds.cover, clouds.density);
    m.set("uCloudShape", clouds.size, clouds.fray, clouds.bright, clouds.far);
    m.set("uCloudDrift", drift);
    m.set("uCloudLean", lean_x, lean_z);
    m.set("uCloudNoise", 4);
    s.shadow.bind();
    screen_.draw();
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

void GLWorldView::sky_to(const gl::Program& p, const Sky* sky, const Atmosphere& a) const {
    const bool air = sky && sky->atmosphere, cloudy = sky && sky->cloudy;
    p.set("uAtmo", air ? a.weight : 0.0f);
    p.set("uCloudVolume", cloudy ? 1.0f : 0.0f);
    if (air) {
        p.set("uAtmoScale", a.scale);
        p.set("uAtmoAltitude", a.altitude);
        p.set("uAtmoSun", a.sun);
        p.set("uAtmoHaze", a.haze);
        p.set("uAtmoSky", 14);
        p.set("uAtmoTransmittance", 15);
        sky->view.bind_color(14);
        sky->transmittance.bind_color(15);
    }
    if (cloudy) {
        p.set("uCloudDome", 3);
        p.set("uCloudShadowMap", 2);
        p.set("uCloudShadowAt", sky->shadow_x, sky->shadow_z, sky->shadow_half, 1.0f);
        sky->dome[sky->dome_now].bind_color(3);
        sky->shadow.bind_color(2);
    } else {
        p.set("uCloudShadowAt", 0.0f, 0.0f, 1.0f, 0.0f);
    }
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

auto GLWorldView::pooled_fog(const std::vector<PlacedRoom>& rooms, const gl::Vec3& eye) const -> Pooled {
    Pooled f;
    // The fogs of the view's rooms nearest the eye (by how far it is from
    // their boxes, not their middles: one it stands in is the nearest).
    struct Near {
        double far;
        const PlacedRoom* placed;
        const Element* e;
        Pose at;
    };
    std::vector<Near> found;
    for (const PlacedRoom& placed : rooms) {
        if (!placed.room) continue;
        for (const Element* e : index_of(*placed.room).fogs) {
            if (!e->alive) continue;
            const Pose at = compose_pose(placed.pose, pose_of(*placed.room, *e));
            const double hx = 0.5 * e->params.num(keys::sx, 1.0), hy = 0.5 * e->params.num(keys::sy, 1.0), hz = 0.5 * e->params.num(keys::sz, 1.0);
            const double dx = at.position.x - eye.x, dy = at.position.y + hy - eye.y, dz = at.position.z - eye.z;
            found.push_back({std::max(std::sqrt(dx * dx + dy * dy + dz * dz) - std::sqrt(hx * hx + hy * hy + hz * hz), 0.0), &placed, e, at});
        }
    }
    if (found.empty()) return f;
    std::stable_sort(found.begin(), found.end(), [](const Near& a, const Near& b) { return a.far < b.far; });
    const StateGraph* graph = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr);
    uint64_t of = 1469598103934665603ULL;
    for (const Near& n : found) {
        if (f.count >= kMaxFogs) break;
        const Params& p = n.e->params;
        const int k = f.count++;
        const bool own = p.has(keys::r) || p.has(keys::g) || p.has(keys::b);
        const float at[4] = {static_cast<float>(n.at.position.x), static_cast<float>(n.at.position.y), static_cast<float>(n.at.position.z), static_cast<float>(n.at.yaw)};
        const float size[4] = {static_cast<float>(0.5 * p.num(keys::sx, 1.0)), static_cast<float>(p.num(keys::sy, 1.0)), static_cast<float>(0.5 * p.num(keys::sz, 1.0)),
                               static_cast<float>(std::max(p.num(Key{"density"}, 0.3), 0.0))};
        const float make[4] = {static_cast<float>(std::max(p.num(Key{"soft"}, 0.5), 0.0)), static_cast<float>(std::max(p.num(Key{"fall"}, 0.0), 0.0)),
                               static_cast<float>(std::clamp(p.num(Key{"drift"}, 0.0), 0.0, 1.0)), static_cast<float>(std::max(p.num(Key{"wisp"}, 2.0), 0.1))};
        const float tint[4] = {static_cast<float>(p.num(keys::r, 0.0)), static_cast<float>(p.num(keys::g, 0.0)), static_cast<float>(p.num(keys::b, 0.0)), own ? 1.0f : 0.0f};
        for (int i = 0; i < 4; ++i) {
            f.at[k][i] = at[i], f.size[k][i] = size[i], f.make[k][i] = make[i], f.tint[k][i] = tint[i];
            of = mix_bits(mix_bits(mix_bits(mix_bits(of, at[i]), size[i]), make[i]), tint[i]);
        }
        // (Its wisps move with the time of the world it is in: its own room's.)
        f.time[k] = static_cast<float>(semantic_time(graph, *n.placed->room));
    }
    f.of = fnv(of, static_cast<uint64_t>(f.count));
    return f;
}

void GLWorldView::pooled_to(const gl::Program& p, const Pooled& f) {
    static const char* const at[kMaxFogs] = {"uFogAt[0]", "uFogAt[1]", "uFogAt[2]", "uFogAt[3]"};
    static const char* const size[kMaxFogs] = {"uFogSize[0]", "uFogSize[1]", "uFogSize[2]", "uFogSize[3]"};
    static const char* const make[kMaxFogs] = {"uFogMake[0]", "uFogMake[1]", "uFogMake[2]", "uFogMake[3]"};
    static const char* const tint[kMaxFogs] = {"uFogTint[0]", "uFogTint[1]", "uFogTint[2]", "uFogTint[3]"};
    static const char* const time[kMaxFogs] = {"uFogTime[0]", "uFogTime[1]", "uFogTime[2]", "uFogTime[3]"};
    p.set("uFogCount", f.count);
    for (int k = 0; k < f.count; ++k) {
        p.set(at[k], f.at[k][0], f.at[k][1], f.at[k][2], f.at[k][3]);
        p.set(size[k], f.size[k][0], f.size[k][1], f.size[k][2], f.size[k][3]);
        p.set(make[k], f.make[k][0], f.make[k][1], f.make[k][2], f.make[k][3]);
        p.set(tint[k], f.tint[k][0], f.tint[k][1], f.tint[k][2], f.tint[k][3]);
        p.set(time[k], f.time[k]);
    }
}

}  // namespace sg::render
