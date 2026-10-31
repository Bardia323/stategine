// The light from all round near each surface, gathered in radiance cascades
// over the depth - in place of the ambient occlusion where a look asks
// (`gi.cascades`, the composite's).
//
// Radiance cascades (Sannikov 2023; Osborne & Sannikov 2024; as Freeman,
// Sannikov & Pitt-Francis 2026 set them for 3D, "Split Radiance Cascades"):
// what a point sees all round is cut by distance into intervals, each
// gathered by probes as far apart as the interval is long and in as many
// ways as it needs - near, many probes and few ways; far, few probes and
// many ways - and merged back to front, each interval's light and how much
// it lets through (Porter and Duff's "over"). Here the probes stand on the
// picture's own surfaces (the screen-space form of the method, as Path of
// Exile 2 runs it): cascade n's probes 2^n half-pixels apart, its ways
// 8 x 2^n round the screen (branching 2: four children for each of a
// probe's ways on the screen's plane is two), its interval four times as
// long as the last (length scaling 4, so that the gap between ways at an
// interval's end grows as the gap between probes does - the paper's
// argument for 4 over 2). Each way is one side of a slice of the half
// sphere over the surface, and what an interval holds is its horizon - how
// high what stands in it rises, seen from the probe - and the light of what
// raised it; what is above every interval's horizon is the room's own
// light from all round, the probes' (sg/domains/Probe.hpp): the last
// cascade, the world's, never the screen's. So:
//
//   what is near shuts out the room's light the way it stands (a slice's
//   side at a time, weighed by the surface's own normal, as GTAO weighs it);
//   and gives back its own light in its place - the lamp's pool on the wall
//   onto the floor below it, the red runner onto the skirting.
//
// Nothing is drawn at random: every probe, way and step is where it was
// last frame, so a still picture is still, and nothing is carried from
// frame to frame - nothing lags, nothing ghosts. What is near is bounded in
// metres (`gi.cascades.reach`): beyond it the probes' light is the light,
// as it is wherever the screen does not see.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace sg::render {

namespace {

// (Five cascades: GLWorldView::kCascades.) Each cascade's ways laid out as tiles of the probes' grid: so many across, so many down.
constexpr int kTilesX[5] = {4, 4, 8, 8, 16};
constexpr int kTilesY[5] = {2, 4, 4, 8, 8};
// Steps along each interval: fewer near (it is short), more far.
constexpr int kSteps[5] = {2, 3, 4, 6, 6};
// The first interval's end, half-pixels; each four times the last.
constexpr float kFirst = 2.0f;

const char* kCommon = R"(
uniform vec2  uFull;      // the picture's pixels
uniform float uTanHalf, uAspect;
// A half-pixel's surface: the full pixel at its corner (2t), so every point
// the cascades know is a point of the picture - nothing averaged into air.
vec3 at_half(ivec2 t, float z) {
    vec2 uv = (vec2(t) * 2.0 + 0.5) / uFull;
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * uTanHalf * uAspect * z, ndc.y * uTanHalf * z, -z);
}
)";

