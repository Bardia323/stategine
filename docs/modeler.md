# The modeller: a shape from a recipe

`sg::Modeler` (`sg/domains/Modeler.hpp`) is a state whose content is a recipe -
text, one operation a line - and whose output is a mesh: triangles as every
model here is (eight floats a corner: position, normal, uv), which a
`Spatial3D` takes with `model(name, triangles)` and draws as `shape` = "model".
It is to geometry what a painted texture is to a picture: every mark is kept,
anyone may add a line, take one back or set the whole, and the mesh is a pure
function of the text.

```cpp
sg::Modeler& m = graph.add<sg::Modeler>(Key{"castle.model"});
m.ports(graph);                         // model.op / .set / .undo / .clear come in here
engine.send(m.id(), Modeler::set_event(), Params{}.set("ops", recipe));
Vec3d size;
room.model(Key{"castle"}, m.fitted(size));   // or fitted_part(i, size) per material
room.mesh(Key{"c"}, x, y, z).params.set("shape", "model").set("model", "castle")
    .set(keys::sx, size.x).set(keys::sy, size.y).set(keys::sz, size.z);
```

`sculpt::build(text, options, files)` is the same without the state;
`sculpt::recipes()` lists the library; `sculpt::to_obj` writes a model out.
`Modeler::set_files` gives it the program's file reader for `import` (the
engine reads no file of its own, as `Texture::set_reader`).

## The language

One statement a line; `#` to the end of a line is a comment. Any number may be
an expression: `$r*1.5`, `floor(($len+$gap)/($w+$gap))`, `sin(30)` (degrees),
`pi`, `sqrt abs floor ceil round min max pow`.

### Shapes

Standing on y = 0, centred over the origin (`centre=1` centres them on it):

| statement | what it is |
| --- | --- |
| `box sx [sy sz]` | a box (one number: a cube; two: sy = sz) |
| `cyl r h` | a cylinder |
| `cone r1 r2 h`, `cone r h` | a frustum, or a cone to a point |
| `sphere r` | a sphere |
| `torus R r` | a ring, R to its middle, r thick |
| `capsule r h` | a rounded bar, h end to end |

As drawn, their coordinates where they say:

| statement | what it is |
| --- | --- |
| `lathe r,y r,y ...` | a profile turned about y, closed at its ends |
| `extrude depth x,y x,y ...` | an outline in x, y pushed out along z, centred |
| `prism h x,z x,z ...` | an outline on the ground pushed up h |
| `loft h x,z x,z ... / x,z ...` | rings of the same count, joined, equally spaced up h |
| `tube r x,y,z x,y,z ...` | a round bar along a path |
| `sweep x,y x,y ... / x,y,z ...` | an outline carried along a path |
| `import file [fit=h] [base=1] [mats=1] [faces=N]` | a Wavefront .obj; `fit` scales it to that height, `base` stands it on y = 0, `mats` keeps its materials, `faces` makes it again (below) |

### Options, on any shape, block or macro call

`at=x,y,z` `rot=yaw` or `rot=rx,ry,rz` (degrees, x then y then z) `scale=s`
or `scale=sx,sy,sz` `mat=<material>` (the part it belongs to) `round=r` (the
box or cylinder rounded, from its field) `chamfer=c` (an extrusion's edges cut
back) `sides=n` `res=metres` (the cell a cut is meshed at) `crease=degrees`
`centre=1`.

### Combining

The word before a statement says how it joins what came before in the block:

| prefix | it is |
| --- | --- |
| (none) or `add` | union - exact: the faces are kept as they are |
| `sub` | taken away |
| `and` | kept only where both are |
| `blend=k` | a smooth union, rounded over k metres |
| `carve=k` | a smooth cut |

