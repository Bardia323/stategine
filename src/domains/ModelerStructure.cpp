// Structure: what holds a building up, as it is really built. `use structure`
// (it uses `pointed` and `mould`). Masonry first, then the frame and skin of
// the modern building; every word exact unless it says.
//
//   structure.footing len t h          stepped courses under a wall, wider than it
//   structure.wall len h t [batter]    a wall of true thickness, leaning in by batter if asked (a loft)
//   structure.pier w h d, column r h   what carries a load down
//   structure.voussoirs w h n t d      a round arch of n wedge stones, a taller keystone
//   structure.barrel w len h t         a barrel vault: the half ring of a round arch run along z
//   structure.groin w d h t res        a groin vault: two barrels crossing, their crowns at h (a field, at res)
//   structure.dome r t [oculus]        a hemispherical shell r within, t thick, an eye at the top if asked (a cut)
//   structure.pdome r t c              a pointed dome: two-centred in section
//   structure.pendentives w h t res    the zone that carries a round dome over a square bay (a field, at res)
//   structure.truss w h t              a king-post truss in the plane z = 0: tie beam, rafters, king post, struts
//   structure.roofframe w len h n      n trusses down z, purlins and a ridge beam over them
//   structure.stair n w rise run       a straight flight rising along +z (treads on a string)
//   structure.dogleg w floor [rise run]  two flights and a half landing, up a storey
//   structure.spiral r h n [newel]     a spiral stair round its newel, n steps up h
//   structure.grid w d nx nz h r       columns on a structural grid
//   structure.floors w d floors fh t   the floor plates of a frame building
//   structure.core w d h floors fh     a service core: the shaft, a lift door on each floor
//   structure.curtain len floors fh    a curtain wall: vision glass, spandrel panels, mullions, transoms
//   structure.ribbon len floors fh     ribbon windows between solid bands
//   structure.punched len floors fh bays   punched windows in a wall: piers, spandrels, recessed glass in frames
//   structure.storefront len h         a shop front: glass, a door, a fascia for the sign, an awning
//   structure.balcony w d              a slab with its railing
//   structure.parapet len h t          a parapet with a coping
//   structure.penthouse w d h          a plant room with louvres and units on a roof
//   structure.portal w h len bays      a steel portal frame and its purlins (a shed's bones)
//   structure.sawtooth w len n h       a sawtooth roof, glass on the steep face
//   structure.dock w h                 a loading dock: platform, roller door, bumpers
//   structure.canopy w d h             a flat canopy on posts
//   structure.cladding len h [rib]     ribbed metal cladding
//
// Everything stands on y = 0, along x, facing +z; placed as any shape is.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_structure() {
    return R"LIB(
use pointed
define structure.footing len=6 t=0.6 h=0.5   # two courses under a wall whose face is at z = 0 and back at -t: each wider and lower than the one above
  box $len+0.4 $h*0.5 $t+0.4 at=0,-$h,-$t/2
  box $len+0.2 $h*0.5 $t+0.2 at=0,-$h*0.5,-$t/2
end
define structure.wall len=6 h=4 t=0.5 batter=0   # a wall along x, its face at z = 0, back at -t: battered, its face leans in by batter*t at the top
  if $batter>0
    loft $h -$len/2,-$t $len/2,-$t $len/2,0 -$len/2,0 / -$len/2,-$t $len/2,-$t $len/2,-$batter*$t -$len/2,-$batter*$t
  else
    box $len $h $t at=0,0,-$t/2
  end
end
define structure.pier w=0.8 h=4 d=0.8   # a pier: a chamfered plinth, the shaft, a plain impost
  box $w*1.2 0.25 $d*1.2
  box $w $h-0.5 $d at=0,0.25,0
  box $w*1.15 0.25 $d*1.15 at=0,$h-0.25,0
