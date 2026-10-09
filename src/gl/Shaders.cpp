#include "sg/gl/Shaders.hpp"

namespace sg::gl {

CrtShape crt_shape(double flat) {
    const double t = flat < 0 ? 0.0 : flat > 1 ? 1.0 : flat;
    const auto mix = [t](double a, double b) { return a + (b - a) * t; };
    return {mix(CrtGlass::half_w, 1.0), mix(CrtGlass::half_h, 1.0), mix(CrtGlass::corner, 0.012),
            CrtGlass::bulge * (1.0 - t), mix(CrtGlass::fit, 1.0)};
}

bool crt_picture(double u, double v, double& pu, double& pv, double flat) {
    const CrtShape c = crt_shape(flat);
    const double gx = (u * 2 - 1) / c.half_w, gy = (v * 2 - 1) / c.half_h;
    const double r2 = gx * gx + gy * gy;
    const double k = (1.0 + c.bulge * r2) / c.fit;
    const double wx = gx * k, wy = gy * k;
    pu = wx * 0.5 + 0.5;
    pv = wy * 0.5 + 0.5;
    return wx >= -1 && wx <= 1 && wy >= -1 && wy <= 1;
}

std::string crt_glsl_constants() {
    return "const float kGlassW = " + std::to_string(CrtGlass::half_w) +
           ";\nconst float kGlassH = " + std::to_string(CrtGlass::half_h) +
           ";\nconst float kCorner = " + std::to_string(CrtGlass::corner) +
           ";\nconst float kBulge = " + std::to_string(CrtGlass::bulge) +
           ";\nconst float kFit = " + std::to_string(CrtGlass::fit) + ";\n";
}

const std::string& lights_glsl() {
    static const std::string source = R"(
// Up to twenty-four lights; the first `uShadowCount` carry shadow maps - a
// room's four strongest, chosen by how bright they are, not where the viewer
// is, so shadows do not come and go as you walk; then light from beyond its
// doorways, whose shadows are what stands in the way of the opening. A sun is always first: it lights everything, from one direction,
// without falling off.
const int MAX_LIGHTS = 24;
uniform int   uLightCount;
uniform vec3  uLightPos[MAX_LIGHTS];
uniform vec3  uLightDir[MAX_LIGHTS];   // pointing away from the lamp
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uLightPower[MAX_LIGHTS];
uniform float uCosInner[MAX_LIGHTS];
uniform float uCosOuter[MAX_LIGHTS];
uniform float uLightSun[MAX_LIGHTS];   // 1: parallel light, no cone, no falloff
uniform float uLightFloor[MAX_LIGHTS]; // light left in its full shadow; < 0: uShadowFloor
uniform float uLightIndirect[MAX_LIGHTS]; // 1: stands in for bounced light - diffuse only; 0.6: lit so, but the probes do not stand in for it
uniform float uLightFalloff[MAX_LIGHTS];  // 0: soft falloff, 1: inverse square
uniform float uStraddle;                  // 1: a door's leaf, half in each room, lit by each as far as it is in it
uniform float uLightNear[MAX_LIGHTS];     // a sun's second, close-up shadow map: its layer, or < 0 for none
// Light from beyond a doorway, let in only through its opening: its middle
// and half its width, then which way across it is (x, z), half its height,
// and whether the light is gated at all.
uniform vec4  uLightGate[MAX_LIGHTS];
uniform vec4  uLightGateAxis[MAX_LIGHTS];
uniform float uLightOpen[MAX_LIGHTS];     // and how much of the opening is clear, for a light with no map to say
uniform float uLightScatter[MAX_LIGHTS];  // how much of it the air scatters, times the look's `scatter` (air_fs)
uniform vec4  uLightFrame[MAX_LIGHTS];    // a projector's: the tangents of its half-angles across and up, how soft its edge; 1 if framed
uniform float uLightRange[MAX_LIGHTS];    // how far a lamp's light goes, ending smoothly there (range_window); 0: for ever

uniform float uLightLayer[MAX_LIGHTS];    // the first layer of its shadow maps, or < 0 for none
uniform float uLightCube[MAX_LIGHTS];     // 1: a lamp with no cone, its maps six, a face of a cube round it each

uniform int   uShadowCount;           // how many lights, from the first, have a shadow map
// The first `uShadowCount` lights each have depth maps, layers of one array
// (from `uLightLayer`: one, or six for a lamp with no cone): where each sees
// from (`uShadowVP`), and how much its depth is let slip (`uShadowBias` - a
// sun's range is far longer than a lamp's).
const int MAX_SHADOWS = 24;
uniform sampler2DArrayShadow uShadowMaps;
uniform mat4 uShadowVP[MAX_SHADOWS];
uniform float uShadowBias[MAX_SHADOWS];

// Which of light `i`'s maps sees `p`: its one, or - a lamp with no cone -
// the face of its cube the way from the lamp to `p` goes out through (the
// greatest of the three coordinates of that way: +x, -x, +y, -y, +z, -z).
int shadow_layer(int i, vec3 p) {
    int first = int(uLightLayer[i] + 0.5);
    if (uLightCube[i] < 0.5) return first;
    vec3 d = p - uLightPos[i], a = abs(d);
    int face = a.x >= a.y && a.x >= a.z ? (d.x > 0.0 ? 0 : 1) : a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
    return first + face;
}

// How much of light `i` comes through its doorway to `p`: the way to it
// (towards a lamp, the whole way; towards a sun, a direction) must pass
// through the opening. The edge softens with the distance from it, as a
// penumbra does - wide, since a lamp is not a point and an opening is not a
// knife edge. And there is no line where the opening's plane is crossed: a
// point on the lamp's own side of it, within a hand's breadth of the opening
// (a door's leaf half in each room, the jamb), is lit as nothing stands in the
// way, fading with its depth - and only there: the rest of that side is not
// this room's, whatever lies beyond the plane.
float through_gate(int i, vec3 p, vec3 way, bool parallel) {
    vec4 g = uLightGateAxis[i];
    if (g.w < 0.5 || uStraddle > 0.5) return 1.0;  // (a door's leaf stands in the opening)
    if (g.w > 1.5) return 0.0;                      // only onto a door's leaf
    vec4 at = uLightGate[i];
    vec3 a = vec3(g.x, 0.0, g.y);
    vec3 n = vec3(-a.z, 0.0, a.x);
    float dn = dot(way, n);
    if (abs(dn) < 1e-5) return 0.0;
    float t = dot(at.xyz - p, n) / dn;
    bool lamp_side = t < 0.0 || (!parallel && t > 1.0);
    // Where the way crosses the opening's plane - or, on the lamp's side, where
    // p itself stands over it.
    vec3 q = lamp_side ? p : p + way * t;
    float soft = 0.06 + 0.12 * max(t * length(way), 0.0);
    float u = abs(dot(q - at.xyz, a)), v = abs(q.y - at.y);
    float opening = (1.0 - smoothstep(at.w - soft, at.w + soft, u)) * (1.0 - smoothstep(g.z - soft, g.z + soft, v)) * uLightOpen[i];
    if (!lamp_side) return opening;
    return opening * (1.0 - smoothstep(0.0, 0.3, abs(dot(p - at.xyz, n))));
}

// How much of light `i` reaches `p` - its power, as it falls off with the
// distance, its cone and its doorway - and which way it is (`l`, towards it).
float light_reach(int i, vec3 p, out vec3 l) {
    if (uLightSun[i] > 0.5) {
        l = normalize(-uLightDir[i]);
        return uLightPower[i] * through_gate(i, p, l, true);
    }
    vec3 toLight = uLightPos[i] - p;
    float dist = length(toLight);
    l = toLight / max(dist, 1e-4);
    float cone;
    if (uLightFrame[i].w > 0.5) {
        // A projector's: a rectangle (across level, up the rest), its edges
        // soft over a little of the way in.
        vec3 f = normalize(uLightDir[i]);
        vec3 r = abs(f.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : normalize(cross(f, vec3(0.0, 1.0, 0.0)));
        vec3 u = cross(r, f), v = p - uLightPos[i];
        float z = dot(v, f);
        if (z <= 1e-4) return 0.0;
        vec2 q = abs(vec2(dot(v, r), dot(v, u)) / z) / max(uLightFrame[i].xy, vec2(1e-4));
        float s = uLightFrame[i].z;
        cone = (1.0 - smoothstep(1.0 - s, 1.0, q.x)) * (1.0 - smoothstep(1.0 - s, 1.0, q.y));
        cone = sqrt(cone);  // (squared below, as a round cone's rim is)
    } else {
        // Spot cone, smooth at the rim.
        float theta = dot(-l, normalize(uLightDir[i]));
        cone = clamp((theta - uCosOuter[i]) / max(uCosInner[i] - uCosOuter[i], 1e-4), 0.0, 1.0);
    }
    if (cone <= 0.0) return 0.0;
    // Softly, or as real light does: the inverse square, kept finite at the
    // lamp itself.
    float soft = 1.0 / (1.0 + 0.22 * dist + 0.14 * dist * dist);
    float square = 1.0 / (1.0 + 2.0 * dist * dist);
    // And ended at its range, smoothly - (1 - (d/R)^4)^2, all but 1 where
    // it lights and 0 at the range - so that a lamp that says how far it
    // goes lights nothing past it, and can be left out where it reaches
    // nothing (render::range_window, the same).
    float window = 1.0;
    if (uLightRange[i] > 0.0) {
        float q = dist / uLightRange[i];
        q *= q;
        window = clamp(1.0 - q * q, 0.0, 1.0);
        window *= window;
        if (window <= 0.0) return 0.0;
    }
    return uLightPower[i] * mix(soft, square, uLightFalloff[i]) * window * through_gate(i, p, toLight, false) * (cone * cone);
}
)";
    return source;
}

const char* air_glsl() {
    return R"(
// The light of a world's lamps that its air scatters towards the eye, gathered
// from the eye out to each distance (air_fs, air_sum_fs) in slices over the
// view, and read between the two slices a distance falls between - by the
// sampler, the slices a volume. (Unit 5; uAir.w is 0 when the air scatters
// nothing, and then nothing is read.)
uniform sampler3D uAirLight;
uniform vec4 uAir;  // its first slice's distance, its last's, how many slices; 1
vec3 air_light(float d) {
    if (uAir.w < 0.5) return vec3(0.0);
    float s = clamp(log(max(d, 1e-4) / uAir.x) / log(uAir.y / uAir.x) * (uAir.z - 1.0), 0.0, uAir.z - 1.0);
    // (Nearer than the first slice, as much less as it is nearer.)
    return texture(uAirLight, vec3(gl_FragCoord.xy / uViewport, (s + 0.5) / uAir.z)).rgb * clamp(d / uAir.x, 0.0, 1.0);
}
)";
}

