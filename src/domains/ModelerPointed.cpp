// Pointed-arch geometry, by compass: the constructions of the gothic
// masons. `use pointed` (it uses `mould`).
//
// A two-centred arch over a span w is struck from two centres on its
// springing line, each c*w from the middle, with radius w/2 + c*w, so the
// arcs pass through the opposite springers: c = 0 is a round arch, 0.5 the
// equilateral arch (the centres at the springers), more a lancet, less a
// drop arch. Every word takes the span and the height to the crown as the
// architect's openings do, and derives the springing from the rise:
//
//   pointed.arch w h c d          the arch, solid, through z (a cutter, a glass pane)
//   pointed.band w h c t d        the ring t thick round it: a vault shell, an archivolt, a hood (exact)
//   pointed.rib w h c r d         a moulded rib on that line: a roll under a fillet
//   pointed.tudor w h d           a four-centred (Tudor) arch: w/4 arcs at the haunches, flat arcs above
//   pointed.ogee w h d            an ogee: convex below, reversed to a point
//   pointed.vault w d h [c res]   a quadripartite rib vault over a bay w by d, crown at h: round
//                                 diagonal ribs, transverse and wall ribs struck so every crown meets
//                                 at h, the web the groin of the two pointed barrels they imply, a boss
//   pointed.vaults w d h bays     a row of them down z
//   pointed.tracery w h c lights  bar tracery in a window: lights under sub-arches, a roundel with
//                                 foils in the head, mullions, glass
//   pointed.window w h c lights   a window: tracery, a hood mould with its stops, a sill
//   pointed.pinnacle w h          a shaft with gablets, a crocketed spire, a finial
//   pointed.buttress h w d stages a buttress in weathered stages under a pinnacle
//   pointed.flyer span h rise t   a flying buttress: a quadrant arch under a sloping coping
//   pointed.rose r petals d       a rose window: a wheel of colonnettes and foils in rings
//   pointed.gable w h t           a steep gable with a coping and a cross
//   pointed.crocket s             a leaf curling out, for the edge of a spire
//
// Everything stands on y = 0, along x, through or facing +z; placed as any
// shape is. Only the vault's web is a field (two pointed barrels cut from a
// slab, at res); everything else is exact.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_pointed() {
    return R"LIB(
use mould
define pointed.arch w=1 h=2.5 c=0.5 d=1   # a two-centred arch, solid: span w, crown at h, centres c*w from the middle (0 round, 0.5 equilateral), through z
  let cx $c*$w
  let R $w/2+$cx
  let rise sqrt($R*$R-$cx*$cx)
  let s $h-$rise
  let ta acos($cx/$R)
  extrude $d -$w/2,0 $w/2,0 $w/2,$s -$cx+$R*cos($ta*1/8),$s+$R*sin($ta*1/8) -$cx+$R*cos($ta*2/8),$s+$R*sin($ta*2/8) -$cx+$R*cos($ta*3/8),$s+$R*sin($ta*3/8) -$cx+$R*cos($ta*4/8),$s+$R*sin($ta*4/8) -$cx+$R*cos($ta*5/8),$s+$R*sin($ta*5/8) -$cx+$R*cos($ta*6/8),$s+$R*sin($ta*6/8) -$cx+$R*cos($ta*7/8),$s+$R*sin($ta*7/8) 0,$h $cx-$R*cos($ta*7/8),$s+$R*sin($ta*7/8) $cx-$R*cos($ta*6/8),$s+$R*sin($ta*6/8) $cx-$R*cos($ta*5/8),$s+$R*sin($ta*5/8) $cx-$R*cos($ta*4/8),$s+$R*sin($ta*4/8) $cx-$R*cos($ta*3/8),$s+$R*sin($ta*3/8) $cx-$R*cos($ta*2/8),$s+$R*sin($ta*2/8) $cx-$R*cos($ta*1/8),$s+$R*sin($ta*1/8) -$w/2,$s
