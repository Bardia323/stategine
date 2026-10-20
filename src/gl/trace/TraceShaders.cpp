#include "TraceShaders.hpp"

#include "sg/gl/Shaders.hpp"

namespace sg::render::trace {

namespace {

// What every pass that reads the eye's surfaces has: where each pixel's
// surface is, which way it faces, how rough it is.
const char* kSurfaces = R"(
in vec2 vUV;
out vec4 FragColor;
// The eye's view drawn as what each pixel's surface is (the scene's
// uSurfaceOnly 5): which way it faces (an octahedron's face, x y), how far
// from the eye (z: 0 where there is none - the sky), how rough (w).
uniform sampler2D uSurf;
uniform vec2 uSurfSize;      // its pixels
uniform vec3 uEye;           // where the eye is
uniform vec3 uFwd, uRight, uUp;  // its way, and across and up its picture to its edges
uniform vec2 uJitter;        // the frame's part of a pixel, as the picture's -1..1 has it
uniform int  uStep;          // pixels of the surfaces to one of this pass's, each way
uniform int  uFrame;

vec3 oct_normal(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}
vec3 eye_way(ivec2 q) {
    vec2 ndc = (vec2(q) + 0.5) / uSurfSize * 2.0 - 1.0 - uJitter;
    return normalize(uFwd + ndc.x * uRight + ndc.y * uUp);
}
// The surface a pixel of this pass stands for (of a part of the pixels: the
// first of its square).
ivec2 surf_pixel() { return ivec2(gl_FragCoord.xy) * uStep; }
bool surface_at(ivec2 q, out vec3 p, out vec3 n, out float dist, out float rough) {
    vec4 s = texelFetch(uSurf, q, 0);
    dist = s.z;
    if (dist <= 0.0) return false;
    n = oct_normal(s.xy);
    rough = s.w;
    p = uEye + eye_way(q) * dist;
    return true;
}

// Numbers that wander (PCG): a pixel's own, new each frame.
uint pcg(uint v) {
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}
float rnd(inout uint s) { s = pcg(s); return float(s >> 8) * (1.0 / 16777216.0); }
uint seed_here(uint salt) { return pcg(uint(gl_FragCoord.x) + pcg(uint(gl_FragCoord.y) + pcg(uint(uFrame) * 9781u + salt))); }

const float kTurn = 6.28318531;  // (kPi is the air's: atmosphere_glsl)
void frame_of(vec3 n, out vec3 t, out vec3 b) {
    // (Frisvad's, made safe at the pole: Duff et al.)
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z), c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}
vec3 cosine_way(vec3 n, vec2 u) {
    float r = sqrt(u.x), phi = kTurn * u.y;
    vec3 t, b;
    frame_of(n, t, b);
    return normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u.x)));
}
)";

// The world as rays meet it: things (a tree of their boxes, two levels -
// the instances', and each shape's triangles in its own frame), and the
// ground (its heights round the eye, in two squares, each with the highest of
// every square of it in its levels).
const char* kWorld = R"(
uniform samplerBuffer uNodes;   // the trees' nodes, four texels each (Bvh.hpp)
uniform samplerBuffer uTris;    // a triangle's corner, its two edges (three texels); its colour, its mask
uniform samplerBuffer uInsts;   // an instance: the world to its frame (three rows), its tree, tint, glow, sides
uniform samplerBuffer uMasks;   // a cut-out triangle's 64 parts, which are solid (four of 16 bits)
uniform int  uTlas;             // the instances' tree's root (-1: none)
uniform sampler2D uHfPoint0, uHfPoint1;  // the ground's heights at the points of each square
// The highest of each cell, and of each two by two of those, and so on: the
// levels one over another in one picture, the first at its foot.
uniform sampler2D uHfMax0, uHfMax1;
int level_row(int level, int n) { return level == 0 ? 0 : 2 * n - (n >> (level - 1)); }
uniform vec4 uHfAt0, uHfAt1;    // each square's corner (x, z), metres a cell, points a side (0: none)
uniform int  uHfTop0, uHfTop1;  // each one's last level
uniform vec3 uHfAlbedo;         // the ground's colour (linear)

struct Hit {
    float t;
    int inst, tri;    // the instance and triangle met (inst -1, tri 0: the ground)
    vec2 bc;
    vec3 n;           // which way what was met faces (the ground's: from its triangle)
};
Hit no_hit(float tmax) { Hit h; h.t = tmax; h.inst = -1; h.tri = -1; h.bc = vec2(0.0); h.n = vec3(0.0, 1.0, 0.0); return h; }

vec3 safe_inv(vec3 d) {
    return 1.0 / vec3(abs(d.x) > 1e-12 ? d.x : (d.x >= 0.0 ? 1e-12 : -1e-12), abs(d.y) > 1e-12 ? d.y : (d.y >= 0.0 ? 1e-12 : -1e-12),
                      abs(d.z) > 1e-12 ? d.z : (d.z >= 0.0 ? 1e-12 : -1e-12));
}
vec2 slab(vec3 lo, vec3 hi, vec3 oi, vec3 inv) {
    vec3 a = lo * inv + oi, b = hi * inv + oi;
    vec3 mn = min(a, b), mx = max(a, b);
    return vec2(max(max(mn.x, mn.y), mn.z), min(min(mx.x, mx.y), mx.z));
}

// A cut-out triangle's 64 parts (eight along each edge): whether the part
// at (u, v) is solid. The parts pointing as the triangle does are numbered
// i + 8 j (i + j <= 7); those pointing the other way, from the far corner.
bool solid_at(int k, float u, float v) {
    vec4 m = texelFetch(uMasks, k);
    float fu = u * 8.0, fv = v * 8.0;
    int i = min(int(fu), 7), j = min(int(fv), 7);
    int bit = (fract(fu) + fract(fv) <= 1.0 || i + j >= 7) ? i + 8 * j : (7 - i) + 8 * (7 - j);
    int w = bit >> 4;
    float word = w == 0 ? m.x : (w == 1 ? m.y : (w == 2 ? m.z : m.w));
    return ((int(word) >> (bit & 15)) & 1) != 0;
}

