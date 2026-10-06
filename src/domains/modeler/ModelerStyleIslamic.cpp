// The islamic style: pointed arches in panels, screens of lattice, a frieze of
// stepped merlons, flat roofs under a dome on its drum, minarets with
// balconies. (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_islamic() {
    return R"LIB(
use girih
use structure
let islamic_ww 0.46
let islamic_wh 0.6
let islamic_sill 0.18
let islamic_roof 0.6
let islamic_head pointed
define islamic.arch w=1 h=2.5 d=1   # a four-centred pointed arch, solid: foot on y = 0, through z
  let s $h-$w*0.62
  extrude $d -$w/2,0 $w/2,0 $w/2,$s $w*0.48,$s+$w*0.18 $w*0.4,$s+$w*0.34 $w*0.26,$s+$w*0.48 0,$s+$w*0.62 -$w*0.26,$s+$w*0.48 -$w*0.4,$s+$w*0.34 -$w*0.48,$s+$w*0.18 -$w/2,$s
end
define islamic.wall len=10 h=7 t=0.6
  box $len $h $t at=0,0,-$t/2 mat=plaster
end
define islamic.opening w=1 h=2.5 d=2
  islamic.arch $w $h $d
end
define islamic.window w=1 h=2.5   # a framing panel (alfiz) of tile round a pointed arch, a mashrabiya screen in the reveal
  pointed.band $w $h 0.2 0.12 0.06 at=0,0,0.03 mat=tile
  box $w+0.6 0.1 0.08 at=0,-0.1,0.04 mat=stone
  box 0.1 $h+0.4 0.08 at=-$w/2-0.25,-0.1,0.04 mat=stone
  box 0.1 $h+0.4 0.08 at=$w/2+0.25,-0.1,0.04 mat=stone
  box $w+0.6 0.1 0.08 at=0,$h+0.2,0.04 mat=stone
  girih.strap $w*0.9 $h*0.85 clamp($w*0.3,0.2,0.5) 0.04 0.035 at=0,0.05,-0.25 mat=wood
end
define islamic.doorway w=2 h=3.4 d=2
  islamic.arch $w $h $d
end
define islamic.door w=2 h=3.4 t=0.6   # an iwan: a tall frame of tile round a pointed arch, a muqarnas hood in its head, the leaves
  girih.iwan $w $h+0.4 $t*0.9 at=0,0,0.3
  islamic.arch $w $h 0.1 at=0,0,-$t*0.5 mat=wood
  girih.star 8 $w*0.12 0.03 at=-$w*0.25,$h*0.5,-$t*0.5+0.06 mat=gold
  girih.star 8 $w*0.12 0.03 at=$w*0.25,$h*0.5,-$t*0.5+0.06 mat=gold
end
define islamic.band len=10   # a frieze of star-and-cross tile
  girih.band $len+0.04 0.4 0.03 at=0,-0.4,0 mat=tile
end
define islamic.pier h=7   # a slender engaged colonnette
  cyl 0.1 $h at=0,0,0.1 mat=stone
end
define islamic.base len=10 h=0.6
  box $len+0.1 $h 0.1 at=0,0,0.05 mat=stone
end
define islamic.cornice len=10   # a muqarnas under a frieze of tile, and stepped merlons
  girih.muqarnas $len+0.2 0.6 3 0.4 at=0,-0.6,0 mat=plaster
  girih.band $len+0.2 0.4 0.03 at=0,0,0.12 mat=tile
  box $len+0.2 0.4 0.12 at=0,0,0.06 mat=tile
  let n floor($len/0.8)
  array n=$n step=0.8,0,0 at=-($n-1)*0.4,0.4,0
    box 0.5 0.3 0.25
    box 0.3 0.25 0.25 at=0,0.3,0
    pyramid 0.32 0.2 at=0,0.55,0
  end
end
define islamic.roof w=12 d=9 h=5   # flat, and a dome on a drum of windows over the middle, an alem on its point
  let r min($w,$d)*0.32
  box $w-0.3 0.2 $d-0.3 mat=plaster
  girih.dome $r drum=1 at=0,0.2,0
end
define islamic.column h=4 r=0.25   # a slim shaft, a block capital
  cyl $r*1.2 $h*0.05 mat=stone
  cyl $r $h*0.85 at=0,$h*0.05,0 mat=stone
  box $r*2.6 $h*0.1 $r*2.6 at=0,$h*0.9,0 mat=stone
end
define islamic.tower r=2.5 h=26   # a minaret, slender within its footprint r: a shaft, two balconies, a lantern, a cap
  let r $r*0.45
  cyl $r*1.25 $h*0.08 sides=8 mat=stone
  cyl $r $h*0.62 at=0,$h*0.08,0 sides=16 mat=plaster
  cyl $r*1.5 0.3 at=0,$h*0.7,0 mat=stone
  cyl $r*0.8 $h*0.15 at=0,$h*0.7+0.3,0 sides=16 mat=plaster
  cyl $r*1.15 0.25 at=0,$h*0.85+0.3,0 mat=stone
  cyl $r*0.6 $h*0.07 at=0,$h*0.85+0.55,0 sides=12 mat=plaster
  cone $r*0.68 $h*0.08 at=0,$h*0.92+0.55,0 mat=tile
  sphere 0.12 at=0,$h+0.7,0 mat=gold
end
let islamic_floor tile
define islamic.wainscot len=10   # a dado of star-and-cross tile, a band over it
  box $len 1.4 0.03 at=0,0,0.015 mat=tile
  girih.starcross $len 1.3 0.42 0.02 at=0,0.05,0.03 mat=plaster
  box $len 0.12 0.05 at=0,1.4,0.025 mat=stone
end
define islamic.ipier h=5   # a slender colonnette
  cyl 0.09 $h at=0,0,0.09 mat=stone
end
define islamic.icornice len=10   # a muqarnas cornice: three tiers of niches stepping out to the ceiling
  girih.muqarnas $len 0.5 3 0.35 at=0,-0.5,0 mat=plaster
end
define islamic.ceiling w=10 d=10 h=5   # a dome over the middle of the room on pendentives, the square turned to a circle, a star at its crown; the ceiling flat round it
  let a min($w,$d)*0.8
  group
    box $w 0.3 $d mat=plaster
    sub box $a $a*0.9 $a at=0,-$a*0.7,0 res=max(0.1,$a/40)
  end
  structure.pendentives $a 0 0.3 max(0.1,$a/40) at=0,-$a*0.7071,0 mat=plaster
  structure.dome $a/2 0.25 at=0,-$a*0.7071+$a/2-0.01,0 sides=32 mat=tile
  girih.star 8 $a*0.12 0.03 at=0,-$a*0.7071+$a-0.1,0 rot=90,0,0 mat=gold
end
)LIB";
}

}  // namespace sg::sculpt