const char* prep_fs() {
    static const std::string s = std::string(R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uDepth;
uniform sampler2D uScene;
uniform float uNear, uFar, uReversedZ;
float linear(float d) {
    if (uReversedZ > 0.5) return uNear * uFar / (uNear + d * (uFar - uNear));
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
void main() {
    // (Half the pixels each way: each the full pixel at its corner - its
    // light and how far it is.)
    ivec2 f = ivec2(gl_FragCoord.xy) * 2;
    float d = texelFetch(uDepth, f, 0).r;
    bool none = uReversedZ > 0.5 ? d <= 0.0 : d >= 1.0;
    vec3 c = texelFetch(uScene, f, 0).rgb;
    FragColor = vec4(c, none ? 1e6 : linear(d));
})");
    return s.c_str();
}

// One cascade's intervals, merged with the cascade above it.
const char* trace_fs() {
    static const std::string s = std::string(R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uPrep;   // light and distance, half the pixels
uniform sampler2D uUpper;  // the cascade above, merged (when uHasUpper)
uniform float uHasUpper;
uniform vec2 uGridV;       // this cascade's probes across and down
uniform vec2 uTilesV;      // its ways, as tiles across and down
uniform vec2 uUpperGridV, uUpperTilesV;
#define uGrid ivec2(uGridV)
#define uTiles ivec2(uTilesV)
#define uUpperGrid ivec2(uUpperGridV)
#define uUpperTiles ivec2(uUpperTilesV)
uniform float uSpacing;    // half-pixels between its probes
uniform float uFrom, uTo;  // its interval, half-pixels
uniform int   uSteps;
uniform float uReach;      // metres: what is near
uniform float uJitter;     // 1: the steps turned from probe to probe; 0: all alike
uniform vec2 uHalfV;       // half the picture's pixels
#define uHalf ivec2(uHalfV)
)") + kCommon + R"(
const float PI = 3.14159265;
float ign(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }
vec4 prep(ivec2 t) { return texelFetch(uPrep, clamp(t, ivec2(0), uHalf - 1), 0); }

// The cascade above at a point (half-pixels), in its two ways within one of
// this cascade's: its probes round the point, each as much as it is near and
// at the same distance - their light and horizons averaged over both ways.
vec4 upper(vec2 at, int way, float z) {
    ivec2 tile0 = ivec2((way * 2) % uUpperTiles.x, (way * 2) / uUpperTiles.x);
    ivec2 tile1 = ivec2((way * 2 + 1) % uUpperTiles.x, (way * 2 + 1) / uUpperTiles.x);
    float sp = uSpacing * 2.0;
    vec2 g = at / sp - 0.5;
    ivec2 g0 = ivec2(floor(g));
    vec2 w = g - vec2(g0);
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int k = 0; k < 4; ++k) {
        ivec2 o = ivec2(k & 1, k >> 1);
        ivec2 q = clamp(g0 + o, ivec2(0), uUpperGrid - 1);
        float bw = (o.x == 1 ? w.x : 1.0 - w.x) * (o.y == 1 ? w.y : 1.0 - w.y);
        float zq = prep(ivec2(floor((vec2(q) + 0.5) * sp))).a;
        float dw = exp(-abs(zq - z) / (0.06 * z + 0.02));
        float ww = bw * max(dw, 1e-3);
        sum += (texelFetch(uUpper, q + tile0 * uUpperGrid, 0) + texelFetch(uUpper, q + tile1 * uUpperGrid, 0)) * ww;
        wsum += ww;
    }
    return sum / max(2.0 * wsum, 1e-6);
}

void main() {
    ivec2 x = ivec2(gl_FragCoord.xy);
    ivec2 tile = x / uGrid, p = x - tile * uGrid;
    int way = tile.y * uTiles.x + tile.x;
    int ways = uTiles.x * uTiles.y;
    vec2 pc = (vec2(p) + 0.5) * uSpacing;
    ivec2 tp = ivec2(floor(pc));
    vec4 here = prep(tp);
    float z = here.a;
    if (z > 1e5) { FragColor = vec4(0.0, 0.0, 0.0, -1.0); return; }
    vec3 P = at_half(tp, z);
    vec3 V = normalize(-P);
    // (One side of a slice: a way on the screen.)
    float a = (float(way) + 0.5) / float(ways) * 2.0 * PI;
    vec2 om = vec2(cos(a), sin(a));
    // What is near, in half-pixels here.
    float half_px = 4.0 * z * uTanHalf / uFull.y;
    float to = min(uTo, uReach / half_px);
    float h = -1.0;
    vec3 J = vec3(0.0);
    if (to > uFrom) {
        // (Where along the interval each step lands: the same every frame,
        // turned from probe to probe so neighbours fill each other's gaps.)
        float j = mix(0.5, ign(vec2(p) + float(way) * 7.0), uJitter);
        float fall = 0.4 * uReach;
        for (int i = 0; i < uSteps; ++i) {
            float r = mix(uFrom, uTo, (float(i) + j) / float(uSteps));
            if (r >= to || r < 0.75) continue;
            ivec2 t = ivec2(floor(vec2(tp) + 0.5 + om * r));
            if (any(lessThan(t, ivec2(0))) || any(greaterThanEqual(t, uHalf))) break;
            vec4 q = prep(t);
            if (q.a > 1e5) continue;
            vec3 d = at_half(t, q.a) - P;
            float l = length(d);
            float c = dot(d / max(l, 1e-5), V);
            // (Fading out towards the reach: past it, the probes' light.)
            c = mix(-1.0, c, clamp((uReach - l) / fall, 0.0, 1.0));
            if (c > h) {
                J += q.rgb * (c - h);
                h = c;
            }
        }
        J /= max(h + 1.0, 1e-4);
    }
    // Merged with what is beyond: the cascade above's two ways within this
    // one, where they rise over what this interval holds, are what is seen
    // there; this interval's own light below its horizon.
    if (uHasUpper > 0.5) {
        vec4 u = upper(pc, way, z);
        if (u.a > h) {
            J = (J * (h + 1.0) + u.rgb * (u.a - h)) / max(u.a + 1.0, 1e-4);
            h = u.a;
        }
    }
    FragColor = vec4(J, h);
})";
    return s.c_str();
}