const char* air_fs() {
    static const std::string source = std::string(R"(#version 330 core
out vec4 FragColor;
)") + lights_glsl() + R"(
// Every slice of a view's air at once, each on its own: what its lamps light
// of the air between that slice and the one before, scattered towards the
// eye and dimmed by the air before it. (air_sum_fs adds them up.) A cell for
// each sixteen pixels of the view; the slices go out from the eye in steps
// that widen as they go, as detail does. They are laid side by side, eight
// to a row: a pixel of this picture is a cell of one slice.
uniform vec2  uAirCells;        // cells across the view, and up it
uniform vec4  uAir;             // the first slice's distance, the last's, how many slices
uniform mat4  uAirUnproject;    // the view's clip space back to the world
uniform vec3  uViewPos;
uniform float uFogDensity;      // the air, as the scene fogs it: thick
uniform float uFogStart;        // and clear near the eye
// How much of the way to `d` is in the air that begins `start` from the
// eye: eased in over half `start` either side of where it begins, so the
// place it begins draws no line (on a ceiling, a ring round the eye).
float air_past(float d, float start) {
    float w = 0.5 * start;
    float x = d - start;
    if (w <= 0.0) return max(x, 0.0);
    return x >= w ? x : (x <= -w ? 0.0 : (x + w) * (x + w) / (4.0 * w));
}
uniform float uScatter;         // how much of the light through it a metre of it scatters
uniform float uScatterAhead;    // how much of that goes on ahead (-1..1, 0 every way alike)
uniform vec4  uAirClip;         // only the air on this plane's side is this view's (a doorway's far side)
uniform float uAirSpin;         // which gathering this is: each samples its cells at other points
uniform int   uAirSteps;        // points of each cell lit, spread through its depth (a thin beam is caught)

float air_at(float i) { return uAir.x * pow(uAir.y / uAir.x, i / (uAir.z - 1.0)); }
// Schlick's phase function: how much of light scattered goes off at an
// angle whose cosine is `c` from the way it was going.
float schlick(float c, float g) {
    float k = 1.55 * g - 0.55 * g * g * g, d = 1.0 - k * c;
    return (1.0 - k * k) / (12.5663706 * d * d);
}
float air_shadow(int layer, vec3 p) {
    vec4 s = uShadowVP[layer] * vec4(p, 1.0);
    vec3 q = s.xyz / max(s.w, 1e-5) * 0.5 + 0.5;
    if (q.z > 1.0 || q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return 1.0;
    return texture(uShadowMaps, vec4(q.xy, float(layer), q.z - 0.0004));
}

// A noise that differs from each cell to the next and never lines up
// (interleaved gradient noise): where in its cell each one is sampled.
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

// A cell of a slice, sampled once - at a point of it the noise picks, across
// and down, not at its middle: a lamp's shaft crossing the cells is caught by
// some and missed by their neighbours, a fine grain, never caught or missed by
// a whole slice at once (bands along the walls, at one distance from the eye).
vec3 slice_light(vec3 dir, float k, float down) {
    // The air thins towards the eye, out to where the fog starts: clear close
    // by, thickening smoothly - never a wall at that distance, where a lamp's
    // glow would stop short round whoever is looking.
    float a = k < 0.5 ? 0.0 : air_at(k - 1.0), b = air_at(k);
    if (b <= a) return vec3(0.0);
    float ahead = clamp(uScatterAhead, -0.9, 0.9);
    vec3 lit = vec3(0.0);
    int steps = max(uAirSteps, 1);
    for (int j = 0; j < 16; ++j) {
    if (j >= steps) break;
    vec3 p = uViewPos + dir * mix(a, b, (float(j) + down) / float(steps));
    if (dot(uAirClip.xyz, p) + uAirClip.w < 0.0) continue;
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        // Light standing in for what bounces about lights no air of its own.
        if (uLightIndirect[i] > 0.5 || uLightScatter[i] <= 0.0) continue;
        vec3 l;
        float reach = light_reach(i, p, l);
        if (reach <= 1e-5) continue;
        if (i < uShadowCount && uLightLayer[i] > -0.5) reach *= air_shadow(shadow_layer(i, p), p);
        // Mostly on ahead, some back: two lobes, as haze has.
        float c = dot(dir, l);
        float phase = mix(schlick(c, ahead), schlick(c, -0.3), 0.25);
        lit += uLightColor[i] * (reach * uLightScatter[i] * phase);
    }
    }
    lit /= float(steps);
    // (Pi: the lights carry it folded in, as the scene's diffuse does.)
    float m = 0.5 * (a + b);
    float t = exp(-uFogDensity * air_past(m, uFogStart));
    float near = uFogStart > 0.0 ? smoothstep(0.0, uFogStart, m) : 1.0;
    vec3 s = lit * (3.14159265 * uScatter * (b - a) * t * near);
    return any(isnan(s)) || any(isinf(s)) ? vec3(0.0) : s;
}

void main() {
    ivec2 cells = ivec2(uAirCells), px = ivec2(gl_FragCoord.xy), tile = px / cells;
    vec2 cell = vec2(px - tile * cells);
    float k = float(tile.y * 8 + tile.x);
    vec2 seed = cell + vec2(k * 5.588238, k * 3.0);
    // (Moved on by an even step each gathering - the plastic numbers - so the
    // gatherings averaged cover each cell evenly.)
    vec3 step3 = uAirSpin * vec3(0.8191725, 0.6710436, 0.5497005);
    vec2 across = fract(vec2(ign(seed), ign(seed + vec2(17.0, 59.0))) + step3.xy) - 0.5;
    vec2 uv = (cell + 0.5 + across) / uAirCells;
    vec4 far = uAirUnproject * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec3 dir = normalize(far.xyz / far.w - uViewPos);
    FragColor = vec4(slice_light(dir, k, fract(ign(seed + vec2(41.0, 7.0)) + step3.z)), 1.0);
}
)";
    return source.c_str();
}

const char* taa_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uCurrent;
uniform sampler2D uHistory;
uniform sampler2D uDepth;
uniform mat4 uReproject;
uniform vec2 uJitter;
uniform vec2 uTexel;
uniform float uFresh;
// Blended in a curve that keeps a bright pixel from outweighing its
// neighbours (and back): what makes a highlight flicker otherwise.
vec3 squash(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 unsquash(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4); }
vec3 ycocg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 rgb(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
// What was, read between its pixels sharply (Catmull-Rom, in five taps):
// read bilinearly, the history would soften a little every frame.
vec3 history_at(vec2 uv) {
    vec2 size = 1.0 / uTexel;
    vec2 pos = uv * size;
    vec2 c = floor(pos - 0.5) + 0.5;
    vec2 f = pos - c;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (c - 1.0) * uTexel, t3 = (c + 2.0) * uTexel, t12 = (c + w2 / w12) * uTexel;
    vec3 r = texture(uHistory, vec2(t12.x, t0.y)).rgb * (w12.x * w0.y) + texture(uHistory, vec2(t0.x, t12.y)).rgb * (w0.x * w12.y) +
             texture(uHistory, t12).rgb * (w12.x * w12.y) + texture(uHistory, vec2(t3.x, t12.y)).rgb * (w3.x * w12.y) +
             texture(uHistory, vec2(t12.x, t3.y)).rgb * (w12.x * w3.y);
    float ws = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / ws, vec3(0.0));
}
void main() {
    vec3 cur = texture(uCurrent, vUV).rgb;
    if (uFresh > 0.5) {
        FragColor = vec4(cur, 1.0);
        return;
    }
    // The neighbourhood: what this frame says the pixel may be, and the
    // nearest depth in it (an edge moves with what is in front).
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    float near = 1.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 at = vUV + vec2(x, y) * uTexel;
            vec3 c = ycocg(squash(texture(uCurrent, at).rgb));
            m1 += c;
            m2 += c * c;
            near = min(near, texture(uDepth, at).r);
        }
    vec3 mean = m1 / 9.0, spread = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    // Where it was last frame.
    vec4 was = uReproject * vec4(vUV * 2.0 - 1.0 - uJitter, near * 2.0 - 1.0, 1.0);
    vec2 back = was.xy / was.w * 0.5 + 0.5;
    if (was.w <= 0.0 || any(lessThan(back, vec2(0.0))) || any(greaterThan(back, vec2(1.0)))) {
        FragColor = vec4(cur, 1.0);
        return;
    }
    // What was, brought within what is: towards the neighbourhood's middle
    // until it is inside its spread.
    vec3 h = ycocg(squash(history_at(back)));
    vec3 lo = mean - spread * 1.25, hi = mean + spread * 1.25;
    vec3 mid = 0.5 * (lo + hi), half_ = max(0.5 * (hi - lo), vec3(1e-5));
    vec3 off = h - mid;
    vec3 out_ = abs(off / half_);
    float most = max(out_.x, max(out_.y, out_.z));
    if (most > 1.0) h = mid + off / most;
    vec3 now = squash(cur);
    FragColor = vec4(unsquash(mix(rgb(h), now, 0.1)), 1.0);
}
)";
}

const char* air_sum_fs() {
    return R"(#version 330 core
layout(location = 0) out vec4 Slice[8];
// Eight slices of a view's air, each what the slices from the eye to it
// gathered (air_fs, laid eight to a row in uAirLocal): the light the air
// scatters towards the eye up to that distance.
uniform sampler2D uAirLocal;
uniform vec2 uAirCells;  // cells across the view, and up it
uniform int uAirGroup;   // the first of the eight
void main() {
    ivec2 cells = ivec2(uAirCells), at = ivec2(gl_FragCoord.xy);
    vec3 sum = vec3(0.0);
    for (int k = 0; k < uAirGroup; ++k) sum += texelFetch(uAirLocal, at + ivec2(k % 8, k / 8) * cells, 0).rgb;
    for (int j = 0; j < 8; ++j) {
        int k = uAirGroup + j;
        sum += texelFetch(uAirLocal, at + ivec2(k % 8, k / 8) * cells, 0).rgb;
        Slice[j] = vec4(sum, 1.0);
    }
}
)";
}