end
define structure.column r=0.3 h=4   # a plain round column on a square foot under a square cap
  box $r*2.4 $r*0.4 $r*2.4
  cyl $r $h-$r*0.8 at=0,$r*0.4,0
  box $r*2.4 $r*0.4 $r*2.4 at=0,$h-$r*0.4,0
end
define structure.voussoirs w=2 h=3 n=9 t=0.3 d=0.5   # a round arch of n wedge stones (n odd: a keystone at the crown, standing a little taller), its foot on y = 0, through z
  let R $w/2
  let s $h-$R
  let a 180/$n
  for i $n
    let a0 $a*$i
    let a1 $a*($i+1)
    let k if(2*$i+1==$n,1.18,1)
    extrude $d $R*cos($a0),$s+$R*sin($a0) ($R+$t*$k)*cos($a0),$s+($R+$t*$k)*sin($a0) ($R+$t*$k)*cos($a1),$s+($R+$t*$k)*sin($a1) $R*cos($a1),$s+$R*sin($a1)
  end
  box $t $s $d at=-$R-$t/2,0,0
  box $t $s $d at=$R+$t/2,0,0
end
define structure.barrel w=4 len=8 h=4 t=0.3   # a barrel vault along z: the half ring of a round arch of span w, crown at h, run len
  pointed.band $w $h 0 $t $len
end
define structure.groin w=6 d=6 h=5 t=0.3 res=0.15   # a groin vault over w by d: two round barrels crossing, both crowns at h, the web between them; a field, at res
  box $w+$t*2 min($w,$d)/2+$t+0.1 $d+$t*2 at=0,$h-min($w,$d)/2-0.1,0 res=$res
  sub pointed.arch $w $h 0 $d+$t*4 res=$res
  sub pointed.arch $d $h 0 $w+$t*4 rot=0,90,0 res=$res
end
define structure.dome r=4 t=0.3 oculus=0 sides=32   # a hemispherical shell, r within and t thick, its foot on y = 0; an eye `oculus` across at its crown
  let R $r
  let Ro $r+$t
  if $oculus>0
    group
      lathe 0,$Ro $Ro*0.0000,$Ro*1.0000 $Ro*0.1736,$Ro*0.9848 $Ro*0.3420,$Ro*0.9397 $Ro*0.5000,$Ro*0.8660 $Ro*0.6428,$Ro*0.7660 $Ro*0.7660,$Ro*0.6428 $Ro*0.8660,$Ro*0.5000 $Ro*0.9397,$Ro*0.3420 $Ro*0.9848,$Ro*0.1736 $Ro*1.0000,$Ro*0.0000 $R*1.0000,$R*0.0000 $R*0.9848,$R*0.1736 $R*0.9397,$R*0.3420 $R*0.8660,$R*0.5000 $R*0.7660,$R*0.6428 $R*0.6428,$R*0.7660 $R*0.5000,$R*0.8660 $R*0.3420,$R*0.9397 $R*0.1736,$R*0.9848 $R*0.0000,$R*1.0000 0,$R sides=$sides
      sub cyl $oculus/2 $t*4 at=0,$R-$t*2,0 sides=24 res=$t/3
    end
  else
    lathe 0,$Ro $Ro*0.0000,$Ro*1.0000 $Ro*0.1736,$Ro*0.9848 $Ro*0.3420,$Ro*0.9397 $Ro*0.5000,$Ro*0.8660 $Ro*0.6428,$Ro*0.7660 $Ro*0.7660,$Ro*0.6428 $Ro*0.8660,$Ro*0.5000 $Ro*0.9397,$Ro*0.3420 $Ro*0.9848,$Ro*0.1736 $Ro*1.0000,$Ro*0.0000 $R*1.0000,$R*0.0000 $R*0.9848,$R*0.1736 $R*0.9397,$R*0.3420 $R*0.8660,$R*0.5000 $R*0.7660,$R*0.6428 $R*0.6428,$R*0.7660 $R*0.5000,$R*0.8660 $R*0.3420,$R*0.9397 $R*0.1736,$R*0.9848 $R*0.0000,$R*1.0000 0,$R sides=$sides
  end
