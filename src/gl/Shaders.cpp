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
const int MAX_LIGHTS = 32;
uniform int   uLightCount;
uniform vec3  uLightPos[MAX_LIGHTS];
// A light's numbers, four to a register (the scene shader's registers are
// few, and an array of single numbers takes one for each):
//   uLightA  x its power; y, z the cosines of its cone's inner and outer
//            edges; w 1 for a sun
//   uLightB  x the light left in its full shadow (< 0: the look's); y 1 lit
//            as bounce and standing in for it, 0.6 lit as bounce only; z its
//            falloff (0 soft, 1 the inverse square); w how much of its
//            opening is clear
//   uLightC  x how much of it the air scatters, times the look's `scatter`;
//            y how far it reaches (0: on for ever)
//   uLightD  x a sun's narrower maps round the eye (how many, the low three
//            bits, then each one's layer, five bits each, the narrowest
//            first; <= 0 none); y its first shadow layer (< 0: none); z 1 a
//            cube of maps; w its place among the frame's lamps (< 0:
//            everywhere)
uniform vec4  uLightA[MAX_LIGHTS];
uniform vec4  uLightB[MAX_LIGHTS];
uniform vec4  uLightC[MAX_LIGHTS];
uniform vec4  uLightD[MAX_LIGHTS];
uniform vec3  uLightDir[MAX_LIGHTS];   // pointing away from the lamp
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uStraddle;                  // 1: a door's leaf, half in each room, lit by each as far as it is in it
// Light from beyond a doorway, let in only through its opening: its middle
// and half its width, then which way across it is (x, z), half its height,
// and whether the light is gated at all.
uniform vec4  uLightGate[MAX_LIGHTS];
uniform vec4  uLightGateAxis[MAX_LIGHTS];
uniform vec4  uLightFrame[MAX_LIGHTS];    // a projector's: the tangents of its half-angles across and up, how soft its edge; 1 if framed


// Which lamps reach which part of the view (GLWorldView::lamp_cells): the
// view cut into cells across and up, and shells out from the eye, each
// saying - two words of bits, a bit a lamp of the frame's - which of them
// can light anything in it. A lamp whose light ends (its `range`) is left out
// where its range does not reach: it gives nothing there.
uniform usampler2D uLampCells;
uniform vec4  uLampCellGrid;              // cells across, up, shells; 1 when there are cells (0: every lamp everywhere)
uniform vec2  uLampShells;                // where the shells begin and end, from the eye
// The cell a point of the view (`uv`, 0..1 across it) at distance `d` is in.
uvec2 lamp_cell(vec2 uv, float d) {
    if (uLampCellGrid.w < 0.5) return uvec2(0xFFFFFFFFu);
    ivec2 cells = ivec2(uLampCellGrid.xy);
    ivec2 at = clamp(ivec2(floor(uv * uLampCellGrid.xy)), ivec2(0), cells - 1);
    int shells = int(uLampCellGrid.z);
    int s = d <= uLampShells.x ? 0 : clamp(int(floor(log(d / uLampShells.x) / log(uLampShells.y / uLampShells.x) * float(shells))), 0, shells - 1);
    return texelFetch(uLampCells, ivec2(at.x, at.y + s * cells.y), 0).xy;
}
bool lamp_in(uvec2 cell, int i) {
    int k = int(floor(uLightD[i].w + 0.5));
    if (k < 0) return true;
    uint word = k < 32 ? cell.x : cell.y;
    return ((word >> uint(k & 31)) & 1u) != 0u;
}

uniform int   uShadowCount;           // how many lights, from the first, have a shadow map
// The first `uShadowCount` lights each have depth maps, layers of one array
// (from `uLightD`'s y: one, six for a lamp with no cone, a sun's boxes): where
// each sees from (`uShadowVP`) - and from that, how wide a texel of it is.
const int MAX_SHADOWS = 24;
uniform sampler2DArrayShadow uShadowMaps;
uniform mat4 uShadowVP[MAX_SHADOWS];

// Which of light `i`'s maps sees `p`: its one, or - a lamp with no cone -
// the face of its cube the way from the lamp to `p` goes out through (the
// greatest of the three coordinates of that way: +x, -x, +y, -y, +z, -z).
// A sun's maps narrower than its own: how many, and the layer of the k-th.
int closer_maps(int i) { return uLightD[i].x < 0.5 ? 0 : int(uLightD[i].x + 0.5) & 7; }
int closer_map(int i, int k) { return (int(uLightD[i].x + 0.5) >> (3 + 5 * k)) & 31; }
// Where `p` is in a map: 0..1 across, up and deep.
vec3 in_map(int layer, vec3 p) {
    vec4 s = uShadowVP[layer] * vec4(p, 1.0);
    return s.xyz / max(s.w, 1e-5) * 0.5 + 0.5;
}
// How far into a sun's map's margin `p` is - 0 well within it, 1 where the
// next wider one has wholly taken over (or past its depth).
float map_margin(int layer, vec3 p) {
    vec3 q = in_map(layer, p);
    if (q.z > 1.0 || q.z < 0.0) return 1.0;
    return smoothstep(0.55, 0.8, max(abs(q.x * 2.0 - 1.0), abs(q.y * 2.0 - 1.0)));
}
int shadow_layer(int i, vec3 p) {
    int first = int(uLightD[i].y + 0.5);
    if (uLightD[i].z < 0.5) {
        // (A sun's: the narrowest that holds `p`.)
        int closer = closer_maps(i);
        for (int k = 0; k < 4; ++k) {
            if (k >= closer) break;
            if (map_margin(closer_map(i, k), p) < 1.0) return closer_map(i, k);
        }
        return first;
    }
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
    float opening = (1.0 - smoothstep(at.w - soft, at.w + soft, u)) * (1.0 - smoothstep(g.z - soft, g.z + soft, v)) * uLightB[i].w;
    if (!lamp_side) return opening;
    return opening * (1.0 - smoothstep(0.0, 0.3, abs(dot(p - at.xyz, n))));
}

// How much of a sun's light (from `l`) gets through the clouds over `p` (a
// look's `clouds.cover`): read off the map of their shadow on the ground
// round the eye (cloud_shadow_fs) where the way to the sun from `p` meets
// it. 1 where there are none.
uniform sampler2D uCloudShadowMap;
uniform vec4 uCloudShadowAt;  // the map's middle (x, z), half its width (metres); w 1 where there is one
float cloud_shadow(vec3 p, vec3 l) {
    if (uCloudShadowAt.w <= 0.0 || l.y <= 0.02) return 1.0;
    vec2 g = p.xz - l.xz * (p.y / l.y);
    return texture(uCloudShadowMap, (g - uCloudShadowAt.xy) / (2.0 * uCloudShadowAt.z) + 0.5).r;
}

// How much of a sun's light (from `l`) the land toward it leaves `p` (a
// terrain's horizon, horizon_fs): lit while the sun stands higher over the
// horizon than the land rises toward it there, soft across a little of it.
// What stands above the ground sees over more of the land: as high as two
// of the grid's cells, the horizon is let go of. 1 where there is none.
uniform sampler2D uHorizonMap;
uniform vec4 uHorizonAt;    // the grid's corner (x, z), 1 / its width (metres); w 1 where there is one
uniform vec2 uHorizonCell;  // a cell of it (metres), and how high its room stands in the view
float horizon_shadow(vec3 p, vec3 l) {
    if (uHorizonAt.w <= 0.0) return 1.0;
    vec2 uv = (p.xz - uHorizonAt.xy) * uHorizonAt.z;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return 1.0;
    vec2 h = texture(uHorizonMap, uv).rg;
    float up = l.y / max(length(l.xz), 1e-4);
    float lit = smoothstep(-0.03, 0.03, up - h.x);
    return mix(lit, 1.0, smoothstep(0.0, 2.0 * uHorizonCell.x, p.y - h.y - uHorizonCell.y));
}

// How much of light `i` reaches `p` - its power, as it falls off with the
// distance, its cone and its doorway - and which way it is (`l`, towards it).
float light_reach(int i, vec3 p, out vec3 l) {
    if (uLightA[i].w > 0.5) {
        l = normalize(-uLightDir[i]);
        // (Under the clouds, as much as gets through them; and the land
        // toward it, as much as it leaves.)
        return uLightA[i].x * through_gate(i, p, l, true) * cloud_shadow(p, l) * horizon_shadow(p, l);
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
        cone = clamp((theta - uLightA[i].z) / max(uLightA[i].y - uLightA[i].z, 1e-4), 0.0, 1.0);
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
    if (uLightC[i].y > 0.0) {
        float q = dist / uLightC[i].y;
        q *= q;
        window = clamp(1.0 - q * q, 0.0, 1.0);
        window *= window;
        if (window <= 0.0) return 0.0;
    }
    return uLightA[i].x * mix(soft, square, uLightB[i].z) * window * through_gate(i, p, toLight, false) * (cone * cone);
}
)";
    return source;
}

const char* capture_glsl() {
    return R"(
// A way out as a point of an octahedron's map unfolded (-1..1, up its
// middle, down its corners), and back.
vec2 oct_of(vec3 d) {
    d /= abs(d.x) + abs(d.y) + abs(d.z);
    vec2 p = d.xz;
    if (d.y < 0.0) p = (1.0 - abs(p.yx)) * vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
    return p;
}
vec3 oct_way(vec2 p) {
    vec3 d = vec3(p.x, 1.0 - abs(p.x) - abs(p.y), p.y);
    if (d.y < 0.0) d.xz = (1.0 - abs(d.zx)) * vec2(d.x >= 0.0 ? 1.0 : -1.0, d.z >= 0.0 ? 1.0 : -1.0);
    return normalize(d);
}
// A map `n` texels a side keeps a texel's border round it - what lies just
// past each edge, folded (a point past an edge is the point as far within
// it, mirrored along it) - so it is read between texels with no seam. The
// way out a texel (gl_FragCoord) is of; and where a way is read.
vec3 capture_way(vec2 px, float n) {
    vec2 p = (px - 1.0) / (n - 2.0) * 2.0 - 1.0;
    if (abs(p.x) > 1.0) p = vec2((p.x > 0.0 ? 2.0 : -2.0) - p.x, -p.y);
    if (abs(p.y) > 1.0) p = vec2(-p.x, (p.y > 0.0 ? 2.0 : -2.0) - p.y);
    return oct_way(p);
}
vec2 capture_uv(vec3 d, float n) {
    return (oct_of(d) * 0.5 + 0.5) * ((n - 2.0) / n) + 1.0 / n;
}
)";
}

const char* glint_down_fs() {
    return R"(#version 330 core
uniform sampler2D uSource;  // the size above, its only level shown
uniform vec2 uTexel;        // a texel of it
in vec2 vUV;
out vec4 FragColor;
void main() {
    vec4 sum = vec4(0.0);
    float weight = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = vec2(float(i & 1), float(i >> 1)) - 0.5;
        vec4 c = texture(uSource, vUV + o * uTexel);
        float w = 1.0 / (1.0 + dot(max(c.rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)));
        sum += c * w;
        weight += w;
    }
    FragColor = sum / max(weight, 1e-6);
}
)";
}

const char* capture_fold_fs() {
    static const std::string source = std::string(R"(#version 330 core
)") + capture_glsl() + R"(
uniform sampler2D uFaces;  // the six faces in a row (+x -x +y -y +z -z), each as a camera looking that way sees it
uniform float uSize;       // the map's texels a side, its border among them
out vec4 FragColor;
const vec3 kFaceF[6] = vec3[6](vec3(1, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), vec3(0, -1, 0), vec3(0, 0, 1), vec3(0, 0, -1));
const vec3 kFaceU[6] = vec3[6](vec3(0, 1, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0, 0, 1), vec3(0, 1, 0), vec3(0, 1, 0));
void main() {
    vec3 d = capture_way(gl_FragCoord.xy, uSize);
    vec3 a = abs(d);
    int f = a.x >= a.y && a.x >= a.z ? (d.x > 0.0 ? 0 : 1) : a.y >= a.z ? (d.y > 0.0 ? 2 : 3) : (d.z > 0.0 ? 4 : 5);
    vec3 r = normalize(cross(kFaceF[f], kFaceU[f]));
    float z = dot(d, kFaceF[f]);
    vec2 uv = vec2(dot(d, r), dot(d, kFaceU[f])) / z;
    // (Read within its own face: the next one along the row is another way.)
    vec2 size = vec2(textureSize(uFaces, 0));
    float face = size.y;
    vec2 at = clamp((uv * 0.5 + 0.5) * face, vec2(0.5), vec2(face - 0.5)) + vec2(float(f) * face, 0.0);
    FragColor = vec4(textureLod(uFaces, at / size, 0.0).rgb, 1.0);
}
)";
    return source.c_str();
}

const char* capture_blur_fs() {
    static const std::string source = std::string(R"(#version 330 core
)") + capture_glsl() + R"(
uniform sampler2D uSource;  // the map whole, its smaller sizes made as a picture's are
uniform float uSize;        // this size's texels a side
uniform float uRough;       // the roughness it is blurred for
out vec4 FragColor;
const float kPi = 3.14159265;
// The points of a Hammersley set, i of n.
vec2 hammersley(uint i, uint n) {
    uint b = i;
    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(n), float(b) * 2.3283064365386963e-10);
}
void main() {
    // Seen straight on (the way out, the normal and the eye one): the
    // microfacets' ways about it, each the light that way, read from as
    // small a size of the map as the way's share of the sphere is big - so a
    // lamp a few texels across is spread, not dotted (Karis 2013, Colbert
    // and Krivanek 2007).
    vec3 n = capture_way(gl_FragCoord.xy, uSize);
    vec3 t = normalize(cross(abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), n)), b = cross(n, t);
    float a = uRough * uRough, a2 = a * a;
    float source = float(textureSize(uSource, 0).x);
    float texel = 4.0 * kPi / (source * source);
    const uint kSamples = 96u;
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < kSamples; ++i) {
        vec2 u = hammersley(i, kSamples);
        float phi = 2.0 * kPi * u.x;
        float ch = sqrt((1.0 - u.y) / (1.0 + (a2 - 1.0) * u.y)), sh = sqrt(1.0 - ch * ch);
        vec3 h = t * (cos(phi) * sh) + b * (sin(phi) * sh) + n * ch;
        vec3 l = 2.0 * dot(n, h) * h - n;
        float ndl = dot(n, l);
        if (ndl <= 0.0) continue;
        float q = ch * ch * (a2 - 1.0) + 1.0;
        float pdf = a2 / (kPi * q * q) * 0.25;
        float lod = max(0.5 * log2(1.0 / (float(kSamples) * pdf * texel)) + 1.0, 0.0);
        sum += textureLod(uSource, capture_uv(l, source), lod).rgb * ndl;
        weight += ndl;
    }
    FragColor = vec4(sum / max(weight, 1e-6), 1.0);
}
)";
    return source.c_str();
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
)") + lights_glsl() + fog_volumes_glsl() + R"(
// Every slice of a view's air at once, each on its own: the light its lamps
// scatter towards the eye from a metre of the air between that slice and the
// one before. (air_sum_fs takes each slice's depth and the air before it, and
// adds them up.) A cell for each sixteen pixels of the view; the slices go
// out from the eye in steps that widen as they go, as detail does. They are
// laid side by side, eight to a row: a pixel of this picture is a cell of one
// slice.
uniform vec2  uAirCells;        // cells across the view, and up it
uniform vec4  uAir;             // the first slice's distance, the last's, how many slices
uniform mat4  uAirUnproject;    // the view's clip space back to the world
uniform vec3  uViewPos;
uniform float uScatterAhead;    // how much of that goes on ahead (-1..1, 0 every way alike)
uniform vec4  uAirClip;         // only the air on this plane's side is this view's (a doorway's far side)
uniform float uAirSpin;         // which gathering this is: each samples its cells at other points
uniform int   uAirSteps;        // points of each cell, spread through its depth (a thin beam is caught)
uniform vec2  uAirPoints;       // which of them this gathering lights: from the first, every so many
// What was gathered before (a metre of each slice's light, laid as this is),
// kept as much as `uAirKeep` says; and where it was gathered from - unless
// it was from here (`uAirMoved` 0), when each cell is where it was.
uniform sampler2D uAirLast;
uniform float uAirKeep;
uniform float uAirMoved;
uniform mat4  uAirLastVP;
uniform vec3  uAirLastEye;
uniform float uAirAt[64];       // where each slice ends, from the eye (air_slices)
uniform float uFogGlow;         // how much more of it a metre's depth of fog pooled there scatters (fog_volumes_glsl)

float air_at(float i) { return uAirAt[int(i)]; }
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
vec3 slice_light(vec3 dir, vec2 uv, float k, float down) {
    float a = k < 0.5 ? 0.0 : air_at(k - 1.0), b = air_at(k);
    if (b <= a) return vec3(0.0);
    float ahead = clamp(uScatterAhead, -0.9, 0.9);
    vec3 lit = vec3(0.0);
    int steps = max(uAirSteps, 1), every = max(int(uAirPoints.y), 1), lit_points = 0;
    for (int j = int(uAirPoints.x); j < 16; j += every) {
    if (j >= steps) break;
    ++lit_points;
    float at = mix(a, b, (float(j) + down) / float(steps));
    vec3 p = uViewPos + dir * at;
    if (dot(uAirClip.xyz, p) + uAirClip.w < 0.0) continue;
    uvec2 cell = lamp_cell(uv, at);
    // (More of it scattered where fog pools: its shape as it stands, not its
    // drift - air gathered again each frame it drifts would never rest.)
    float pooled = uFogCount > 0 ? (1.0 + uFogGlow * fog_volume_at(p, false)) * exp(-2.0 * dark_at(p)) : 1.0;
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        // Light standing in for what bounces about lights no air of its own;
        // and a lamp whose light ends before this part of the view, none.
        if (uLightB[i].y > 0.5 || uLightC[i].x <= 0.0 || !lamp_in(cell, i)) continue;
        vec3 l;
        float reach = light_reach(i, p, l);
        if (reach <= 1e-5) continue;
        if (i < uShadowCount && uLightD[i].y > -0.5) reach *= air_shadow(shadow_layer(i, p), p);
        // Mostly on ahead, some back: two lobes, as haze has.
        float c = dot(dir, l);
        float phase = mix(schlick(c, ahead), schlick(c, -0.3), 0.25);
        lit += uLightColor[i] * (reach * uLightC[i].x * phase) * pooled;
    }
    }
    lit /= float(max(lit_points, 1));
    return any(isnan(lit)) || any(isinf(lit)) ? vec3(0.0) : lit;
}

// What was gathered before of the air at `p`, from where it was gathered:
// the slice holding that distance (and the next, by how far between them it
// lies) of the cell it was seen in. False where the last view did not see it.
bool air_before(vec3 p, ivec2 cells, out vec3 kept) {
    vec4 c = uAirLastVP * vec4(p, 1.0);
    if (c.w <= 1e-4) return false;
    vec2 uv = c.xy / c.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return false;
    float d = length(p - uAirLastEye);
    // (A slice ends at air_at(k), so the middle of slice k is at k - 0.5.)
    float s = d <= uAir.x ? 0.0 : log(d / uAir.x) / log(uAir.y / uAir.x) * (uAir.z - 1.0) + 0.5;
    if (s > uAir.z) return false;
    s = clamp(s, 0.0, uAir.z - 1.0);
    int k0 = int(floor(s)), k1 = min(k0 + 1, int(uAir.z) - 1);
    ivec2 at = clamp(ivec2(uv * vec2(cells)), ivec2(0), cells - 1);
    vec3 a = texelFetch(uAirLast, at + ivec2(k0 % 8, k0 / 8) * cells, 0).rgb;
    vec3 b = texelFetch(uAirLast, at + ivec2(k1 % 8, k1 / 8) * cells, 0).rgb;
    kept = mix(a, b, s - float(k0));
    return true;
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
    vec3 now = slice_light(dir, uv, k, fract(ign(seed + vec2(41.0, 7.0)) + step3.z));
    // Kept: what was gathered of this cell before - where it was, seen from
    // here; else where its middle was seen from the last view.
    vec3 kept = vec3(0.0);
    float keep = 0.0;
    if (uAirKeep > 0.0) {
        if (uAirMoved < 0.5) {
            kept = texelFetch(uAirLast, px, 0).rgb, keep = uAirKeep;
        } else {
            vec2 mid = (cell + 0.5) / uAirCells;
            vec4 m = uAirUnproject * vec4(mid * 2.0 - 1.0, 1.0, 1.0);
            float a = k < 0.5 ? 0.0 : air_at(k - 1.0), b = air_at(k);
            if (air_before(uViewPos + normalize(m.xyz / m.w - uViewPos) * (0.5 * (a + b)), cells, kept)) keep = uAirKeep;
        }
    }
    FragColor = vec4(mix(now, kept, keep), 1.0);
}
)";
    return source.c_str();
}

const char* mosh_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uCurrent;
uniform sampler2D uMoshed;
uniform sampler2D uLast;
uniform sampler2D uDepth;
uniform mat4 uReproject;
uniform vec2 uTexel;
uniform float uAmount;
uniform float uBlock;
uniform float uFresh;
uniform float uReversedZ;
// A pixel's whole: read from where it is, never between pixels (read
// between, what is carried on would blur a little every frame).
vec2 whole(vec2 px) { return (floor(px) + 0.5) * uTexel; }
float tone(vec3 c) { return dot(c / (1.0 + c), vec3(0.299, 0.587, 0.114)); }
void main() {
    vec3 cur = texture(uCurrent, vUV).rgb;
    if (uFresh > 0.5) {
        FragColor = vec4(cur, 1.0);
        return;
    }
    vec2 px = gl_FragCoord.xy;
    // How its block moved: the view's motion at the block's middle - the
    // motion of what is seen now, as a codec finds it, one for the block.
    vec2 block = floor(px / uBlock);
    vec2 mid = (block + 0.5) * uBlock * uTexel;
    float d = texture(uDepth, mid).r;
    d = uReversedZ > 0.5 ? 1.0 - d : d;
    vec4 was = uReproject * vec4(mid * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec2 moved = was.w > 0.0 ? (mid - (was.xy / was.w * 0.5 + 0.5)) / uTexel : vec2(0.0);
    vec2 from = px - moved;
    // Come in from beyond the edge: nothing was there to carry - as it is now.
    if (any(lessThan(from, vec2(0.0))) || any(greaterThanEqual(from * uTexel, vec2(1.0)))) {
        FragColor = vec4(cur, 1.0);
        return;
    }
    // What was moshed, carried by that motion; and laid over it only what
    // changed this frame that is small - a big change (the cut itself, the
    // frame the key frame was) is lost, so the old picture stays. Less moshed,
    // more of the change is kept, and the new picture comes into it.
    vec3 m = texture(uMoshed, whole(from)).rgb;
    vec3 change = cur - texture(uLast, whole(from)).rgb;
    float big = abs(tone(cur) - tone(texture(uLast, whole(from)).rgb));
    float keep = mix(0.7, 0.05, uAmount);
    m += change * (1.0 - smoothstep(keep * 0.5, keep, big));
    m = mix(m, cur, clamp((0.4 - uAmount) * 1.2, 0.0, 1.0));
    FragColor = vec4(max(m, vec3(0.0)), 1.0);
}
)";
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
// What is made: the screen's pixels - the same as this frame's, or more
// (uUp 1: drawn at fewer, Quality::render_scale).
uniform vec2 uOutTexel;
uniform float uUp;
uniform float uFresh;
uniform float uReversedZ;  // 1: the depth is reversed (near 1, far 0)
// Blended in a curve that keeps a bright pixel from outweighing its
// neighbours (and back): what makes a highlight flicker otherwise.
vec3 squash(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 unsquash(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4); }
vec3 ycocg(vec3 c) { return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b); }
vec3 rgb(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
// What was, read between its pixels sharply (Catmull-Rom, in five taps):
// read bilinearly, the history would soften a little every frame.
vec3 history_at(vec2 uv) {
    vec2 size = 1.0 / uOutTexel;
    vec2 pos = uv * size;
    vec2 c = floor(pos - 0.5) + 0.5;
    vec2 f = pos - c;
    vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    vec2 w3 = f * f * (-0.5 + 0.5 * f);
    vec2 w12 = w1 + w2;
    vec2 t0 = (c - 1.0) * uOutTexel, t3 = (c + 2.0) * uOutTexel, t12 = (c + w2 / w12) * uOutTexel;
    vec3 r = texture(uHistory, vec2(t12.x, t0.y)).rgb * (w12.x * w0.y) + texture(uHistory, vec2(t0.x, t12.y)).rgb * (w0.x * w12.y) +
             texture(uHistory, t12).rgb * (w12.x * w12.y) + texture(uHistory, vec2(t3.x, t12.y)).rgb * (w3.x * w12.y) +
             texture(uHistory, vec2(t12.x, t3.y)).rgb * (w12.x * w3.y);
    float ws = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(r / ws, vec3(0.0));
}
void main() {
    // This frame read where its pixel is unmoved by the jitter: the picture
    // made of the frames before stays where it is, not swaying with the
    // jitter's last few steps (on its cycle of eight, every edge stepped).
    vec2 here = vUV + 0.5 * uJitter;
    vec3 cur = texture(uCurrent, here).rgb;
    // Drawn at fewer pixels: the one of them nearest this pixel of the
    // screen, and how near it fell - near, it says much of this pixel, as it
    // is; further, less, and the frames before say the rest.
    float fell_near = 1.0;
    if (uUp > 0.5) {
        vec2 t = (floor(here / uTexel) + 0.5) * uTexel;
        vec2 fell = (t - 0.5 * uJitter - vUV) / uOutTexel;
        fell_near = exp(-1.5 * dot(fell, fell));
        cur = mix(cur, texture(uCurrent, t).rgb, fell_near);
    }
    // (Its alpha: how many frames the picture here has held, over 64.)
    if (uFresh > 0.5) {
        FragColor = vec4(cur, 0.0);
        return;
    }
    // The neighbourhood: what this frame says the pixel may be, and the
    // nearest depth in it (an edge moves with what is in front).
    vec3 m1 = vec3(0.0), m2 = vec3(0.0);
    float near = 1.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 at = here + vec2(x, y) * uTexel;
            vec3 c = ycocg(squash(texture(uCurrent, at).rgb));
            m1 += c;
            m2 += c * c;
            float d = texture(uDepth, at).r;
            near = min(near, uReversedZ > 0.5 ? 1.0 - d : d);
        }
    vec3 mean = m1 / 9.0, spread = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    // Where it was last frame.
    vec4 was = uReproject * vec4(vUV * 2.0 - 1.0, near * 2.0 - 1.0, 1.0);
    vec2 back = was.xy / was.w * 0.5 + 0.5;
    if (was.w <= 0.0 || any(lessThan(back, vec2(0.0))) || any(greaterThan(back, vec2(1.0)))) {
        FragColor = vec4(cur, 0.0);
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
    // How long the picture here has held: a frame more where what was is
    // still what is; less as far as it had to be brought in (something
    // changed there), none where it was far out. Held long, each new frame
    // is taken in less - a still view settles, its jitter gone from every
    // edge and every fine line (a blind's slats); moving, the frames before
    // are carried by a resampling that softens, so a new one counts for more.
    float age = texture(uHistory, back).a * 64.0;
    age = most > 1.0 ? age * clamp(1.5 - 0.5 * most, 0.0, 1.0) : age + 1.0;
    age = min(age, 63.0);
    float moved = length((back - vUV) / uOutTexel);
    float least = mix(0.035, 0.12, clamp(moved * 0.5, 0.0, 1.0));
    float take = min(max(1.0 / (age + 1.0), least), 0.1);
    vec3 now = squash(cur);
    FragColor = vec4(unsquash(mix(rgb(h), now, uUp > 0.5 ? max(0.03 + 0.2 * fell_near, least) * min(1.0, take * 10.0) : take)), age / 64.0);
}
)";
}