end
define pointed.band w=1 h=2.5 c=0.5 t=0.2 d=0.3   # the ring between the arch and one t outside it, exact: a vault's shell, an archivolt, a hood mould
  let cx $c*$w
  let R $w/2+$cx
  let Ro $R+$t
  let rise sqrt($R*$R-$cx*$cx)
  let riseo sqrt($Ro*$Ro-$cx*$cx)
  let s $h-$rise
  let ta acos($cx/$R)
  let to acos($cx/$Ro)
  extrude $d -$w/2-$t,0 -$w/2,0 -$w/2,$s $cx-$R*cos($ta*1/8),$s+$R*sin($ta*1/8) $cx-$R*cos($ta*2/8),$s+$R*sin($ta*2/8) $cx-$R*cos($ta*3/8),$s+$R*sin($ta*3/8) $cx-$R*cos($ta*4/8),$s+$R*sin($ta*4/8) $cx-$R*cos($ta*5/8),$s+$R*sin($ta*5/8) $cx-$R*cos($ta*6/8),$s+$R*sin($ta*6/8) $cx-$R*cos($ta*7/8),$s+$R*sin($ta*7/8) 0,$h -$cx+$R*cos($ta*7/8),$s+$R*sin($ta*7/8) -$cx+$R*cos($ta*6/8),$s+$R*sin($ta*6/8) -$cx+$R*cos($ta*5/8),$s+$R*sin($ta*5/8) -$cx+$R*cos($ta*4/8),$s+$R*sin($ta*4/8) -$cx+$R*cos($ta*3/8),$s+$R*sin($ta*3/8) -$cx+$R*cos($ta*2/8),$s+$R*sin($ta*2/8) -$cx+$R*cos($ta*1/8),$s+$R*sin($ta*1/8) $w/2,$s $w/2,0 $w/2+$t,0 $w/2+$t,$s -$cx+$Ro*cos($to*1/8),$s+$Ro*sin($to*1/8) -$cx+$Ro*cos($to*2/8),$s+$Ro*sin($to*2/8) -$cx+$Ro*cos($to*3/8),$s+$Ro*sin($to*3/8) -$cx+$Ro*cos($to*4/8),$s+$Ro*sin($to*4/8) -$cx+$Ro*cos($to*5/8),$s+$Ro*sin($to*5/8) -$cx+$Ro*cos($to*6/8),$s+$Ro*sin($to*6/8) -$cx+$Ro*cos($to*7/8),$s+$Ro*sin($to*7/8) 0,$s+$riseo $cx-$Ro*cos($to*7/8),$s+$Ro*sin($to*7/8) $cx-$Ro*cos($to*6/8),$s+$Ro*sin($to*6/8) $cx-$Ro*cos($to*5/8),$s+$Ro*sin($to*5/8) $cx-$Ro*cos($to*4/8),$s+$Ro*sin($to*4/8) $cx-$Ro*cos($to*3/8),$s+$Ro*sin($to*3/8) $cx-$Ro*cos($to*2/8),$s+$Ro*sin($to*2/8) $cx-$Ro*cos($to*1/8),$s+$Ro*sin($to*1/8) -$w/2-$t,$s
end
define pointed.rib w=4 h=4 c=0.5 r=0.12 d=0.2   # a rib on the arch's line: a flat band d deep, a round roll r thick under its edge
  let cx $c*$w
  let R $w/2+$cx
  let rise sqrt($R*$R-$cx*$cx)
  let s $h-$rise
  let ta acos($cx/$R)
  let rr $r*0.8
  let Rr $R-$rr
  let riser sqrt($Rr*$Rr-$cx*$cx)
  pointed.band $w $h $c $r*1.6 $d
  tube $r -$w/2+$rr,0,0 $cx-$Rr*cos($ta*0/8),$s+$Rr*sin($ta*0/8),0 $cx-$Rr*cos($ta*1/8),$s+$Rr*sin($ta*1/8),0 $cx-$Rr*cos($ta*2/8),$s+$Rr*sin($ta*2/8),0 $cx-$Rr*cos($ta*3/8),$s+$Rr*sin($ta*3/8),0 $cx-$Rr*cos($ta*4/8),$s+$Rr*sin($ta*4/8),0 $cx-$Rr*cos($ta*5/8),$s+$Rr*sin($ta*5/8),0 $cx-$Rr*cos($ta*6/8),$s+$Rr*sin($ta*6/8),0 $cx-$Rr*cos($ta*7/8),$s+$Rr*sin($ta*7/8),0 0,$s+$riser,0 -$cx+$Rr*cos($ta*7/8),$s+$Rr*sin($ta*7/8),0 -$cx+$Rr*cos($ta*6/8),$s+$Rr*sin($ta*6/8),0 -$cx+$Rr*cos($ta*5/8),$s+$Rr*sin($ta*5/8),0 -$cx+$Rr*cos($ta*4/8),$s+$Rr*sin($ta*4/8),0 -$cx+$Rr*cos($ta*3/8),$s+$Rr*sin($ta*3/8),0 -$cx+$Rr*cos($ta*2/8),$s+$Rr*sin($ta*2/8),0 -$cx+$Rr*cos($ta*1/8),$s+$Rr*sin($ta*1/8),0 -$cx+$Rr*cos($ta*0/8),$s+$Rr*sin($ta*0/8),0 $w/2-$rr,0,0