end
define structure.pdome r=4 t=0.3 c=0.25 sides=32   # a pointed dome: each side of its section an arc struck from a centre c*r past the middle
  let cx $c*$r
  let Rp $r+$cx
  let Ro $Rp+$t
  let ta acos($cx/$Rp)
  let to acos($cx/$Ro)
  lathe 0,$Ro*sin($to) -$cx+$Ro*cos($to*7/8),$Ro*sin($to*7/8) -$cx+$Ro*cos($to*6/8),$Ro*sin($to*6/8) -$cx+$Ro*cos($to*5/8),$Ro*sin($to*5/8) -$cx+$Ro*cos($to*4/8),$Ro*sin($to*4/8) -$cx+$Ro*cos($to*3/8),$Ro*sin($to*3/8) -$cx+$Ro*cos($to*2/8),$Ro*sin($to*2/8) -$cx+$Ro*cos($to*1/8),$Ro*sin($to*1/8) -$cx+$Ro*cos($to*0/8),$Ro*sin($to*0/8) -$cx+$Rp*cos($ta*0/8),$Rp*sin($ta*0/8) -$cx+$Rp*cos($ta*1/8),$Rp*sin($ta*1/8) -$cx+$Rp*cos($ta*2/8),$Rp*sin($ta*2/8) -$cx+$Rp*cos($ta*3/8),$Rp*sin($ta*3/8) -$cx+$Rp*cos($ta*4/8),$Rp*sin($ta*4/8) -$cx+$Rp*cos($ta*5/8),$Rp*sin($ta*5/8) -$cx+$Rp*cos($ta*6/8),$Rp*sin($ta*6/8) -$cx+$Rp*cos($ta*7/8),$Rp*sin($ta*7/8) 0,$Rp*sin($ta) sides=$sides
end
define structure.pendentives w=8 h=6 t=0.4 res=0.2   # over a square bay w across whose walls reach h: the sphere that stands on the bay's four corners (its equator at h), cut to the walls and cut level where the dome's ring begins at h + w/2 - what turns a square into a circle; a field, at res. The ring's cornice is laid; the dome is another word
  let Rd $w*0.7071
  let R $Rd
  let Ro $Rd+$t
  group
    lathe 0,$Ro $Ro*0.0000,$Ro*1.0000 $Ro*0.1736,$Ro*0.9848 $Ro*0.3420,$Ro*0.9397 $Ro*0.5000,$Ro*0.8660 $Ro*0.6428,$Ro*0.7660 $Ro*0.7660,$Ro*0.6428 $Ro*0.8660,$Ro*0.5000 $Ro*0.9397,$Ro*0.3420 $Ro*0.9848,$Ro*0.1736 $Ro*1.0000,$Ro*0.0000 $R*1.0000,$R*0.0000 $R*0.9848,$R*0.1736 $R*0.9397,$R*0.3420 $R*0.8660,$R*0.5000 $R*0.7660,$R*0.6428 $R*0.6428,$R*0.7660 $R*0.5000,$R*0.8660 $R*0.3420,$R*0.9397 $R*0.1736,$R*0.9848 $R*0.0000,$R*1.0000 0,$R at=0,$h,0 res=$res
    and box $w+0.02 $w/2 $w+0.02 at=0,$h,0 res=$res
  end
  cyl $w/2+$t*1.2 $t*0.6 at=0,$h+$w/2-0.01,0 sides=32