const char* motion_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uMoved;   // what the scene said of each pixel (1e4: not known)
uniform sampler2D uDepth;
uniform mat4 uReproject;    // this frame's clip space, unmoved, to the last frame's
uniform vec2 uJitter;       // this frame's jitter, in clip units
uniform vec2 uSize;         // the picture's pixels
uniform float uReversedZ;
uniform float uFresh;       // 1: no last frame to have come from
void main() {
    vec2 moved = texture(uMoved, vUV).rg;
    if (uFresh > 0.5) {
        FragColor = vec4(0.0);
        return;
    }
    if (moved.x > 5e3) {
        // (Moved as the view moved past it: found where it was by its depth,
        // as the frames' smoothing finds it - at the point this pixel shows,
        // the jitter taken back.)
        float d = texture(uDepth, vUV).r;
        if (uReversedZ > 0.5) d = 1.0 - d;
        vec2 now = vUV * 2.0 - 1.0 - uJitter;
        vec4 was = uReproject * vec4(now, d * 2.0 - 1.0, 1.0);
        moved = was.w > 1e-6 ? (now - was.xy / was.w) * 0.5 * uSize : vec2(0.0);
    }
    FragColor = vec4(moved, 0.0, 1.0);
}
)";
}

const char* air_sum_fs() {
    return R"(#version 330 core
layout(location = 0) out vec4 Slice[8];
// Eight slices of a view's air, each what the slices from the eye to it
// gathered (air_fs, a metre of each laid eight to a row in uAirLocal) - each
// as deep as it is, and dimmed by the air before it: the light the air
// scatters towards the eye up to that distance.
uniform sampler2D uAirLocal;
uniform vec2 uAirCells;  // cells across the view, and up it
uniform int uAirGroup;   // the first of the eight
// What a metre of each slice gives of itself: how deep it is, dimmed by the
// air before it, as much as the air scatters (worked out once a gathering: air_slices).
uniform float uAirWeight[64];
vec3 slice_of(ivec2 at, ivec2 cells, int k) {
    vec3 s = texelFetch(uAirLocal, at + ivec2(k % 8, k / 8) * cells, 0).rgb * uAirWeight[k];
    return any(isnan(s)) || any(isinf(s)) ? vec3(0.0) : s;
}
void main() {
    ivec2 cells = ivec2(uAirCells), at = ivec2(gl_FragCoord.xy);
    vec3 sum = vec3(0.0);
    for (int k = 0; k < uAirGroup; ++k) sum += slice_of(at, cells, k);
    // (Each output by a constant index: GLSL ES takes no other.)
#define SG_SLICE(j) sum += slice_of(at, cells, uAirGroup + j); Slice[j] = vec4(sum, 1.0);
    SG_SLICE(0) SG_SLICE(1) SG_SLICE(2) SG_SLICE(3) SG_SLICE(4) SG_SLICE(5) SG_SLICE(6) SG_SLICE(7)
}
)";
}

// --- a planet's air (GLWorldView::sky_for) ------------------------------------------
const std::string& atmosphere_glsl() {
    static const std::string source = R"(
// A planet's air, in kilometres: its ground, and the top of its air.
const float kGroundR = 6360.0, kTopR = 6460.0;
const float kPi = 3.14159265;
// What a kilometre of it takes out of the light at the ground, each colour:
// its molecules (Rayleigh: all they take scattered), its haze (Mie: a little
// of what it takes kept) and its ozone (kept, none scattered).
const vec3  kRayleigh = vec3(5.802, 13.558, 33.1) * 1e-3;
const float kMieScatter = 3.996e-3, kMieTakes = 4.40e-3;
const vec3  kOzone = vec3(0.650, 1.881, 0.085) * 1e-3;
uniform float uAtmoHaze;    // the haze, as many times the usual
uniform float uAtmoGround;  // how much of the light on it the ground gives back
uniform sampler2D uAtmoTransmittance;
uniform sampler2D uAtmoMultiple;

// The air `h` kilometres up: what it scatters (molecules, haze) and what it
// takes in all - each thinning as it goes up, the ozone thickest at 25 km.
struct AirHere { vec3 rayleigh; float mie; vec3 takes; };
AirHere air_here(float h) {
    float r = exp(-max(h, 0.0) / 8.0), m = exp(-max(h, 0.0) / 1.2), o = max(0.0, 1.0 - abs(h - 25.0) / 15.0);
    AirHere a;
    a.rayleigh = kRayleigh * r;
    a.mie = kMieScatter * m * uAtmoHaze;
    a.takes = a.rayleigh + vec3(kMieTakes * m * uAtmoHaze) + kOzone * o;
    return a;
}

// How far a ray from `r` from the planet's middle, `mu` the cosine of its
// angle from straight up, goes to the sphere of radius `big` round it (from
// inside: its far side); whether it meets the ground, and how far it goes to it.
float to_sphere(float r, float mu, float big) { return max(0.0, -r * mu + sqrt(max(r * r * (mu * mu - 1.0) + big * big, 0.0))); }
bool meets_ground(float r, float mu) { return mu < 0.0 && r * r * (mu * mu - 1.0) + kGroundR * kGroundR >= 0.0; }
float to_ground(float r, float mu) { return max(0.0, -r * mu - sqrt(max(r * r * (mu * mu - 1.0) + kGroundR * kGroundR, 0.0))); }

// A picture's pixels from its first to its last, as 0 to 1 (each end a
// pixel's middle, so what is at an end is read exactly there), and back.
vec2 texel_to_unit(vec2 at, vec2 size) { return (at - 0.5) / (size - 1.0); }
vec2 unit_to_texel(vec2 u, vec2 size) { return (u * (size - 1.0) + 0.5) / size; }

// Where in the transmittance picture the way from (r, mu) to the top of the
// air is (Bruneton's: across, how far it goes, of the least and most a way
// from that height could; up, how high), and back.
vec2 transmittance_uv(float r, float mu) {
    float H = sqrt(kTopR * kTopR - kGroundR * kGroundR), rho = sqrt(max(r * r - kGroundR * kGroundR, 0.0));
    float d = to_sphere(r, mu, kTopR), dmin = kTopR - r, dmax = rho + H;
    return vec2(clamp((d - dmin) / max(dmax - dmin, 1e-4), 0.0, 1.0), clamp(rho / H, 0.0, 1.0));
}
void transmittance_at(vec2 uv, out float r, out float mu) {
    float H = sqrt(kTopR * kTopR - kGroundR * kGroundR), rho = H * uv.y;
    r = sqrt(rho * rho + kGroundR * kGroundR);
    float dmin = kTopR - r, dmax = rho + H, d = dmin + uv.x * (dmax - dmin);
    mu = d <= 0.0 ? 1.0 : clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
}
vec3 to_top(float r, float mu) {
    return texture(uAtmoTransmittance, unit_to_texel(transmittance_uv(r, mu), vec2(textureSize(uAtmoTransmittance, 0)))).rgb;
}
// What gets through of the sun's light to (r) from its way (mu): none where
// the ground is in the way.
vec3 light_through(float r, float mu) { return meets_ground(r, mu) ? vec3(0.0) : to_top(r, mu); }
// What gets through along the whole of the way from (r, mu) out: to the top
// of the air, or to the ground (the way back up from it, less the eye's).
vec3 whole_way(float r, float mu) {
    if (!meets_ground(r, mu)) return to_top(r, mu);
    float d = to_ground(r, mu), mu_d = (r * mu + d) / kGroundR;
    return min(to_top(kGroundR, -mu_d) / max(to_top(r, -mu), vec3(1e-6)), vec3(1.0));
}

// How much of the light scattered goes each way, by the cosine of its turn:
// by molecules (Rayleigh), and by haze, mostly on ahead (Cornette-Shanks, g 0.8).
float rayleigh_phase(float c) { return 3.0 / (16.0 * kPi) * (1.0 + c * c); }
float mie_phase(float c) {
    const float g = 0.8;
    float k = 3.0 / (8.0 * kPi) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + c * c) / pow(max(1.0 + g * g - 2.0 * g * c, 1e-4), 1.5);
}

// The light scattered twice and more at (r) for the sun at (mu_s), a sun's
// worth of it (the multiple scattering picture: across the sun's height, up
// the point's).
vec3 multiple_at(float r, float mu_s) {
    vec2 u = vec2(mu_s * 0.5 + 0.5, clamp((r - kGroundR) / (kTopR - kGroundR), 0.0, 1.0));
    return texture(uAtmoMultiple, unit_to_texel(u, vec2(textureSize(uAtmoMultiple, 0)))).rgb;
}

// The sky view picture: up it, the angle from straight up - its lower half
// above the horizon, its upper half below it, each finest at the horizon;
// across, the turn from the sun's way round, finest near the sun. Seen from
// `r` from the planet's middle.
float sky_view_zenith(float v, float r) {
    float beta = acos(clamp(sqrt(max(r * r - kGroundR * kGroundR, 0.0)) / r, -1.0, 1.0)), horizon = kPi - beta;
    if (v < 0.5) {
        float c = 1.0 - 2.0 * v;
        return horizon * (1.0 - c * c);
    }
    float c = 2.0 * v - 1.0;
    return horizon + beta * c * c;
}
vec2 sky_view_uv(vec3 dir, vec3 sun, float r) {
    float beta = acos(clamp(sqrt(max(r * r - kGroundR * kGroundR, 0.0)) / r, -1.0, 1.0)), horizon = kPi - beta;
    float z = acos(clamp(dir.y, -1.0, 1.0));
    float v = z < horizon ? 0.5 * (1.0 - sqrt(max(1.0 - z / horizon, 0.0))) : 0.5 + 0.5 * sqrt(max((z - horizon) / max(beta, 1e-4), 0.0));
    float la = length(dir.xz), lb = length(sun.xz);
    float turn = la < 1e-5 || lb < 1e-5 ? 0.0 : acos(clamp(dot(dir.xz, sun.xz) / (la * lb), -1.0, 1.0));
    return vec2(sqrt(turn / kPi), v);
}
)";
    return source;
}

const char* atmosphere_transmittance_fs() {
    static const std::string source = std::string(R"(#version 330 core
out vec4 FragColor;
)") + atmosphere_glsl() + R"(
uniform vec2 uAtmoSize;
// How much of the light gets through from each height, each way, to the
// top of the air: the air it passes, added up along the way (40 steps).
void main() {
    float r, mu;
    transmittance_at(texel_to_unit(gl_FragCoord.xy, uAtmoSize), r, mu);
    float len = to_sphere(r, mu, kTopR), dt = len / 40.0;
    vec3 depth = vec3(0.0);
    for (int i = 0; i < 40; ++i) {
        float t = (float(i) + 0.5) * dt;
        depth += air_here(sqrt(r * r + t * t + 2.0 * r * mu * t) - kGroundR).takes * dt;
    }
    FragColor = vec4(exp(-depth), 1.0);
}
)";
    return source.c_str();
}

const char* atmosphere_multiple_fs() {
    static const std::string source = std::string(R"(#version 330 core
out vec4 FragColor;
)") + atmosphere_glsl() + R"(
uniform vec2 uAtmoSize;
// The light scattered twice and more at a height for a height of the sun
// (after Hillaire, 2020): along 64 ways out from the point, the sun's light
// scattered once towards it (l), and the share of what reaches it that the
// air on the way scatters at all (f) - each averaged over the ways; and,
// taking every further order of scattering as that share of the one
// before, all of them at once: l / (1 - f). The ground gives back its share.
void main() {
    vec2 u = texel_to_unit(gl_FragCoord.xy, uAtmoSize);
    float mu_s = u.x * 2.0 - 1.0, r = mix(kGroundR + 0.01, kTopR - 0.01, u.y);
    vec3 sun = vec3(sqrt(max(1.0 - mu_s * mu_s, 0.0)), mu_s, 0.0);
    vec3 l = vec3(0.0), f = vec3(0.0);
    const int kWays = 8, kSteps = 20;
    for (int i = 0; i < kWays; ++i)
        for (int j = 0; j < kWays; ++j) {
            // (Spread evenly over the sphere: equal bands of height, equal turns.)
            float cy = 1.0 - 2.0 * (float(i) + 0.5) / float(kWays), ph = 2.0 * kPi * (float(j) + 0.5) / float(kWays);
            float sy = sqrt(max(1.0 - cy * cy, 0.0));
            vec3 dir = vec3(sy * cos(ph), cy, sy * sin(ph));
            bool ground = meets_ground(r, cy);
            float len = ground ? to_ground(r, cy) : to_sphere(r, cy, kTopR), dt = len / float(kSteps);
            vec3 through = vec3(1.0);
            for (int k = 0; k < kSteps; ++k) {
                vec3 p = vec3(0.0, r, 0.0) + dir * ((float(k) + 0.5) * dt);
                float rp = length(p);
                AirHere a = air_here(rp - kGroundR);
                vec3 scatters = a.rayleigh + vec3(a.mie), takes = max(a.takes, vec3(1e-7)), step_through = exp(-takes * dt);
                vec3 lit = scatters * light_through(rp, dot(p / rp, sun)) / (4.0 * kPi);
                l += through * (lit - lit * step_through) / takes;
                f += through * (scatters - scatters * step_through) / takes;
                through *= step_through;
            }
            if (ground) {
                vec3 up = normalize(vec3(0.0, r, 0.0) + dir * len);
                float lit = max(dot(up, sun), 0.0);
                l += through * light_through(kGroundR, dot(up, sun)) * lit * uAtmoGround / kPi;
            }
        }
    l /= float(kWays * kWays), f /= float(kWays * kWays);
    FragColor = vec4(l / max(vec3(1.0) - f, vec3(1e-3)), 1.0);
}
)";
    return source.c_str();
}

const char* atmosphere_sky_fs() {
    static const std::string source = std::string(R"(#version 330 core
out vec4 FragColor;
)") + atmosphere_glsl() + R"(
uniform vec2 uAtmoSize;
uniform float uAtmoEye;     // the eye's distance from the planet's middle, km
uniform float uAtmoSunUp;   // the sine of the sun's height over the horizon
// The sky seen from the eye every way, a sun's worth of light: along each
// way out to the top of the air or the ground (30 steps, closer near the
// eye), the sun's light that gets to each point scattered towards the eye
// once - by molecules and by haze, each its own way - and the light
// scattered there more than once (multiple_at), as much as gets to the eye.
void main() {
    vec2 u = texel_to_unit(gl_FragCoord.xy, uAtmoSize);
    float r = uAtmoEye, zenith = sky_view_zenith(u.y, r), turn = kPi * u.x * u.x;
    vec3 dir = vec3(sin(zenith) * cos(turn), cos(zenith), sin(zenith) * sin(turn));
    vec3 sun = vec3(sqrt(max(1.0 - uAtmoSunUp * uAtmoSunUp, 0.0)), uAtmoSunUp, 0.0);
    float len = meets_ground(r, dir.y) ? to_ground(r, dir.y) : to_sphere(r, dir.y, kTopR);
    float c = dot(dir, sun), pr = rayleigh_phase(c), pm = mie_phase(c);
    vec3 through = vec3(1.0), l = vec3(0.0);
    const int kSteps = 30;
    for (int k = 0; k < kSteps; ++k) {
        float s = (float(k) + 0.5) / float(kSteps), t = len * s * s, dt = len * 2.0 * s / float(kSteps);
        vec3 p = vec3(0.0, r, 0.0) + dir * t;
        float rp = length(p), mu_s = dot(p / rp, sun);
        AirHere a = air_here(rp - kGroundR);
        vec3 takes = max(a.takes, vec3(1e-7)), step_through = exp(-takes * dt);
        vec3 lit = (a.rayleigh * pr + vec3(a.mie * pm)) * light_through(rp, mu_s) + (a.rayleigh + vec3(a.mie)) * multiple_at(rp, mu_s);
        l += through * (lit - lit * step_through) / takes;
        through *= step_through;
    }
    FragColor = vec4(l, 1.0);
}
)";
    return source.c_str();
}

// --- a world's clouds (GLWorldView::sky_for) ----------------------------------------
// --- a terrain's horizon toward the sun (GLWorldView::horizon_for) --------------
//
// For each point of the grid, how steeply the land toward the sun rises over
// it (the tangent of the horizon that way, the steepest of the land met
// along it, stepping further each step) and the ground's height there.
const char* horizon_fs() {
    static const std::string source = R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uHeights;  // the ground's heights, a cell a texel
uniform vec2 uHorizonGrid;   // cells across, metres a cell
uniform vec2 uHorizonSun;    // the sun's bearing across the ground (towards it, unit)
void main() {
    float n = uHorizonGrid.x, cell = uHorizonGrid.y;
    vec2 at = gl_FragCoord.xy;
    float h0 = texture(uHeights, at / n).r, most = -1e3, s = 1.0;
    for (int i = 0; i < 128; ++i) {
        vec2 q = at + uHorizonSun * s;
        if (q.x < 0.0 || q.y < 0.0 || q.x > n || q.y > n) break;
        most = max(most, (texture(uHeights, q / n).r - h0) / (s * cell));
        s = s * 1.05 + 0.5;
    }
    FragColor = vec4(most, h0, 0.0, 1.0);
}
)";
    return source.c_str();
}

