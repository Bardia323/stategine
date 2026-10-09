// Stategine - the shader sources for the forward+post pipeline.
//
// Scene: up to eight lights - spots, and suns - the nearest two with PCF
// shadows, a hemispheric ambient term, a Cook-Torrance-ish specular lobe,
// procedural surface detail, a sky for open worlds, distance fog and the
// lamps' light the air scatters (air_fs), written to an HDR target. Post:
// bright pass, separable blur and a wide mip-chain glow, then the film (the
// look's grade, a tone curve, grain) with bloom - spread further in thick
// air - vignette and a light FXAA.
#pragma once

#include <string>

namespace sg::gl {

// --- scene --------------------------------------------------------------------
inline const char* scene_vs() {
    return R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
// Drawn as one of many at once: where this one is in its room (its frame is
// uFrame), and what it is made of - albedo and roughness; surface, emissive,
// highlight and mirror. Otherwise uModel and the material uniforms say.
layout(location=3) in mat4 iLocal;
layout(location=7) in vec4 iMat0;
layout(location=8) in vec4 iMat1;
// Its depth layer (x): steps nearer the eye it is drawn, past its size's (`depth_layer`).
layout(location=9) in vec4 iMat2;
uniform float uDepthLayer;
uniform int uInstanced;
uniform mat4 uFrame;
flat out vec4 vMat0;
flat out vec4 vMat1;
flat out float vInstanced;

uniform mat4 uModel;
// The same placement *without* the room's own, so surface detail is a property
// of the surface rather than of where the room currently sits. Which room the
// viewer stands in decides the world frame; it must not decide where the floor
// tiles fall.
uniform mat4 uTexModel;
uniform mat4 uViewProj;

// The half-spaces this room owns: one per doorway, the room's own side of the
// plane it is glued along. Two rooms both build a wall on that plane; each
// keeps only its half, so no surface is drawn twice and none fight.
const int MAX_BOUNDS = 8;
uniform vec4 uClip[MAX_BOUNDS];
uniform int  uClipCount;
out float gl_ClipDistance[MAX_BOUNDS];

out vec3 vWorld;
out vec3 vRoom;
out vec3 vNormal;
out vec2 vUV;
out vec3 vLocal;
out vec3 vObject;
out vec3 vObjNormal;
out vec3 vRoomNormal;
out vec3 vTexScale;

void main() {
    mat4 model = uInstanced == 1 ? uFrame * iLocal : uModel;
    mat4 texModel = uInstanced == 1 ? iLocal : uTexModel;
    vMat0 = iMat0;
    vMat1 = iMat1;
    vInstanced = float(uInstanced);
    vec4 world = model * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vRoom = (texModel * vec4(aPos, 1.0)).xyz;
    vLocal = aPos;
    // The mesh's own frame at its own size: materials that belong to a thing
    // (grain, brushing, weave) ride along with it however it is moved.
    vec3 scale = vec3(length(model[0].xyz), length(model[1].xyz), length(model[2].xyz));
    vObject = aPos * scale;
    vObjNormal = aNormal;
    // In the room's frame, as a surface's normal goes when it is scaled.
    vec3 texScale = vec3(length(texModel[0].xyz), length(texModel[1].xyz), length(texModel[2].xyz));
    vTexScale = texScale;
    vRoomNormal = mat3(texModel) * (aNormal / max(texScale * texScale, vec3(1e-8)));
    vNormal = normalize(mat3(model) * aNormal);
    vUV = aUV;
    for (int i = 0; i < MAX_BOUNDS; ++i)
        gl_ClipDistance[i] = i < uClipCount ? dot(uClip[i], vec4(world.xyz, 1.0)) : 1.0;
    gl_Position = uViewProj * world;
    // No two surfaces fight. Where two coincide (a lining in a wall's face,
    // a plate on a floor) their depths differ only by rounding, a step or
    // two of the depth buffer, whatever the distance - so each thing is drawn
    // a few of its steps nearer the eye the smaller it is (three steps for
    // each halving of its size from 16 m down; a hair more for where it stands, to part
    // two of one size). The smaller - the detail laid on the larger - is the
    // one seen, the same every frame. A step is 2^-24 of the depth range: a
    // nudge of a fraction of a millimetre at arm's length, a centimetre or
    // two thirty metres off.
    float size = max(pow(max(scale.x * scale.y * scale.z, 1e-12), 1.0 / 3.0), 1e-4);
    float rank = clamp(3.0 * (4.0 - log2(size)), 0.0, 60.0) + fract(sin(dot(model[3].xyz, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
    // A thing may say more (`depth_layer`): the parts of one model, made to
    // fit together, each a layer of its own, so none ties with another.
    rank += uInstanced == 1 ? iMat2.x : uDepthLayer;
    gl_Position.z -= rank * (2.0 / 16777216.0) * gl_Position.w;
})";
}

// --- the glass of a screen -------------------------------------------------------
// A panel with `crt` set is a tube: its texture is the picture, and the glass
// is drawn per pixel. These numbers are shared by the shader and by anything
// that has to find which pixel of the picture is under a point of the glass -
// a mouse pointer on the screen - so the two cannot disagree.
struct CrtGlass {
    static constexpr float half_w = 0.975f;  // the glass, as a fraction of the panel
    static constexpr float half_h = 0.965f;
    static constexpr float corner = 0.09f;   // corner radius, in glass units
    static constexpr float bulge = 0.045f;   // how far the picture bows out
    static constexpr float fit = 0.975f;     // the picture inside the glass, with a margin
};

// A tube seen flat (`flat`, 0..1, the panel's `flat` parameter): as someone
// with their face up to it sees it - the picture straight, filling the glass
// edge to edge, the corners all but square. The glass as it is at `flat`.
struct CrtShape {
    double half_w, half_h, corner, bulge, fit;
};
CrtShape crt_shape(double flat);

// Panel coordinates (0..1, v down) to picture coordinates. False where the
// glass shows the dark margin rather than the picture.
bool crt_picture(double u, double v, double& pu, double& pv, double flat = 0.0);

std::string crt_glsl_constants();

// The lights a pass is lit by, as every pass that lights reads them: GLSL
// declaring the light and shadow uniforms the renderer sets, and giving
// `light_reach(i, p, l)` (how much of light i reaches p, and which way) and
// `through_gate` (how much of it a doorway lets through).
const std::string& lights_glsl();

// A view's air, lit: what a world's lamps light of the air in each slice of
// the view out from the eye (cells of the view, the slices widening as they
// go), scattered back towards the eye and shadowed by the lamps' own maps -
// every cell of every slice at once, laid side by side eight slices to a row.
// Built by the renderer when the look's scene pass says its air scatters
// (`scatter`).
const char* air_fs();

// And the same slices added up from the eye out, eight a pass, each pass on
// its own: the light gathered up to each slice's distance.
const char* air_sum_fs();

// Temporal antialiasing: this frame's picture (uCurrent, its view moved by
// uJitter, in clip units) and what was made of the frames before
// (uHistory), found where each pixel was last frame by its depth (uDepth)
// and how the view moved (uReproject: this frame's clip space, unmoved, to
// the last frame's), taken only within what this frame's neighbourhood of
// the pixel holds - so what moved or came into sight does not trail - and
// blended, a tenth of this frame to nine of what was. uFresh 1: no history.
const char* taa_fs();
// Relighting a room's light from all round (GLWorldView::relight_room): what
// each box sees lit by one light, and that taken to nine harmonics a box.
const char* relight_fs();
const char* relight_sh_fs();

// The same air as a scene shader reads it: GLSL giving `air_light(d)`, the
// light gathered up to `d` metres from the eye at this pixel (after
// uViewport is declared).
const char* air_glsl();

// What the eye adjusts to: a small picture of the scene's light, each pixel
// the log (base 2) of how bright the scene is about there, weighted towards
// the middle of the view, and that weight - so its mipmaps, taken down to one
// pixel, are the weighted mean of the log (the eye's own auto-exposure).
const char* exposure_meter_fs();

const char* scene_fs();

// Depth-only pass for the shadow map.
inline const char* depth_vs() {
    return R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightViewProj;
// Drawn as one of many at once: where this one is in its room, whose frame is uFrame.
layout(location=3) in mat4 iLocal;
uniform int uInstanced;
uniform mat4 uFrame;
// For light from beyond a doorway, only what is on this side of the opening
// stands in its way: a half-space, (normal, offset); all of space when 0.
uniform vec4 uCasterSide;
out float gl_ClipDistance[1];
void main() {
    vec4 world = (uInstanced == 1 ? uFrame * iLocal : uModel) * vec4(aPos, 1.0);
    gl_ClipDistance[0] = dot(uCasterSide.xyz, world.xyz) + uCasterSide.w;
    gl_Position = uLightViewProj * world;
})";
}

inline const char* depth_fs() {
    return R"(#version 330 core
void main() {})";
}

// --- post ---------------------------------------------------------------------
// One oversized triangle, UVs derived from gl_VertexID: no vertex buffer.
inline const char* post_vs() {
    return R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";
}

inline const char* bright_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
void main() {
    vec3 c = texture(uScene, vUV).rgb;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float k = max(luma - uThreshold, 0.0) / max(luma, 1e-4);
    FragColor = vec4(c * k, 1.0);
})";
}

