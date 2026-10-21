// The eye's lens, as programs: a camera's depth of field (a thin lens) and
// its shutter's motion blur (GLWorldView::run_lens, WorldLens.cpp).
#include <string>

#include "sg/gl/Shaders.hpp"

namespace sg::gl {

const char* lens_glsl() {
    return R"(
uniform sampler2D uDepth;
uniform vec2  uNearFar;    // the view's near and far, metres
uniform float uReversedZ;  // 1: the depth is reversed (near 1, far 0)
// The thin lens: how wide its aperture is across the picture (A over the
// sensor's height, times the picture's height in pixels), its focal length
// and where it is focused (metres; 0: on what is in the middle of the
// picture), and the widest circle of confusion drawn (pixels).
uniform vec4  uLens;
// How the view moved since the last frame: this frame's clip space to the
// last's; the part of that way the shutter saw (its time open over the
// frame's); the whole picture's pixels; the longest half-streak (pixels).
uniform mat4  uReproject;
uniform float uShutter;
uniform vec2  uFull;
uniform float uMostMove;

// How far along the view's axis what a pixel shows is; as far as the view
// goes where nothing was drawn.
float lens_far(vec2 uv) {
    float d = texture(uDepth, uv).r;
    float n = uNearFar.x, f = uNearFar.y;
    return uReversedZ > 0.5 ? (d <= 0.0 ? f : n * f / (n + d * (f - n)))
                            : (d >= 1.0 ? f : 2.0 * n * f / (f + n - (d * 2.0 - 1.0) * (f - n)));
}
// Where the lens is focused: where it is told, or on the nearest of what is
// in the middle of the picture.
float lens_focus() {
    if (uLens.z > 0.0) return uLens.z;
    float s = lens_far(vec2(0.5));
    s = min(s, lens_far(vec2(0.51, 0.5)));
    s = min(s, lens_far(vec2(0.49, 0.5)));
    s = min(s, lens_far(vec2(0.5, 0.51)));
    s = min(s, lens_far(vec2(0.5, 0.49)));
    return s;
}
// A point `z` metres off, seen by the thin lens focused at `s`: the circle it
// makes on the sensor, c = A |z - s| / z * f / (s - f), in pixels - signed,
// less than 0 in front of the focus.
float lens_coc(float z, float s) {
    float f = uLens.y;
    s = max(s, f * 1.01);
    return clamp(uLens.x * f / (s - f) * (1.0 - s / z), -uLens.w, uLens.w);
}
// How far, and which way, the picture under a pixel went while the shutter
// was open, either side of it (pixels): where it is now less where it was
// last frame, as much of that as the shutter saw.
vec2 lens_moved(vec2 uv) {
    if (uShutter <= 0.0) return vec2(0.0);
    float d = texture(uDepth, uv).r;
    float z = uReversedZ > 0.5 ? 1.0 - d : d;
    vec4 was = uReproject * vec4(uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
    if (was.w <= 0.0) return vec2(0.0);
    vec2 v = (uv - (was.xy / was.w * 0.5 + 0.5)) * uFull * (0.5 * uShutter);
    float l = length(v);
    return l > uMostMove ? v * (uMostMove / l) : v;
}
)";
}

const char* lens_setup_fs() {
    static const std::string source = std::string(R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform vec2 uSrcTexel;  // a pixel of the whole picture
)") + lens_glsl() + R"(
void main() {
    // Four pixels of the whole picture, their light kept as it is (a lamp's
    // spreads into a disc as bright as it was). The nearest of them says how
    // far, and only what is about as near is taken: an edge in front goes
    // with what is in front, and nothing of what is behind it with it.
    float s = lens_focus();
    vec2 uv[4];
    float z[4];
    float nearest = 1e30;
    for (int i = 0; i < 4; ++i) {
        uv[i] = vUV + (vec2(i & 1, i >> 1) - 0.5) * uSrcTexel;
        z[i] = lens_far(uv[i]);
        nearest = min(nearest, z[i]);
    }
    vec3 sum = vec3(0.0);
    float n = 0.0;
    for (int i = 0; i < 4; ++i)
        if (z[i] <= nearest * 1.05 + 0.02) sum += texture(uScene, uv[i]).rgb, n += 1.0;
    // (Its circle as a radius, in pixels of the half picture.)
    FragColor = vec4(sum / n, 0.25 * lens_coc(nearest, s));
})";
    return source.c_str();
}

const char* lens_tiles_fs() {
    static const std::string source = std::string(R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uHalf;
uniform float uDof;  // 1: a depth of field
)") + lens_glsl() + R"(
void main() {
    ivec2 t = ivec2(gl_FragCoord.xy);
    float most = 0.0;
    if (uDof > 0.5) {
        ivec2 size = textureSize(uHalf, 0) - 1;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) most = max(most, abs(texelFetch(uHalf, min(t * 8 + ivec2(x, y), size), 0).a));
    }
    // (Every other pixel each way: the view's motion changes only with how
    // far things are.)
    vec2 moved = vec2(0.0);
    if (uShutter > 0.0)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                vec2 v = lens_moved(min(vec2(t * 16) + vec2(x, y) * 2.0 + 1.0, uFull - 0.5) / uFull);
                if (dot(v, v) > dot(moved, moved)) moved = v;
            }
    FragColor = vec4(most, moved, 0.0);
})";
    return source.c_str();
}