// Each pixel's light from all round near it, from the first cascade's
// probes round it: what is open (as GTAO integrates it, a slice at a time,
// weighed by the surface's own normal) and what is near's light in what is not.
const char* gather_fs() {
    static const std::string s = std::string(R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uDepth;
uniform sampler2D uPrep;
uniform sampler2D uLevel;  // the first cascade, merged
uniform vec2 uGridV, uTilesV, uHalfV;
#define uGrid ivec2(uGridV)
#define uTiles ivec2(uTilesV)
#define uHalf ivec2(uHalfV)
uniform float uNear, uFar, uReversedZ;
)") + kCommon + R"(
const float PI = 3.14159265, HALF_PI = 1.5707963;
float linear(float d) {
    if (uReversedZ > 0.5) return uNear * uFar / (uNear + d * (uFar - uNear));
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
vec3 view_at(vec2 uv) {
    float z = linear(textureLod(uDepth, uv, 0.0).r);
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * uTanHalf * uAspect * z, ndc.y * uTanHalf * z, -z);
}
float fast_acos(float x) {
    float a = abs(x), r = (-0.156583 * a + HALF_PI) * sqrt(1.0 - a);
    return x >= 0.0 ? r : PI - r;
}
void main() {
    float d = texture(uDepth, vUV).r;
    if (uReversedZ > 0.5 ? d <= 0.0 : d >= 1.0) { FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec2 tx = 1.0 / uFull;
    vec3 p = view_at(vUV);
    vec3 px1 = view_at(vUV + vec2(tx.x, 0.0)) - p, px0 = p - view_at(vUV - vec2(tx.x, 0.0));
    vec3 py1 = view_at(vUV + vec2(0.0, tx.y)) - p, py0 = p - view_at(vUV - vec2(0.0, tx.y));
    vec3 n = normalize(cross(abs(px1.z) < abs(px0.z) ? px1 : px0, abs(py1.z) < abs(py0.z) ? py1 : py0));
    vec3 v = normalize(-p);
    if (dot(n, v) < 0.0) n = -n;
    float z = -p.z;
    // The four probes round this pixel, as near as they are and as alike in distance.
    vec2 g = (gl_FragCoord.xy - 0.5) * 0.5;
    ivec2 g0 = ivec2(floor(g));
    vec2 w = g - vec2(g0);
    ivec2 q[4];
    float qw[4];
    float wsum = 0.0;
    for (int k = 0; k < 4; ++k) {
        ivec2 o = ivec2(k & 1, k >> 1);
        q[k] = clamp(g0 + o, ivec2(0), uGrid - 1);
        float bw = (o.x == 1 ? w.x : 1.0 - w.x) * (o.y == 1 ? w.y : 1.0 - w.y);
        float zq = texelFetch(uPrep, q[k], 0).a;
        qw[k] = bw * max(exp(-abs(zq - z) / (0.04 * z + 0.01)), 1e-4);
        wsum += qw[k];
    }
    int ways = uTiles.x * uTiles.y, slices = ways / 2;
    float open = 0.0;
    vec3 given = vec3(0.0);
    for (int s = 0; s < slices; ++s) {
        // Its two sides: way s along the screen, way s + slices against it.
        vec4 side[2];
        for (int e = 0; e < 2; ++e) {
            int way = s + e * slices;
            ivec2 tile = ivec2(way % uTiles.x, way / uTiles.x);
            vec4 m = vec4(0.0);
            for (int k = 0; k < 4; ++k) m += texelFetch(uLevel, q[k] + tile * uGrid, 0) * qw[k];
            side[e] = m / max(wsum, 1e-6);
        }
        float a = (float(s) + 0.5) / float(ways) * 2.0 * PI;
        vec3 dir = vec3(cos(a), sin(a), 0.0);
        vec3 ortho = normalize(dir - dot(dir, v) * v);
        vec3 axis = normalize(cross(ortho, v));
        vec3 pn = n - axis * dot(n, axis);
        float pn_len = length(pn);
        float cos_n = clamp(dot(pn, v) / max(pn_len, 1e-4), 0.0, 1.0);
        float nn = sign(dot(ortho, pn)) * fast_acos(cos_n);
        float sn = sin(nn);
        // (Each side's horizon as an angle from the eye's ray, held to the surface's half.)
        float h1 = fast_acos(clamp(side[0].a, -1.0, 1.0));
        float h0 = -fast_acos(clamp(side[1].a, -1.0, 1.0));
        h0 = nn + clamp(h0 - nn, -HALF_PI, HALF_PI);
        h1 = nn + clamp(h1 - nn, -HALF_PI, HALF_PI);
        float f0 = nn - HALF_PI, f1 = nn + HALF_PI;
        float arc0 = (cos_n + 2.0 * h0 * sn - cos(2.0 * h0 - nn)) * 0.25;
        float arc1 = (cos_n + 2.0 * h1 * sn - cos(2.0 * h1 - nn)) * 0.25;
        float all0 = (cos_n + 2.0 * f0 * sn - cos(2.0 * f0 - nn)) * 0.25;
        float all1 = (cos_n + 2.0 * f1 * sn - cos(2.0 * f1 - nn)) * 0.25;
        open += pn_len * (arc0 + arc1);
        given += pn_len * (max(all0 - arc0, 0.0) * side[1].rgb + max(all1 - arc1, 0.0) * side[0].rgb);
    }
    open = clamp(open / float(slices), 0.0, 1.0);
    given /= float(slices);
    FragColor = vec4(given, open);
})";
    return s.c_str();
}

// The gather smoothed, kept to one surface (as the occlusion's blur is).
const char* blur_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSource;
uniform sampler2D uDepth;
uniform vec2  uTexel;
uniform float uNear, uFar, uReversedZ;
float linear(float d) {
    if (uReversedZ > 0.5) return uNear * uFar / (uNear + d * (uFar - uNear));
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
void main() {
    float zc = linear(texture(uDepth, vUV).r);
    vec4 sum = vec4(0.0);
    float weight = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = vUV + vec2(x, y) * uTexel;
            float z = linear(texture(uDepth, uv).r);
            float w = exp(-0.35 * float(x * x + y * y)) * exp(-abs(z - zc) / (zc * 0.02));
            sum += texture(uSource, uv) * w;
            weight += w;
        }
    FragColor = sum / max(weight, 1e-4);
})";
}