bool tri_meet(vec3 o, vec3 d, int tri, float tmax, out float t, out vec2 bc) {
    vec4 a = texelFetch(uTris, tri * 3), e1 = texelFetch(uTris, tri * 3 + 1), e2 = texelFetch(uTris, tri * 3 + 2);
    vec3 p = cross(d, e2.xyz);
    float det = dot(e1.xyz, p);
    t = tmax;
    if (abs(det) < 1e-14) return false;
    float inv = 1.0 / det;
    vec3 s = o - a.xyz;
    float u = dot(s, p) * inv;
    if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(s, e1.xyz);
    float v = dot(d, q) * inv;
    if (v < 0.0 || u + v > 1.0) return false;
    float tt = dot(e2.xyz, q) * inv;
    if (tt <= 0.0 || tt >= tmax) return false;
    if (e1.w > 0.5 && !solid_at(int(e1.w) - 1, u, v)) return false;
    t = tt, bc = vec2(u, v);
    return true;
}

// One shape's triangles, in its own frame (o, d there), nearer first; `any`:
// the first met will do (a shadow's question).
void shape_trace(int root, vec3 o, vec3 d, int inst, bool any, inout Hit h) {
    vec3 inv = safe_inv(d), oi = -o * inv;
    int stack[32];
    float near[32];
    int sp = 0, node = root;
    for (int guard = 0; guard < 4096; ++guard) {
        int at = node * 4;
        vec4 l0 = texelFetch(uNodes, at), l1 = texelFetch(uNodes, at + 1), r0 = texelFetch(uNodes, at + 2), r1 = texelFetch(uNodes, at + 3);
        vec2 tl = slab(l0.xyz, l1.xyz, oi, inv), tr = slab(r0.xyz, r1.xyz, oi, inv);
        bool hl = tl.x <= tl.y && tl.y > 0.0 && tl.x < h.t;
        bool hr = tr.x <= tr.y && tr.y > 0.0 && tr.x < h.t;
        if (hl && l1.w > 0.5) {
            int first = int(l0.w), n = int(l1.w);
            for (int k = 0; k < 8; ++k) {
                if (k >= n) break;
                float t;
                vec2 bc;
                if (tri_meet(o, d, first + k, h.t, t, bc)) {
                    h.t = t, h.tri = first + k, h.inst = inst, h.bc = bc;
                    if (any) return;
                }
            }
            hl = false;
        }
        if (hr && r1.w > 0.5) {
            int first = int(r0.w), n = int(r1.w);
            for (int k = 0; k < 8; ++k) {
                if (k >= n) break;
                float t;
                vec2 bc;
                if (tri_meet(o, d, first + k, h.t, t, bc)) {
                    h.t = t, h.tri = first + k, h.inst = inst, h.bc = bc;
                    if (any) return;
                }
            }
            hr = false;
        }
        if (hl && hr) {
            bool left_first = tl.x <= tr.x;
            if (sp < 32) stack[sp] = left_first ? int(r0.w) : int(l0.w), near[sp] = left_first ? tr.x : tl.x, ++sp;
            node = left_first ? int(l0.w) : int(r0.w);
        } else if (hl) {
            node = int(l0.w);
        } else if (hr) {
            node = int(r0.w);
        } else {
            // (What was put by for later, unless something nearer is met now.)
            bool found = false;
            while (sp > 0) {
                --sp;
                if (near[sp] < h.t) {
                    node = stack[sp];
                    found = true;
                    break;
                }
            }
            if (!found) return;
        }
    }
}

// The instances: each met box's shape traced in its own frame.
void things_trace(vec3 o, vec3 d, bool any, inout Hit h) {
    if (uTlas < 0) return;
    vec3 inv = safe_inv(d), oi = -o * inv;
    int stack[32];
    float near[32];
    int sp = 0, node = uTlas;
    for (int guard = 0; guard < 4096; ++guard) {
        int at = node * 4;
        vec4 l0 = texelFetch(uNodes, at), l1 = texelFetch(uNodes, at + 1), r0 = texelFetch(uNodes, at + 2), r1 = texelFetch(uNodes, at + 3);
        vec2 tl = slab(l0.xyz, l1.xyz, oi, inv), tr = slab(r0.xyz, r1.xyz, oi, inv);
        bool hl = tl.x <= tl.y && tl.y > 0.0 && tl.x < h.t;
        bool hr = tr.x <= tr.y && tr.y > 0.0 && tr.x < h.t;
        for (int side = 0; side < 2; ++side) {
            bool here = side == 0 ? hl : hr;
            vec4 c0 = side == 0 ? l0 : r0, c1 = side == 0 ? l1 : r1;
            if (!here || c1.w < 0.5) continue;
            int first = int(c0.w), n = int(c1.w);
            for (int k = 0; k < 4; ++k) {
                if (k >= n) break;
                int i = (first + k) * 4;
                vec4 m0 = texelFetch(uInsts, i), m1 = texelFetch(uInsts, i + 1), m2 = texelFetch(uInsts, i + 2), m3 = texelFetch(uInsts, i + 3);
                vec3 lo = vec3(dot(m0, vec4(o, 1.0)), dot(m1, vec4(o, 1.0)), dot(m2, vec4(o, 1.0)));
                vec3 ld = vec3(dot(m0.xyz, d), dot(m1.xyz, d), dot(m2.xyz, d));
                shape_trace(int(m3.x), lo, ld, first + k, any, h);
                if (any && h.tri >= 0) return;
            }
            if (side == 0) hl = false; else hr = false;
        }
        if (hl && hr) {
            bool left_first = tl.x <= tr.x;
            if (sp < 32) stack[sp] = left_first ? int(r0.w) : int(l0.w), near[sp] = left_first ? tr.x : tl.x, ++sp;
            node = left_first ? int(l0.w) : int(r0.w);
        } else if (hl) {
            node = int(l0.w);
        } else if (hr) {
            node = int(r0.w);
        } else {
            bool found = false;
            while (sp > 0) {
                --sp;
                if (near[sp] < h.t) {
                    node = stack[sp];
                    found = true;
                    break;
                }
            }
            if (!found) return;
        }
    }
}