// --- relighting a room's light from all round (GLWorldView::relight_room) ----------
//
// What each box sees, a box a row of six faces (`size` square each, rows
// from the bottom), lit by one light - its own (the scene's light_reach, as
// light 0) shadowed by how far the light sees (a lamp's six faces in a row,
// or one view down a sun's way) - and, after the first bounce, by the boxes'
// light as the scene shader blends them.
const char* relight_fs() {
    static const std::string source = std::string(R"(#version 330 core
)") + lights_glsl() + R"(
uniform sampler2D uWhere;    // where each surface is
uniform sampler2D uFacing;   // which way it faces (none: no surface)
uniform sampler2D uScatter;  // what of the light it scatters
uniform sampler2D uFar;      // how far the light sees, every way
uniform int   uFarSun;       // 1: one view down a sun's way; 0: a lamp's six faces in a row
uniform int   uFarSize;
uniform vec3  uFarAt;        // where it sees from
uniform vec3  uFarF, uFarR, uFarU;
uniform float uFarT;         // a sun's view: the tangent of its half-angle
uniform int   uBoxes;
uniform vec4  uBoxAt[8];     // each box's middle, and its turn
uniform vec3  uBoxHalf[8];
uniform vec3  uBoxLo[8];
uniform vec3  uBoxHi[8];
uniform sampler2D uBefore;   // the boxes' light, the bounce before: nine across, a face of a box up (relight_sh_fs)
uniform int   uBounce;       // 1: and the boxes' light on it
out vec4 FragColor;

const vec3 kFaceF[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 kFaceU[6] = vec3[6](vec3(0, 1, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 0));

// Whether the light reaches `p` with nothing in the way.
float seen_by_light(vec3 p) {
    vec3 d = p - uFarAt;
    float len = length(d);
    ivec2 q;
    if (uFarSun == 1) {
        float z = dot(d, uFarF);
        if (z <= 0.0) return 0.0;
        vec2 uv = vec2(dot(d, uFarR), dot(d, uFarU)) / (z * uFarT);
        if (abs(uv.x) >= 1.0 || abs(uv.y) >= 1.0) return 1.0;  // beyond what its shadow holds
        q = clamp(ivec2((uv + 1.0) * 0.5 * float(uFarSize)), ivec2(0), ivec2(uFarSize - 1));
    } else {
        vec3 a = abs(d);
        int f = a.x >= a.y && a.x >= a.z ? (d.x > 0.0 ? 0 : 1) : a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
        vec3 r = normalize(cross(kFaceF[f], kFaceU[f]));
        float z = dot(d, kFaceF[f]);
        vec2 uv = vec2(dot(d, r), dot(d, kFaceU[f])) / z;
        q = clamp(ivec2((uv + 1.0) * 0.5 * float(uFarSize)), ivec2(0), ivec2(uFarSize - 1));
        q.x += f * uFarSize;
    }
    float seen = texelFetch(uFar, q, 0).r;
    return seen <= 0.0 || len <= seen + 0.05 + 0.03 * (uFarSun == 1 ? 1.0 : len) ? 1.0 : 0.0;
}

// How much box `i` holds `p`: the scene shader's probe_weight.
float box_weight(int i, vec3 p) {
    vec3 d = p - uBoxAt[i].xyz;
    float c = cos(uBoxAt[i].w), s = sin(uBoxAt[i].w);
    vec3 q = vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
    vec3 a = clamp(1.0 + (q + uBoxHalf[i]) / max(uBoxLo[i], vec3(1e-4)), 0.0, 1.0);
    vec3 b = clamp(1.0 + (uBoxHalf[i] - q) / max(uBoxHi[i], vec3(1e-4)), 0.0, 1.0);
    vec3 w = a * b;
    w = w * w * (3.0 - 2.0 * w);
    return w.x * w.y * w.z;
}

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec3 n = texelFetch(uFacing, px, 0).xyz;
    if (length(n) < 0.5) {
        FragColor = vec4(0.0);
        return;
    }
    n = normalize(n);
    vec3 p = texelFetch(uWhere, px, 0).xyz;
    vec3 albedo = texelFetch(uScatter, px, 0).rgb;
    vec3 l;
    float reach = light_reach(0, p, l);
    float ndl = dot(n, l);
    vec3 c = reach > 0.0 && ndl > 0.0 ? albedo * uLightColor[0] * (reach * ndl * seen_by_light(p)) : vec3(0.0);
    if (uBounce == 1) {
        // The boxes' light on it, as a surface facing `n` is given it (2/3
        // and 1/4 on the finer bands), blended by how much each holds it.
        float b[9] = float[9](0.282095, 0.488603 * n.y * (2.0 / 3.0), 0.488603 * n.z * (2.0 / 3.0), 0.488603 * n.x * (2.0 / 3.0),
                              1.092548 * n.x * n.y * 0.25, 1.092548 * n.y * n.z * 0.25, 0.315392 * (3.0 * n.z * n.z - 1.0) * 0.25,
                              1.092548 * n.x * n.z * 0.25, 0.546274 * (n.x * n.x - n.y * n.y) * 0.25);
        vec3 sum = vec3(0.0);
        float total = 0.0;
        for (int i = 0; i < 8; ++i) {
            if (i >= uBoxes) break;
            float w = box_weight(i, p);
            if (w <= 0.0) continue;
            vec3 e = vec3(0.0);
            for (int k = 0; k < 9; ++k)
                for (int f = 0; f < 6; ++f) e += texelFetch(uBefore, ivec2(k, i * 6 + f), 0).rgb * b[k];
            sum += max(e, vec3(0.0)) * w;
            total += w;
        }
        if (total > 0.0) c += albedo * sum * (min(total, 1.0) / total);
    }
    FragColor = vec4(c, 1.0);
}
)";
    return source.c_str();
}

// The light a box sees, every way, as nine harmonics: a pixel a harmonic
// (across) and a face of a box (up: six a box), each the sum over the face
// of the light seen that way times the harmonic there, over the solid angle
// a texel covers (Sh9::add, the same basis) - a box's six added up after.
const char* relight_sh_fs() {
    return R"(#version 330 core
uniform sampler2D uLight;   // what each box sees, lit (relight_fs)
uniform int uSize;
out vec4 FragColor;
const vec3 kFaceF[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 kFaceU[6] = vec3[6](vec3(0, 1, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 0));
float basis(int k, vec3 d) {
    if (k == 0) return 0.282095;
    if (k == 1) return 0.488603 * d.y;
    if (k == 2) return 0.488603 * d.z;
    if (k == 3) return 0.488603 * d.x;
    if (k == 4) return 1.092548 * d.x * d.y;
    if (k == 5) return 1.092548 * d.y * d.z;
    if (k == 6) return 0.315392 * (3.0 * d.z * d.z - 1.0);
    if (k == 7) return 1.092548 * d.x * d.z;
    return 0.546274 * (d.x * d.x - d.y * d.y);
}
void main() {
    int k = int(gl_FragCoord.x), row = int(gl_FragCoord.y);
    int box = row / 6, f = row - box * 6;
    float texel = 2.0 / float(uSize);
    vec3 sum = vec3(0.0);
    vec3 r = normalize(cross(kFaceF[f], kFaceU[f]));
    for (int y = 0; y < uSize; ++y)
        for (int x = 0; x < uSize; ++x) {
            vec3 c = texelFetch(uLight, ivec2(f * uSize + x, box * uSize + y), 0).rgb;
            if (c == vec3(0.0)) continue;
            float u = (float(x) + 0.5) * texel - 1.0, v = (float(y) + 0.5) * texel - 1.0;
            vec3 d = normalize(kFaceF[f] + r * u + kFaceU[f] * v);
            float w = texel * texel / pow(1.0 + u * u + v * v, 1.5);
            sum += c * (basis(k, d) * w);
        }
    FragColor = vec4(sum, 1.0);
}
)";
}


