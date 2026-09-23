// Stategine - the shader sources for the forward+post pipeline.
//
// Scene: up to eight lights - spots, and suns - the nearest two with PCF
// shadows, a hemispheric ambient term, a Cook-Torrance-ish specular lobe,
// procedural surface detail, a sky for open worlds and distance fog, written
// to an HDR target. Post: bright pass, separable blur, then ACES
// tonemap with bloom, vignette and a light FXAA.
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

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vRoom = (uTexModel * vec4(aPos, 1.0)).xyz;
    vLocal = aPos;
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

// Panel coordinates (0..1, v down) to picture coordinates. False where the
// glass shows the dark margin rather than the picture.
inline bool crt_picture(double u, double v, double& pu, double& pv) {
    const double gx = (u * 2 - 1) / CrtGlass::half_w, gy = (v * 2 - 1) / CrtGlass::half_h;
    const double r2 = gx * gx + gy * gy;
    const double k = (1.0 + CrtGlass::bulge * r2) / CrtGlass::fit;
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

out vec4 FragColor;

uniform vec3  uAlbedo;
uniform float uRoughness;     // 0 mirror-ish, 1 chalk
uniform float uEmissive;
uniform float uHighlight;
uniform float uSurface;       // 0 plain, 1 floor tiles, 2 wall plaster, 3 crate, 4 wood,
                              // 5 brushed metal, 6 moulded plastic, 7 fabric, 8 sand and
                              // rock, 9 sky
uniform float uTexMix;        // 0 albedo only, 1 texture only
uniform float uGlow;          // extra emission for an active interface

// Up to eight lights; the four strongest carry shadow maps - chosen by how
// bright they are, not where the viewer is, so shadows do not come and go as
// you walk. A sun is always first: it lights everything, from one direction,
// without falling off.
const int MAX_LIGHTS = 8;
uniform int   uLightCount;
uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec3  uLightDir[MAX_LIGHTS];   // pointing away from the lamp
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uLightPower[MAX_LIGHTS];
uniform float uCosInner[MAX_LIGHTS];
uniform float uCosOuter[MAX_LIGHTS];
uniform float uLightSun[MAX_LIGHTS];   // 1: parallel light, no cone, no falloff
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
)") + crt_glsl_constants() + R"(
float hash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i), b = hash(i + vec2(1, 0)), c = hash(i + vec2(0, 1)), d = hash(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec3 crt_sample(vec2 uv) {
    vec2 g = (uv * 2.0 - 1.0) / vec2(kGlassW, kGlassH);
    // The glass: a rounded rectangle, its edge spread over one pixel.
    vec2 d = abs(g) - vec2(1.0 - kCorner);
    float glass_sd = length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - kCorner;
    float gpx = max(fwidth(glass_sd), 1e-5);
    float glass = 1.0 - smoothstep(-gpx, gpx, glass_sd);
    // The picture bows out with the tube and sits wholly inside the glass,
    // with a dark margin, the way a real one does - no corner of it is lost.
    float r2 = dot(g, g);
    vec2 w = g * (1.0 + kBulge * uCRT * r2) / kFit;
    float pic_sd = max(abs(w.x), abs(w.y)) - 1.0;
    float ppx = max(fwidth(pic_sd), 1e-5);
    float picture = 1.0 - smoothstep(-ppx, ppx, pic_sd);
    vec2 s = clamp(w * 0.5 + 0.5, 0.0, 1.0);
    vec3 col = texture(uTex, s).rgb;
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
    float vignette = 1.0 - 0.16 * r2;
    vec3 black = vec3(0.012, 0.013, 0.013);
    vec3 screen = mix(black, col * line * mask * vignette + 0.012, picture);
    // A faint sheen on the curved glass, brightest towards the top.
    screen += vec3(0.018) * smoothstep(0.1, 0.9, -g.y) * (1.0 - 0.6 * r2);
    // The bezel: dark plastic, a lighter lip where it meets the glass.
    float lip = 1.0 - clamp(glass_sd * 14.0, 0.0, 1.0);
    vec3 bezel = vec3(0.022, 0.021, 0.02) * (1.0 + lip) + 0.01 * (1.0 - uv.y);
    return mix(bezel, screen, glass);
}

vec3 sky(vec3 dir) {
    float t = clamp(dir.y, -1.0, 1.0);
    vec3 c = mix(uSkyHorizon, uSkyTop, pow(max(t, 0.0), 0.45));
    c = mix(c, uSkyHorizon * 0.8, clamp(-t * 5.0, 0.0, 1.0));  // below the horizon, haze
    float s = max(dot(dir, normalize(uSunDir + vec3(0.0, 1e-4, 0.0))), 0.0);
    c += uSunColor * (pow(s, 1200.0) * 40.0 + pow(s, 24.0) * 0.35 + pow(s, 4.0) * 0.1);
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

// Percentage-closer filtering: 5x5 taps, each one the hardware's own 2x2,
// spread `uShadowSoft` texels apart, with a slope-scaled bias. What is left
// in the darkest shadow is `uShadowFloor`.
float shadow_factor(vec4 light_space, sampler2DShadow shadow_map, vec3 n, vec3 l, float bias_scale) {
    vec3 proj = light_space.xyz / max(light_space.w, 1e-5);
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;
    float spread = max(uShadowSoft, 0.5);
    float bias = max(0.0016 * (1.0 - dot(n, l)), 0.0006) * bias_scale * (0.6 + 0.4 * spread);
    float sum = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            vec2 off = vec2(x, y) * spread * uShadowTexel;
            sum += texture(shadow_map, vec3(proj.xy + off, proj.z - bias));
        }
    }
    return mix(uShadowFloor, 1.0, sum / 25.0);
}

