// Islamic geometry: star polygons, the star-and-cross tiling, rosettes,
// strapwork lattices, muqarnas, the horseshoe and the multifoil arch, domes
// on drums, screens, the iwan. `use girih` (it uses `pointed` and `mould`).
//
//   girih.star n r d [in=]         a {n/2} star (n = 5 6 8 10 12), outer radius r, standing d out of the plane z = 0, facing +z
//   girih.starcross w h cell d     the eight-point star-and-cross tiling over w by h: stars at a square lattice, crosses between
//   girih.rosette r d              a ten-point rosette: the star, ten petals, a ring of ten
//   girih.strap w h cell d t       a lattice of octagons and squares, as bands t wide: a window grille, a screen
//   girih.muqarnas w h tiers d     a honeycomb of corbelled niches, tier over tier, each stepping out d/tiers: under a cornice, in a hood
//   girih.horseshoe w h d          a horseshoe arch: a circle that comes back in below its centre
//   girih.multifoil w h n d        a cusped arch of n lobes (5 7 9) on a round arch
//   girih.arch w h d               the pointed arch of Persia: two-centred, struck a fifth of the span in
//   girih.dome r h [drum ribs]     a bulbous dome on a drum of windows between piers, a finial
//   girih.iwan w h d               a vaulted portal in a tall frame: the arch, its reveal, a muqarnas hood, bands of tile
//   girih.screen w h cell          a mashrabiya: the strap lattice in a frame, as a screen facing +z
//   girih.band len h d             a frieze of star-and-cross
//
// Everything stands on y = 0 along x and faces +z, placed as any shape is.
// All exact.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_girih() {
    return R"LIB(
use pointed
define girih.star n=8 r=1 d=0.05 in=0   # a {n/2} star of n points (5, 6, 8, 10 or 12), outer radius r, inner radius in (0: the points' lines run through), d thick, facing +z
  let ri if($in>0,$in,$r*cos(360/$n)/cos(180/$n))
  if $n==5
    extrude $d $r*1.0000,$r*0.0000 $ri*0.8090,$ri*0.5878 $r*0.3090,$r*0.9511 $ri*-0.3090,$ri*0.9511 $r*-0.8090,$r*0.5878 $ri*-1.0000,$ri*0.0000 $r*-0.8090,$r*-0.5878 $ri*-0.3090,$ri*-0.9511 $r*0.3090,$r*-0.9511 $ri*0.8090,$ri*-0.5878 at=0,0,$d/2
  end
  if $n==6
    extrude $d $r*1.0000,$r*0.0000 $ri*0.8660,$ri*0.5000 $r*0.5000,$r*0.8660 $ri*0.0000,$ri*1.0000 $r*-0.5000,$r*0.8660 $ri*-0.8660,$ri*0.5000 $r*-1.0000,$r*0.0000 $ri*-0.8660,$ri*-0.5000 $r*-0.5000,$r*-0.8660 $ri*-0.0000,$ri*-1.0000 $r*0.5000,$r*-0.8660 $ri*0.8660,$ri*-0.5000 at=0,0,$d/2
  end
  if $n==8
    extrude $d $r*1.0000,$r*0.0000 $ri*0.9239,$ri*0.3827 $r*0.7071,$r*0.7071 $ri*0.3827,$ri*0.9239 $r*0.0000,$r*1.0000 $ri*-0.3827,$ri*0.9239 $r*-0.7071,$r*0.7071 $ri*-0.9239,$ri*0.3827 $r*-1.0000,$r*0.0000 $ri*-0.9239,$ri*-0.3827 $r*-0.7071,$r*-0.7071 $ri*-0.3827,$ri*-0.9239 $r*-0.0000,$r*-1.0000 $ri*0.3827,$ri*-0.9239 $r*0.7071,$r*-0.7071 $ri*0.9239,$ri*-0.3827 at=0,0,$d/2
  end
  if $n==10
    extrude $d $r*1.0000,$r*0.0000 $ri*0.9511,$ri*0.3090 $r*0.8090,$r*0.5878 $ri*0.5878,$ri*0.8090 $r*0.3090,$r*0.9511 $ri*0.0000,$ri*1.0000 $r*-0.3090,$r*0.9511 $ri*-0.5878,$ri*0.8090 $r*-0.8090,$r*0.5878 $ri*-0.9511,$ri*0.3090 $r*-1.0000,$r*0.0000 $ri*-0.9511,$ri*-0.3090 $r*-0.8090,$r*-0.5878 $ri*-0.5878,$ri*-0.8090 $r*-0.3090,$r*-0.9511 $ri*-0.0000,$ri*-1.0000 $r*0.3090,$r*-0.9511 $ri*0.5878,$ri*-0.8090 $r*0.8090,$r*-0.5878 $ri*0.9511,$ri*-0.3090 at=0,0,$d/2
  end
  if $n==12
    extrude $d $r*1.0000,$r*0.0000 $ri*0.9659,$ri*0.2588 $r*0.8660,$r*0.5000 $ri*0.7071,$ri*0.7071 $r*0.5000,$r*0.8660 $ri*0.2588,$ri*0.9659 $r*0.0000,$r*1.0000 $ri*-0.2588,$ri*0.9659 $r*-0.5000,$r*0.8660 $ri*-0.7071,$ri*0.7071 $r*-0.8660,$r*0.5000 $ri*-0.9659,$ri*0.2588 $r*-1.0000,$r*0.0000 $ri*-0.9659,$ri*-0.2588 $r*-0.8660,$r*-0.5000 $ri*-0.7071,$ri*-0.7071 $r*-0.5000,$r*-0.8660 $ri*-0.2588,$ri*-0.9659 $r*-0.0000,$r*-1.0000 $ri*0.2588,$ri*-0.9659 $r*0.5000,$r*-0.8660 $ri*0.7071,$ri*-0.7071 $r*0.8660,$r*-0.5000 $ri*0.9659,$ri*-0.2588 at=0,0,$d/2
  end