const char* scene_fs() {
    static const std::string source = std::string(R"(#version 330 core
#extension GL_ARB_shader_image_load_store : enable
in vec3 vWorld;
// Where a fragment is lit as being: where it is - or, for a copy of a space
// that wraps, where its original is (uLatticeShift), lit and shadowed as that is.
uniform vec3 uLatticeShift;
vec3 vLit;
// How `vLit` changes from this pixel to the next across and up: taken once,
// before the lamps are gone through (derivatives are not to be had where
// some pixels of a quad have left the loop), for the shadow's receiver plane.
vec3 vLitDx, vLitDy;
in vec3 vRoom;
in vec3 vNormal;
in vec2 vUV;
in vec3 vLocal;
in vec3 vObject;
in vec3 vObjNormal;
in vec3 vRoomNormal;
in vec3 vTexScale;

// Whether this point is seen is known before it is shaded: what stands in
// front is never lit for nothing. (Not where a picture is cut out of its
// card - SG_CUTOUT, the program those are drawn with - since what is cut away
// must not hide what is behind it.)
#if !defined(SG_CUTOUT) && defined(GL_ARB_shader_image_load_store)
layout(early_fragment_tests) in;
#endif
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
uniform float uSkin;          // 1: a box wearing a texture atlas, a cell a face (skin_uv); 2: a picture tiled over the world (world_uv)
uniform float uTile;          // with uSkin 2: the metres of the world one picture covers
uniform float uSkinTile;      // with uSkin 1: metres of the thing a cell covers, its pattern going on round it; 0: a face the whole cell
uniform vec3  uSkinSize;      // with uSkinTile: the thing's size
uniform float uSkinBlend;     // with uSkin 1: 0 a face its own cell; 1 a curve soft between the cells of the ways it faces
uniform float uSkinFramed;    // 1: projected in the frame of what wears it (uSkinFrame) - the thing, all its parts as one
uniform mat4  uSkinFrame;     // the room's frame to that thing's unit box
uniform float uSkinOwn;       // 1: the frame is the mesh's own (drawn with others of its shape, each its own size)
uniform float uSkinRelief;    // > 0: the skin's alpha is how high its paint stands, this many metres at 1 - the surface bent by it
uniform vec4  uUVRect;        // a cell of the picture: its corner, its size (unused while its size is 0)
uniform float uCutout;        // 1: clear pixels are not drawn (a sprite)
uniform float uGlow;          // extra emission for an active interface
uniform float uGlass;         // > 0: glass, this clear - what is behind it seen through it, more of it reflected at a glance (Fresnel)
)" + lights_glsl() + R"(
uniform vec3  uViewPos;
uniform vec3  uFogColor;
uniform float uFogDensity;
uniform float uFogStart;      // how far from the eye the air begins
// How much of the way to `d` is in the air that begins `start` from the
// eye: eased in over half `start` either side of where it begins, so the
// place it begins draws no line (on a ceiling, a ring round the eye).
float air_past(float d, float start) {
    float w = 0.5 * start;
    float x = d - start;
    if (w <= 0.0) return max(x, 0.0);
    return x >= w ? x : (x <= -w ? 0.0 : (x + w) * (x + w) / (4.0 * w));
}
uniform vec4  uHostFog;       // seen through a doorway from another world: its air (density, start, full, and 1)
uniform vec3  uHostFogColor;
uniform vec4  uDoorPlane;     // and the doorway's plane, here: normal and offset
uniform float uFogFull;       // 1: the air takes all at last (a space with no end); else at most 0.85 of it
uniform vec3  uSky;           // ambient from above
uniform vec3  uGround;        // ambient bounced from the floor
// The doorways of the room being drawn, and the light from all round on
// their other sides: the middle of each opening and half its width; which
// way is across it (x, z) and half its height; which way is into this room
// (x, z); the other side's sky and ground light, each times its amount.
const int MAX_DOORS = 4;
uniform int  uDoorCount;
uniform vec4 uDoorAt[MAX_DOORS];
uniform vec4 uDoorAxis[MAX_DOORS];
uniform vec4 uDoorIn[MAX_DOORS];
uniform vec3 uDoorSky[MAX_DOORS];
uniform vec3 uDoorGround[MAX_DOORS];
uniform float uAmbient;
// Light probes of the room being drawn (sg/domains/Probe.hpp): a box each -
// its middle and its turn (yaw), half its size, how far in from each side
// its light fades in (the low sides, the high) - and what its lamps give it
// from every way after lighting the room, as nine coefficients of spherical
// harmonics.
const int MAX_PROBES = 8;
uniform int  uProbeCount;
// What a surface is, not how it is lit (relight_probes): 1 where it is, 2
// which way it faces, 3 what of the light it scatters, 4 how far it is
// from the eye. 0: lit, as ever.
uniform int  uSurfaceOnly;
uniform vec4 uProbeAt[MAX_PROBES];
uniform vec3 uProbeHalf[MAX_PROBES];
uniform vec3 uProbeSoftLo[MAX_PROBES];
uniform vec3 uProbeSoftHi[MAX_PROBES];
uniform vec3 uProbeSH[MAX_PROBES * 9];
uniform float uDark;          // 1: what glows of itself does not (a probe's bake, lamp by lamp)

// An open world has a sky instead of a ceiling: a gradient, and the sun in it.
uniform vec3  uSkyTop;
uniform vec3  uSkyHorizon;
uniform vec3  uSunDir;        // towards the sun
uniform vec3  uSunColor;

uniform sampler2D uTex;
uniform samplerCube uEnv;     // what is seen every way from a polished thing (a glass's reflection)
// The planes the eye's room reflects in, and the room seen in each
// (GLWorldView::draw_mirrors): a plane as normal and offset; the part of the
// screen it was drawn for (clip x0, y0, x1, y1); how much it reflects, how
// much of its picture holds that part (x, y), and its picture's mip levels.
uniform int  uMirrorCount;
uniform vec4 uMirrorPlane[2];
uniform vec4 uMirrorSeen[2];
uniform vec4 uMirrorUse[2];
uniform vec2 uMirrorScreen;   // the picture being drawn, in pixels
uniform float uGlassMirror;   // 1 + which of them a window's glass is (0: none)
uniform float uSeenFrom;      // 1: a doorway in a mirror, its picture where this eye sees it
uniform mat4  uSeenFromVP;
uniform sampler2D uMirror0;
uniform sampler2D uMirror1;
uniform float uEnvMix;        // > 0: it reflects uEnv, by Fresnel - faint face on, strong at its edges
uniform vec2 uShadowTexel;
uniform float uShadowSoft;    // how wide the filter is, in texels (1: tight)
uniform float uShadowFloor;   // how much light is left in a full shadow - bounce, faked
uniform float uTime;
uniform float uWind;          // sand drifting over the dunes, 0 for none
uniform float uStars;         // how much of the night sky shows, 0 by day
uniform float uClouds;        // how much of the sky is cloud, 0 for none
uniform vec3  uCloudColor;    // a cloud's lit side
uniform vec3  uCloudShade;    // and its shaded underside
uniform float uMirror;        // how much of the real sky a surface reflects
// Land (sg::terrain): uSplat 1 - ground (surface 19) whose uTex is each of
// four layers' share at each point (RGBA), over the room's xz by
// uSplatRect (its corner, one over its size); each layer a surface of these
// (uLayerSurface) in its colour (uLayerColor). uSplat 2 - water (surface 18)
// whose uTex's red is how deep it is under each point of its own uv, as a
// share of uWaterDeepest metres, deepening to uWaterDeep. uLines: a road's
// painted lines (asphalt, by its uv: across 0..1, along in metres).
uniform float uSplat;
uniform vec4  uSplatRect;
uniform vec4  uLayerSurface;
uniform vec3  uLayerColor[4];
uniform vec3  uWaterDeep;
uniform float uWaterDeepest;
uniform float uLines;
uniform float uCalm;          // water: 0 its full swell, 1 still

// The material this fragment is of: the uniforms, or - drawn as one of many
// at once - what its instance carries.
flat in vec4 vMat0;
flat in vec4 vMat1;
flat in float vInstanced;
vec3 mAlbedo;
float mRoughness, mSurface, mEmissive, mHighlight, mMirror;
uniform float uTexFlip;       // 1: the picture's rows run bottom up (a rendered one)
uniform float uUntone;        // 1: the picture is already developed (a world's feed): undo the tone curve

// A portal into another room is sampled in screen space: the other side was
// rendered with the matching virtual camera, so the quad becomes a window
// rather than a picture hanging on the wall.
uniform float uScreenUV;
uniform vec4 uScreenRect;                // (uScreenUV) the part of the picture the quad shows, 0..1: all of it is 0,0,1,1
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
uniform float uUndim;         // 1: this is part of the screen the room dims round, not dimmed with it
// How flat the tube is seen (crt_shape): 0 as it is, 1 face up to the glass.
uniform float uFlat;
)") + crt_glsl_constants() + air_glsl() + R"(
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

// Where on a skin's cell (0..5) a point of the thing is, seen from that cell's
// way: the whole face 0..1 (as skin_uv), or - tiling - metres over
// uSkinTile, the four sides one band round the thing (+z, +x, -z, -x) so
// what runs round it runs on from face to face.
vec2 skin_at(float cell, vec3 p, vec3 size) {
    if (uSkinTile <= 0.0) {
        if (cell < 0.5) return vec2(0.5 - p.z, p.y + 0.5);
        if (cell < 1.5) return vec2(p.z + 0.5, p.y + 0.5);
        if (cell < 2.5) return vec2(p.x + 0.5, p.y + 0.5);
        if (cell < 3.5) return vec2(0.5 - p.x, p.y + 0.5);
        if (cell < 4.5) return vec2(p.x + 0.5, 0.5 - p.z);
        return vec2(p.x + 0.5, p.z + 0.5);
    }
    vec3 h = size * 0.5, m = p * size;
    float u;
    if (cell < 0.5) u = 2.0 * h.x + (h.z - m.z);
    else if (cell < 1.5) u = 4.0 * h.x + 2.0 * h.z + (m.z + h.z);
    else if (cell < 2.5) u = m.x + h.x;
    else if (cell < 3.5) u = 2.0 * h.x + 2.0 * h.z + (h.x - m.x);
    else u = m.x;
    float v = cell < 3.5 ? m.y : (cell < 4.5 ? -m.z : m.z);
    return vec2(u, v) / uSkinTile;
}

// That point of that cell of the atlas, filtered as the unwrapped place
// changes across the screen (no seam where a tiling wraps).
vec4 skin_sample(float cell, vec2 uv, vec2 dx, vec2 dy) {
    vec2 f = clamp(uSkinTile > 0.0 ? fract(uv) : uv, 0.002, 0.998);
    float col = mod(cell, 3.0), row = floor(cell / 3.0);
    vec2 k = vec2(1.0 / 3.0, -0.5);
    return textureGrad(uTex, vec2((col + f.x) / 3.0, (row + 1.0 - f.y) / 2.0), dx * k, dy * k);
}

// A skin worn by any shape: each point from the cell of the way it most
// faces, or (uSkinBlend) from those of the ways it half faces, weighted.
vec4 skin_texel() {
    // In the unit box of the thing that wears it, facing as it truly faces
    // there (its normal scaled back out of the box).
    vec3 size = uSkinOwn > 0.5 ? vTexScale : uSkinSize;
    vec3 p = uSkinOwn > 0.5 ? vLocal : (uSkinFrame * vec4(vRoom, 1.0)).xyz;
    vec3 n = normalize(uSkinOwn > 0.5 ? vObjNormal / max(size, vec3(1e-4)) : uSkinSize * (mat3(uSkinFrame) * vRoomNormal)), a = abs(n);
    float cx = n.x > 0.0 ? 0.0 : 1.0, cy = n.y > 0.0 ? 4.0 : 5.0, cz = n.z > 0.0 ? 2.0 : 3.0;
    vec2 ux = skin_at(cx, p, size), uy = skin_at(cy, p, size), uz = skin_at(cz, p, size);
    vec2 dxx = dFdx(ux), dyx = dFdy(ux), dxy = dFdx(uy), dyy = dFdy(uy), dxz = dFdx(uz), dyz = dFdy(uz);
    vec3 w = pow(a, vec3(mix(24.0, 2.0, clamp(uSkinBlend, 0.0, 1.0))));
    if (uSkinBlend <= 0.0) w = a.x >= a.y && a.x >= a.z ? vec3(1, 0, 0) : (a.z >= a.y ? vec3(0, 0, 1) : vec3(0, 1, 0));
    w /= max(w.x + w.y + w.z, 1e-5);
    vec4 c = vec4(0.0);
    if (w.x > 0.001) c += w.x * skin_sample(cx, ux, dxx, dyx);
    if (w.y > 0.001) c += w.y * skin_sample(cy, uy, dxy, dyy);
    if (w.z > 0.001) c += w.z * skin_sample(cz, uz, dxz, dyz);
    return c;
}

// A picture tiled over the world, as a wall or a floor wears it: laid on
// the plane the surface most faces, upright on walls.
vec2 world_uv() {
    // In the room's own frame, so it stays put seen through a doorway too.
    vec3 a = abs(cross(dFdx(vRoom), dFdy(vRoom))), w = vRoom / max(uTile, 1e-3);
    if (a.y >= a.x && a.y >= a.z) return vec2(w.x, w.z);
    if (a.x >= a.z) return vec2(w.z, -w.y);
    return vec2(w.x, -w.y);
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

// The light from all round at `p`: this room's, and near a doorway, halfway
// to the other side's at the opening itself - so a thing standing through a
// doorway (a door ajar, half in each room) is lit as one thing, softly,
// and not cut in two where the rooms meet.
void around_at(vec3 p, out vec3 sky, out vec3 ground) {
    sky = uSky * uAmbient;
    ground = uGround * uAmbient;
    for (int i = 0; i < MAX_DOORS; ++i) {
        if (i >= uDoorCount) break;
        if (uDoorIn[i].z > 0.5) continue;  // shut: only its leaf is in both rooms
        vec3 d = p - uDoorAt[i].xyz;
        float s = dot(d.xz, uDoorIn[i].xy);
        float u = abs(dot(d.xz, uDoorAxis[i].xy)) - uDoorAt[i].w, v = abs(d.y) - uDoorAxis[i].z;
        // Only in the opening itself: its frame and the wall round it are
        // this room's, all of them.
        float there = (1.0 - smoothstep(-0.12, -0.02, u)) * (1.0 - smoothstep(-0.12, -0.02, v)) * (1.0 - smoothstep(-0.35, 0.35, s));
        sky = mix(sky, uDoorSky[i], there);
        ground = mix(ground, uDoorGround[i], there);
    }
}

// How much probe `i` holds `p`: 1 in its box, fading to 0 over its soft
// edge beyond each side (so a box the size of its room holds its walls).
float probe_weight(int i, vec3 p) {
    vec4 at = uProbeAt[i];
    vec3 d = p - at.xyz;
    float c = cos(at.w), s = sin(at.w);
    vec3 q = vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
    vec3 lo = q + uProbeHalf[i], hi = uProbeHalf[i] - q;
    vec3 a = clamp(1.0 + lo / max(uProbeSoftLo[i], vec3(1e-4)), 0.0, 1.0);
    vec3 b = clamp(1.0 + hi / max(uProbeSoftHi[i], vec3(1e-4)), 0.0, 1.0);
    vec3 w = a * b;
    w = w * w * (3.0 - 2.0 * w);
    return w.x * w.y * w.z;
}

// The light the probes hold arriving along `d` - each band weighted as a
// surface takes it (w1, w2: 2/3 and 1/4 for the light a surface facing `d`
// is given, over pi; 1 and 1 for what arrives along it) - blended by how much
// each holds `p`; and how much they hold it at all, in `cover` (0: none).
vec3 probe_light(vec3 p, vec3 d, float w1, float w2, out float cover) {
    float x = d.x, y = d.y, z = d.z;
    float b[9] = float[9](0.282095, 0.488603 * y * w1, 0.488603 * z * w1, 0.488603 * x * w1,
                          1.092548 * x * y * w2, 1.092548 * y * z * w2, 0.315392 * (3.0 * z * z - 1.0) * w2,
                          1.092548 * x * z * w2, 0.546274 * (x * x - y * y) * w2);
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int i = 0; i < MAX_PROBES; ++i) {
        if (i >= uProbeCount) break;
        float w = probe_weight(i, p);
        if (w <= 0.0) continue;
        vec3 e = vec3(0.0);
        for (int k = 0; k < 9; ++k) e += uProbeSH[i * 9 + k] * b[k];
        sum += max(e, vec3(0.0)) * w;
        total += w;
    }
    cover = min(total, 1.0);
    return total > 0.0 ? sum / total : vec3(0.0);
}

// Percentage-closer filtering over a disc: 16 taps on a Vogel spiral, each the
// hardware's own 2x2, the spiral turned for each pixel so the penumbra is a
// smooth gradient dithered finely, not the steps of a fixed grid. The disc
// reaches `uShadowSoft` times two texels, with a slope-scaled bias. What is
// left in the darkest shadow is `uShadowFloor`.
// Sixteen points of a spiral (the golden angle round, the square root out),
// turned for each pixel by one angle: soft, and no seam of a pattern.
const vec2 kSpiral[16] = vec2[16](vec2(0.176777, 0.000000), vec2(-0.225772, 0.206826), vec2(0.034558, -0.393771), vec2(0.284571, 0.371173),
    vec2(-0.522223, -0.092374), vec2(0.494695, -0.314685), vec2(-0.165466, 0.615525), vec2(-0.315562, -0.607594),
    vec2(0.684642, 0.250030), vec2(-0.712256, 0.294009), vec2(0.343354, -0.733729), vec2(0.253731, 0.808932),
    vec2(-0.764746, -0.443186), vec2(0.897134, -0.197233), vec2(-0.547507, 0.778772), vec2(-0.126487, -0.976090));

float shadow_factor(int layer, vec3 n, vec3 l, float floor_) {
    float spread = max(uShadowSoft, 0.5);
    // Looked up a little off the surface, along its normal, by about as many
    // texels as the filter reaches - measured in metres where the point is
    // (`uShadowBias`: a texel's width at a unit's distance from a lamp, or a
    // sun's whole), not in depth: so a shadow meets what casts it - a table's
    // foot, the bottom of a bin - with no gap and no light under it, and a lit
    // face does not shadow itself.
    vec4 here = uShadowVP[layer] * vec4(vLit, 1.0);
    float texel = uShadowBias[layer] * max(here.w, 1e-5);
    float ndl = clamp(dot(n, l), 0.0, 1.0);
    float slope = sqrt(max(1.0 - ndl * ndl, 0.0));
    // (Less than it was: each tap now follows the surface's own slope in
    // the map - the receiver plane, below - so the push need only cover
    // the filter's own error, not the slope's.)
    vec4 light_space = uShadowVP[layer] * vec4(vLit + n * texel * (0.3 + 0.8 * slope) * spread, 1.0);
    vec3 proj = light_space.xyz / max(light_space.w, 1e-5);
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;
    // The receiver plane: how the surface's depth in the map changes across
    // the map, so that a tap off to one side is compared with the surface
    // as it is there, not as it is here - a wide filter on a slanted floor
    // otherwise shadows the floor with itself. Found from how this pixel
    // and its neighbours land in the map: J takes a step on the screen to
    // a step in the map, g to a step in depth; a step in the map is then
    // inverse(J)^T g in depth. Where J is all but singular (the surface seen
    // edge on from the light) nothing is said, and the push does it.
    vec3 base = here.xyz / max(here.w, 1e-5) * 0.5 + 0.5;
    vec4 hx = uShadowVP[layer] * vec4(vLit + vLitDx, 1.0);
    vec4 hy = uShadowVP[layer] * vec4(vLit + vLitDy, 1.0);
    vec3 sx = hx.xyz / max(hx.w, 1e-5) * 0.5 + 0.5 - base;
    vec3 sy = hy.xyz / max(hy.w, 1e-5) * 0.5 + 0.5 - base;
    float det = sx.x * sy.y - sy.x * sx.y;
    vec2 dz_duv = vec2(0.0);
    if (abs(det) > 1e-14) dz_duv = vec2(sy.y * sx.z - sx.y * sy.z, sx.x * sy.z - sy.x * sx.z) / det;
    // (No steeper than a few depth steps a texel: across an edge, where
    // the neighbour is another surface, it means nothing.)
    float most = 0.002 / max(max(uShadowTexel.x, uShadowTexel.y), 1e-6);
    float steep = length(dz_duv);
    if (steep > most) dz_duv *= most / steep;
    // Towards the edge of the map, the shadow fades out rather than stopping
    // on a line: a sun's box round the viewer has an edge a lamp's cone does
    // not, and far off is where it would show.
    float edge = smoothstep(0.82, 0.98, max(abs(proj.x * 2.0 - 1.0), abs(proj.y * 2.0 - 1.0)));
    if (edge >= 1.0) return 1.0;
    float bias = 0.00006;
    // (Turned again each 24th of a second of the world's time, as the film's
    // grain is: a dither that stood still was a layer of noise on the screen,
    // the same wherever the eye looked.)
    float turn = fract(ign(gl_FragCoord.xy) + floor(uTime * 24.0) * 0.618034) * 6.2831853;
    float c = cos(turn), s = sin(turn);
    mat2 spin = mat2(c, s, -s, c) * 2.4 * spread;
    vec2 texel2 = uShadowTexel;
    // Four, across the spiral, first: wholly lit or wholly in shadow there,
    // it is so all through - only an edge between (a penumbra) takes all
    // sixteen.
    float sum = 0.0;
    for (int i = 3; i < 16; i += 4) {
        vec2 off = spin * kSpiral[i] * texel2;
        sum += texture(uShadowMaps, vec4(proj.xy + off, float(layer), proj.z + dot(dz_duv, off) - bias));
    }
    float k;
    if (sum < 0.001 || sum > 3.999) {
        k = sum * 0.25;
    } else {
        for (int i = 0; i < 16; ++i)
            if ((i & 3) != 3) {
                vec2 off = spin * kSpiral[i] * texel2;
                sum += texture(uShadowMaps, vec4(proj.xy + off, float(layer), proj.z + dot(dz_duv, off) - bias));
            }
        k = sum / 16.0;
    }
    return mix(mix(floor_, 1.0, k), 1.0, edge);
}

// A sun's shadow, close up and far: its wide map reaches over the whole of
// what it lights and has texels the width of a hand, so a second, small one
// round the viewer takes over where they are looking at something near (a
// door, the leaf's edge, a chair's legs), the two blended before the near
// one's own edge begins to fade.
float shadow_cascade(int i, vec3 n, vec3 l, float floor_) {
    float wide = shadow_factor(shadow_layer(i, vLit), n, l, floor_);
    if (uLightNear[i] < -0.5) return wide;
    int near = int(uLightNear[i] + 0.5);
    vec4 light_space = uShadowVP[near] * vec4(vLit, 1.0);
    vec3 proj = light_space.xyz / max(light_space.w, 1e-5) * 0.5 + 0.5;
    float edge = smoothstep(0.55, 0.8, max(abs(proj.x * 2.0 - 1.0), abs(proj.y * 2.0 - 1.0)));
    if (edge >= 1.0 || proj.z > 1.0) return wide;
    return mix(shadow_factor(near, n, l, floor_), wide, edge);
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
    if (mSurface < 10.5) {
        // Planks: long boards, staggered, each its own shade, with grain.
        vec2 q = p * vec2(0.6, 6.0);
        float row = floor(q.y);
        q.x += hash(vec2(row, 3.0)) * 7.0;
        vec2 cell = fract(q);
        float seam = smoothstep(0.0, 0.04, min(cell.y, 1.0 - cell.y)) * smoothstep(0.0, 0.01, min(cell.x, 1.0 - cell.x));
        float shade = 0.78 + 0.3 * hash(floor(q));
        float grain = noise(vec2(q.x * 30.0, q.y * 3.0));
        rough_mod = -0.1;
        return mAlbedo * shade * (0.85 + 0.2 * grain) * mix(0.5, 1.0, seam);
    }
    if (mSurface < 11.5) {
        // Concrete: mottled, with pour lines.
        float m = noise(p * 1.3) * 0.6 + noise(p * 7.0) * 0.3 + noise(p * 40.0) * 0.1;
        float lines = smoothstep(0.0, 0.02, abs(fract(p.y * 0.8) - 0.5) - 0.48);
        rough_mod = 0.15;
        return mAlbedo * (0.8 + 0.3 * m) * (1.0 - 0.08 * lines);
    }
    if (mSurface < 12.5) {
        // Checker: squares of half a metre, two tones.
        vec2 c = floor(p * 2.0);
        float k = mod(c.x + c.y, 2.0);
        return mAlbedo * mix(0.35, 1.0, k) * (0.96 + 0.06 * noise(p * 9.0));
    }
    if (mSurface < 13.5) {
        // Brick: running bond, mortar between.
        vec2 q = p * vec2(4.0, 12.0);
        q.x += mod(floor(q.y), 2.0) * 0.5;
        vec2 cell = fract(q);
        float mortar = smoothstep(0.0, 0.06, min(cell.y, 1.0 - cell.y)) * smoothstep(0.0, 0.03, min(cell.x, 1.0 - cell.x));
        float shade = 0.75 + 0.35 * hash(floor(q));
        rough_mod = 0.1;
        return mix(vec3(0.62, 0.6, 0.56), mAlbedo * shade * (0.9 + 0.15 * noise(p * 20.0)), mortar);
    }
    if (mSurface < 14.5) {
        // Carpet: a dense short pile.
        float pile = noise(p * 90.0) * 0.5 + noise(p * 23.0) * 0.5;
        rough_mod = 0.3;
        return mAlbedo * (0.82 + 0.3 * pile);
    }
    if (mSurface < 15.5) {
        // Metal plate: panels with seams and a diamond tread.
        vec2 cell = fract(p * 0.8);
        float seam = smoothstep(0.0, 0.015, min(min(cell.x, 1.0 - cell.x), min(cell.y, 1.0 - cell.y)));
        vec2 d = fract(vec2(p.x + p.y, p.x - p.y) * 12.0);
        float tread = smoothstep(0.35, 0.5, 1.0 - abs(d.x - 0.5) * 2.0) * 0.12;
        rough_mod = -0.25;
        return mAlbedo * (0.85 + tread + 0.08 * noise(vec2(p.x * 300.0, p.y * 4.0))) * mix(0.45, 1.0, seam);
    }
    if (mSurface > 16.5 && mSurface < 17.5) {
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
        vec3 veins = mAlbedo * vec3(0.78, 0.58, 0.70);
        vec3 stone = mAlbedo * (0.96 + 0.05 * hash(slab));
        return mix(mix(stone, veins, vein * 0.55 + fine * 0.25), mAlbedo * 0.8, 1.0 - seam);
    }
    if (mSurface > 19.5 && mSurface < 20.5) {
        // Earth: clods and crumbs, darker where it lies in hollows, a few stones.
        float m = noise(p * 0.6) * 0.45 + noise(p * 3.3) * 0.3 + noise(p * 17.0) * 0.25;
        float stones = smoothstep(0.78, 0.86, noise(p * 9.0 + 3.7)) * 0.25;
        rough_mod = 0.1;
        return mAlbedo * (0.72 + 0.5 * m) * (1.0 + stones * 1.6);
    }
    if (mSurface > 20.5 && mSurface < 21.5) {
        // Asphalt: fine aggregate, patched, cracked; and a road's lines.
        float agg = noise(p * 60.0) * 0.5 + noise(p * 13.0) * 0.3 + noise(p * 1.1) * 0.2;
        float mend = smoothstep(0.62, 0.66, noise(p * 0.18 + 5.0)) * 0.08;
        float crack = 1.0 - smoothstep(0.0, 0.025, abs(noise(p * 0.7 + 11.0) - 0.5)) * smoothstep(0.55, 0.7, noise(p * 0.25));
        vec3 c = mAlbedo * (0.82 + 0.35 * agg) * (1.0 - mend) * (1.0 - 0.35 * crack);
        if (uLines > 0.5) {
            float u = vUV.x, v = vUV.y;
            float aa = fwidth(u) * 1.5;
            float centre = (1.0 - smoothstep(0.012, 0.012 + aa, abs(u - 0.5))) * step(fract(v / 9.0), 0.45);
            float edges = (1.0 - smoothstep(0.01, 0.01 + aa, abs(u - 0.04))) + (1.0 - smoothstep(0.01, 0.01 + aa, abs(u - 0.96)));
            float worn = smoothstep(0.25, 0.6, noise(vec2(u * 30.0, v * 0.8)));
            c = mix(c, vec3(0.72, 0.70, 0.62), clamp(centre + edges, 0.0, 1.0) * worn);
        }
        rough_mod = 0.15;
        return c;
    }
    if (mSurface > 21.5 && mSurface < 22.5) {
        // Rock: weathered stone seen from every side (three ways, blended by
        // the way it faces), banded, lichened in the flat.
        vec3 n = abs(normalize(vNormal));
        n = n / (n.x + n.y + n.z);
        vec3 q = vRoom;
        float a = noise(q.zy * 0.7) * 0.5 + noise(q.zy * 3.1) * 0.3 + noise(q.zy * 13.0) * 0.2;
        float b = noise(q.xz * 0.7) * 0.5 + noise(q.xz * 3.1) * 0.3 + noise(q.xz * 13.0) * 0.2;
        float c = noise(q.xy * 0.7) * 0.5 + noise(q.xy * 3.1) * 0.3 + noise(q.xy * 13.0) * 0.2;
        float m = a * n.x + b * n.y + c * n.z;
        float bands = 0.5 + 0.5 * sin(q.y * 1.7 + m * 3.0);
        float lichen = smoothstep(0.6, 0.75, noise(q.xz * 1.3)) * n.y;
        rough_mod = 0.05;
        return mix(mAlbedo * (0.68 + 0.45 * m) * (0.9 + 0.12 * bands), mAlbedo * vec3(0.8, 0.9, 0.6), lichen * 0.4);
    }
    if (mSurface > 22.5 && mSurface < 23.5) {
        // Snow: soft drifts, a little blue in their hollows, a glint.
        float d = noise(p * 0.4) * 0.6 + noise(p * 2.7) * 0.4;
        float glint = step(0.985, hash(floor(p * 40.0))) * 0.3;
        rough_mod = -0.1;
        return mAlbedo * mix(vec3(0.86, 0.9, 1.0), vec3(1.0), d) + glint;
    }
    if (mSurface > 17.5 && mSurface < 18.5) {
        // Water: its colour; the waves are in its normal (main). Over land
        // that says how deep it is, shallow at the shore and dark where deep,
        // with a pale lap of foam at the edge.
        rough_mod = -0.2;
        if (uSplat > 1.5) {
            float d = texture(uTex, vUV).r * uWaterDeepest;
            vec3 c = mix(mAlbedo, uWaterDeep, 1.0 - exp(-d * 0.5));
            float lap = (1.0 - smoothstep(0.0, 0.05, d)) * smoothstep(0.45, 0.75, noise(p * 2.5 + vec2(uTime * 0.25, 0.0)));
            return mix(c, mAlbedo * 2.2 + 0.04, lap * 0.35);
        }
        return mAlbedo * (0.9 + 0.1 * noise(p * 0.3 + uTime * 0.05));
    }
    // Grass: clumps of green and dry.
    float c = noise(p * 3.0) * 0.5 + noise(p * 17.0) * 0.3 + noise(p * 80.0) * 0.2;
    rough_mod = 0.3;
    return mAlbedo * mix(vec3(0.75, 0.8, 0.5), vec3(1.1, 1.15, 0.9), c);
}

vec3 surface_albedo(out float rough_mod);

vec3 ground_albedo(out float rough_mod) {
    vec2 p = vRoom.xz;
    vec4 w = texture(uTex, (p - uSplatRect.xy) * uSplatRect.zw);
    vec4 jag = vec4(noise(p * 0.8), noise(p * 0.8 + 17.0), noise(p * 0.8 + 31.0), noise(p * 0.8 + 53.0)) * 0.6
             + vec4(noise(p * 4.3), noise(p * 4.3 + 7.0), noise(p * 4.3 + 13.0), noise(p * 4.3 + 29.0)) * 0.4;
    w = w * (0.4 + 1.2 * jag);
    w = w * w * w;
    w /= max(w.x + w.y + w.z + w.w, 1e-4);
    vec3 keep = mAlbedo;
    vec3 sum = vec3(0.0);
    float rough = 0.0, most = -1.0, kept = 19.0;
    for (int i = 0; i < 4; ++i) {
        if (w[i] < 0.01) continue;
        mSurface = uLayerSurface[i];
        mAlbedo = uLayerColor[i];
        float rm;
        sum += surface_albedo(rm) * w[i];
        rough += rm * w[i];
        if (w[i] > most) most = w[i], kept = uLayerSurface[i];
    }
    mSurface = kept > 17.5 && kept < 18.5 ? 19.0 : kept;
    mAlbedo = keep;
    rough_mod = rough;
    return sum;
}

vec3 surface_albedo(out float rough_mod) {
    rough_mod = 0.0;
    if (mSurface < 0.5) return mAlbedo;
    if (mSurface > 9.5) return room_material(rough_mod);

    if (mSurface < 1.5) {
        // Floor: large tiles with grout and a little grain.
        vec2 t = vRoom.xz * 0.5;
        vec2 cell = fract(t);
        float grout = smoothstep(0.0, 0.035, min(cell.x, cell.y)) *
                      smoothstep(0.0, 0.035, min(1.0 - cell.x, 1.0 - cell.y));
        float shade = mix(0.55, 1.0, hash(floor(t)) * 0.35 + 0.65);
        vec3 tile = mAlbedo * shade * mix(0.45, 1.0, grout);
        rough_mod = mix(-0.25, 0.05, grout);          // grout is rougher than tile
        return tile * (0.94 + 0.12 * noise(vRoom.xz * 8.0));
    }
    if (mSurface < 2.5) {
        // Walls: plaster, with a subtle vertical gradient.
        float grain = 0.92 + 0.16 * noise(vRoom.xz * 6.0 + vRoom.y * 3.0);
        float height = clamp(vRoom.y / 4.0, 0.0, 1.0);
        return mAlbedo * grain * mix(0.82, 1.06, height);
    }
    if (mSurface > 7.5) {
        // Sand, rippled by the wind, giving way to banded rock where the
        // ground is steep. The ripples fade out before they would alias.
        vec3 n = normalize(vNormal);
        vec2 p = vRoom.xz;
        float arg = dot(p, vec2(0.8, 0.6)) * 21.0 + noise(p * 0.35) * 6.0;
        float fade = clamp(1.5 - fwidth(arg) * 0.6, 0.0, 1.0);
        float ripple = 0.5 + 0.5 * sin(arg);
        float grain = noise(p * 37.0);
        vec3 sand = mAlbedo * (0.93 + 0.09 * (ripple - 0.5) * fade + 0.07 * (grain - 0.5) * fade)
                  * (0.9 + 0.2 * noise(p * 0.04));
        float strata = 0.5 + 0.5 * sin(vRoom.y * 2.6 + noise(p * 0.15) * 2.5);
        vec3 rock = vec3(0.50, 0.30, 0.20) * (0.72 + 0.32 * strata) * (0.85 + 0.2 * noise(p * 2.0 + vRoom.y));
        // Wind: pale veils of sand streaming over the ground, downwind.
        vec2 w = p * vec2(0.35, 1.2) + vec2(uTime * 1.7, uTime * 0.3);
        float drift = smoothstep(0.55, 0.9, noise(w) * 0.7 + noise(w * 3.1 + 7.0) * 0.3) * uWind;
        sand = mix(sand, mAlbedo * 1.12 + 0.02, drift * 0.35 * fade);
        float steep = smoothstep(0.3, 0.5, 1.0 - n.y);
        rough_mod = 0.25;
        return mix(sand, rock, steep);
    }
    vec3 a = abs(vLocal);
    float edge = max(max(a.x, a.y), a.z);
    if (mSurface > 3.5 && mSurface < 4.5) {
        // Wood: long grain along the thing's own x and z, rings, a softened edge.
        float along = abs(dot(normalize(vObjNormal), vec3(0.0, 1.0, 0.0))) > 0.5 ? vObject.z : vObject.y;
        float grain = noise(vec2(vObject.x * 2.0 + vObject.z * 2.0, along * 55.0));
        float rings = 0.5 + 0.5 * sin((vObject.x + vObject.z) * 9.0 + grain * 6.0);
        rough_mod = -0.05 * rings;
        return mAlbedo * (0.82 + 0.14 * rings + 0.1 * grain) * mix(1.0, 0.8, smoothstep(0.46, 0.5, edge));
    }
    if (mSurface > 4.5 && mSurface < 5.5) {
        // Brushed metal: fine streaks, and smoother than its roughness says.
        float brush = noise(vec2(vObject.x * 400.0, vObject.y * 6.0 + vObject.z * 6.0));
        rough_mod = -0.2 + 0.1 * brush;
        return mAlbedo * (0.9 + 0.12 * brush);
    }
    if (mSurface > 5.5 && mSurface < 6.5) {
        // Moulded plastic: a faint speckle and rounded, darker edges.
        float speck = noise(vObject.xz * 180.0 + vObject.y * 90.0);
        return mAlbedo * (0.96 + 0.06 * speck) * mix(1.0, 0.78, smoothstep(0.45, 0.5, edge));
    }
    if (mSurface > 6.5) {
        // Fabric: a weave, matte - fine, a millimetre or so on a lamp's
        // shade, a few on a seat (it goes with the thing, carried or not);
        // finer on the screen than a few pixels, averaged away, as cloth is
        // seen across a room - sampled coarser than it is woven, it was a
        // lattice of diagonal stripes.
        vec2 p = (vObject.xz + vObject.yy) * 1500.0;
        float fine = max(length(fwidth(p.x)), length(fwidth(p.y)));
        float weave = 0.5 + 0.25 * (sin(p.x) + sin(p.y)) * (1.0 - smoothstep(0.6, 1.6, fine));
        rough_mod = 0.2;
        return mAlbedo * (0.85 + 0.2 * weave) * mix(1.0, 0.85, smoothstep(0.46, 0.5, edge));
    }
    // Crates: planks plus a darker bevel near the edges of the cube.
    float bevel = smoothstep(0.42, 0.5, edge);
    float planks = 0.88 + 0.12 * sin(vLocal.y * 42.0 + hash(vLocal.xz) * 3.0);
    return mAlbedo * planks * mix(1.0, 0.55, bevel);
}

void main() {
    vLit = vWorld + uLatticeShift;
    vLitDx = dFdx(vWorld), vLitDy = dFdy(vWorld);  // (uLatticeShift is the same at every pixel)
    if (vInstanced > 0.5) {
        mAlbedo = vMat0.rgb, mRoughness = vMat0.a;
        mSurface = vMat1.x, mEmissive = vMat1.y, mHighlight = vMat1.z, mMirror = vMat1.w;
    } else {
        mAlbedo = uAlbedo, mRoughness = uRoughness;
        mSurface = uSurface, mEmissive = uEmissive, mHighlight = uHighlight, mMirror = uMirror;
    }
    if (mSurface > 8.5 && mSurface < 9.5) {
        // The sky is not lit and not fogged: it is what the fog fades into.
        // (The lamps' light in the air before it is: air_light.) But where
        // the air takes all (uFogFull), the furthest ground is the air's own
        // colour - so the sky is too, at the horizon and below it: no seam
        // where the last of the ground meets it.
        vec3 d = normalize(vWorld - uViewPos);
        vec3 s = mix(sky(d), uFogColor, uFogFull * (1.0 - smoothstep(0.0, 0.35, d.y)));
        FragColor = vec4((s + air_light(1e9)) * (1.0 - uDim), 0.0);
        return;
    }
    float rough_mod;
    vec3 albedo = mSurface > 18.5 && mSurface < 19.5 && uSplat > 0.5 && uSplat < 1.5 ? ground_albedo(rough_mod) : surface_albedo(rough_mod);
    float relief_h = -1.0;  // how high a skin's paint stands here, if it says
    if (uTexMix > 0.0) {
        vec4 seen_at = uSeenFromVP * vec4(vWorld, 1.0);
        vec2 on_screen = uSeenFrom > 0.5 ? seen_at.xy / seen_at.w * 0.5 + 0.5 : gl_FragCoord.xy / uViewport;
        vec2 uv = uScreenUV > 0.5 ? (on_screen - uScreenRect.xy) / max(uScreenRect.zw - uScreenRect.xy, vec2(1e-4))
                : uSkin > 1.5 ? world_uv() : uSkin > 0.5 ? skin_uv() : vUV;
        if (uTexFlip > 0.5) uv.y = 1.0 - uv.y;
        if (uUVRect.z > 0.0) uv = uUVRect.xy + uv * uUVRect.zw;
        vec4 texel = uSkin > 0.5 && uSkin < 1.5 && uSkinFramed > 0.5 ? skin_texel() : texture(uTex, uv);
#ifdef SG_CUTOUT
        if (uCutout > 0.5 && texel.a < 0.5) discard;
#endif
        vec3 tex = uCRT > 0.0 ? crt_sample(uv) : texel.rgb;
        if (uSkinRelief > 0.0) relief_h = texel.a * uSkinRelief;
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
        vec3 seen = albedo;
        // Glass: what is round it, mirrored in it - little where it faces
        // the eye, all but everything at its rim - over what is seen through.
        if (uEnvMix > 0.0) {
            vec3 gn = normalize(vNormal), gv = normalize(uViewPos - vWorld);
            float fres = 0.04 + 0.96 * pow(1.0 - clamp(dot(gn, gv), 0.0, 1.0), 5.0);
            seen = mix(seen, texture(uEnv, reflect(-gv, gn)).rgb, clamp(fres * uEnvMix, 0.0, 1.0));
        }
        // A window's glass: the room seen in it, as much as glass reflects
        // from where it is looked at (4% face on, all but everything edge on).
        if (uGlassMirror > 0.5) {
            int k = int(uGlassMirror + 0.5) - 1;
            vec3 gn = normalize(vNormal), gv = normalize(uViewPos - vWorld);
            float fres = 0.04 + 0.96 * pow(1.0 - abs(dot(gn, gv)), 5.0);
            vec4 s = uMirrorSeen[k];
            vec2 ndc = gl_FragCoord.xy / uMirrorScreen * 2.0 - 1.0;
            vec2 t = vec2((s.z - ndc.x) / max(s.z - s.x, 1e-4), (ndc.y - s.y) / max(s.w - s.y, 1e-4));
            t = clamp(t, vec2(0.0), vec2(1.0)) * uMirrorUse[k].yz;
            vec3 room = k == 0 ? textureLod(uMirror0, t, 0.0).rgb : textureLod(uMirror1, t, 0.0).rgb;
            seen = mix(seen, room, clamp(fres * uMirrorUse[k].x, 0.0, 1.0));
        }
        // This side's air up to the opening is this side's to light.
        seen += air_light(length(vWorld - uViewPos));
        FragColor = vec4(seen * (1.0 - uDim * (1.0 - uUndim)), 0.0);
        return;
    }
    float roughness = clamp(mRoughness + rough_mod, 0.05, 1.0);
    // Metal (the brushed surface) reflects in its own colour and scatters
    // less. Only partly: most of what wears it is painted, and a bare metal
    // with only the sky and the floor to reflect would go dark. Everything
    // else reflects 4% head on, white.
    float metal = (mSurface > 4.5 && mSurface < 5.5) ? 0.35 : 0.0;
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 diffuse = albedo * (1.0 - metal);
    if (uSurfaceOnly > 0) {
        if (uSurfaceOnly == 1) FragColor = vec4(vLit, 1.0);
        else if (uSurfaceOnly == 2) FragColor = vec4(normalize(vNormal), 1.0);
        else if (uSurfaceOnly == 3) FragColor = vec4(diffuse, 1.0);
        else FragColor = vec4(length(vWorld - uViewPos), 0.0, 0.0, 1.0);
        return;
    }

    vec3 n = normalize(vNormal);
    vec3 v = normalize(uViewPos - vWorld);
    bool bent = false;  // whether relief or waves turned the normal from the surface's own
    if (relief_h >= 0.0) {
        // Bent by the paint's relief, from how its height changes across the
        // screen (Mikkelsen's surface gradient): no tangents, any projection.
        vec3 dpx = dFdx(vWorld), dpy = dFdy(vWorld);
        vec3 r1 = cross(dpy, n), r2 = cross(n, dpx);
        float det = dot(dpx, r1);
        vec3 grad = sign(det) * (dFdx(relief_h) * r1 + dFdy(relief_h) * r2);
        n = normalize(abs(det) * n - grad);
        bent = true;
    }
    if (mSurface > 17.5 && mSurface < 18.5 && n.y > 0.5) {
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
        slope *= 1.0 - uCalm;
        n = normalize(n - vec3(slope.x, 0.0, slope.y));
        bent = true;
    }
    float ndv = clamp(dot(n, v), 1e-3, 1.0);
    float a2 = roughness * roughness * roughness * roughness;
    // Specular antialiasing (Kaplanyan & Hill): where the normal turns fast
    // across a pixel - a rounded edge thinner than a pixel - the highlight is
    // widened by as much, or it flickers from pixel to pixel and steps.
    vec3 dndx = dFdx(n), dndy = dFdy(n);
    float variance = 0.25 * (dot(dndx, dndx) + dot(dndy, dndy));
    a2 = clamp(a2 + min(2.0 * variance, 0.25), 0.0, 1.0);

    // How much a surface reflects of light from all round, by angle and
    // roughness (env_brdf) - wanted before the lamps as well as after: one
    // bounce of the microfacets is all the GGX lobe counts, and a rough
    // surface loses the rest, so a rough metal goes darker than it is. What
    // is lost is given back in the reflection's own colour (Fdez-Aguera's
    // multiple scattering): 1 + f0 (1 / (a + b) - 1).
    vec2 ab = env_brdf(ndv, roughness);
    vec3 multi = 1.0 + f0 * (1.0 / max(ab.x + ab.y, 1e-3) - 1.0);
    // And a normal bent by relief or waves can face a reflection into the
    // surface itself, below its horizon: none of that is seen.
    vec3 r = reflect(-v, n);
    float horizon = 1.0;
    if (bent) {
        horizon = clamp(1.0 + dot(r, normalize(vNormal)), 0.0, 1.0);
        horizon *= horizon;
    }
    vec3 spec_k = multi * horizon;

    // The light the room's probes hold for a surface facing this way, and
    // how much they hold this point (0: the light from all round below).
    float probe_cover = 0.0;
    vec3 probe_given = uProbeCount > 0 ? probe_light(vLit, n, 2.0 / 3.0, 0.25, probe_cover) : vec3(0.0);

    vec3 direct = vec3(0.0), bounced = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        // Light let in through a doorway reaches a door's leaf, half in the
        // room beyond, as that room's own does: nothing this side's shadow
        // maps hold stands between them. And a bounce let in stands for that
        // room's light from all round, which the leaf has itself (below):
        // it is not lit by it twice.
        bool let_in = uStraddle > 0.5 && uLightGateAxis[i].w > 0.5;
        if (let_in && uLightIndirect[i] > 0.5) continue;
        vec3 l;
        float reach = light_reach(i, vLit, l);
        // A lamp that gives this point nothing - behind it, outside its cone,
        // too far - costs it nothing either.
        if (reach <= 1e-5 || dot(n, l) <= 0.0) continue;
        vec3 h = normalize(l + v);

        float ndl = max(dot(n, l), 0.0);
        float shadow = 1.0;
        // (Only where the lamp reaches: outside its cone it adds nothing,
        // and its shadow there is nothing.)
        if (ndl > 0.0 && i < uShadowCount && uLightLayer[i] > -0.5 && !let_in) {
            float fl = uLightFloor[i] < 0.0 ? uShadowFloor : uLightFloor[i];
            shadow = shadow_cascade(i, n, l, fl);
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
        vec3 lobe = diffuse * (1.0 - f) + d * vis * f * spec_k;

        // (Light standing in for bounce is the probes' where they hold: their
        // bake saw the bounce itself, and it would count twice.)
        if (uLightIndirect[i] > 0.5)
            bounced += diffuse * ndl * uLightColor[i] * reach * shadow * (1.0 - (uLightIndirect[i] > 0.8 ? probe_cover : 0.0));
        else direct += lobe * ndl * uLightColor[i] * reach * shadow;
    }

    // Light from all round: the sky's colour from above, the floor's bounce
    // from below. Scattered by the diffuse, and seen in the mirror direction
    // by the reflection - blurred towards the normal as the surface roughens,
    // and weighted by how much it reflects at this angle.
    vec3 reflected = (f0 * ab.x + ab.y) * multi;
    float up = mix(r.y, n.y, roughness * roughness);
    vec3 sky_here, ground_here;
    around_at(vLit, sky_here, ground_here);
    if (uStraddle > 0.5) {
        // A door's leaf is lit by the lamps of both rooms, each face by those
        // before it (n.l says which); the light from all round is that of the
        // room the face looks into. The doorway it hangs in: the nearest.
        int k = -1;
        float best = 1e9;
        for (int i = 0; i < MAX_DOORS; ++i) {
            if (i >= uDoorCount) break;
            vec3 d = vLit - uDoorAt[i].xyz;
            float score = abs(dot(d.xz, uDoorIn[i].xy)) + max(abs(dot(d.xz, uDoorAxis[i].xy)) - uDoorAt[i].w, 0.0) * 4.0;
            if (score < best) best = score, k = i;
        }
        if (k >= 0) {
            float here = smoothstep(-0.3, 0.3, dot(n.xz, uDoorIn[k].xy));
            sky_here = mix(uDoorSky[k], sky_here, here);
            ground_here = mix(uDoorGround[k], ground_here, here);
        }
    }
    vec3 around = mix(ground_here, sky_here, n.y * 0.5 + 0.5);
    vec3 mirrored = mix(ground_here, sky_here, smoothstep(-0.35, 0.35, up));
    if (probe_cover > 0.0) {
        // Where the probes hold, the light from all round is the look's and
        // the lamps' come back off the room, as the probes saw it: given to
        // the diffuse as a surface facing this way takes it, and seen in the
        // mirror direction - blurred toward the normal, its finer bands
        // fading, as the surface roughens.
        float rr = roughness * roughness, unused;
        vec3 m = probe_light(vLit, normalize(mix(r, n, rr)), mix(1.0, 2.0 / 3.0, rr), mix(1.0, 0.25, rr), unused);
        around += probe_given * probe_cover;
        mirrored += m * probe_cover;
    }
    // Under an open sky a glossy surface reflects the sky itself - its
    // colours, its clouds, the sun's glint - as rougher surfaces cannot.
    if (mMirror > 0.0) mirrored = mix(mirrored, sky(normalize(vec3(r.x, abs(r.y), r.z))), mMirror * (1.0 - roughness));
    // Lying in a plane the room is seen in: what is seen there, turned left
    // for right as a mirror turns it, bent where the surface is (relief,
    // waves), and blurred as it roughens.
    for (int k = 0; k < 2; ++k) {
        if (k >= uMirrorCount) break;
        vec4 plane = uMirrorPlane[k];
        if (abs(dot(plane.xyz, vLit) + plane.w) > 0.01 || dot(normalize(vNormal), plane.xyz) < 0.9) continue;
        vec4 s = uMirrorSeen[k];
        vec2 ndc = gl_FragCoord.xy / uMirrorScreen * 2.0 - 1.0;
        vec2 t = vec2((s.z - ndc.x) / max(s.z - s.x, 1e-4), (ndc.y - s.y) / max(s.w - s.y, 1e-4));
        vec3 side = n - plane.xyz * dot(n, plane.xyz);
        t += vec2(-side.x, side.z) * 0.08;
        t = clamp(t, vec2(0.0), vec2(1.0)) * uMirrorUse[k].yz;
        float lod = roughness * roughness * uMirrorUse[k].w * 0.6;
        vec3 seen = k == 0 ? textureLod(uMirror0, t, lod).rgb : textureLod(uMirror1, t, lod).rgb;
        mirrored = seen;
        // How much of what is before it it says is seen in it, face on as
        // edge on (0.2 or so a polished floor, 1 a mirror) - not only edge on
        // as bare Fresnel gives - and less as it roughens.
        reflected = max(reflected, vec3(uMirrorUse[k].x * (1.0 - roughness)));
    }
    vec3 ambient = diffuse * around * (1.0 - reflected) + mirrored * reflected * horizon + bounced;

    vec3 color = ambient + direct + albedo * (mEmissive + uGlow) * (1.0 - uDark);
    // How much of what is seen here is light from all round - the only part
    // occlusion takes away (ao_apply_fs): a corner in lamplight stays lit.
    const vec3 lum = vec3(0.2126, 0.7152, 0.0722);
    float indirect = clamp(dot(ambient, lum) / max(dot(color, lum), 1e-5), 0.0, 1.0);
    color = mix(color, vec3(1.0, 0.86, 0.45) * (0.3 + 0.7 * length(color)), mHighlight * 0.35);

    // Fog, brighter where it is looked at towards the sun: light scattered
    // on its way through the air.
    // (From `uFogStart` on: the air near the eye clear. Seen through a
    // doorway the eye is carried, so the distance is the whole way there.)
    // Seen through a doorway from another world, the way here is that
    // world's air up to the doorway (`uDoorPlane`) and this one's beyond:
    // the far stretch fogged by this air, then the near one by the host's.
    // (Through a doorway onto its own world the two are one air: the same.)
    vec3 to_frag = vWorld - uViewPos;
    float len = length(to_frag), t = 0.0;
    if (uHostFog.w > 0.5) {
        float dn = dot(uDoorPlane.xyz, to_frag / max(len, 1e-5));
        if (abs(dn) > 1e-5) t = clamp(-(dot(uDoorPlane.xyz, uViewPos) + uDoorPlane.w) / dn, 0.0, len);
    }
    // (This side's air from the doorway on, eased in where it begins.)
    float fog = 1.0 - exp(-uFogDensity * max(air_past(len, uFogStart) - air_past(t, uFogStart), 0.0));
    float toward = pow(max(dot(normalize(to_frag), normalize(uSunDir + vec3(0.0, 1e-4, 0.0))), 0.0), 6.0);
    // Air that takes all at last is the background too: as far as it goes,
    // what is seen is what is beyond the last thing drawn.
    vec3 haze = uFogColor + uSunColor * toward * 0.25 * (1.0 - uFogFull);
    fog = clamp(fog, 0.0, mix(0.85, 1.0, uFogFull));
    color = mix(color, haze, fog);
    // And the light of this world's lamps that its air scatters towards the
    // eye on the way (air_light: none unless its look says the air scatters).
    color += air_light(len);
    if (uHostFog.w > 0.5) color = mix(color, uHostFogColor, clamp(1.0 - exp(-uHostFog.x * max(t - uHostFog.y, 0.0)), 0.0, mix(0.85, 1.0, uHostFog.z)));

    // The eye adjusted to a screen: the room around it dims, the picture
    // on the screen does not. Alpha: the share of it occlusion may darken.
    // Glass: as much of it drawn over what is behind as it is not clear,
    // and more where it is seen edge on (Schlick's Fresnel) - its alpha, as
    // the glass pass blends it (the alpha behind it kept).
    if (uGlass > 0.0) {
        float facing = abs(dot(n, normalize(-to_frag)));
        float fresnel = 0.04 + 0.96 * pow(1.0 - facing, 5.0);
        FragColor = vec4(color * (uCRT > 0.0 ? 1.0 : (1.0 - uDim)), clamp(mix(1.0 - uGlass, 1.0, fresnel), 0.0, 1.0));
        return;
    }
    FragColor = vec4(color * (uCRT > 0.0 ? 1.0 : (1.0 - uDim)), indirect * (1.0 - fog));
})";
    return source.c_str();
}