end
define structure.truss w=8 h=2.4 t=0.18   # a king-post truss in the plane z = 0: a tie beam, two rafters, the king post, two struts
  box $w $t $t at=0,0,0
  let l hypot($w/2,$h)
  let ang atan2($h,$w/2)
  box $l $t $t at=-$w/4,$h/2,0 rot=0,0,$ang
  box $l $t $t at=$w/4,$h/2,0 rot=0,0,-$ang
  box $t $h $t at=0,0,0
  let sl hypot($w/4,$h/2)
  box $sl $t*0.7 $t*0.7 at=-$w/8,$h/4+$t/2,0 rot=0,0,-$ang
  box $sl $t*0.7 $t*0.7 at=$w/8,$h/4+$t/2,0 rot=0,0,$ang
end
define structure.roofframe w=8 len=12 h=2.4 n=4 t=0.18   # n trusses down z under purlins and a ridge beam: the frame of a roof seen from inside
  array n=$n step=0,0,$len/($n-1) at=0,0,-$len/2
    structure.truss $w $h $t
  end
  box $t*1.2 $t*1.6 $len at=0,$h-$t*0.4,0
  for i 2
    box $t $t $len at=-$w*0.27,$h*0.46-$t,0
    box $t $t $len at=$w*0.27,$h*0.46-$t,0
  end
end
define structure.stair n=12 w=1.2 rise=0.18 run=0.28   # a straight flight rising along +z: treads and risers on two strings, a handrail on posts
  for i $n
    box $w $rise $run at=0,$i*$rise,$i*$run+$run/2
  end
  let slope atan2($rise*$n,$run*$n)
  box 0.05 $rise*1.6 hypot($n*$run,$n*$rise) at=-$w/2+0.025,$n*$rise/2-$rise*0.3,$n*$run/2 rot=-$slope,0,0
  box 0.05 $rise*1.6 hypot($n*$run,$n*$rise) at=$w/2-0.025,$n*$rise/2-$rise*0.3,$n*$run/2 rot=-$slope,0,0
  for i $n
    box 0.04 0.9 0.04 at=$w/2-0.05,$i*$rise+$rise,$i*$run+$run/2
  end
  box 0.05 0.05 hypot($n*$run,$n*$rise) at=$w/2-0.05,0.9+$n*$rise/2+$rise/2,$n*$run/2 rot=-$slope,0,0
end
define structure.dogleg w=2.6 floor=3.6 rise=0.18 run=0.28   # up one storey in two flights with a half landing, back over itself: the first flight up +z, the landing, the second back along -z beside it
  let n ceil($floor/$rise)
  let n1 floor($n/2)
  let n2 $n-$n1
  let r1 $floor/$n
  let fw $w/2-0.05
  structure.stair $n1 $fw $r1 $run at=-$w/4,0,0
  box $w $r1 $fw at=0,$n1*$r1-$r1,$n1*$run+$fw/2
  structure.stair $n2 $fw $r1 $run at=$w/4,$n1*$r1,$n1*$run+$fw rot=0,180,0
end
define structure.spiral r=1.2 h=3.6 n=16 newel=0.12   # a spiral stair about a newel at the origin: n wedge steps up h, a handrail on their outer edge
  let ang 360/$n
  let rise $h/$n
  cyl $newel $h+1 sides=12
  for i $n
    extrude $rise $newel*0.8,0 $r,0 $r*cos($ang*1.08),$r*sin($ang*1.08) $newel*0.8*cos($ang*1.08),$newel*0.8*sin($ang*1.08) rot=90,0,0 rot=0,-$ang*$i,0 at=0,$i*$rise+$rise/2,0
    box 0.04 0.9 0.04 at=($r-0.06)*cos($ang*($i+0.5)),$i*$rise+$rise,-($r-0.06)*sin($ang*($i+0.5))
    tube 0.03 ($r-0.06)*cos($ang*$i),$i*$rise+$rise+0.9,-($r-0.06)*sin($ang*$i) ($r-0.06)*cos($ang*($i+1)),$i*$rise+2*$rise+0.9,-($r-0.06)*sin($ang*($i+1)) sides=8
  end