end
define girih.starcross w=4 h=4 cell=0.8 d=0.03   # the star-and-cross tiling over w by h, foot on y = 0, facing +z: eight-point stars at a square lattice, touching tip to tip; between four of them, a cross
  let nx max(1,floor($w/$cell))
  let ny max(1,floor($h/$cell))
  let r $cell/2
  for i $nx
    for j $ny
      girih.star 8 $r*0.98 $d at=-$w/2+$cell*($i+0.5),$cell*($j+0.5),0
    end
  end
end
define girih.band len=6 h=0.6 d=0.03   # a frieze of star-and-cross along x
  girih.starcross $len $h $h $d
end
define girih.rosette r=1 d=0.05   # a ten-point rosette: the star at the middle, ten petals (kites) round it, a ring of ten pentagons about them
  girih.star 10 $r*0.42 $d
  radial n=10 axis=z
    extrude $d 0,$r*0.42 $r*0.14,$r*0.58 0,$r*0.78 -$r*0.14,$r*0.58 at=0,0,$d/2 rot=0,0,18
  end
  radial n=10 axis=z
    extrude $d -$r*0.12,$r*0.76 $r*0.12,$r*0.76 $r*0.2,$r*0.92 0,$r -$r*0.2,$r*0.92 at=0,0,$d/2
  end
end
define girih.strap w=3 h=3 cell=0.6 d=0.04 t=0.05   # a lattice of octagons and squares as bands t wide: each cell an octagon, its corners cut, the squares between them where four meet; foot on y = 0, facing +z
  let nx max(1,round($w/$cell))
  let ny max(1,round($h/$cell))
  let c $cell
  let k $c*0.2929
  for i $nx
    for j $ny
      group at=-$w/2+$c*($i+0.5),$c*($j+0.5),$d/2
        box $c-$k*2 $t $d at=0,$c/2-$t/2,0
        box $c-$k*2 $t $d at=0,-$c/2+$t/2,0
        box $t $c-$k*2 $d at=$c/2-$t/2,0,0
        box $t $c-$k*2 $d at=-$c/2+$t/2,0,0
        box $k*1.5 $t $d at=$c/2-$k/2,$c/2-$k/2,0 rot=0,0,-45
        box $k*1.5 $t $d at=-$c/2+$k/2,$c/2-$k/2,0 rot=0,0,45
        box $k*1.5 $t $d at=$c/2-$k/2,-$c/2+$k/2,0 rot=0,0,45
        box $k*1.5 $t $d at=-$c/2+$k/2,-$c/2+$k/2,0 rot=0,0,-45
      end
    end
  end