end
define pointed.tudor w=2 h=2 d=1   # a four-centred arch: arcs of w/4 at the haunches to 60 degrees, flatter arcs from far below meeting at the crown (its rise less than 0.43 w)
  let r1 $w/4
  let al 60
  let rise min($h,$w*0.43)
  let s $h-$rise
  let q $rise*$rise/($w/2*cos($al)+$w/2-2*$rise*sin($al))
  let r2 $r1+$q
  let o2x $w/4-$q*cos($al)
  let o2y $s-$q*sin($al)
  let fa atan2($h-$o2y,-$o2x)
  extrude $d -$w/2,0 $w/2,0 $w/4+$r1*cos($al*0/4),$s+$r1*sin($al*0/4) $w/4+$r1*cos($al*1/4),$s+$r1*sin($al*1/4) $w/4+$r1*cos($al*2/4),$s+$r1*sin($al*2/4) $w/4+$r1*cos($al*3/4),$s+$r1*sin($al*3/4) $w/4+$r1*cos($al*4/4),$s+$r1*sin($al*4/4) $o2x+$r2*cos($al+($fa-$al)*1/8),$o2y+$r2*sin($al+($fa-$al)*1/8) $o2x+$r2*cos($al+($fa-$al)*2/8),$o2y+$r2*sin($al+($fa-$al)*2/8) $o2x+$r2*cos($al+($fa-$al)*3/8),$o2y+$r2*sin($al+($fa-$al)*3/8) $o2x+$r2*cos($al+($fa-$al)*4/8),$o2y+$r2*sin($al+($fa-$al)*4/8) $o2x+$r2*cos($al+($fa-$al)*5/8),$o2y+$r2*sin($al+($fa-$al)*5/8) $o2x+$r2*cos($al+($fa-$al)*6/8),$o2y+$r2*sin($al+($fa-$al)*6/8) $o2x+$r2*cos($al+($fa-$al)*7/8),$o2y+$r2*sin($al+($fa-$al)*7/8) 0,$h -$o2x-$r2*cos($al+($fa-$al)*7/8),$o2y+$r2*sin($al+($fa-$al)*7/8) -$o2x-$r2*cos($al+($fa-$al)*6/8),$o2y+$r2*sin($al+($fa-$al)*6/8) -$o2x-$r2*cos($al+($fa-$al)*5/8),$o2y+$r2*sin($al+($fa-$al)*5/8) -$o2x-$r2*cos($al+($fa-$al)*4/8),$o2y+$r2*sin($al+($fa-$al)*4/8) -$o2x-$r2*cos($al+($fa-$al)*3/8),$o2y+$r2*sin($al+($fa-$al)*3/8) -$o2x-$r2*cos($al+($fa-$al)*2/8),$o2y+$r2*sin($al+($fa-$al)*2/8) -$o2x-$r2*cos($al+($fa-$al)*1/8),$o2y+$r2*sin($al+($fa-$al)*1/8) -$w/4-$r1*cos($al*4/4),$s+$r1*sin($al*4/4) -$w/4-$r1*cos($al*3/4),$s+$r1*sin($al*3/4) -$w/4-$r1*cos($al*2/4),$s+$r1*sin($al*2/4) -$w/4-$r1*cos($al*1/4),$s+$r1*sin($al*1/4) -$w/4-$r1*cos($al*0/4),$s+$r1*sin($al*0/4)
