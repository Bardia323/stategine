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
const Vec3d at = sculpt::stand(m.model(), {x, y, z}, yaw);   // the recipe's origin at x, y, z
room.mesh(Key{"c"}, at.x, at.y, at.z).params.set("shape", "model").set("model", "castle")
    .set(keys::sx, size.x).set(keys::sy, size.y).set(keys::sz, size.z).set(keys::yaw, yaw);
```

A thing drawn from `fitted` stands by the foot of the model's box
(`Model::foot()`, the middle of its bottom face), as every sized thing does;
`sculpt::stand` says where that foot goes so that the recipe's own origin is
where it is put - a model stands where its recipe says, whatever its box.

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
| `tube r x,y,z x,y,z ... [bend=r]` | a round bar along a path |
| `sweep x,y x,y ... / x,y,z ... [bend=r]` | an outline carried along a path |
| `import file [fit=h] [base=1] [mats=1] [faces=N]` | a Wavefront .obj; `fit` scales it to that height, `base` stands it on y = 0, `mats` keeps its materials, `faces` makes it again (below). Its faces keep their places on its pictures (`vt`), and each part says the picture its material wears (`Part::texture`, from the .mtl beside it) - a thing wears it by them with `skin` and `uv` = 1 |
| `opening w h [head=round\|pointed] [walk=1] [recess=m]` | no faces: a hole asked of the walls round it (below) |
| `grow axiom rule ... [n= angle= ...]` | a plant from an L-system (below, *Growing*) |

A sweep carries its outline along the path without twisting it: each
segment's frame is the last one turned by the least turn that takes one way
on to the next (a rotation-minimizing frame), and each corner is mitred - the
outline meets the plane half way between the way in and the way out, so a
corner bending any way (up, down, round) neither pinches nor turns. The
outline's x lies across the path, level where the path starts level, its y
the third way. A path that ends where it began goes round: no ends, its last
corner mitred too. `bend=r` rounds each corner over r metres (never cutting
back more than half a side).

### Options, on any shape, block or macro call

`at=x,y,z` `rot=yaw` or `rot=rx,ry,rz` (degrees, x then y then z) `scale=s`
or `scale=sx,sy,sz` `mat=<material>` (the part it belongs to) `round=r` (the
box or cylinder rounded, from its field) `chamfer=c` (an extrusion's edges cut
back) `sides=n` `res=metres` (the cell a cut is meshed at: on the cut, or on
what it cuts - the finer) `crease=degrees` `centre=1`.

A word is said once on a line. `rot=` may be said again, and the turns
compose, the first first: `rot=90,0,0 rot=30` stands a thing up and then turns
it about up. Any other word said twice - or a macro's parameter said both by
its place and by its name - is an error naming it, and the line is left out
(a block is still opened, placed nowhere, so its `end` still ends it).

### Combining

The word before a statement says how it joins what came before in the block:

| prefix | it is |
| --- | --- |
| (none) or `add` | union - exact: the faces are kept as they are |
| `sub` | taken away |
| `and` | kept only where both are |
| `blend=k` | a smooth union, rounded over k metres |
| `carve=k` | a smooth cut |

A cut is meshed where it cuts, and only there. `sub`, `carve=` and `blend=`
mesh their field (dual contouring, at `res` or the state's `cell`) in a box
round the cut - the cutter's box, padded by the blend and two cells - and
every piece it touches keeps its exact faces outside that box; the two are
joined along the box's face, the strip between their rims filled face by
face, into one closed surface. A cutter in pieces apart (a tower's slits)
cuts each where it is. So a castle of boxes and cylinders with a gate cut
through one wall is crisp everywhere and costs a field only round the gate,
and a quatrefoil cut through a wall at 2 cm is a thousand faces, not a
hundred and forty thousand. A cut that takes most of a piece, or whose two
sides will not join, has the piece meshed whole from its field, at the cut's
cell. `and` meshes what it keeps whole. The faces a cut makes are made only
when they are asked for: a block made again by `remesh` never pays for them.

What a field makes is then made fewer where it says least: edges collapsed by
how far that moves the surface off the planes it was made from, cheapest
first, never turning a face over or pinching it, closed as it was - flat
spans become a few large faces, a curve or an edge keeps its own, and the
exact faces round a cut stay exactly as they were.

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
(smoothly, with `blend`) and meshes that one field: one closed surface with no
faces hidden inside - the mesh-to-volume-to-mesh way. With a budget, its
resolution follows the detail: meshed at the cell the budget gives, then made
again finer (up to a quarter of that cell, as the budget allows) wherever the
cell misses the field by more than a tenth of itself - a bead on a slab, a
hand - each such place in a box of its own, joined in as a cut is; then made
fewer, flat spans first, down to the budget. So `faces=N` is at most N
faces, fewer where the surface is plain. A field missed nearly everywhere (a
figure all folds) has nowhere to spend more than anywhere else, and gets the
budget's cell. With `res=` it is meshed at that cell and made fewer where
flat. `faces=` on any shape does it to that shape alone. Thin things
narrower than a cell close up: give the budget they need.

### Openings: holes asked of the walls

```
opening 2.2 7 head=pointed recess=1 at=-7,2.5,0 rot=90   # a window in the wall at x = -7, looking +x
```

`opening w h` makes no faces. It is a request, carried with the solid as any
shape is (blocks move, turn and size it): `Model::openings`, each with its
foot's middle, which way it looks, its size, its head (`round`, `pointed`,
or square), whether it is walked through (`walk=1`) and how far back in the
wall what fills it stands (`recess`). A room whose walls the model stands
among makes each one that falls in one of its walls an opening of it, and
lays the wall round it by its one rule - so windows and an arcade are holes
in a room's walls, not faces over them (sg::Room: openings one over another,
and the wall round an arch, as `<opening>.head`).

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
turret arch window gatehouse stairs roof pyramid pine rock`; the
architecture's libraries are under *Architecture* below. `arch` and
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
  file's stamp); the same text is never built twice in a run. With a cache
  folder (sg/core/Cache.hpp) it is kept on disk too, by the text, the
  settings, the libraries a program defined and the modeller's own code - and,
  for a recipe that reads files (`import`, `use <file>`), with a digest of
  each, read back only while each file says the same.