const std::string& fog_volumes_glsl() {
    static const std::string source = R"(
const int MAX_FOGS = 4;
uniform int   uFogCount;
uniform vec4  uFogAt[MAX_FOGS];    // its base's middle (the world), its yaw
uniform vec4  uFogSize[MAX_FOGS];  // half its width, its height, half its depth; how dense it is (a metre's depth of it)
uniform vec4  uFogMake[MAX_FOGS];  // how soft its sides and top (metres), how much thinner a metre up, how much it drifts (0..1), its wisps' size
uniform vec4  uFogTint[MAX_FOGS];  // its own colour, and whether it has one (else its look's fog's); over 1, a fog that takes light (dark: 1 + how much); the dark's r g: its breath and how fast
uniform float uFogTime[MAX_FOGS];  // the time of the world it is in, which its wisps move with
float fog_hash(vec3 c) { return fract(sin(dot(c, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float fog_noise(vec3 p) {
    vec3 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(fog_hash(i), fog_hash(i + vec3(1, 0, 0)), u.x), mix(fog_hash(i + vec3(0, 1, 0)), fog_hash(i + vec3(1, 1, 0)), u.x), u.y),
               mix(mix(fog_hash(i + vec3(0, 0, 1)), fog_hash(i + vec3(1, 0, 1)), u.x), mix(fog_hash(i + vec3(0, 1, 1)), fog_hash(i + vec3(1, 1, 1)), u.x), u.y), u.z);
}
// `p` (the world) seen from fog `k`'s base, turned as it is.
vec3 fog_local(int k, vec3 p) {
    vec3 d = p - uFogAt[k].xyz;
    float c = cos(uFogAt[k].w), s = sin(uFogAt[k].w);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}
// How thick fog `k` is at `q` (its own frame): its sides and top soft, its
// base where it lies, thinner as it rises - and, where `drift` is asked, its
// wisps where its world's time has carried them.
float fog_in(int k, vec3 q, bool drift) {
    vec3 h = uFogSize[k].xyz;
    if (uFogTint[k].w > 1.0) {
        // The dark is no box of air, and no figure either: a ragged column
        // of black as tall as its box, round in plan, thinning at its foot
        // and its top - an absence, never a body with a head. Its edges are
        // eaten by its wisps, moving with its world's time.
        // It breathes: its edge drawn in and let out, a slow swell climbing
        // it (`breath` how far, its colour's r; `breath_rate`, its g), on
        // its world's time as its wisps are.
        float hy = max(h.y, 1e-3);
        float swell = 1.0 + uFogTint[k].r * sin(uFogTime[k] * uFogTint[k].g - q.y * 2.4);
        float plan = 1.0 - length(vec2(q.x / max(h.x * swell, 1e-3), q.z / max(h.z * swell, 1e-3)));
        float tall = 1.0 - abs(q.y - 0.5 * hy) / (0.5 * hy);
        float inside = min(plan, tall * 0.8);
        // (Well outside it, nothing its wisps could bring in: no noise asked -
        // every lit thing in the room asks this.)
        if (inside < -0.65 * (drift ? uFogMake[k].z : 0.0)) return 0.0;
        if (drift && uFogMake[k].z > 0.0) {
            vec3 r = q / max(uFogMake[k].w, 0.05) + vec3(0.07, 0.31, 0.05) * uFogTime[k];
            inside += (0.65 * fog_noise(r) + 0.35 * fog_noise(r * 2.7 + 3.1) - 0.5) * 1.3 * uFogMake[k].z;
        }
        return smoothstep(0.0, max(uFogMake[k].x, 0.02), inside) * uFogSize[k].w;
    }
    float soft = max(uFogMake[k].x, 1e-3);
    float edge = min(min(h.x - abs(q.x), h.z - abs(q.z)), h.y - q.y);
    if (q.y < 0.0 || edge <= -soft) return 0.0;
    float w = smoothstep(-soft, soft, edge) * exp(-uFogMake[k].y * q.y);
    if (drift && uFogMake[k].z > 0.0) {
        vec3 r = q / max(uFogMake[k].w, 0.1) + vec3(0.11, 0.03, 0.07) * uFogTime[k];
        w *= mix(1.0, 2.0 * (0.65 * fog_noise(r) + 0.35 * fog_noise(r * 2.3 + 7.1)), uFogMake[k].z);
    }
    return w * uFogSize[k].w;
}
// How thick the fogs are at `p` (the world), all together - those that are
// air (a fog that takes light, the dark, is no air to scatter in).
float fog_volume_at(vec3 p, bool drift) {
    float d = 0.0;
    for (int k = 0; k < MAX_FOGS; ++k) {
        if (k >= uFogCount) break;
        if (uFogTint[k].w > 1.0) continue;
        d += fog_in(k, fog_local(k, p), drift);
    }
    return d;
}
// How much light the dark takes at `p` (the world): a fog that takes light
// (`takes`), as thick as it is there, times how much it takes.
// Round it, what stands near it is lit the less too: the light it drinks
// from the floor and walls beside it, falling off over about twice its
// width outward - no noise, one length a lamp-lit point (a hollow in the
// light, seen at a glance even where it stands against the dark).
float dark_halo(int k, vec3 q) {
    vec3 h = uFogSize[k].xyz;
    float hy = max(h.y, 1e-3);
    float r = length(vec2(q.x / max(h.x, 1e-3), q.z / max(h.z, 1e-3)));
    float up = clamp((q.y + 0.3) / 0.5, 0.0, 1.0) * clamp((hy + 0.4 - q.y) / 0.8, 0.0, 1.0);
    return exp(-0.9 * max(r - 1.0, 0.0)) * up * 0.3 * uFogSize[k].w;
}
float dark_at(vec3 p) {
    float d = 0.0;
    for (int k = 0; k < MAX_FOGS; ++k) {
        if (k >= uFogCount) break;
        if (uFogTint[k].w <= 1.0) continue;
        vec3 q = fog_local(k, p);
        d += max(fog_in(k, q, true), dark_halo(k, q)) * (uFogTint[k].w - 1.0);
    }
    return d;
}
// The box of fog `k` along the way from `a`, `d` (its own frame), up to
// `len`: where the way goes in and where it comes out (none if out <= in).
vec2 fog_crossing(int k, vec3 qa, vec3 d, float len) {
    float soft = uFogMake[k].x;
    vec3 lo = vec3(-uFogSize[k].x - soft, 0.0, -uFogSize[k].z - soft), hi = uFogSize[k].xyz + soft;
    vec3 inv = 1.0 / mix(d, vec3(1e-6), lessThan(abs(d), vec3(1e-6)));
    vec3 t0 = (lo - qa) * inv, t1 = (hi - qa) * inv;
    vec3 tn = min(t0, t1), tf = max(t0, t1);
    return vec2(max(max(tn.x, tn.y), max(tn.z, 0.0)), min(min(tf.x, tf.y), min(tf.z, len)));
}
// How much light the dark takes along the way from `a` to `b` (the world):
// what is seen through it, and the air's light on the way, darkened by it.
float dark_depth(vec3 a, vec3 b) {
    float len = length(b - a), depth = 0.0;
    if (uFogCount == 0 || len < 1e-4) return 0.0;
    for (int k = 0; k < MAX_FOGS; ++k) {
        if (k >= uFogCount) break;
        if (uFogTint[k].w <= 1.0) continue;
        vec3 qa = fog_local(k, a), d = (fog_local(k, b) - qa) / len;
        vec2 io = fog_crossing(k, qa, d, len);
        if (io.y <= io.x) continue;
        float sum = 0.0;
        for (int i = 0; i < 8; ++i) sum += fog_in(k, qa + d * mix(io.x, io.y, (float(i) + 0.5) / 8.0), true);
        depth += sum / 8.0 * (io.y - io.x) * (uFogTint[k].w - 1.0);
    }
    return depth;
}
// Their depth along the way from `a` to `b` (the world): each box the way
// crosses sampled across where it crosses it. In `own`, how much of that is
// of a fog with its own colour: that colour times its depth, and its depth.
float fog_volume_depth(vec3 a, vec3 b, out vec4 own) {
    own = vec4(0.0);
    float len = length(b - a), depth = 0.0;
    if (uFogCount == 0 || len < 1e-4) return 0.0;
    for (int k = 0; k < MAX_FOGS; ++k) {
        if (k >= uFogCount) break;
        if (uFogTint[k].w > 1.0) continue;
        vec3 qa = fog_local(k, a), d = (fog_local(k, b) - qa) / len;
        float soft = uFogMake[k].x;
        vec3 lo = vec3(-uFogSize[k].x - soft, 0.0, -uFogSize[k].z - soft), hi = uFogSize[k].xyz + soft;
        vec3 inv = 1.0 / mix(d, vec3(1e-6), lessThan(abs(d), vec3(1e-6)));
        vec3 t0 = (lo - qa) * inv, t1 = (hi - qa) * inv;
        vec3 tn = min(t0, t1), tf = max(t0, t1);
        float enter = max(max(tn.x, tn.y), max(tn.z, 0.0)), leave = min(min(tf.x, tf.y), min(tf.z, len));
        if (leave <= enter) continue;
        float sum = 0.0;
        for (int i = 0; i < 6; ++i) sum += fog_in(k, qa + d * mix(enter, leave, (float(i) + 0.5) / 6.0), true);
        float here = sum / 6.0 * (leave - enter);
        depth += here;
        own += vec4(uFogTint[k].rgb, 1.0) * (here * uFogTint[k].w);
    }
    return depth;
}
)";
    return source;
}

const std::string& clouds_glsl() {
    static const std::string source = R"(
// The clouds as a volume (a look's `clouds.cover`; cloud_dome_fs).
uniform sampler3D uCloudNoise;  // their noise (cloud_noise_fs)
uniform vec4  uCloudLayer;      // their base and top (metres over the ground), how much of the sky they cover, how dense
uniform vec4  uCloudShape;      // metres across the noise, how much their edges fray, how bright, how far they are drawn (metres)
uniform vec3  uCloudDrift;      // how far the wind has carried them and they have boiled up, metres (from world time)
uniform vec2  uCloudLean;       // how far their tops lean along the wind, metres
// What a metre of cloud as dense as 1 takes out of the light - all of it
// scattered: a cloud keeps none.
const float kCloudTakes = 0.04;
// How much of their finest fraying is drawn (1 near; less far off, where
// it is finer than what draws it and would only glitter).
float cloud_fine = 1.0;

// How dense the cloud is at `p` (world metres), `h` over the ground: none
// past where its cover says. Where it says some, a heap: flat at its base
// (where the rising air condenses, one height for all of them) and domed
// over it, solid within - a heap of cloud is about as tall as it is wide,
// so its top over a spot rises with how far in from its edge the spot is,
// to a third of the noise's size at its middle (a cumulus a kilometre or
// two across, a kilometre or less tall), no higher than the layer. The
// noise only eats into it from outside, at two sizes (its billows and the
// billows on them), so its outline is heaped and its inside whole - not
// the noise cut at a level everywhere, which breaks a heap into bubbles
// stacked one over another. Frayed at its edges (wispy near its base)
// unless `rough` - which is all a ray towards the sun far from the point
// needs.
float cloud_at(vec3 p, float h, bool rough) {
    float thick = uCloudLayer.y - uCloudLayer.x, hf = (h - uCloudLayer.x) / thick;
    if (hf < 0.0 || hf > 1.0) return 0.0;
    vec3 q = (vec3(p.x, h, p.z) + uCloudDrift + vec3(uCloudLean.x, 0.0, uCloudLean.y) * hf) / uCloudShape.x;
    float cover = texture(uCloudNoise, vec3(q.xz * 0.23, 0.13)).r * 0.6 + texture(uCloudNoise, vec3(q.xz * 0.61 + 0.37, 0.71)).g * 0.4;
    // (The noise spread over 0..1 first: most of it lies between a third and two thirds.)
    cover = clamp((cover - 0.3) / 0.45, 0.0, 1.0);
    // (Rising slowly from its edge in, not at once: the dome's slope.)
    cover = smoothstep(1.0 - uCloudLayer.z, 1.0 - uCloudLayer.z + 0.5, cover) * min(uCloudLayer.z * 4.0, 1.0);
    if (cover <= 0.0) return 0.0;
    float tall = min(uCloudShape.x * 0.33 * sqrt(cover) / thick, 1.0), up = hf / max(tall, 1e-3);
    if (up >= 1.0) return 0.0;
    float dome = cover * (1.0 - up * up * up) * smoothstep(0.0, min(40.0 / thick, 0.05), hf);
    // (Its billows read turned from the cover and at a size no whole
    // multiple of it, and the billows on them turned again: heaps far off
    // are not the same heaps over again in rows.)
    vec3 qs = vec3(0.8 * q.x - 0.6 * q.z, q.y, 0.6 * q.x + 0.8 * q.z) * 1.13 + vec3(0.41, 0.0, 0.77);
    float shape = texture(uCloudNoise, qs).r;
    if (!rough) shape = shape * 0.6 + texture(uCloudNoise, vec3(0.8 * qs.x + 0.6 * qs.z, qs.y, -0.6 * qs.x + 0.8 * qs.z) * 2.71 + vec3(0.31, 0.17, 0.53)).r * 0.4;
    // (The noise spread over 0..1 first, as the cover's is.)
    shape = clamp((shape - 0.35) / 0.35, 0.0, 1.0);
    float d = clamp((dome - (1.0 - shape) * 0.7) / 0.15, 0.0, 1.0);
    if (d <= 0.0 || rough) return d * uCloudLayer.w;
    // Its edge heaped again, finer: cut in between the cells, kept over
    // them - billows on billows, a cauliflower's - least at its base, where
    // the air condenses evenly.
    float fray = dot(texture(uCloudNoise, qs * 5.3 + vec3(0.0, uCloudDrift.y / uCloudShape.x, 0.0)).gba, vec3(0.625, 0.25, 0.125));
    fray = clamp((0.85 - mix(0.62, fray, cloud_fine)) / 0.45, 0.0, 1.0) * uCloudShape.y * mix(0.35, 1.0, smoothstep(0.0, 0.3, up));
    return clamp((d - fray) / max(1.0 - fray, 1e-3), 0.0, 1.0) * uCloudLayer.w;
}

// How much of the sun's light (from `l`) gets through the clouds to `p`
// (world metres): the cloud on the way, eight steps through their layer
// (near the eye the planet's curve is nothing).
float through_clouds(vec3 p, vec3 l) {
    if (l.y <= 0.02 || p.y >= uCloudLayer.y) return 1.0;
    float t0 = max(uCloudLayer.x - p.y, 0.0) / l.y, t1 = (uCloudLayer.y - p.y) / l.y, dt = (t1 - t0) / 8.0, depth = 0.0;
    for (int k = 0; k < 8; ++k) {
        vec3 q = p + l * (t0 + (float(k) + 0.5) * dt);
        depth += cloud_at(q, q.y, false) * dt;
    }
    // (Never all of it: some comes on through the cloud, scattered ahead.)
    return max(exp(-depth * kCloudTakes), 0.1);
}
)";
    return source;
}

const std::string& cloud_dome_glsl() {
    static const std::string source = R"(
// The sky above as a square (an octahedron's upper half laid flat): straight
// up its middle, the horizon all round its edge.
vec3 dome_dir(vec2 uv) {
    vec2 f = uv * 2.0 - 1.0, d = vec2(f.x + f.y, f.x - f.y) * 0.5;
    return normalize(vec3(d.x, max(1.0 - abs(d.x) - abs(d.y), 0.0), d.y));
}
vec2 dome_uv(vec3 dir) {
    vec3 n = dir / max(abs(dir.x) + abs(dir.y) + abs(dir.z), 1e-6);
    return vec2(n.x + n.z, n.x - n.z) * 0.5 + 0.5;
}
)";
    return source;
}

const char* cloud_noise_fs() {
    return R"(#version 330 core
layout(location = 0) out vec4 Layer[8];
// The clouds' noise, a volume that repeats each way: its shape (red: a
// gradient noise with the gaps between cells cut out of it, so it billows)
// and cells at three sizes for fraying its edges (green, blue, alpha:
// Worley's, each the next finer). Eight of its slices a pass.
uniform int uNoiseGroup;   // the first of them
uniform float uNoiseSize;  // its texels each way
uint scramble(uint h) {
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    return h ^ (h >> 16);
}
vec3 lattice(ivec3 c) {
    uint h = scramble(uint(c.x) * 73856093u ^ uint(c.y) * 19349663u ^ uint(c.z) * 83492791u);
    return vec3(float(h & 1023u), float((h >> 10) & 1023u), float((h >> 20) & 1023u)) / 1023.0;
}
// Near a point scattered one to a cell, 1; as far as a cell from every one,
// 0 (`n` cells across the whole, going on from the other side past an edge).
float cells(vec3 p, int n) {
    vec3 q = p * float(n), f = fract(q);
    ivec3 c = ivec3(floor(q));
    float best = 1e9;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                ivec3 o = ivec3(x, y, z);
                vec3 d = vec3(o) + lattice((c + o + n) % n) - f;
                best = min(best, dot(d, d));
            }
    return 1.0 - clamp(sqrt(best), 0.0, 1.0);
}
float cells_of(vec3 p, int n) { return cells(p, n) * 0.625 + cells(p, n * 2) * 0.25 + cells(p, n * 4) * 0.125; }
// Perlin's gradient noise, `n` cells across the whole and repeating: about -1..1.
float gradient(vec3 p, int n) {
    vec3 q = p * float(n), f = fract(q), u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    ivec3 c = ivec3(floor(q));
    float v[8];
    for (int i = 0; i < 8; ++i) {
        ivec3 o = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        v[i] = dot(normalize(lattice((c + o) % n) * 2.0 - 1.0 + vec3(1e-4)), f - vec3(o));
    }
    return mix(mix(mix(v[0], v[1], u.x), mix(v[2], v[3], u.x), u.y), mix(mix(v[4], v[5], u.x), mix(v[6], v[7], u.x), u.y), u.z);
}
void main() {
    for (int k = 0; k < 8; ++k) {
        vec3 p = vec3(gl_FragCoord.xy, float(uNoiseGroup + k) + 0.5) / uNoiseSize;
        float smooth_part = (gradient(p, 4) * 0.5 + gradient(p, 8) * 0.25 + gradient(p, 16) * 0.125) / 0.875 * 0.7 + 0.5;
        float billows = cells_of(p, 4);
        float shape = clamp((smooth_part - billows + 1.0) / (2.0 - billows), 0.0, 1.0);
        Layer[k] = vec4(shape, billows, cells_of(p, 8), cells_of(p, 16));
    }
}
)";
}

const char* cloud_shadow_fs() {
    static const std::string source = std::string(R"(#version 330 core
out vec4 FragColor;
)") + clouds_glsl() + R"(
// The clouds' shadow on the ground round the eye: at each texel's spot on
// the ground (height 0), how much of the sun's light gets through them.
uniform vec2 uMapSize;
uniform vec4 uCloudShadowAt;  // the map's middle (x, z), half its width (metres)
uniform vec3 uSunDir;         // towards the sun
void main() {
    vec2 at = uCloudShadowAt.xy + ((gl_FragCoord.xy / uMapSize) * 2.0 - 1.0) * uCloudShadowAt.z;
    FragColor = vec4(through_clouds(vec3(at.x, 0.0, at.y), normalize(uSunDir + vec3(0.0, 1e-4, 0.0))), 0.0, 0.0, 1.0);
}
)";
    return source.c_str();
}