// The ground of one square, between t0 and t1 along the ray: down through
// the levels of its highest where the ray passes under them, on over a whole
// square of cells at once where it passes over (Tevs et al.'s maximum
// mipmaps), and at a cell, its two triangles.
bool ground_square(sampler2D points, sampler2D most, vec4 at, int top, vec3 o, vec3 d, float t0, float t1, out float t, out vec3 n) {
    t = t1;
    n = vec3(0.0, 1.0, 0.0);
    if (at.w < 2.0) return false;
    float cells = at.w - 1.0;
    vec2 o2 = (o.xz - at.xy) / at.z, d2 = d.xz / at.z;
    vec2 inv2 = 1.0 / vec2(abs(d2.x) > 1e-12 ? d2.x : 1e-12, abs(d2.y) > 1e-12 ? d2.y : 1e-12);
    vec2 a = (vec2(0.0) - o2) * inv2, b = (vec2(cells) - o2) * inv2;
    float ta = max(max(min(a.x, b.x), min(a.y, b.y)), t0), tb = min(min(max(a.x, b.x), max(a.y, b.y)), t1);
    if (ta >= tb) return false;
    vec2 ahead = vec2(d2.x >= 0.0 ? 1.0 : 0.0, d2.y >= 0.0 ? 1.0 : 0.0);
    float tc = ta;
    int level = top;
    for (int i = 0; i < 256; ++i) {
        if (tc >= tb) return false;
        vec2 p = clamp(o2 + d2 * tc, vec2(0.0), vec2(cells - 1e-3));
        float size = exp2(float(level));
        vec2 cell = floor(p / size);
        float highest = texelFetch(most, ivec2(cell) + ivec2(0, level_row(level, int(at.w))), 0).r;
        vec2 edge = ((cell + ahead) * size - o2) * inv2;
        float tx = min(min(edge.x, edge.y), tb);
        float y0 = o.y + d.y * tc, y1 = o.y + d.y * tx;
        if (min(y0, y1) > highest) {
            tc = tx + 1e-4;
            level = min(level + 1, top);
            continue;
        }
        if (level > 0) {
            --level;
            continue;
        }
        // A cell: its two triangles, cut from its first corner to its last.
        ivec2 c = ivec2(cell);
        float h00 = texelFetch(points, c, 0).r, h10 = texelFetch(points, c + ivec2(1, 0), 0).r;
        float h01 = texelFetch(points, c + ivec2(0, 1), 0).r, h11 = texelFetch(points, c + ivec2(1, 1), 0).r;
        vec3 p00 = vec3(at.x + cell.x * at.z, h00, at.y + cell.y * at.z);
        vec3 ex = vec3(at.z, 0.0, 0.0), ez = vec3(0.0, 0.0, at.z);
        vec3 p10 = p00 + ex, p01 = p00 + ez, p11 = p00 + ex + ez;
        p10.y = h10, p01.y = h01, p11.y = h11;
        float best = tx + 1e-3;
        bool met = false;
        for (int k = 0; k < 2; ++k) {
            vec3 v0 = p00, v1 = k == 0 ? p10 : p11, v2 = k == 0 ? p11 : p01;
            vec3 e1 = v1 - v0, e2 = v2 - v0, q = cross(d, e2);
            float det = dot(e1, q);
            if (abs(det) < 1e-12) continue;
            float inv = 1.0 / det;
            vec3 s = o - v0;
            float u = dot(s, q) * inv;
            if (u < -1e-5 || u > 1.00001) continue;
            vec3 r = cross(s, e1);
            float v = dot(d, r) * inv;
            if (v < -1e-5 || u + v > 1.00001) continue;
            float tt = dot(e2, r) * inv;
            if (tt < tc - 1e-3 || tt > best) continue;
            best = tt, met = true;
            n = normalize(cross(e2, e1));
        }
        if (met && best > t0 && best < t1) {
            t = best;
            if (n.y < 0.0) n = -n;
            return true;
        }
        tc = tx + 1e-4;
    }
    return false;
}

// The ground (inst -1, tri 0 in what is met): its fine square near the eye,
// the coarse one round it beyond.
void ground_trace(vec3 o, vec3 d, inout Hit h) {
    if (uHfAt0.w < 2.0 && uHfAt1.w < 2.0) return;
    // Where the ray is over the fine square, the coarse one is not asked.
    float a = 0.0, b = 0.0;
    if (uHfAt0.w >= 2.0) {
        float span = (uHfAt0.w - 1.0) * uHfAt0.z;
        vec2 inv2 = 1.0 / vec2(abs(d.x) > 1e-12 ? d.x : 1e-12, abs(d.z) > 1e-12 ? d.z : 1e-12);
        vec2 lo = (uHfAt0.xy - o.xz) * inv2, hi = (uHfAt0.xy + span - o.xz) * inv2;
        a = max(max(min(lo.x, hi.x), min(lo.y, hi.y)), 0.0);
        b = max(min(min(max(lo.x, hi.x), max(lo.y, hi.y)), h.t), a);
    }
    float t;
    vec3 n;
    if (a > 0.0 && ground_square(uHfPoint1, uHfMax1, uHfAt1, uHfTop1, o, d, 0.0, min(a, h.t), t, n)) {
        h.t = t, h.inst = -1, h.tri = 0, h.n = n;
        return;
    }
    if (b > a && ground_square(uHfPoint0, uHfMax0, uHfAt0, uHfTop0, o, d, a, b, t, n)) {
        h.t = t, h.inst = -1, h.tri = 0, h.n = n;
        return;
    }
    if (ground_square(uHfPoint1, uHfMax1, uHfAt1, uHfTop1, o, d, max(b, 0.0), h.t, t, n)) h.t = t, h.inst = -1, h.tri = 0, h.n = n;
}