end
define structure.grid w=20 d=16 nx=4 nz=3 h=3.6 r=0.25   # columns at a structural grid of nx by nz bays over w by d
  for i $nx+1
    for j $nz+1
      cyl $r $h at=-$w/2+$w/$nx*$i,0,-$d/2+$d/$nz*$j sides=12
    end
  end
end
define structure.floors w=20 d=16 floors=5 fh=3.6 t=0.3   # the floor plates: a slab t thick at every floor from the first up, and the roof slab
  for i $floors
    box $w $t $d at=0,($i+1)*$fh-$t,0
  end
end
define structure.core w=6 d=4 h=18 floors=5 fh=3.6   # a service core: a shaft, lift doors on its +z face at every floor
  box $w $h $d at=0,0,0
  for i $floors
    box 1.1 2.2 0.06 at=-$w*0.2,$i*$fh+0.15,$d/2 mat=metal
    box 1.1 2.2 0.06 at=$w*0.2,$i*$fh+0.15,$d/2 mat=metal
    box 0.9 2.1 0.06 at=0,$i*$fh+0.15,-$d/2 mat=metal
  end
end
define structure.curtain len=20 floors=5 fh=3.6 mull=1.5 spandrel=0.9 t=0.12   # a curtain wall along x, its face at z = 0: at each floor vision glass over a spandrel panel at the slab, mullions every `mull`, transoms at sill and head
  let h $floors*$fh
  let n max(1,round($len/$mull))
  box $len $h 0.02 at=0,0,-$t*0.6 mat=glass
  for i $floors
    box $len $spandrel 0.04 at=0,$i*$fh,-$t*0.5 mat=metal
    box $len 0.06 $t at=0,$i*$fh+$spandrel-0.03,-$t/2 mat=metal
    box $len 0.06 $t at=0,$i*$fh,-$t/2 mat=metal
  end
  for i $n+1
    box 0.06 $h $t at=-$len/2+$len/$n*$i,0,-$t/2 mat=metal
  end
  box $len 0.08 $t at=0,$h-0.08,-$t/2 mat=metal
end
define structure.ribbon len=20 floors=5 fh=3.6 sill=0.9 head=0.4 t=0.3 mull=1.2   # ribbon windows: at each floor a band of glass between a solid sill wall and a head band, mullions every `mull`
  let n max(1,round($len/$mull))
  for i $floors
    box $len $sill $t at=0,$i*$fh,-$t/2
    box $len $head $t at=0,($i+1)*$fh-$head,-$t/2
    box $len $fh-$sill-$head 0.02 at=0,$i*$fh+$sill,-$t*0.5 mat=glass
    box $len 0.05 0.12 at=0,$i*$fh+$sill,-$t*0.5 mat=metal
    for j $n+1
      box 0.05 $fh-$sill-$head 0.1 at=-$len/2+$len/$n*$j,$i*$fh+$sill,-$t*0.5 mat=metal
    end
  end