const char* lens_spread_fs() {
    return R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uTiles;
void main() {
    ivec2 t = ivec2(gl_FragCoord.xy), size = textureSize(uTiles, 0) - 1;
    float most = 0.0;
    vec2 moved = vec2(0.0);
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec4 n = texelFetch(uTiles, clamp(t + ivec2(x, y), ivec2(0), size), 0);
            most = max(most, n.r);
            if (dot(n.gb, n.gb) > dot(moved, moved)) moved = n.gb;
        }
    FragColor = vec4(most, moved, 0.0);
})";
}

const char* lens_fill_fs() {
    return R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uHalf;
void main() {
    // The light of what is out of focus, with how much of it is (alpha): a
    // sharp pixel's none - so the picture made coarser for a wide disc
    // spreads only what is blurred, never the sharp edge beside it.
    vec4 p = texelFetch(uHalf, ivec2(gl_FragCoord.xy), 0);
    float k = smoothstep(0.75, 1.5, abs(p.a));
    FragColor = vec4(p.rgb * k, k);
})";
}

const char* lens_bokeh_fs() {
    return R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uHalf;    // the picture at half its pixels, its circles of confusion
uniform sampler2D uFill;    // what of it is out of focus, mipmapped (lens_fill_fs)
uniform sampler2D uSpread;  // how far each square must look
uniform vec2 uTexel;        // a pixel of it
// How much a point `r` from a disc's middle is in it, the disc `size` in
// radius: its edge a pixel soft, as a real one's is.
float cover(float size, float r) { return clamp(size - r + 0.5, 0.0, 1.0); }
void main() {
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec4 c0 = texelFetch(uHalf, at, 0);
    float reach = max(texelFetch(uSpread, at / 8, 0).r, abs(c0.a));
    // Nothing round it out of focus: the pixel as it is.
    if (reach < 0.5) {
        FragColor = vec4(c0.rgb, 0.0);
        return;
    }
    // Points spread evenly over the disc it looks across (a sunflower's
    // spiral: no rings), each read from the picture as coarse as the points
    // are apart - so a lamp smaller than they are is still met, its disc
    // filled, not dotted.
    int n = int(clamp(ceil(reach * reach * 1.5), 16.0, 256.0));
    float each = reach * reach / float(n);  // the part of the disc a point stands for, over pi
    float level = max(0.5 * log2(3.14159 * each) - 0.5, 0.0);
    vec2 uv = (vec2(at) + 0.5) * uTexel;
    // Gathering what would be scattered: each point spreads its light over
    // its own disc (1 / its area), and lands here if its disc reaches here.
    // Behind: what is behind the pixel spreads no wider than the pixel's own
    // circle (what stands in front of it hides it), what is not behind as
    // wide as its own - so a sharp pixel is all its own. In front: what is in
    // front of the focus and out of focus spreads over everything, said
    // apart, with how much of here it covers (its alpha).
    float c = c0.a, s = abs(c);
    ivec2 last = textureSize(uHalf, 0) - 1;
    vec3 back = vec3(0.0), front = vec3(0.0);
    float back_w = 0.0, front_w = 0.0, covered = 0.0;
    for (int i = -1; i < 256; ++i) {
        if (i >= n) break;
        // (Its own pixel only while near sharp: out of focus, it is one of
        // the disc's points like any other, not one more over its middle.)
        if (i < 0 && s >= 1.5) continue;
        float r = i < 0 ? 0.0 : reach * sqrt((float(i) + 0.5) / float(n));
        float a = float(i) * 2.3999632;
        // Its circle, there; and its light - that point's own if it is near
        // sharp, else what is out of focus round it (uFill, as coarse as the
        // points are apart: nothing sharp in it to spread).
        vec2 q = uv + vec2(cos(a), sin(a)) * r * uTexel;
        vec4 p = i < 0 ? c0 : texelFetch(uHalf, clamp(ivec2(q / uTexel), ivec2(0), last), 0);
        float sp = abs(p.a);
        if (sp >= 1.5) {
            vec4 fill = textureLod(uFill, q, level);
            if (fill.a > 1e-3) p.rgb = fill.rgb / fill.a;
        }
        float m = smoothstep(0.5, 1.0, -p.a);  // how much it is of what is in front
        if (m > 0.0) {
            float wf = m * cover(sp, r) / (sp * sp);
            front += p.rgb * wf;
            front_w += wf;
            covered += each * wf;
        }
        float sb = p.a > c ? min(sp, s) : sp;
        float wb = (p.a >= c ? 1.0 : 1.0 - m) * cover(sb, r) / max(sb * sb, 1.0);
        back += p.rgb * wb;
        back_w += wb;
    }
    float alpha = clamp(covered, 0.0, 1.0);
    vec3 behind = back_w > 0.0 ? back / back_w : c0.rgb;
    FragColor = vec4(front_w > 0.0 ? mix(behind, front / front_w, alpha) : behind, alpha);
})";
}

