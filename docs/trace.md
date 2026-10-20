# Light traced along rays

A look may ask for its light to be traced (`trace`): the sun's shadow found
by rays sent toward its disc, in place of its maps, and what a glossy surface
mirrors by a ray sent the way its facets turn it. The light from all round is
the engine's own, as ever - its probes, relit by the lamps (README *Light
probes*) - never traced pixel by pixel. It is the renderer's, as its shadow
maps are: it reads the world and writes nothing of it. A look that does not
say `trace` draws exactly as it did and pays nothing (`sg_trace_gl`); nothing
is made until one does.

It runs wherever the renderer's GL runs - OpenGL, and Vulkan and D3D12
through `src/gl/native` - as pictures drawn over the screen that read the
world from texel buffers: no card's own ray tracing is asked for. WebGPU
does not trace.

## Asking for it

A look's scene pass settings (`look.setting(sg::passes::scene, ...)`):

| Setting | Means | Unless it says |
| --- | --- | --- |
| `trace` | trace the light: the sun's shadow, what is mirrored | 0: none |
| `trace.sun` | the sun's shadow traced (0: its maps, as ever) | 1 |
| `trace.lamps` | the nearest lamps' shadows traced (up to three that the scene shadows): a ray a pixel to a point of each one's bulb (`size`, metres; a centimetre and a half if it says none) - as soft as the bulb is big against how far what stands in the way is from it | 1 |
| `trace.mirror` | the roughest a surface may be and what it mirrors still traced, faded out from half of it (0: none traced). Rougher, one ray a pixel is noise that smoothing turns to crawling blotches; the probes' reflection is smooth there | 0.22 |
| `trace.scale` | the part of the pixels each way what is mirrored is traced at: 1, 0.5 or 0.25 | 0.5 |
| `trace.far` | how far a ray goes before it is taken for the sky, metres | 1000 |
| `trace.reach` | how far a ray from what is mirrored meets things (the ground, as far as `trace.far`) | 150 |
| `trace.sky` | the sky's light, as many times | 1 |
| `trace.brightest` | what a ray brings back at most (its brightness) | 4 |
| `trace.hold` | frames a picture's history holds at most | 24 |
| `trace.now` | traced before the scene, for this frame (a pass of the surfaces more); else after it, the scene reading the last frame's, carried to where each pixel now is | 0 |