end
define structure.punched len=12 floors=3 fh=3.2 bays=4 ww=0 wh=0 sill=0.9 t=0.35 reveal=0.15 door=0   # a wall of punched windows: a pier between bays, a spandrel under each sill and a lintel band over each head (the wall itself, in pieces), the glass in a frame set back by `reveal`; door=1: the middle bay of the ground floor is a doorway
  let bw $len/$bays
  let ww if($ww>0,$ww,$bw*0.5)
  let wh if($wh>0,$wh,$fh*0.5)
  let mid floor($bays/2)
  for i $floors
    box $len $fh-$sill-$wh $t at=0,$i*$fh+$sill+$wh,-$t/2
    for b $bays+1
      box $bw-$ww $sill+$wh $t at=-$len/2+$bw*$b,$i*$fh,-$t/2
    end
    for b $bays
      let x -$len/2+$bw*($b+0.5)
      if $door*($i==0)*($b==$mid)
        box $ww $sill+$wh 0.06 at=$x,0,-$t+0.03 mat=wood
        box 0.04 $sill+$wh 0.1 at=$x,0,-$t+0.05 mat=metal
        box $ww+0.3 0.12 0.3 at=$x,$sill+$wh,0.0 mat=stone
      else
        box $ww $sill $t at=$x,$i*$fh,-$t/2
        box $ww $wh 0.02 at=$x,$i*$fh+$sill,-$reveal mat=glass
      end
      box $ww 0.06 0.08 at=$x,$i*$fh+$sill,-$reveal mat=metal
      box $ww 0.06 0.08 at=$x,$i*$fh+$sill+$wh-0.06,-$reveal mat=metal
      box 0.06 $wh 0.08 at=$x-$ww/2+0.03,$i*$fh+$sill,-$reveal mat=metal
      box 0.06 $wh 0.08 at=$x+$ww/2-0.03,$i*$fh+$sill,-$reveal mat=metal
      box $ww+0.1 0.06 0.14 at=$x,$i*$fh+$sill-0.06,0.02 mat=stone
    end
  end
end
define structure.storefront len=8 h=4 sign=0.7 awning=1 door=1   # a shop front along x, face at z = 0: glass to the ground on a low stall riser, mullions, a door, a fascia for the sign over all, an awning
  let gh $h-$sign
  box $len 0.3 0.2 at=0,0,-0.1 mat=stone
  box $len $gh-0.3 0.02 at=0,0.3,-0.12 mat=glass
  let n max(2,round($len/1.8))
  for i $n+1
    box 0.08 $gh 0.14 at=-$len/2+$len/$n*$i,0,-0.07 mat=metal
  end
  box $len 0.08 0.14 at=0,$gh-0.08,-0.07 mat=metal
  if $door
    box 1.0 2.2 0.06 at=0,0,-0.06 mat=metal
    box 0.04 2.0 0.1 at=0,0.1,-0.03 mat=metal
  end
  box $len $sign 0.25 at=0,$gh,0.05 mat=metal
  box $len*0.6 $sign*0.5 0.04 at=0,$gh+$sign*0.25,0.3 mat=plaster
  if $awning
    box $len*0.9 0.06 1.2 at=0,$gh-0.2,0.6 rot=12,0,0 mat=fabric
  end
end
define structure.balcony w=3 d=1.4 h=1.1 t=0.18   # a balcony slab cantilevered to +z from the wall z = 0, a railing of posts and a top rail round it
  box $w $t $d at=0,-$t,$d/2
  let n max(2,round($w/0.15))
  for i $n+1
    box 0.025 $h 0.025 at=-$w/2+0.03+($w-0.06)/$n*$i,0,$d-0.05 mat=metal
  end
  let m max(1,round($d/0.15))
  for i $m
    box 0.025 $h 0.025 at=-$w/2+0.03,0,$d/$m*($i+0.5) mat=metal
    box 0.025 $h 0.025 at=$w/2-0.03,0,$d/$m*($i+0.5) mat=metal
  end
  box $w 0.05 0.08 at=0,$h,$d-0.05 mat=metal
  box 0.08 0.05 $d at=-$w/2+0.03,$h,$d/2 mat=metal
  box 0.08 0.05 $d at=$w/2-0.03,$h,$d/2 mat=metal
end
define structure.parapet len=20 h=0.9 t=0.3   # a parapet along x, face at z = 0, a coping over it
  box $len $h $t at=0,0,-$t/2
  box $len+0.04 0.08 $t+0.1 at=0,$h,-$t/2
end
define structure.penthouse w=6 d=4 h=2.8   # a plant room on a roof: a box with louvres along its +z face, units beside it
  box $w $h $d
  let n max(2,floor($h/0.2))
  for i $n
    box $w*0.8 0.04 0.12 at=0,0.2+$i*($h-0.4)/$n,$d/2 rot=30,0,0 mat=metal
  end
  box 1.6 1.2 1.6 at=$w/2+1.2,0,0 mat=metal
  cyl 0.6 0.3 at=$w/2+1.2,1.2,0 sides=16 mat=metal
  box 0.6 2.4 0.6 at=-$w/2-0.8,0,0 mat=metal