end
define girih.cell w=0.4 h=0.35 d=0.3   # one niche of a muqarnas: a hood lofted from a half octagon at its mouth to a point at its back, hanging under y = h, its mouth on the plane z = 0 opening to +z
  loft $h -$w/2,0 -$w/2,-$d*0.1 -$w*0.3536,-$d*0.65 0,-$d $w*0.3536,-$d*0.65 $w/2,-$d*0.1 $w/2,0 / -$w*0.08,0 -$w*0.08,-$d*0.02 -$w*0.06,-$d*0.1 0,-$d*0.14 $w*0.06,-$d*0.1 $w*0.08,-$d*0.02 $w*0.08,0
end
define girih.muqarnas w=4 h=1.2 tiers=3 d=0.6   # a honeycomb of niches under y = h along x, each tier standing out d/tiers from the one above: the niches of a tier side by side, the next tier's between them
  let th $h/$tiers
  let step $th*1.15
  for t $tiers
    let z ($tiers-1-$t)*$d/$tiers
    let n max(1,floor(($w-$z*0.5)/$step))
    let off mod($t,2)*$step/2
    for i $n
      let x -$n*$step/2+$step*($i+0.5)+$off
      if $x<$w/2-$step/2+0.001
        girih.cell $step*0.98 $th $d/$tiers at=$x,$t*$th,$z
      end
    end
    box $w $th $z+$d/$tiers at=0,$t*$th,($z-$d/$tiers)/2
  end
end
define girih.horseshoe w=1.5 h=3 d=1   # a horseshoe arch: a circle whose centre stands above the springing, the arc coming back in below it to the springers (as at Cordoba), span w at the springing, crown at h
  let R $w/(2*0.866)
  let yc $h-$R
  extrude $d -$w/2,0 $w/2,0 $R*0.8660,$yc+$R*-0.5000 $R*0.9659,$yc+$R*-0.2588 $R*1.0000,$yc+$R*0.0000 $R*0.9659,$yc+$R*0.2588 $R*0.8660,$yc+$R*0.5000 $R*0.7071,$yc+$R*0.7071 $R*0.5000,$yc+$R*0.8660 $R*0.2588,$yc+$R*0.9659 $R*0.0000,$yc+$R*1.0000 $R*-0.2588,$yc+$R*0.9659 $R*-0.5000,$yc+$R*0.8660 $R*-0.7071,$yc+$R*0.7071 $R*-0.8660,$yc+$R*0.5000 $R*-0.9659,$yc+$R*0.2588 $R*-1.0000,$yc+$R*0.0000 $R*-0.9659,$yc+$R*-0.2588 $R*-0.8660,$yc+$R*-0.5000
