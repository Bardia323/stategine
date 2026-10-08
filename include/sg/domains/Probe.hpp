// Stategine - Probe: the light from all round at a place, kept as data.
//
// A room lit only by a sky colour from above and a ground colour from below
// is lit alike from end to end. A probe is an element of a Spatial3D (kind
// `probe`) that says what its room's lamps give it from every way, having
// lit the room - their light come back off the walls, the floor and what
// stands there - as nine numbers a colour (spherical harmonics, bands 0 to
// 2), lamp by lamp: a set for each lamp it was baked with, lit alone at
// unit strength in white. Light adds, so what arrives now is each lamp's set
// times that lamp's light now (its `intensity` and colour): a lamp switched,
// dimmed or recoloured needs no bake. The look's own light from all round
// (its sky and ground) is the look's, as it is now, and is added to it where
// the probe is drawn: no set is kept of it, so it is never stale.
//
// A probe is a box, posed as a thing is (x, y, z its base's middle, sx sy sz,
// yaw), its light reaching what stands in it and fading out over `soft`
// metres beyond each side (or `soft.px`, `soft.nx`, `soft.py`, ... for one
// side): a box the size of its room holds its walls, and two boxes that
// meet blend across where they meet. Its sets are params, `sh.<light>`, each the 27 numbers as text
// (coefficient by coefficient, red, green and blue), with `digest` what it
// was baked from (probe_digest). A renderer reads them; a bake (a renderer's
// work, handed back as data) writes them through whatever may edit the world.
//
// The laws of a probe (probe_faults, a Spatial3D's faults): its box lies in
// its room, its numbers are numbers, every lamp it has a set for is there -
// and it was baked from the room as it is (else it is stale).
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "sg/domains/Light.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg {

namespace kinds {
inline const Key probe{"probe"};
}

// Light arriving from every way, as spherical harmonics (bands 0-2): the
// radiance's nine coefficients, each in red, green and blue.
struct Sh9 {
    std::array<std::array<double, 3>, 9> c{};

    // The nine basis functions at a way (a unit vector).
    static std::array<double, 9> basis(const Vec3d& d);
    // `w` more of `radiance` arriving from `d` (a unit vector) over `w`
    // steradians: summed over a sphere of ways, the projection.
    void add(const Vec3d& d, const Rgb& radiance, double w);
    // `s` times another, each colour by its own.
    void add(const Sh9& o, const Rgb& s);
    // The light it gives a surface facing `n`, over pi (irradiance as the
    // scene's diffuse takes it: the pi folded in).
    Rgb irradiance(const Vec3d& n) const;
    // The light arriving along `d` (radiance), its finer bands faded as a
    // rough surface blurs them: `rough` 0 the radiance, 1 the irradiance.
    Rgb radiance(const Vec3d& d, double rough = 0.0) const;
    bool finite() const;
    std::string text() const;
    // 27 numbers, as text writes them; false if they are not.
    static bool parse(const std::string& s, Sh9& out);
};

// A probe in a world: a box, `soft` metres of fade inside each side.
Element& add_probe(Spatial3D& world, Key id, Vec3d base, Vec3d size, double soft = 0.5);
std::vector<const Element*> probes_of(const State& world);

inline Key probe_set_key(Key light) { return Key{"sh." + light.str()}; }

// The sets a probe keeps, by the lamp each is of; and whether they read as numbers.
std::map<std::string, Sh9> probe_sets(const Element& probe, bool* readable = nullptr);

// What the probe's lamps give it now: each set times that lamp's light in
// `world` now (a lamp gone: nothing) - from its sets as kept, or as read.
Sh9 probe_light(const State& world, const Element& probe);
Sh9 probe_light(const State& world, const std::map<std::string, Sh9>& sets);

// What a probe's bake is made from, as a digest: the room's shell (its size,
// its walls, laid round its openings), its lamps' places and cones (not how bright, nor
// their colour - those a set is multiplied by; not a sun's, nor one that
// stands in for bounce - neither is baked), and the probe's own box.
// Things that stand in the room - and pictures on panels - are seen by the
// bake, but moving one does not make the probe stale.
std::string probe_digest(const State& world, const Element& probe);

// A bake's result for one probe: its sets (by lamp) and its digest -
// written by bake_into.
struct ProbeBake {
    Key probe;
    std::map<std::string, Sh9> sets;
    std::string digest;
};
// Writes a bake into its probes (those still there): what an edit does
// with what the renderer handed back. Sets of lamps it did not bake go.
void bake_into(Spatial3D& world, const std::vector<ProbeBake>& bakes);

// What is wrong with a world's probes (empty: nothing).
std::vector<std::string> probe_faults(const State& world);

}  // namespace sg
