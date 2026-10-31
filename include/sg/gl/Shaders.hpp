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
// Its depth layer (x): steps nearer the eye it is drawn, past its size's
// (`depth_layer`); how metal it is (y); how lived-in (z, `grime`); how far
// it bends (w, `bend`).
layout(location=9) in vec4 iMat2;
// Which way the corner's u runs on its surface, and which side its v (w):
// a model's own normal map is laid along them (gl::Mesh::set_tangents);
// (0, 0, 0, 1) where a mesh has none, and the scene finds them on the screen.
layout(location=10) in vec4 aTangent;
out vec4 vTangent;
// A skin's corner (gl::Mesh::create_bound): the four joints that move it and
// how much each does - none, not a skin's - each joint's move three rows of
// a 3 x 4 matrix in uJoints (Mesh::pose), the last texel how its normal is
// sized back (Spatial3D::skin_model).
// What its own shape asks of its wear (gl::Mesh::set_wear, render::wear):
// convex, concave, shut from the sky by itself, and 1 where none was made.
layout(location=13) in vec4 aWear;
out vec4 vMeshWear;
layout(location=11) in vec4 aJoints;
layout(location=12) in vec4 aWeights;
uniform samplerBuffer uJoints;
void posed_by_joints(inout vec3 pos, inout vec3 nrm) {
    if (aWeights == vec4(0.0)) return;
    vec3 p = vec3(0.0), n = vec3(0.0);
    vec4 at = vec4(pos, 1.0);
    for (int k = 0; k < 4; ++k) {
        float w = aWeights[k];
        if (w <= 0.0) continue;
        int j = int(aJoints[k] + 0.5) * 3;
        vec4 r0 = texelFetch(uJoints, j), r1 = texelFetch(uJoints, j + 1), r2 = texelFetch(uJoints, j + 2);
        p += w * vec3(dot(r0, at), dot(r1, at), dot(r2, at));
        n += w * vec3(dot(r0.xyz, nrm), dot(r1.xyz, nrm), dot(r2.xyz, nrm));
    }
    n *= texelFetch(uJoints, textureSize(uJoints) - 1).xyz;
    pos = p, nrm = n / (length(n) + 1e-12);
}
uniform float uDepthLayer;
uniform float uBend;  // metres a box's corners stand off its middle, along its thinnest way (`bend`)
uniform vec3 uCurl;   // a panel's curl: metres its edge goes back, which edge (1 its picture's right), over how much of it (`curl`)
uniform int uInstanced;
uniform mat4 uFrame;
flat out vec4 vMat0;
flat out vec4 vMat1;
flat out float vMetal;
flat out float vGrime;
flat out float vInstanced;

uniform mat4 uModel;
// The same placement *without* the room's own, so surface detail is a property
// of the surface rather than of where the room currently sits. Which room the
// viewer stands in decides the world frame; it must not decide where the floor
// tiles fall.
uniform mat4 uTexModel;
uniform mat4 uViewProj;
uniform float uSnap;        // (the lines of the picture corners are put on: 0, none)
uniform float uSnapAspect;  // (its width over its height)
uniform float uDepthNudge;  // 1: depth reversed (near 1, far 0, float), the nudge below a share of it; 0: as GL keeps depth

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
// Where this corner was last frame (the program for a view whose motion is
// asked for: SG_VELOCITY): this frame's view unmoved by the jitter, the last
// frame's with its room where it stood then, and where the thing stood in
// its room then (uWasModel; the scene's fragment stage reads it only where
// the thing says it, uMoving).
#ifdef SG_VELOCITY
uniform mat4 uMotionNow;
uniform mat4 uMotionWas;
uniform mat4 uWasModel;
out vec4 vNowClip;
out vec4 vWasClip;
#endif