end
define girih.multifoil w=2 h=3 n=7 d=1   # a cusped arch: n lobes (5, 7 or 9), each a small semicircle, round a round arch of span w, crown at h
  let R $w/2
  let s $h-$R*1.1
  let Rc $R*0.92
  let rl $Rc*sin(90/$n)
  if $n==5
    extrude $d -$w/2,0 $w/2,0 $w/2,$s $Rc*0.9511+$rl*0.3090,$s+$Rc*0.3090+$rl*-0.9511 $Rc*0.9511+$rl*0.8910,$s+$Rc*0.3090+$rl*-0.4540 $Rc*0.9511+$rl*0.9511,$s+$Rc*0.3090+$rl*0.3090 $Rc*0.9511+$rl*0.4540,$s+$Rc*0.3090+$rl*0.8910 $Rc*0.9511+$rl*-0.3090,$s+$Rc*0.3090+$rl*0.9511 $Rc*0.5878+$rl*0.8090,$s+$Rc*0.8090+$rl*-0.5878 $Rc*0.5878+$rl*0.9877,$s+$Rc*0.8090+$rl*0.1564 $Rc*0.5878+$rl*0.5878,$s+$Rc*0.8090+$rl*0.8090 $Rc*0.5878+$rl*-0.1564,$s+$Rc*0.8090+$rl*0.9877 $Rc*0.5878+$rl*-0.8090,$s+$Rc*0.8090+$rl*0.5878 $Rc*0.0000+$rl*1.0000,$s+$Rc*1.0000+$rl*0.0000 $Rc*0.0000+$rl*0.7071,$s+$Rc*1.0000+$rl*0.7071 $Rc*0.0000+$rl*0.0000,$s+$Rc*1.0000+$rl*1.0000 $Rc*0.0000+$rl*-0.7071,$s+$Rc*1.0000+$rl*0.7071 $Rc*0.0000+$rl*-1.0000,$s+$Rc*1.0000+$rl*0.0000 $Rc*-0.5878+$rl*0.8090,$s+$Rc*0.8090+$rl*0.5878 $Rc*-0.5878+$rl*0.1564,$s+$Rc*0.8090+$rl*0.9877 $Rc*-0.5878+$rl*-0.5878,$s+$Rc*0.8090+$rl*0.8090 $Rc*-0.5878+$rl*-0.9877,$s+$Rc*0.8090+$rl*0.1564 $Rc*-0.5878+$rl*-0.8090,$s+$Rc*0.8090+$rl*-0.5878 $Rc*-0.9511+$rl*0.3090,$s+$Rc*0.3090+$rl*0.9511 $Rc*-0.9511+$rl*-0.4540,$s+$Rc*0.3090+$rl*0.8910 $Rc*-0.9511+$rl*-0.9511,$s+$Rc*0.3090+$rl*0.3090 $Rc*-0.9511+$rl*-0.8910,$s+$Rc*0.3090+$rl*-0.4540 $Rc*-0.9511+$rl*-0.3090,$s+$Rc*0.3090+$rl*-0.9511 -$w/2,$s
  end
  if $n==7
    extrude $d -$w/2,0 $w/2,0 $w/2,$s $Rc*0.9749+$rl*0.2225,$s+$Rc*0.2225+$rl*-0.9749 $Rc*0.9749+$rl*0.8467,$s+$Rc*0.2225+$rl*-0.5320 $Rc*0.9749+$rl*0.9749,$s+$Rc*0.2225+$rl*0.2225 $Rc*0.9749+$rl*0.5320,$s+$Rc*0.2225+$rl*0.8467 $Rc*0.9749+$rl*-0.2225,$s+$Rc*0.2225+$rl*0.9749 $Rc*0.7818+$rl*0.6235,$s+$Rc*0.6235+$rl*-0.7818 $Rc*0.7818+$rl*0.9937,$s+$Rc*0.6235+$rl*-0.1120 $Rc*0.7818+$rl*0.7818,$s+$Rc*0.6235+$rl*0.6235 $Rc*0.7818+$rl*0.1120,$s+$Rc*0.6235+$rl*0.9937 $Rc*0.7818+$rl*-0.6235,$s+$Rc*0.6235+$rl*0.7818 $Rc*0.4339+$rl*0.9010,$s+$Rc*0.9010+$rl*-0.4339 $Rc*0.4339+$rl*0.9439,$s+$Rc*0.9010+$rl*0.3303 $Rc*0.4339+$rl*0.4339,$s+$Rc*0.9010+$rl*0.9010 $Rc*0.4339+$rl*-0.3303,$s+$Rc*0.9010+$rl*0.9439 $Rc*0.4339+$rl*-0.9010,$s+$Rc*0.9010+$rl*0.4339 $Rc*0.0000+$rl*1.0000,$s+$Rc*1.0000+$rl*0.0000 $Rc*0.0000+$rl*0.7071,$s+$Rc*1.0000+$rl*0.7071 $Rc*0.0000+$rl*0.0000,$s+$Rc*1.0000+$rl*1.0000 $Rc*0.0000+$rl*-0.7071,$s+$Rc*1.0000+$rl*0.7071 $Rc*0.0000+$rl*-1.0000,$s+$Rc*1.0000+$rl*0.0000 $Rc*-0.4339+$rl*0.9010,$s+$Rc*0.9010+$rl*0.4339 $Rc*-0.4339+$rl*0.3303,$s+$Rc*0.9010+$rl*0.9439 $Rc*-0.4339+$rl*-0.4339,$s+$Rc*0.9010+$rl*0.9010 $Rc*-0.4339+$rl*-0.9439,$s+$Rc*0.9010+$rl*0.3303 $Rc*-0.4339+$rl*-0.9010,$s+$Rc*0.9010+$rl*-0.4339 $Rc*-0.7818+$rl*0.6235,$s+$Rc*0.6235+$rl*0.7818 $Rc*-0.7818+$rl*-0.1120,$s+$Rc*0.6235+$rl*0.9937 $Rc*-0.7818+$rl*-0.7818,$s+$Rc*0.6235+$rl*0.6235 $Rc*-0.7818+$rl*-0.9937,$s+$Rc*0.6235+$rl*-0.1120 $Rc*-0.7818+$rl*-0.6235,$s+$Rc*0.6235+$rl*-0.7818 $Rc*-0.9749+$rl*0.2225,$s+$Rc*0.2225+$rl*0.9749 $Rc*-0.9749+$rl*-0.5320,$s+$Rc*0.2225+$rl*0.8467 $Rc*-0.9749+$rl*-0.9749,$s+$Rc*0.2225+$rl*0.2225 $Rc*-0.9749+$rl*-0.8467,$s+$Rc*0.2225+$rl*-0.5320 $Rc*-0.9749+$rl*-0.2225,$s+$Rc*0.2225+$rl*-0.9749 -$w/2,$s
  end
  if $n==9
    extrude $d -$w/2,0 $w/2,0 $w/2,$s $Rc*0.9848+$rl*0.1736,$s+$Rc*0.1736+$rl*-0.9848 $Rc*0.9848+$rl*0.8192,$s+$Rc*0.1736+$rl*-0.5736 $Rc*0.9848+$rl*0.9848,$s+$Rc*0.1736+$rl*0.1736 $Rc*0.9848+$rl*0.5736,$s+$Rc*0.1736+$rl*0.8192 $Rc*0.9848+$rl*-0.1736,$s+$Rc*0.1736+$rl*0.9848 $Rc*0.8660+$rl*0.5000,$s+$Rc*0.5000+$rl*-0.8660 $Rc*0.8660+$rl*0.9659,$s+$Rc*0.5000+$rl*-0.2588 $Rc*0.8660+$rl*0.8660,$s+$Rc*0.5000+$rl*0.5000 $Rc*0.8660+$rl*0.2588,$s+$Rc*0.5000+$rl*0.9659 $Rc*0.8660+$rl*-0.5000,$s+$Rc*0.5000+$rl*0.8660 $Rc*0.6428+$rl*0.7660,$s+$Rc*0.7660+$rl*-0.6428 $Rc*0.6428+$rl*0.9962,$s+$Rc*0.7660+$rl*0.0872 $Rc*0.6428+$rl*0.6428,$s+$Rc*0.7660+$rl*0.7660 $Rc*0.6428+$rl*-0.0872,$s+$Rc*0.7660+$rl*0.9962 $Rc*0.6428+$rl*-0.7660,$s+$Rc*0.7660+$rl*0.6428 $Rc*0.3420+$rl*0.9397,$s+$Rc*0.9397+$rl*-0.3420 $Rc*0.3420+$rl*0.9063,$s+$Rc*0.9397+$rl*0.4226 $Rc*0.3420+$rl*0.3420,$s+$Rc*0.9397+$rl*0.9397 $Rc*0.3420+$rl*-0.4226,$s+$Rc*0.9397+$rl*0.9063 $Rc*0.3420+$rl*-0.9397,$s+$Rc*0.9397+$rl*0.3420 $Rc*0.0000+$rl*1.0000,$s+$Rc*1.0000+$rl*0.0000 $Rc*0.0000+$rl*0.7071,$s+$Rc*1.0000+$rl*0.7071 $Rc*0.0000+$rl*0.0000,$s+$Rc*1.0000+$rl*1.0000 $Rc*0.0000+$rl*-0.7071,$s+$Rc*1.0000+$rl*0.7071 $Rc*0.0000+$rl*-1.0000,$s+$Rc*1.0000+$rl*0.0000 $Rc*-0.3420+$rl*0.9397,$s+$Rc*0.9397+$rl*0.3420 $Rc*-0.3420+$rl*0.4226,$s+$Rc*0.9397+$rl*0.9063 $Rc*-0.3420+$rl*-0.3420,$s+$Rc*0.9397+$rl*0.9397 $Rc*-0.3420+$rl*-0.9063,$s+$Rc*0.9397+$rl*0.4226 $Rc*-0.3420+$rl*-0.9397,$s+$Rc*0.9397+$rl*-0.3420 $Rc*-0.6428+$rl*0.7660,$s+$Rc*0.7660+$rl*0.6428 $Rc*-0.6428+$rl*0.0872,$s+$Rc*0.7660+$rl*0.9962 $Rc*-0.6428+$rl*-0.6428,$s+$Rc*0.7660+$rl*0.7660 $Rc*-0.6428+$rl*-0.9962,$s+$Rc*0.7660+$rl*0.0872 $Rc*-0.6428+$rl*-0.7660,$s+$Rc*0.7660+$rl*-0.6428 $Rc*-0.8660+$rl*0.5000,$s+$Rc*0.5000+$rl*0.8660 $Rc*-0.8660+$rl*-0.2588,$s+$Rc*0.5000+$rl*0.9659 $Rc*-0.8660+$rl*-0.8660,$s+$Rc*0.5000+$rl*0.5000 $Rc*-0.8660+$rl*-0.9659,$s+$Rc*0.5000+$rl*-0.2588 $Rc*-0.8660+$rl*-0.5000,$s+$Rc*0.5000+$rl*-0.8660 $Rc*-0.9848+$rl*0.1736,$s+$Rc*0.1736+$rl*0.9848 $Rc*-0.9848+$rl*-0.5736,$s+$Rc*0.1736+$rl*0.8192 $Rc*-0.9848+$rl*-0.9848,$s+$Rc*0.1736+$rl*0.1736 $Rc*-0.9848+$rl*-0.8192,$s+$Rc*0.1736+$rl*-0.5736 $Rc*-0.9848+$rl*-0.1736,$s+$Rc*0.1736+$rl*-0.9848 -$w/2,$s
  end