end
define structure.portal w=18 h=6 len=30 bays=5 pitch=0.15 t=0.3   # a steel portal frame along z: at each bay a pair of columns and the rafters between them, purlins along the roof, rails along the walls
  let rise $w/2*$pitch
  for i $bays+1
    let z -$len/2+$len/$bays*$i
    box $t $h $t*0.6 at=-$w/2+$t/2,0,$z mat=metal
    box $t $h $t*0.6 at=$w/2-$t/2,0,$z mat=metal
    let l hypot($w/2,$rise)
    let ang atan2($rise,$w/2)
    box $l $t*0.9 $t*0.6 at=-$w/4,$h+$rise/2,$z rot=0,0,$ang mat=metal
    box $l $t*0.9 $t*0.6 at=$w/4,$h+$rise/2,$z rot=0,0,-$ang mat=metal
  end
  let np max(2,floor($w/3))
  for i $np+1
    let x -$w/2+$w/$np*$i
    box 0.12 0.12 $len at=$x,$h+$rise*(1-abs($x)/($w/2))+$t*0.9,0 mat=metal
  end
  for i 3
    box 0.12 0.12 $len at=-$w/2+0.06,$h*($i+1)/4,0 mat=metal
    box 0.12 0.12 $len at=$w/2-0.06,$h*($i+1)/4,0 mat=metal
  end
end
define structure.sawtooth w=18 len=30 n=4 h=2.5   # a sawtooth roof over w by len, foot on y = 0: n teeth along x, each a slope of roofing and a steep face of glass turned to -x
  let tw $w/$n
  for i $n
    let x -$w/2+$tw*$i
    extrude $len $x,0 $x+$tw,0 $x+$tw*0.2,$h mat=metal
    extrude $len $x,0 $x+$tw*0.2,$h $x+$tw*0.2-0.05,$h $x-0.05,0 mat=glass
  end
end
define structure.dock w=3.6 h=4   # a loading dock in the wall z = 0 facing +z: a raised platform, a roller door of ribbed steel, rubber bumpers, a number
  box $w+1 1.2 2.4 at=0,0,1.2 mat=concrete
  box $w $h-1.2 0.08 at=0,1.2,-0.04 mat=metal
  let n floor(($h-1.2)/0.3)
  for i $n
    box $w 0.04 0.03 at=0,1.2+0.15+$i*0.3,0.015 mat=metal
  end
  box $w+0.3 0.2 0.2 at=0,$h,0.1 mat=metal
  box 0.3 0.3 0.12 at=-$w/2+0.3,0.8,2.4 mat=rubber
  box 0.3 0.3 0.12 at=$w/2-0.3,0.8,2.4 mat=rubber
end
define structure.canopy w=6 d=3 h=3.2   # a flat canopy out to +z from the wall z = 0, on two slender posts at its edge
  box $w 0.15 $d at=0,$h,$d/2 mat=metal
  cyl 0.06 $h at=-$w/2+0.3,0,$d-0.3 sides=10 mat=metal
  cyl 0.06 $h at=$w/2-0.3,0,$d-0.3 sides=10 mat=metal
end
define structure.cladding len=12 h=6 rib=0.3 t=0.04   # ribbed metal cladding along x on the face z = 0: a sheet, a rib every `rib`
  box $len $h $t at=0,0,-$t/2 mat=metal
  let n max(1,floor($len/$rib))
  for i $n
    box 0.06 $h 0.03 at=-$len/2+$rib*($i+0.5),0,0.015 mat=metal
  end
end
)LIB";
}

}  // namespace sg::sculpt