vec3 surface_albedo(out float rough_mod) {
    rough_mod = 0.0;
    if (uSurface < 0.5) return uAlbedo;

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
        // Wood: long grain along the room's x and z, rings, a softened edge.
        float along = abs(dot(normalize(vNormal), vec3(0.0, 1.0, 0.0))) > 0.5 ? vRoom.z : vRoom.y;
        float grain = noise(vec2(vRoom.x * 2.0 + vRoom.z * 2.0, along * 55.0));
        float rings = 0.5 + 0.5 * sin((vRoom.x + vRoom.z) * 9.0 + grain * 6.0);
        rough_mod = -0.05 * rings;
        return uAlbedo * (0.82 + 0.14 * rings + 0.1 * grain) * mix(1.0, 0.8, smoothstep(0.46, 0.5, edge));
    }
    if (uSurface > 4.5 && uSurface < 5.5) {
        // Brushed metal: fine streaks, and smoother than its roughness says.
        float brush = noise(vec2(vRoom.x * 400.0, vRoom.y * 6.0 + vRoom.z * 6.0));
        rough_mod = -0.2 + 0.1 * brush;
        return uAlbedo * (0.9 + 0.12 * brush);
    }
    if (uSurface > 5.5 && uSurface < 6.5) {
        // Moulded plastic: a faint speckle and rounded, darker edges.
        float speck = noise(vRoom.xz * 180.0 + vRoom.y * 90.0);
        return uAlbedo * (0.96 + 0.06 * speck) * mix(1.0, 0.78, smoothstep(0.45, 0.5, edge));
    }
    if (uSurface > 6.5) {
        // Fabric: a weave, matte.
        vec2 p = (vRoom.xz + vRoom.yy) * 260.0;
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
    if (uSurface > 8.5) {
        // The sky is not lit and not fogged: it is what the fog fades into.
        FragColor = vec4(sky(normalize(vWorld - uViewPos)), 1.0);
        return;
    }
    float rough_mod;
    vec3 albedo = surface_albedo(rough_mod);
    if (uTexMix > 0.0) {
        vec2 uv = uScreenUV > 0.5 ? gl_FragCoord.xy / uViewport : vUV;
        vec3 tex = uCRT > 0.0 ? crt_sample(uv) : texture(uTex, uv).rgb;
        albedo = mix(albedo, tex, uTexMix);
    }
    float roughness = clamp(uRoughness + rough_mod, 0.05, 1.0);

    vec3 n = normalize(vNormal);
    vec3 v = normalize(uViewPos - vWorld);
    float a2 = roughness * roughness * roughness * roughness;

    vec3 direct = vec3(0.0);
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
            atten = uLightPower[i] / (1.0 + 0.22 * dist + 0.14 * dist * dist);
        }
        vec3 h = normalize(l + v);

        float ndl = max(dot(n, l), 0.0);
        float shadow = 1.0;
        if (ndl > 0.0) {
            if (i == 0) shadow = shadow_factor(vLightSpace0, uShadowMap0, n, l, uShadowBias.x);
            else if (i == 1) shadow = shadow_factor(vLightSpace1, uShadowMap1, n, l, uShadowBias.y);
            else if (i == 2) shadow = shadow_factor(vLightSpace2, uShadowMap2, n, l, uShadowBias.z);
            else if (i == 3) shadow = shadow_factor(vLightSpace3, uShadowMap3, n, l, uShadowBias.w);
        }

        // GGX-ish specular, kept cheap.
        float ndh = max(dot(n, h), 0.0);
        float denom = ndh * ndh * (a2 - 1.0) + 1.0;
        float spec = a2 / (3.14159 * denom * denom + 1e-4);
        spec *= mix(0.04, 0.35, 1.0 - roughness);

        direct += (albedo * ndl + vec3(spec) * ndl) * uLightColor[i] * atten * cone * shadow;
    }

    // Hemispheric ambient: one colour from above, the floor's bounce from below.
    vec3 ambient = albedo * mix(uGround, uSky, n.y * 0.5 + 0.5) * uAmbient;

    vec3 color = ambient + direct + albedo * (uEmissive + uGlow);
    color = mix(color, vec3(1.0, 0.86, 0.45) * (0.3 + 0.7 * length(color)), uHighlight * 0.35);

    // Fog, brighter where it is looked at towards the sun: light scattered
    // on its way through the air.
    vec3 to_frag = vWorld - uViewPos;
    float fog = 1.0 - exp(-uFogDensity * length(to_frag));
    float toward = pow(max(dot(normalize(to_frag), normalize(uSunDir + vec3(0.0, 1e-4, 0.0))), 0.0), 6.0);
    vec3 haze = uFogColor + uSunColor * toward * 0.25;
    color = mix(color, haze, clamp(fog, 0.0, 0.85));

    FragColor = vec4(color, 1.0);
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

