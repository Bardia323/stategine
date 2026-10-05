// The classical style: a palazzo's language - a rusticated base, pilasters,
// windows under pediments, string courses, a deep cornice, a low hipped roof
// of tiles; columns with entasis, capital and base. (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_classical() {
    return R"LIB(
let classical_ww 0.42
let classical_wh 0.52
let classical_sill 0.26
let classical_roof 0.22
define classical.wall len=10 h=7 t=0.5
  box $len $h $t at=0,0,-$t/2 mat=plaster
end
define classical.opening w=1 h=1.8 d=2
  box $w $h $d
end
define classical.window w=1 h=1.8   # an architrave round it, a sill, and a pediment over it
  box $w+0.3 0.1 0.22 at=0,-0.1,0.11 mat=stone
  box 0.14 $h 0.08 at=-$w/2-0.07,0,0.04 mat=stone
  box 0.14 $h 0.08 at=$w/2+0.07,0,0.04 mat=stone
  box $w+0.42 0.16 0.12 at=0,$h,0.06 mat=stone
  box $w+0.6 0.08 0.2 at=0,$h+0.16,0.1 mat=stone
  extrude 0.16 -$w/2-0.32,0 $w/2+0.32,0 0,$w*0.24 at=0,$h+0.24,0.08 mat=stone
  box $w 0.04 0.04 at=0,$h*0.62,-0.2 mat=wood
  box 0.04 $h 0.04 at=0,0,-0.2 mat=wood
end
define classical.doorway w=1.6 h=3 d=2
  box $w $h $d
end
define classical.door w=1.6 h=3 t=0.5   # pilasters, an entablature, a pediment, and the leaves
  box $w $h 0.08 at=0,0,-$t*0.55 mat=wood
  box 0.04 $h 0.1 at=0,0,-$t*0.5 mat=wood
  box 0.3 $h 0.18 at=-$w/2-0.2,0,0.09 mat=stone
  box 0.3 $h 0.18 at=$w/2+0.2,0,0.09 mat=stone
  box $w+0.9 0.35 0.26 at=0,$h,0.13 mat=stone
  extrude 0.24 -$w/2-0.5,0 $w/2+0.5,0 0,$w*0.3 at=0,$h+0.35,0.12 mat=stone
end
define classical.band len=10
  box $len+0.2 0.16 0.14 at=0,-0.08,0.07 mat=stone
end
define classical.pier h=7   # a pilaster: base, shaft, capital
  box 0.5 0.3 0.16 at=0,0,0.08 mat=stone
  box 0.38 $h-0.55 0.1 at=0,0.3,0.05 mat=stone
  box 0.52 0.25 0.18 at=0,$h-0.25,0.09 mat=stone
end
define classical.base len=10 h=0.6   # rusticated: blocks in courses
  box $len+0.12 $h 0.12 at=0,0,0.06 mat=stone
  let n max(1,floor($h/0.3))
  for i $n
    box $len+0.16 0.05 0.14 at=0,$h*($i+1)/$n-0.03,0.07 mat=stone
  end
end
define classical.cornice len=10   # architrave, frieze, dentils, corona
  box $len+0.3 0.22 0.18 at=0,0,0.09 mat=stone
  box $len+0.3 0.3 0.1 at=0,0.22,0.05 mat=plaster
  let n floor(($len+0.3)/0.3)
  array n=$n step=0.3,0,0 at=-($n-1)*0.15,0.52,0.2
    box 0.14 0.14 0.14 mat=stone
  end
  box $len+0.7 0.12 0.4 at=0,0.52,0.13 mat=stone
  box $len+1 0.22 0.62 at=0,0.66,0.24 mat=stone
  box $len+1.1 0.1 0.7 at=0,0.88,0.28 mat=stone
end
define classical.roof w=12 d=9 h=2   # hipped, of tiles, its eaves over the cornice
  let ww $w+1.4
  let dd $d+1.4
  let r max(0.05,abs($ww-$dd))
  if $ww>=$dd
    loft $h -$ww/2,-$dd/2 $ww/2,-$dd/2 $ww/2,$dd/2 -$ww/2,$dd/2 / -$r/2,-0.02 $r/2,-0.02 $r/2,0.02 -$r/2,0.02 at=0,0.98,0 mat=rooftiles
  else
    loft $h -$ww/2,-$dd/2 $ww/2,-$dd/2 $ww/2,$dd/2 -$ww/2,$dd/2 / -0.02,-$r/2 0.02,-$r/2 0.02,$r/2 -0.02,$r/2 at=0,0.98,0 mat=rooftiles
  end
end
define classical.column h=5 r=0.35   # base, a shaft that swells and narrows, a capital
  cyl $r*1.45 $h*0.04
  torus $r*1.18 $r*0.18 at=0,$h*0.06,0
  lathe $r*1.08,0 $r*1.1,$h*0.25 $r*1.02,$h*0.6 $r*0.88,$h*0.88 0,$h*0.88 at=0,$h*0.06,0
  torus $r*0.95 $r*0.12 at=0,$h*0.92,0
  lathe $r*0.92,0 $r*1.25,$h*0.05 0,$h*0.05 at=0,$h*0.93,0
  box $r*2.8 $h*0.03 $r*2.8 at=0,$h*0.97,0
end
define classical.tower r=2.5 h=14   # a campanile: a square shaft, a belfry of arches, a pyramid roof
  box $r*2 $h*0.7 $r*2 mat=plaster
  classical.band $r*2 at=0,$h*0.35,$r
  box $r*2.2 $h*0.04 $r*2.2 at=0,$h*0.7,0 mat=stone
  box $r*2 $h*0.18 $r*2 at=0,$h*0.74,0 mat=plaster
  sub group
    arch $r*0.9 $h*0.14 $r*3 at=0,$h*0.75,0
    arch $r*0.9 $h*0.14 $r*3 at=0,$h*0.75,0 rot=90
  end
  box $r*2.3 $h*0.03 $r*2.3 at=0,$h*0.92,0 mat=stone
  pyramid $r*2.2 $h*0.12 at=0,$h*0.95,0 mat=rooftiles
end
let classical_floor stone
define classical.wainscot len=10   # panelling to the dado, a rail on it
  box $len 0.9 0.05 at=0,0,0.025 mat=wood
  box $len 0.06 0.08 at=0,0.9,0.04 mat=wood
  box $len 0.14 0.07 at=0,0,0.035 mat=wood
end
define classical.ipier h=5   # a pilaster on the inside
  box 0.4 $h 0.1 at=0,0,0.05 mat=plaster
  box 0.55 0.3 0.16 at=0,$h-0.3,0.08 mat=plaster
  box 0.55 0.25 0.14 at=0,0,0.07 mat=plaster
end
define classical.icornice len=10   # a crown moulding, stepped
  box $len 0.25 0.2 at=0,-0.25,0.1 mat=plaster
  box $len 0.12 0.32 at=0,-0.37,0.16 mat=plaster
end
define classical.ceiling w=10 d=10 h=6   # coffered: a flat soffit, a grid of beams under it
  box $w 0.2 $d mat=plaster
  let nx max(2,round($w/1.6))
  let nz max(2,round($d/1.6))
  for i $nx+1
    box 0.16 0.24 $d at=-$w/2+$w/$nx*$i,-0.24,0 mat=plaster
  end
  for j $nz+1
    box $w 0.24 0.16 at=0,-0.24,-$d/2+$d/$nz*$j mat=plaster
  end
end
)LIB";
}

}  // namespace sg::sculpt