void main() {
    mat4 model = uInstanced == 1 ? uFrame * iLocal : uModel;
    mat4 texModel = uInstanced == 1 ? iLocal : uTexModel;
    vMat0 = iMat0;
    vMat1 = iMat1;
    vMetal = iMat2.y;
    vGrime = iMat2.z;
    vInstanced = float(uInstanced);
    // A box that bends: its thinnest way pushed out, the more the further
    // from its middle across its face (a bowl, `bend` metres deep at its
    // corners), and its faces' normals turned as the bowl turns them.
    vec3 pos = aPos, nrm = aNormal;
    posed_by_joints(pos, nrm);
    float bend = uInstanced == 1 ? iMat2.w : uBend;
    if (bend != 0.0) {
        vec3 sc = vec3(length(model[0].xyz), length(model[1].xyz), length(model[2].xyz));
        int k = sc.x <= sc.y && sc.x <= sc.z ? 0 : (sc.y <= sc.z ? 1 : 2);
        int i = (k + 1) % 3, j = (k + 2) % 3;
        float u = pos[i] * 2.0, v = pos[j] * 2.0;
        pos[k] += bend * 0.5 * (u * u + v * v) / max(sc[k], 1e-5);
        if (abs(nrm[k]) > 0.5) {
            vec3 w = vec3(0.0);
            float s = sign(nrm[k]);
            w[i] = -s * 2.0 * bend * u / max(sc[i], 1e-5);
            w[j] = -s * 2.0 * bend * v / max(sc[j], 1e-5);
            w[k] = s;
            nrm = w;
        }
    }
    // A panel that curls: one edge of it gone back behind its face, the
    // curve over `span` of its width (its x its face, its z across it).
    bool curled = uInstanced == 0 && uCurl.x != 0.0;
    if (curled) {
        float w = max(length(model[2].xyz), 1e-5);
        float u = 0.5 - pos.z;                      // 0 at its picture's left .. 1 its right
        float t = uCurl.y > 0.5 ? u : 1.0 - u;      // 0 at the far edge .. 1 at the edge that curls
        float span = uCurl.z;
        float s = clamp((t - (1.0 - span)) / span, 0.0, 1.0);
        pos.x -= uCurl.x * s * s;
        // (Its slope across it, in metres: its face turned by as much.)
        float dt = (uCurl.y > 0.5 ? -1.0 : 1.0) / w;
        float slope = -2.0 * uCurl.x * s / span * dt;
        nrm = vec3(1.0, 0.0, -slope);
    }
    vec4 world = model * vec4(pos, 1.0);
    vWorld = world.xyz;
    vRoom = (texModel * vec4(pos, 1.0)).xyz;
    vLocal = pos;
    // The mesh's own frame at its own size: materials that belong to a thing
    // (grain, brushing, weave) ride along with it however it is moved.
    vec3 scale = vec3(length(model[0].xyz), length(model[1].xyz), length(model[2].xyz));
    vObject = pos * scale;
    vObjNormal = nrm;
    // In the room's frame, as a surface's normal goes when it is scaled.
    vec3 texScale = vec3(length(texModel[0].xyz), length(texModel[1].xyz), length(texModel[2].xyz));
    vTexScale = texScale;
    // (A bent face's normal is already the way it faces in the box's own
    // metres: carried by its turn alone, its size taken back out.)
    bool metric = bend != 0.0 || curled;
    vRoomNormal = mat3(texModel) * (metric ? nrm / max(texScale, vec3(1e-8)) : nrm / max(texScale * texScale, vec3(1e-8)));
    vNormal = normalize(mat3(model) * (metric ? nrm / max(scale, vec3(1e-8)) : nrm));
    vUV = aUV;
    vMeshWear = aWear;
    // (Turned over - a model mirrored - its v runs the other side of its u.)
    vTangent = vec4(mat3(model) * aTangent.xyz, aTangent.w * (determinant(mat3(model)) < 0.0 ? -1.0 : 1.0));
    for (int i = 0; i < MAX_BOUNDS; ++i)
        gl_ClipDistance[i] = i < uClipCount ? dot(uClip[i], vec4(world.xyz, 1.0)) : 1.0;
    gl_Position = uViewProj * world;
#ifdef SG_VELOCITY
    vNowClip = uMotionNow * world;
    vWasClip = uMotionWas * (uWasModel * vec4(pos, 1.0));
#endif
    // An older machine's: each corner put on a pixel of a picture uSnap lines
    // high (0: where it is) - things wobble a little as the eye moves.
    if (uSnap > 0.0 && gl_Position.w > 1e-4) {
        vec2 grid = vec2(uSnap * uSnapAspect, uSnap) * 0.5;
        gl_Position.xy = floor(gl_Position.xy / gl_Position.w * grid + 0.5) / grid * gl_Position.w;
    }
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
    // (Depth reversed, in float: the nudge is a share of the distance - five
    // parts in a million a step of rank, more for a thing far from the
    // world's middle, whose corners round coarser - the same share at every
    // corner of a thing, so its faces stay flat: a twentieth of a millimetre
    // a step at a metre, a few millimetres at forty for the smallest things.)
    if (uDepthNudge > 0.0) gl_Position.z *= 1.0 + rank * max(5e-6, length(model[3].xyz) * 1e-7);
    else gl_Position.z -= rank * (2.0 / 16777216.0) * gl_Position.w;
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

// The air of a world as a planet's atmosphere (a look's `atmosphere`): air
// whose molecules scatter blue more than red (Rayleigh), whose haze scatters
// all alike and mostly on ahead (Mie), and whose ozone takes some orange out,
// each thinning with height. Worked out into three small pictures - how much
// of the light gets through from any height any way (transmittance), the
// light scattered more than once at each height for each height of the sun
// (multiple scattering), and the sky seen from the eye every way (sky view) -
// made in that order (after Hillaire's method, 2020), then read by the scene:
// its sky, and the air between the eye and what it sees (aerial perspective).
const std::string& atmosphere_glsl();
const char* atmosphere_transmittance_fs();
const char* atmosphere_multiple_fs();
const char* atmosphere_sky_fs();

// A world's clouds as a volume (a look's `clouds.cover`): a layer of cloud
// between two heights over the planet, where cloud is said by a coverage
// that drifts with the wind, a height profile that rounds each cloud's base
// and lifts its top as the cover thickens, and a noise that billows it and
// frays its edges - lit by the sun through the cloud between (Beer), in two
// lobes (on ahead and back), with what is scattered more than once (in
// octaves), and by the sky above and the ground below. Marched into a
// picture of the whole sky above (the dome: straight up its middle, the
// horizon its edge), a sixteenth of it each frame, so a turning eye costs
// nothing and every view of the world, and its mirrors, see the same clouds.
// `cloud_dome_glsl`: the dome's way from a point of it, and back.
// `cloud_noise_fs`: the noise, made once (eight slices of a volume a pass).
const std::string& cloud_dome_glsl();
// `clouds_glsl`: how dense they are anywhere (`cloud_at`), and how much of
// the sun gets through them to a point under them (`through_clouds`).
// `cloud_shadow_fs`: that, on the ground round the eye - the map
// `lights_glsl`'s suns read (`cloud_shadow`) where `uCloudShadowAt` says.
const std::string& clouds_glsl();

// Fog where a world says it pools (sg/domains/Fog.hpp, four at most): each
// a box in the world, soft at its sides and top, thinning as it rises, its
// wisps moved by the time of the world it is in - how thick at a point
// (`fog_volume_at`) and how deep along a way (`fog_volume_depth`), read by
// the scene (its fog) and the lit air (how much more light it scatters).
const std::string& fog_volumes_glsl();

// A terrain's horizon toward the sun (GLWorldView::horizon_for): from its
// heights on an even grid, for each point how steeply the land toward the
// sun rises over it, and the ground's height there - read by every sun's
// light (lights_glsl's `horizon_shadow`).
const char* horizon_fs();
const char* cloud_shadow_fs();
const char* cloud_noise_fs();
const char* cloud_dome_fs();

// Temporal antialiasing: this frame's picture (uCurrent, its view moved by
// uJitter, in clip units) and what was made of the frames before
// (uHistory), found where each pixel was last frame by its depth (uDepth)
// and how the view moved (uReproject: this frame's clip space, unmoved, to
// the last frame's), taken only within what this frame's neighbourhood of
// the pixel holds - so what moved or came into sight does not trail - and
// blended, a tenth of this frame to nine of what was. uFresh 1: no history.
const char* taa_fs();
// How each pixel of the eye's view moved since the last frame, in its
// pixels, the jitter left out (GLWorldView::run_motion): what moved itself
// as the scene said (uMoved), the rest as the view moved past it - found from
// its depth (uDepth) and how the view moved (uReproject), as taa_fs finds it.
// For an upscaler of the card's (DLSS), whose motion vectors these are.
const char* motion_fs();
// Data moshing (a look's `mosh`, 0..1): a picture whose key frame was lost.
// What it started as (the frame before the look asked for it: whatever was
// seen before the cut) is carried on by the motion of what is seen now -
// block by block (uBlock pixels), the view's motion found from depth as taa_fs
// finds it (uReproject) - with only the small changes of this frame laid
// over it (uCurrent less uLast where it came from), a big one (the cut) lost:
// the old picture dragged and smeared by the new one's motion. What comes in
// from beyond the edge is as it is now; the less uAmount, the bigger the
// changes kept, until the new picture is all there is. uFresh 1: it starts.
const char* mosh_fs();
// The eye's lens (GLWorldView::run_lens; README *The lens*): a camera's
// depth of field, as a thin lens of its focal length and aperture sees it,
// and its shutter's motion blur. `lens_glsl` is what they share: how far a
// pixel is (uDepth), how wide its circle of confusion (uLens), and how it
// moved since the last frame (uReproject, uShutter).
const char* lens_glsl();
// The picture at half its pixels each way, and each one's circle of
// confusion (alpha: its radius in half pixels, less than 0 in front of the focus).
const char* lens_setup_fs();
// Squares of the picture (kLensTile pixels): the widest circle of confusion
// in each (r) and the longest motion (gb); and then the most of each square
// and its eight neighbours (lens_spread_fs) - how far a pixel must look.
const char* lens_tiles_fs();
const char* lens_spread_fs();
// What of the half picture is out of focus (its light times how much, and
// how much): mipmapped, what a wide disc reads its far points from.
const char* lens_fill_fs();
// The out-of-focus picture, at half its pixels: each point a disc as wide as
// its circle of confusion, gathered (what is behind spreading only as far as
// the pixel's own, what is in front over everything), the part of it in
// front of the focus said apart (alpha).
const char* lens_bokeh_fs();
// The sharp picture and the out-of-focus one, laid together pixel by pixel.
const char* lens_merge_fs();
// Motion blur: each pixel gathered along the longest motion round it (the
// squares' most, as McGuire et al. 2012 find it) - what is in front of it
// for the part of the shutter's time its own streak lies over it, the rest
// what its own streak passes over.
const char* lens_motion_fs();
// Relighting a room's light from all round (GLWorldView::relight_room): what
// each box sees lit by one light, and that taken to nine harmonics a box.
// How many light-from-all-round boxes a room hands its shaders (the scene's
// MAX_PROBES, the relight's uBox arrays, the tracer's): one number, every
// shader's arrays held to it by sg_shader_counts.
inline constexpr int kShaderProbes = 24;
const char* relight_fs();
const char* relight_sh_fs();
// Reflection captures (GLWorldView::capture_rooms): GLSL for a way out as a
// texel of an octahedron's map unfolded, a texel's border round it (the
// border what lies past the fold) - `capture_way`, `capture_uv`; the six
// faces of a capture (a camera's view each, in a row) folded into that map;
// and a size of it blurred as a surface of a roughness blurs what it
// mirrors (GGX, importance-sampled from the map's own smaller sizes).
const char* capture_glsl();
// A picture's next smaller size from the one above it, each of its four
// texels weighed by 1 / (1 + its brightness) (Karis): a glint one texel
// wide - a bulb in polished silver - does not flood the size below with its
// light, as a plain average lets it; what is broad and even is as it was.
const char* glint_down_fs();
const char* capture_fold_fs();
const char* capture_blur_fs();

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
// A doorway's view laid again, depth only: each pixel as far off as what the
// view shows there - its depth read from the view (uViewDepth, drawn between
// uViewNF's near and far), put into the room's range (uDrawNF).
const char* deep_view_fs();

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
// A skin's corner posed by its joints, as the scene poses it (scene_vs).
layout(location=11) in vec4 aJoints;
layout(location=12) in vec4 aWeights;
uniform samplerBuffer uJoints;
vec3 posed_by_joints(vec3 pos) {
    if (aWeights == vec4(0.0)) return pos;
    vec3 p = vec3(0.0);
    vec4 at = vec4(pos, 1.0);
    for (int k = 0; k < 4; ++k) {
        float w = aWeights[k];
        if (w <= 0.0) continue;
        int j = int(aJoints[k] + 0.5) * 3;
        p += w * vec3(dot(texelFetch(uJoints, j), at), dot(texelFetch(uJoints, j + 1), at), dot(texelFetch(uJoints, j + 2), at));
    }
    return p;
}
void main() {
    vec4 world = (uInstanced == 1 ? uFrame * iLocal : uModel) * vec4(posed_by_joints(aPos), 1.0);
    gl_ClipDistance[0] = dot(uCasterSide.xyz, world.xyz) + uCasterSide.w;
    gl_Position = uLightViewProj * world;
})";
}

