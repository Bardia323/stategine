// The modern style: white planes, slab edges at every floor, deep-set glass
// in thin frames, fins between bays, a recessed dark plinth, a flat roof with a
// parapet; pilotis for columns. (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_modern() {
    return R"LIB(
let modern_ww 0.86
let modern_wh 0.7
let modern_sill 0.14
let modern_roof 0.08
let modern_head square
define modern.wall len=10 h=7 t=0.4
  box $len $h $t at=0,0,-$t/2 mat=plaster
end
define modern.opening w=2 h=2.4 d=2
  box $w $h $d
end
define modern.window w=2 h=2.4   # glass deep in the wall, a thin frame, a mullion
  box $w $h 0.03 at=0,0,-0.26 mat=glass
  box $w 0.05 0.08 at=0,0,-0.24 mat=metal
  box $w 0.05 0.08 at=0,$h-0.05,-0.24 mat=metal
  box 0.05 $h 0.08 at=-$w/2+0.025,0,-0.24 mat=metal
  box 0.05 $h 0.08 at=$w/2-0.025,0,-0.24 mat=metal
  box 0.04 $h 0.08 at=0,0,-0.24 mat=metal
end
define modern.doorway w=1.8 h=2.6 d=2
  box $w $h $d
end
define modern.door w=1.8 h=2.6 t=0.4   # glass doors under a cantilevered canopy
  box $w $h 0.03 at=0,0,-$t*0.6 mat=glass
  box 0.05 $h 0.1 at=0,0,-$t*0.6 mat=metal
  box $w+1.6 0.14 1.6 at=0,$h+0.2,0.8 mat=concrete
end
define modern.band len=10   # the slab's edge
  box $len+0.02 0.24 0.12 at=0,-0.12,0.06 mat=concrete
end
define modern.pier h=7   # a thin fin
  box 0.08 $h 0.36 at=0,0,0.18 mat=metal
end
define modern.base len=10 h=0.6   # a dark plinth, set back
  box $len $h 0.02 at=0,0,-0.1 mat=concrete
end
define modern.cornice len=10   # a parapet, flush
  box $len+0.02 0.6 0.14 at=0,0,0.07 mat=concrete
end
define modern.roof w=12 d=9 h=0.6   # flat: a slab, a plant room set back
  box $w-0.2 0.2 $d-0.2 mat=concrete
  box $w*0.3 $h*3 $d*0.35 at=$w*0.15,0.2,-$d*0.1 mat=plaster
end
define modern.column h=5 r=0.3   # a piloti
  cyl $r $h mat=concrete
end
define modern.tower r=3 h=30   # a slab tower: floors of glass between slab edges, a crown
  let n floor($h/3.4)
  box $r*2 $h $r*2 mat=glass
  for i $n+1
    box $r*2+0.1 0.3 $r*2+0.1 at=0,$i*$h/$n-0.15,0 mat=concrete
  end
  box $r*1.2 $h*0.06 $r*1.2 at=0,$h,0 mat=concrete
end
let modern_floor concrete
define modern.wainscot len=10   # a skirting, flush, and a shadow gap over it
  box $len 0.1 0.02 at=0,0,0.01 mat=metal
end
define modern.ipier h=5   # nothing between bays: a modern wall is plain
  box 0.02 0.02 0.02 at=0,0,0.01 mat=plaster
end
define modern.icornice len=10   # a shadow gap at the ceiling
  box $len 0.04 0.02 at=0,-0.04,0.01 mat=metal
end
define modern.ceiling w=10 d=10 h=5   # a flat soffit, strips of light in it
  box $w 0.2 $d mat=plaster
  let n max(2,floor($w/2.4))
  for i $n
    box 0.12 0.03 $d*0.8 at=-$w/2+$w/$n*($i+0.5),-0.03,0 mat=metal
  end
end
)LIB";
}

}  // namespace sg::sculpt