inline const char* composite_fs() {
    return R"(#version 330 core
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

// Narkowicz's ACES approximation.
vec3 aces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec3 scene = texture(uScene, vUV).rgb;
    scene += texture(uBloom, vUV).rgb * uBloomStrength;
    vec3 color = aces(scene * uExposure);

    // Cheap FXAA: blend towards the neighbourhood average along the edge.
    float l  = luma(color);
    float lN = luma(texture(uScene, vUV + vec2(0.0,  uTexel.y)).rgb * uExposure);
    float lS = luma(texture(uScene, vUV - vec2(0.0,  uTexel.y)).rgb * uExposure);
    float lE = luma(texture(uScene, vUV + vec2(uTexel.x, 0.0)).rgb * uExposure);
    float lW = luma(texture(uScene, vUV - vec2(uTexel.x, 0.0)).rgb * uExposure);
    float edge = abs(lN + lS + lE + lW - 4.0 * l);
    if (edge > 0.12) {
        vec3 blur = (texture(uScene, vUV + vec2(uTexel.x, 0.0)).rgb +
                     texture(uScene, vUV - vec2(uTexel.x, 0.0)).rgb +
                     texture(uScene, vUV + vec2(0.0, uTexel.y)).rgb +
                     texture(uScene, vUV - vec2(0.0, uTexel.y)).rgb) * 0.25;
        color = mix(color, aces(blur * uExposure), clamp(edge * 2.0, 0.0, 0.6));
    }

    color = mix(vec3(luma(color)), color, uSaturation) * uTint;

    // Vignette and a touch of grain, so flat walls do not band.
    vec2 d = vUV - 0.5;
    color *= 1.0 - dot(d, d) * uVignette;
    color += (fract(sin(dot(vUV, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * uGrain;

    FragColor = vec4(pow(max(color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
})";
}

}  // namespace sg::gl
