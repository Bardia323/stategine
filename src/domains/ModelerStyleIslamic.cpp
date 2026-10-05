// The islamic style: pointed arches in panels, screens of lattice, a frieze of
// stepped merlons, flat roofs under a dome on its drum, minarets with
// balconies. (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_islamic() {
    return R"LIB(
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
define islamic.window w=1 h=2.5   # a framing panel (alfiz), a lattice screen
  group
    box $w+0.6 $h+0.4 0.08 at=0,-0.15,0.04 mat=stone
    sub islamic.arch $w $h 0.6
  end
  for i 5
    box 0.04 $h 0.04 at=-$w/2+$w*($i+1)/6,0,-0.25 mat=wood
  end
  for i floor($h/0.3)
    box $w 0.04 0.04 at=0,0.15+$i*0.3,-0.25 mat=wood
  end
end
define islamic.doorway w=2 h=3.4 d=2
  islamic.arch $w $h $d
end
define islamic.door w=2 h=3.4 t=0.6   # a tall portal panel round a pointed arch, and the leaves
  group
    box $w+1.4 $h+1.2 0.24 at=0,0,0.12 mat=stone
    sub islamic.arch $w $h 1
  end
  box $w+1.6 0.2 0.32 at=0,$h+1.2,0.16 mat=stone
  islamic.arch $w $h 0.1 at=0,0,-$t*0.5 mat=wood
end
define islamic.band len=10   # a band of tile
  box $len+0.04 0.3 0.05 at=0,-0.15,0.025 mat=tile
end
define islamic.pier h=7   # a slender engaged colonnette
  cyl 0.1 $h at=0,0,0.1 mat=stone
end
define islamic.base len=10 h=0.6
  box $len+0.1 $h 0.1 at=0,0,0.05 mat=stone
end
define islamic.cornice len=10   # a frieze, and stepped merlons
  box $len+0.2 0.4 0.12 at=0,0,0.06 mat=tile
  let n floor($len/0.8)
  array n=$n step=0.8,0,0 at=-($n-1)*0.4,0.4,0
    box 0.5 0.3 0.25
    box 0.3 0.25 0.25 at=0,0.3,0
    pyramid 0.32 0.2 at=0,0.55,0
  end
end
define islamic.roof w=12 d=9 h=5   # flat, and a dome on a drum
  let r min($w,$d)*0.32
  box $w-0.3 0.2 $d-0.3 mat=plaster
  cyl $r*1.05 $r*0.5 at=0,0.2,0 sides=16 mat=plaster
  lathe $r,0 $r*1.04,$r*0.3 $r*0.9,$r*0.75 $r*0.6,$r*1.05 $r*0.25,$r*1.25 0,$r*1.32 at=0,0.2+$r*0.5,0 mat=tile
  cyl 0.05 $r*0.5 at=0,0.2+$r*1.8,0 mat=gold
  sphere 0.14 at=0,0.2+$r*2.1,0 mat=gold
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
define islamic.wainscot len=10   # a dado of tile, a band over it
  box $len 1.4 0.03 at=0,0,0.015 mat=tile
  box $len 0.12 0.05 at=0,1.4,0.025 mat=stone
end
define islamic.ipier h=5   # a slender colonnette
  cyl 0.09 $h at=0,0,0.09 mat=stone
end
define islamic.icornice len=10   # a band of stalactite steps
  for i 3
    box $len 0.14 0.1+$i*0.1 at=0,-0.14-$i*0.14,0.05+$i*0.05 mat=stone
  end
end
define islamic.ceiling w=10 d=10 h=5   # flat round a dome over the middle, seen from under it
  let r min($w,$d)*0.4
  group
    box $w 0.3 $d mat=plaster
    sub cyl $r 1 at=0,-0.3,0 sides=24
  end
  group mat=tile
    sphere $r+0.25 sides=24
    sub sphere $r sides=24
    sub box $r*3 $r+1 $r*3 at=0,-$r-1,0
  end
end
)LIB";
}

}  // namespace sg::sculpt