uniform int uSkip;     // (for timing: 1 no things, 2 no ground, 4 no shadow at a ray's end)
uniform float uReach;  // how far a ray of light come back meets things (the ground, as far as it goes)
// What a ray of light come back meets first: things within its reach, the
// ground however far.
Hit nearest(vec3 o, vec3 d, float tmax) {
    Hit h = no_hit(min(tmax, uReach));
    if ((uSkip & 1) == 0) things_trace(o, d, false, h);
    if (h.tri < 0) h.t = tmax;
    if ((uSkip & 2) == 0) ground_trace(o, d, h);
    return h;
}
bool blocked(vec3 o, vec3 d, float tmax) {
    Hit h = no_hit(tmax);
    if ((uSkip & 2) == 0) ground_trace(o, d, h);
    if (h.tri >= 0) return true;
    h.t = min(tmax, uReach);
    if ((uSkip & 1) == 0) things_trace(o, d, true, h);
    return h.tri >= 0;
}

vec3 unpack_colour(float f) {
    int v = int(f);
    vec3 c = vec3(float(v & 255), float((v >> 8) & 255), float((v >> 16) & 255)) / 255.0;
    return c * c;
}
// What was met: which way it faces (toward the ray, as a leaf's either
// side), its colour, how much it glows.
void what_met(Hit h, vec3 d, out vec3 n, out vec3 albedo, out float glow) {
    glow = 0.0;
    if (h.inst < 0) {
        n = h.n, albedo = uHfAlbedo;
    } else {
        int i = h.inst * 4;
        vec4 m0 = texelFetch(uInsts, i), m1 = texelFetch(uInsts, i + 1), m2 = texelFetch(uInsts, i + 2), m3 = texelFetch(uInsts, i + 3);
        vec4 a = texelFetch(uTris, h.tri * 3), e1 = texelFetch(uTris, h.tri * 3 + 1), e2 = texelFetch(uTris, h.tri * 3 + 2);
        vec3 on = cross(e1.xyz, e2.xyz);
        // (Into the world: by the inverse's transpose.)
        n = normalize(on.x * m0.xyz + on.y * m1.xyz + on.z * m2.xyz);
        albedo = unpack_colour(a.w) * unpack_colour(m3.y);
        glow = m3.z;
    }
    if (dot(n, d) > 0.0) n = -n;
}
)";

// The sky the rays leave into, as the look draws it - its colours, the
// planet's air, its clouds - but not the sun's disc, which is the sun's own
// light, asked of it directly.
const char* kSky = R"(
uniform float uAtmo, uAtmoScale, uAtmoAltitude, uAtmoSun;
uniform sampler2D uAtmoSky;
uniform float uCloudVolume;
uniform sampler2D uCloudDome;
uniform vec3 uSkyTop, uSkyHorizon;
uniform vec3 uSunDir;     // toward the sun
uniform vec3 uSunColor;   // the sky's sun (as the scene has it)
uniform vec3 uSunLight;   // the sun's light as it lights a surface facing it (its lamp's colour, power, the air)
uniform float uSunCone;   // the sine of the sun's radius as seen (0: a point)
uniform float uSkyEyeY;   // how high the eye is, metres
uniform float uSkyGain;   // the look's light from all round, as many times
vec3 trace_sky(vec3 dir) {
    float t = clamp(dir.y, -1.0, 1.0);
    vec3 c = mix(uSkyHorizon, uSkyTop, pow(max(t, 0.0), 0.45));
    c = mix(c, uSkyHorizon * 0.8, clamp(-t * 5.0, 0.0, 1.0));
    vec3 sd = normalize(uSunDir + vec3(0.0, 1e-4, 0.0));
    float s = max(dot(dir, sd), 0.0);
    c += uSunColor * (pow(s, 24.0) * 0.35 + pow(s, 4.0) * 0.1);
    if (uAtmo > 0.0) {
        float r = clamp(kGroundR + uAtmoAltitude + uSkyEyeY * uAtmoScale, kGroundR + 0.001, kTopR - 0.01);
        vec3 sky = texture(uAtmoSky, unit_to_texel(sky_view_uv(dir, sd, r), vec2(textureSize(uAtmoSky, 0)))).rgb * uSunColor * uAtmoSun;
        c = mix(c, sky, uAtmo);
    }
    if (uCloudVolume > 0.0 && dir.y > 0.0) {
        vec4 cloud = texture(uCloudDome, dome_uv(dir));
        c = mix(c, c * cloud.a + cloud.rgb, uCloudVolume * smoothstep(0.0, 0.03, dir.y));
    }
    return c * uSkyGain;
}
)";

// The light from all round a surface facing a way gets (the irradiance
// over pi, as the scene's `around` is), the sky's alone: an octahedron's
// picture of it, made by sky_fs.
const char* kSkyLight = R"(
uniform sampler2D uSkyLight;
vec2 oct_of(vec3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    vec2 e = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return e * 0.5 + 0.5;
}
vec3 sky_light(vec3 n) { return texture(uSkyLight, oct_of(n)).rgb; }

// A point of the sun's disc, as this pixel's number has it.
vec3 sun_way(vec2 u) {
    vec3 l = normalize(uSunDir + vec3(0.0, 1e-4, 0.0));
    if (uSunCone <= 0.0) return l;
    vec3 t, b;
    frame_of(l, t, b);
    float r = uSunCone * sqrt(u.x), phi = 2.0 * kPi * u.y;
    return normalize(l + t * (r * cos(phi)) + b * (r * sin(phi)));
}

