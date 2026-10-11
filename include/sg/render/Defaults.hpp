// Backend-independent fallback settings for the existing Look passes.
#pragma once
#include <string>
#include <vector>
#include "sg/domains/Look.hpp"
namespace sg::render {
struct Quality {
    int shadow_size=2048,msaa=4;
    float bloom_strength=0.55f,bloom_threshold=1.05f,exposure=1.15f;
    int bloom_passes=3;
    bool instancing=true;
    // What things wear sent to the card packed (sg/render/Pack.hpp), where
    // the card takes it: a quarter of the bytes, as warmed before the first frame.
    bool pack=true;
    // How much of the card the views' shadow maps may hold, in megabytes:
    // past it, the maps of the views asked for longest ago are let go (and
    // laid again, whole, the frame they are next asked for) - only ever a
    // copy of what the lamps and casters say, so nothing is lost but the
    // laying. 0: as the card allows (a sixteenth of it, 512 MB to 2 GB; 1 GB
    // where the card does not say); below 0: every map kept, as many as
    // there are.
    int shadow_budget_mb=0;
    // Edges and what shimmers within a surface (highlights, paint, fine
    // lines far off) made smooth over frames: each frame's view moved by a
    // part of a pixel, and the frames before taken in where they still hold
    // (temporal antialiasing). With it, `msaa` 0 is as smooth, for less.
    bool taa=false;
    // A plane that says it reflects shows the room in it, drawn again from
    // the mirrored eye at `reflection_scale` of the screen's pixels (0.5: a
    // quarter of them), where the plane is on the screen: two such planes at
    // most. A thing's `reflects` (on the face across its thinnest side that
    // faces the eye), a room's `floor_reflects`, a window's `reflects` on its
    // glass: 0..1, how much of what is before it is seen in it face on (0.2
    // or so a polished floor, 1 a mirror; a window's glass as glass does).
    bool reflections=true;
    float reflection_scale=0.5f;
    // How many of the screen's pixels, each way, one pixel of the picture
    // takes: the view drawn at a part of the screen's size and laid on it
    // whole, each of its pixels a hard-edged square (1: the screen's own).
    // The look of a game of an older day - and a ninth of the shading at 3.
    int pixel=1;
    // The rooms round the eye's glued into one space where they agree
    // (sg::glue_space): drawn where they stand, in one frame, not each
    // through its doorway as a view of its own - no view to make when one
    // comes into sight, nothing to draw twice. Doorways that do not glue
    // (a ring that does not close, a world in its own look) are views as ever.
    bool glue=false;
};
void standard_look(LookState& look,const Quality& quality={});

// A project's quality, as a file it ships with (`render.conf`, beside the
// program: see `stategine_render_conf` in cmake/StategineModules.cmake):
// one `key = value` a line, `#` to the end of a line a remark. Every line is
// optional - a key not said keeps what `into` has, so a project with no file,
// or an empty one, is drawn as the engine draws by default. What cannot be
// taken (a key not known, a value not of its kind) is left as it was and said
// in `problems`, never a failure: the program starts all the same.
void read_quality(const std::string& text, Quality& into, std::vector<std::string>* problems = nullptr);
// The file, every key said as `q` has it and what each is: a project's
// starting point.
std::string quality_text(const Quality& q = {});
// A project's render.conf read into `into`, if there is one: the file named
// by `file`, or a render.conf beside the program `program` names (argv[0])
// when `file` is empty. False, `into` as it was, when there is none.
bool load_quality(const std::string& file, const std::string& program, Quality& into, std::vector<std::string>* problems = nullptr);
}