const char* lens_merge_fs() {
    static const std::string source = std::string(R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uHalf;   // its circles (alpha)
uniform sampler2D uBokeh;
)") + lens_glsl() + R"(
void main() {
    // Sharp where the pixel's own circle is under a pixel and nothing in
    // front of the focus is over it; the out-of-focus picture as the circle
    // grows, and wherever what is in front covers it.
    vec3 sharp = texelFetch(uScene, ivec2(gl_FragCoord.xy), 0).rgb;
    float c = lens_coc(lens_far(vUV), lens_focus());
    // The out-of-focus picture's four pixels round this one, each as much as
    // it is near and its circle is this pixel's own: a soft wall beside a
    // sharp post takes nothing of the post's half pixels.
    ivec2 size = textureSize(uBokeh, 0) - 1;
    vec2 p = vUV * vec2(size + 1) - 0.5, f = fract(p);
    ivec2 i0 = ivec2(floor(p));
    vec4 soft = vec4(0.0);
    float ws = 0.0;
    for (int i = 0; i < 4; ++i) {
        ivec2 o = ivec2(i & 1, i >> 1);
        ivec2 q = clamp(i0 + o, ivec2(0), size);
        float d = texelFetch(uHalf, q, 0).a - 0.25 * c;
        float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) / (0.01 + d * d);
        soft += texelFetch(uBokeh, q, 0) * w;
        ws += w;
    }
    soft /= max(ws, 1e-8);
    float own = max(smoothstep(1.0, 3.0, abs(c)), soft.a);
    FragColor = vec4(own > 0.0 ? mix(sharp, soft.rgb, own) : sharp, 1.0);
})";
    return source.c_str();
}

const char* lens_motion_fs() {
    static const std::string source = std::string(R"(#version 330 core
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uSpread;
)") + lens_glsl() + R"(
// Whether what is `a` metres off is in front of what is `b` off: 1 in
// front, 0 behind, soft over a few centimetres a metre.
float in_front(float a, float b) { return clamp(1.0 - (a - b) / (0.02 * b + 0.01), 0.0, 1.0); }
float cylinder(float d, float len) { return 1.0 - smoothstep(0.95 * len, 1.05 * len, d); }
void main() {
    ivec2 at = ivec2(gl_FragCoord.xy);
    vec3 c0 = texelFetch(uScene, at, 0).rgb;
    // The longest motion round it: none, and the pixel is as it is.
    vec2 most = texelFetch(uSpread, at / 16, 0).gb;
    float reach = length(most);
    if (reach < 0.5) {
        FragColor = vec4(c0, 1.0);
        return;
    }
    vec2 uv = (vec2(at) + 0.5) / uFull;
    float zc = lens_far(uv), lc = max(length(lens_moved(uv)), 0.5);
    // Taps along that motion, either side, moved a little each pixel so
    // their steps make no bands. What is in front of this pixel is seen here
    // for the part of the shutter's time its own streak lies over it (each
    // tap of it spread over its streak: as much as the taps are apart, over
    // its length); the rest of the time, what this pixel's own streak
    // passes over that is not in front of it - so what is in front smears
    // over what is behind and is seen through at its edges, and what is
    // behind never smears over what is in front.
    int taps = int(clamp(reach * 1.5, 6.0, 48.0));
    float apart = 2.0 * reach / float(taps + 1);
    float jit = fract(52.9829189 * fract(dot(vec2(at), vec2(0.06711056, 0.00583715)))) - 0.5;
    vec3 back = c0, front = vec3(0.0);
    float back_w = 1.0, covered = 0.0;
    for (int i = 0; i < 48; ++i) {
        if (i >= taps) break;
        float t = mix(-1.0, 1.0, (float(i) + jit + 1.0) / float(taps + 1));
        vec2 uy = uv + most * t / uFull;
        if (any(lessThan(uy, vec2(0.0))) || any(greaterThan(uy, vec2(1.0)))) continue;
        float zy = lens_far(uy), ly = max(length(lens_moved(uy)), 0.5), d = reach * abs(t);
        vec3 cy = textureLod(uScene, uy, 0.0).rgb;
        float f = 1.0 - in_front(zc, zy);  // it, in front of this pixel
        float wf = f * cylinder(d, ly) * apart / (2.0 * ly);
        front += cy * wf;
        covered += wf;
        float wb = (1.0 - f) * cylinder(d, lc);
        back += cy * wb;
        back_w += wb;
    }
    if (covered > 1.0) front /= covered, covered = 1.0;
    FragColor = vec4(front + (1.0 - covered) * back / back_w, 1.0);
})";
    return source.c_str();
}

}  // namespace sg::gl