// Lamps a ray's end may be lit by (the room's strongest few, round points).
const int MAX_TRACE_LAMPS = 8;
uniform int  uLampCount;
uniform vec4 uLampAt[MAX_TRACE_LAMPS];     // where, how far it reaches (0: everywhere)
uniform vec3 uLampLight[MAX_TRACE_LAMPS];  // its colour times its power
uniform vec4 uLampWay[MAX_TRACE_LAMPS];    // the way it points, and how it falls off (0 softly, 1 as the inverse square)
uniform vec2 uLampCone[MAX_TRACE_LAMPS];   // the cosines of its cone's inner and outer edges
uniform float uOpenSky;   // 1: the world is open to the sky - what a ray meets has the sky's light; 0, a room shut round
uniform float uBrightest; // what a ray brings back at most (its brightness): a bulb met close by is not a speck burning through
// A lamp's light at p (towards it, l), as the scene lights by it (light_reach:
// its cone, its falloff, its range), without what stands between.
vec3 lamp_at(int k, vec3 p, out vec3 l, out float d) {
    vec3 to = uLampAt[k].xyz - p;
    d = max(length(to), 1e-3);
    l = to / d;
    float theta = dot(-l, uLampWay[k].xyz);
    float cone = clamp((theta - uLampCone[k].y) / max(uLampCone[k].x - uLampCone[k].y, 1e-4), 0.0, 1.0);
    float soft = 1.0 / (1.0 + 0.22 * d + 0.14 * d * d), square = 1.0 / (1.0 + 2.0 * d * d);
    float window = 1.0;
    if (uLampAt[k].w > 0.0) {
        float q = d / uLampAt[k].w;
        q *= q;
        window = clamp(1.0 - q * q, 0.0, 1.0);
        window *= window;
    }
    return uLampLight[k] * mix(soft, square, uLampWay[k].w) * window * cone * cone;
}

uniform float uFar;   // how far a ray goes before it is taken for the sky

// The sun's share as the eye's own pixels have it already (sun_fs, this
// frame), where a ray's end is one of them: no ray of its own to the sun.
uniform sampler2D uSunSeen;
uniform int  uSunSeenOn;
uniform mat4 uVP;
float sun_seen(vec3 p) {
    if (uSunSeenOn == 0) return -1.0;
    vec4 c = uVP * vec4(p, 1.0);
    if (c.w <= 0.0) return -1.0;
    vec2 uv = c.xy / c.w * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0)))) return -1.0;
    ivec2 q = ivec2(uv * uSurfSize);
    float d = texelFetch(uSurf, q, 0).z, want = length(p - uEye);
    if (abs(d - want) > 0.01 * want + 0.05) return -1.0;
    return texelFetch(uSunSeen, q, 0).r;
}

// What lights a point a ray has reached, of the sun, the lamps and the sky:
// what comes off it toward the ray (its colour times what reaches it).
// (`sky`: and the sky's light from all round it - unless the path goes on
// round from here, and finds the sky itself.)
vec3 lit_end(vec3 p, vec3 n, vec3 albedo, float glow, inout uint seed, bool sky) {
    vec3 got = sky ? sky_light(n) * uOpenSky : vec3(0.0);
    vec3 l = sun_way(vec2(rnd(seed), rnd(seed)));
    float ndl = dot(n, l);
    if (ndl > 0.0 && dot(uSunLight, uSunLight) > 0.0) {
        float bias = 0.02 + 0.0005 * length(p - uEye);
        float seen = sun_seen(p);
        if (seen >= 0.0) got += uSunLight * ndl * seen;
        else if ((uSkip & 4) != 0 || !blocked(p + n * bias, l, uFar)) got += uSunLight * ndl;
    }
    if (uLampCount > 0) {
        // Every lamp's light here as if nothing stood between; one of them
        // chosen as much as it gives, asked if anything does - their sum
        // times what that one finds (no lamp's light here is lost, and the
        // strongest are asked most).
        vec3 lit[MAX_TRACE_LAMPS];
        float give[MAX_TRACE_LAMPS];
        float total = 0.0;
        for (int k = 0; k < MAX_TRACE_LAMPS; ++k) {
            give[k] = 0.0, lit[k] = vec3(0.0);
            if (k >= uLampCount) continue;
            vec3 l;
            float d;
            vec3 c = lamp_at(k, p, l, d) * max(dot(n, l), 0.0);
            lit[k] = c, give[k] = dot(c, vec3(0.2126, 0.7152, 0.0722));
            total += give[k];
        }
        if (total > 0.0) {
            float pick = rnd(seed) * total;
            int k = 0;
            for (int j = 0; j < MAX_TRACE_LAMPS; ++j) {
                if (j >= uLampCount) break;
                k = j;
                if (pick < give[j]) break;
                pick -= give[j];
            }
            vec3 l;
            float d;
            lamp_at(k, p, l, d);
            float bias = 0.01 + 0.0005 * length(p - uEye);
            if (give[k] > 0.0 && !blocked(p + n * bias, l, d - 0.05)) got += lit[k] * (total / give[k]);
        }
    }
    vec3 back = albedo * (got + glow);
    float bright = dot(back, vec3(0.2126, 0.7152, 0.0722));
    return bright > uBrightest ? back * (uBrightest / bright) : back;
}
)";

std::string with_world(const char* own) {
    return std::string("#version 330 core\n") + kSurfaces + gl::atmosphere_glsl() + gl::cloud_dome_glsl() + kWorld + kSky + kSkyLight + own;
}

}  // namespace

