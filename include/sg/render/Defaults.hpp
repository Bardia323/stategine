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
    // With `taa`, the eye's view drawn at this part of the screen's pixels
    // each way and made whole over frames: each frame's few pixels laid
    // where they fall among the screen's, the frames before filling the rest
    // (temporal upsampling). 0.75: a little over half the shading; 1: none.
    float render_scale=1.0f;
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
    // Every doorway whose rooms agree sheafified, not only those that say
    // `sheaf` (sg::sheafify, `every`): to try a world as one space. A
    // sheafified doorway is drawn as an opening, the room beyond where it
    // stands; one that is not (a ring that does not close, a world in its
    // own look) is a view as ever.
    bool sheafify=false;
    // A view through a doorway the eye cannot see - hidden behind a wall of
    // its room, or what stands before it - is left out by the card: the
    // rooms the eye is in are laid as depth alone first, and each doorway
    // asked of it (never where a mirror is planned, which may see a doorway
    // the eye cannot). What the view keeps from frame to frame (its shadow
    // maps, its air) is kept as ever.
    bool skip_unseen=true;
    // Each part of a view asks only the lamps that can light it: a lamp whose
    // light ends (`range`) is left out where its range does not reach, the
    // view cut into cells and shells out from the eye. The picture is the
    // same; what is saved is the lamps a part of the view need not ask.
    // (Lamps that say no range light everywhere.)
    bool lamp_cells=true;
    // A model seen from further off drawn with fewer faces (sg::shapes::fewer):
    // made after `warm`, by a hand of their own below the frame's, kept on
    // disk, each fewer by no more than it moves its surface, and drawn only
    // where that is under a third of a pixel on the picture - so the picture
    // is the same but for a few edge pixels. For a model of a few hundred
    // faces or more, not worn by its own uvs. Off unless said: it pays where
    // much is seen far off (open land, a city across a valley), and costs a
    // world seen close (memory, and a core for a while at the start).
    bool lod=false;
    // Still things of a room drawn together: what stands still in each cell
    // of a room (`proxy_cell` metres, 16 unless the room says), and would be
    // drawn with others of its shape, merged into one mesh whose every corner
    // carries its thing's place and make - drawn as they would be, in one
    // call. A thing that moves, or is changed, is drawn on its own until its
    // cell has stood still again for a while. The same picture.
    bool proxies=true;
    // And on a card that does its own work (GL 4.3): each merged cell's
    // things culled by the card - what of a cell is out of the view, or too
    // small to see, not drawn - its draws said by the card to itself, the
    // processor doing nothing a thing. Off unless said; where the card cannot,
    // nothing changes.
    bool gpu_cull=false;
    // A picture packed for the card sent by its levels: those of 256 pixels
    // or less when it is made, each finer one as it is drawn with, a few
    // megabytes a frame at most (gl::Texture::create_packed's `streamed`) - a
    // world of many photographs is ready sooner, and holds on the card the
    // detail of what has been seen near. Off unless said.
    bool stream_textures=false;
    // Light traced along rays through the world where a look asks for it
    // (its scene pass's `trace` and `path`: docs/trace.md) - the sun's
    // shadows, the light from all round and what glossy things mirror. Off
    // here, no look's asking traces anything, and every view is drawn as
    // ever: for a card too slow for it.
    bool trace=true;
    // Who makes the eye's picture whole over frames: the engine's own
    // smoothing (`taa`, as `taa` and `render_scale` say), or the card's -
    // NVIDIA's DLSS (`dlss`: drawn at fewer pixels as `dlss_mode` says, and
    // made the screen's from them, their depth, how each pixel moved and the
    // frames before; `dlaa`: at the screen's own pixels, smoothed alone).
    // DLSS is the Vulkan backend's (SG_GPU=vulkan) on an NVIDIA card with its
    // file beside the program; asked for where it cannot be, the eye is
    // smoothed by `taa` instead and the view says why, once. A machine's
    // choice, as `trace` is: no look or world can ask for it. With DLSS the
    // scene is drawn with no multisampling, whatever `msaa` says.
    enum Upscaler { upscale_taa = 0, upscale_dlss = 1, upscale_dlaa = 2 };
    int upscaler=upscale_taa;
    // With `dlss`, how few pixels it is drawn at: `quality` (two thirds of
    // the screen's each way), `balanced` (0.58), `performance` (a half),
    // `ultra_performance` (a third), `native` (all of them: DLAA).
    enum DlssMode { dlss_quality = 0, dlss_balanced = 1, dlss_performance = 2, dlss_ultra_performance = 3, dlss_native = 4 };
    int dlss_mode=dlss_quality;
};
// The part of the screen's pixels each way DLSS draws at for a mode
// (Quality::DlssMode; 1 at `native`).
float dlss_scale(int mode);
// What the machine a program runs on says over the quality it was given -
// `SG_QUALITY`, render.conf's lines parted by `;` ("upscaler = dlss;
// dlss_mode = performance") - so one program can be drawn another way
// without changing it. Nothing said: `q` as it is.
Quality with_machine(Quality q);
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