inline const char* depth_fs() {
    return R"(#version 330 core
void main() {})";
}

// The same, for a thing cut out of its picture (`cutout`: a leaf on a card):
// what is cut away casts nothing, so its shadow is the leaf's. Its own program,
// so every other caster keeps the depth test before its (empty) shading.
inline const char* depth_cut_vs() {
    return R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uLightViewProj;
uniform vec4 uCasterSide;
out float gl_ClipDistance[1];
out vec2 vUV;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    gl_ClipDistance[0] = dot(uCasterSide.xyz, world.xyz) + uCasterSide.w;
    vUV = aUV;
    gl_Position = uLightViewProj * world;
})";
}

inline const char* depth_cut_fs() {
    return R"(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform float uCutout;  // what alpha is kept: at least this
void main() {
    // As the scene cuts it (scene_fs: cut_alpha), so the shadow is of what is seen.
    vec2 px = vUV * vec2(textureSize(uTex, 0));
    float lod = max(0.0, 0.5 * log2(max(dot(dFdx(px), dFdx(px)), dot(dFdy(px), dFdy(px)))));
    if (texture(uTex, vUV).a * (1.0 + lod * 0.25) < uCutout) discard;
})";
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
float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
vec3 over(vec3 c) { float y = luma(c); return c * (max(y - uThreshold, 0.0) / max(y, 1e-4)); }
void main() {
    // (Four pixels round this one, each weighed by 1 / (1 + its light) - a
    // Karis average: one pixel far brighter than those round it - a glossy
    // highlight caught by a lone texel - would otherwise glow as a star of
    // its own and flash as it comes and goes. A lamp, many pixels across,
    // glows as bright as before.)
    vec2 t = 0.5 / vec2(textureSize(uScene, 0));
    vec3 s0 = over(texture(uScene, vUV + vec2(-t.x, -t.y)).rgb), s1 = over(texture(uScene, vUV + vec2(t.x, -t.y)).rgb);
    vec3 s2 = over(texture(uScene, vUV + vec2(-t.x, t.y)).rgb), s3 = over(texture(uScene, vUV + vec2(t.x, t.y)).rgb);
    float w0 = 1.0 / (1.0 + luma(s0)), w1 = 1.0 / (1.0 + luma(s1)), w2 = 1.0 / (1.0 + luma(s2)), w3 = 1.0 / (1.0 + luma(s3));
    FragColor = vec4((s0 * w0 + s1 * w1 + s2 * w2 + s3 * w3) / (w0 + w1 + w2 + w3), 1.0);
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
// How much of the light from all round reaches each pixel, from the depth
// buffer: GTAO (Jimenez et al. 2016, "Practical Real-Time Strategies for
// Accurate Indirect Occlusion"; as Intel's XeGTAO does it). In a few
// directions across the screen it walks out each way to the highest thing
// standing over the surface within `uRadius` metres (the horizon, fading out
// past 0.6 of it), then integrates what is open between the two horizons,
// weighted by the cosine to the surface's normal - the visibility a ray
// tracer finds, not a count of taps. The directions and steps turn with each
// pixel (interleaved gradient noise) and, where frames are averaged (TAA,
// DLSS: `uFrame`), with each frame: the average is the integral.
inline const char* ao_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uDepth;
uniform vec2  uTexel;     // one pixel of the depth buffer
uniform float uNear, uFar, uTanHalf, uAspect, uRadius;
uniform float uReversedZ;  // 1: the depth is reversed (near 1, far 0)
uniform float uFrame;      // which frame, where frames are averaged (else 0)
uniform int   uSlices, uSteps;

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
float ign(vec2 px) { return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715)))); }