inline const char* blur_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSource;
uniform vec2 uDirection;   // texel-sized step, horizontal or vertical
void main() {
    float w[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);
    vec3 sum = texture(uSource, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        sum += texture(uSource, vUV + uDirection * float(i)).rgb * w[i];
        sum += texture(uSource, vUV - uDirection * float(i)).rgb * w[i];
    }
    FragColor = vec4(sum, 1.0);
})";
}

// The wide glow: the bright pass taken down a chain of halvings (Jimenez's
// 13 taps, the first level a Karis average so one hot pixel does not flash)
// and back up with a tent, each level adding its own. Light spreads in a lens
// and an eye as far as it is bright, not a fixed few pixels.
inline const char* bloom_down_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSource;
uniform vec2 uTexel;       // one pixel of the source
uniform float uFirst;
float w(vec3 c) { return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }
void main() {
    vec2 t = uTexel;
    vec3 a = texture(uSource, vUV + t * vec2(-2, 2)).rgb, b = texture(uSource, vUV + t * vec2(0, 2)).rgb;
    vec3 c = texture(uSource, vUV + t * vec2(2, 2)).rgb, d = texture(uSource, vUV + t * vec2(-2, 0)).rgb;
    vec3 e = texture(uSource, vUV).rgb, f = texture(uSource, vUV + t * vec2(2, 0)).rgb;
    vec3 g = texture(uSource, vUV + t * vec2(-2, -2)).rgb, h = texture(uSource, vUV + t * vec2(0, -2)).rgb;
    vec3 i = texture(uSource, vUV + t * vec2(2, -2)).rgb, j = texture(uSource, vUV + t * vec2(-1, 1)).rgb;
    vec3 k = texture(uSource, vUV + t * vec2(1, 1)).rgb, l = texture(uSource, vUV + t * vec2(-1, -1)).rgb;
    vec3 m = texture(uSource, vUV + t * vec2(1, -1)).rgb;
    vec3 g0 = (j + k + l + m) * 0.25, g1 = (a + b + d + e) * 0.25, g2 = (b + c + e + f) * 0.25;
    vec3 g3 = (d + e + g + h) * 0.25, g4 = (e + f + h + i) * 0.25;
    if (uFirst > 0.5) {
        float w0 = w(g0) * 0.5, w1 = w(g1) * 0.125, w2 = w(g2) * 0.125, w3 = w(g3) * 0.125, w4 = w(g4) * 0.125;
        FragColor = vec4((g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4), 1.0);
    } else {
        FragColor = vec4(g0 * 0.5 + (g1 + g2 + g3 + g4) * 0.125, 1.0);
    }
})";
}