end
define pointed.ogee w=2 h=3 d=1   # an ogee arch: equilateral arcs to 45 degrees, then reversed arcs of the same radius meeting at a point
  let c 0.5
  let be 45
  let cx $c*$w
  let R $w/2+$cx
  let c2x -$cx+2*$R*cos($be)
  let fa 180+acos($c2x/$R)
  let rise 2*$R*sin($be)-$R*sin(acos($c2x/$R))
  let s $h-$rise
  let c2y $s+2*$R*sin($be)
  extrude $d -$w/2,0 $w/2,0 -$cx+$R*cos($be*0/4),$s+$R*sin($be*0/4) -$cx+$R*cos($be*1/4),$s+$R*sin($be*1/4) -$cx+$R*cos($be*2/4),$s+$R*sin($be*2/4) -$cx+$R*cos($be*3/4),$s+$R*sin($be*3/4) -$cx+$R*cos($be*4/4),$s+$R*sin($be*4/4) $c2x+$R*cos($be+180-($be+180-$fa)*1/8),$c2y+$R*sin($be+180-($be+180-$fa)*1/8) $c2x+$R*cos($be+180-($be+180-$fa)*2/8),$c2y+$R*sin($be+180-($be+180-$fa)*2/8) $c2x+$R*cos($be+180-($be+180-$fa)*3/8),$c2y+$R*sin($be+180-($be+180-$fa)*3/8) $c2x+$R*cos($be+180-($be+180-$fa)*4/8),$c2y+$R*sin($be+180-($be+180-$fa)*4/8) $c2x+$R*cos($be+180-($be+180-$fa)*5/8),$c2y+$R*sin($be+180-($be+180-$fa)*5/8) $c2x+$R*cos($be+180-($be+180-$fa)*6/8),$c2y+$R*sin($be+180-($be+180-$fa)*6/8) $c2x+$R*cos($be+180-($be+180-$fa)*7/8),$c2y+$R*sin($be+180-($be+180-$fa)*7/8) 0,$h -$c2x-$R*cos($be+180-($be+180-$fa)*7/8),$c2y+$R*sin($be+180-($be+180-$fa)*7/8) -$c2x-$R*cos($be+180-($be+180-$fa)*6/8),$c2y+$R*sin($be+180-($be+180-$fa)*6/8) -$c2x-$R*cos($be+180-($be+180-$fa)*5/8),$c2y+$R*sin($be+180-($be+180-$fa)*5/8) -$c2x-$R*cos($be+180-($be+180-$fa)*4/8),$c2y+$R*sin($be+180-($be+180-$fa)*4/8) -$c2x-$R*cos($be+180-($be+180-$fa)*3/8),$c2y+$R*sin($be+180-($be+180-$fa)*3/8) -$c2x-$R*cos($be+180-($be+180-$fa)*2/8),$c2y+$R*sin($be+180-($be+180-$fa)*2/8) -$c2x-$R*cos($be+180-($be+180-$fa)*1/8),$c2y+$R*sin($be+180-($be+180-$fa)*1/8) $cx-$R*cos($be*4/4),$s+$R*sin($be*4/4) $cx-$R*cos($be*3/4),$s+$R*sin($be*3/4) $cx-$R*cos($be*2/4),$s+$R*sin($be*2/4) $cx-$R*cos($be*1/4),$s+$R*sin($be*1/4) $cx-$R*cos($be*0/4),$s+$R*sin($be*0/4)
end
define pointed.vault w=6 d=6 h=8 c=-1 res=0.15 t=0.25 rib=0.14   # a quadripartite rib vault over a bay w (x) by d (z), its crown at h, springing where its round diagonal ribs put it; c<0: the transverse and wall ribs struck to meet it (else as said)
  let diag hypot($w,$d)
  let Rd $diag/2
  let s $h-$Rd
  let cw if($c<0,($Rd*$Rd-$w*$w/4)/($w*$w),$c)
  let cd if($c<0,($Rd*$Rd-$d*$d/4)/($d*$d),$c)
  let hw if($c<0,$h,$s+sqrt(($w/2+$c*$w)*($w/2+$c*$w)-$c*$c*$w*$w))
  let hd if($c<0,$h,$s+sqrt(($d/2+$c*$d)*($d/2+$c*$d)-$c*$c*$d*$d))
  group
    box $w+$t*2 $Rd+$t+0.1 $d+$t*2 at=0,$s-0.1,0 res=$res
    sub pointed.arch $w $hw $cw $d+$t*4 res=$res
    sub pointed.arch $d $hd $cd $w+$t*4 rot=0,90,0 res=$res
  end
  pointed.rib $w $hw $cw $rib $rib*1.6 at=0,0,$d/2-$rib*0.8
  pointed.rib $w $hw $cw $rib $rib*1.6 at=0,0,-$d/2+$rib*0.8
  pointed.rib $d $hd $cd $rib $rib*1.6 at=$w/2-$rib*0.8,0,0 rot=0,90,0
  pointed.rib $d $hd $cd $rib $rib*1.6 at=-$w/2+$rib*0.8,0,0 rot=0,90,0
  let ang atan2($d,$w)
  pointed.rib $diag $h 0 $rib $rib*1.6 rot=0,$ang,0
  pointed.rib $diag $h 0 $rib $rib*1.6 rot=0,-$ang,0
  sphere $rib*2.2 at=0,$h-$rib*1.2,0 sides=10
