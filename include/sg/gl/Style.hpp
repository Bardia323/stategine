// Stategine - a style: how a world's picture is made, under its look.
//
// A look is a world's grade and its effects, a state of the world, fading
// as worlds change. Beneath it, how the picture is made at all - its curve,
// its light from all round, its occlusion, its sharpening, its lens, its
// pixels - is a style: a named set of a look's values (and a few of the
// world's own), kept as data - a `.style` file, one `pass.key = value` a
// line, `#` a remark:
//
//   composite.uSharpen = 0.6         a look's uniform (vectors: three numbers)
//   scene.trace.gi = 0               a look's setting
//   composite.shader = pixel         a pass's shader: one of the engine's
//                                    (`standard`, `pixel`: the pixel composite)
//   place.probe_cell = 1.3           a param of the world itself
//
// Laid under a look when the world is made: each value the look (or the
// world) does not already say is taken from the style - what the look says
// is its own and stays. So a look stays on top of its style, a fade between
// two worlds fades between their styles as it does their looks, and the
// renderer knows nothing of styles. The machine's quality (render.conf:
// antialiasing, the upscaler, resolution) is not a style's to say.
//
// The engine's own (`photoreal`, `psx`, `standard`) are built in; a project's
// are files, found by name in `SG_STYLES` (folders, `:`-parted), then the
// `styles` folder beside the program, then as a path.
#pragma once

#include <string>
#include <vector>

#include "sg/domains/Look.hpp"

namespace sg {
class State;
}

namespace sg::gl {

// The text of a style by name (built in, or its file); empty, and why in
// `problems`, if there is none.
std::string style_text(const std::string& name, const std::string& program = {}, std::vector<std::string>* problems = nullptr);

// `style` (a name, or its text if it has a newline) laid under `look` and
// `world` (may be null: `place.` lines are then left). What cannot be read
// is said in `problems`, never a failure. True if the style was found.
bool apply_style(LookState& look, State* world, const std::string& style, const std::string& program = {},
                 std::vector<std::string>* problems = nullptr);

// The names of the engine's own.
std::vector<std::string> built_in_styles();

}  // namespace sg::gl