inline const char* bloom_up_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uSource;
uniform vec2 uTexel;       // one pixel of the source
uniform float uWeight;
void main() {
    vec2 t = uTexel;
    vec3 s = texture(uSource, vUV).rgb * 4.0;
    s += (texture(uSource, vUV + vec2(t.x, 0)).rgb + texture(uSource, vUV - vec2(t.x, 0)).rgb +
          texture(uSource, vUV + vec2(0, t.y)).rgb + texture(uSource, vUV - vec2(0, t.y)).rgb) * 2.0;
    s += texture(uSource, vUV + t).rgb + texture(uSource, vUV - t).rgb +
         texture(uSource, vUV + vec2(t.x, -t.y)).rgb + texture(uSource, vUV + vec2(-t.x, t.y)).rgb;
    FragColor = vec4(s / 16.0 * uWeight, 1.0);
})";
}

// --- ambient occlusion ---------------------------------------------------------
// How much of the sky each pixel can see, from the depth buffer alone: a
// hemisphere of taps round it, each asking whether the depth there stands in
// front. Creases, corners and whatever sits on something darken, as bounced
// light does not reach them. The tap pattern turns with the pixel over a 4x4
// tile, so the blur after it (a 4x4 box, kept to one surface by depth)
// averages the noise away.
inline const char* ao_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uDepth;
uniform vec2  uTexel;     // one pixel of the depth buffer
uniform float uNear, uFar, uTanHalf, uAspect, uRadius;