const char* fog_bloom_glsl() {
    return R"(
uniform sampler2D uDepth;
uniform vec4  uDepthView;
uniform vec2  uAirThick;
// How much of the way to `d` is in the air that begins `start` from the
// eye: eased in over half `start` either side of where it begins, so the
// place it begins draws no line (on a ceiling, a ring round the eye).
float air_past(float d, float start) {
    float w = 0.5 * start;
    float x = d - start;
    if (w <= 0.0) return max(x, 0.0);
    return x >= w ? x : (x <= -w ? 0.0 : (x + w) * (x + w) / (4.0 * w));
}
uniform float uFogBloom;
uniform float uFogBloomCap;
float fog_bloom(vec2 uv) {
    if (uFogBloom <= 0.0) return 0.0;
    float d = texture(uDepth, uv).r;
    float n = uDepthView.x, f = uDepthView.y;
    // Along the ray, not the view's axis; what nothing stands in front of
    // is as far as the view goes.
    float z = d >= 1.0 ? f : 2.0 * n * f / (f + n - (d * 2.0 - 1.0) * (f - n));
    vec2 p = (uv * 2.0 - 1.0) * vec2(uDepthView.z * uDepthView.w, uDepthView.z);
    float tau = uAirThick.x * air_past(z * length(vec3(p, 1.0)), uAirThick.y);
    return uFogBloom * min(tau, uFogBloomCap > 0.0 ? uFogBloomCap : 3.0);
}
)";
}

