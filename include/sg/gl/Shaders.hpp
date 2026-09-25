// Stategine - the shader sources for the forward+post pipeline.
//
// Scene: up to eight lights - spots, and suns - the nearest two with PCF
// shadows, a hemispheric ambient term, a Cook-Torrance-ish specular lobe,
// procedural surface detail, a sky for open worlds and distance fog, written
// to an HDR target. Post: bright pass, separable blur and a wide mip-chain
// glow, then the film (ACES tone curve, grain) with bloom, vignette and a light FXAA.
#pragma once

#include <string>

namespace sg::gl {

// --- scene --------------------------------------------------------------------
inline const char* scene_vs() {
    return R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uModel;
// The same placement *without* the room's own, so surface detail is a property
// of the surface rather than of where the room currently sits. Which room the
// viewer stands in decides the world frame; it must not decide where the floor
// tiles fall.
uniform mat4 uTexModel;
uniform mat4 uViewProj;
uniform mat4 uLightViewProj0;
uniform mat4 uLightViewProj1;
uniform mat4 uLightViewProj2;
uniform mat4 uLightViewProj3;

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
out vec4 vLightSpace0;
out vec4 vLightSpace1;
out vec4 vLightSpace2;
out vec4 vLightSpace3;
out vec3 vLocal;
out vec3 vObject;
out vec3 vObjNormal;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vRoom = (uTexModel * vec4(aPos, 1.0)).xyz;
    vLocal = aPos;
    // The mesh's own frame at its own size: materials that belong to a thing
    // (grain, brushing, weave) ride along with it however it is moved.
    vec3 scale = vec3(length(uModel[0].xyz), length(uModel[1].xyz), length(uModel[2].xyz));
    vObject = aPos * scale;
    vObjNormal = aNormal;
    vNormal = normalize(mat3(uModel) * aNormal);
    vUV = aUV;
    vLightSpace0 = uLightViewProj0 * world;
    vLightSpace1 = uLightViewProj1 * world;
    vLightSpace2 = uLightViewProj2 * world;
    vLightSpace3 = uLightViewProj3 * world;
    for (int i = 0; i < MAX_BOUNDS; ++i)
        gl_ClipDistance[i] = i < uClipCount ? dot(uClip[i], vec4(world.xyz, 1.0)) : 1.0;
    gl_Position = uViewProj * world;
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
inline CrtShape crt_shape(double flat) {
    const double t = flat < 0 ? 0.0 : flat > 1 ? 1.0 : flat;
    const auto mix = [t](double a, double b) { return a + (b - a) * t; };
    return {mix(CrtGlass::half_w, 1.0), mix(CrtGlass::half_h, 1.0), mix(CrtGlass::corner, 0.012),
            CrtGlass::bulge * (1.0 - t), mix(CrtGlass::fit, 1.0)};
}

// Panel coordinates (0..1, v down) to picture coordinates. False where the
// glass shows the dark margin rather than the picture.
inline bool crt_picture(double u, double v, double& pu, double& pv, double flat = 0.0) {
    const CrtShape c = crt_shape(flat);
    const double gx = (u * 2 - 1) / c.half_w, gy = (v * 2 - 1) / c.half_h;
    const double r2 = gx * gx + gy * gy;
    const double k = (1.0 + c.bulge * r2) / c.fit;
    const double wx = gx * k, wy = gy * k;
    pu = wx * 0.5 + 0.5;
    pv = wy * 0.5 + 0.5;
    return wx >= -1 && wx <= 1 && wy >= -1 && wy <= 1;
}

inline std::string crt_glsl_constants() {
    return "const float kGlassW = " + std::to_string(CrtGlass::half_w) +
           ";\nconst float kGlassH = " + std::to_string(CrtGlass::half_h) +
           ";\nconst float kCorner = " + std::to_string(CrtGlass::corner) +
           ";\nconst float kBulge = " + std::to_string(CrtGlass::bulge) +
           ";\nconst float kFit = " + std::to_string(CrtGlass::fit) + ";\n";
}

inline const char* scene_fs() {
    static const std::string source = std::string(R"(#version 330 core
in vec3 vWorld;
in vec3 vRoom;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpace0;
in vec4 vLightSpace1;
in vec4 vLightSpace2;
in vec4 vLightSpace3;
in vec3 vLocal;
in vec3 vObject;
in vec3 vObjNormal;

out vec4 FragColor;

uniform vec3  uAlbedo;
uniform float uRoughness;     // 0 mirror-ish, 1 chalk
uniform float uEmissive;
uniform float uHighlight;
uniform float uSurface;       // 0 plain, 1 floor tiles, 2 wall plaster, 3 crate, 4 wood,
                              // 5 brushed metal, 6 moulded plastic, 7 fabric, 8 sand and
                              // rock, 9 sky; laid in the room's own metres, for floors,
                              // walls and ceilings of any size: 10 planks, 11 concrete,
                              // 12 checker, 13 brick, 14 carpet, 15 metal plate, 16 grass
uniform float uTexMix;        // 0 albedo only, 1 texture only
uniform float uSkin;          // 1: a box wearing a texture atlas, a cell a face (skin_uv)
uniform float uGlow;          // extra emission for an active interface

// Up to eight lights; the four strongest carry shadow maps - chosen by how
// bright they are, not where the viewer is, so shadows do not come and go as
// you walk. A sun is always first: it lights everything, from one direction,
// without falling off.
const int MAX_LIGHTS = 16;
uniform int   uLightCount;
uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec3  uLightDir[MAX_LIGHTS];   // pointing away from the lamp
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uLightPower[MAX_LIGHTS];
uniform float uCosInner[MAX_LIGHTS];
uniform float uCosOuter[MAX_LIGHTS];
uniform float uLightSun[MAX_LIGHTS];   // 1: parallel light, no cone, no falloff
uniform float uLightFloor[MAX_LIGHTS]; // light left in its full shadow; < 0: uShadowFloor
uniform float uLightIndirect[MAX_LIGHTS]; // 1: stands in for bounced light - diffuse only
uniform float uLightFalloff[MAX_LIGHTS];  // 0: soft falloff, 1: inverse square
uniform vec3  uViewPos;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform vec3  uSky;           // ambient from above
uniform vec3  uGround;        // ambient bounced from the floor
uniform float uAmbient;

// An open world has a sky instead of a ceiling: a gradient, and the sun in it.
uniform vec3  uSkyTop;
uniform vec3  uSkyHorizon;
uniform vec3  uSunDir;        // towards the sun
uniform vec3  uSunColor;

uniform sampler2DShadow uShadowMap0;
uniform sampler2DShadow uShadowMap1;
uniform sampler2DShadow uShadowMap2;
uniform sampler2DShadow uShadowMap3;
uniform sampler2D uTex;
uniform vec2 uShadowTexel;
uniform vec4 uShadowBias;     // per map: a sun's depth range is far longer than a lamp's
uniform float uShadowSoft;    // how wide the filter is, in texels (1: tight)
uniform float uShadowFloor;   // how much light is left in a full shadow - bounce, faked
uniform float uTime;
uniform float uWind;          // sand drifting over the dunes, 0 for none
uniform float uStars;         // how much of the night sky shows, 0 by day
uniform float uClouds;        // how much of the sky is cloud, 0 for none
uniform vec3  uCloudColor;    // a cloud's lit side
uniform vec3  uCloudShade;    // and its shaded underside
uniform float uMirror;        // how much of the real sky a surface reflects
uniform float uTexFlip;       // 1: the picture's rows run bottom up (a rendered one)
uniform float uUntone;        // 1: the picture is already developed (a world's feed): undo the tone curve

// A portal into another room is sampled in screen space: the other side was
// rendered with the matching virtual camera, so the quad becomes a window
// rather than a picture hanging on the wall.
uniform float uScreenUV;
uniform vec2  uViewport;

// A panel can be a screen: its texture is what the tube shows, and the glass
// is drawn here, per pixel - curvature, scanlines, the phosphor stripe, the
// dark bezel and rounded corners, every edge antialiased. 0 is a flat picture.
uniform float uCRT;
uniform vec2  uTexSize;
// Halation: how much of the phosphor's light spreads in the glass round it.
uniform float uHalo;
// The eye's adjustment: how much everything but a screen's picture is
// dimmed (0: not at all - what an unset uniform says).
uniform float uDim;
// How flat the tube is seen (crt_shape): 0 as it is, 1 face up to the glass.
uniform float uFlat;
)") + crt_glsl_constants() + R"(
float hash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i), b = hash(i + vec2(1, 0)), c = hash(i + vec2(0, 1)), d = hash(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 crt_sample(vec2 uv) {
    // Seen flat, the same sums with the glass straightened (crt_shape).
    float corner = mix(kCorner, 0.012, uFlat);
    vec2 g = (uv * 2.0 - 1.0) / mix(vec2(kGlassW, kGlassH), vec2(1.0), uFlat);
    // The glass: a rounded rectangle, its edge spread over one pixel.
    vec2 d = abs(g) - vec2(1.0 - corner);
    float glass_sd = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - corner;
    float gpx = max(fwidth(glass_sd), 1e-5);
    float glass = 1.0 - smoothstep(-gpx, gpx, glass_sd);
    // The picture bows out with the tube and sits wholly inside the glass,
    // with a dark margin, the way a real one does - no corner of it is lost.
    float r2 = dot(g, g);
    vec2 w = g * (1.0 + kBulge * (1.0 - uFlat) * uCRT * r2) / mix(kFit, 1.0, uFlat);
    float pic_sd = max(abs(w.x), abs(w.y)) - 1.0;
    float ppx = max(fwidth(pic_sd), 1e-5);
    float picture = 1.0 - smoothstep(-ppx, ppx, pic_sd);
    vec2 s = clamp(w * 0.5 + 0.5, 0.0, 1.0);
    vec3 col = texture(uTex, s).rgb;
    // Halation: light from the phosphor spreading in the glass - a ring of
    // samples round each point, near and farther out, added as light. Done
    // here, per pixel of the tube, so what is painted onto it stays flat.
    if (uHalo > 0.0) {
        vec2 t = 1.0 / uTexSize;
        vec3 near = vec3(0.0), far = vec3(0.0);
        for (int i = 0; i < 8; ++i) {
            float a = float(i) * 0.7853982 + 0.39;
            vec2 d = vec2(cos(a), sin(a));
            near += texture(uTex, s + d * t * 2.0).rgb;
            far += texture(uTex, s + d * t * 6.0).rgb;
        }
        col += uHalo * (near * 0.045 + far * 0.03);
    }
    // Scanlines, two texels to a line, and an aperture grille in texels -
    // both fading out as the screen is seen smaller than its texture, where
    // they would only alias into moire.
    float lines_per_pixel = fwidth(s.y * uTexSize.y);
    float texels_per_pixel = fwidth(s.x * uTexSize.x);
    float scan_amount = 0.22 * clamp(1.6 - lines_per_pixel, 0.0, 1.0);
    float mask_amount = 0.18 * clamp(1.6 - texels_per_pixel, 0.0, 1.0);
    float line = (1.0 - scan_amount) + scan_amount * cos(s.y * uTexSize.y * 3.14159265);
    float stripe = mod(floor(s.x * uTexSize.x), 3.0);
    vec3 mask = vec3(stripe == 0.0 ? 1.0 : 1.0 - mask_amount, stripe == 1.0 ? 1.0 : 1.0 - mask_amount,
                     stripe == 2.0 ? 1.0 : 1.0 - mask_amount);
    // Seen flat the picture keeps its lines, its grille and its glow, and
    // loses most of the tube's fall-off and all of its grey: black is black.
    float vignette = 1.0 - mix(0.16, 0.05, uFlat) * r2;
    vec3 black = vec3(0.012, 0.013, 0.013) * (1.0 - uFlat);
    vec3 screen = mix(black, col * line * mask * vignette + 0.012 * (1.0 - uFlat), picture);
    // A faint sheen on the curved glass, brightest towards the top.
    screen += vec3(0.018) * (1.0 - uFlat) * smoothstep(0.1, 0.9, -g.y) * (1.0 - 0.6 * r2);
    // The bezel: dark plastic, a lighter lip where it meets the glass - gone
    // to black as the eye leaves it.
    float lip = 1.0 - clamp(glass_sd * 14.0, 0.0, 1.0);
    vec3 bezel = (vec3(0.022, 0.021, 0.02) * (1.0 + lip) + 0.01 * (1.0 - uv.y)) * (1.0 - uDim) * (1.0 - uFlat);
    return mix(bezel, screen, glass);
}

// A skinned box: the texture is an atlas of six cells, three across and two
// down - +x, -x, +z on top, -z, +y, -y below - each face showing its own cell,
// upright on the four sides. So a book's spine, covers and page edges are one
// picture.
vec2 skin_uv() {
    vec3 n = vObjNormal, a = abs(n), p = vLocal;
    float cell, u, v;
    if (a.x >= a.y && a.x >= a.z) {
        cell = n.x > 0.0 ? 0.0 : 1.0;
        u = n.x > 0.0 ? 0.5 - p.z : p.z + 0.5;
        v = p.y + 0.5;
    } else if (a.z >= a.y) {
        cell = n.z > 0.0 ? 2.0 : 3.0;
        u = n.z > 0.0 ? p.x + 0.5 : 0.5 - p.x;
        v = p.y + 0.5;
    } else {
        cell = n.y > 0.0 ? 4.0 : 5.0;
        u = p.x + 0.5;
        v = n.y > 0.0 ? 0.5 - p.z : p.z + 0.5;
    }
    u = clamp(u, 0.002, 0.998);
    v = clamp(v, 0.002, 0.998);
    float col = mod(cell, 3.0), row = floor(cell / 3.0);
    return vec2((col + u) / 3.0, (row + 1.0 - v) / 2.0);
}

vec3 sky(vec3 dir) {
    float t = clamp(dir.y, -1.0, 1.0);
    vec3 c = mix(uSkyHorizon, uSkyTop, pow(max(t, 0.0), 0.45));
    c = mix(c, uSkyHorizon * 0.8, clamp(-t * 5.0, 0.0, 1.0));  // below the horizon, haze
    float s = max(dot(dir, normalize(uSunDir + vec3(0.0, 1e-4, 0.0))), 0.0);
    c += uSunColor * (pow(s, 1200.0) * 40.0 + pow(s, 24.0) * 0.35 + pow(s, 4.0) * 0.1);
    // Clouds: a layer overhead, seen in perspective, drifting; lit on the
    // side towards the sun and gold at the rim, their undersides shaded,
    // thinning into the haze at the horizon.
    if (uClouds > 0.0 && dir.y > 0.0) {
        vec2 p = dir.xz / (dir.y + 0.06) * 0.9 + vec2(uTime * 0.006, uTime * 0.002);
        float d = noise(p * 1.1) * 0.5 + noise(p * 2.3 + 3.7) * 0.25 + noise(p * 4.9 + 9.1) * 0.15 + noise(p * 10.3) * 0.1;
        float cover = smoothstep(1.0 - uClouds, 1.0 - uClouds + 0.28, d);
        float thick = smoothstep(1.0 - uClouds + 0.1, 1.0, d);
        vec3 sd = normalize(uSunDir + vec3(0.0, 1e-4, 0.0));
        float toward = max(dot(dir, sd), 0.0);
        vec3 cloud = mix(uCloudColor, uCloudShade, thick * 0.8);
        cloud += uSunColor * (pow(toward, 6.0) * 0.8 + pow(toward, 40.0) * 1.2) * (1.0 - thick * 0.6);
        c = mix(c, cloud, cover * smoothstep(0.0, 0.12, dir.y));
    }
    // Stars: one in a few thousand cells of the sky's grid lit, twinkling,
    // fading into the haze near the horizon.
    if (uStars > 0.0 && dir.y > 0.0) {
        vec3 q = dir * 420.0;
        vec3 cell = floor(q);
        float h = fract(sin(dot(cell, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
        if (h > 0.996) {
            float d = length(fract(q) - 0.5);
            float twinkle = 0.7 + 0.3 * sin(uTime * (2.0 + h * 40.0) + h * 100.0);
            float bright = 0.4 + (h - 0.996) * 1500.0;
            c += vec3(0.8, 0.85, 1.0) * smoothstep(0.35, 0.0, d) * bright * twinkle * uStars *
                 smoothstep(0.0, 0.25, dir.y);
        }
    }
    return c;
}

// Interleaved gradient noise (Jimenez): a per-pixel number in [0, 1) whose
// neighbours differ as much as they can, so a pattern turned by it dithers
// finely instead of showing its shape.
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

// Percentage-closer filtering over a disc: 16 taps on a Vogel spiral, each the
// hardware's own 2x2, the spiral turned for each pixel so the penumbra is a
// smooth gradient dithered finely, not the steps of a fixed grid. The disc
// reaches `uShadowSoft` times two texels, with a slope-scaled bias. What is
// left in the darkest shadow is `uShadowFloor`.
float shadow_factor(vec4 light_space, sampler2DShadow shadow_map, vec3 n, vec3 l, float bias_scale, float floor_) {
    vec3 proj = light_space.xyz / max(light_space.w, 1e-5);
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;
    float spread = max(uShadowSoft, 0.5);
    float bias = max(0.0016 * (1.0 - dot(n, l)), 0.0006) * bias_scale * (0.6 + 0.4 * spread);
    float turn = ign(gl_FragCoord.xy) * 6.2831853;
    vec2 reach = 2.4 * spread * uShadowTexel;
    float sum = 0.0;
    for (int i = 0; i < 16; ++i) {
        float a = float(i) * 2.3999632 + turn;
        vec2 off = vec2(cos(a), sin(a)) * sqrt((float(i) + 0.5) / 16.0) * reach;
        sum += texture(shadow_map, vec3(proj.xy + off, proj.z - bias));
    }
    // Towards the edge of the map, the shadow fades out rather than stopping
    // on a line: a sun's box round the viewer has an edge a lamp's cone does
    // not, and far off is where it would show.
    float edge = smoothstep(0.82, 0.98, max(abs(proj.x * 2.0 - 1.0), abs(proj.y * 2.0 - 1.0)));
    return mix(mix(floor_, 1.0, sum / 16.0), 1.0, edge);
}

// How much a surface reflects of light arriving from all round, by angle and
// roughness: Karis's fit of the split-sum environment term, (scale, bias) on F0.
vec2 env_brdf(float ndv, float roughness) {
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * ndv)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

// A point of a floor, wall or ceiling, flattened onto the plane it faces,
// in metres: so a pattern keeps its size however big the surface.
vec2 room_plane() {
    vec3 n = abs(normalize(vNormal));
    if (n.y > 0.7) return vRoom.xz;
    return n.x > n.z ? vec2(vRoom.z, vRoom.y) : vec2(vRoom.x, vRoom.y);
}

vec3 room_material(out float rough_mod) {
    vec2 p = room_plane();
    rough_mod = 0.0;
    if (uSurface < 10.5) {
        // Planks: long boards, staggered, each its own shade, with grain.
        vec2 q = p * vec2(0.6, 6.0);
        float row = floor(q.y);
        q.x += hash(vec2(row, 3.0)) * 7.0;
        vec2 cell = fract(q);
        float seam = smoothstep(0.0, 0.04, min(cell.y, 1.0 - cell.y)) * smoothstep(0.0, 0.01, min(cell.x, 1.0 - cell.x));
        float shade = 0.78 + 0.3 * hash(floor(q));
        float grain = noise(vec2(q.x * 30.0, q.y * 3.0));
        rough_mod = -0.1;
        return uAlbedo * shade * (0.85 + 0.2 * grain) * mix(0.5, 1.0, seam);
    }
    if (uSurface < 11.5) {
        // Concrete: mottled, with pour lines.
        float m = noise(p * 1.3) * 0.6 + noise(p * 7.0) * 0.3 + noise(p * 40.0) * 0.1;
        float lines = smoothstep(0.0, 0.02, abs(fract(p.y * 0.8) - 0.5) - 0.48);
        rough_mod = 0.15;
        return uAlbedo * (0.8 + 0.3 * m) * (1.0 - 0.08 * lines);
    }
    if (uSurface < 12.5) {
        // Checker: squares of half a metre, two tones.
        vec2 c = floor(p * 2.0);
        float k = mod(c.x + c.y, 2.0);
        return uAlbedo * mix(0.35, 1.0, k) * (0.96 + 0.06 * noise(p * 9.0));
    }
    if (uSurface < 13.5) {
        // Brick: running bond, mortar between.
        vec2 q = p * vec2(4.0, 12.0);
        q.x += mod(floor(q.y), 2.0) * 0.5;
        vec2 cell = fract(q);
        float mortar = smoothstep(0.0, 0.06, min(cell.y, 1.0 - cell.y)) * smoothstep(0.0, 0.03, min(cell.x, 1.0 - cell.x));
        float shade = 0.75 + 0.35 * hash(floor(q));
        rough_mod = 0.1;
        return mix(vec3(0.62, 0.6, 0.56), uAlbedo * shade * (0.9 + 0.15 * noise(p * 20.0)), mortar);
    }
    if (uSurface < 14.5) {
        // Carpet: a dense short pile.
        float pile = noise(p * 90.0) * 0.5 + noise(p * 23.0) * 0.5;
        rough_mod = 0.3;
        return uAlbedo * (0.82 + 0.3 * pile);
    }
    if (uSurface < 15.5) {
        // Metal plate: panels with seams and a diamond tread.
        vec2 cell = fract(p * 0.8);
        float seam = smoothstep(0.0, 0.015, min(min(cell.x, 1.0 - cell.x), min(cell.y, 1.0 - cell.y)));
        vec2 d = fract(vec2(p.x + p.y, p.x - p.y) * 12.0);
        float tread = smoothstep(0.35, 0.5, 1.0 - abs(d.x - 0.5) * 2.0) * 0.12;
        rough_mod = -0.25;
        return uAlbedo * (0.85 + tread + 0.08 * noise(vec2(p.x * 300.0, p.y * 4.0))) * mix(0.45, 1.0, seam);
    }
    if (uSurface > 16.5 && uSurface < 17.5) {
        // Marble: large slabs, faintly different, with soft veins that
        // wander across them in a darker, warmer tint of the stone.
        vec2 q = p * 0.9;
        vec2 slab = floor(p * 0.8);
        vec2 cell = fract(p * 0.8);
        float seam = smoothstep(0.0, 0.006, min(min(cell.x, 1.0 - cell.x), min(cell.y, 1.0 - cell.y)));
        float warp = noise(q * 1.7 + slab * 3.1) * 3.0 + noise(q * 4.3) * 1.2;
        float vein = 1.0 - smoothstep(0.0, 0.12, abs(sin((q.x + q.y * 0.6) * 2.2 + warp)));
        float fine = 1.0 - smoothstep(0.0, 0.06, abs(sin((q.x * 0.4 - q.y) * 5.0 + warp * 1.7)));
        rough_mod = -0.1;
        vec3 veins = uAlbedo * vec3(0.78, 0.58, 0.70);
        vec3 stone = uAlbedo * (0.96 + 0.05 * hash(slab));
        return mix(mix(stone, veins, vein * 0.55 + fine * 0.25), uAlbedo * 0.8, 1.0 - seam);
    }
    if (uSurface > 17.5 && uSurface < 18.5) {
        // Water: its colour; the waves are in its normal (main).
        rough_mod = -0.2;
        return uAlbedo * (0.9 + 0.1 * noise(p * 0.3 + uTime * 0.05));
    }
    // Grass: clumps of green and dry.
    float c = noise(p * 3.0) * 0.5 + noise(p * 17.0) * 0.3 + noise(p * 80.0) * 0.2;
    rough_mod = 0.3;
    return uAlbedo * mix(vec3(0.75, 0.8, 0.5), vec3(1.1, 1.15, 0.9), c);
}

vec3 surface_albedo(out float rough_mod) {
    rough_mod = 0.0;
    if (uSurface < 0.5) return uAlbedo;
    if (uSurface > 9.5) return room_material(rough_mod);

    if (uSurface < 1.5) {
        // Floor: large tiles with grout and a little grain.
        vec2 t = vRoom.xz * 0.5;
        vec2 cell = fract(t);
        float grout = smoothstep(0.0, 0.035, min(cell.x, cell.y)) *
                      smoothstep(0.0, 0.035, min(1.0 - cell.x, 1.0 - cell.y));
        float shade = mix(0.55, 1.0, hash(floor(t)) * 0.35 + 0.65);
        vec3 tile = uAlbedo * shade * mix(0.45, 1.0, grout);
        rough_mod = mix(-0.25, 0.05, grout);          // grout is rougher than tile
        return tile * (0.94 + 0.12 * noise(vRoom.xz * 8.0));
    }
    if (uSurface < 2.5) {
        // Walls: plaster, with a subtle vertical gradient.
        float grain = 0.92 + 0.16 * noise(vRoom.xz * 6.0 + vRoom.y * 3.0);
        float height = clamp(vRoom.y / 4.0, 0.0, 1.0);
        return uAlbedo * grain * mix(0.82, 1.06, height);
    }
    if (uSurface > 7.5) {
        // Sand, rippled by the wind, giving way to banded rock where the
        // ground is steep. The ripples fade out before they would alias.
        vec3 n = normalize(vNormal);
        vec2 p = vRoom.xz;
        float arg = dot(p, vec2(0.8, 0.6)) * 21.0 + noise(p * 0.35) * 6.0;
        float fade = clamp(1.5 - fwidth(arg) * 0.6, 0.0, 1.0);
        float ripple = 0.5 + 0.5 * sin(arg);
        float grain = noise(p * 37.0);
        vec3 sand = uAlbedo * (0.93 + 0.09 * (ripple - 0.5) * fade + 0.07 * (grain - 0.5) * fade)
                  * (0.9 + 0.2 * noise(p * 0.04));
        float strata = 0.5 + 0.5 * sin(vRoom.y * 2.6 + noise(p * 0.15) * 2.5);
        vec3 rock = vec3(0.50, 0.30, 0.20) * (0.72 + 0.32 * strata) * (0.85 + 0.2 * noise(p * 2.0 + vRoom.y));
        // Wind: pale veils of sand streaming over the ground, downwind.
        vec2 w = p * vec2(0.35, 1.2) + vec2(uTime * 1.7, uTime * 0.3);
        float drift = smoothstep(0.55, 0.9, noise(w) * 0.7 + noise(w * 3.1 + 7.0) * 0.3) * uWind;
        sand = mix(sand, uAlbedo * 1.12 + 0.02, drift * 0.35 * fade);
        float steep = smoothstep(0.3, 0.5, 1.0 - n.y);
        rough_mod = 0.25;
        return mix(sand, rock, steep);
    }
    vec3 a = abs(vLocal);
    float edge = max(max(a.x, a.y), a.z);
    if (uSurface > 3.5 && uSurface < 4.5) {
        // Wood: long grain along the thing's own x and z, rings, a softened edge.
        float along = abs(dot(normalize(vObjNormal), vec3(0.0, 1.0, 0.0))) > 0.5 ? vObject.z : vObject.y;
        float grain = noise(vec2(vObject.x * 2.0 + vObject.z * 2.0, along * 55.0));
        float rings = 0.5 + 0.5 * sin((vObject.x + vObject.z) * 9.0 + grain * 6.0);
        rough_mod = -0.05 * rings;
        return uAlbedo * (0.82 + 0.14 * rings + 0.1 * grain) * mix(1.0, 0.8, smoothstep(0.46, 0.5, edge));
    }
    if (uSurface > 4.5 && uSurface < 5.5) {
        // Brushed metal: fine streaks, and smoother than its roughness says.
        float brush = noise(vec2(vObject.x * 400.0, vObject.y * 6.0 + vObject.z * 6.0));
        rough_mod = -0.2 + 0.1 * brush;
        return uAlbedo * (0.9 + 0.12 * brush);
    }
    if (uSurface > 5.5 && uSurface < 6.5) {
        // Moulded plastic: a faint speckle and rounded, darker edges.
        float speck = noise(vObject.xz * 180.0 + vObject.y * 90.0);
        return uAlbedo * (0.96 + 0.06 * speck) * mix(1.0, 0.78, smoothstep(0.45, 0.5, edge));
    }
    if (uSurface > 6.5) {
        // Fabric: a weave, matte.
        vec2 p = (vObject.xz + vObject.yy) * 260.0;
        float weave = 0.5 + 0.25 * (sin(p.x) + sin(p.y));
        rough_mod = 0.2;
        return uAlbedo * (0.85 + 0.2 * weave) * mix(1.0, 0.85, smoothstep(0.46, 0.5, edge));
    }
    // Crates: planks plus a darker bevel near the edges of the cube.
    float bevel = smoothstep(0.42, 0.5, edge);
    float planks = 0.88 + 0.12 * sin(vLocal.y * 42.0 + hash(vLocal.xz) * 3.0);
    return uAlbedo * planks * mix(1.0, 0.55, bevel);
}

void main() {
    if (uSurface > 8.5 && uSurface < 9.5) {
        // The sky is not lit and not fogged: it is what the fog fades into.
        FragColor = vec4(sky(normalize(vWorld - uViewPos)) * (1.0 - uDim), 0.0);
        return;
    }
    float rough_mod;
    vec3 albedo = surface_albedo(rough_mod);
    if (uTexMix > 0.0) {
        vec2 uv = uScreenUV > 0.5 ? gl_FragCoord.xy / uViewport : uSkin > 0.5 ? skin_uv() : vUV;
        if (uTexFlip > 0.5) uv.y = 1.0 - uv.y;
        vec3 tex = uCRT > 0.0 ? crt_sample(uv) : texture(uTex, uv).rgb;
        if (uUntone > 0.5) {
            // A picture already developed - a world drawn in its own look -
            // is taken back through the tone curve (ACES, solved for its
            // input), so that developed again with the room it comes out as
            // it went in, not twice as flat.
            vec3 y = min(tex, vec3(0.985));
            vec3 a = 2.43 * y - 2.51, b = 0.59 * y - 0.03, c = 0.14 * y;
            tex = max((-b - sqrt(max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a), 0.0);
        }
        albedo = mix(albedo, tex, uTexMix);
    }
    // A portal's view of another room arrives already lit and already fogged,
    // by that room's own light and air: it is shown as it is, not lit or
    // fogged a second time by the room it is seen from.
    if (uScreenUV > 0.5 && uTexMix > 0.99) {
        FragColor = vec4(albedo * (1.0 - uDim), 0.0);
        return;
    }
    float roughness = clamp(uRoughness + rough_mod, 0.05, 1.0);
    // Metal (the brushed surface) reflects in its own colour and scatters
    // less. Only partly: most of what wears it is painted, and a bare metal
    // with only the sky and the floor to reflect would go dark. Everything
    // else reflects 4% head on, white.
    float metal = (uSurface > 4.5 && uSurface < 5.5) ? 0.35 : 0.0;
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 diffuse = albedo * (1.0 - metal);

    vec3 n = normalize(vNormal);
    vec3 v = normalize(uViewPos - vWorld);
    if (uSurface > 17.5 && uSurface < 18.5 && n.y > 0.5) {
        // Waves: the slope of a few long swells and shorter chop, each a
        // travelling sine, crossing; the short ones fade with distance
        // before they would shimmer.
        vec2 p = vRoom.xz;
        float t = uTime;
        float far = length(vWorld - uViewPos);
        vec2 slope = vec2(0.0);
        const vec2 dirs[6] = vec2[](vec2(0.8, 0.6), vec2(-0.45, 0.89), vec2(0.96, -0.28), vec2(0.2, 0.98),
                                    vec2(-0.87, 0.5), vec2(0.6, -0.8));
        for (int i = 0; i < 6; ++i) {
            float k = 0.35 * pow(1.9, float(i));               // wavenumber
            float a = 0.09 / (1.0 + float(i) * 0.8);           // steepness
            float fade = exp(-far * k * 0.004);
            float phase = dot(dirs[i], p) * k - t * sqrt(9.8 * k) + float(i) * 1.7;
            slope += dirs[i] * cos(phase) * a * k * fade / k * 1.6;
        }
        n = normalize(n - vec3(slope.x, 0.0, slope.y));
    }
    float ndv = clamp(dot(n, v), 1e-3, 1.0);
    float a2 = roughness * roughness * roughness * roughness;
    // Specular antialiasing (Kaplanyan & Hill): where the normal turns fast
    // across a pixel - a rounded edge thinner than a pixel - the highlight is
    // widened by as much, or it flickers from pixel to pixel and steps.
    vec3 dndx = dFdx(n), dndy = dFdy(n);
    float variance = 0.25 * (dot(dndx, dndx) + dot(dndy, dndy));
    a2 = clamp(a2 + min(2.0 * variance, 0.25), 0.0, 1.0);

    vec3 direct = vec3(0.0), bounced = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        vec3 l;
        float atten, cone;
        if (uLightSun[i] > 0.5) {
            l = normalize(-uLightDir[i]);
            atten = uLightPower[i];
            cone = 1.0;
        } else {
            vec3 toLight = uLightPos[i] - vWorld;
            float dist = length(toLight);
            l = toLight / max(dist, 1e-4);
            // Spot cone, smooth at the rim.
            float theta = dot(-l, normalize(uLightDir[i]));
            cone = clamp((theta - uCosOuter[i]) / max(uCosInner[i] - uCosOuter[i], 1e-4), 0.0, 1.0);
            cone *= cone;
            // Softly, or as real light does: the inverse square, kept
            // finite at the lamp itself.
            float soft = 1.0 / (1.0 + 0.22 * dist + 0.14 * dist * dist);
            float square = 1.0 / (1.0 + 2.0 * dist * dist);
            atten = uLightPower[i] * mix(soft, square, uLightFalloff[i]);
        }
        vec3 h = normalize(l + v);

        float ndl = max(dot(n, l), 0.0);
        float shadow = 1.0;
        if (ndl > 0.0) {
            float fl = uLightFloor[i] < 0.0 ? uShadowFloor : uLightFloor[i];
            if (i == 0) shadow = shadow_factor(vLightSpace0, uShadowMap0, n, l, uShadowBias.x, fl);
            else if (i == 1) shadow = shadow_factor(vLightSpace1, uShadowMap1, n, l, uShadowBias.y, fl);
            else if (i == 2) shadow = shadow_factor(vLightSpace2, uShadowMap2, n, l, uShadowBias.z, fl);
            else if (i == 3) shadow = shadow_factor(vLightSpace3, uShadowMap3, n, l, uShadowBias.w, fl);
        }

        // Cook-Torrance: GGX for the spread of the highlight, Smith's
        // height-correlated masking (Hammon's fit), and Schlick's Fresnel -
        // a surface reflects more the more edge-on it is seen. What it
        // reflects is not also scattered, so the diffuse loses as much.
        // (The diffuse carries pi folded into the light, so the highlight does.)
        float ndh = max(dot(n, h), 0.0);
        float vdh = clamp(dot(v, h), 0.0, 1.0);
        float denom = ndh * ndh * (a2 - 1.0) + 1.0;
        float d = a2 / (denom * denom + 1e-7);
        float vis = 0.5 / mix(2.0 * ndl * ndv, ndl + ndv, sqrt(a2));
        vec3 f = f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);
        vec3 lobe = diffuse * (1.0 - f) + d * vis * f;

        if (uLightIndirect[i] > 0.5) bounced += diffuse * ndl * uLightColor[i] * atten * cone * shadow;
        else direct += lobe * ndl * uLightColor[i] * atten * cone * shadow;
    }

    // Light from all round: the sky's colour from above, the floor's bounce
    // from below. Scattered by the diffuse, and seen in the mirror direction
    // by the reflection - blurred towards the normal as the surface roughens,
    // and weighted by how much it reflects at this angle.
    vec2 ab = env_brdf(ndv, roughness);
    vec3 reflected = f0 * ab.x + ab.y;
    vec3 r = reflect(-v, n);
    float up = mix(r.y, n.y, roughness * roughness);
    vec3 around = mix(uGround, uSky, n.y * 0.5 + 0.5) * uAmbient;
    vec3 mirrored = mix(uGround, uSky, smoothstep(-0.35, 0.35, up)) * uAmbient;
    // Under an open sky a glossy surface reflects the sky itself - its
    // colours, its clouds, the sun's glint - as rougher surfaces cannot.
    if (uMirror > 0.0) mirrored = mix(mirrored, sky(normalize(vec3(r.x, abs(r.y), r.z))), uMirror * (1.0 - roughness));
    vec3 ambient = diffuse * around * (1.0 - reflected) + mirrored * reflected + bounced;

    vec3 color = ambient + direct + albedo * (uEmissive + uGlow);
    // How much of what is seen here is light from all round - the only part
    // occlusion takes away (ao_apply_fs): a corner in lamplight stays lit.
    const vec3 lum = vec3(0.2126, 0.7152, 0.0722);
    float indirect = clamp(dot(ambient, lum) / max(dot(color, lum), 1e-5), 0.0, 1.0);
    color = mix(color, vec3(1.0, 0.86, 0.45) * (0.3 + 0.7 * length(color)), uHighlight * 0.35);

    // Fog, brighter where it is looked at towards the sun: light scattered
    // on its way through the air.
    vec3 to_frag = vWorld - uViewPos;
    float fog = 1.0 - exp(-uFogDensity * length(to_frag));
    float toward = pow(max(dot(normalize(to_frag), normalize(uSunDir + vec3(0.0, 1e-4, 0.0))), 0.0), 6.0);
    vec3 haze = uFogColor + uSunColor * toward * 0.25;
    color = mix(color, haze, clamp(fog, 0.0, 0.85));

    // The eye adjusted to a screen: the room around it dims, the picture
    // on the screen does not. Alpha: the share of it occlusion may darken.
    FragColor = vec4(color * (uCRT > 0.0 ? 1.0 : (1.0 - uDim)), indirect * (1.0 - clamp(fog, 0.0, 0.85)));
})";
    return source.c_str();
}

// Depth-only pass for the shadow map.
inline const char* depth_vs() {
    return R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightViewProj;
void main() { gl_Position = uLightViewProj * uModel * vec4(aPos, 1.0); })";
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
//   vec3 tonemap(vec3 hdr)  scene light to display light: the ACES curve,
//                           half per channel and half on luminance, so bright
//                           colours keep most of their hue on the way to white.
//   vec3 film(vec3 c, float grain, float time)
//                           display light to the screen: encoded, then grain -
//                           soft, a pixel and a half across, strongest in the
//                           mid tones and least in the highlights, moving each
//                           frame - and a last dither, so nothing bands.
inline const char* film_glsl() {
    return R"(
vec3 film_aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
vec3 tonemap(vec3 hdr) {
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
float film_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(film_hash(i), film_hash(i + vec2(1, 0)), f.x),
               mix(film_hash(i + vec2(0, 1)), film_hash(i + vec2(1, 1)), f.x), f.y);
}
vec3 film(vec3 c, float grain, float time) {
    vec3 e = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
    vec2 px = gl_FragCoord.xy;
    vec2 jump = vec2(film_hash(vec2(floor(time * 24.0), 7.0)), film_hash(vec2(floor(time * 24.0), 13.0))) * 911.0;
    float y = dot(e, vec3(0.2126, 0.7152, 0.0722));
    float g = film_noise((px + jump) / 1.5) * 0.6 + film_hash(px + jump) * 0.4 - 0.5;
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

float ray_hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }

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
    vec2 p = uv + stp * ray_hash(uv * 911.0);  // jittered, so the steps do not band
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
vec3 smooth_edges(vec2 uv, vec3 toned) {
    const vec3 w = vec3(0.299, 0.587, 0.114);
    vec3 n = texture(uScene, uv + vec2(0.0, uTexel.y)).rgb, s = texture(uScene, uv - vec2(0.0, uTexel.y)).rgb;
    vec3 e = texture(uScene, uv + vec2(uTexel.x, 0.0)).rgb, o = texture(uScene, uv - vec2(uTexel.x, 0.0)).rgb;
    float edge = abs(dot(n + s + e + o, w) * uExposure - 4.0 * dot(toned, w));
    if (edge <= 0.12) return toned;
    return mix(toned, tonemap((n + s + e + o) * 0.25 * uExposure), clamp(edge * 2.0, 0.0, 0.6));
}
)";
}

inline const char* composite_fs() {
    static const std::string source = std::string(R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float uBloomStrength;
uniform float uExposure;
uniform vec2  uTexel;
uniform vec3  uTint;
uniform float uSaturation;
uniform float uVignette;
uniform float uGrain;
uniform float uTime;
)") + film_glsl() + godrays_glsl() + fxaa_glsl() + R"(

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec3 scene = texture(uScene, vUV).rgb;
    scene += texture(uBloom, vUV).rgb * uBloomStrength;
    scene += godrays(vUV);
    vec3 color = smooth_edges(vUV, tonemap(scene * uExposure));

    color = mix(vec3(luma(color)), color, uSaturation) * uTint;

    // Vignette, then the film.
    vec2 d = vUV - 0.5;
    color *= 1.0 - dot(d, d) * uVignette;
    FragColor = vec4(film(color, uGrain, uTime), 1.0);
})";
    return source.c_str();
}

}  // namespace sg::gl