const char* cloud_dome_fs() {
    static const std::string source = std::string(R"(#version 330 core
out vec4 FragColor;
)") + atmosphere_glsl() + cloud_dome_glsl() + clouds_glsl() + R"(
uniform sampler2D uDomeLast;    // the dome as it was
uniform sampler2D uAtmoSky;     // the planet's sky (atmosphere_sky_fs), where it is drawn
uniform vec2  uDomeSize;
uniform int   uDomePart;        // which texel of each four by four is made again (-1: all)
uniform float uDomeKeep;        // how much of what was there is kept where it is made again
uniform float uSpin;            // a number for this frame, so each march begins somewhere else
uniform vec3  uEye;             // where the dome is seen from, metres
uniform float uPlanet;          // the planet's radius, metres
uniform int   uCloudSteps;
uniform vec3  uSunDir, uSunColor;
uniform vec3  uSkyTop, uSkyHorizon; // the sky's own colours, for the light from all round where the planet's sky is not drawn
uniform float uAtmo, uAtmoScale, uAtmoAltitude, uAtmoSun;
uniform vec4  uCirrus;          // how much of the sky it covers (0..1), how high, its streaks' size (metres), its wind's way
uniform vec2  uCirrusDrift;     // how far its wind has carried it (metres, from world time)


// How far a way from `r` from the planet's middle, at `mu`, goes to the
// sphere of radius `big` round it, from inside - worked out so as to lose
// nothing to a planet's metres.
float out_of(float r, float mu, float big) {
    float d = (big - r) * (big + r), s = sqrt((r * mu) * (r * mu) + d);
    return mu > 0.0 ? d / (s + r * mu) : s - r * mu;
}
// Henyey-Greenstein's: how much of the light goes `c` (a cosine) off its way.
float lobe(float g, float c) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
}

// The planet's sky that way, as bright as its sun, from `r` km.
vec3 planet_sky(vec3 dir, vec3 sun, float r) {
    return texture(uAtmoSky, unit_to_texel(sky_view_uv(dir, sun, r), vec2(textureSize(uAtmoSky, 0)))).rgb * uSunColor * uAtmoSun;
}

// The clouds seen the way `dir` from the eye: the light they send (as
// much of it as gets through the air on the way, and the air's own light in
// front of them) and how much of what is behind them is seen through.
vec4 clouds_along(vec3 dir, float jitter) {
    if (dir.y <= 0.0 || uEye.y >= uCloudLayer.x) return vec4(0.0, 0.0, 0.0, 1.0);
    float r = uPlanet + uEye.y;
    float t0 = out_of(r, dir.y, uPlanet + uCloudLayer.x);
    if (t0 >= uCloudShape.w) return vec4(0.0, 0.0, 0.0, 1.0);
    float t1 = min(out_of(r, dir.y, uPlanet + uCloudLayer.y), t0 + uCloudShape.w * 0.5);
    vec3 sun = normalize(uSunDir + vec3(0.0, 1e-4, 0.0));
    float c = dot(dir, sun);
    // The sun's light at the clouds, and the light from all round: the sky
    // above, the ground below - the planet's, where it is drawn; else the look's.
    float rk = clamp(kGroundR + uAtmoAltitude + (uEye.y + 0.5 * (uCloudLayer.x + uCloudLayer.y)) * uAtmoScale, kGroundR + 0.001, kTopR - 0.01);
    vec3 sunlight = uSunColor * 10.0, above = (uSkyTop + uSkyHorizon) * 0.7, below = uSkyHorizon * 0.45;
    if (uAtmo > 0.0) {
        sunlight = mix(sunlight, uSunColor * uAtmoSun * light_through(rk, sun.y), uAtmo);
        vec3 sky_above = 0.5 * (planet_sky(vec3(0.0, 1.0, 0.0), sun, rk) + planet_sky(normalize(vec3(sun.z, 0.3, -sun.x)), sun, rk));
        above = mix(above, sky_above, uAtmo);
        below = mix(below, sky_above * 0.15 + sunlight * max(sun.y, 0.0) * uAtmoGround / kPi * 0.5, uAtmo);
    }
    // Steps as long as the layer is thick over `uCloudSteps`, longer as they
    // go out (a hundredth of the way, past where that is longer) - not the
    // way through it over `uCloudSteps`, which near the horizon is many
    // kilometres and steps over whole clouds.
    float near_dt = (uCloudLayer.y - uCloudLayer.x) / float(uCloudSteps), through = 1.0, weight = 0.0, far_sum = 0.0;
    float t = t0 + jitter * max(near_dt, t0 * 0.01);
    vec3 light = vec3(0.0);
    for (int k = 0; k < 256; ++k) {
        if (t >= t1 || through < 0.01) break;
        float ts = t, dt = max(near_dt, ts * 0.01);
        t += dt;
        cloud_fine = 1.0 - smoothstep(1500.0, 8000.0, ts);
        vec3 at = vec3(dir.x * ts, r + dir.y * ts, dir.z * ts);
        float h = length(at) - uPlanet;
        vec3 p = uEye + dir * ts;
        float d = cloud_at(p, h, false);
        if (d <= 0.0) continue;
        // How much cloud is between it and the sun: six steps, each twice the last.
        float depth = 0.0, s = 0.0, len = 30.0;
        for (int j = 0; j < 6; ++j) {
            vec3 q = p + sun * (s + 0.5 * len);
            vec3 qa = at + sun * (s + 0.5 * len);
            depth += cloud_at(q, length(qa) - uPlanet, j > 2) * len;
            s += len, len *= 2.0;
        }
        // Scattered once - most on ahead, some back - as much as comes the
        // whole way from the sun; and scattered many times, the light
        // diffused through the cloud, wider: deep in a thick cloud that falls
        // off as 1 / (1 + 0.75 depth (1 - g)) (a thick cloud's diffuse
        // transmission), not as the light that comes straight - so a cloud
        // with the sun behind it is grey through, not black within a rim of
        // light.
        // (Diffused, the deeper the more it has lost the way it came: a
        // little cloud on the way still sends it on ahead, a deep one all
        // round alike - so a heap with the sun behind it is dark in its
        // body, with a bright edge, as it is seen.)
        float takes = kCloudTakes * depth, g = 0.4 * exp(-0.15 * takes);
        float sunlit = mix(lobe(0.6, c), lobe(-0.25, c), 0.25) * exp(-takes);
        sunlit += 0.75 * mix(lobe(g, c), lobe(-0.3 * g, c), 0.25) / (1.0 + 0.75 * takes * (1.0 - 0.85));
        // Dark at a thin edge seen from the sun's side: light that gets in
        // must first be scattered by a little cloud (the powder look).
        sunlit *= mix(1.0, 1.0 - exp(-d * kCloudTakes * 100.0), 0.5 * (1.0 - c));
        // The light from all round: the sky's from above, as much as the
        // cloud over the point lets through, diffused (three steps up, each
        // longer); the ground's from below, as much as the cloud under it
        // lets - so a heap is bright on its top, dark in its folds and grey
        // at its base, as a volume is, not lit alike all through.
        float up = (cloud_at(p, h + 60.0, true) * 100.0 + cloud_at(p, h + 260.0, true) * 300.0 + cloud_at(p, h + 700.0, true) * 600.0) * kCloudTakes;
        float down = (cloud_at(p, h - 60.0, true) * 100.0 + cloud_at(p, h - 260.0, true) * 300.0) * kCloudTakes;
        vec3 around = above / (1.0 + 0.75 * up * (1.0 - 0.85)) + below / (1.0 + 0.75 * down * (1.0 - 0.85));
        vec3 here = (sunlight * sunlit + around * 0.6) * uCloudShape.z;
        float step_through = exp(-d * kCloudTakes * dt);
        light += through * here * (1.0 - step_through);
        weight += through * (1.0 - step_through), far_sum += ts * through * (1.0 - step_through);
        through *= step_through;
    }
    // Seen through the planet's air: as much of their light as gets through
    // to the eye, and the air's own light in front of them, for as much of
    // the sky as they hide.
    if (uAtmo > 0.0 && weight > 1e-4) {
        float dist = far_sum / weight, km = dist * uAtmoScale;
        float re = clamp(kGroundR + uAtmoAltitude + uEye.y * uAtmoScale, kGroundR + 0.001, kTopR - 0.01);
        vec3 near = exp(-air_here(uAtmoAltitude + (uEye.y + 0.5 * dir.y * dist) * uAtmoScale).takes * km);
        vec3 air = planet_sky(dir, sun, re) * (1.0 - near) / max(vec3(1.0) - whole_way(re, dir.y), vec3(1e-3));
        light = mix(light, (1.0 - through) * air + near * light, uAtmo);
    }
    // (Fading out towards as far as they are drawn.)
    float fade = 1.0 - smoothstep(0.6, 1.0, t0 / uCloudShape.w);
    return vec4(light * fade, mix(1.0, through, fade));
}

// A smooth noise of the plane that never repeats (values on a lattice,
// smoothed), and five octaves of it, each turned from the last: 0..1.
float cirrus_hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float cirrus_noise(vec2 p) {
    vec2 i = floor(p), f = fract(p), u = f * f * (3.0 - 2.0 * f);
    return mix(mix(cirrus_hash(i), cirrus_hash(i + vec2(1.0, 0.0)), u.x), mix(cirrus_hash(i + vec2(0.0, 1.0)), cirrus_hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float cirrus_fbm(vec2 p) {
    float sum = 0.0, a = 0.5;
    const mat2 turn = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 5; ++i) {
        sum += a * cirrus_noise(p);
        p = turn * p * 2.03 + vec2(17.1, 3.3);
        a *= 0.5;
    }
    return sum / 0.97;
}
// The same, each octave finer than a pixel holds (`fw`: how far the noise
// goes across one, at its first octave) let go to its mean - fine fibres
// far off are a haze of them, not a moire.
float cirrus_fbm(vec2 p, float fw) {
    float sum = 0.0, a = 0.5;
    const mat2 turn = mat2(0.8, -0.6, 0.6, 0.8);
    for (int i = 0; i < 5; ++i) {
        float keep = 1.0 - smoothstep(0.2, 0.55, fw);
        sum += a * mix(0.5, cirrus_noise(p), keep);
        p = turn * p * 2.03 + vec2(17.1, 3.3);
        a *= 0.5, fw *= 2.03;
    }
    return sum / 0.97;
}

// High cloud of ice the way `dir`: a thin layer, drawn out along its wind
// into streaks (the noise read stretched across the wind, short along it),
// lit by the sun through the air at its height - brightest looking towards
// it, and coloured as the low sun colours it - and the sky's light; seen
// through the air below it. Its light, and how much of what is behind it
// is seen through.
vec4 cirrus_along(vec3 dir) {
    if (uCirrus.x <= 0.0 || dir.y <= 0.0) return vec4(0.0, 0.0, 0.0, 1.0);
    float r = uPlanet + uEye.y, t = out_of(r, dir.y, uPlanet + uCirrus.y);
    vec3 p = uEye + dir * t;
    float c = cos(uCirrus.w), s = sin(uCirrus.w);
    vec2 w = vec2(c * p.x + s * p.z, -s * p.x + c * p.z) + vec2(c * uCirrusDrift.x + s * uCirrusDrift.y, -s * uCirrusDrift.x + c * uCirrusDrift.y);
    // Made of a noise that never repeats (a lattice's values, smoothed,
    // octave over octave each turned from the last) - no picture tiled
    // over the sky. Its streaks long along the wind and fine across it,
    // ridged so each is a fibre with an edge; bent a little as the wind up
    // there bends; in patches with the sky open between; frayed.
    float size = uCirrus.z;
    w.y += (cirrus_fbm(w / (size * 7.0) + 3.7) - 0.5) * size * 0.9;
    float patches = smoothstep(0.62 - 0.32 * uCirrus.x, 0.86 - 0.24 * uCirrus.x, cirrus_fbm(w / (size * 3.5) + 11.3));
    vec2 fine = fwidth(w);
    vec2 q1 = vec2(w.x / (size * 1.4), w.y / (size * 0.11)), q2 = vec2(w.x / (size * 0.5), w.y / (size * 0.06));
    float fw1 = max(fine.x / (size * 1.4), fine.y / (size * 0.11)), fw2 = max(fine.x / (size * 0.5), fine.y / (size * 0.06));
    float fibre = cirrus_fbm(q1 + 5.1, fw1);
    float ridge = 1.0 - abs(2.0 * fibre - 1.0);
    // (Too fine to hold, the ridged fibres are their mean: a veil.)
    float strands = mix(smoothstep(0.45, 0.95, ridge), 0.3, smoothstep(0.25, 0.6, fw1)) * (0.55 + 0.45 * cirrus_fbm(q2 + 9.9, fw2));
    float d = strands * patches * (0.45 + 0.4 * uCirrus.x);
    d *= 1.0 - smoothstep(0.55, 1.0, t / (uCloudShape.w * 2.0));
    if (d <= 1e-4) return vec4(0.0, 0.0, 0.0, 1.0);
    vec3 sun = normalize(uSunDir + vec3(0.0, 1e-4, 0.0));
    float rk = clamp(kGroundR + uAtmoAltitude + (uEye.y + uCirrus.y) * uAtmoScale, kGroundR + 0.001, kTopR - 0.01);
    vec3 sunlight = uSunColor * 10.0, around = (uSkyTop + uSkyHorizon) * 0.6;
    if (uAtmo > 0.0) {
        sunlight = mix(sunlight, uSunColor * uAtmoSun * light_through(rk, sun.y), uAtmo);
        around = mix(around, planet_sky(vec3(0.0, 1.0, 0.0), sun, rk), uAtmo);
    }
    // Ice: much on ahead, a little all round.
    float cs = dot(dir, sun);
    vec3 light = (sunlight * mix(lobe(0.6, cs), 0.08, 0.4) * 0.35 + around * 0.5) * d;
    if (uAtmo > 0.0) {
        float re = clamp(kGroundR + uAtmoAltitude + uEye.y * uAtmoScale, kGroundR + 0.001, kTopR - 0.01);
        vec3 through_air = whole_way(re, dir.y);
        light = mix(light, light * through_air + planet_sky(dir, sun, re) * (1.0 - through_air) * d * 0.25, uAtmo);
    }
    return vec4(light, 1.0 - d);
}

void main() {
    ivec2 px = ivec2(gl_FragCoord.xy);
    vec4 last = texelFetch(uDomeLast, px, 0);
    if (uDomePart >= 0 && (px.x % 4) + 4 * (px.y % 4) != uDomePart) {
        FragColor = last;
        return;
    }
    float jitter = fract(52.9829189 * fract(dot(vec2(px) + uSpin * vec2(5.588238, 3.1416), vec2(0.06711056, 0.00583715))));
    vec3 dir = dome_dir((vec2(px) + 0.5) / uDomeSize);
    vec4 now = uCloudLayer.z > 0.0 ? clouds_along(dir, jitter) : vec4(0.0, 0.0, 0.0, 1.0);
    // (The cirrus behind them, high over all of it.)
    vec4 high = cirrus_along(dir);
    now = vec4(now.rgb + now.a * high.rgb, now.a * high.a);
    FragColor = uDomeKeep > 0.0 ? mix(now, last, uDomeKeep) : now;
}
)";
    return source.c_str();
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
uniform vec4  uBoxAt[24];    // each box's middle, and its turn (as many as kMaxProbes)
uniform vec3  uBoxHalf[24];
uniform vec3  uBoxLo[24];
uniform vec3  uBoxHi[24];
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
        for (int i = 0; i < 24; ++i) {
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
in vec4 vTangent;
in vec4 vMeshWear;

// Whether this point is seen is known before it is shaded: what stands in
// front is never lit for nothing. (Not where a picture is cut out of its
// card - SG_CUTOUT, the program those are drawn with - since what is cut away
// must not hide what is behind it.)
#if !defined(SG_CUTOUT) && defined(GL_ARB_shader_image_load_store)
layout(early_fragment_tests) in;
#endif
layout(location = 0) out vec4 FragColor;
// What a ray needs of this surface (src/gl/trace), into a second picture
// where the eye's view asks for one (gl::RenderTarget::second) - as
// uSurfaceOnly 5 writes it; nothing where nothing of a surface is. All that
// is traced is in the program made for a view traced (SG_TRACED:
// GLWorldView::traced_of): one that is not has none of it, and pays nothing.
#ifdef SG_TRACED
layout(location = 1) out vec4 TraceSurf;
#else
vec4 TraceSurf;
#endif
// How this pixel moved since the last frame, in pixels (the jitter left
// out): a thing that says where it stood last frame (uMoving 1) by that,
// all else written as not known (1e4), for the view's own motion to be
// found from depth after (GLWorldView::run_motion). Only in the program for
// a view whose motion is asked for (SG_VELOCITY: GLWorldView::moving_of).
// Each pixel's diffuse colour, as much of it as the light from all round
// reaches (its own hollows, its air, its dimming), for the light the
// cascades find bounced onto it from what is near (GLWorldView::run_cascades).
// Only in the program for a view that asks (SG_DIFFUSE: GLWorldView::diffuse_of).
#ifdef SG_DIFFUSE
layout(location = 3) out vec4 Diffuse;
#else
vec4 Diffuse;
#endif
#ifdef SG_VELOCITY
layout(location = 2) out vec4 Motion;
in vec4 vNowClip;
in vec4 vWasClip;
uniform float uMoving;
uniform vec2 uMotionSize;  // the picture's pixels
#endif

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
uniform float uTileBreak;     // with uSkin 2: how much a second laying of it, turned and larger, breaks its repeats (0..1)
uniform vec4  uLand;          // a land's own: where its water stands (y, its room's frame), how far up from it the ground is damp (0: none);
                              // its sand's wind ripples (surface 8): how far apart (metres; 0: none), the way the wind blew (radians from +x towards +z)
uniform float uSkinTile;      // with uSkin 1: metres of the thing a cell covers, its pattern going on round it; 0: a face the whole cell
uniform vec3  uSkinSize;      // with uSkinTile: the thing's size
uniform float uSkinBlend;     // with uSkin 1: 0 a face its own cell; 1 a curve soft between the cells of the ways it faces
// A worn texture's dirt (Texture::dirt_raster: colour, and its grains'
// height in a): how much (x), how far from a corner or an edge it gathers
// (y, metres), metres to a picture (z), how rough (w); how high up from the
// floor (uDirtBand.x) and down from the ceiling (y) it reaches, and how much
// lies everywhere besides (z). uRoomBox: the room's own box (0: none).
uniform sampler2D uDirt;
uniform vec4 uDirtSet;
uniform vec4 uDirtBand;       // w: how much settles on what faces up (dust, by gravity)
uniform vec3 uRoomBox;
// Rooms made one (sg::make_space): the floor each covered (x0 z0 x1 z1) and
// its height, up to four - its walls and corners where each room's were.
uniform vec4 uRegion[4];
uniform vec4 uRegionH;
uniform int uRegions;
// (Packed, as the shader's registers are few: x dirt worn, y damp worn, z
// the room's age, w how many of its leaks are given.)
uniform vec4 uWear;
// Its damp (Texture::wear_raster `damp`): where water has been - under its
// room's leaks, rising from the floor (y, metres) - how much (x), metres to a
// picture (z), how rough it leaves the surface (w). The room's causes (Wear):
// its age (0 new .. 1 long neglected), its leaks (xyz in its frame, reach).
uniform sampler2D uDamp;
uniform vec4 uDampSet;
uniform float uSheen;      // a pile's sheen: velvet, plush carpet (0: none)
uniform vec2 uDampCover;   // x 0: water - the surface darkened by its stain; 1: the photograph laid over as it is (blood, paint); y how much it runs in thin runs (0 a sheet)
uniform vec4 uLeak[8];
// Its room's wear fields (sg::field_picture): dirt in r, damp in g, over a
// rectangle of its floor (x0, z0, 1/w, 1/d; w 0: none) - each point's share
// added to what its causes gather there.
uniform sampler2D uWearMap;
uniform vec4 uWearField;
// What its own shape asks (vMeshWear, a mesh's wear made from it): dirt kept
// in its hollows (x) and creases (y); its edges worn - how much (uEdge.x),
// how much paler (y, times its colour), how rough (z); and moss, a
// photograph of it (Texture::wear_raster `moss`): how much (uMossSet.x),
// metres to a picture (y), how rough (z), w 1 worn - gathered where it faces
// up (uMossWhere.x), where it faces its shaded way (y; the way, w radians
// from +x towards +z), where it is shut in (z). uVary: its colour (x) and
// roughness (y) varying over the world, in patches z metres across.
uniform vec4 uMeshDirt;
uniform vec4 uEdge;
uniform sampler2D uMoss;
uniform vec4 uMossSet;
uniform vec4 uMossWhere;
uniform vec4 uVary;
vec2 wear_field() {
    if (uWearField.z <= 0.0) return vec2(0.0);
    vec2 uv = (vRoom.xz - uWearField.xy) * uWearField.zw;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) return vec2(0.0);
    // (A field of the floor: what lies on it - a rug, a runner on it - and
    // up what stands on it only as far as it splashes or is kicked, a hand's
    // breadth; not up a wall to the ceiling over where it lies, nor on a
    // table's top over it.)
    float up = 1.0 - smoothstep(0.03, 0.12, vRoom.y);
    return texture(uWearMap, uv).rg * up;
}
// A tiled skin's repeat never shown (Texture `scatter`, 0..1: x): each
// hexagon of it read from a place of its own, turned within y radians
// (`scatter_turn`; 0: as laid - a grain, a grid of tiles).
uniform vec2 uSkinScatter;
uniform float uSkinOne;       // with uSkin 1: the texture is one tile, the same for every way (Texture::one_tile), not six cells
uniform float uSkinFramed;    // 1: projected in the frame of what wears it (uSkinFrame) - the thing, all its parts as one
uniform mat4  uSkinFrame;     // the room's frame to that thing's unit box
uniform float uSkinOwn;       // 1: the frame is the mesh's own (drawn with others of its shape, each its own size)
uniform float uSkinRelief;    // > 0: the skin's alpha is how high its paint stands, this many metres at 1 - the surface bent by it
uniform vec4  uUVRect;        // a cell of the picture: its corner, its size (unused while its size is 0)
uniform float uCutout;        // > 0: pixels clearer than this are not drawn (a sprite at 0.5; a thing's `cutout` - a leaf on a card)
uniform float uSubsurface;    // > 0: skin - light through it, scattered (a character's `subsurface`; how much, texel by texel, its picture's alpha)
uniform float uTwoSided;      // 1: thin, seen from either side - lit on the side it is seen from (`two_sided`)
uniform float uTranslucent;   // and how much of a lamp's light behind it comes through (`translucent`: a leaf)
uniform float uGlow;          // extra emission for an active interface
uniform float uFillCut;       // how much of the light from all round (and back off the walls) is taken away: 0 as it is, 1 none - lamps' pools in the dark
uniform float uUnlit;         // 1: what it shows arrives already lit - no light of the room on it (a panel that says `lit` 0)
uniform float uAdd;           // 1: light added over what is behind it (a panel that says `add`) - the air dims it, adds nothing to it
uniform float uGlass;         // > 0: glass, this clear - what is behind it seen through it, more of it reflected at a glance (Fresnel)
)" + lights_glsl() + atmosphere_glsl() + cloud_dome_glsl() + fog_volumes_glsl() + R"(
// The clouds as a volume (a look's `clouds.cover`): the sky above with them
// in it (cloud_dome_fs) - their light, and how much is seen through them.
uniform float uCloudVolume;
uniform sampler2D uCloudDome;
// The sky, and the air between the eye and what it sees, as a planet's (a
// look's `atmosphere`): this much of them, 0 none.
uniform float uAtmo;
uniform float uAtmoScale;     // kilometres of the planet to a metre of the world
uniform float uAtmoAltitude;  // how high the world's 0 is over its ground, km
uniform float uAtmoSun;       // how bright the sun is in it
uniform sampler2D uAtmoSky;   // the sky seen from the eye every way (atmosphere_sky_fs)
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
// Air thinner the higher it is (0: the same everywhere): from `uFogFloor`
// up it thins by e for every 1 / `uFogFalloff` metres, below it is as thick
// as `uFogDensity` says. How much of the way from `a` to `b` it is, against
// air as thick everywhere: its thickness along the way, integrated.
uniform float uFogFalloff;
uniform float uFogFloor;
float air_height(vec3 a, vec3 b) {
    if (uFogFalloff <= 0.0) return 1.0;
    float ya = max(a.y - uFogFloor, 0.0), yb = max(b.y - uFogFloor, 0.0);
    float k = uFogFalloff, dy = (yb - ya) * k;
    float at = exp(-min(ya * k, 80.0));
    // (Level ways: the thickness where they are; else exp's mean between.)
    return abs(dy) < 1e-3 ? at * (1.0 - 0.5 * dy) : at * (1.0 - exp(-dy)) / dy;
}
// Sunlight the air scatters towards the eye: by Henyey and Greenstein's
// phase, `uFogSunG` how much of it goes on forward (0: the old glow
// round the sun), `uFogSunAmount` how much.
uniform float uFogSunG;
uniform float uFogSunAmount;
float sun_phase(float c) {
    float g = uFogSunG, d = 1.0 + g * g - 2.0 * g * c;
    return (1.0 - g * g) / (d * sqrt(d));
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
const int MAX_PROBES = 24;
uniform int  uProbeCount;
// 1: they are an open land's, round the eye - they saw what the ground gives
// back, and where they hold the look's stand-in for it (uGround) gives way.
uniform float uProbeOpen;
// What a surface is, not how it is lit (relight_probes): 1 where it is, 2
// which way it faces, 3 what of the light it scatters, 4 how far it is
// from the eye; 5 all a ray needs of it (src/gl/trace): which way it faces
// (an octahedron's face), how far, how rough. 0: lit, as ever.
uniform int  uSurfaceOnly;
// Light traced along rays through the world, where the eye's look asks
// (src/gl/trace, docs/trace.md) - each a picture of the eye's view, read at
// this pixel where it is of this surface (uTraceSurf: how far each pixel's
// was): x, the sun's light through what stands between (its shadow); z,
// what is mirrored, by surfaces no rougher than it; w, the light from all
// round traced (uTraceGI: what a surface facing its way gets, over pi, and
// how many frames its history holds) - 1 in a room shut round (the look's
// own light from all round kept beside it), 2 under an open sky (the rays
// found the sky themselves). All 0: none of it, lit as ever. (Only in the
// program for a view traced: SG_TRACED.)
#ifdef SG_TRACED
uniform vec4 uTrace;
uniform vec3 uTraceSunDir;      // the sun it traced to
uniform mat4 uTraceVP;          // the view it was traced for, and from where
uniform vec3 uTraceEye;
uniform sampler2D uTraceSurf;
uniform sampler2D uTraceSun;
uniform sampler2D uTraceMirror;
uniform sampler2D uTraceGI;
// y: how many lamps' shadows were traced (uTraceLamps r, g, b), each of the
// lamp standing at uTraceLamp[k].
uniform vec3 uTraceLamp[3];
uniform sampler2D uTraceLamps;
// Which of the traced lamps lamp i is (-1: none of them).
int traced_lamp(int i) {
    for (int k = 0; k < 3; ++k)
        if (float(k) < uTrace.y && distance(uLightPos[i], uTraceLamp[k]) < 0.01) return k;
    return -1;
}
#endif
uniform vec4 uProbeAt[MAX_PROBES];
uniform highp usampler2D uProbeIndex;  // the boxes reaching each cell of the floor plan (GLWorldView::probe_index)
uniform vec4 uProbeGrid;               // its origin x, z, 1 / its cell, and 1 where there is one
uniform vec2 uProbeGridSize;  // its cells across and deep
uniform vec3 uProbeHalf[MAX_PROBES];
uniform vec3 uProbeSoftLo[MAX_PROBES];
uniform vec3 uProbeSoftHi[MAX_PROBES];
uniform vec3 uProbeSH[MAX_PROBES * 9];
// Reflection captures (GLWorldView::capture_rooms): what each probe box of
// the room sees every way from its middle, lit as the room stands - a layer
// a box, as the probes are numbered - and how the room's box lies, for what
// is mirrored to be read where it stands on it (box-projected): its corner
// where it is drawn and its turn (uCaptureAt), its near and far corners in
// its own frame (uCaptureLo, uCaptureHi), and how many boxes hold one
// (uCaptureHi.w; 0: none, the probes' as ever).
uniform sampler2DArray uCaptures;
uniform vec4 uCaptureAt;
uniform vec3 uCaptureLo;
uniform vec4 uCaptureHi;
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
uniform float uShowMirror;   // (SG_SHOW_MIRROR: where a mirror is laid, magenta)
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
// share of uWaterDeepest metres, deepening to uWaterDeep. uSplat 3 - ground
// laid with photographed beds (sg::terrain's `bed0..3`): uTex each bed's
// share as for 1, uLayerSurface each bed's metres to a picture (below 0: no
// bed), uLayerColor its tint; uBeds their colours (alpha how high their
// grains stand), uBedNormals their normals (alpha roughness), a layer a bed.
// uLines: a road's painted lines (asphalt, by its uv: across 0..1, along in
// metres).
uniform float uSplat;
uniform vec4  uSplatRect;
uniform vec4  uLayerSurface;
uniform vec3  uLayerColor[4];
uniform sampler2DArray uBeds;
uniform sampler2DArray uBedNormals;
uniform vec4  uBedTurn;       // how far each bed's patches may be turned (radians): its pattern's way kept to within it
uniform vec3  uWaterDeep;
uniform float uWaterDeepest;
uniform float uLines;
uniform float uCalm;          // water: 0 its full swell, 1 still
const int MAX_RIPPLES = 16;
uniform int  uRippleCount;    // water: the rings spreading on it (sg::ripple) -
uniform vec4 uRipple[MAX_RIPPLES];  // each where it began (x, z of its room), when (its world's time), how strong

// The material this fragment is of: the uniforms, or - drawn as one of many
// at once - what its instance carries.
flat in vec4 vMat0;
flat in vec4 vMat1;
flat in float vInstanced;
flat in float vMetal;
flat in float vGrime;
uniform float uMetal;         // how metal, 0..1: reflects in its own colour, scatters none
uniform float uGrime;         // `grime` 0..1: how lived-in - the unevenness a room's surface has at the scale of the room
vec3 mAlbedo;
float mGrime;
float mRoughness, mSurface, mEmissive, mHighlight, mMirror, mMetal;
// A worn texture's surface map (Texture::surface_raster): red how open its
// hollows are to the light from all round, green roughness, blue metal -
// what the thing is there, in place of its own roughness and metal.
uniform sampler2D uSurfaceMap;
uniform float uSurfaceMapOn;
// A worn texture's normal map (Texture::normal_raster): which way its surface
// faces on each cell, x along the cell's u, y up its v (OpenGL's way), z out
// - drawn in place of the screen's guess from its relief; how strongly.
uniform sampler2D uNormalMap;
uniform float uNormalMapOn;
uniform float uNormalStrength;
// A thing's own pictures read through its corners' uv (not a texture's cells:
// uSkinFramed 0) are bound to the same two units: its surface map (`skin_surface`)
// and normal map (`skin_normal`), the normal laid along its corners' u and v
// (vTangent, or the screen's), green up its picture (-1, OpenGL's way: the uv's
// v runs down it) or down (1, DirectX's: `normal_dx`).
uniform float uNormalFlipY;
uniform float uTexFlip;       // 1: the picture's rows run bottom up (a rendered one)
uniform float uUntone;        // 1: the picture is already developed (a world's feed): undo the tone curve
uniform float uUntoneGain;    // and how bright it is shown: a screen's `bright` (a set's tube in the place's light scale)

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
uniform float uDimNear;       // how far the room is dimmed round what is held up close to read (a look's, as uDim is)
uniform float uUndim;         // 1: this is part of the screen the room dims round, not dimmed with it
// How flat the tube is seen (crt_shape): 0 as it is, 1 face up to the glass.
uniform float uFlat;
// A round tube (x: 0 square, 1 round), and the panel's width over its height (y).
uniform vec2  uRound;
)") + crt_glsl_constants() + air_glsl() + capture_glsl() + R"(
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
    // (Round: the circle as high as the panel, in the middle of it - and
    // nothing outside its rim: what it is set in shows there.)
    vec2 aspect = vec2(max(uRound.y, 1e-3), 1.0);
    glass_sd = mix(glass_sd, length(g * aspect) - 1.0, uRound.x);
    if (uRound.x > 0.5 && length(g * aspect) > 1.06) discard;
    float gpx = max(fwidth(glass_sd), 1e-5);
    float glass = 1.0 - smoothstep(-gpx, gpx, glass_sd);
    // The picture bows out with the tube and sits wholly inside the glass,
    // with a dark margin, the way a real one does - no corner of it is lost.
    float r2 = dot(g, g);
    vec2 w = g * (1.0 + kBulge * (1.0 - uFlat) * uCRT * r2) / mix(kFit, 1.0, uFlat);
    float pic_sd = mix(max(abs(w.x), abs(w.y)), length(w * aspect), uRound.x) - 1.0;
    float ppx = max(fwidth(pic_sd), 1e-5);
    float picture = 1.0 - smoothstep(-ppx, ppx, pic_sd);
    vec2 s = clamp(w * 0.5 + 0.5, 0.0, 1.0);
    // (Round: the picture's own middle, at its own shape - not squeezed to
    // the panel's.)
    if (uRound.x > 0.5) s.x = clamp(0.5 + (s.x - 0.5) * uRound.y / max(uTexSize.x / max(uTexSize.y, 1.0), 1e-3), 0.0, 1.0);
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
    if (uSkinOne > 0.5) return vec2(u, 1.0 - v);
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

// That point of that cell of the atlas `t` (the picture, or its surface map:
// laid alike), filtered as the unwrapped place changes across the screen (no
// seam where a tiling wraps).
// How many texels a picture is seen at, at most, across a cell (or the whole
// picture of a model's own): an older machine's, each a square of its colour,
// no smoothing between (0: as it is).
uniform float uTexels;
vec2 texel_snap(vec2 f, float n) { return n > 0.0 ? (floor(f * n) + 0.5) / n : f; }
vec4 skin_cell(sampler2D t, float cell, vec2 uv, vec2 dx, vec2 dy) {
    vec2 f = clamp(uSkinTile > 0.0 ? fract(uv) : uv, 0.002, 0.998);
    f = texel_snap(f, uTexels);
    // One tile, every way's (Texture::one_tile): the whole picture is the cell.
    if (uSkinOne > 0.5) return textureGrad(t, vec2(f.x, 1.0 - f.y), dx * vec2(1.0, -1.0), dy * vec2(1.0, -1.0));
    float col = mod(cell, 3.0), row = floor(cell / 3.0);
    vec2 k = vec2(1.0 / 3.0, -0.5);
    return textureGrad(t, vec2((col + f.x) / 3.0, (row + 1.0 - f.y) / 2.0), dx * k, dy * k);
}
// A tiled skin read without its repeat ever showing (uSkinScatter): the
// face cut into hexagons, two to a tile across (Mikkelsen's hex-tiling, as
// a land's beds are laid), each reading the picture from a place of its own,
// turned within uSkinScatter.y and a little larger or smaller, the three a point
// lies between weighed as near as it is to each - the same weights for every
// map of it, so its colour, its bumps and its roughness stay together. A
// normal map's way is turned back as its picture was turned (`normal`).
// Untiled, or not scattered: the picture as laid.
vec4 skin_sample_as(sampler2D t, float cell, vec2 uv, vec2 dx, vec2 dy, bool normal) {
    if (uSkinScatter.x <= 0.0 || uSkinTile <= 0.0) return skin_cell(t, cell, uv, dx, dy);
    vec2 st = uv * 3.4641016;
    vec2 skew = vec2(st.x, -0.57735027 * st.x + 1.15470054 * st.y);
    vec2 base = floor(skew);
    vec3 f = vec3(fract(skew), 0.0);
    f.z = 1.0 - f.x - f.y;
    float s = step(0.0, -f.z), s2 = 2.0 * s - 1.0;
    vec3 w = vec3(-f.z * s2, s - f.y * s2, s - f.x * s2);
    vec2 at[3] = vec2[](base + vec2(s, s), base + vec2(s, 1.0 - s), base + vec2(1.0 - s, s));
    // (Narrower blends the more it scatters: each hexagon its own the more.)
    float sharp = mix(2.0, 5.0, clamp(uSkinScatter.x, 0.0, 1.0));
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 3; ++i) {
        vec2 q = at[i] + cell * 3.17;
        vec2 r = fract(sin(vec2(dot(q, vec2(127.1, 311.7)), dot(q, vec2(269.5, 183.3)))) * 43758.5453);
        float a = (r.x * 2.0 - 1.0) * uSkinScatter.y, scale = 1.0 + 0.15 * uSkinScatter.x * (fract(r.y * 13.7) * 2.0 - 1.0);
        mat2 turn = mat2(cos(a), sin(a), -sin(a), cos(a));
        vec4 c = skin_cell(t, cell, turn * uv * scale + r * 7.0, turn * dx * scale, turn * dy * scale);
        if (normal) c.xy = transpose(turn) * (c.xy * 2.0 - 1.0) * 0.5 + 0.5;
        float wi = pow(w[i], sharp);
        sum += c * wi, wsum += wi;
    }
    return sum / max(wsum, 1e-5);
}
vec4 skin_sample(sampler2D t, float cell, vec2 uv, vec2 dx, vec2 dy) { return skin_sample_as(t, cell, uv, dx, dy, false); }

// Where a point of a thing wearing a skin is on it: the thing's size, the
// cells of the three ways it faces (x, y, z) and how much each counts, the
// point on each cell and how that moves across the screen.
struct SkinAt {
    vec3 size, w, cell;
    vec2 u[3], dx[3], dy[3];
};
SkinAt skin_here() {
    SkinAt s;
    // In the unit box of the thing that wears it, facing as it truly faces
    // there (its normal scaled back out of the box).
    s.size = uSkinOwn > 0.5 ? vTexScale : uSkinSize;
    vec3 p = uSkinOwn > 0.5 ? vLocal : (uSkinFrame * vec4(vRoom, 1.0)).xyz;
    vec3 n = normalize(uSkinOwn > 0.5 ? vObjNormal / max(s.size, vec3(1e-4)) : uSkinSize * (mat3(uSkinFrame) * vRoomNormal)), a = abs(n);
    s.cell = vec3(n.x > 0.0 ? 0.0 : 1.0, n.y > 0.0 ? 4.0 : 5.0, n.z > 0.0 ? 2.0 : 3.0);
    for (int i = 0; i < 3; ++i) {
        s.u[i] = skin_at(s.cell[i], p, s.size);
        s.dx[i] = dFdx(s.u[i]), s.dy[i] = dFdy(s.u[i]);
    }
    vec3 w = pow(a, vec3(mix(24.0, 2.0, clamp(uSkinBlend, 0.0, 1.0))));
    if (uSkinBlend <= 0.0) w = a.x >= a.y && a.x >= a.z ? vec3(1, 0, 0) : (a.z >= a.y ? vec3(0, 0, 1) : vec3(0, 1, 0));
    s.w = w / max(w.x + w.y + w.z, 1e-5);
    return s;
}

// A skin worn by any shape: each point from the cell of the way it most
// faces, or (uSkinBlend) from those of the ways it half faces, weighted.
vec4 skin_texel(sampler2D t) {
    SkinAt s = skin_here();
    vec4 c = vec4(0.0);
    for (int i = 0; i < 3; ++i)
        if (s.w[i] > 0.001) c += s.w[i] * skin_sample(t, s.cell[i], s.u[i], s.dx[i], s.dy[i]);
    return c;
}

// The metres of the thing a cell's u and v go across from 0 to 1: the tile
// when it tiles, else the face it is laid on (as skin_at lays them).
vec2 cell_metres(float cell, vec3 size) {
    if (uSkinTile > 0.0) return vec2(uSkinTile);
    if (cell < 1.5) return vec2(size.z, size.y);
    if (cell < 3.5) return vec2(size.x, size.y);
    return vec2(size.x, size.z);
}

// A skin's normal map as how its height changes from one pixel to the next
// across the screen (x, y), in metres: each cell's normal a slope - metres
// of height a metre across - carried onto the screen by how its place on the
// cell moves there, and the cells weighed as their colours are. A height's
// change is what the surface gradient bends a normal by (main), so no
// tangents are needed: any shape, any projection, each face its own way up.
vec2 skin_height_steps() {
    SkinAt s = skin_here();
    vec2 steps = vec2(0.0);
    for (int i = 0; i < 3; ++i) {
        if (s.w[i] <= 0.001) continue;
        vec3 t = skin_sample_as(uNormalMap, s.cell[i], s.u[i], s.dx[i], s.dy[i], true).xyz * 2.0 - 1.0;
        vec2 per_uv = -t.xy / max(t.z, 0.1) * cell_metres(s.cell[i], s.size) * uNormalStrength;
        steps += s.w[i] * vec2(dot(per_uv, s.dx[i]), dot(per_uv, s.dy[i]));
    }
    return steps;
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
// A picture read where it is laid - and, laid over the world with
// `uTileBreak`, never the same twice: each cell of it (a picture's size)
// read from a place of the picture of its own, chosen by the cell, the
// cells' edges crossing over each other in a narrow band (a fifth of a cell,
// the picture's own steps kept so its levels hold) - so no cell is its
// neighbour again and nothing lines up from far off. (Its features keep
// their way: wind's ripples run as the wind does in every cell.)
vec2 tile_hash(vec2 p) { return fract(sin(vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)))) * 43758.5453); }
vec4 tiled(sampler2D s, vec2 uv) {
    if (uTileBreak <= 0.0 || uSkin < 1.5 || uSkin > 2.5) return texture(s, uv);
    vec2 i = floor(uv), f = fract(uv), dx = dFdx(uv), dy = dFdy(uv);
    vec4 a = textureGrad(s, uv + tile_hash(i), dx, dy);
    vec4 b = textureGrad(s, uv + tile_hash(i + vec2(1.0, 0.0)), dx, dy);
    vec4 c = textureGrad(s, uv + tile_hash(i + vec2(0.0, 1.0)), dx, dy);
    vec4 d = textureGrad(s, uv + tile_hash(i + vec2(1.0, 1.0)), dx, dy);
    float band = 0.1 + 0.4 * (1.0 - uTileBreak);
    // (The second neighbour of each cell is the one after it: each cell's
    // own reading holds from its middle, and gives way at its far edge.)
    vec2 m = smoothstep(vec2(0.0), vec2(1.0), (f - 0.5 + band) / (2.0 * band));
    return mix(mix(a, b, m.x), mix(c, d, m.x), m.y);
}