float linear(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
vec3 view_at(vec2 uv) {
    float z = linear(texture(uDepth, uv).r);
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * uTanHalf * uAspect * z, ndc.y * uTanHalf * z, -z);
}

void main() {
    float d = texture(uDepth, vUV).r;
    if (d >= 1.0) { FragColor = vec4(1.0); return; }
    vec3 p = view_at(vUV);
    // The surface's normal from its neighbours, each side's nearer one, so
    // an edge does not bend it.
    vec3 px1 = view_at(vUV + vec2(uTexel.x, 0.0)) - p, px0 = p - view_at(vUV - vec2(uTexel.x, 0.0));
    vec3 py1 = view_at(vUV + vec2(0.0, uTexel.y)) - p, py0 = p - view_at(vUV - vec2(0.0, uTexel.y));
    vec3 dx = abs(px1.z) < abs(px0.z) ? px1 : px0;
    vec3 dy = abs(py1.z) < abs(py0.z) ? py1 : py0;
    vec3 n = normalize(cross(dx, dy));
    vec3 t = normalize(abs(n.y) < 0.9 ? cross(n, vec3(0, 1, 0)) : cross(n, vec3(1, 0, 0)));
    vec3 b = cross(n, t);

    vec2 cell = mod(floor(gl_FragCoord.xy), 4.0);
    float turn = (cell.x * 4.0 + cell.y) / 16.0 * 6.2831853;
    const int N = 16;
    float occluded = 0.0;
    for (int i = 0; i < N; ++i) {
        float fi = float(i);
        float a = fi * 2.3999632 + turn;                 // the golden angle
        float h = fract(fi * 0.618034 + 0.13);           // how far up the hemisphere
        float r = mix(0.12, 1.0, (fi + 0.5) / float(N));
        r *= r;                                           // more taps close in
        vec3 dir = normalize(cos(a) * sqrt(1.0 - h * h) * t + sin(a) * sqrt(1.0 - h * h) * b + h * n);
        vec3 s = p + dir * (r * uRadius);
        vec2 uv = vec2(s.x / (-s.z * uTanHalf * uAspect), s.y / (-s.z * uTanHalf)) * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) continue;
        float there = linear(texture(uDepth, uv).r);
        float in_front = step(there, -s.z - 0.02);
        float near = smoothstep(0.0, 1.0, uRadius / max(abs(-p.z - there), 1e-4));
        occluded += in_front * near;
    }
    float ao = 1.0 - occluded / float(N);
    FragColor = vec4(vec3(ao), 1.0);
})";
}