end
define girih.arch w=2 h=4 d=1   # the pointed arch of Persia and Egypt: two-centred, struck a fifth of the span in from the middle
  pointed.arch $w $h 0.2 $d
end
define girih.dome r=4 h=0 drum=1 ribs=0 piers=16   # a dome on a drum: the drum a ring of piers with glass between (its windows), a cornice; the dome bulbous (h to its point, 0: 1.3 r), ribbed if asked; an alem on top
  let hh if($h>0,$h,$r*1.3)
  let dh if($drum,$r*0.55,0)
  if $drum
    cyl $r*1.06 $dh*0.12 sides=32 mat=stone
    cyl $r*0.97 $dh sides=32 at=0,0,0 mat=glass
    radial n=$piers
      box $r*0.12 $dh $r*0.14 at=$r*0.96,0,0 mat=stone
    end
    cyl $r*1.04 $dh*0.14 sides=32 at=0,$dh*0.86,0 mat=stone
  end
  lathe 0,0 $r*0.96,0 $r*0.99,$hh*0.1 $r,$hh*0.22 $r*0.97,$hh*0.36 $r*0.88,$hh*0.5 $r*0.74,$hh*0.63 $r*0.56,$hh*0.75 $r*0.38,$hh*0.85 $r*0.2,$hh*0.93 $r*0.06,$hh*0.985 0,$hh at=0,$dh,0 sides=32 mat=tile
  if $ribs
    radial n=16
      tube $r*0.025 $r*0.98,$dh,0 $r*1.01,$dh+$hh*0.1,0 $r*1.02,$dh+$hh*0.22,0 $r*0.99,$dh+$hh*0.36,0 $r*0.9,$dh+$hh*0.5,0 $r*0.76,$dh+$hh*0.63,0 $r*0.58,$dh+$hh*0.75,0 $r*0.4,$dh+$hh*0.85,0 $r*0.22,$dh+$hh*0.93,0 $r*0.08,$dh+$hh*0.985,0 sides=8 mat=gold
    end
  end
  cyl $r*0.03 $r*0.3 at=0,$dh+$hh-0.02,0 sides=8 mat=gold
  sphere $r*0.07 at=0,$dh+$hh+$r*0.12,0 sides=12 mat=gold
  sphere $r*0.05 at=0,$dh+$hh+$r*0.24,0 sides=12 mat=gold
  cone $r*0.03 0 $r*0.16 at=0,$dh+$hh+$r*0.28,0 sides=8 mat=gold