// The eye's distance from the planet's middle, in its kilometres (kept
// within its air).
float atmo_eye() { return clamp(kGroundR + uAtmoAltitude + uViewPos.y * uAtmoScale, kGroundR + 0.001, kTopR - 0.01); }
// The planet's sky seen from the eye that way, as bright as its sun.
vec3 atmo_sky(vec3 dir) {
    vec3 sun = normalize(uSunDir + vec3(0.0, 1e-4, 0.0));
    return texture(uAtmoSky, unit_to_texel(sky_view_uv(dir, sun, atmo_eye()), vec2(textureSize(uAtmoSky, 0)))).rgb * uSunColor * uAtmoSun;
}
// The planet's air between the eye and what it sees `len` metres off that
// way: as much of what is there as gets through, and as much of the sky's
// light that way as this stretch of the air has of the whole way's.
vec3 atmo_between(vec3 color, vec3 dir, float len) {
    float r = atmo_eye(), km = len * uAtmoScale;
    vec3 near = exp(-air_here(r - kGroundR + 0.5 * dir.y * km).takes * km);
    vec3 far = whole_way(r, dir.y);
    vec3 light = atmo_sky(dir) * (1.0 - near) / max(1.0 - far, vec3(1e-3));
    return mix(color, color * near + light, uAtmo);
}

// The sun's own disc seen `s` (the cosine) off its middle: as wide as the
// sun is from here (half a degree across), its edge darker than its middle
// as a star's limb is, its edge a pixel soft - and far brighter than any
// sky (its glow round it is the lens's and the haze's, not the disc's).
float sun_disc(float s) {
    const float r = 0.00465;  // radians, its radius
    float a = sqrt(max(2.0 * (1.0 - s), 0.0));  // radians off its middle (small)
    float edge = max(fwidth(a), 2e-4);
    float on = 1.0 - smoothstep(r - edge, r + edge, a);
    float mu = sqrt(max(1.0 - (a * a) / (r * r), 0.0));
    return on * (1.0 - 0.6 * (1.0 - mu)) * 600.0;
}