const std::string& sky_fs() {
    static const std::string source = std::string("#version 330 core\n") + R"(
in vec2 vUV;
out vec4 FragColor;
vec3 oct_normal(vec2 e) {
    vec3 n = vec3(e, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return normalize(n);
}
void frame_of(vec3 n, out vec3 t, out vec3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z), c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}
)" + gl::atmosphere_glsl() + gl::cloud_dome_glsl() + kSky + R"(
// Every texel a way a surface may face: the sky over its side, weighed by
// how squarely each part of it falls on it - 256 ways, the same for every
// texel (a Fibonacci spiral), so the picture is smooth.
void main() {
    vec3 n = oct_normal(vUV * 2.0 - 1.0);
    vec3 t, b;
    frame_of(n, t, b);
    vec3 sum = vec3(0.0);
    const int N = 256;
    for (int i = 0; i < N; ++i) {
        float u = (float(i) + 0.5) / float(N), phi = float(i) * 2.39996323;
        float r = sqrt(u);
        vec3 d = t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(1.0 - u);
        // (Below the horizon, the ground's light where nothing else is
        // known: the sky's own near the horizon, darkened.)
        sum += d.y >= 0.0 ? trace_sky(d) : trace_sky(normalize(vec3(d.x, 0.02, d.z))) * 0.25;
    }
    FragColor = vec4(sum / float(N), 1.0);
}
)";
    return source;
}

const std::string& sun_fs() {
    static const std::string source = with_world(R"(
// Each pixel's share of the sun: one way to a point of its disc, asked if
// anything stands between (r) - and how far off what stood there was (g), so
// the shadow can be smoothed as wide as its penumbra and no wider. b: the
// pixel's own distance, for the scene to know it is its own.
void main() {
    vec3 p, n;
    float dist, rough;
    ivec2 q = surf_pixel();
    if (!surface_at(q, p, n, dist, rough)) {
        FragColor = vec4(1.0, 0.0, 0.0, 0.0);
        return;
    }
    uint seed = seed_here(17u);
    vec3 l = sun_way(vec2(rnd(seed), rnd(seed)));
    // (Facing away, it is asked all the same, from its other side: a leaf
    // lit through from behind is shadowed as its far side is.)
    vec3 v = normalize(uEye - p);
    float bias = 0.015 + 0.0006 * dist;
    vec3 o = p + (dot(n, l) >= 0.0 ? n : -n) * bias + v * (0.2 * bias);
    Hit h = no_hit(uFar);
    ground_trace(o, l, h);
    if (h.tri < 0) things_trace(o, l, true, h);
    FragColor = h.tri >= 0 ? vec4(0.0, h.t, dist, 1.0) : vec4(1.0, 0.0, dist, 1.0);
}
)");
    return source;
}

const std::string& lamps_fs() {
    static const std::string source = with_world(R"(
// Each pixel's share of the nearest lamps' light through what stands between
// (r, g, b: one lamp each): one way to a point of each lamp's bulb - a disc
// as wide as the bulb, square to the way to it - asked if anything stands
// between. A thing near the bulb (its cage, its shade's rim) throws a shadow
// as soft as the bulb is wide against how far off it is; a thing touching
// the floor, a sharp one.
uniform int  uShadowLamps;
uniform vec4 uShadowLamp[3];   // where each is, and its bulb's radius
void main() {
    vec3 p, n;
    float dist, rough;
    ivec2 q = surf_pixel();
    if (!surface_at(q, p, n, dist, rough)) {
        FragColor = vec4(1.0, 1.0, 1.0, 0.0);
        return;
    }
    uint seed = seed_here(29u);
    vec3 v = normalize(uEye - p);
    float bias = 0.01 + 0.0006 * dist;
    vec3 seen = vec3(1.0);
    for (int k = 0; k < 3; ++k) {
        if (k >= uShadowLamps) break;
        vec3 c = uShadowLamp[k].xyz;
        float r = uShadowLamp[k].w;
        vec3 w = normalize(c - p);
        vec3 t, b;
        frame_of(w, t, b);
        float a = r * sqrt(rnd(seed)), phi = kTurn * rnd(seed);
        vec3 to = c + t * (a * cos(phi)) + b * (a * sin(phi)) - p;
        float d = length(to);
        vec3 l = to / max(d, 1e-4);
        // (Facing away, asked from its other side: a leaf lit through.)
        vec3 o = p + (dot(n, l) >= 0.0 ? n : -n) * bias + v * (0.2 * bias);
        // (Up to the bulb's own glass, not into it.)
        seen[k] = blocked(o, l, max(d - 1.5 * r - bias, 0.0)) ? 0.0 : 1.0;
    }
    FragColor = vec4(seen, 1.0);
}
)");
    return source;
}