const char* exposure_meter_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform vec2 uCell;  // one pixel of the meter, in the scene's 0..1
void main() {
    // Four looks about the cell, so a lamp or a dark seam between two
    // samples is not missed - each as bright as its light, not its colour.
    float sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = (vec2(i & 1, i >> 1) - 0.5) * 0.5 * uCell;
        vec3 c = texture(uScene, vUV + o).rgb;
        sum += log2(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1.0 / 16384.0));
    }
    // The middle of the view counts most, as the eye looks at it; the edges
    // still a quarter as much, so a lamp at the edge is not all ignored.
    vec2 d = vUV - 0.5;
    float w = 0.25 + exp(-dot(d, d) / (2.0 * 0.2 * 0.2));
    FragColor = vec4(w * sum * 0.25, w, 0.0, 1.0);
})";
}

const char* composite_fs() {
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
)") + film_glsl() + godrays_glsl() + fxaa_glsl() + fog_bloom_glsl() + R"(

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
    vec3 scene = texture(uScene, vUV).rgb;
    scene += texture(uBloom, vUV).rgb * (uBloomStrength + fog_bloom(vUV));
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

const char* pixel_composite_fs() {
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
uniform float uTime;
uniform float uPixels;    // rows of pixels in the picture (0: as many as the screen has)
uniform float uLevels;    // steps of each colour channel (0: as many as the screen has)
uniform float uDither;    // how much the ordered dither mixes the steps, 0..1
uniform float uContrast;  // 1 as it is
uniform float uFringe;    // pixels of colour fringe at the frame's edge
)") + film_glsl() + R"(
float bayer(vec2 p) {
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 i = ivec2(mod(p, 4.0));
    return (m[i.y * 4 + i.x] + 0.5) / 16.0;
}

vec3 seen(vec2 uv) {
    return texture(uScene, uv).rgb + texture(uBloom, uv).rgb * uBloomStrength;
}

void main() {
    vec2 res = 1.0 / uTexel;
    float size = uPixels > 0.0 ? max(1.0, res.y / uPixels) : 1.0;
    vec2 cell = floor(vUV * res / size);
    vec2 uv = (cell + 0.5) * size / res;
    vec2 d = uv - 0.5;
    vec2 off = d * dot(d, d) * uFringe * size * uTexel * 4.0;
    vec3 hdr = vec3(seen(uv + off).r, seen(uv).g, seen(uv - off).b);
    vec3 c = tonemap(hdr * uExposure);
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, uSaturation) * uTint;
    c = clamp((c - 0.5) * uContrast + 0.5, 0.0, 1.0);
    c *= 1.0 - dot(d, d) * uVignette;
    c = film(c, 0.0, uTime);
    if (uLevels > 0.0) {
        float steps = uLevels - 1.0;
        c = floor(c * steps + mix(0.5, bayer(cell), uDither)) / steps;
    }
    FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
})";
    return source.c_str();
}