vec3 sky(vec3 dir) {
    float t = clamp(dir.y, -1.0, 1.0);
    vec3 c = mix(uSkyHorizon, uSkyTop, pow(max(t, 0.0), 0.45));
    c = mix(c, uSkyHorizon * 0.8, clamp(-t * 5.0, 0.0, 1.0));  // below the horizon, haze
    float s = max(dot(dir, normalize(uSunDir + vec3(0.0, 1e-4, 0.0))), 0.0);
    c += uSunColor * (sun_disc(s) + pow(s, 24.0) * 0.35 + pow(s, 4.0) * 0.1);
    // The planet's own sky, as much of it as the look says: its sun's disc
    // as much of the sun as gets through the air to the eye (its glow round
    // it is the haze's, in the sky view).
    vec3 sunlit = uSunColor;
    if (uAtmo > 0.0) {
        vec3 through = light_through(atmo_eye(), dir.y);
        c = mix(c, atmo_sky(dir) + uSunColor * through * sun_disc(s), uAtmo);
        sunlit = mix(sunlit, uSunColor * light_through(atmo_eye(), normalize(uSunDir + vec3(0.0, 1e-4, 0.0)).y), uAtmo);
    }
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
        // (Under the planet's sky, lit by as much of the sun as gets through
        // its air: gold low in the evening.)
        cloud += sunlit * (pow(toward, 6.0) * 0.8 + pow(toward, 40.0) * 1.2) * (1.0 - thick * 0.6);
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
    // The clouds as a volume, over all of it: as much of the sky behind them
    // as is seen through them, and their light (thinning into the horizon).
    if (uCloudVolume > 0.0 && dir.y > 0.0) {
        vec4 cloud = texture(uCloudDome, dome_uv(dir));
        c = mix(c, c * cloud.a + cloud.rgb, uCloudVolume * smoothstep(0.0, 0.03, dir.y));
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
    // Under a planet's sky (`atmosphere`), the light from all round is the
    // light that sky gives, not the look's colours: its radiance over the
    // upper half, as a face turned up takes it (the zenith and four ways at
    // 35 degrees, cosine-weighed) - as bright as the sun that lights the
    // surfaces (its colour times its power), not as the sky is drawn (times
    // `atmosphere.sun`); and from below, the ground's: what it gives back
    // (`atmosphere.ground`) of the sun through the air and that sky.
    if (uAtmo > 0.0) {
        float power = 0.0;
        for (int i = 0; i < MAX_LIGHTS; ++i) {
            if (i >= uLightCount) break;
            if (uLightA[i].w > 0.5) {
                power = uLightA[i].x;
                break;
            }
        }
        float k = power / max(uAtmoSun, 1e-4);
        const float c = 0.8192, s = 0.5736;
        vec3 l = atmo_sky(vec3(0.0, 1.0, 0.0)) * 0.36 +
                 (atmo_sky(vec3(c, s, 0.0)) + atmo_sky(vec3(-c, s, 0.0)) + atmo_sky(vec3(0.0, s, c)) + atmo_sky(vec3(0.0, s, -c))) * 0.16;
        l *= k;
        vec3 sun = normalize(uSunDir + vec3(0.0, 1e-4, 0.0));
        vec3 g = uAtmoGround * (uSunColor * power * light_through(atmo_eye(), sun.y) * max(sun.y, 0.0) / kPi + l);
        sky = mix(sky, l, uAtmo);
        ground = mix(ground, g, uAtmo);
    }
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
    // (Out of its reach - its box and the blend round it, a ball round both -
    // nothing: no turn worked out for a box the point is nowhere near.)
    vec3 reach = uProbeHalf[i] + max(uProbeSoftLo[i], uProbeSoftHi[i]);
    if (dot(d, d) > dot(reach, reach)) return 0.0;
    float c = cos(at.w), s = sin(at.w);
    vec3 q = vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
    vec3 lo = q + uProbeHalf[i], hi = uProbeHalf[i] - q;
    vec3 a = clamp(1.0 + lo / max(uProbeSoftLo[i], vec3(1e-4)), 0.0, 1.0);
    vec3 b = clamp(1.0 + hi / max(uProbeSoftHi[i], vec3(1e-4)), 0.0, 1.0);
    vec3 w = a * b;
    w = w * w * (3.0 - 2.0 * w);
    return w.x * w.y * w.z;
}

// Which boxes may hold `p`: the four its cell of the floor plan names (255:
// none more), or - more than four there (254), or no map - every one.
uvec4 probe_candidates(vec3 p) {
    if (uProbeGrid.w < 0.5) return uvec4(254u);
    ivec2 c = ivec2(floor((p.xz - uProbeGrid.xy) * uProbeGrid.z));
    if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(vec2(c), uProbeGridSize))) return uvec4(255u);
    return texelFetch(uProbeIndex, c, 0);
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
    uvec4 near = probe_candidates(p);
    bool every = near.x == 254u;
    for (int k = 0; k < MAX_PROBES; ++k) {
        int i = k;
        if (!every) {
            if (k >= 4 || near[k] >= 255u) break;
            i = int(near[k]);
        }
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

// What the room's captures saw along `r` from `p` (uCaptures), for a
// surface of `roughness`: the way out followed to the room's box, and each
// capture read towards where it meets it from that capture's own middle -
// so a wall mirrored in the floor stands where the wall is, not where it
// would be seen from the box's middle - at the size blurred for that
// roughness (between the two nearest), blended as the probes are; how much
// they hold `p`, in `cover` (0: none).
const int CAPTURE_LEVELS = 6;      // GLWorldView::kCaptureLevels
const float CAPTURE_SIZE = 256.0;  // GLWorldView::kCaptureSize
vec3 capture_level(int layer, vec3 d, int k) {
    float n = CAPTURE_SIZE / float(1 << k);
    return textureLod(uCaptures, vec3(capture_uv(d, n), float(layer)), float(k)).rgb;
}
vec3 captured(vec3 p, vec3 r, float roughness, out float cover) {
    cover = 0.0;
    int count = int(uCaptureHi.w + 0.5);
    if (count <= 0) return vec3(0.0);
    float c = cos(uCaptureAt.w), s = sin(uCaptureAt.w);
    vec3 d = p - uCaptureAt.xyz;
    vec3 q = clamp(vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z), uCaptureLo + 1e-3, uCaptureHi.xyz - 1e-3);
    vec3 w = vec3(c * r.x + s * r.z, r.y, -s * r.x + c * r.z);
    vec3 inv = 1.0 / max(abs(w), vec3(1e-5)) * vec3(w.x >= 0.0 ? 1.0 : -1.0, w.y >= 0.0 ? 1.0 : -1.0, w.z >= 0.0 ? 1.0 : -1.0);
    vec3 out_at = max((uCaptureHi.xyz - q) * inv, (uCaptureLo - q) * inv);
    vec3 meets = q + w * min(min(out_at.x, out_at.y), out_at.z);
    float lod = clamp(roughness, 0.0, 1.0) * float(CAPTURE_LEVELS - 1);
    int k0 = int(floor(lod)), k1 = min(k0 + 1, CAPTURE_LEVELS - 1);
    float f = lod - float(k0);
    vec3 sum = vec3(0.0);
    float total = 0.0;
    uvec4 near = probe_candidates(p);
    bool every = near.x == 254u;
    for (int k = 0; k < MAX_PROBES; ++k) {
        int i = k;
        if (!every) {
            if (k >= 4 || near[k] >= 255u) break;
            i = int(near[k]);
        }
        if (i >= uProbeCount || i >= count) break;
        float wi = probe_weight(i, p);
        if (wi <= 0.0) continue;
        vec3 m = uProbeAt[i].xyz - uCaptureAt.xyz;
        vec3 way = normalize(meets - vec3(c * m.x + s * m.z, m.y, -s * m.x + c * m.z));
        sum += mix(capture_level(i, way, k0), capture_level(i, way, k1), f) * wi;
        total += wi;
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

// A sun's maps again, their depths as they are (not compared): how far what
// stands in its way is from what it shadows. Its half-width as the ground
// sees it (radians) - how far a shadow's edge spreads for each metre between
// what casts it and where it falls - and how far off, sunward, anything that
// casts may stand (a palm's shadow at sunset: hundreds of metres).
uniform sampler2DArray uShadowDepths;
const float kSunRadius = 0.00465;
const float kSunCasts = 400.0;

// `size`: how big the light is - a sun's disc (the tangent of its radius;
// 0: the sun's own) or a lamp's bulb (its radius, metres; 0: a point) - and
// a lamp's place (`at`): its shadow as soft as that makes it, sharp where a
// thing meets what it stands on, wide where what stands in the way is near
// the light (a cage round a bulb: a blur, not a net), as the traced light is.
float shadow_factor(int layer, vec3 n, vec3 l, float floor_, bool sun, float size, vec3 at) {
    float spread = max(uShadowSoft, 0.5);
    // Looked up a little off the surface, along its normal, by about as many
    // texels as the filter reaches - measured in metres where the point is (a
    // texel's width at a unit's distance from a lamp, or a sun's anywhere: two
    // over how many texels across the map sees a unit of its view's across),
    // not in depth: so a shadow meets what casts it - a table's foot, the
    // bottom of a bin - with no gap and no light under it, and a lit face
    // does not shadow itself.
    vec4 here = uShadowVP[layer] * vec4(vLit, 1.0);
    float texel = 2.0 * uShadowTexel.x / max(length(vec3(uShadowVP[layer][0][0], uShadowVP[layer][1][0], uShadowVP[layer][2][0])), 1e-9) * max(here.w, 1e-5);
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
    vec2 texel2 = uShadowTexel;
    float radius = 2.4 * spread;  // how far the filter reaches, in texels
    // A sun's penumbra as wide as the sun makes it: what stands in its way
    // found round here in the map, and the shadow's edge spread by how far
    // off that is - the sun's half-width for each metre: sharp where a thing
    // stands on the ground, soft at the end of a long shadow at sunset. In
    // metres, so each of its boxes draws the same shadow where one gives way
    // to the next (in texels, it was four times as soft in the next); never
    // finer than the map holds.
    if (sun) {
        float sun_r = size > 0.0 ? size : kSunRadius;
        float metres = 2.0 / max(length(vec3(uShadowVP[layer][0][2], uShadowVP[layer][1][2], uShadowVP[layer][2][2])), 1e-9);  // a whole depth of the map
        float look = clamp(sun_r * kSunCasts / texel, 2.0, 24.0);
        float found = 0.0, apart = 0.0;
        for (int i = 0; i < 16; ++i) {
            vec2 off = mat2(c, s, -s, c) * kSpiral[i] * look * texel2;
            float z = proj.z + dot(dz_duv, off) - bias;
            float d = texture(uShadowDepths, vec3(proj.xy + off, float(layer))).r;
            if (d < z) found += 1.0, apart += z - d;
        }
        if (found < 0.5) return 1.0;  // nothing in its way, near or far
        radius = clamp(apart / found * metres * sun_r / texel, 1.5 * spread, 1.5 * look);
    } else if (size > 0.0) {
        // A lamp with a bulb: what stands in its way found round here in its
        // map, and how far it is from the lamp - a depth of a perspective map
        // taken back to metres along the way from the lamp (its depth row and
        // its w row: w is the distance along the lamp's axis, z/w = a/(t b3)
        // + b2/b3 for a point t along the way) - the penumbra as wide as the
        // bulb times how much further the surface is than what shades it.
        mat4 m = uShadowVP[layer];
        vec4 r2 = vec4(m[0][2], m[1][2], m[2][2], m[3][2]), r3 = vec4(m[0][3], m[1][3], m[2][3], m[3][3]);
        vec3 way = vLit - at;
        float far_ = length(way);
        way /= max(far_, 1e-4);
        float a = dot(r2, vec4(at, 1.0)), b2 = dot(r2.xyz, way), b3 = dot(r3.xyz, way);
        float look = clamp(size / max(texel, 1e-6), 2.0, 24.0);
        float found = 0.0, near_ = 0.0;
        for (int i = 0; i < 16; ++i) {
            vec2 off = mat2(c, s, -s, c) * kSpiral[i] * look * texel2;
            float z = proj.z + dot(dz_duv, off) - bias;
            float d = texture(uShadowDepths, vec3(proj.xy + off, float(layer))).r;
            if (d < z) {
                float t = a / (b3 * (d * 2.0 - 1.0) - b2);
                if (t > 0.0 && t < far_) found += 1.0, near_ += t;
            }
        }
        if (found < 0.5) return 1.0;
        near_ /= found;
        float wide = size * max(far_ - near_, 0.0) / max(near_, 1e-3);  // metres of penumbra here
        radius = clamp(wide / max(texel, 1e-6), 1.5 * spread, 1.5 * look);
    }
    mat2 spin = mat2(c, s, -s, c) * radius;
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

// A sun's shadow, close up and far: its boxes round the eye, each wider than
// the last, its own the widest - reaching over all the eye sees - with texels
// many metres wide; a point takes the narrowest that holds it (close up, a
// texel a centimetre: a door, a leaf's edge, a chair's legs), blended into
// the next wider across its margin, before its own edge begins to fade.
float shadow_cascade(int i, vec3 n, vec3 l, float floor_) {
    // (Its size: a sun's disc or a lamp's bulb, unless it throws a framed picture.)
    float size = uLightFrame[i].w < 0.5 ? uLightFrame[i].x : 0.0;
    vec3 at = uLightPos[i];
    bool sun = uLightA[i].w > 0.5;
    int closer = uLightD[i].z < 0.5 ? closer_maps(i) : 0;
    for (int k = 0; k < 4; ++k) {
        if (k >= closer) break;
        int layer = closer_map(i, k);
        float margin = map_margin(layer, vLit);
        if (margin >= 1.0) continue;
        float here = shadow_factor(layer, n, l, floor_, sun, size, at);
        if (margin <= 0.0) return here;
        int wider = k + 1 < closer ? closer_map(i, k + 1) : int(uLightD[i].y + 0.5);
        return mix(here, shadow_factor(wider, n, l, floor_, sun, size, at), margin);
    }
    return shadow_factor(shadow_layer(i, vLit), n, l, floor_, sun, size, at);
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

// A light with a size - a disc as seen from here, `across` the tangent of
// its radius - is not lit from its middle, as a point is: each point of it
// lights the surface, and the highlight is brightest from the one whose
// half-way vector stands nearest the normal. Of the points round its rim
// (where the brightest is unless the mirror direction falls inside it, and
// then it is the brightest there is), start from the one nearest the mirror
// direction (Karis's) and take one Newton step round the rim from it
// towards the greatest (n.h)^2 - in the tangent of half the angle round, so
// that no sine, cosine or root but two is needed (de Carpentier, 2017).
// Everything from the dot products of the normal, the eye and the light's
// middle; what it gives is (n.h)^2, for GGX.
float sized_ndh2(float across, float ndl, float ndv, float vdl) {
    float c = inversesqrt(1.0 + across * across);  // the cosine of its radius
    float rdl = 2.0 * ndl * ndv - vdl;             // the mirror direction on its middle
    if (rdl >= c) return 1.0;
    // (The rim's point nearest the mirror direction: the middle turned by
    // the radius towards it, along t - the mirror direction across the
    // middle's way, made unit; n.t and v.t, times the radius's sine.)
    float k = c * across * inversesqrt(max(1.0 - rdl * rdl, 1e-7));
    float nt = k * (ndv - rdl * ndl), vt = k * (2.0 * ndv * ndv - 1.0 - rdl * vdl);
    // (And b, the third way, square to both: n.b and v.b from the volume
    // the three make.)
    float vol = sqrt(clamp(1.0 - ndl * ndl - ndv * ndv - vdl * vdl + 2.0 * ndl * ndv * vdl, 0.0, 1.0));
    float nb = k * vol, vb = k * (2.0 * vol * ndv);
    // (Where it starts: both are points of the rim, so neither is ever more
    // than the truth - and the step, on a disc as wide as a room's lamp is
    // seen from under it, can land short of where it began.)
    float nh0 = ndv + ndl * c + nt, from = nh0 * nh0 / max(2.0 * (vdl * c + vt) + 2.0, 1e-6);
    // One Newton step from there round the rim, in the tangent of half the
    // angle turned, towards the greatest (n.h)^2.
    float nlv = ndl * c + ndv + nt, vlv = vdl * c + 1.0 + vt;
    float p = nb * vlv, q = nlv * vlv, s = vb * nlv;
    float num = q * (-0.5 * p + 0.25 * vb * nlv);
    float den = p * p + s * (s - 2.0 * p) + nlv * ((ndl * c + ndv) * vlv * vlv + q * (-0.5 * (vlv + vdl * c) - 0.5));
    float twice = 2.0 * num / (den * den + num * num);
    float sn = twice * den, cs = 1.0 - twice * num;
    nt = cs * nt + sn * nb, vt = cs * vt + sn * vb;
    // (n.h)^2 from the light turned to that point.
    float nh = ndv + ndl * c + nt, hh = 2.0 * (vdl * c + vt) + 2.0;
    // (A squared cosine, never more than 1: looking almost straight at the
    // light past a surface's edge - a back-lit rail, a thin leg against a
    // low sun - the half-way vector is nearly nothing long, and divided by
    // it this would pass 1 and GGX's denominator through zero: one pixel
    // thousands of times as bright as the light gives, flashing.)
    return clamp(max(nh * nh / max(hh, 1e-6), from), 0.0, 1.0);
}

// How a heap of grains scatters a lamp's light, over the smooth surface's
// (1 for anything but sand, surface 8): each grain a little ball lit on
// one side, so the heap is lit flatter than a smooth surface would be
// (Oren and Nayar's rough diffuse, the grains' slopes spread 25 degrees),
// brighter looking away from the light than towards it; and brightest of
// all with the light straight behind the eye, where every grain hides its
// own shadow (the opposition surge, a few degrees wide).
float grains(vec3 n, vec3 l, vec3 v, float ndl, float ndv) {
    if (mSurface < 7.5 || mSurface > 8.5) return 1.0;
    const float s2 = 0.19;  // (0.436 rad)^2
    const float A = 1.0 - 0.5 * s2 / (s2 + 0.33), B = 0.45 * s2 / (s2 + 0.09);
    float s = max(dot(l, v) - ndl * ndv, 0.0);
    float rough = A + B * s / max(max(ndl, ndv), 1e-3);
    float g = acos(clamp(dot(l, v), -1.0, 1.0));
    return rough * (1.0 + 0.3 * exp(-g / 0.07));
}

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

// One bed where the ground is (uSplat 3), its picture laid along the world's
// own ways and read from each way the ground faces, as much as it faces it
// (`facing`, summing to 1): from above, its u along x and v along z; from the
// side it faces along x, u along z and v up; along z, u along x and v up. Its
// colour and how high its grains stand (`color`), and how its normal bends
// the ground's, in the world (`bend`), with how long the normal map's normal
// is there (`held`: under 1 where the map's mipmaps averaged a pattern finer
// than they keep - how much of it is not seen).
// A bed's picture laid over the ground from above without its repeat ever
// showing: the ground cut into hexagons (Mikkelsen's hex-tiling), each
// reading the picture from a place of its own, turned by an angle of its
// own (within uBedTurn: ripples keep across their wind) and a little larger
// or smaller - and where three meet, each counting as much as it is near and
// as high as its grains stand there, so the picture whose grains are on top
// shows, not a blur of three. Its colour and height (`c`), its normal turned
// with its patch (`t`, unnormalised: how long it is says what its mipmaps
// averaged away) and its roughness (`rough`).
void bed_hex(int k, vec2 uv, out vec4 c, out vec3 t, out float rough) {
    float layer = float(k);
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    // (Which three hexagons' middles this point lies between, and how near
    // each: a triangle of the grid skewed to equal sides.)
    vec2 st = uv * 3.4641016;  // two hexagons to a picture across
    vec2 skew = vec2(st.x, -0.57735027 * st.x + 1.15470054 * st.y);
    vec2 base = floor(skew);
    vec3 f = vec3(fract(skew), 0.0);
    f.z = 1.0 - f.x - f.y;
    float s = step(0.0, -f.z), s2 = 2.0 * s - 1.0;
    vec3 w = vec3(-f.z * s2, s - f.y * s2, s - f.x * s2);
    vec2 at[3] = vec2[](base + vec2(s, s), base + vec2(s, 1.0 - s), base + vec2(1.0 - s, s));
    vec4 cs[3], ms[3];
    vec3 ts[3];
    float most = 0.0;
    for (int i = 0; i < 3; ++i) {
        vec2 r = tile_hash(at[i] + float(k) * 7.31);
        float a = (r.x * 2.0 - 1.0) * uBedTurn[k], scale = 0.85 + 0.3 * fract(r.y * 13.7);
        mat2 turn = mat2(cos(a), sin(a), -sin(a), cos(a));
        vec2 q = turn * uv * scale + r * 7.0;
        cs[i] = textureGrad(uBeds, vec3(q, layer), turn * dx * scale, turn * dy * scale);
        ms[i] = textureGrad(uBedNormals, vec3(q, layer), turn * dx * scale, turn * dy * scale);
        vec3 n = ms[i].xyz * 2.0 - 1.0;
        // (Its normal turned back as its picture was turned onto the ground.)
        ts[i] = vec3(transpose(turn) * n.xy, n.z);
        w[i] = pow(w[i], 3.0) * (0.2 + cs[i].a);
    }
    w /= max(w.x + w.y + w.z, 1e-5);
    c = cs[0] * w.x + cs[1] * w.y + cs[2] * w.z;
    t = ts[0] * w.x + ts[1] * w.y + ts[2] * w.z;
    rough = ms[0].a * w.x + ms[1].a * w.y + ms[2].a * w.z;
}

void bed_at(int k, vec3 facing, vec3 g, out vec4 color, out vec3 bend, out float held, out float rough) {
    vec3 p = vRoom / uLayerSurface[k];
    color = vec4(0.0), bend = vec3(0.0), held = 0.0, rough = 0.0;
    float layer = float(k);
    if (facing.y > 0.0) {
        vec4 c;
        vec3 t;
        float r;
        bed_hex(k, p.xz, c, t, r);
        color += c * facing.y, bend += vec3(t.x, 0.0, t.y) * facing.y, held += length(t) * facing.y, rough += r * facing.y;
    }
    if (facing.x > 0.0) {
        vec4 c = texture(uBeds, vec3(p.zy, layer)), m = texture(uBedNormals, vec3(p.zy, layer));
        vec3 t = m.xyz * 2.0 - 1.0;
        color += c * facing.x, bend += vec3(0.0, t.y, t.x) * facing.x, held += length(t) * facing.x, rough += m.a * facing.x;
    }
    if (facing.z > 0.0) {
        vec4 c = texture(uBeds, vec3(p.xy, layer)), m = texture(uBedNormals, vec3(p.xy, layer));
        vec3 t = m.xyz * 2.0 - 1.0;
        color += c * facing.z, bend += vec3(t.x, t.y, 0.0) * facing.z, held += length(t) * facing.z, rough += m.a * facing.z;
    }
    color.rgb *= uLayerColor[k];
}

// A land laid with photographed beds: each where its share says, and where
// two meet, the one whose grains stand higher there showing through the
// other's (a narrow band, as sand fills the hollows of gravel) - not a fade
// of one picture into another. Over them all, the tone drifting across the
// land from hundreds of metres down to a few, so no picture's repeat lines
// up from far off (the finest of it only where a pixel holds it). Its
// colour; its roughness; its normal (`n`, from the ground's own `g`); and
// the spread of the slopes its maps average away where they are finer than
// a pixel (`unseen`, given to the roughness as the sand's ripples' are).
vec3 beds_albedo(vec3 g, out float rough, out vec3 n, out float unseen) {
    vec2 p = vRoom.xz;
    vec4 share = texture(uTex, (p - uSplatRect.xy) * uSplatRect.zw);
    // (From above where it is level, from the sides where it is steep: each
    // way's share sharpened so that only a slope reads from two.)
    vec3 facing = pow(abs(g), vec3(4.0));
    facing /= facing.x + facing.y + facing.z;
    facing = max(facing - 0.02, 0.0);
    facing /= facing.x + facing.y + facing.z;
    vec4 color[4];
    vec3 bend[4];
    float held[4], rr[4], stands[4];
    float top = -1e9;
    for (int k = 0; k < 4; ++k) {
        stands[k] = -1e9;
        if (uLayerSurface[k] <= 0.0 || share[k] < 0.004) continue;
        bed_at(k, facing, g, color[k], bend[k], held[k], rr[k]);
        stands[k] = share[k] + 0.6 * color[k].a;
        top = max(top, stands[k]);
    }
    vec3 sum = vec3(0.0), b = vec3(0.0);
    float total = 0.0, h = 0.0, r = 0.0;
    for (int k = 0; k < 4; ++k) {
        float w = max(stands[k] - top + 0.15, 0.0);
        if (w <= 0.0) continue;
        sum += color[k].rgb * w, b += bend[k] * w, h += held[k] * w, r += rr[k] * w, total += w;
    }
    total = max(total, 1e-4);
    sum /= total, b /= total, h /= total, r /= total;
    // (The drift: four octaves, the finest faded out as a pixel takes more
    // than an eighth of its wave.)
    float foot = max(length(dFdx(p)), length(dFdy(p)));
    float drift = noise(p * 0.0017 + 1.7) * 0.42 + noise(p * 0.0071 + 5.3) * 0.28 + noise(p * 0.031 + 9.1) * 0.18;
    float fine = 1.0 - smoothstep(0.06, 0.12, foot * 0.13);
    drift += mix(0.5, noise(p * 0.13 + 2.9), fine) * 0.12;
    sum *= 0.88 + 0.24 * drift;
    rough = r;
    n = normalize(g + b);
    unseen = clamp(2.0 * (1.0 - h) / max(h, 0.1), 0.0, 0.5);
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
        // Sand: one colour, as a dune's sand is - sorted by the wind, not
        // speckled. What it shows is its shape and the light on it (its
        // ripples are in its normal, its grains in how it scatters: main);
        // only a slip face, fresh from its avalanches, a shade lighter, and
        // the tone drifting over hundreds of metres. Banded rock where the
        // ground is steeper than sand can lie.
        vec3 n = normalize(vNormal);
        vec2 p = vRoom.xz;
        float slip = smoothstep(0.95, 0.86, n.y);
        vec3 sand = mAlbedo * (1.0 + 0.05 * slip) * (0.96 + 0.08 * noise(p * 0.0035 + 2.3));
        float strata = 0.5 + 0.5 * sin(vRoom.y * 2.6 + noise(p * 0.15) * 2.5);
        vec3 rock = vec3(0.50, 0.30, 0.20) * (0.72 + 0.32 * strata) * (0.85 + 0.2 * noise(p * 2.0 + vRoom.y));
        // Wind: pale veils of sand streaming over the ground, downwind -
        // where they are finer than a pixel, gone.
        vec2 w = p * vec2(0.35, 1.2) + vec2(uTime * 1.7, uTime * 0.3);
        float fade = 1.0 - smoothstep(0.3, 1.2, length(fwidth(w)));
        float drift = smoothstep(0.55, 0.9, noise(w) * 0.7 + noise(w * 3.1 + 7.0) * 0.3) * uWind;
        sand = mix(sand, mAlbedo * 1.12 + 0.02, drift * 0.35 * fade);
        float steep = smoothstep(0.38, 0.5, 1.0 - n.y);
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
    TraceSurf = vec4(0.0);
    Diffuse = vec4(0.0);
#ifdef SG_VELOCITY
    Motion = vec4(1e4, 1e4, 0.0, 0.0);
    if (uMoving > 0.5 && vNowClip.w > 1e-6 && vWasClip.w > 1e-6)
        Motion.xy = (vNowClip.xy / vNowClip.w - vWasClip.xy / vWasClip.w) * 0.5 * uMotionSize;
#endif
    vLit = vWorld + uLatticeShift;
    vLitDx = dFdx(vWorld), vLitDy = dFdy(vWorld);  // (uLatticeShift is the same at every pixel)
    if (vInstanced > 0.5) {
        mAlbedo = vMat0.rgb, mRoughness = vMat0.a;
        mSurface = vMat1.x, mEmissive = vMat1.y, mHighlight = vMat1.z, mMirror = vMat1.w, mMetal = vMetal, mGrime = vGrime;
    } else {
        mAlbedo = uAlbedo, mRoughness = uRoughness;
        mSurface = uSurface, mEmissive = uEmissive, mHighlight = uHighlight, mMirror = uMirror, mMetal = uMetal, mGrime = uGrime;
    }
    if (mSurface > 8.5 && mSurface < 9.5) {
#ifdef SG_TRACED
        if (uSurfaceOnly == 5) {
            FragColor = vec4(0.0);
            return;
        }
#endif
        // The sky is not lit and not fogged: it is what the fog fades into.
        // (The lamps' light in the air before it is: air_light.) But where
        // the air takes all (uFogFull), the furthest ground is the air's own
        // colour - so the sky is too, at the horizon and below it: no seam
        // where the last of the ground meets it.
        vec3 d = normalize(vWorld - uViewPos);
        vec3 s = mix(sky(d), uFogColor, uFogFull * (1.0 - smoothstep(0.0, 0.35, d.y)));
        FragColor = vec4((s + air_light(1e9)) * (1.0 - max(uDim, uDimNear)), 0.0);
        return;
    }
#if defined(SG_TRACED) && !defined(SG_CUTOUT)
    // What a ray needs of a surface, asked at once (src/gl/trace): which way
    // it faces as drawn - its relief, its ripples, are the scene's to shade
    // - how far, how rough. (Water, and what is cut out of its picture, go
    // on: a wave's slope is what it mirrors by, and a cut-out's holes are
    // where nothing is.)
    if (uSurfaceOnly == 5 && !(mSurface > 17.5 && mSurface < 18.5)) {
        vec3 fn = normalize(vNormal);
        if (uTwoSided > 0.5 && dot(fn, uViewPos - vWorld) < 0.0) fn = -fn;
        vec3 o = fn / (abs(fn.x) + abs(fn.y) + abs(fn.z));
        vec2 e = o.z >= 0.0 ? o.xy : (1.0 - abs(o.yx)) * vec2(o.x >= 0.0 ? 1.0 : -1.0, o.y >= 0.0 ? 1.0 : -1.0);
        FragColor = vec4(e, length(vWorld - uViewPos), clamp(mRoughness, 0.05, 1.0));
        return;
    }
#endif
    float rough_mod;
    vec3 albedo = mSurface > 18.5 && mSurface < 19.5 && uSplat > 0.5 && uSplat < 1.5 ? ground_albedo(rough_mod) : surface_albedo(rough_mod);
    // A land laid with beds: their colour, roughness and normal in place of
    // its own (its surface's light - sand's grains - and its damp kept).
    float bed_rough = -1.0, bed_unseen = 0.0;
    vec3 bed_n = vec3(0.0, 1.0, 0.0);
    if (uSplat > 2.5 && uSplat < 3.5) albedo = beds_albedo(normalize(vNormal), bed_rough, bed_n, bed_unseen);
    float relief_h = -1.0;  // how high a skin's paint stands here, if it says
    vec4 surface_at = vec4(-1.0);  // a worn texture's surface map here (occlusion, roughness, metal), if it has one
    bool mapped = false;           // its normal map says how the surface is bent here
    vec2 height_steps = vec2(0.0); // ... as its height's change across the screen (skin_height_steps)
    bool uv_mapped = false;        // its own normal map, read through its uv, says so instead:
    vec3 uv_normal = vec3(0.0, 0.0, 1.0);  // ... which way it faces, along its u and v and out
    float map_spread = 0.0;        // ... and how far its normals part within the pixel (Toksvig)
    float scatter = 0.0;                   // how much light this skin lets through and spreads (uSubsurface)
    vec2 uv_here = vUV, uv_dx = vec2(0.0), uv_dy = vec2(0.0);
    if (uTexMix > 0.0) {
        vec4 seen_at = uSeenFromVP * vec4(vWorld, 1.0);
        vec2 on_screen = uSeenFrom > 0.5 ? seen_at.xy / seen_at.w * 0.5 + 0.5 : gl_FragCoord.xy / uViewport;
        vec2 uv = uScreenUV > 0.5 ? (on_screen - uScreenRect.xy) / max(uScreenRect.zw - uScreenRect.xy, vec2(1e-4))
                : uSkin > 1.5 ? world_uv() : uSkin > 0.5 ? skin_uv() : vUV;
        if (uTexFlip > 0.5) uv.y = 1.0 - uv.y;
        if (uUVRect.z > 0.0) uv = uUVRect.xy + uv * uUVRect.zw;
        bool framed_skin = uSkin > 0.5 && uSkin < 1.5 && uSkinFramed > 0.5;
        // (A model's own picture, or a screen's - but a screen keeps its pixels.)
        if (!framed_skin && uTexels > 0.0 && uScreenUV < 0.5 && uCRT < 0.5) uv = texel_snap(uv, min(uTexels * 2.0, float(textureSize(uTex, 0).y)));
        vec4 texel = framed_skin ? skin_texel(uTex) : tiled(uTex, uv);
        if (uSubsurface > 0.0) scatter = uSubsurface * texel.a, texel.a = 1.0;
        // (A land's ground, its tone moving slowly across it.)
        if (uTileBreak > 0.0 && uSkin > 1.5 && uSkin < 2.5)
            texel.rgb *= mix(1.0, 0.86 + 0.28 * noise(vRoom.xz * 0.013 + 4.1) * (0.7 + 0.3 * noise(vRoom.xz * 0.061)), uTileBreak);
        if (framed_skin && uSurfaceMapOn > 0.5) surface_at = skin_texel(uSurfaceMap);
        if (framed_skin && uNormalMapOn > 0.5) mapped = true, height_steps = skin_height_steps();
        if (!framed_skin) {
            uv_here = uv, uv_dx = dFdx(uv), uv_dy = dFdy(uv);
            if (uSurfaceMapOn > 0.5) surface_at = tiled(uSurfaceMap, uv);
            if (uNormalMapOn > 0.5) {
                uv_normal = tiled(uNormalMap, uv).xyz * 2.0 - 1.0;
                // (Read from afar, a mipmap averages the normals of many
                // texels, and the average is shorter the more they part:
                // normalised, the bumps' spread would be lost and a glossy
                // relief seen far off would keep a mirror's highlight,
                // caught by a lone texel - a glint with no light to give
                // it, flashing as the eye moves. The spread widens the
                // highlight instead (Toksvig); an 8-bit map's own rounding
                // is not spread.)
                float map_len = length(uv_normal);
                map_spread = max(1.0 - map_len - 0.01, 0.0) / max(map_len, 0.1) * uNormalStrength * uNormalStrength;
                uv_normal.y *= uNormalFlipY;
                uv_normal.xy *= uNormalStrength;
                uv_mapped = true;
            }
        }
#ifdef SG_CUTOUT
        // A picture's alpha as it is seen: its mipmaps, taken down, average
        // the leaf with the gaps round it and it would thin away with
        // distance - so it is made the more solid the further down they are
        // read (Golus's coverage), and a far tree keeps its leaves.
        vec2 cut_px = uv * vec2(textureSize(uTex, 0));
        float cut_lod = max(0.0, 0.5 * log2(max(dot(dFdx(cut_px), dFdx(cut_px)), dot(dFdy(cut_px), dFdy(cut_px)))));
        if (uCutout > 0.0 && texel.a * (1.0 + cut_lod * 0.25) < uCutout) discard;
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
            tex = max((-b - sqrt(max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a), 0.0) * uUntoneGain;
        }
        albedo = mix(albedo, tex, uTexMix);
    }
    // A portal's view of another room arrives already lit and already fogged,
    // by that room's own light and air: it is shown as it is, not lit or
    // fogged a second time by the room it is seen from.
    if (uScreenUV > 0.5 && uTexMix > 0.99) {
#ifdef SG_TRACED
        if (uSurfaceOnly == 5) {
            FragColor = vec4(0.0);
            return;
        }
#endif
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
            seen = mix(seen, room, clamp(fres * abs(uMirrorUse[k].x), 0.0, 1.0));
        }
        // This side's air up to the opening is this side's to light.
        seen += air_light(length(vWorld - uViewPos));
        FragColor = vec4(seen * (1.0 - max(uDim, uDimNear) * (1.0 - uUndim)), 0.0);
        return;
    }
    float roughness = clamp(mRoughness + rough_mod, 0.05, 1.0);
    // Metal reflects in its own colour and scatters none: as much as it says
    // (`metal`). The brushed surface is at least partly metal - only partly:
    // most of what wears it is painted, and a bare metal with only the sky
    // and the floor to reflect would go dark. Everything else reflects 4%
    // head on, white.
    float metal = max(clamp(mMetal, 0.0, 1.0), (mSurface > 4.5 && mSurface < 5.5) ? 0.35 : 0.0);
    // A worn texture's surface map says what the thing is, point by point:
    // its roughness and metal in place of the thing's own, and how open its
    // hollows are to the light from all round (`occlusion`, below).
    float occlusion = 1.0;
    if (surface_at.x >= 0.0) {
        occlusion = clamp(surface_at.r, 0.0, 1.0);
        roughness = clamp(surface_at.g, 0.05, 1.0);
        metal = clamp(surface_at.b, 0.0, 1.0);
    }
    if (bed_rough >= 0.0) roughness = clamp(bed_rough, 0.05, 1.0);
    // `grime`: how lived-in a surface is - what no tile repeats: paint gone
    // patchy and a little yellow over years, larger than any tile and never
    // the same twice across a room (noise in the room's own frame, on the
    // plane the surface lies in), roughness going with it; and on a wall,
    // the scuffs and dust of the floor's edge, its line not quite straight.
    if (mGrime > 0.0) {
        vec3 an = abs(normalize(vRoomNormal));
        vec2 gp = an.y > 0.7 ? vRoom.xz : (an.x > an.z ? vRoom.zy : vRoom.xy);
        // (Patches a metre or two across, edges a hand's width soft; inside
        // them finer mottling - none of it lined up with anything.)
        float big = noise(gp * 0.55 + 3.1) * 0.5 + noise(gp * 1.6 + 7.7) * 0.3 + noise(gp * 4.7 + 1.3) * 0.2;
        float blotch = smoothstep(0.42, 0.72, big);
        float mottle = noise(gp * 13.0) * 0.6 + noise(gp * 37.0) * 0.4;
        float shade = 1.0 - mGrime * (0.3 * blotch + 0.12 * (mottle - 0.5));
        // (Streaks run down a wall: what ran down it once, faint, long.)
        if (an.y < 0.5) {
            float run = noise(vec2(gp.x * 7.0, gp.y * 0.35 + 5.0));
            shade *= 1.0 - mGrime * 0.12 * smoothstep(0.62, 0.9, run);
            float edge = 0.22 + 0.14 * noise(vec2(gp.x * 2.3, 0.0));
            shade *= 1.0 - mGrime * 0.32 * (1.0 - smoothstep(0.0, edge, vRoom.y)) * (0.7 + 0.3 * noise(gp * 9.0));
        }
        albedo = mix(albedo, albedo * vec3(0.92, 0.85, 0.7), mGrime * blotch * 0.8) * shade;
        roughness = clamp(roughness + mGrime * 0.18 * (blotch - 0.4), 0.05, 1.0);
    }
    // What its own shape says, where a mesh's wear was made from it (its w
    // 0): its hollows shut to the light from all round as they are to the
    // sky; and the world's variation over every worn thing - a block paler
    // than the next, a stretch of wall greyer - in patches no two things share.
    bool shaped = vMeshWear.w < 0.5;
    // (The photograph's own joints and pits - its occlusion map - before the
    // shape's: where moss and soil take first, at the finest size.)
    float cavity = 1.0 - occlusion;
    if (shaped) occlusion *= 1.0 - 0.55 * vMeshWear.z;
    if (uVary.z > 0.0) {
        vec2 vp = vRoom.xz / uVary.z + vec2(vRoom.y * 0.37 / uVary.z);
        float v = noise(vp) * 0.6 + noise(vp * 2.9 + 4.1) * 0.4 - 0.5;
        float warm = noise(vp * 0.7 + 11.3) - 0.5;
        albedo *= 1.0 + uVary.x * 2.0 * v;
        albedo *= vec3(1.0 + uVary.x * warm, 1.0, 1.0 - uVary.x * warm);
        roughness = clamp(roughness + uVary.y * 2.0 * v, 0.05, 1.0);
    }
    // Its edges worn: knocked, rubbed, weathered first where it stands out -
    // paler, smoother - their line ragged as the surface's own grain makes it.
    if (shaped && uEdge.x > 0.0) {
        vec3 an = abs(normalize(vRoomNormal));
        vec2 gp = an.y > 0.7 ? vRoom.xz : (an.x > an.z ? vRoom.zy : vRoom.xy);
        float grain = noise(gp * 23.0) * 0.55 + noise(gp * 71.0) * 0.3 + noise(gp * 5.0) * 0.15;
        float worn = smoothstep(0.12, 0.7, vMeshWear.x) * uEdge.x;
        float m = smoothstep(1.0 - worn - 0.05, 1.0 - worn + 0.05, grain) * step(0.001, worn);
        albedo = mix(albedo, min(albedo * uEdge.y, vec3(0.9)), m);
        roughness = mix(roughness, uEdge.z, m);
    }
    // Dirt where dirt gathers - a photograph of it, not a pattern made up:
    // along the floor (what a mop leaves, what is kicked up), under the
    // ceiling (soot, damp), into the corners where two walls meet and along
    // the floor's edges; how much each point gathers says how high the
    // photograph's grains must stand there to show, so its edge is the
    // photograph's own (a stain's rim, a crust), never a line or a blur. Laid
    // on the plane the surface lies in, in the room's frame, at two sizes
    // crossed, so it repeats nowhere a wall is long.
    if (uWear.x > 0.5 && uDirtSet.x > 0.0) {
        vec3 an = abs(normalize(vRoomNormal));
        vec2 gp = an.y > 0.7 ? vRoom.xz : (an.x > an.z ? vRoom.zy : vRoom.xy);
        vec4 d1 = texture(uDirt, gp / uDirtSet.z);
        vec4 d2 = texture(uDirt, gp / (uDirtSet.z * 2.71) + vec2(0.37, 0.61));
        float grains = 0.65 * d1.a + 0.35 * d2.a;
        float gather = uDirtBand.z;
        float up = vRoom.y;
        // (Settled on whatever faces up: a sill, a cabinet's top, the floor.)
        gather = max(gather, uDirtBand.w * smoothstep(0.7, 0.95, normalize(vRoomNormal).y));
        if (an.y < 0.7) gather = max(gather, 1.0 - smoothstep(0.0, uDirtBand.x, up));
        // (The room's box - or, rooms made one, the box of the room this is in.)
        vec3 box = uRoomBox;
        vec2 from = vec2(0.0);
        for (int k = 0; k < 4; ++k) {
            if (k >= uRegions) break;
            if (all(greaterThan(vRoom.xz, uRegion[k].xy - 0.4)) && all(lessThan(vRoom.xz, uRegion[k].zw + 0.4))) {
                box = vec3(uRegion[k].z - uRegion[k].x, uRegionH[k], uRegion[k].w - uRegion[k].y);
                from = uRegion[k].xy;
                break;
            }
        }
        if (box.y > 0.0) {
            if (an.y < 0.7) gather = max(gather, 1.0 - smoothstep(0.0, uDirtBand.y, box.y - up));
            vec2 q = vRoom.xz - from;
            float cx = min(q.x, box.x - q.x), cz = min(q.y, box.z - q.y);
            // (Where two walls meet; on the floor and the ceiling, along the walls.)
            float corner = an.y > 0.7 ? min(cx, cz) : (an.x > an.z ? cz : cx);
            gather = max(gather, 1.0 - smoothstep(0.0, uDirtSet.y, max(corner, 0.0)));
        }
        // (And in its own hollows and creases, where its shape keeps it.)
        if (shaped) gather = max(gather, (smoothstep(0.15, 0.8, vMeshWear.z) * uMeshDirt.x + smoothstep(0.1, 0.6, vMeshWear.y) * uMeshDirt.y) * (0.6 + 1.2 * cavity));
        gather = min(gather + wear_field().r, 1.0);
        gather *= uDirtSet.x * uWear.z;
        float m = smoothstep(1.0 - gather - 0.06, 1.0 - gather + 0.06, grains) * step(0.001, gather);
        albedo = mix(albedo, d1.rgb * mix(1.0, d2.rgb.g * 1.6, 0.3), m);
        roughness = mix(roughness, uDirtSet.w, m);
    }
    // Moss where moss takes: on what faces up and holds water, on the side
    // the sun reaches least, in what is shut in and stays damp - a
    // photograph of it, its edge the photograph's own, as dirt's is.
    if (uMossSet.w > 0.5) {
        vec3 rn = normalize(vRoomNormal), an = abs(rn);
        vec2 gp = an.y > 0.7 ? vRoom.xz : (an.x > an.z ? vRoom.zy : vRoom.xy);
        vec4 m1 = texture(uMoss, gp / uMossSet.y);
        vec4 m2 = texture(uMoss, gp / (uMossSet.y * 2.37) + vec2(0.29, 0.83));
        float grains = 0.6 * m1.a + 0.4 * m2.a;
        // (Low and shut in, it stays damp longest: more of it there.)
        float low = shaped ? smoothstep(0.2, 0.9, vMeshWear.z) : 0.0;
        float take = uMossWhere.x * smoothstep(0.35, 0.9, rn.y);
        float shade = smoothstep(0.0, 0.8, dot(rn.xz, vec2(cos(uMossWhere.w), sin(uMossWhere.w)))) * (1.0 - smoothstep(0.3, 0.9, -rn.y));
        take = max(take, uMossWhere.y * shade * (0.35 + 0.65 * low));
        take = max(take, uMossWhere.z * low);
        // (Into the stone's own joints and pits first.)
        take *= 0.35 + 1.6 * cavity;
        // (In patches: where it took, not everywhere it could.)
        take *= 0.55 + 0.9 * (noise(gp * 0.8 + 5.7) - 0.5) + 0.45;
        take = clamp(take * uMossSet.x * uWear.z, 0.0, 1.0);
        float m = smoothstep(1.0 - take - 0.06, 1.0 - take + 0.06, grains) * step(0.001, take);
        albedo = mix(albedo, m1.rgb * mix(1.0, m2.rgb.g * 1.6, 0.3), m);
        roughness = mix(roughness, uMossSet.z, m);
    }
    // Damp where water has been: down a wall below a leak, in streaks that
    // widen as they fall and thin out; a blot on the ceiling round it; a
    // tide mark on the floor where it lands; and rising from the floor up a
    // wall. Its edge, as dirt's, the photograph's own.
    float covered = 0.0;  // how far what covers it fills its relief: flat where it lies thick
    if (uWear.y > 0.5 && uDampSet.x > 0.0) {
        vec3 rn = normalize(vRoomNormal), an = abs(rn);
        vec2 gp = an.y > 0.7 ? vRoom.xz : (an.x > an.z ? vRoom.zy : vRoom.xy);
        vec4 d1 = texture(uDamp, gp / uDampSet.z);
        vec4 d2 = texture(uDamp, gp / (uDampSet.z * 2.43) + vec2(0.71, 0.19));
        float grains = 0.6 * d1.a + 0.4 * d2.a;
        float wet = 0.0;
        if (an.y < 0.7) wet = max(wet, 1.0 - smoothstep(0.0, uDampSet.y, vRoom.y));
        for (int i = 0; i < 8; ++i) {
            if (float(i) >= uWear.w) break;
            vec3 d = vRoom - uLeak[i].xyz;
            float reach = max(uLeak[i].w, 0.05);
            if (an.y < 0.7) {
                // (Down the wall: how far below the leak, how far along the wall from under it.)
                float below = -d.y, along = an.x > an.z ? d.z : d.x, off = an.x > an.z ? d.x : d.z;
                float wide = 0.12 + 0.3 * max(below, 0.0) / reach;
                float streak = exp(-(along * along) / (wide * wide)) * smoothstep(-0.15, 0.1, below) * exp(-max(below, 0.0) / (3.0 * reach));
                streak *= exp(-(off * off) / (reach * reach));
                // (In streaks: the water runs in rivulets, not a sheet.)
                streak *= 0.6 + 0.4 * texture(uDamp, vec2(along * 1.7, 0.31)).a;
                if (uDampCover.y > 0.0) {
                    // Thin runs (`damp_runs`): what runs heavy runs in lines
                    // two or three centimetres apart, each of its own length,
                    // narrowing as it falls, a bead where it stopped - many
                    // under the leak, fewer out to its reach.
                    float cols = along * 36.0, id = floor(cols), fx = fract(cols) - 0.5;
                    float r1 = fract(sin(id * 12.9898 + float(i) * 78.233) * 43758.5453);
                    float r2 = fract(sin(id * 39.3468 + float(i) * 11.135) * 24634.6345);
                    float r3 = fract(sin(id * 73.156 + float(i) * 52.235) * 13758.937);
                    float here = step(r1, exp(-(along * along) / (reach * reach * 0.3)) * 0.42 * uDampCover.y);
                    // (Each from its own height: where it gathered enough to run.)
                    float from = below - r3 * 0.18 * reach;
                    float len = (0.2 + 2.4 * r2 * r2) * reach;
                    float k = clamp(from / len, 0.0, 1.0);
                    float w = mix(0.46, 0.16, k) * (0.6 + 0.6 * r3);
                    float run = here * (1.0 - smoothstep(w * 0.6, w, abs(fx))) * smoothstep(-0.03, 0.0, from) * (1.0 - smoothstep(len - 0.02, len, from));
                    // (The bead: a drop about 8 mm across where it stopped; fx / 36 is metres across.)
                    float bead = here * (1.0 - smoothstep(0.003, 0.0045, length(vec2(fx / 36.0, from - len + 0.004))));
                    streak = mix(streak, max(run, bead), uDampCover.y);
                }
                wet = max(wet, streak);
            } else {
                float r = length(d.xz) / reach;
                wet = max(wet, rn.y < 0.0 ? exp(-r * r * 1.5) : exp(-r * r * 3.0) * (1.0 - smoothstep(0.0, 0.5, vRoom.y)));
            }
        }
        wet = min(wet + wear_field().g, 1.0);
        wet *= uDampSet.x * uWear.z;
        float m = smoothstep(1.0 - wet - 0.07, 1.0 - wet + 0.07, grains) * step(0.001, wet);
        // (What covers - blood, paint - is a film: thin, it is seen through,
        // tinting what is under it; thick, it hides it. As thick as its
        // field says, the grains only a little in it - not all or nothing
        // grain by grain, which makes of a thin smear a dark speckled band.)
        m = mix(m, clamp(wet * 1.1, 0.0, 1.0) * mix(0.8, 1.0, grains), uDampCover.x);
        // (Water darkens what it soaks; what covers - blood, paint - is its own colour over it.)
        albedo = mix(albedo, mix(albedo * d1.rgb * 1.6, d1.rgb, uDampCover.x), m);
        covered = m * uDampCover.x * smoothstep(0.02, 0.22, wet);
        roughness = mix(roughness, uDampSet.w, m * mix(0.7, 1.0, uDampCover.x));
    }
    // Damp by its water (a land that says where its water stands): darker
    // and glossier the nearer the waterline, as wet sand is - its edge
    // ragged, as the water left it.
    if (uLand.y > 0.0) {
        float above = vRoom.y - uLand.x + (noise(vRoom.xz * 0.9) - 0.5) * uLand.y * 0.6;
        float wet = 1.0 - smoothstep(0.0, uLand.y, above);
        albedo *= mix(1.0, 0.5, wet);
        roughness = mix(roughness, 0.22, wet * 0.85);
    }
    vec3 f0 = mix(vec3(0.04), albedo, metal);
    vec3 diffuse = albedo * (1.0 - metal);
    if (uSurfaceOnly > 0 && uSurfaceOnly != 5) {
        if (uSurfaceOnly == 1) FragColor = vec4(vLit, 1.0);
        else if (uSurfaceOnly == 2) FragColor = vec4(normalize(vNormal), 1.0);
        else if (uSurfaceOnly == 3) FragColor = vec4(diffuse, 1.0);
        else if (uSurfaceOnly == 6) FragColor = vec4(vMeshWear.w > 0.5 ? vec3(0.2) : vMeshWear.xyz, 1.0);
        else FragColor = vec4(length(vWorld - uViewPos), 0.0, 0.0, 1.0);
        return;
    }

    vec3 n = normalize(vNormal);
    vec3 v = normalize(uViewPos - vWorld);
    // A thin thing seen from behind (a leaf) faces the eye there too.
    if (uTwoSided > 0.5 && dot(n, uViewPos - vWorld) < 0.0) n = -n;
    vec3 face_n = n;  // the surface's own, before any map bends it
    bool bent = false;  // whether relief or waves turned the normal from the surface's own
    if (bed_rough >= 0.0) n = bed_n, bent = true;
    if (uv_mapped) {
        // Its own normal map, laid along which way its corners' u and v run
        // (as it was baked: MikkTSpace's ways, per corner) - or, a mesh that
        // has none, as the screen sees them run here (Schüler's frame).
        vec3 t, b;
        if (dot(vTangent.xyz, vTangent.xyz) > 1e-6) {
            t = normalize(vTangent.xyz - n * dot(n, vTangent.xyz));
            b = cross(n, t) * (vTangent.w < 0.0 ? -1.0 : 1.0);
        } else {
            vec3 dpx = dFdx(vWorld), dpy = dFdy(vWorld);
            vec3 px = cross(dpy, n), py = cross(n, dpx);
            t = px * uv_dx.x + py * uv_dy.x;
            b = px * uv_dx.y + py * uv_dy.y;
            float k = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
            t *= k, b *= k;
        }
        n = normalize(t * uv_normal.x + b * uv_normal.y + n * max(uv_normal.z, 1e-3));
        bent = true;
    }
    if (mapped || relief_h >= 0.0) {
        // Bent by the paint's relief, from how its height changes across the
        // screen (Mikkelsen's surface gradient): no tangents, any projection.
        // A normal map says that change itself, as finely as it was made; the
        // relief alone, only as the screen sees the height step from pixel to pixel.
        vec3 dpx = dFdx(vWorld), dpy = dFdy(vWorld);
        vec3 r1 = cross(dpy, n), r2 = cross(n, dpx);
        float det = dot(dpx, r1);
        vec2 dh = mapped ? height_steps : vec2(dFdx(relief_h), dFdy(relief_h));
        vec3 grad = sign(det) * (dh.x * r1 + dh.y * r2);
        n = normalize(abs(det) * n - grad);
        bent = true;
    }
    // (What covers it - blood pooled, paint - fills the pile and the grain
    // where it lies thick: its face is its own, smooth.)
    if (covered > 0.0) n = normalize(mix(n, face_n, covered));
    // A pile's sheen (`sheen`, a texture's: velvet, plush carpet): its fibres
    // stand up out of the surface, so seen square on the eye looks down
    // between them into their shade, and seen aslant along their sides,
    // lit - darker face on, brighter toward the grazing, in its own colour;
    // the map's bends (the pile's lie) turning it, so it moves as the eye does.
    if (uSheen > 0.0) {
        float nv = clamp(dot(n, v), 0.0, 1.0);
        diffuse *= max(1.0 + uSheen * (1.5 * pow(1.0 - nv, 3.0) - 0.22 * nv * nv), 0.0);
    }
    // Water over land that says how deep it is: no line where it meets the
    // shore - in its last fifth of a metre it thins out, the ground under it
    // showing through more the shallower it is (a share of its pixels left
    // out, a new share each frame, which the frames' blend makes a fade).
    if (mSurface > 17.5 && mSurface < 18.5 && uSplat > 1.5) {
        float deep = texture(uTex, vUV).r * uWaterDeepest;
        float shown = smoothstep(0.0, 0.2, deep);
        if (shown < 0.999 && shown <= fract(ign(gl_FragCoord.xy) + fract(uTime * 24.0) * 0.618034)) discard;
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
        // And the rings spreading from where something met it (sg::ripple),
        // where its world's time says each has spread to: a few crests,
        // leaving at a walking pace, dying as they spread.
        for (int i = 0; i < MAX_RIPPLES; ++i) {
            if (i >= uRippleCount) break;
            vec4 r = uRipple[i];
            float age = t - r.z;
            if (age < 0.0 || age > 6.0) continue;
            vec2 d = p - r.xy;
            float dist = length(d), x = dist - (0.15 + 0.8 * age);
            float crests = exp(-x * x * 6.0) * r.w * exp(-0.6 * age) / sqrt(1.0 + 2.0 * dist);
            if (crests < 1e-3) continue;
            slope += d / max(dist, 1e-3) * (0.6 * crests * cos(x * 18.0));
        }
        n = normalize(n - vec3(slope.x, 0.0, slope.y));
        bent = true;
    }
    // Sand's wind ripples, where a land asks for them: crests across the
    // wind as far apart as it says (a hand, as sand has them), fifteen times as long as they stand high, a long gentle side
    // the wind comes up and a short steep one it leaves by; their crests
    // wandering, now and then two running into one - none on a slip face,
    // where the sand has avalanched smooth, nor on what is steeper still.
    // Seen only as near as a pixel can hold them: further off they are what
    // makes the sand a little rougher than smooth, and the sand is smooth.
    float sand_unseen = 0.0;  // slope variance of ripples finer than the pixel, given to the roughness
    if (mSurface > 7.5 && mSurface < 8.5 && uLand.z > 0.0 && face_n.y > 0.85) {
        float lambda = uLand.z;
        vec2 wind = vec2(cos(uLand.w), sin(uLand.w));
        vec2 p = vRoom.xz;
        float along = dot(p, wind), across = dot(p, vec2(-wind.y, wind.x));
        // (Wandering: the crests bent along their length, and the spacing
        // a little wider here than there, so crests now and then merge.)
        float wander = lambda * (2.4 * (noise(vec2(across, along * 0.25) / (lambda * 9.0) + 3.1) - 0.5)
                               + 0.9 * (noise(vec2(across, along * 0.4) / (lambda * 3.2) + 8.7) - 0.5));
        float spacing = lambda * (1.0 + 0.16 * (noise(p / (lambda * 40.0) + 5.3) - 0.5));
        float phase = (along + wander) / spacing;
        // (Drawn only where each takes a dozen pixels or more, gone by six:
        // finer, they would beat against the pixels in false whorls. How many
        // is how much ground a pixel takes, its longer way - not how fast
        // their wandering phase turns, which would show them in streaks.)
        float foot = max(length(dFdx(p)), length(dFdy(p))) / spacing;
        float held = 1.0 - smoothstep(0.08, 0.17, foot);
        float lies = smoothstep(0.85, 0.92, face_n.y);
        // (Their slope along the wind: up the long side, 70% of each, down
        // the short; eased where they turn, as a crest is rounded.)
        const float stoss = 0.7, tall = 1.0 / 15.0;
        float f = fract(phase);
        float up = tall / stoss, down = -tall / (1.0 - stoss);
        float s = mix(up, down, smoothstep(stoss - 0.06, stoss + 0.04, f)) ;
        s = mix(s, up, smoothstep(0.96, 1.0, f));
        float k = held * lies;
        n = normalize(n - vec3(wind.x, 0.0, wind.y) * (s * k));
        bent = k > 0.0 || bent;
        // (Their slopes' spread: tall^2 (1/stoss + 1/(1-stoss)); twice that GGX's.)
        sand_unseen = 2.0 * tall * tall * (1.0 / stoss + 1.0 / (1.0 - stoss)) * lies * (1.0 - held);
    }
    float ndv = clamp(dot(n, v), 1e-3, 1.0);
    float a2 = roughness * roughness * roughness * roughness + sand_unseen + bed_unseen;
    // Specular antialiasing (Kaplanyan & Hill): where the normal turns fast
    // across a pixel - a rounded edge thinner than a pixel - the highlight is
    // widened by as much, or it flickers from pixel to pixel and steps.
    vec3 dndx = dFdx(n), dndy = dFdy(n);
    float variance = 0.25 * (dot(dndx, dndx) + dot(dndy, dndy));
    a2 = clamp(a2 + min(2.0 * variance, 0.25) + min(2.0 * map_spread, 0.5), 0.0, 1.0);
#ifdef SG_TRACED
    {
        vec3 o = n / (abs(n.x) + abs(n.y) + abs(n.z));
        vec2 e = o.z >= 0.0 ? o.xy : (1.0 - abs(o.yx)) * vec2(o.x >= 0.0 ? 1.0 : -1.0, o.y >= 0.0 ? 1.0 : -1.0);
        TraceSurf = vec4(e, length(vWorld - uViewPos), roughness);
        if (uSurfaceOnly == 5) {
            FragColor = TraceSurf;
            return;
        }
    }
    // Where this point was in the picture traced (the last frame's, unless it
    // was traced for this one: uTraceVP, uTraceEye) - if what was traced
    // there is this surface, as far from where it was seen.
    ivec2 trace_px = ivec2(0);
    bool traced = false;
    if (uTrace != vec4(0.0) && uGlass <= 0.0 && uAdd < 0.5) {
        vec4 c = uTraceVP * vec4(vWorld, 1.0);
        vec2 uv = c.xy / max(c.w, 1e-6) * 0.5 + 0.5;
        ivec2 size = textureSize(uTraceSurf, 0);
        trace_px = ivec2(uv * vec2(size));
        if (c.w > 0.0 && all(greaterThanEqual(trace_px, ivec2(0))) && all(lessThan(trace_px, size))) {
            float there = length(vWorld - uTraceEye);
            traced = abs(texelFetch(uTraceSurf, trace_px, 0).z - there) < 0.01 * there + 0.03;
        }
    }
#endif

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
        horizon = clamp(1.0 + dot(r, face_n), 0.0, 1.0);
        horizon *= horizon;
    }
    vec3 spec_k = multi * horizon;

    // The light the room's probes hold for a surface facing this way, and
    // how much they hold this point (0: the light from all round below).
    float probe_cover = 0.0;
    vec3 probe_given = uProbeCount > 0 ? probe_light(vLit, n, 2.0 / 3.0, 0.25, probe_cover) : vec3(0.0);
    // Where it is traced, the light from all round the rays found - taken
    // whole once its history holds a few frames, the probes' before that -
    // and a lamp standing in for bounce gives way to it as to the probes.
    float bounce_cover = probe_cover;
#ifdef SG_TRACED
    float gi_cover = 0.0;
    vec3 gi_given = vec3(0.0);
    if (traced && uTrace.w > 0.0) {
        vec4 gi = texelFetch(uTraceGI, trace_px, 0);
        gi_cover = clamp((gi.a - 1.0) / 3.0, 0.0, 1.0);
        gi_given = gi.rgb;
        bounce_cover = mix(probe_cover, 1.0, gi_cover);
    }
#endif

    vec3 direct = vec3(0.0), bounced = vec3(0.0);
    // (A lamp whose light ends before this part of the view gives it nothing.)
    uvec2 cell_here = lamp_cell(gl_FragCoord.xy / uViewport, length(vLit - uViewPos));
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= uLightCount) break;
        if (!lamp_in(cell_here, i)) continue;
        // Light let in through a doorway reaches a door's leaf, half in the
        // room beyond, as that room's own does: nothing this side's shadow
        // maps hold stands between them. And a bounce let in stands for that
        // room's light from all round, which the leaf has itself (below):
        // it is not lit by it twice.
        bool let_in = uStraddle > 0.5 && uLightGateAxis[i].w > 0.5;
        if (let_in && uLightB[i].y > 0.5) continue;
        vec3 l;
        float reach = light_reach(i, vLit, l);
        // A lamp that gives this point nothing - behind it, outside its cone,
        // too far - costs it nothing either.
        // (Skin takes light a little past where it turns away: SKIN_WRAP below.)
        if (reach <= 1e-5 || (dot(n, l) <= 0.0 && uTranslucent <= 0.0 && dot(n, l) <= -0.45 * scatter)) continue;
        if (dot(n, l) <= 0.0 && uTranslucent > 0.0) {
            // A leaf lit from behind: as much of it as comes through, scattered
            // - shadowed as its far side is.
            float through = -dot(n, l) * uTranslucent;
#ifdef SG_TRACED
            if (traced && uTrace.x > 0.0 && uLightA[i].w > 0.5 && dot(l, uTraceSunDir) > 0.9999)
                through *= texelFetch(uTraceSun, trace_px, 0).r;
            else if (traced && uTrace.y > 0.5 && uLightA[i].w < 0.5 && i < uShadowCount && uLightD[i].y > -0.5 && !let_in && traced_lamp(i) >= 0)
                through *= texelFetch(uTraceLamps, trace_px, 0)[traced_lamp(i)];
            else
#endif
            if (i < uShadowCount && uLightD[i].y > -0.5 && !let_in)
                through *= shadow_cascade(i, -n, l, uLightB[i].x < 0.0 ? uShadowFloor : uLightB[i].x);
            if (uLightB[i].y > 0.5) bounced += diffuse * through * uLightColor[i] * reach;
            else direct += diffuse * through * uLightColor[i] * reach;
            continue;
        }
        vec3 h = normalize(l + v);

        float ndl = max(dot(n, l), 0.0);
        float shadow = 1.0;
        // (Only where the lamp reaches: outside its cone it adds nothing,
        // and its shadow there is nothing. The sun traced: what the rays found.)
#ifdef SG_TRACED
        if (traced && uTrace.x > 0.0 && uLightA[i].w > 0.5 && dot(l, uTraceSunDir) > 0.9999) {
            shadow = texelFetch(uTraceSun, trace_px, 0).r;
        } else if (traced && uTrace.y > 0.5 && uLightA[i].w < 0.5 && ndl > 0.0 && i < uShadowCount && uLightD[i].y > -0.5 && !let_in && traced_lamp(i) >= 0) {
            // (A lamp traced: to its bulb, as soft as the bulb is wide.)
            shadow = texelFetch(uTraceLamps, trace_px, 0)[traced_lamp(i)];
        } else
#endif
        if ((ndl > 0.0 || scatter > 0.0) && i < uShadowCount && uLightD[i].y > -0.5 && !let_in) {
            float fl = uLightB[i].x < 0.0 ? uShadowFloor : uLightB[i].x;
            shadow = shadow_cascade(i, n, l, fl);
        }
        // (A land's beds bend their normals towards the light even where the
        // ground faces away from it - a ripple's far side on a dune in its
        // own shade - and a normal map casts no shadow: at a light this low
        // over the ground its faces turned to it would take many times what
        // the ground does, the rest nothing, the ground brighter than it is.
        // What a ripple's face takes is held to twice what the ground there
        // faces, and none where the ground itself turns from it (eased over
        // a degree or two): its ripples still lit and dark, the ground as
        // bright as it is. Ground the light only grazes is lit as much as it
        // faces it - never darkened for being grazed: a dune's flanks under
        // a low sun are lit, not shadowed.)
        if (bed_rough >= 0.0) {
            float face_ndl = dot(face_n, l);
            shadow *= smoothstep(0.0, 0.03, face_ndl) * min(1.0, (2.0 * max(face_ndl, 0.0) + 0.02) / max(ndl, 1e-4));
        }

        // Cook-Torrance: GGX for the spread of the highlight, Smith's
        // height-correlated masking (Hammon's fit), and Schlick's Fresnel -
        // a surface reflects more the more edge-on it is seen. What it
        // reflects is not also scattered, so the diffuse loses as much.
        // (The diffuse carries pi folded into the light, so the highlight does.)
        float ndh = max(dot(n, h), 0.0);
        float vdh = clamp(dot(v, h), 0.0, 1.0);
        // A light with a size (the sun's disc, a bulb) is lit from the point
        // of it whose highlight is brightest here (sized_ndh2), its highlight
        // as much wider as it is, and no brighter for being so.
        float ndh2 = ndh * ndh, energy = 1.0;
        if (uLightFrame[i].w < 0.5 && uLightFrame[i].x > 0.0) {
            float across = uLightA[i].w > 0.5 ? uLightFrame[i].x : uLightFrame[i].x / max(length(uLightPos[i] - vLit), 1e-3);
            ndh2 = sized_ndh2(across, ndl, ndv, dot(v, l));
            float sin2 = across * across / (1.0 + across * across);
            energy = a2 / clamp(a2 + sin2 / (vdh * 3.6 + 0.4), 1e-6, 1.0);
        }
        float denom = ndh2 * (a2 - 1.0) + 1.0;
        float d = a2 / (denom * denom + 1e-7) * energy;
        float vis = 0.5 / mix(2.0 * ndl * ndv, ndl + ndv, sqrt(a2));
        vec3 f = f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);
        vec3 lobe = diffuse * (1.0 - f) * grains(n, l, v, ndl, ndv) + d * vis * f * spec_k;
        vec3 lit = lobe * ndl;
        // SKIN_WRAP: skin's diffuse light wraps past the line where it turns
        // from the light, red furthest (light under the skin, scattered: the
        // soft warm edge of a lit face instead of a plastic one's hard line) -
        // a few sums on skin alone, nothing elsewhere.
        if (scatter > 0.0) {
            vec3 w = scatter * vec3(0.45, 0.2, 0.12);
            vec3 wrapped = clamp((dot(n, l) + w) / ((1.0 + w) * (1.0 + w)), 0.0, 1.0);
            lit = diffuse * (1.0 - f) * grains(n, l, v, ndl, ndv) * wrapped + d * vis * f * spec_k * ndl;
        }

        // (Light standing in for bounce is the probes' where they hold: their
        // bake saw the bounce itself, and it would count twice.)
        if (uLightB[i].y > 0.5)
            bounced += diffuse * ndl * uLightColor[i] * reach * shadow * (1.0 - (uLightB[i].y > 0.8 ? bounce_cover : 0.0));
        else direct += lit * uLightColor[i] * reach * shadow;
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
    ground_here *= 1.0 - probe_cover * uProbeOpen;
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
        // And where the room was captured, what is mirrored is what the
        // captures saw - the room itself, its lamps' shades, its doorways'
        // light - in place of the probes' blur of it.
        float capture_cover;
        vec3 seen = captured(vLit, normalize(mix(r, n, rr)), roughness, capture_cover);
        mirrored = mix(mirrored, seen, capture_cover);
    }