inline const char* ao_blur_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uAO;
uniform sampler2D uDepth;
uniform vec2  uTexel;     // one pixel of the AO target
uniform float uNear, uFar;
float linear(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
void main() {
    float zc = linear(texture(uDepth, vUV).r);
    float sum = 0.0, weight = 0.0;
    // Exactly the 4x4 tile the taps turn over, so their pattern cancels;
    // a neighbour on another surface (a depth break) is left out.
    for (int y = -2; y < 2; ++y)
        for (int x = -2; x < 2; ++x) {
            vec2 uv = vUV + vec2(x, y) * uTexel;
            float z = linear(texture(uDepth, uv).r);
            float w = abs(z - zc) < zc * 0.04 ? 1.0 : 0.0;
            sum += texture(uAO, uv).r * w;
            weight += w;
        }
    FragColor = vec4(vec3(sum / max(weight, 1e-4)), 1.0);
})";
}

// The scene with its occlusion laid on: `uStrength` of it, on the share of
// each pixel that is light from all round (the scene's alpha) - a lamp, a lit
// screen or a patch of sun is not darkened by what is round it.
inline const char* ao_apply_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uAO;
uniform sampler2D uDepth;
uniform float uStrength;
uniform float uNear, uFar;
uniform vec2  uTexel;
float linear(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
void main() {
    vec4 scene = texture(uScene, vUV);
    vec3 c = scene.rgb;
    float ao = texture(uAO, vUV).r;
    // On a silhouette the picture is a blend of both sides (it was
    // multisampled) but the occlusion belongs to one: take the lightest
    // round it, so no dark, stepped fringe is laid along the edge.
    float zc = linear(texture(uDepth, vUV).r), edge = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0) * uTexel;
        edge = max(edge, abs(linear(texture(uDepth, vUV + o).r) - zc) / zc);
    }
    if (edge > 0.02) {
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) ao = max(ao, texture(uAO, vUV + vec2(x, y) * uTexel).r);
    }
    float k = uStrength * scene.a;
    FragColor = vec4(c * mix(1.0, pow(ao, 1.6), k), 1.0);
})";
}