end
define pointed.vaults w=6 d=24 h=8 bays=4 res=0.15 rib=0.14   # a row of rib vaults down z, a bay each, the ribs between them shared
  let bd $d/$bays
  for i $bays
    pointed.vault $w $bd $h res=$res rib=$rib at=0,0,-$d/2+$bd*($i+0.5)
  end
end
define pointed.foil r=0.3 n=4 t=0.04   # n lobes round a ring: a quatrefoil (4), a trefoil (3), a cinquefoil (5) - leaded, facing +z, centred on the origin
  let sd max(6,round(12*$detail))
  radial n=$n axis=z
    torus $r*0.55 $t at=0,$r*0.5,0 rot=90,0,0 sides=$sd
  end
  torus $r $t*1.1 rot=90,0,0 sides=max(8,round(16*$detail))
end
define pointed.tracery w=2 h=5 c=0.5 lights=2 d=0.14   # bar tracery filling the arch: each light under a sub-arch of the same strike, mullions between, a roundel with a foil in the head, glass in every opening
  let cx $c*$w
  let R $w/2+$cx
  let rise sqrt($R*$R-$cx*$cx)
  let s $h-$rise
  let mw clamp($w*0.05,0.06,0.16)
  let lw ($w-($lights-1)*$mw)/$lights
  let lcx $c*$lw
  let lR $lw/2+$lcx
  let lrise sqrt($lR*$lR-$lcx*$lcx)
  let hl $s+$lrise
  let rr min(($h-$hl)*0.36,$w*0.26)
  let yc $hl+$rr*1.05
  for i $lights
    let x -$w/2+$lw/2+$i*($lw+$mw)
    pointed.arch $lw*0.98 $hl*0.995 $c 0.02 at=$x,0,-$d*0.3 mat=glass
    pointed.band $lw $hl $c $mw*0.8 $d at=$x,0,-$d/2
    if $i<$lights-1
      box $mw $s $d at=$x+$lw/2+$mw/2,0,-$d/2
      cyl $mw*0.45 $s at=$x+$lw/2+$mw/2,0,0 sides=8
    end
  end
  pointed.foil $rr $lights+2 $mw*0.45 at=0,$yc,-$d*0.1
  cyl $rr*0.98 0.02 at=0,$yc,-$d*0.3 rot=90,0,0 centre=1 sides=16 mat=glass
  pointed.arch $w*0.99 $h*0.998 $c 0.02 at=0,0,-$d*0.35 mat=glass
  pointed.band $w $h $c $mw*0.9 $d at=0,0,-$d/2
end
define pointed.window w=2 h=5 c=0.5 lights=2 d=0.14   # a window for the wall z = 0: its tracery in the reveal, a hood mould standing proud with a stop at either springer, a weathered sill
  let cx $c*$w
  let R $w/2+$cx
  let rise sqrt($R*$R-$cx*$cx)
  let s $h-$rise
  pointed.tracery $w $h $c $lights $d
  pointed.band $w+0.12 $h+0.1 $c 0.1 0.08 at=0,-0.04,0.04
  box 0.2 0.2 0.14 at=-$w/2-0.1,$s-0.1,0.07
  box 0.2 0.2 0.14 at=$w/2+0.1,$s-0.1,0.07
  extrude $w+0.3 0,0 0.14,0 0,0.1 at=0,-0.1,0 rot=0,-90,0