end
define girih.iwan w=4 h=7 d=2.5   # an iwan: a tall rectangular frame (pishtaq) round a pointed arch, the vault behind it d deep with a muqarnas hood in its head, bands of tile up the frame; the portal faces +z, its foot on y = 0, the frame's face at z = 0
  let b $w*0.22
  let fw $w+$b*2
  let fh $h+$b*1.4
  box $fw $fh $d*0.1 at=0,0,-$d*0.05 mat=plaster
  group
    box $fw $fh 0.001 at=0,0,-$d*0.1
  end
  box $b $fh $d at=-$w/2-$b/2,0,-$d/2 mat=plaster
  box $b $fh $d at=$w/2+$b/2,0,-$d/2 mat=plaster
  box $fw $fh-$h+$w*0.1 $d at=0,$h-$w*0.1,-$d/2 mat=plaster
  box $w+0.02 $h $d*0.12 at=0,0,-$d+$d*0.06 mat=tile
  pointed.band $w $h 0.2 $b*0.5 $d*0.1 at=0,0,-$d*0.1 mat=tile
  pointed.band $w $h 0.2 $b*0.25 0.08 at=0,0,0.04 mat=tile
  girih.muqarnas $w*0.96 $w*0.35 3 $d*0.5 at=0,$h-$w*0.6,-$d*0.1 mat=plaster
  girih.band $b*0.9 $b*0.9 0.02 at=-$w/2-$b/2,$fh*0.1,0 mat=tile
  girih.band $b*0.9 $b*0.9 0.02 at=$w/2+$b/2,$fh*0.1,0 mat=tile
  girih.strap $b*0.9 $fh*0.7 $b*0.45 0.02 $b*0.04 at=-$w/2-$b/2,$fh*0.2,0 mat=tile
  girih.strap $b*0.9 $fh*0.7 $b*0.45 0.02 $b*0.04 at=$w/2+$b/2,$fh*0.2,0 mat=tile
  girih.starcross $fw*0.9 $b*1.2 $b*0.6 0.02 at=0,$fh-$b*1.3,0 mat=tile
end
define girih.screen w=2 h=2.4 cell=0.4   # a mashrabiya: the strap lattice in a frame of wood, facing +z, foot on y = 0
  box $w+0.16 0.08 0.08 at=0,-0.08,0 mat=wood
  box $w+0.16 0.08 0.08 at=0,$h,0 mat=wood
  box 0.08 $h 0.08 at=-$w/2-0.04,0,0 mat=wood
  box 0.08 $h 0.08 at=$w/2+0.04,0,0 mat=wood
  girih.strap $w $h $cell 0.05 0.04 at=0,0,-0.025 mat=wood
end
)LIB";
}

}  // namespace sg::sculpt