const char* deband_glsl() {
    return R"(
uniform float uDeband;
// Nothing drawn here: the depth the view was cleared to. (Read back from a
// 24-bit buffer the clear may come as a hair under 1; nothing drawn stands
// that far.)
bool deband_empty(vec2 uv) { return texture(uDepth, uv).r >= 0.9999999; }
// Only where nothing was drawn, and nothing drawn is within a pixel: an
// antialiased edge's pixel may say "empty" by its one depth while its colour
// holds some of what was drawn, so neither it nor any pixel beside a drawn
// one is changed, and none is taken into a ring. Each ring is turned from
// the last by the golden angle, so the taps lay no cross; a ring is taken
// only if its taps, and the pixel beyond each, are empty.
vec3 deband(vec2 uv, vec3 c) {
    if (uDeband <= 0.0 || !deband_empty(uv)) return c;
    if (!deband_empty(uv + vec2(uTexel.x, 0.0)) || !deband_empty(uv - vec2(uTexel.x, 0.0)) ||
        !deband_empty(uv + vec2(0.0, uTexel.y)) || !deband_empty(uv - vec2(0.0, uTexel.y))) return c;
    vec3 smooth_c = c;
    float r = 1.0, a = 0.0;
    for (int i = 0; i < 8; ++i) {
        vec2 d = vec2(cos(a), sin(a));       // a unit step, in pixels
        vec2 e = vec2(-d.y, d.x);
        vec2 o = d * r * uTexel, p = e * r * uTexel, o1 = d * (r + 1.0) * uTexel, p1 = e * (r + 1.0) * uTexel;
        if (!(deband_empty(uv + o) && deband_empty(uv - o) && deband_empty(uv + p) && deband_empty(uv - p) &&
              deband_empty(uv + o1) && deband_empty(uv - o1) && deband_empty(uv + p1) && deband_empty(uv - p1))) break;
        vec3 sum = texture(uFrame, uv + o).rgb + texture(uFrame, uv - o).rgb + texture(uFrame, uv + p).rgb + texture(uFrame, uv - p).rgb;
        smooth_c = (smooth_c + sum) / 5.0;
        r *= 1.5;
        a += 2.3999632;
    }
    // A band is a step of a level or two; more than that is a picture, and
    // is kept.
    return c + clamp(smooth_c - c, vec3(-2.0 / 255.0), vec3(2.0 / 255.0)) * min(uDeband, 1.0);
}
)";
}

