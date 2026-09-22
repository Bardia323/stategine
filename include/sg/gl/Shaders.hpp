// Stategine - the shader sources for the forward+post pipeline.
//
// Scene: one spot light with PCF shadows, a hemispheric ambient term, a
// Cook-Torrance-ish specular lobe, procedural surface detail and distance fog,
// written to an HDR target. Post: bright pass, separable blur, then ACES
// tonemap with bloom, vignette and a light FXAA.
#pragma once

namespace sg::gl {

// --- scene --------------------------------------------------------------------
inline const char* scene_vs() {
    return R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat4 uLightViewProj;

out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLightSpace;
out vec3 vLocal;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorld = world.xyz;
    vLocal = aPos;
    vNormal = normalize(mat3(uModel) * aNormal);
    vUV = aUV;
    vLightSpace = uLightViewProj * world;
    gl_Position = uViewProj * world;
})";
}

inline const char* scene_fs() {
    return R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpace;
in vec3 vLocal;

out vec4 FragColor;

uniform vec3  uAlbedo;
uniform float uRoughness;     // 0 mirror-ish, 1 chalk
uniform float uEmissive;
uniform float uHighlight;
uniform float uSurface;       // 0 plain, 1 floor tiles, 2 wall plaster, 3 crate
uniform float uTexMix;        // 0 albedo only, 1 texture only
uniform float uGlow;          // extra emission for an active interface

// Up to four spot lights. Only the first casts shadows: one map, aimed at
// whichever lamp matters most to the viewer.
const int MAX_LIGHTS = 4;
uniform int   uLightCount;
uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec3  uLightDir[MAX_LIGHTS];   // pointing away from the lamp
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uLightPower[MAX_LIGHTS];
uniform float uCosInner[MAX_LIGHTS];
uniform float uCosOuter[MAX_LIGHTS];
uniform vec3  uViewPos;
uniform vec3  uFogColor;
uniform float uFogDensity;

uniform sampler2DShadow uShadowMap;
uniform sampler2D uTex;
uniform vec2 uShadowTexel;

// A portal into another room is sampled in screen space: the other side was
// rendered with the matching virtual camera, so the quad becomes a window
// rather than a picture hanging on the wall.
uniform float uScreenUV;
uniform vec2  uViewport;

float hash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i), b = hash(i + vec2(1, 0)), c = hash(i + vec2(0, 1)), d = hash(i + vec2(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Percentage-closer filtering, 4x4 taps, with a slope-scaled bias.
float shadow_factor(vec3 n, vec3 l) {
    vec3 proj = vLightSpace.xyz / max(vLightSpace.w, 1e-5);
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;
    float bias = max(0.0016 * (1.0 - dot(n, l)), 0.0006);
    float sum = 0.0;
    for (int y = -2; y <= 1; ++y) {
        for (int x = -2; x <= 1; ++x) {
            vec2 off = (vec2(x, y) + 0.5) * uShadowTexel;
            sum += texture(uShadowMap, vec3(proj.xy + off, proj.z - bias));
        }
    }
    return sum / 16.0;
}

vec3 surface_albedo(out float rough_mod) {
    rough_mod = 0.0;
    if (uSurface < 0.5) return uAlbedo;

    if (uSurface < 1.5) {
        // Floor: large tiles with grout and a little grain.
        vec2 t = vWorld.xz * 0.5;
        vec2 cell = fract(t);
        float grout = smoothstep(0.0, 0.035, min(cell.x, cell.y)) *
                      smoothstep(0.0, 0.035, min(1.0 - cell.x, 1.0 - cell.y));
        float shade = mix(0.55, 1.0, hash(floor(t)) * 0.35 + 0.65);
        vec3 tile = uAlbedo * shade * mix(0.45, 1.0, grout);
        rough_mod = mix(-0.25, 0.05, grout);          // grout is rougher than tile
        return tile * (0.94 + 0.12 * noise(vWorld.xz * 8.0));
    }
    if (uSurface < 2.5) {
        // Walls: plaster, with a subtle vertical gradient.
        float grain = 0.92 + 0.16 * noise(vWorld.xz * 6.0 + vWorld.y * 3.0);
        float height = clamp(vWorld.y / 4.0, 0.0, 1.0);
        return uAlbedo * grain * mix(0.82, 1.06, height);
    }
    // Crates: planks plus a darker bevel near the edges of the cube.
    vec3 a = abs(vLocal);
    float edge = max(max(a.x, a.y), a.z);
    float bevel = smoothstep(0.42, 0.5, edge);
    float planks = 0.88 + 0.12 * sin(vLocal.y * 42.0 + hash(vLocal.xz) * 3.0);
    return uAlbedo * planks * mix(1.0, 0.55, bevel);
}

void main() {
    float rough_mod;
    vec3 albedo = surface_albedo(rough_mod);
    if (uTexMix > 0.0) {
        vec2 uv = uScreenUV > 0.5 ? gl_FragCoord.xy / uViewport : vUV;
        albedo = mix(albedo, texture(uTex, uv).rgb, uTexMix);
    }
    float roughness = clamp(uRoughness + rough_mod, 0.05, 1.0);

    vec3 n = normalize(vNormal);
    vec3 v = normalize(uViewPos - vWorld);
    float a2 = roughness * roughness * roughness * roughness;

    vec3 direct = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        vec3 toLight = uLightPos[i] - vWorld;
        float dist = length(toLight);
        vec3 l = toLight / max(dist, 1e-4);
        vec3 h = normalize(l + v);

        // Spot cone, smooth at the rim.
        float theta = dot(normalize(-l), normalize(uLightDir[i]));
        float cone = clamp((theta - uCosOuter[i]) / max(uCosInner[i] - uCosOuter[i], 1e-4),
                           0.0, 1.0);
        cone *= cone;

        float atten = uLightPower[i] / (1.0 + 0.22 * dist + 0.14 * dist * dist);
        float ndl = max(dot(n, l), 0.0);
        // Only the first light has a shadow map.
        float shadow = (i == 0 && ndl > 0.0) ? shadow_factor(n, l) : 1.0;

        // GGX-ish specular, kept cheap.
        float ndh = max(dot(n, h), 0.0);
        float denom = ndh * ndh * (a2 - 1.0) + 1.0;
        float spec = a2 / (3.14159 * denom * denom + 1e-4);
        spec *= mix(0.04, 0.35, 1.0 - roughness);

        direct += (albedo * ndl + vec3(spec) * ndl) * uLightColor[i] * atten * cone * shadow;
    }

    // Hemispheric ambient: cool from above, warm bounce from the floor.
    vec3 sky = vec3(0.10, 0.13, 0.20);
    vec3 ground = vec3(0.14, 0.10, 0.07);
    vec3 ambient = albedo * mix(ground, sky, n.y * 0.5 + 0.5) * 0.55;

    vec3 color = ambient + direct + albedo * (uEmissive + uGlow);
    color = mix(color, vec3(1.0, 0.86, 0.45) * (0.3 + 0.7 * length(color)), uHighlight * 0.35);

    float fog = 1.0 - exp(-uFogDensity * length(uViewPos - vWorld));
    color = mix(color, uFogColor, clamp(fog, 0.0, 0.85));

    FragColor = vec4(color, 1.0);
})";
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

    // Vignette and a touch of grain, so flat walls do not band.
    vec2 d = vUV - 0.5;
    color *= 1.0 - dot(d, d) * 0.55;
    color += (fract(sin(dot(vUV, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) * 0.015;

    FragColor = vec4(pow(max(color, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
})";
}

}  // namespace sg::gl
