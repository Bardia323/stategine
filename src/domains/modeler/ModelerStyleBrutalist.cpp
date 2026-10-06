// The brutalist style: mass of concrete, windows deep in projecting frames,
// heavy slabs, piers like walls, a crown that overhangs. (The words:
// ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_brutalist() {
    return R"LIB(
let brutalist_ww 0.62
let brutalist_wh 0.5
let brutalist_sill 0.28
let brutalist_roof 0.1
let brutalist_head square
define brutalist.wall len=10 h=7 t=0.6
  box $len $h $t at=0,0,-$t/2 mat=concrete
end
define brutalist.opening w=1.6 h=1.6 d=3
  box $w $h $d
end
define brutalist.window w=1.6 h=1.6   # a deep concrete hood round it, the glass far back
  group
    box $w+0.5 $h+0.5 0.9 at=0,-0.25,0.45 mat=concrete
    sub box $w $h 3
  end
  box $w $h 0.03 at=0,0,-0.4 mat=glass
end
define brutalist.doorway w=2.4 h=2.8 d=3
  box $w $h $d
end
define brutalist.door w=2.4 h=2.8 t=0.6   # a slab porch on two blades
  box $w+3 0.5 2.4 at=0,$h,1.2 mat=concrete
  box 0.4 $h 2.4 at=-$w/2-1.2,0,1.2 mat=concrete
  box 0.4 $h 2.4 at=$w/2+1.2,0,1.2 mat=concrete
  box $w $h 0.04 at=0,0,-$t*0.6 mat=glass
end
define brutalist.band len=10   # a floor slab, thick
  box $len+0.3 0.45 0.35 at=0,-0.3,0.17 mat=concrete
end
define brutalist.pier h=7   # a wall-like pier
  box 0.5 $h 0.7 at=0,0,0.35 mat=concrete
end
define brutalist.base len=10 h=0.6   # a massive plinth
  box $len+0.6 $h 0.6 at=0,0,0.3 mat=concrete
end
define brutalist.cornice len=10   # an overhanging crown
  box $len+1.6 1.2 1.4 at=0,0,0.4 mat=concrete
end
define brutalist.roof w=12 d=9 h=1   # flat, with a stair tower and a tank
  box $w 0.3 $d at=0,1.2,0 mat=concrete
  box $w*0.22 $h*4 $d*0.3 at=-$w*0.3,1.5,$d*0.1 mat=concrete
  cyl $d*0.12 $h*2.5 at=$w*0.3,1.5,-$d*0.15 mat=concrete
end
define brutalist.column h=5 r=0.5   # a square pier that splays at its head
  box $r*1.6 $h*0.8 $r*1.6 mat=concrete
  loft $h*0.2 -$r*0.8,-$r*0.8 $r*0.8,-$r*0.8 $r*0.8,$r*0.8 -$r*0.8,$r*0.8 / -$r*1.6,-$r*1.6 $r*1.6,-$r*1.6 $r*1.6,$r*1.6 -$r*1.6,$r*1.6 at=0,$h*0.8,0 mat=concrete
end
define brutalist.tower r=4 h=30   # stacked blocks, each turned from the last, on a core
  box $r*0.9 $h $r*0.9 mat=concrete
  for i 5
    box $r*2*(1-0.08*mod($i,2)) $h*0.14 $r*1.4 at=$r*0.2*(mod($i,2)*2-1),$h*0.08+$i*$h*0.18,0 rot=$i*15 mat=concrete
  end
end
let brutalist_floor concrete
define brutalist.wainscot len=10   # a plinth of the wall's own concrete
  box $len 0.3 0.08 at=0,0,0.04 mat=concrete
end
define brutalist.ipier h=5   # a heavy pilaster
  box 0.6 $h 0.3 at=0,0,0.15 mat=concrete
end
define brutalist.icornice len=10   # a deep downstand beam
  box $len 0.6 0.4 at=0,-0.6,0.2 mat=concrete
end
define brutalist.ceiling w=10 d=10 h=5   # a waffle slab: deep ribs both ways
  box $w 0.3 $d mat=concrete
  let nx max(2,round($w/1.5))
  let nz max(2,round($d/1.5))
  for i $nx+1
    box 0.2 0.5 $d at=-$w/2+$w/$nx*$i,-0.5,0 mat=concrete
  end
  for j $nz+1
    box $w 0.5 0.2 at=0,-0.5,-$d/2+$d/$nz*$j mat=concrete
  end
end
)LIB";
}

}  // namespace sg::sculpt