const char* finish_fs() {
    static const std::string source = std::string(R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uFrame;
uniform sampler2D uDepth;
uniform sampler2D uHistory;
uniform vec2  uTexel;
uniform float uSmearKeep;   // how much of the last frame stays (0: none)
uniform float uSmearBlur;   // the blur's taps apart, in pixels
)") + deband_glsl() + R"(
// The last frame, blurred: 4, 2 and 1 over the centre, its edges and its
// corners, of sixteen.
vec3 blur9(vec2 uv) {
    vec2 t = uTexel * max(uSmearBlur, 1.0);
    vec3 s = texture(uHistory, uv).rgb * 4.0;
    s += (texture(uHistory, uv + vec2(t.x, 0.0)).rgb + texture(uHistory, uv - vec2(t.x, 0.0)).rgb +
          texture(uHistory, uv + vec2(0.0, t.y)).rgb + texture(uHistory, uv - vec2(0.0, t.y)).rgb) * 2.0;
    s += texture(uHistory, uv + t).rgb + texture(uHistory, uv - t).rgb +
         texture(uHistory, uv + vec2(t.x, -t.y)).rgb + texture(uHistory, uv + vec2(-t.x, t.y)).rgb;
    return s / 16.0;
}

void main() {
    // (The pixel itself, exactly: what deband leaves is the picture as composited.)
    vec3 now = deband(vUV, texelFetch(uFrame, ivec2(gl_FragCoord.xy), 0).rgb);
    if (uSmearKeep > 0.0) now = mix(now, blur9(vUV), uSmearKeep);
    FragColor = vec4(now, 1.0);
})";
    return source.c_str();
}

const char* present_fs() {
    return R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uFrame;
void main() {
    FragColor = vec4(texelFetch(uFrame, ivec2(gl_FragCoord.xy), 0).rgb, 1.0);
})";
}

}  // namespace sg::gl