Only what a cut touches is meshed from a signed distance field (dual
contouring, at `res` or the state's `cell`); the rest keeps its exact faces, so
a castle of boxes and cylinders with a gate cut through one wall is crisp
everywhere and costs a field only there.

### Made again: one surface at a budget

```
import scan.obj faces=20000      # a mesh sent in, made again before it is used
remesh faces=8000 [blend=0.05]   # everything so far in the block, as one surface
remesh res=0.02                  # or at a cell, not a budget
```

A shape's field is its true signed distance - an imported mesh's too: the
distance to its nearest face, inside where its winding number says, so meshes
lying in one another (a heap of parts, as a scene often is) are one solid
where any of them is. `remesh` joins the fields of everything so far
(smoothly, with `blend`) and meshes that one field at the cell that gives
about `faces` faces: one closed surface, even, with no faces hidden inside -
the mesh-to-volume-to-mesh way, which simplifies a dense or tangled mesh
better than collapsing its edges. `faces=` on any shape does it to that shape
alone. Thin things narrower than a cell close up: give the budget they need.

### Looking at it

`sculpt::picture(model, w, h)` draws four views - three-quarters from above,
front (from +z), side (from +x), top - each fitted and shaded, a colour per
material; `sculpt::png` makes it a file. The `sgmodel` tool does it from the
command line, and says faces, parts, size, open edges and errors:

```
sgmodel castle.recipe -s 1000 [-o castle.png] [--obj castle.obj] [--cell 0.05]
sgmodel -e "box 2 1 2 / sub cyl 0.4 1 / remesh faces=4000"
```

Write, look, write again.

### Blocks, to `end`

```
group [name]            everything to `end`, placed as one (`copy name` places it again)
array n=N step=dx,dy,dz [turn=deg]
radial n=N [axis=y] [arc=360]
mirror x|y|z            the block and its reflection
for i N                 the block N times, $i = 0..N-1
```

### Macros and settings

```
define tower r=2 h=8 roof=3     # a round tower with a conical roof
  cyl $r $h
  cone $r*1.25 $roof at=0,$h,0
end
tower 2 9 at=5,0,5              # by position, or tower h=6 roof=2
let n floor($len/0.6)
set res=0.05 sides=32 crease=35 mat=stone
```

The library (`sculpt::recipes()`): `column merlons battlement wall tower
turret arch window gatehouse stairs roof pyramid pine rock`. `arch` and
`window` are cutters: `sub arch 2 3 4 at=0,0,-6` cuts a gate through a wall.

### A castle

```
wall 12 4 0.9 at=0,0,-6
sub arch 2.2 3 3 at=0,0,-6
wall 12 4 0.9 at=0,0,6
wall 12 4 0.9 at=-6,0,0 rot=90
wall 12 4 0.9 at=6,0,0 rot=90
radial n=4
  tower 2 7 3 at=6,0,6 mat=stone
end
box 4 5 4
roof 4.6 4.6 2 at=0,5,0 mat=slate
```

## Rules it keeps

- The mesh is memoised on the recipe's text and settings (and on an imported
  file's stamp); the same text is never built twice in a run.
- Fields are meshed closed and wound outwards; exact shapes are closed; the
  normals are smooth within `crease` degrees and sharp past it.
- Errors are reported, a line each, in `Model::errors`; a bad line is skipped,
  never fatal.

## Architecture

`use arch` and a style (`use gothic`, `classical`, `romanesque`, `islamic`,
`japanese`, `modern`, `brutalist`, `artdeco`), then a composition:

```
building style=gothic w=12 d=9 floors=2 bays=5      # outside: four facades and a roof
interior style=classical w=10 d=14 h=6 aisles=1     # inside: floor, walls' insides, ceiling (walls=0: none of its own)
building style=classical ... inside=1               # both: hollow, its ground floor a room
church | temple | street seed=3 | courtyard | colonnade | tower
```

A style is a library that says the same words its own way (listed at the
top of `src/domains/ModelerArch.cpp`): `wall opening window doorway door band
pier base cornice roof column tower` outside, `ceiling wainscot icornice
ipier` inside, and its proportions as variables (`<style>_ww`, `_wh`,
`_sill`, `_roof`, `_floor`). A new style is a new file; any style goes in
any composition. `door=2` leaves the door standing open, `core=0` makes a
shell with nothing in it (a building gone into, whose inside is a room of
its own), `dw`/`dh` say the door's size.

The language for it: `use <library>`, `if <expr> ... else ... end`,
comparisons (`$a<2`), `rand(a, b, ...)` (the same number for the same
arguments), `if(c, a, b)`, `mix`, `clamp`, `mod`, a variable naming a macro
(`$style.window`) or another variable (`$${style}_ww`).
`sgmodel recipe --eye x,y,z,yaw,pitch,fov` looks from inside.