end
define pointed.crocket s=0.12   # a leaf curling out and up, for a spire's edge
  sphere $s*0.5 scale=0.7,1,1.4 at=0,$s*0.3,$s*0.4 sides=6
  sphere $s*0.35 at=0,$s*0.9,$s*0.85 sides=5
end
define pointed.pinnacle w=0.6 h=3   # a pinnacle: a square shaft with a gablet on each face, a crocketed spire, a finial
  let sh $h*0.4
  box $w $sh $w
  radial n=4
    extrude $w*0.3 -$w/2,$sh*0.6 $w/2,$sh*0.6 0,$sh*1.25 at=0,0,$w/2-$w*0.15
    box $w*0.5 $sh*0.5 $w*0.08 at=0,$sh*0.1,$w/2
  end
  cone $w*0.66 $h*0.52 sides=4 rot=0,45,0 at=0,$sh,0
  let n max(1,floor($h*0.8*$detail))
  radial n=4
    for i $n
      let f ($i+0.5)/$n
      pointed.crocket $w*0.2*(1.1-$f*0.5) at=0,$sh+$h*0.52*$f,$w*0.66*(1-$f)
    end
  end
  mould.finial $h*0.12 $w*0.12 at=0,$sh+$h*0.5,0
end
define pointed.buttress h=7 w=0.7 d=1.2 stages=2 pin=1   # a buttress against the wall z = 0 (the wall behind, the buttress standing out +z): stages set back as they rise, each weathered to a slope, a pinnacle over all
  let sh $h/($stages+0.6)
  for i $stages
    let k 1-$i*0.3/$stages
    let dd $d*$k
    box $w $sh*0.82 $dd at=0,$i*$sh,$dd/2
    extrude $w 0,0 $dd,0 $dd*0.3,$sh*0.18 0,$sh*0.3 at=0,$i*$sh+$sh*0.82,0 rot=0,-90,0
  end
  let dt $d*(1-0.3)
  box $w $sh*0.5 $dt*0.7 at=0,$stages*$sh,$dt*0.35
  if $pin
    pointed.pinnacle $w*0.9 $sh*1.6 at=0,$stages*$sh+$sh*0.5,$dt*0.35
  else
    extrude $w 0,0 $dt*0.7,0 0,$sh*0.3 at=0,$stages*$sh+$sh*0.5,0 rot=0,-90,0
  end
end
define pointed.flyer span=4 h=8 t=0.35 w=0.5   # a flying buttress from the wall at x = 0, meeting it at h, down to its pier at x = span: a quadrant arch under a sloping coping of stone
  let R $span
  let hb $h-$R-$t
  extrude $w $R,0 $R*0.9659,$R*0.2588 $R*0.866,$R*0.5 $R*0.7071,$R*0.7071 $R*0.5,$R*0.866 $R*0.2588,$R*0.9659 0,$R 0,$R+$t $R,$R*0.15+$t at=0,$hb,0
end
define pointed.rose r=2 petals=12 d=0.15   # a rose window facing +z, its middle at the origin: a roundel, colonnettes out to the ring, a foiled light between each pair, glass behind all
  torus $r $d*0.6 rot=90,0,0 sides=48
  torus $r*0.3 $d*0.45 rot=90,0,0 sides=24
  radial n=$petals axis=z
    cyl $d*0.3 $r*0.7 at=0,$r*0.3,0 sides=8
    torus $r*0.17 $d*0.3 at=0,$r*0.72,0 rot=90,0,0 sides=12
  end
  cyl $r*0.99 0.02 rot=90,0,0 centre=1 sides=48 at=0,0,-$d*0.3 mat=glass
end
define pointed.gable w=6 h=5 t=0.4   # a gable wall over a span w, h to its apex, t thick: a coping up its slopes, a cross at the top
  extrude $t -$w/2,0 $w/2,0 0,$h at=0,0,0
  let l hypot($w/2,$h)
  let ang atan2($h,$w/2)
  box $l+0.3 0.18 $t+0.24 at=-$w/4,$h/2,0 rot=0,0,$ang
  box $l+0.3 0.18 $t+0.24 at=$w/4,$h/2,0 rot=0,0,-$ang
  box 0.14 1.2 0.14 at=0,$h,0
  box 0.7 0.14 0.14 at=0,$h+0.8,0
end
)LIB";
}

}  // namespace sg::sculpt