#ifdef SG_TRACED
    // Traced: what the rays found, for the diffuse, in place of the probes'
    // (and, under an open sky, of the look's sky and ground, which they met
    // themselves). What is mirrored keeps the probes'.
    if (gi_cover > 0.0) around = mix(around, (uTrace.w > 1.5 ? vec3(0.0) : mix(ground_here, sky_here, n.y * 0.5 + 0.5)) + gi_given, gi_cover);
#endif
    // Under an open sky a glossy surface reflects the sky itself - its
    // colours, its clouds, the sun's glint - as rougher surfaces cannot.
    if (mMirror > 0.0) mirrored = mix(mirrored, sky(normalize(vec3(r.x, abs(r.y), r.z))), mMirror * (1.0 - roughness));
    // Lying in a plane the room is seen in: what is seen there, turned left
    // for right as a mirror turns it, bent where the surface is (relief,
    // waves), and blurred as it roughens.
    for (int k = 0; k < 2; ++k) {
        if (k >= uMirrorCount) break;
        // (A window's glass is read by that glass alone: what else lies in
        // its plane - a bookcase's back against the wall - is not it.)
        if (uMirrorUse[k].x <= 0.0) continue;
        vec4 plane = uMirrorPlane[k];
        if (abs(dot(plane.xyz, vLit) + plane.w) > 0.01 || dot(normalize(vNormal), plane.xyz) < 0.9) continue;
        vec4 s = uMirrorSeen[k];
        vec2 ndc = gl_FragCoord.xy / uMirrorScreen * 2.0 - 1.0;
        vec2 t = vec2((s.z - ndc.x) / max(s.z - s.x, 1e-4), (ndc.y - s.y) / max(s.w - s.y, 1e-4));
        // (Only where it was drawn: past that, what lies in its plane is not it.)
        if (any(lessThan(t, vec2(0.0))) || any(greaterThan(t, vec2(1.0)))) continue;
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
        if (uShowMirror > 0.5) { FragColor = vec4(seen, 1.0); return; }
    }
    // Traced: what a glossy surface mirrors, as it is that way.