- A model stands where its recipe says: a thing drawn from it is stood by its
  box's foot (`Model::foot`), and `sculpt::stand` puts that foot where the
  recipe's origin is wanted.
- Fields are meshed closed and wound outwards; exact shapes are closed; the
  normals are smooth within `crease` degrees and sharp past it.
- Errors are reported, a line each, in `Model::errors`; a bad line is skipped,
  never fatal.

## Growing

`grow <axiom> <rule> ...` grows a plant from an L-system: each rule is a
symbol, `:` and what it becomes (`A:F[&A]/A`), or several for one symbol with
weights (`F:0.7:F F:0.3:F/(20)F`, chosen at random by the seed). The axiom is
rewritten `n` times and the word walked by a turtle:

| symbol | what the turtle does |
| --- | --- |
| `F` `G` | a step of wood, `len` long (`f`: a step with none) |
| `+` `-` | turn about up, by `angle` degrees - or its own, `+(30)` |
| `&` `^` | pitch down, up |
| `\` `/` | roll |
| `\|` | turn about |
| `[` `]` | a branch: what it says, then back to where it began (its steps `branch` times as long) |
| `'` | its steps from here `shorten` times as long |
| `L` | leaves here, as well as at the tips |
| `!` | no leaves at the tips of what grows after it (a root) |

The wood is as thick as the pipe model says - a branch as thick as the
twigs it carries together (`pipe`, 2.2, the exponent), `width` at the foot -
so the trunk is thickest and the twigs finest. Wood thinner than `min` is
pruned (its leaves kept at the cut), runs of steps within `merge` degrees made
one span, and fewer faces go round thinner wood (`sides` round the trunk).
Each branch is a tube tapering node to node, untwisted, its end a point;
every tip carries leaves - clumps (`leaves=1`, `leaf` their size, `detail`
how round, `leafy` how many), crossed cards (`leaves=2`), leaves (`leaves=3`:
blades on stalks, folded along the midrib and drooping, `leafwidth` their
breadth as a share of their length, `leafy` to a tip, spread round it),
tufts of needles (`leaves=4`) or none (`leaves=0`). `jitter` degrees of wander at every turn and step; a pull
`bend` towards `toward=x,y,z` (down for a droop, up for the light);
`height` scales it to stand that high; `seed` which of its kind. Its wood is
`mat=` (bark), its leaves `leafmat=` (leaf). The same recipe and seed grow the
same plant.