// Laid over the scene: the share of each pixel that is light from all round
// (the scene's alpha) as open as the cascades found it, and the light of
// what is near given to its diffuse colour.
const char* apply_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uGather;
uniform sampler2D uDepth;
uniform sampler2D uDiffuse;
uniform float uStrength, uBounce, uShow;
uniform float uNear, uFar, uReversedZ;
uniform vec2  uTexel;
float linear(float d) {
    if (uReversedZ > 0.5) return uNear * uFar / (uNear + d * (uFar - uNear));
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
void main() {
    vec4 scene = texture(uScene, vUV);
    vec4 g = texture(uGather, vUV);
    // (On a silhouette, as the occlusion does: the most open round it.)
    float zc = linear(texture(uDepth, vUV).r), edge = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0) * uTexel;
        edge = max(edge, abs(linear(texture(uDepth, vUV + o).r) - zc) / zc);
    }
    if (edge > 0.02)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) g.a = max(g.a, texture(uGather, vUV + vec2(x, y) * uTexel).a);
    vec4 dif = texture(uDiffuse, vUV);
    vec3 c = scene.rgb * mix(1.0, g.a, uStrength * scene.a);
    if (dif.a > 0.5) c += dif.rgb * g.rgb * uBounce;
    FragColor = vec4(c, 1.0);
    // (`gi.cascades.show`: 1 how open, 2 the light given back, 3 the diffuse colour.)
    if (uShow > 0.5) FragColor = vec4(uShow < 1.5 ? vec3(g.a) : uShow < 2.5 ? g.rgb : dif.rgb, 1.0);
})";
}

}  // namespace