// The film every composite develops its picture on: GLSL to paste into a
// composite shader.
//   vec3 tonemap(vec3 hdr)  scene light to display light: the look's grade
//                           (grade), then its curve - the ACES curve, half per
//                           channel and half on luminance, so bright colours
//                           keep most of their hue on the way to white; or
//                           (`uTonemap` 1) a curve fitted through mid grey with
//                           each channel let go to white on its own as it
//                           nears the top (Lottes's, with crosstalk).
//   vec3 grade(vec3 hdr)    the look's grade, in scene light, before any
//                           curve - so it is the same whatever shows the
//                           picture, and fades with the look as numbers do:
//                           `uGradeExposure` (stops), `uGradeContrast` (about
//                           mid grey, in stops: 0.2 a fifth more),
//                           `uGradeSaturation` (0.3 a third more, -1 grey),
//                           `uGradeShadows` (a colour lifted into the darks)
//                           and `uGradeHighlights` (the lights times 1 + it).
//                           All 0 - unset - it is as it was; what it pushes out
//                           of the widest gamut (BT.2020) is brought back in.
//   vec3 film(vec3 c, float grain, float time)
//                           display light to the screen: encoded, then grain -
//                           soft, a pixel and a half across, strongest in the
//                           mid tones and least in the highlights, moving each
//                           frame - and a last dither, so nothing bands.
inline const char* film_glsl() {
    return R"(
uniform float uGradeExposure;
uniform float uGradeContrast;
uniform float uGradeSaturation;
uniform vec3  uGradeShadows;
uniform vec3  uGradeHighlights;
uniform float uTonemap;
vec3 film_aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 grade(vec3 c) {
    const vec3 lum = vec3(0.2126, 0.7152, 0.0722);
    c *= exp2(uGradeExposure);
    if (uGradeContrast != 0.0) {
        float l = max(dot(c, lum), 1e-6);
        c *= 0.18 * exp2(log2(l / 0.18) * (1.0 + uGradeContrast)) / l;
    }
    if (uGradeSaturation != 0.0 || uGradeShadows != vec3(0.0) || uGradeHighlights != vec3(0.0)) {
        float l = dot(c, lum);
        c += uGradeShadows * 0.18 * (1.0 - smoothstep(0.0, 0.36, l));
        c *= 1.0 + uGradeHighlights * smoothstep(0.18, 1.0, l);
        c = mix(vec3(dot(c, lum)), c, 1.0 + uGradeSaturation);
        // Out of BT.2020 and back: what a grade pushed past every colour
        // there is goes to the edge of what there is.
        const mat3 to2020 = mat3(0.6274, 0.0691, 0.0164, 0.3293, 0.9195, 0.0880, 0.0433, 0.0114, 0.8956);
        const mat3 from2020 = mat3(1.6605, -0.1246, -0.0182, -0.5876, 1.1329, -0.1006, -0.0728, -0.0083, 1.1187);
        c = from2020 * max(to2020 * c, vec3(0.0));
    }
    return c;
}
// Lottes's curve: the brightest channel taken through a curve that passes
// mid grey (0.18 to 0.267) and reaches white at 8, the others kept in
// proportion to it - each let go towards white as the curve nears the top
// (crosstalk), so a bright colour goes to white as film does, not to a
// flat bright hue.
vec3 film_crosstalk(vec3 hdr) {
    const vec3 lum = vec3(0.2126, 0.7152, 0.0722);
    const float a = 1.6, d = 0.977, hdr_max = 8.0, mid_in = 0.18, mid_out = 0.267;
    float ad = a * d, ma = pow(mid_in, a), mad = pow(mid_in, ad), ha = pow(hdr_max, a), had = pow(hdr_max, ad);
    float b = (-ma + ha * mid_out) / ((had - mad) * mid_out);
    float c0 = (had * ma - ha * mad * mid_out) / ((had - mad) * mid_out);
    vec3 c = max(hdr, vec3(0.0));
    float lc = dot(c, lum);
    if (lc > 0.0) c *= max(dot(hdr, lum), 0.0) / lc;
    float peak = max(max(c.r, c.g), max(c.b, 1e-8));
    vec3 ratio = c / peak;
    float z = pow(peak, a);
    float t = z / (pow(z, d) * b + c0);
    const vec3 sat = vec3(2.0), crosstalk = vec3(4.0);
    ratio = pow(ratio, vec3(a) / sat);
    ratio = mix(ratio, vec3(1.0), pow(vec3(t), crosstalk));
    ratio = pow(ratio, sat);
    return clamp(ratio * t, 0.0, 1.0);
}
vec3 film_curve(vec3 hdr);
vec3 tonemap(vec3 hdr) {
    vec3 graded = grade(hdr);
    return uTonemap > 0.5 ? film_crosstalk(graded) : film_curve(graded);
}
vec3 film_curve(vec3 hdr) {
    // Per channel, the curve turns a bright orange yellow and a bright blue
    // violet; on luminance alone it keeps the hue but runs a colour past
    // white. Half of each: the tone of the one, most of the hue of the other,
    // and past a knee the brightest channel is bent smoothly towards white.
    const vec3 lum = vec3(0.2126, 0.7152, 0.0722);
    vec3 x = max(hdr, vec3(0.0));
    float l = max(dot(x, lum), 1e-6);
    float lt = film_aces(vec3(l)).x;
    vec3 c = x * (lt / l);
    float m = max(c.r, max(c.g, c.b));
    const float knee = 0.75;
    if (m > knee) {
        float target = knee + (1.0 - knee) * (1.0 - exp(-(m - knee) / (1.0 - knee)));
        c = lt + clamp((target - lt) / max(m - lt, 1e-6), 0.0, 1.0) * (c - lt);
    }
    return clamp(mix(film_aces(x), c, 0.5), 0.0, 1.0);
}
float film_hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
// Grain in clumps a pixel or two across: white noise blurred by the same
// small kernel at every pixel, so it is as strong at one pixel as the next.
// (Value noise on a lattice a pixel and a half across was a third weaker on
// every third row and column: a faint grid over anything bright and flat.)
float film_clump(vec2 p) {
    return film_hash(p) * 0.4 + (film_hash(p + vec2(1.0, 0.0)) + film_hash(p - vec2(1.0, 0.0)) +
                                 film_hash(p + vec2(0.0, 1.0)) + film_hash(p - vec2(0.0, 1.0))) * 0.15;
}
vec3 film(vec3 c, float grain, float time) {
    vec3 e = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
    vec2 px = gl_FragCoord.xy;
    vec2 jump = vec2(film_hash(vec2(floor(time * 24.0), 7.0)), film_hash(vec2(floor(time * 24.0), 13.0))) * 911.0;
    float y = dot(e, vec3(0.2126, 0.7152, 0.0722));
    float g = (film_clump(px + jump) - 0.5) * 1.1;
    float amount = grain * 2.4 * mix(0.45, 1.0, smoothstep(0.0, 0.3, y)) * (1.0 - 0.7 * smoothstep(0.55, 1.0, y));
    vec3 chroma = vec3(film_hash(px + jump + 3.1), film_hash(px + jump + 5.7), film_hash(px + jump + 9.3)) - 0.5;
    e += (g + chroma * 0.15) * amount;
    e += (film_hash(px + jump * 1.37) - film_hash(px + 17.0 + jump) ) / 255.0;
    return e;
}
)";
}