void main() {
    float d = texture(uDepth, vUV).r;
    if (uReversedZ > 0.5 ? d <= 0.0 : d >= 1.0) { FragColor = vec4(1.0); return; }
    vec3 p = view_at(vUV);
    // The surface's normal from its neighbours, each side's nearer one, so an edge does not bend it.
    vec3 px1 = view_at(vUV + vec2(uTexel.x, 0.0)) - p, px0 = p - view_at(vUV - vec2(uTexel.x, 0.0));
    vec3 py1 = view_at(vUV + vec2(0.0, uTexel.y)) - p, py0 = p - view_at(vUV - vec2(0.0, uTexel.y));
    vec3 n = normalize(cross(abs(px1.z) < abs(px0.z) ? px1 : px0, abs(py1.z) < abs(py0.z) ? py1 : py0));
    vec3 v = normalize(-p);
    if (dot(n, v) < 0.0) n = -n;
    // The radius in pixels where the surface is; too small to see, nothing is hidden.
    float pixel = -p.z * 2.0 * uTanHalf * uTexel.y;
    float reach = min(uRadius / pixel, 0.4 / uTexel.y);
    if (reach < 1.5) { FragColor = vec4(1.0); return; }
    float falloff = 0.615 * uRadius, from = uRadius - falloff;
    float fmul = -1.0 / falloff, fadd = from / falloff + 1.0;
    // The noise: the slice's angle and the steps' offsets, per pixel, per frame.
    vec2 cell = gl_FragCoord.xy + uFrame * vec2(5.588238, 5.588238);
    float noise_slice = ign(cell), noise_step = ign(cell + vec2(17.0, 31.0));
    // The slices turn evenly round the eye's ray to this pixel, not round the
    // screen: off the middle of the view the ray leans, and slices even on
    // the screen are not even round it - an open floor came out greyer the
    // further from the middle it was seen (sg_ao_gl).
    vec3 t1 = normalize(abs(v.y) < 0.99 ? cross(v, vec3(0.0, 1.0, 0.0)) : cross(v, vec3(1.0, 0.0, 0.0)));
    vec3 t2 = cross(v, t1);
    vec2 here = vec2(p.x / (-p.z * uTanHalf * uAspect), p.y / (-p.z * uTanHalf));
    float visible = 0.0;
    for (int s = 0; s < uSlices; ++s) {
        float phi = (float(s) + noise_slice) / float(uSlices) * PI;
        vec3 dir = cos(phi) * t1 + sin(phi) * t2;
        // (Which way that is on the screen: where a step along it lands, in pixels.)
        vec3 q = p + dir * (0.01 * -p.z);
        vec2 there = vec2(q.x / (-q.z * uTanHalf * uAspect), q.y / (-q.z * uTanHalf));
        vec2 omega = (there - here) * 0.5 / uTexel;
        omega /= max(length(omega), 1e-6);
        vec3 ortho = dir;
        vec3 axis = normalize(cross(ortho, v));
        vec3 pn = n - axis * dot(n, axis);
        float pn_len = length(pn);
        float cos_n = clamp(dot(pn, v) / max(pn_len, 1e-4), 0.0, 1.0);
        float nn = sign(dot(ortho, pn)) * fast_acos(cos_n);
        float low0 = cos(nn + HALF_PI), low1 = cos(nn - HALF_PI);
        float h0c = low0, h1c = low1;
        for (int k = 0; k < uSteps; ++k) {
            float t = fract(noise_step + float(s + k * uSlices) * 0.618034);
            float st = (float(k) + t) / float(uSteps);
            st = st * st + 1.3 / reach;   // more steps near; never the pixel itself
            vec2 off = round(st * reach * omega) * uTexel;
            vec2 ua = vUV + off, ub = vUV - off;
            vec3 a = view_at(ua) - p, b = view_at(ub) - p;
            float la = length(a), lb = length(b);
            float ca = dot(a / max(la, 1e-5), v), cb = dot(b / max(lb, 1e-5), v);
            // (Past the screen's edge nothing is known: no horizon there - the
            // edge's own depth, read again, would stand up as one.)
            float wa = all(greaterThanEqual(ua, vec2(0.0))) && all(lessThanEqual(ua, vec2(1.0))) ? 1.0 : 0.0;
            float wb = all(greaterThanEqual(ub, vec2(0.0))) && all(lessThanEqual(ub, vec2(1.0))) ? 1.0 : 0.0;
            ca = mix(low0, ca, clamp(la * fmul + fadd, 0.0, 1.0) * wa);
            cb = mix(low1, cb, clamp(lb * fmul + fadd, 0.0, 1.0) * wb);
            h0c = max(h0c, ca), h1c = max(h1c, cb);
        }
        float h0 = -fast_acos(h1c), h1 = fast_acos(h0c);
        h0 = nn + clamp(h0 - nn, -HALF_PI, HALF_PI);
        h1 = nn + clamp(h1 - nn, -HALF_PI, HALF_PI);
        float sn = sin(nn);
        float arc0 = (cos_n + 2.0 * h0 * sn - cos(2.0 * h0 - nn)) * 0.25;
        float arc1 = (cos_n + 2.0 * h1 * sn - cos(2.0 * h1 - nn)) * 0.25;
        visible += pn_len * (arc0 + arc1);
    }
    visible = clamp(visible / float(uSlices), 0.0, 1.0);
    FragColor = vec4(vec3(visible), 1.0);
})";
}