#ifdef SG_TRACED
    // (Traced only while its lobe is narrow enough that one ray a pixel is
    // nearly the answer; faded out towards uTrace.z, past which the probes'
    // and the sky's reflection - smooth, with no noise to smooth away - is
    // all. A rough reflection traced is a ray's noise smoothed into blotches
    // that crawl as the history turns over.)
    if (traced && uTrace.z > 0.0 && roughness <= uTrace.z)
        mirrored = mix(mirrored, texelFetch(uTraceMirror, trace_px, 0).rgb, 1.0 - smoothstep(0.5 * uTrace.z, uTrace.z, roughness));
#endif
    vec3 ambient = (diffuse * around * (1.0 - reflected) + mirrored * reflected * horizon + bounced) * (1.0 - uFillCut);
    // What its own hollows shut out of the light from all round and back off
    // the room (a surface map's occlusion): the lamps' direct light still
    // reaches where they shine.
    ambient *= occlusion;

    vec3 color = (ambient + direct) * (1.0 - uUnlit) + albedo * (mEmissive + uGlow) * (1.0 - uDark);
    // Where the dark stands (a fog that takes light), the light on what is in it is taken.
    if (uFogCount > 0) color *= exp(-3.0 * dark_at(vLit));
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
    float air_depth = uFogDensity * max(air_past(len, uFogStart) - air_past(t, uFogStart), 0.0);
    // (Thinner up high, where the look says so: along the way it fogs.)
    if (uFogFalloff > 0.0 && air_depth > 0.0) {
        float from = max(t, min(uFogStart, len));
        air_depth *= air_height(uViewPos + to_frag * (from / max(len, 1e-5)), vWorld);
    }
    // And where the way crosses fog pooled in this world (fog_volumes_glsl),
    // that too - of its own colour where it has one, else of this air's.
    vec4 own;
    float pooled = fog_volume_depth(uViewPos + to_frag * (t / max(len, 1e-5)), vWorld, own);
    float fog = 1.0 - exp(-air_depth - pooled);
    float sun_cos = dot(normalize(to_frag), normalize(uSunDir + vec3(0.0, 1e-4, 0.0)));
    float toward = uFogSunG > 0.0 ? uFogSunAmount * sun_phase(sun_cos) * 4.0 : pow(max(sun_cos, 0.0), 6.0);
    // Air that takes all at last is the background too: as far as it goes,
    // what is seen is what is beyond the last thing drawn.
    vec3 fog_color = (uFogColor * (air_depth + pooled - own.w) + own.rgb) / max(air_depth + pooled, 1e-6);
    vec3 haze = (air_depth + pooled > 1e-6 ? fog_color : uFogColor) + uSunColor * toward * 0.25 * (1.0 - uFogFull);
    fog = clamp(fog, 0.0, mix(0.85, 1.0, uFogFull));
    if (uAdd > 0.5) {
        // Light added over what is behind: the air between takes some of it,
        // and gives nothing of its own (that is in what is behind already).
        // Where it has none it is not there at all - not even as a depth:
        // what is behind is what is seen there, as far off as it is.
        if (max(max(color.r, color.g), color.b) < 0.01) discard;
        color *= 1.0 - fog;
    } else {
    color = mix(color, haze, fog);
    // And the light of this world's lamps that its air scatters towards the
    // eye on the way (air_light: none unless its look says the air scatters).
    color += air_light(len);
    // And seen through the dark, all of it taken as much as the dark is deep
    // on the way: a hole in the light, black against what is lit behind it.
    if (uFogCount > 0) color *= exp(-dark_depth(uViewPos + to_frag * (t / max(len, 1e-5)), vWorld));
    // And the planet's air on the way, as much of it as the look says.
    if (uAtmo > 0.0) color = atmo_between(color, to_frag / max(len, 1e-5), len);
    if (uHostFog.w > 0.5) color = mix(color, uHostFogColor, clamp(1.0 - exp(-uHostFog.x * max(t - uHostFog.y, 0.0)), 0.0, mix(0.85, 1.0, uHostFog.z)));
    }

    // The eye adjusted to a screen: the room around it dims, the picture
    // on the screen does not. Alpha: the share of it occlusion may darken.
    // Glass: as much of it drawn over what is behind as it is not clear,
    // and more where it is seen edge on (Schlick's Fresnel) - its alpha, as
    // the glass pass blends it (the alpha behind it kept).
    if (uGlass > 0.0) {
        float facing = abs(dot(n, normalize(-to_frag)));
        float fresnel = 0.04 + 0.96 * pow(1.0 - facing, 5.0);
        FragColor = vec4(color * (uCRT > 0.0 ? 1.0 : (1.0 - max(uDim, uDimNear) * (1.0 - uUndim))), clamp(mix(1.0 - uGlass, 1.0, fresnel), 0.0, 1.0));
        return;
    }
    // (Dimmed round a screen leaned in to, or round what is held up to read
    // - but not what it is: a screen, a page that says `undim`.)
    FragColor = vec4(color * (uCRT > 0.0 ? 1.0 : (1.0 - max(uDim, uDimNear) * (1.0 - uUndim))), indirect * (1.0 - fog));
    // (Light added over what is behind takes none: its diffuse is nothing.)
    if (uAdd < 0.5)
        Diffuse = vec4(diffuse * occlusion * (1.0 - uFillCut) * (1.0 - uUnlit) * (1.0 - fog) * (uCRT > 0.0 ? 1.0 : (1.0 - max(uDim, uDimNear) * (1.0 - uUndim))), 1.0);
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
uniform float uFogReversed;  // 1: the depth is reversed (near 1, far 0)
float fog_bloom(vec2 uv) {
    if (uFogBloom <= 0.0) return 0.0;
    float d = texture(uDepth, uv).r;
    float n = uDepthView.x, f = uDepthView.y;
    // Along the ray, not the view's axis; what nothing stands in front of
    // is as far as the view goes.
    float z = uFogReversed > 0.5 ? (d <= 0.0 ? f : n * f / (n + d * (f - n)))
                                 : (d >= 1.0 ? f : 2.0 * n * f / (f + n - (d * 2.0 - 1.0) * (f - n)));
    vec2 p = (uv * 2.0 - 1.0) * vec2(uDepthView.z * uDepthView.w, uDepthView.z);
    float tau = uAirThick.x * air_past(z * length(vec3(p, 1.0)), uAirThick.y);
    return uFogBloom * min(tau, uFogBloomCap > 0.0 ? uFogBloomCap : 3.0);
}
)";
}

const char* deep_view_fs() {
    return R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uViewDepth;
uniform vec4 uScreenRect;
uniform vec4 uUVRect;
uniform vec2 uViewport;
uniform vec2 uViewNF;
uniform vec2 uDrawNF;
uniform float uReversed;
void main() {
    vec2 on_screen = gl_FragCoord.xy / uViewport;
    vec2 uv = (on_screen - uScreenRect.xy) / max(uScreenRect.zw - uScreenRect.xy, vec2(1e-4));
    if (uUVRect.z > 0.0) uv = uUVRect.xy + uv * uUVRect.zw;
    float d = textureLod(uViewDepth, uv, 0.0).r;
    float ds = uReversed > 0.5 ? 1.0 - d : d;
    float vn = uViewNF.x, vf = uViewNF.y, n = uDrawNF.x, f = uDrawNF.y;
    // How far along the view's axis (the view's eye is this one carried: the
    // same axis, the same distances).
    float lin = ds >= 0.999999 ? f : 2.0 * vn * vf / (vf + vn - (ds * 2.0 - 1.0) * (vf - vn));
    lin = clamp(lin, n, f);
    float out_d = 0.5 * ((f + n - 2.0 * n * f / lin) / (f - n)) + 0.5;
    if (uReversed > 0.5) out_d = 1.0 - out_d;
    // Never nearer than the doorway itself.
    gl_FragDepth = uReversed > 0.5 ? min(out_d, gl_FragCoord.z) : max(out_d, gl_FragCoord.z);
    FragColor = vec4(0.0);
}
)";
}

const char* exposure_meter_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform vec2 uCell;  // one pixel of the meter, in the scene's 0..1
uniform float uSpot; // how wide the middle that counts most is (a part of the view)
uniform float uEdge; // how much the rest counts beside it
void main() {
    // Four looks about the cell, so a lamp or a dark seam between two
    // samples is not missed - each as bright as its light, not its colour.
    float sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        vec2 o = (vec2(i & 1, i >> 1) - 0.5) * 0.5 * uCell;
        vec3 c = texture(uScene, vUV + o).rgb;
        sum += log2(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1.0 / 16384.0));
    }
    // The middle of the view counts most, as the eye looks at it (as wide as
    // the look says: `exposure.spot`); the edges as much as it says beside
    // it (`exposure.edge`), so a lamp at the edge is not all ignored.
    vec2 d = vUV - 0.5;
    float w = uEdge + exp(-dot(d, d) / (2.0 * uSpot * uSpot));
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
uniform float uRaw;  // 1: the light itself, undeveloped (a viewer's film: the eye that looks at it develops it)
// What the eye's lens keeps sharp (its camera's `focus`): where on the
// screen (0..1), how far round it all is sharp (a part of the screen's
// height), how soft at most beyond (levels of the picture's mipmaps) - and
// over how much more it goes from sharp to that (uFocusFall).
uniform vec4  uFocus;
uniform float uFocusFall;
uniform float uFringe;     // the lens's colour fringe: pixels the red and the blue part at the frame's corner (0: none)
uniform float uBarrel;     // a wide lens's barrel: how far the frame's corner is bowed out (0: none; 0.05 a little)
uniform float uHalation;   // film's red glow round what is brightest (0: none)
uniform float uSharpen;    // what a camera's processing gives back of the detail its smoothing over frames took (0: none, 1 most)
uniform float uSharpenHalo; // a phone's or camcorder's sharpening: an unsharp mask left unclamped - its edges ringed light and dark (0: none)
uniform float uEdgeSmear;  // a cheap lens's edges: each point smeared out along its line from the middle, its colours parted along it - red out, blue in (pixels at the corner; 0: none)
uniform float uReadout;    // a rolling shutter: seconds its rows take to be read, top to bottom (0: a global shutter)
uniform vec2  uTurnRate;   // how fast the view turns across the screen, parts of it a second (the renderer's)
uniform float uCornerSoft; // a cheap lens's corners, softer than its middle (0: none, 1 most)
uniform float uChromaHalf; // video's colour: carried at half the pixels each way, its brightness at all (0: none, 1 video's 4:2:0)
uniform float uDarkChroma; // a sensor's darks losing their colour: how much of it the deepest keep less (0: none, 1 grey)
)") + film_glsl() + godrays_glsl() + fxaa_glsl() + fog_bloom_glsl() + R"(

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// The scene at a pixel, its detail given back as much as it can take: each
// pixel pushed from its four neighbours by as much as their spread leaves
// room for (contrast-adaptive - least where an edge is already hard, so
// nothing rings), in light pressed into 0..1 so a lamp does not throw a
// dark halo round it; never past what the five of them span.
vec3 scene_at(vec2 uv) {
    vec3 c = texture(uScene, uv).rgb;
    // (A cheap camera's sharpening: the picture pushed from its own blur and
    // nothing to hold it - each edge gets its halo, dark one side, light the
    // other, as video's processing leaves them.)
    if (uSharpenHalo > 0.0) {
        vec3 pc = c / (1.0 + c), pb = textureLod(uScene, uv, 1.2).rgb;
        pb = pb / (1.0 + pb);
        vec3 p = clamp(pc + uSharpenHalo * (pc - pb), 0.0, 0.995);
        c = p / (1.0 - p);
    }
    if (uSharpen <= 0.0) return c;
    vec3 n = texture(uScene, uv + vec2(0.0, uTexel.y)).rgb, s = texture(uScene, uv - vec2(0.0, uTexel.y)).rgb;
    vec3 e = texture(uScene, uv + vec2(uTexel.x, 0.0)).rgb, w = texture(uScene, uv - vec2(uTexel.x, 0.0)).rgb;
    vec3 pc = c / (1.0 + c), pn = n / (1.0 + n), ps = s / (1.0 + s), pe = e / (1.0 + e), pw = w / (1.0 + w);
    vec3 lo = min(pc, min(min(pn, ps), min(pe, pw))), hi = max(pc, max(max(pn, ps), max(pe, pw)));
    vec3 amp = sqrt(clamp(min(lo, 1.0 - hi) / max(hi, vec3(1e-4)), 0.0, 1.0));
    vec3 k = -amp * mix(0.125, 0.2, clamp(uSharpen, 0.0, 1.0));
    vec3 p = clamp((pc + k * (pn + ps + pe + pw)) / (1.0 + 4.0 * k), lo, hi);
    return p / max(1.0 - p, vec3(1e-4));
}

// The scene as the lens sees it: sharp round its point of focus, softer the
// further from it on the screen (a tilt-shift turned round that point, as a
// small world's photograph is) - each point a disc of the picture as wide as
// it is out of focus, filled: taps spread over it evenly (a sunflower's
// spiral), each read from the mipmap a level finer than the disc so they
// overlap and no copy of an edge stands apart from another.
vec3 focused(vec2 uv) {
    vec3 sharp = scene_at(uv);
    // (A cheap lens's corners: softer the further from its middle.)
    if (uCornerSoft > 0.0) {
        vec2 d = (uv - 0.5) * vec2(uTexel.y / uTexel.x, 1.0);
        float k = smoothstep(0.3, 0.95, length(d) * 1.2) * uCornerSoft;
        if (k > 0.0) sharp = mix(sharp, textureLod(uScene, uv, 1.3).rgb, k);
    }
    if (uFocus.w <= 0.0) return sharp;
    vec2 d = (uv - uFocus.xy) * vec2(uTexel.y / uTexel.x, 1.0);
    float k = smoothstep(uFocus.z, uFocus.z + max(uFocusFall, 1e-3), length(d));
    if (k <= 0.0) return sharp;
    float lod = k * uFocus.w, reach = exp2(lod) * 1.25;
    float level = max(lod - 1.5, 0.0);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 24; ++i) {
        float a = float(i) * 2.3999632, r = sqrt((float(i) + 0.5) / 24.0) * reach;
        sum += textureLod(uScene, uv + vec2(cos(a), sin(a)) * r * uTexel, level).rgb;
    }
    return mix(sharp, sum / 24.0, smoothstep(0.0, 0.2, k));
}

// Where a wide lens takes this pixel's light from: straight lines bowed out
// from the middle, more the further out (r^2), the picture drawn a little
// larger so the bowing still fills the frame's corners.
vec2 lens_uv(vec2 uv) {
    if (uBarrel <= 0.0) return uv;
    vec2 d = (uv - 0.5) * vec2(uTexel.y / uTexel.x, 1.0);
    float corner = dot(vec2(0.5 * uTexel.y / uTexel.x, 0.5), vec2(0.5 * uTexel.y / uTexel.x, 0.5));
    float k = uBarrel / max(corner, 1e-4);
    // (The corner reads the corner: the middle is drawn larger, the edges
    // gathered in - a barrel.)
    vec2 bowed = d * (1.0 + k * dot(d, d)) / (1.0 + k * corner);
    return 0.5 + bowed * vec2(uTexel.x / uTexel.y, 1.0);
}

void main() {
    vec2 uv = lens_uv(vUV);
    // A rolling shutter: its rows read top to bottom, each a little later -
    // the view turning, each row is where the view had turned to by then: a
    // turn leans what stands upright.
    if (uReadout > 0.0) uv -= uTurnRate * uReadout * (0.5 - vUV.y);
    vec3 scene = focused(uv);
    // (Its colours parted towards the edges, red out and blue in, as a
    // lens's glass bends them unevenly.)
    if (uFringe > 0.0) {
        vec2 d = uv - 0.5, off = d * dot(d, d) * 2.0 * uFringe * uTexel;
        scene = vec3(focused(uv + off).r, scene.g, focused(uv - off).b);
    }
    // (A cheap wide lens toward its edges: each point drawn out along its
    // line from the middle - a zoom blur that grows as the square of how far
    // out - and its colours spread along that line, red to the outside, blue
    // to the inside: the edge's aberration as a smear, not two copies.)
    if (uEdgeSmear > 0.0) {
        vec2 d = uv - 0.5;
        vec2 reach = d * dot(d, d) * 4.0 * uEdgeSmear * uTexel;
        vec3 sum = vec3(0.0), wsum = vec3(0.0);
        for (int i = 0; i < 11; ++i) {
            float t = float(i) / 10.0 * 2.0 - 1.0;  // -1 inside .. +1 outside
            vec3 w = vec3(smoothstep(-0.6, 1.0, t), 1.0 - abs(t) * 0.8, smoothstep(0.6, -1.0, t));
            sum += texture(uScene, uv + reach * t).rgb * w;
            wsum += w;
        }
        vec3 smeared = sum / wsum;
        float k = smoothstep(0.05, 0.45, length(d * vec2(uTexel.y / uTexel.x, 1.0)) * 1.6);
        scene = mix(scene, smeared, k);
    }
    // (Film's halation: the light of what is brightest come back through the
    // film red, round it - seen where it falls on what is darker, lost on
    // what is as bright.)
    if (uHalation > 0.0) {
        vec3 glow = texture(uBloom, uv).rgb;
        float under = 1.0 - smoothstep(0.25, 1.0, luma(scene * uExposure));
        scene += glow * vec3(1.0, 0.32, 0.12) * uHalation * under;
    }
    if (uRaw > 0.5) {
        // (Its bloom is the developing eye's own, laid on what it sees.)
        FragColor = vec4(scene + texture(uBloom, uv).rgb * fog_bloom(uv) + godrays(uv), 1.0);
        return;
    }
    scene += texture(uBloom, uv).rgb * (uBloomStrength + fog_bloom(uv));
    scene += godrays(uv);
    // (A video sensor's noise, in the light as exposed, before the curve.)
    if (uSensor.x > 0.0) scene += sensor_noise(scene * uExposure, gl_FragCoord.xy, uTime) / max(uExposure, 1e-4);
    vec3 color = smooth_edges(uv, tonemap(scene * uExposure));
    // (Video's colour: its brightness every pixel's, its colour a block of
    // four's - read from the picture a level down, through the same curve.)
    if (uChromaHalf > 0.0) {
        vec3 low = tonemap((textureLod(uScene, uv, 1.0).rgb + texture(uBloom, uv).rgb * uBloomStrength) * uExposure);
        vec3 swapped = luma(color) + (low - luma(low));
        color = mix(color, max(swapped, vec3(0.0)), uChromaHalf);
    }

    color = mix(vec3(luma(color)), color, uSaturation) * uTint;
    // (A sensor's darks: few photons, its colour the first to go - the
    // shadows grey towards black while the lit keep theirs.)
    if (uDarkChroma > 0.0) color = mix(vec3(luma(color)), color, 1.0 - uDarkChroma * (1.0 - smoothstep(0.015, 0.22, luma(color))));

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
uniform float uDebandReversed;  // 1: the depth is reversed: nothing drawn is 0
bool deband_empty(vec2 uv) {
    float d = texture(uDepth, uv).r;
    return uDebandReversed > 0.5 ? d <= 1e-7 : d >= 0.9999999;
}
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