// Light shafts, drawn in the composite on the screen: from each pixel, a
// march towards where the source stands on screen, gathering whatever is
// bright near it. Whatever dark stands between - a window bar, a cliff, a
// door's frame - gathers nothing, so it casts its shadow through the shafts.
// The source is a direction from the eye (uRayDir) with the camera's own
// (uCamFwd, uTanHalf): sg::aim_rays sets them. GLSL to paste into a composite
// shader after uScene and uTexel; it gives `godrays(uv)`.
//   uRays       how strong (0: none)          uRayColor   their colour
//   uRayCut     how bright a pixel must be     uRaySpread  how far from the
//               to shine                                   source it may be
inline const char* godrays_glsl() {
    return R"(
uniform vec3  uRayDir;
uniform vec3  uCamFwd;
uniform float uTanHalf;
uniform float uRays;
uniform vec3  uRayColor;
uniform float uRayCut;
uniform float uRaySpread;

vec3 godrays(vec2 uv) {
    if (uRays <= 0.0) return vec3(0.0);
    vec3 f = normalize(uCamFwd);
    vec3 r = normalize(cross(f, abs(f.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0)));
    vec3 u = cross(r, f);
    vec3 d = normalize(uRayDir);
    float z = dot(d, f);
    if (z <= 0.02) return vec3(0.0);  // behind: nothing to stream from
    float aspect = uTexel.y / uTexel.x;
    vec2 src = vec2(dot(d, r) / (z * uTanHalf * aspect), dot(d, u) / (z * uTanHalf)) * 0.5 + 0.5;

    // Fewer shafts as the source leaves the screen or turns away.
    float seen = smoothstep(0.05, 0.35, z) * (1.0 - smoothstep(0.7, 1.6, length(src - 0.5)));
    if (seen <= 0.0) return vec3(0.0);

    const int N = 56;
    vec2 stp = (src - uv) / float(N) * 0.92;
    // Each pixel's march starts a little further along, so the steps do not
    // band - by an amount moved on each 24th of a second of the composite's
    // time (uTime, which every composite that pastes this declares), as the
    // grain is: an offset that stood still was a layer of noise stuck to the
    // screen over everything bright.
    float start = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + floor(uTime * 24.0) * 0.618034);
    vec2 p = uv + stp * start;
    float w = 1.0;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < N; ++i) {
        p += stp;
        vec3 s = texture(uScene, clamp(p, vec2(0.0), vec2(1.0))).rgb;
        vec2 off = (p - src) * vec2(aspect, 1.0);
        float near_src = exp(-dot(off, off) / (uRaySpread * uRaySpread));
        // Cut on brightness, not per channel, so the shafts keep the
        // colour of what they stream from.
        float ls = dot(s, vec3(0.299, 0.587, 0.114));
        sum += s * (max(ls - uRayCut, 0.0) / max(ls, 1e-4)) * near_src * w;
        w *= 0.965;
    }
    return sum / float(N) * uRays * uRayColor * seen;
}
)";
}