// The noise smoothed, kept to one surface: a 5 x 5 gaussian of the pixels
// at the same depth (within 2% of it) - the frames' averaging does the rest.
inline const char* ao_blur_fs() {
    return R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uAO;
uniform sampler2D uDepth;
uniform vec2  uTexel;     // one pixel of the AO target
uniform float uNear, uFar;
uniform float uReversedZ;  // 1: the depth is reversed (near 1, far 0)
float linear(float d) {
    if (uReversedZ > 0.5) return uNear * uFar / (uNear + d * (uFar - uNear));
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}
void main() {
    float zc = linear(texture(uDepth, vUV).r);
    float sum = 0.0, weight = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            vec2 uv = vUV + vec2(x, y) * uTexel;
            float z = linear(texture(uDepth, uv).r);
            float w = exp(-0.35 * float(x * x + y * y)) * exp(-abs(z - zc) / (zc * 0.02));
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
uniform float uBounce;     // the albedo light bounces off inside a crease (0..1)
uniform float uShow;
uniform float uNear, uFar;
uniform vec2  uTexel;
uniform float uReversedZ;  // 1: the depth is reversed (near 1, far 0)
float linear(float d) {
    if (uReversedZ > 0.5) return uNear * uFar / (uNear + d * (uFar - uNear));
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
    // Light bounced again inside what hides it (Jimenez's fit, `uBounce` the
    // surfaces' albedo): a pale crease is not as dark as a black one.
    float A = uBounce, a = 2.0404 * A - 0.3324, b = -4.7951 * A + 0.6417, cc = 2.7552 * A + 0.6903;
    ao = max(ao, ((ao * a + b) * ao + cc) * ao);
    float k = uStrength * scene.a;
    FragColor = vec4(c * mix(1.0, ao, k), 1.0);
    // (`ao.show`: 1 the occlusion alone, 2 the share of each pixel it may darken - to look at.)
    if (uShow > 0.5) FragColor = vec4(vec3(uShow < 1.5 ? ao : scene.a), 1.0);
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
//                           and a video sensor's noise if `uSensor` says (x how
//                           much, y its colour's share, z its blotches' size) -
//                           soft, a pixel and a half across, strongest in the
//                           mid tones and least in the highlights, moving each
//                           frame - and a last dither, so nothing bands.
//   vec3 film_at(vec3 c, float grain, float time, vec2 px)
//                           the same, its grain laid by `px` (the pixel by
//                           default): a picture drawn at another size than
//                           the screen keeps its grain the size it was.
inline const char* film_glsl() {
    return R"(
uniform float uGradeExposure;
uniform float uGradeContrast;
uniform float uGradeSaturation;
uniform vec3  uGradeShadows;
uniform vec3  uGradeHighlights;
uniform float uTonemap;
uniform vec3  uSensor;  // a video sensor's noise: x how much, y how much of it is colour, z its blotches' size, pixels (0: none)
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
// Smooth value noise over the pixels (0..1): the sensor's blotches.
float film_soft(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(film_hash(i), film_hash(i + vec2(1.0, 0.0)), f.x), mix(film_hash(i + vec2(0.0, 1.0)), film_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}
// A video sensor's noise (`uSensor`: x how much, y its colour's share, z
// the colour's blotches, pixels), added to the picture's light before its
// curve, as a sensor adds it - not film's grain over the print. Its spread
// the sensor's own (Poisson-Gaussian: variance = a light + b - its shot
// noise, the light counted, rising with it; its read noise, there in the
// blacks too), each photosite's own, its colour a little coarser as the
// demosaic spreads it; a new frame of it thirty times a second.
// `light`: the scene's light as exposed (times the exposure).
vec3 sensor_noise(vec3 light, vec2 px, float time) {
    if (uSensor.x <= 0.0) return vec3(0.0);
    vec2 at = px + vec2(film_hash(vec2(floor(time * 30.0), 3.0)), film_hash(vec2(floor(time * 30.0), 29.0))) * 977.0;
    // (Two uniforms summed: near enough a gaussian for the eye.)
    float l = film_hash(floor(at)) + film_hash(floor(at) + 71.3) - 1.0;
    vec3 ch = vec3(film_soft(at / max(uSensor.z, 1.0) + 11.0), film_soft(at / max(uSensor.z, 1.0) + 23.0), film_soft(at / max(uSensor.z, 1.0) + 37.0)) - 0.5;
    ch -= dot(ch, vec3(0.2126, 0.7152, 0.0722));
    vec3 x = max(light, vec3(0.0));
    // (Its read noise kept low in the deepest black: noise cannot go under
    // black, so there it only greys it.)
    vec3 sigma = uSensor.x * sqrt(0.5 * x + 0.0012) * smoothstep(0.0, 0.004, x + 0.0008);
    return sigma * (vec3(l) * (1.0 - 0.5 * uSensor.y) + ch * 2.2 * uSensor.y);
}
vec3 film_at(vec3 c, float grain, float time, vec2 px) {
    vec3 e = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
    vec2 jump = vec2(film_hash(vec2(floor(time * 24.0), 7.0)), film_hash(vec2(floor(time * 24.0), 13.0))) * 911.0;
    float y = dot(e, vec3(0.2126, 0.7152, 0.0722));
    float g = (film_clump(px + jump) - 0.5) * 1.1;
    float amount = grain * 2.4 * mix(0.45, 1.0, smoothstep(0.0, 0.3, y)) * (1.0 - 0.7 * smoothstep(0.55, 1.0, y));
    vec3 chroma = vec3(film_hash(px + jump + 3.1), film_hash(px + jump + 5.7), film_hash(px + jump + 9.3)) - 0.5;
    e += (g + chroma * 0.15) * amount;
    e += (film_hash(px + jump * 1.37) - film_hash(px + 17.0 + jump) ) / 255.0;
    return e;
}
vec3 film(vec3 c, float grain, float time) { return film_at(c, grain, time, gl_FragCoord.xy); }
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