const std::string& mirror_fs() {
    static const std::string source = with_world(R"(
// What a glossy surface mirrors: a way out picked as its microfacets turn
// it (GGX, the facets seen: Heitz's), and what lies that way, lit as
// a ray's end is lit (lit_end) - or the sky. Surfaces rougher than uRough are left
// to the scene's own (a: -1). a: how far the ray went.
uniform float uRough;
vec3 facet(vec3 v, float a, vec2 u) {
    // (In the surface's frame, v its z up.)
    vec3 vh = normalize(vec3(a * v.x, a * v.y, v.z));
    float lensq = vh.x * vh.x + vh.y * vh.y;
    vec3 t1 = lensq > 0.0 ? vec3(-vh.y, vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = cross(vh, t1);
    float r = sqrt(u.x), phi = 2.0 * kPi * u.y;
    float p1 = r * cos(phi), p2 = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(1.0 - p1 * p1) + s * p2;
    vec3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
    return normalize(vec3(a * nh.x, a * nh.y, max(0.0, nh.z)));
}
void main() {
    vec3 p, n;
    float dist, rough;
    ivec2 q = surf_pixel();
    if (!surface_at(q, p, n, dist, rough) || rough > uRough) {
        FragColor = vec4(0.0, 0.0, 0.0, -1.0);
        return;
    }
    uint seed = seed_here(53u);
    vec3 v = normalize(uEye - p);
    vec3 t, b;
    frame_of(n, t, b);
    vec3 vl = vec3(dot(v, t), dot(v, b), dot(v, n));
    float a = max(rough * rough, 0.002);
    vec3 m = facet(vl, a, vec2(rnd(seed), rnd(seed)));
    vec3 hm = t * m.x + b * m.y + n * m.z;
    vec3 d = reflect(-v, hm);
    // (Below the surface: the mirror way itself.)
    if (dot(d, n) <= 0.0) d = reflect(-v, n);
    float bias = 0.02 + 0.0006 * dist;
    vec3 o = p + n * bias;
    Hit h = nearest(o, d, uFar);
    if (h.tri < 0) {
        FragColor = vec4(trace_sky(d), uFar);
        return;
    }
    vec3 hp = o + d * h.t, hn, albedo;
    float glow;
    what_met(h, d, hn, albedo, glow);
    vec3 light = lit_end(hp, hn, albedo, glow, seed, true);
    FragColor = vec4(light, h.t);
}
)");
    return source;
}

const std::string& carry_fs() {
    static const std::string source = std::string("#version 330 core\n") + kSurfaces + R"(
// This frame's picture taken into what was kept: where each pixel's surface
// was last frame (uLastVP), if that is the same surface there - as far from
// the last eye as it should be, facing the same way - what was kept there
// is taken, by the four nearest of its pixels that are; the new is weighed in
// as one of as many frames as the history holds, up to uMost. a: how many
// frames it holds now. A pixel uncovered starts again from this frame.
// Mirrors (uMirror): what is seen in a mirror moves as its image does, not
// as the mirror: found where that image was.
uniform sampler2D uNew;
uniform sampler2D uKept;
uniform sampler2D uLastSurf;
uniform mat4 uLastVP;
uniform vec3 uLastEye;
uniform float uMost;
uniform float uMirror;
uniform vec2 uKeptSize;
// What was kept held to what this frame's neighbours say it could be: above
// 0, within that many of their spread of their mean (variance clipping);
// below 0, within their least and most (a shadow that moves leaves none of
// itself behind). 0: as kept.
uniform float uClamp;
void main() {
    vec4 now = texelFetch(uNew, ivec2(gl_FragCoord.xy), 0);
    vec3 lo = now.rgb, hi = now.rgb, m1 = vec3(0.0), m2 = vec3(0.0);
    if (uClamp != 0.0) {
        ivec2 size = textureSize(uNew, 0);
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) {
                vec3 c = texelFetch(uNew, clamp(ivec2(gl_FragCoord.xy) + ivec2(i, j), ivec2(0), size - 1), 0).rgb;
                lo = min(lo, c), hi = max(hi, c), m1 += c, m2 += c * c;
            }
        m1 /= 9.0, m2 /= 9.0;
        if (uClamp > 0.0) {
            vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));
            lo = m1 - uClamp * sigma, hi = m1 + uClamp * sigma;
        }
    }
    vec3 p, n;
    float dist, rough;
    if (!surface_at(surf_pixel(), p, n, dist, rough)) {
        FragColor = vec4(now.rgb, 0.0);
        return;
    }
    vec3 seen = p;
    if (uMirror > 0.5 && now.a > 0.0 && rough < 0.2) seen = uEye + normalize(p - uEye) * (dist + now.a);
    vec4 was = uLastVP * vec4(seen, 1.0);
    float held = 0.0;
    vec4 kept = vec4(0.0);
    if (was.w > 0.0) {
        vec2 at = (was.xy / was.w * 0.5 + 0.5) * uKeptSize - 0.5;
        vec2 f = fract(at);
        ivec2 base = ivec2(floor(at));
        float wsum = 0.0;
        float expect = length(p - uLastEye);
        for (int k = 0; k < 4; ++k) {
            ivec2 o = ivec2(k & 1, k >> 1), c = base + o;
            if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, ivec2(uKeptSize)))) continue;
            vec4 s = texelFetch(uLastSurf, c * uStep, 0);
            if (s.z <= 0.0) continue;
            // (The same surface: where it should be, facing as it does.)
            if (abs(s.z - expect) > 0.03 * expect + 0.05) continue;
            if (dot(oct_normal(s.xy), n) < 0.85) continue;
            float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y);
            kept += texelFetch(uKept, c, 0) * w;
            wsum += w;
        }
        if (wsum > 1e-3) kept /= wsum, held = kept.a;
    }
    // (What was kept, not a number - a picture never drawn into: none kept.)
    if (!(held >= 0.0 && held < 1e5) || any(isnan(kept.rgb)) || any(isinf(kept.rgb))) held = 0.0, kept = vec4(0.0);
    if (uClamp != 0.0 && held > 0.0) {
        vec3 was = kept.rgb;
        kept.rgb = clamp(kept.rgb, lo, hi);
        // (Held far from what it was: the history is younger than it says.)
        float moved = length(was - kept.rgb) / (length(was) + 1e-3);
        held = mix(held, min(held, 4.0), clamp(moved * 4.0, 0.0, 1.0));
    }
    float frames = min(held + 1.0, uMost);
    vec3 c = held > 0.0 ? mix(kept.rgb, now.rgb, 1.0 / frames) : now.rgb;
    FragColor = vec4(c, frames);
}
)";
    return source;
}

