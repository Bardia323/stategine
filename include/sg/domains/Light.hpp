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
//                 bright - eased, so a cut in the picture flickers the room
//                 only a little.
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

// A lamp standing in for a glowing screen: paler than the picture (a screen's
// light is mostly its glass glowing, whatever is on it), as strong as the
// picture is bright up to `most`, each step `ease` of the way there.
void spill(Element& lamp, const Surface2D& screen, double most, double ease = 0.25);

}  // namespace sg