A thing that says `trace` 0 is not met by rays: it casts no traced shadow.
One that says `cast` 0 neither (water, a walker's own legs - as with maps).
Glass, light added over what is behind it and pictures standing up are
never met. A land that says `trace` 0 is not either.

`Quality::trace` (render.conf `trace = false`) turns all of it off for a
machine too slow for it, whatever a look asks.

## What it does

**The surfaces.** The scene, as it draws the eye's view, draws beside it
(a second picture: `gl::RenderTarget::add_second`, the scene shader's
`TraceSurf`) what a ray needs of each pixel's surface: which way it faces
(an octahedron's face), how far it is, how rough. The rays are traced after
the scene from those, and the scene the next frame reads what they found,
each pixel finding where it was in the picture traced (`uTraceVP`) and
taking it only if that was its own surface. A light a frame behind costs no
pass of its own; `trace.now` traces before the scene instead, the surfaces
drawn alone first (the scene shader's `uSurfaceOnly` 5, which leaves at once
for all but water and cut-outs).

**The world.** Things: each shape's triangles under a tree of boxes (SAH,
binned; two children a node, each child's box in its parent, so a ray reads
a node once and goes into the nearer child first), made once by what the
shape is made from - its corners, the picture it wears, where it is cut out
- and kept on the card; and the instances under one more tree, made again
only when something has moved. A skin's triangles are posed on the processor
as its joints move and its tree's boxes fitted round them again, its shape
kept. A triangle carries its colour (its picture's, at four places on it,
times its thing's) and, where it is cut out of its picture, which of its 64
parts are solid - a leaf's shadow is a leaf's, never its card's. The ground:
its heights as drawn, seen from above into two squares round the eye (half a
metre a point out to half a kilometre, four metres out to four), each with
the highest of every two by two, and of those, in levels over it: a ray goes
over a whole square of the ground at once where it is above the highest of
it, and down through the levels where it is not (Tevs et al.'s maximum
mipmaps), meeting a cell's two triangles at the last. The squares are laid
again as the eye goes on two metres (thirty-two the coarse one).

**The sun.** At every pixel, one ray to a point of the sun's disc, a new one
each frame: its shadow as soft as the sun's size makes it - sharp at a
thing's foot, soft at the end of a long one. Its maps are not laid where it
is traced; the widest alone, if the look's air scatters (the air's shafts
are shadowed by it). A leaf lit through from behind is shadowed as its far
side is. A thing a shadow map was too coarse for - a branch on the sand, a
shrub's twigs - casts its own. In a room shut round with no sun of its own,
the sun or moon let in at its windows is the one traced: the rays find the
window's frame and bars, the boards nailed over it, as they find a branch.

**The lamps.** The nearest lamps the scene shadows (up to three, `trace.lamps`)
each get one ray a pixel to a point of their bulb - a disc as wide as the
bulb (`size`) square to the way to it - their shadows as soft as the bulb
is big against how far off what stands in the way is: a cage round a bulb
throws a blur, a chair's foot on the floor a sharp shadow. In the scene,
each takes the place of its lamp's shadow map where it was traced
(`uTrace.y`, `uTraceLamps`).

**What is mirrored.** For surfaces as smooth as `trace.mirror` or more - faded
out from half of it: past that, one ray a pixel is noise that no smoothing
turns into anything but blotches crawling over the surface as its history
turns over, and the probes' and the sky's reflection, smooth by making, is
the better one - a
ray the way a facet turns it (GGX, the facets seen: Heitz's), and what it
meets lit by the sun (its share as the eye's pixels already have it, where
the point met is one of them; a ray of its own where not), the strongest
lamps (each as the scene lights by it, one chosen as much as it gives and
asked if anything stands between), and the sky where the world is open to
it - so water mirrors what stands round it, not only the sky.

**Over frames, within surfaces.** Each picture is kept from frame to frame
where each pixel's surface was the same last frame (as far from where the
eye was, facing the same way), by the four nearest pixels that were; held to
what this frame's neighbours say it could be (their least and most for the
sun - a moving shadow leaves nothing behind - their spread round their mean
for the rest), a history so held taken as younger than it said. What is
mirrored in something smooth is found where its image was, not the mirror.
Then smoothed within surfaces (Dammertz's a-trous, a few passes, each twice
as wide; the sun's only within its penumbra and only as wide as it is), and
the part-of-the-pixels pictures laid on every pixel by the four nearest that
are of its surface. More rays where a history is new - just uncovered - and
fewer where it holds (variable-rate, after Infinity Ward's).

## What it costs

On a 3090 Ti, whole frames:

| | Not traced | `trace` |
| --- | --- | --- |
| memory-leak's `desert`, the crane over the dunes (1440 x 654) | 13.4 ms | 12.7 ms |
| memory-leak's house, `tour`, living room (1600 x 900) | 8.4 ms | 16.2 ms |

Under a sun, the sun's maps it no longer lays pay for all of it and more (5.5 ms of
the desert's card were the sun's five maps). In a house shut round there is
no sun to trace: what it costs there is what is mirrored and the instances'
tree (3 ms of the processor's while things move) - ask for it where it pays.

`SG_TRACE_TIMES=1` says where the time goes every 60 frames (each part waited
for: only for looking); `SG_TRACE_DUMP=<folder>` writes each picture there as
a `.pfm` at frame `SG_TRACE_DUMP_AT` (60); `SG_TRACE_SKIP` 1, 2, 4 leaves out
things, the ground, the sun's rays at a ray's end (for timing).

## Where it is

`src/gl/trace/`, a part of `stategine_gl`:

| File | What it is |
| --- | --- |
| `Bvh.cpp` | the trees of boxes: made (binned SAH), moved where they are laid |
| `TraceWorld.cpp` | the world as rays meet it: shapes, skins posed, instances, the ground's squares |
| `TraceShaders.cpp` | the programs: the world met (both trees, the ground), the sky, and each pass |
| `Tracer.cpp` | a frame: what a look asks, the passes in turn, what the scene is told |

The renderer's own part is small and says so: the scene shader's
`TraceSurf` and its use of `uTrace` - only in its twin for a view traced
(`SG_TRACED`, `GLWorldView::traced_of`, as `SG_CUTOUT` is for cut-outs), so
the program every other view draws with has none of it; `draw_world` drawing the surfaces
beside the eye's view and laying no maps for a traced sun; `render` calling
the tracer before and after the eye's scene.

## Not yet

Lamps' shadows are their maps. A skin's tree is fitted, never made again, however far it bends
from how it was bound. Strewn things (`placings`) are not met. The surfaces
of views through doorways and in mirrors are not traced: what they show is
lit as ever.
