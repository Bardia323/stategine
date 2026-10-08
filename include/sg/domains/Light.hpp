// Stategine - Light: the parts of lighting that are the same in every world.
//
// Nothing here knows about GL. What it gives is numbers a look carries (see
// Look.hpp) and a lamp's parameters, for any renderer to read:
//
//   Daylight      the sky at an hour: where the sun (or the moon) is, its
//                 colour and strength, the sky's colours, the light from all
//                 round, the exposure and the stars. show_daylight() puts it
//                 into a look's scene and composite passes.
//   aim_rays      light shafts in a look's composite (gl::godrays_glsl): from
//                 a direction as the camera sees it, how strong, what colour.
//   spill         a lamp standing in for a glowing screen: it takes the
//                 colour of the screen's picture, and is as strong as it is
//                 bright. What the picture asks (spill_of) is carried to the
//                 lamp by a functor; the lamp is eased there by the arrow of
//                 the state it is in, on that state's own time (spill), so a
//                 cut in the picture flickers the room only a little.
//
//   sg::Daylight d = sg::daylight(17.5, {0.55, 0.40, 0.26});  // late, over sand
//   sg::show_daylight(look, d);
//   sg::aim_rays(look, room.camera(), d.sun, 0.7 * d.day, d.light);
#pragma once

#include <algorithm>
#include <cmath>

#include "sg/domains/Look.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/domains/Surface.hpp"

namespace sg {

struct Rgb {
    double r = 1, g = 1, b = 1;
};

Rgb mix(const Rgb& a, const Rgb& b, double t);

// The colour of a glowing body at `K` kelvin - a candle 1900, a tungsten
// bulb 2700, a halogen 3200, noon 5500, an overcast sky 6500 - in linear
// sRGB, as bright as white (luminance 1): only its colour, so a lamp says
// how strong it is apart. Read off the Planckian locus (Kim et al.'s cubic
// splines, 1667 to 25000 K; held there outside them) and taken from xy to
// XYZ to linear sRGB; what falls outside sRGB is brought to its edge.
Rgb kelvin(double K);

// --- daylight ---------------------------------------------------------------------
struct Daylight {
    Vec3d sun{0, -1, 0};  // towards the sun (or, at night, the moon)
    Rgb light;            // its colour
    double intensity = 0; // and strength, as a lamp's
    Rgb top, horizon;     // the sky overhead and at the horizon
    Rgb sky_ambient, ground_ambient;  // light from all round, from above and below
    double ambient = 0;   // how much of it
    double exposure = 1;  // what the eye opens to
    double stars = 0;     // how much of the night sky shows
    double day = 0;       // 0 night, 1 full day
    double elevation = 0; // the sun's height, radians
};

// The sky at `hour` (0 to 24): the sun climbs from the east (+x), is high at
// noon and sets in the west, its light going gold and then red on the way
// down; then twilight, and a night with a moon and stars. `ground` is the
// colour of the ground by day - what the light bounced up from below takes.
Daylight daylight(double hour, const Rgb& ground = {0.45, 0.40, 0.34});

// The sky into a look: its colours, the air's, the light from all round,
// the stars and the exposure.
void show_daylight(LookState& l, const Daylight& d);

// --- shafts -----------------------------------------------------------------------
// Aim a look's shafts: towards `dir` from the camera `cam`, at `strength`
// (0: none), in `colour`. The shader places the source on screen itself, so
// this is set once a frame with no knowledge of the screen's size.
void aim_rays(LookState& look, const Element& cam, const Vec3d& dir, double strength, const Rgb& colour);

// --- a screen's light ---------------------------------------------------------------
// The picture's average colour, in linear light, from a sparse grid of its
// pixels.
Rgb average_colour(const Surface2D& s);

// What a picture asks of a lamp standing in for its screen: paler than the
// picture (a screen's light is mostly its glass glowing, whatever is on it),
// and as strong as the picture is bright, up to `most`. Taken in steps of
// 1/512: a picture's colour moving by less asks nothing new. Pure in the
// picture - what a functor from the screen carries to its lamp - and never
// eased here.
struct Spill {
    double r = 1, g = 1, b = 1, level = 0;
};
Spill spill_of(const Surface2D& screen, double most);

// How long a lamp takes to follow its picture: a quarter of the way in a
// sixtieth of a second (tau = -1 / (60 ln 0.75)), as it went when it was eased
// a quarter of the way each frame - now a second of the lamp's own time is a
// second at any frame rate.
inline constexpr double kSpillTau = 0.05793432494637012;

// The lamp `dt` of its own time nearer what its picture asks (`to`): the part
// k = 1 - exp(-dt / tau) of the way, and there once within half a step of it -
// an easing that never arrives changes for ever, and lights its room again
// each time. Not `on`, it gives no light. No time, no change. For the driven
// arrow of the state the lamp is in: a function of the lamp and its `{dt}`.
void spill(Element& lamp, const Spill& to, double dt, bool on = true, double tau = kSpillTau);

}  // namespace sg
