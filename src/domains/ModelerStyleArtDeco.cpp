// The art deco style: vertical piers that run up past the roof, recessed
// window strips with dark spandrels, a stepped crown, setbacks and a spire.
// (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_artdeco() {
    return R"LIB(
let artdeco_ww 0.62
let artdeco_wh 0.68
let artdeco_sill 0.16
let artdeco_roof 0.5
let artdeco_head square
define artdeco.wall len=10 h=7 t=0.45
  box $len $h $t at=0,0,-$t/2 mat=stone
end
define artdeco.opening w=1.4 h=2.3 d=2
  box $w $h $d
end
define artdeco.window w=1.4 h=2.3   # a glazed strip, a dark spandrel under it, a chevron over it
  box $w $h 0.03 at=0,0,-0.2 mat=glass
  box 0.05 $h 0.08 at=0,0,-0.18 mat=metal
  box $w 0.5 0.06 at=0,-0.55,0.02 mat=metal
  extrude 0.06 -$w*0.3,0 $w*0.3,0 0,$w*0.14 at=0,$h+0.08,0.03 mat=metal
end
define artdeco.doorway w=2.4 h=3.4 d=2
  box $w $h $d
end
define artdeco.door w=2.4 h=3.4 t=0.45   # a stepped portal, a sunburst over it
  for i 3
    box $w+0.6+$i*0.5 0.3 0.2+$i*0.12 at=0,$h+$i*0.3,0.1+$i*0.06 mat=stone
    box 0.3 $h+$i*0.3 0.2+$i*0.12 at=-$w/2-0.15-$i*0.25,0,0.1+$i*0.06 mat=stone
    box 0.3 $h+$i*0.3 0.2+$i*0.12 at=$w/2+0.15+$i*0.25,0,0.1+$i*0.06 mat=stone
  end
  for i 7
    box 0.06 $w*0.42 0.04 at=0,$h,0.12 rot=0,0,-72+$i*24 mat=gold
  end
  box $w $h 0.04 at=0,0,-$t*0.6 mat=metal
end
define artdeco.band len=10
  box $len 0.1 0.04 at=0,-0.05,0.02 mat=stone
end
define artdeco.pier h=7   # a rib that runs up past the crown
  box 0.4 $h+1.4 0.3 at=0,0,0.15 mat=stone
  box 0.24 0.6 0.2 at=0,$h+1.4,0.1 mat=stone
end
define artdeco.base len=10 h=0.6
  box $len+0.3 $h 0.2 at=0,0,0.1 mat=stone
end
define artdeco.cornice len=10   # a stepped crown
  for i 3
    box $len+0.4-$i*1.2 0.45 0.25 at=0,$i*0.45,0.12-$i*0.06 mat=stone
  end
end
define artdeco.roof w=12 d=9 h=4   # setbacks, each smaller, and a spire
  for i 3
    box $w*(0.8-$i*0.2) $h*0.25 $d*(0.8-$i*0.2) at=0,1.35+$i*$h*0.25,0 mat=stone
  end
  cone min($w,$d)*0.1 $h*1.5 sides=8 at=0,1.35+$h*0.75,0 mat=metal
end
define artdeco.column h=5 r=0.35   # a square shaft, a stepped head
  box $r*2 $h*0.88 $r*2 mat=stone
  for i 3
    box $r*2.2+$i*0.15 $h*0.04 $r*2.2+$i*0.15 at=0,$h*(0.88+$i*0.04),0 mat=stone
  end
end
define artdeco.tower r=4 h=50   # a skyscraper in setbacks, its piers running up, a spire
  for i 4
    let k 1-$i*0.2
    box $r*2*$k $h*0.22 $r*2*$k at=0,$i*$h*0.22,0 mat=stone
    radial n=4
      for j 3
        box 0.3 $h*0.22 0.3 at=$r*$k,$i*$h*0.22,-$r*$k*0.5+$j*$r*$k*0.5 mat=stone
      end
    end
  end
  cone $r*0.35 $h*0.25 sides=8 at=0,$h*0.88,0 mat=metal
end
let artdeco_floor stone
define artdeco.wainscot len=10   # stone panels to the dado, a band of metal over them
  box $len 1.1 0.04 at=0,0,0.02 mat=stone
  box $len 0.05 0.06 at=0,1.1,0.03 mat=gold
end
define artdeco.ipier h=5   # a fluted pilaster: three ribs
  for i 3
    box 0.08 $h 0.1 at=-0.14+$i*0.14,0,0.05 mat=stone
  end
end
define artdeco.icornice len=10   # a stepped cove
  for i 3
    box $len 0.12 0.1+$i*0.1 at=0,-0.12-$i*0.12,0.05+$i*0.05 mat=plaster
  end
end
define artdeco.ceiling w=10 d=10 h=5   # stepped up in frames to a lit centre
  box $w 0.2 $d at=0,0.6,0 mat=plaster
  for i 3
    group
      box $w-$i*1.2 0.2 $d-$i*1.2 at=0,$i*0.2,0 mat=plaster
      sub box $w-$i*1.2-1.2 1 $d-$i*1.2-1.2 at=0,-0.2,0
    end
  end
  cyl min($w,$d)*0.12 0.05 at=0,0.55,0 mat=gold
end
)LIB";
}

}  // namespace sg::sculpt