void GLWorldView::run_cascades(float strength) {
    if (!rc_prep_prog_) {
        rc_prep_prog_ = std::make_unique<gl::Program>(gl::post_vs(), prep_fs(), "cascades prep");
        rc_trace_prog_ = std::make_unique<gl::Program>(gl::post_vs(), trace_fs(), "cascades");
        rc_gather_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gather_fs(), "cascades gather");
        rc_blur_prog_ = std::make_unique<gl::Program>(gl::post_vs(), blur_fs(), "cascades blur");
        rc_apply_prog_ = std::make_unique<gl::Program>(gl::post_vs(), apply_fs(), "cascades apply");
    }
    const int w = lit_.width(), h = lit_.height();
    const int hw = std::max(1, w / 2), hh = std::max(1, h / 2);
    // The pictures, made as the view's size is.
    int grid_w[kCascades], grid_h[kCascades];
    for (int n = 0; n < kCascades; ++n) {
        const int sp = 1 << n;
        grid_w[n] = (hw + sp - 1) / sp, grid_h[n] = (hh + sp - 1) / sp;
        const int lw = grid_w[n] * kTilesX[n], lh = grid_h[n] * kTilesY[n];
        if (!rc_level_[n].valid() || rc_level_[n].width() != lw || rc_level_[n].height() != lh)
            rc_level_[n].create(lw, lh, gl::GL_RGBA16F, 0, false);
    }
    if (!rc_prep_.valid() || rc_prep_.width() != hw || rc_prep_.height() != hh) rc_prep_.create(hw, hh, gl::GL_RGBA32F, 0, false);
    if (!rc_diffuse_.valid() || rc_diffuse_.width() != w || rc_diffuse_.height() != h) rc_diffuse_.create(w, h, gl::GL_RGBA8, 0, false);

    gl::glDisable(gl::GL_DEPTH_TEST);
    scene_ms().blit_depth_to(depth_);
    if (diffuse_wanted_) scene_ms().fourth_to(rc_diffuse_);
    const float tan_half = std::tan(view_.fov * 0.5f);
    const float reversed = gl::reversed_depth() ? 1.0f : 0.0f;
    const float reach = static_cast<float>(setting(post_, passes::composite, "gi.cascades.reach", 1.0));

    // Half the pixels: each one's light and distance.
    rc_prep_.bind();
    rc_prep_prog_->use();
    rc_prep_prog_->set("uDepth", 0);
    rc_prep_prog_->set("uScene", 1);
    rc_prep_prog_->set("uNear", view_.znear);
    rc_prep_prog_->set("uFar", view_.zfar);
    rc_prep_prog_->set("uReversedZ", reversed);
    depth_.bind_depth(0);
    resolve_.bind_color(1);
    screen_.draw();

    // The cascades, the farthest first: each merged with the one above.
    rc_trace_prog_->use();
    rc_trace_prog_->set("uPrep", 0);
    rc_trace_prog_->set("uUpper", 1);
    rc_trace_prog_->set("uFull", static_cast<float>(w), static_cast<float>(h));
    rc_trace_prog_->set("uTanHalf", tan_half);
    rc_trace_prog_->set("uAspect", view_.aspect);
    rc_trace_prog_->set("uReach", reach);
    rc_trace_prog_->set("uJitter", static_cast<float>(setting(post_, passes::composite, "gi.cascades.jitter", 0.0)));
    rc_trace_prog_->set("uHalfV", static_cast<float>(hw), static_cast<float>(hh));
    rc_prep_.bind_color(0);
    for (int n = kCascades - 1; n >= 0; --n) {
        rc_level_[n].bind();
        const bool upper = n + 1 < kCascades;
        rc_trace_prog_->set("uHasUpper", upper ? 1.0f : 0.0f);
        rc_trace_prog_->set("uGridV", static_cast<float>(grid_w[n]), static_cast<float>(grid_h[n]));
        rc_trace_prog_->set("uTilesV", static_cast<float>(kTilesX[n]), static_cast<float>(kTilesY[n]));
        if (upper) {
            rc_trace_prog_->set("uUpperGridV", static_cast<float>(grid_w[n + 1]), static_cast<float>(grid_h[n + 1]));
            rc_trace_prog_->set("uUpperTilesV", static_cast<float>(kTilesX[n + 1]), static_cast<float>(kTilesY[n + 1]));
            rc_level_[n + 1].bind_color(1);
        }
        rc_trace_prog_->set("uSpacing", static_cast<float>(1 << n));
        const float to = kFirst * std::pow(4.0f, static_cast<float>(n));
        rc_trace_prog_->set("uFrom", n == 0 ? 0.0f : to * 0.25f);
        rc_trace_prog_->set("uTo", to);
        rc_trace_prog_->set("uSteps", kSteps[n]);
        screen_.draw();
    }

    // Each pixel's: how open, and what is near gives it.
    ao_a_.bind();
    rc_gather_prog_->use();
    rc_gather_prog_->set("uDepth", 0);
    rc_gather_prog_->set("uPrep", 1);
    rc_gather_prog_->set("uLevel", 2);
    rc_gather_prog_->set("uGridV", static_cast<float>(grid_w[0]), static_cast<float>(grid_h[0]));
    rc_gather_prog_->set("uTilesV", static_cast<float>(kTilesX[0]), static_cast<float>(kTilesY[0]));
    rc_gather_prog_->set("uHalfV", static_cast<float>(hw), static_cast<float>(hh));
    rc_gather_prog_->set("uFull", static_cast<float>(w), static_cast<float>(h));
    rc_gather_prog_->set("uTanHalf", tan_half);
    rc_gather_prog_->set("uAspect", view_.aspect);
    rc_gather_prog_->set("uNear", view_.znear);
    rc_gather_prog_->set("uFar", view_.zfar);
    rc_gather_prog_->set("uReversedZ", reversed);
    depth_.bind_depth(0);
    rc_prep_.bind_color(1);
    rc_level_[0].bind_color(2);
    screen_.draw();

    ao_b_.bind();
    rc_blur_prog_->use();
    rc_blur_prog_->set("uSource", 0);
    rc_blur_prog_->set("uDepth", 1);
    rc_blur_prog_->set("uTexel", 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
    rc_blur_prog_->set("uNear", view_.znear);
    rc_blur_prog_->set("uFar", view_.zfar);
    rc_blur_prog_->set("uReversedZ", reversed);
    ao_a_.bind_color(0);
    depth_.bind_depth(1);
    screen_.draw();

    lit_.bind();
    rc_apply_prog_->use();
    rc_apply_prog_->set("uScene", 0);
    rc_apply_prog_->set("uGather", 1);
    rc_apply_prog_->set("uDepth", 2);
    rc_apply_prog_->set("uDiffuse", 3);
    rc_apply_prog_->set("uStrength", strength);
    rc_apply_prog_->set("uBounce", diffuse_wanted_ ? static_cast<float>(setting(post_, passes::composite, "gi.cascades", 1.0)) : 0.0f);
    rc_apply_prog_->set("uShow", static_cast<float>(setting(post_, passes::composite, "gi.cascades.show", 0.0)));
    rc_apply_prog_->set("uNear", view_.znear);
    rc_apply_prog_->set("uFar", view_.zfar);
    rc_apply_prog_->set("uReversedZ", reversed);
    rc_apply_prog_->set("uTexel", 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
    resolve_.bind_color(0);
    ao_b_.bind_color(1);
    depth_.bind_depth(2);
    rc_diffuse_.bind_color(3);
    screen_.draw();
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

}  // namespace sg::render
