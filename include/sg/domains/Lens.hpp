// Stategine - a lens: the glass a camera sees through.
//
// A look is a world's grade and its air; the glass is the camera's (AGENTS
// rule 7: a look belongs to the state, the glass to the interface). A lens
// is a kind of glass, kept as data - its focal length, and what it does to
// the picture: a wide cheap one bows straight lines out toward the corners
// (`barrel`), parts its colours at the edges (`fringe`), darkens and softens
// its corners (`vignette`, `corner_soft`) and smears its edges outward
// (`edge_smear`); a long prime does none of it. A `.lens` file, one
// `key = value` a line, `#` a remark:
//
//   focal = 135          its focal length, mm
//   fstop = 2.8          the aperture it is set to (0 or unsaid: all sharp)
//   barrel = 0           straight lines bowed (0.04 reads wide, 0.14 a camcorder's)
//   fringe = 0.1         colours parted at its edges
//   vignette = 0.15      its corners darker
//   corner_soft = 0      its corners softer than its middle (0..1)
//   edge_smear = 0       its edges smeared out from the middle (pixels at the corner)
//
// A camera mounts one by its arrow (`camera.mount` on a Camera's lens,
// `space3.mount` on a world's own eye), the event carrying the lens's values
// (`mount_event`): its `lens` (the name), its `focal` and the `fov` that
// focal length gives over the film's height, and its glass. What the camera
// mounts the renderer draws through, in place of its look's lens (README
// *The lens*); a camera that mounts none is seen through its look's.
//
// The engine's own are built in (`lens_names`); a project's are files, found
// by name in `SG_LENSES` (folders, `:`-parted), then the `lenses` folder
// beside the program, then as a path.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sg/core/State.hpp"

namespace sg {

struct LensKind {
    std::string name;
    double focal = 50.0;        // mm
    double fstop = 0.0;         // 0: all sharp
    double barrel = 0.0, fringe = 0.0, vignette = 0.0, corner_soft = 0.0, edge_smear = 0.0;
};

// What a camera's element keeps of the lens it has mounted.
const std::vector<Key>& lens_keys();

// The text of a lens by name (built in, or its file); empty, and why in
// `problems`, if there is none.
std::string lens_text(const std::string& name, const std::string& program = {}, std::vector<std::string>* problems = nullptr);

// A lens by name, or from its text (if it has a newline); none if not found.
std::optional<LensKind> lens_named(const std::string& name, const std::string& program = {},
                                   std::vector<std::string>* problems = nullptr);

// The vertical field of view (degrees) a focal length gives over a film
// `film_height` mm tall: 24 is a full frame held level, 36 the same held
// upright (a 9:16 picture).
double lens_fov(double focal_mm, double film_height_mm = 24.0);

// The event that mounts `lens` on a camera (`camera.mount`, or a world eye's
// `space3.mount`): its values as the event's arguments, the fov its focal
// gives over `film_height_mm`.
Event mount_event(Key event, const LensKind& lens, double film_height_mm = 24.0);

// What the mount arrow does: the event's lens laid on the camera's element -
// its fov, focal and glass; what the lens leaves unsaid (no aperture) taken
// off, so nothing of the last lens stays.
void mount(Element& camera, const Event& ev);

// The names of the engine's own.
std::vector<std::string> lens_names();

}  // namespace sg