`use trees` is a library of them: `tree.oak pine birch dead willow swamp
palm`, `bush`, `plant.grass`, `plant.reeds`, each by `h` and `seed`, and its
low-poly self with `low=1` (fewer steps and sides, leaves of eight faces,
twigs pruned). A land strews them ([terrain.md](terrain.md)).

## Architecture

Buildings are made as buildings are built: from the constructions each
tradition really used, each a library of composable words, and a style is
what chooses among them. `use arch` and a style (`use gothic`, `classical`,
`romanesque`, `islamic`, `japanese`, `modern`, `brutalist`, `artdeco`, or
`param` - below), then a composition:

```
building style=gothic w=12 d=9 floors=2 bays=5      # outside: four facades and a roof
interior style=classical w=10 d=14 h=6 aisles=1     # inside: floor, walls' insides, ceiling (walls=0: none of its own)
building style=classical ... inside=1               # both: hollow, its ground floor a room
church | cathedral | temple | mosque | palace | castle | street seed=3 | courtyard | colonnade | tower
```

A style is a library that says the same words its own way (listed at the
top of `src/domains/modeler/ModelerArch.cpp`): `wall opening window doorway door band
pier base cornice roof column tower` outside, `ceiling wainscot icornice
ipier` inside, and its proportions as variables (`<style>_ww`, `_wh`,
`_sill`, `_roof`, `_floor`, `_head` - its arches: `round`, `pointed` or
`square`). A new style is a new file; any style goes in any composition.
`door=2` leaves the door standing open, `core=0` makes a shell with nothing
in it (a building gone into, whose inside is a room of its own), `dw`/`dh`
say the door's size. Every window a facade makes it also asks for
(`opening`, in the recipe's frame, `walk=1` for the door): a room standing
inside the shell opens its own wall there, so the window seen from the
street is the window seen from the room. `inwindow <style> w h recess` is
that window as the room sees it: the glass in the opening's own shape, the
lining, the sill. `interior ... walls=0` (a room whose walls are another's
- a realm's, laid by its own rule) leaves its front wall's inside open at
the door, `dw` wide, and asks those walls for its windows down both sides,
a bay each (`ww wh wsill` their size, else the style's) - and with
`arcade=1` for a blind arcade low along them, in place of the style's
wainscot. `holes=0` asks for none.

### What a style is made of

The styles say their words with these libraries, and so may any recipe
(`use mould`, `use orders`, ...). Each file's head lists its words and what
each takes; everything stands on y = 0, runs along x and faces +z, and is
placed as any shape is. All of it is exact - swept profiles, lofts, extruded
constructions - except where a vault's web or a pendentive has to be a field,
and that at a `res` the word takes.

- **`mould`** (`ModelerMouldings.cpp`): the profiles the handbooks draw,
  struck with compasses and swept along their run - `fillet ovolo cavetto
  cyma reversa torus scotia astragal` (each `len h p`: height and
  projection) - and what is built of them: `cornice` (bed mould, dentils or
  modillions, ovolo, corona, cyma, fillet, in Vignola's shares),
  `stringcourse`, `architrave`, `dentils`, `modillions`, `frame` (an
  architrave round an opening), `panel` (raised or sunk), `baluster` and
  `balustrade`, `rustication` (courses of chamfered blocks), `quoins`,
  `keystone`, `console`, `urn finial ball`.
- **`orders`** (`ModelerOrders.cpp`): the five orders by Vignola's modules.
  Ask `order.column <kind> h` and the module is `h` over the order's number
  (Tuscan 14, Doric 16, Ionic 18, Corinthian and Composite 20); base, shaft
  (entasis, drawing in to the order's top; `fluted=1` lofts twenty flutes)
  and capital follow - echinus and abacus, volutes (a spiral tube), a bell of
  two rows of acanthus. `entablature <kind> len ent` is architrave, frieze
  (Doric triglyphs and guttae; Ionic pulvinated) and cornice; `pilaster`,
  `pedestal`, `pediment` (`seg=1` segmental; the raking cornice a profile
  swept up both slopes and mitred at the apex), `portico`, `aedicule` (a
  window's surround: sill on consoles, pilasters, entablature, pediment),
  `arcade` (piers, engaged columns, archivolts, keystones), `colonnade`,
  `archband` (the half ring of a round arch, exact).
- **`pointed`** (`ModelerPointed.cpp`): the gothic masons' geometry. A
  two-centred arch over a span `w` is struck from centres `c*w` from the
  middle with radius `w/2 + c*w`: `c` = 0 round, 0.5 equilateral, more a
  lancet, less a drop arch - `pointed.arch w h c d` (the solid: a cutter or a
  pane), `band` (the ring round it: a hood, an archivolt, a vault shell),
  `rib` (a roll under a fillet on that line), `tudor` (four-centred: arcs of
  w/4 at the haunches, flat arcs from far below), `ogee`. `vault w d h`: a
  quadripartite rib vault whose diagonal ribs are round and whose transverse
  and wall ribs are struck so that every crown meets at `h` (the centres
  derived from the diagonal's radius), the web the groin of the two pointed
  barrels they imply (a field, at `res`), a boss at the crossing; `vaults`
  a row of them. `tracery w h c lights`: lights under sub-arches of the
  same strike, mullions, a foiled roundel in the head, glass in every
  opening; `window` adds a hood mould with stops and a sill; `pinnacle`
  (gablets, a crocketed spire, a finial), `buttress` (weathered stages),
  `flyer` (a quadrant under a sloping coping), `rose`, `gable`, `foil`,
  `crocket`.
- **`girih`** (`ModelerGirih.cpp`): islamic geometry. `star n r d` ({n/2}
  stars of 5 6 8 10 12 points), `starcross` (the eight-point star-and-cross
  tiling: stars at a square lattice touching tip to tip, crosses between),
  `band`, `rosette` (ten-point, petals, a ring of ten), `strap` (a lattice of
  octagons and squares as bands), `muqarnas w h tiers d` (a honeycomb of
  lofted niches, tier over tier, each stepping out), `horseshoe` (a circle
  coming back in below its centre), `multifoil w h n` (cusped, 5 7 9 lobes),
  `arch` (Persia's: two-centred, a fifth in), `dome r` (bulbous, on a drum of
  piers and glass, ribbed if asked, an alem), `iwan` (a pishtaq round a
  pointed arch, a muqarnas hood, bands of tile), `screen` (a mashrabiya).
- **`structure`** (`ModelerStructure.cpp`): what holds up. `footing`, `wall`
  (battered if asked: a loft), `pier`, `column`, `voussoirs w h n` (an arch
  of wedge stones, a taller keystone), `barrel`, `groin` (two barrels
  crossing, a field), `dome r t [oculus]` (a shell, the lathe's profile from
  the axis down the outside and up the inside), `pdome` (pointed in
  section), `pendentives w h` (the sphere on a square bay's corners, cut to
  the walls and level at the ring: a field), `truss` (king post) and
  `roofframe`, `stair`, `dogleg` (two flights and a half landing up a
  storey), `spiral`. And the modern frame building's parts: `grid` (columns),
  `floors` (plates), `core` (a shaft with lift doors a floor), `curtain`
  (vision glass, spandrels, mullions, transoms), `ribbon`, `punched` (piers,
  spandrels, lintels, glass in frames set back; `door=1`), `storefront`,
  `balcony`, `parapet`, `penthouse`, `portal` (a steel frame), `sawtooth`,
  `dock`, `canopy`, `cladding`.
- **`city`** (`ModelerCity.cpp`): the modern city from those rules.
  `skyscraper w d floors` (tiers set back, a core, the plates, a skin -
  `facade` 0 curtain 1 fins 2 ribbon 3 punched 4 brick - a double-height
  lobby under a canopy, a crown, `spire=1`, `inside=1` to see the plates
  through the glass), `block w d floors` (a city block's perimeter building:
  `use` 0 apartments 1 offices sets the floor height, shops along the ground
  floor, balconies, `roof` 0 flat with its plant 1 pitched with dormers),
  `house` (brick, punched windows, a porch, a gabled or hipped roof,
  dormers, a chimney, `garage=1`), `rowhouse n w d` (stoops, bay windows,
  bracketed cornices, party walls), `warehouse` (a portal frame clad in
  ribbed steel under a sawtooth or gable roof, docks, an office),
  `shop`; and `city.lot kind w d seed`, `city.block w d zone seed` (a
  sidewalk, lots along its faces, buildings by zone: 0 downtown 1 midtown 2
  residential 3 industrial), `city.street`, `city.district nx nz`
  (blocks and streets, downtown at the middle and houses at the edge). Kinds
  are numbers so that a seed can choose them.

`default name value` is `let` unless the name is already set - how a library
takes a setting a program gives before `use`. `detail` is one such, read by
every library: how finely ornament is made (balusters' sides, dentils' step,
crockets, foils, consoles) - 1 at arm's length, 0.5 for a palace of four
hundred windows. Expressions also know `asin acos atan atan2(y, x) hypot`.

### A style as a point

`use param` is a style whose words read eleven axes instead of a tradition:
`param_arch` (0 flat, 0..1 a segmental arch of that rise, 1 round, 1..2
pointed to the equilateral at 2, past it a lancet), `param_orn`,
`param_mass`, `param_vert`, `param_pitch`, `param_rustic`, `param_glaze`,
`param_trace`, `param_dome`, `param_pattern`, `param_order` (0 tuscan .. 4
composite), and `param_twist`, `param_taper` for towers. Every known style is
a point (`<style>_p_<axis>`), and the style starts as the mix of two:

```
let pa gothic
let pb islamic
let pt 0.4            # forty percent of the way from one to the other
use arch
use param
let param_dome 1      # then any axis moved alone, after the `use`
building style=param w=12 d=9 floors=2 bays=5
```

Its words are composed of the libraries above - the arch's shape is one
construction continuous in the parameter, a window gets tracery, an aedicule
or a hood as the axes say, the roof a dome on a drum - so wherever the point
is, what comes out is built, not drawn: byzantine, baroque, a twisted tower
(`param_twist 60`) with a dome.

What fills a building is a library too: `use church` (`ModelerChurch.cpp`) -
`church.pew`, `pews`, `altar`, `candlestick`, `cross`, `pulpit`, `font`,
`chandelier`, `lights` and `rose` (stained glass, pane by pane, each pane a
coloured glass: `ruby cobalt amber emerald`, leaded), `organ`, `arcade` (a
style's piers and pointed arches), `redeemer` and `plinth` (a statue of
Christ, one smooth surface). Each is its own model, facing +z on y = 0.

A model leaves the engine whole: `sgmodel recipe --obj out.obj` (and the
lab's `model <name> export`) writes it as a Wavefront .obj, a part per
material.

What costs, as learned furnishing a church with it:

- A cut costs a field only in a box round it - but a box at its own cell: a
  long thin cut (a groove the length of a pew) is a long box. Say a moulding
  as a `sweep` round its path, a band with a hole in it as one `extrude` of
  its outline (the gothic vault), a carving as raised shapes - exact, a few
  hundred faces.
- A field closes anything thinner than its cell: a 5 cm panel cut at the
  default cell comes out as nothing at all.
- A budget is spent where the field is detailed, but a figure detailed all
  over has its budget's cell all over: a face that must be finer still is
  remeshed in a block of its own.

The language for it: `use <library>`, `if <expr> ... else ... end`,
comparisons (`$a<2`), `rand(a, b, ...)` (the same number for the same
arguments), `if(c, a, b)`, `mix`, `clamp`, `mod`, a variable naming a macro
(`$style.window`) or another variable (`$${style}_ww`).
`sgmodel recipe --eye x,y,z,yaw,pitch,fov` looks from inside.
