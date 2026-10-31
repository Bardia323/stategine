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
//   focal = 135          its focal length, mm (0: glass alone - the eye keeps its view)
//   fstop = 2.8          the aperture it is set to (0 or unsaid: all sharp)
//   barrel = 0           straight lines bowed (0.04 reads wide, 0.14 a camcorder's)
//   fringe = 0.1         colours parted at its edges
//   vignette = 0.15      its corners darker
//   corner_soft = 0      its corners softer than its middle (0..1)
//   edge_smear = 0       its edges smeared out from the middle (pixels at the corner)
//
// A hung camera mounts one by its arrow (`camera.mount` on a Camera's lens),
// a world's eye through a lens state (below); the event carries the lens's values
// (`mount_event`): its `lens` (the name), its `focal` and the `fov` that
// focal length gives over the film's height, and its glass. What the camera
// mounts the renderer draws through, in place of its look's lens (README
// *The lens*); a camera that mounts none is seen through its look's.
//
// The engine's own are built in (`lens_names`); a project's are files, found
// by name in `SG_LENSES` (folders, `:`-parted), then the `lenses` folder
// beside the program, then as a path.
//
// A lens is also a state of its own (`LensState`, the DSL's `lens` kind):
// the glass, with its own arrows - mounted (`lens.mount`), focused and
// stopped down (`lens.focus`) - on its own clock line, and a world's eye
// seen through it (`look_through`): the lens embedded in the eye, its put
// (the glass onto the eye: the eye follows the lens) and its get (what the
// eye says of it - its view, where it is focused - back onto the glass).
// What the renderer draws through is the eye's; the lens is where it is
// decided. A look says nothing of the glass (rule 7).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sg/core/State.hpp"
#include "sg/core/StateGraph.hpp"

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

// The event that mounts `lens` (`camera.mount` on a hung camera,
// `lens.mount` on a lens state): its values as the event's arguments, the fov its focal
// gives over `film_height_mm`.
Event mount_event(Key event, const LensKind& lens, double film_height_mm = 24.0);

// What the mount arrow does: the event's lens laid on the camera's element -
// its fov, focal and glass; what the lens leaves unsaid (no aperture) taken
// off, so nothing of the last lens stays.
void mount(Element& camera, const Event& ev);

// The names of the engine's own.
std::vector<std::string> lens_names();

// What of an eye a lens decides: its glass (lens_keys), its view (`fov`)
// and where it is focused (`focus_distance` metres, or the thing `focus`
// and `focus_up` metres over where it stands).
const std::vector<Key>& glass_keys();

// A lens, as a state: one element, its glass (`glass_id`), mounted with a
// lens by name at the start (`normal50` unless said).
//   lens.mount   another glass (mount_event's arguments)
//   lens.focus   `focus_distance` (metres), or `focus` (a thing's id) and
//                `focus_up`; `fstop`, the aperture it is stopped to (0 all
//                sharp) - whichever it names
class LensState : public State {
public:
    static Key glass_id() { return Key{"glass"}; }
    static Key mount_event() { return Key{"lens.mount"}; }
    static Key focus_event() { return Key{"lens.focus"}; }

    explicit LensState(Key id, const std::string& lens = "normal50");
    // (One made in code: its `focal` 0 for glass alone, the eye's own view kept.)
    LensState(Key id, const LensKind& lens);

    Key kind() const override { return Key{"lens"}; }
    const Element& glass() const { return element(glass_id()); }
};

// `world`'s eye (its element `eye`, the world's camera unless said) seen
// through `lens`: the lens embedded in the eye (`<world>/<eye>:<lens>`,
// Live, open, taking no input), its put `<lens>.onto.<world>` (the glass
// onto the eye, whenever it changes) and its get `<world>.into.<lens>`
// (the eye's glass back) - held to the lens laws - the eye given the
// lens's glass at once. Returns the put's name.
Key look_through(StateGraph& g, Key world, Key lens, Key eye = Key{});

}  // namespace sg