// A cheap FXAA: where the toned picture has an edge, blend towards the
// average of the four neighbours. GLSL to paste into a composite shader after
// uScene, uTexel, uExposure and tonemap (film_glsl); it gives
// `smooth_edges(uv, toned)`.
inline const char* fxaa_glsl() {
    return R"(
uniform float uEdgesSmooth;  // 1: the picture's edges were made smooth over frames (taa): left as they are
vec3 smooth_edges(vec2 uv, vec3 toned) {
    if (uEdgesSmooth > 0.5) return toned;
    const vec3 w = vec3(0.299, 0.587, 0.114);
    vec3 n = texture(uScene, uv + vec2(0.0, uTexel.y)).rgb, s = texture(uScene, uv - vec2(0.0, uTexel.y)).rgb;
    vec3 e = texture(uScene, uv + vec2(uTexel.x, 0.0)).rgb, o = texture(uScene, uv - vec2(uTexel.x, 0.0)).rgb;
    float edge = abs(dot(n + s + e + o, w) * uExposure - 4.0 * dot(toned, w));
    if (edge <= 0.12) return toned;
    return mix(toned, tonemap((n + s + e + o) * 0.25 * uExposure), clamp(edge * 2.0, 0.0, 0.6));
}
)";
}

// Glow that thick air spreads: the farther through the air a pixel is seen,
// the more the bloom spreads over it - light scattered again and again on
// its way, cheaply. GLSL to paste into a composite shader; it gives
// `fog_bloom(uv)`, how much more of the bloom to add there (0 unless the
// look's composite says `uFogBloom`). The renderer binds the view's depth
// (uDepth, unit 2) and says how it was taken (uDepthView: near, far, the
// tangent of half the field of view, the aspect) and the air of the world
// the viewer is in (uAirThick: its density and where it starts, from the
// look's scene pass).
//   uFogBloom     how much more bloom for each unit of optical depth
//   uFogBloomCap  the optical depth past which it spreads no more (0: 3)
const char* fog_bloom_glsl();

const char* composite_fs();

// A composite for pixel art: the picture taken down to `uPixels` rows of
// big square pixels, each coloured once from the scene at its centre; the
// colours pushed - saturated, contrast raised - and cut to `uLevels` steps a
// channel through an ordered dither, as a small palette draws; a hair of
// colour fringe at the edges of the frame. A look wears it as its
// composite shader (LookState::shader), setting the uniforms it reads.
const char* pixel_composite_fs();

// --- after the composite --------------------------------------------------------
// What a look may lay over its finished picture, each off unless the look's
// composite pass says (settings, not uniforms: no look's shader reads them):
//   deband      0..1: where the view has no depth - a world drawn on its
//               clear colour, with no sky - the steps a smooth fall of light
//               shows there are smoothed away. Rings of four taps round the
//               pixel, each half as far again as the last (1, 1.5, 2.25 ...
//               pixels, eight at most), each mixed into the centre as
//               (c + sum) / 5, stopping at the first ring that touches
//               anything drawn: an edge is never smeared into the empty.
//   smear       0..1: how much of the last frame shown stays, blurred, under
//               this one - a picture that trails, as a slow tube does. Kept
//               per second as `smear` is kept per thirtieth of one, so it
//               trails as long at any frame rate.
//   smear.blur  how far apart the blur's taps are, in pixels (0: 1).
// The last frame is presentation history, as a TAA's is: kept by the view on
// the screen only (a feed does not smear), and let go on a cut - a look
// that cuts in or out (`fade` 0), or the eye taken to a world by anything
// but a seam that admits `view`.
//
// `deband(uv, c)`, GLSL to paste after uFrame (the composited picture),
// uDepth (the view's depth) and uTexel.
const char* deband_glsl();
// The pass itself: the composited picture (uFrame) debanded (uDeband) and
// laid over the last frame shown (uHistory, by uSmearKeep, its taps
// uSmearBlur pixels apart).
const char* finish_fs();
// A picture put on the screen exactly as it is (uFrame, pixel for pixel).
const char* present_fs();

}  // namespace sg::gl