const std::string& smooth_fs() {
    static const std::string source = std::string("#version 330 core\n") + kSurfaces + R"(
// One pass of smoothing within surfaces (Dammertz's a-trous, 3 x 3 taps
// uSpread pixels apart): a neighbour counts as much as it lies in this
// pixel's plane and faces its way. The less the history holds (a), the
// wider it reaches. uPenumbra: a shadow, smoothed only as wide as its
// penumbra is on the picture (its blocker's distance in uHits' g).
uniform sampler2D uPicture;
uniform sampler2D uHits;
uniform int  uSpread;
uniform float uPenumbra;   // the sun's radius (its sine), or 0
uniform float uPixel;      // how wide a pixel is a metre from the eye
uniform float uSharp;      // surfaces smoother than it left as they are: a still pool's image is sharp
void main() {
    ivec2 q = ivec2(gl_FragCoord.xy);
    vec4 here = texelFetch(uPicture, q, 0);
    vec3 p, n;
    float dist, rough;
    if (!surface_at(surf_pixel(), p, n, dist, rough) || rough < uSharp) {
        FragColor = here;
        return;
    }
    if (uPenumbra > 0.0) {
        // (Only within a penumbra, and as wide as it is on the picture: the
        // blocker's distance times the sun's size, over a pixel's width
        // there. Wholly lit or wholly shadowed, held a while: left as it is.)
        bool soft = here.r > 0.02 && here.r < 0.98;
        float blocker = texelFetch(uHits, q, 0).g;
        float wide = blocker * uPenumbra * 2.0 / max(uPixel * dist, 1e-5);
        if (here.a > 3.0 && (!soft || (blocker > 0.0 && wide < 0.5 * float(uSpread)))) {
            FragColor = here;
            return;
        }
    }
    const float k[3] = float[](0.25, 0.5, 0.25);
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    ivec2 size = textureSize(uPicture, 0);
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            ivec2 c = q + ivec2(i, j) * uSpread;
            if (any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, size))) continue;
            vec3 cp, cn;
            float cd, cr;
            if (!surface_at(c * uStep, cp, cn, cd, cr)) continue;
            float plane = abs(dot(cp - p, n)) / (0.02 + 0.004 * dist * float(uSpread * uStep));
            float w = k[i + 1] * k[j + 1] * exp(-plane) * pow(max(dot(cn, n), 0.0), 32.0);
            sum += texelFetch(uPicture, c, 0) * w;
            wsum += w;
        }
    vec4 s = wsum > 1e-6 ? sum / wsum : here;
    FragColor = vec4(s.rgb, here.a);
}
)";
    return source;
}

const std::string& widen_fs() {
    static const std::string source = std::string("#version 330 core\n") + kSurfaces + R"(
// A picture made at every uStep-th pixel each way laid on every pixel: of
// the four nearest it was made at, those whose surface is this pixel's (in
// its plane, facing its way) by how near. a: this pixel's own distance, so
// the scene knows the picture is of the surface it draws.
uniform sampler2D uPicture;
uniform float uRough;   // above 0: only those of the surfaces no rougher than it
void main() {
    ivec2 q = ivec2(gl_FragCoord.xy);
    vec3 p, n;
    float dist, rough;
    if (!surface_at(q, p, n, dist, rough)) {
        FragColor = vec4(0.0);
        return;
    }
    vec2 at = (vec2(q) + 0.5) / float(uStep) - 0.5;
    ivec2 base = ivec2(floor(at));
    vec2 f = fract(at);
    ivec2 size = textureSize(uPicture, 0);
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    vec3 any_c = vec3(0.0);
    for (int k = 0; k < 4; ++k) {
        ivec2 o = ivec2(k & 1, k >> 1), c = clamp(base + o, ivec2(0), size - 1);
        vec3 cp, cn;
        float cd, cr;
        if (!surface_at(c * uStep, cp, cn, cd, cr) || (uRough > 0.0 && cr > uRough)) continue;
        vec3 v = texelFetch(uPicture, c, 0).rgb;
        any_c = v;
        float plane = abs(dot(cp - p, n)) / (0.02 + 0.003 * dist);
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) + 1e-3;
        w *= exp(-2.0 * plane) * pow(max(dot(cn, n), 0.0), 16.0);
        sum += v * w;
        wsum += w;
    }
    FragColor = vec4(wsum > 1e-5 ? sum / wsum : any_c, dist);
}
)";
    return source;
}

const std::string& heights_vs() {
    static const std::string source = R"(#version 330 core
layout(location=0) in vec3 aPos;
uniform vec4 uSquare;   // its corner (x, z), metres a cell, points a side
uniform vec2 uDepth;    // the lowest and highest the ground may be
out float vHeight;
void main() {
    // A point of the square at the middle of its pixel: the picture's
    // pixels are the square's points.
    vec2 at = (aPos.xz - uSquare.xy) / uSquare.z;
    vec2 ndc = (at + 0.5) / uSquare.w * 2.0 - 1.0;
    float z = 1.0 - 2.0 * clamp((aPos.y - uDepth.x) / (uDepth.y - uDepth.x), 0.0, 1.0);
    vHeight = aPos.y;
    gl_Position = vec4(ndc, z, 1.0);
}
)";
    return source;
}

const std::string& heights_fs() {
    static const std::string source = R"(#version 330 core
in float vHeight;
out vec4 FragColor;
void main() { FragColor = vec4(vHeight, 0.0, 0.0, 1.0); }
)";
    return source;
}

const std::string& highest_fs() {
    static const std::string source = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
// The highest of each cell of the level below: of its two by two (and at
// the first level, from the points, each cell's four corners).
uniform sampler2D uBelow;
uniform int uFromPoints;
void main() {
    ivec2 size = textureSize(uBelow, 0);
    ivec2 c = ivec2(gl_FragCoord.xy) * (uFromPoints == 1 ? 1 : 2);
    float h = -1e30;
    for (int k = 0; k < 4; ++k) {
        ivec2 at = min(c + ivec2(k & 1, k >> 1), size - 1);
        h = max(h, texelFetch(uBelow, at, 0).r);
    }
    FragColor = vec4(h, 0.0, 0.0, 1.0);
}
)";
    return source;
}

}  // namespace sg::render::trace
