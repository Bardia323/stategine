// Stategine - Distribution: how things are spread, as laws of chance kept as
// data.
//
// Where a world's things stand, how big each is, which way it turns, how much
// of a stain lies where - none of it is even, and none of it is the same
// noise everywhere: each is a process of its own, and each has its law.
// Things that keep their distance (trees competing for light, beds in a
// ward) stand as blue noise; things that seed and gather (moss, stains,
// rubble from one fall) stand in clusters; what follows a cause (damp near a
// leak, litter by a path) is as dense as the cause says. Sizes run by a power
// law - many small, few large; turns gather round a way (a wind's, a slope's);
// amounts between none and all by a beta.
//
// A Distribution is a state of its own holding named laws (elements of kind
// `law`): what the law is (`law`) and its numbers. Anything that spreads
// things takes its laws from it - by name, or carried onto its own elements
// by a kept functor (law_to: the law's numbers onto the element that uses
// it) - so one change of a law changes everything spread by it, and two
// domains spreading by one law agree.
//
// Value laws (draw): uniform (lo hi), normal (mean sd), lognormal (median,
// spread - the sd of its log), power (lo hi alpha: a Pareto cut at hi),
// beta (a b, scaled to lo..hi), vonmises (mean kappa: an angle round mean),
// choice (w0 w1 ...: an index by its weights).
// Point laws (points, over a rectangle): poisson (density, a hundred square
// metres), disk (spacing: Bridson's blue noise), thomas (parents, a hundred
// square metres; children, the mean of each; sd: their spread round it),
// matern (parents; children; radius: in a disc round it), and any of them
// thinned by a density a caller gives (inhomogeneous: kept where a draw is
// under it).
//
// Every draw is a function of the law, its seed and the index asked for: the
// same law and seed give the same world, on any machine, in any order.
//
// The laws (distribution_faults): a law says what it is and is one of these;
// its numbers are numbers and in their ranges.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "sg/core/State.hpp"

namespace sg {

namespace kinds {
inline const Key law{"law"};
}

class Distribution : public State {
public:
    explicit Distribution(Key id);
    Key kind() const override { return Key{"distribution"}; }

    // A law by name: what it is, and its numbers (law_of reads them).
    Element& law(Key name, const std::string& what, const Params& numbers = {});
    const Element* law(Key name) const;
    std::vector<std::string> faults() const override;
};

// A law, read from an element's params - its own (`law`, the numbers by
// their names), or, with `prefix`, those beginning with it (`size.law`,
// `size.lo` ...): a thing that carries several.
struct Law {
    std::string what;
    Params p;
    uint64_t seed = 0;
    double num(const char* k, double fallback) const;
};
Law law_of(const Params& params, const std::string& prefix = {});

// A value of a value law: its `i`th draw.
double draw(const Law& law, uint64_t i);

// A rectangle of the plane (x0..x1 by z0..z1).
struct Area {
    double x0 = 0, z0 = 0, x1 = 1, z1 = 1;
};
// The points of a point law over `area`; thinned by `density` (0..1 at a
// point: how much of it is kept) if one is given.
std::vector<Vec3d> points(const Law& law, const Area& area, const std::function<double(double, double)>& density = {});

// --- fields, and superposition ------------------------------------------------
// Every point law is an intensity over the plane - how many points a square
// metre expects there - and intensities add: points drawn from several laws
// at once are points of their sum (superposition), and a sum is a law too.
// Field laws (intensity only, sampled as any is):
//   bump   at_x at_z sd amount   a cause's own field: a Gaussian round a
//                                point (a leak, a fallen ceiling, a door)
//   stroke from_x from_z to_x to_z sd amount (sd_to amount_to)
//                                a cause along a way: a band `sd` either
//                                side of a segment, `amount` a metre of it -
//                                a trail, a path worn, something dragged -
//                                narrowing or fading to its end as `sd_to`
//                                and `amount_to` say (as at its start unless)
//   drag   path="x,z x,z ..." width contact (width_to amount amount_to fade
//          reload tug wander clots arms drips amount_drip prints amount_print seed)
//                                a body dragged along a way, what it leaves
//                                (bloodstain analysis's wipe): a smear
//                                `width` across, ragged and feathered, its
//                                streaks by what touched the floor -
//                                `clothed` (folds; the cloth lifting off in
//                                gaps), `skin` (broad, blotchy, heavy at its
//                                edges), `hair` (fine and dense); its blood
//                                running out (`fade` metres to half) and
//                                taken up again where it was set down
//                                (`reload`: distances along the way); drops
//                                beside it (`drips` a metre); whoever
//                                dragged it, walking backward ahead of it,
//                                heel first (`prints` steps, each fainter); hauled in pulls (`tug` metres: heavy where
//                                it stopped, a crescent, thin as it slid),
//                                the body off the line (`wander` metres),
//                                clots dragged out (`clots` a metre), an
//                                arm's fingers trailing at its sides (`arms`)
//   sum    of="a b c" [w_a=]     the named laws of the distribution, added
//                                (each times its weight, 1 unless said)
// The intensity of a named law of `d` at (x, z), per square metre (a
// cluster's: of its children round its parents, as drawn for `area`).
double intensity(const Distribution& d, Key law, double x, double z, const Area& area);
// Points of a named law of `d` - any, a field or a sum included - over
// `area`, by a sampling process of its own (`sample`):
//   poisson   a Poisson's with the law's intensity (Lewis and Shedler's
//             thinning: as random as rain)
//   disk      blue noise thinned by the intensity: dense where it is high,
//             but no two nearer than `spacing` (trees, beds, boulders)
//   halton    a low-discrepancy sequence thinned by it: even, no clumps,
//             no grid (scattered papers, grains)
// (A point law sampled `as` drawn: its own process - a cluster's clusters.)
std::vector<Vec3d> sample(const Distribution& d, Key law, const Area& area, const std::string& sampler = "as", double spacing = 1.0);
// A field as a picture over `area`: its intensity at each texel, scaled so
// its most is white (alpha) - for a material to read as where it wears
// (sg::Texture's `<name>_mask`), so what is strewn and what is stained by
// one field agree.
std::vector<unsigned char> field_picture(const Distribution& d, Key law, const Area& area, int px, double* most = nullptr);
// The same, `pw` pixels across `area`'s x and `ph` down its z - a long hall's
// field fine across it and as long as it is. Kept on disk by what makes it
// (sg::cache: every law of `d`), so it is worked out once.
std::vector<unsigned char> field_picture(const Distribution& d, Key law, const Area& area, int pw, int ph, double* most = nullptr);

// A stain photographed - a strip, its way along it top to bottom, its
// alpha the stain - laid along a way ("x,z x,z ..." as `drag`'s), one after
// another end to end: each `width` across (as wide as the stain is in its
// picture), as long as its picture says for that, a little shorter or
// longer, narrower or wider, mirrored or not, which by chance (`seed`);
// each `overlap` of its length over the last. A picture over `area` as
// field_picture's (its alpha, white where the stain is whole), kept on disk.
struct Strip {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};
std::vector<unsigned char> strip_picture(const std::string& way, double width, const std::vector<Strip>& strips, const Area& area, int pw, int ph,
                                         uint64_t seed = 1, double overlap = 0.12);

// A law's numbers carried onto the element that spreads by it (a kept
// functor's transport: `natives.transport("law_to", sg::law_to)`).
void law_to(const Element& law, Element& user);

// What is wrong with a distribution's laws (none: nothing).
std::vector<std::string> distribution_faults(const State& d);

}  // namespace sg
