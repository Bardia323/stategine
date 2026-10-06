// The romanesque style: thick walls, round arches, pilaster strips, a blind
// arcade under the eaves, a low tiled roof, a square belfry; stout columns
// with cushion capitals. (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_romanesque() {
    return R"LIB(
use structure
use orders
let romanesque_ww 0.3
let romanesque_wh 0.5
let romanesque_sill 0.3
let romanesque_roof 0.3
let romanesque_head round
define romanesque.wall len=10 h=7 t=0.9
  box $len $h $t at=0,0,-$t/2 mat=stone
end
define romanesque.opening w=1 h=2 d=2
  arch $w $h $d
end
define romanesque.window w=1 h=2   # a round-headed light: a roll moulding round the head on nook shafts, a splayed sill, the glass deep in the wall
  order.archband $w/2+0.02 0.16 0.14 at=0,$h-$w/2-0.02,0.07 mat=stone
  cyl 0.07 $h-$w/2 at=-$w/2-0.09,0,0.07 sides=10 mat=stone
  cyl 0.07 $h-$w/2 at=$w/2+0.09,0,0.07 sides=10 mat=stone
  extrude $w+0.4 0,0 0.16,0 0,0.1 at=0,-0.1,0 rot=0,-90,0 mat=stone
  arch $w*0.9 $h*0.95 0.03 at=0,0,-0.4 mat=glass
  box 0.05 $h*0.9 0.05 at=0,0,-0.4 mat=stone
end
define romanesque.doorway w=1.8 h=3 d=2
  arch $w $h $d
end
define romanesque.door w=1.8 h=3 t=0.9   # stepped orders of round arches on shafts with cushion capitals, a tympanum over the lintel, the leaves with their ironwork
  for i 3
    let r ($w+$i*0.34)/2
    order.archband $r 0.17 0.18 at=0,$h+$i*0.17-$r,(2-$i)*0.14 mat=stone
    box 0.17 $h+$i*0.17-$r 0.18 at=-$r-0.085,0,(2-$i)*0.14+0.09 mat=stone
    box 0.17 $h+$i*0.17-$r 0.18 at=$r+0.085,0,(2-$i)*0.14+0.09 mat=stone
    cyl 0.07 $h+$i*0.17-$r-0.2 at=-$r-0.17,0,(2-$i)*0.14+0.18 sides=10 mat=stone
    cyl 0.07 $h+$i*0.17-$r-0.2 at=$r+0.17,0,(2-$i)*0.14+0.18 sides=10 mat=stone
    box 0.18 0.2 0.18 at=-$r-0.17,$h+$i*0.17-$r-0.2,(2-$i)*0.14+0.18 mat=stone
    box 0.18 0.2 0.18 at=$r+0.17,$h+$i*0.17-$r-0.2,(2-$i)*0.14+0.18 mat=stone
  end
  box $w+0.1 0.2 0.12 at=0,$h*0.72,-0.06 mat=stone
  arch $w $h 0.1 at=0,0,-$t*0.5 mat=wood
  for i 2
    box $w*0.4 0.05 0.03 at=-$w*0.25,$h*0.2+$i*$h*0.3,-$t*0.5+0.06 mat=metal
    box $w*0.4 0.05 0.03 at=$w*0.25,$h*0.2+$i*$h*0.3,-$t*0.5+0.06 mat=metal
  end
end
define romanesque.band len=10   # a string course of a roll under a fillet
  mould.torus $len+0.1 0.12 at=0,-0.16,0 mat=stone
  mould.fillet $len+0.1 0.05 0.08 at=0,-0.05,0 mat=stone
end
define romanesque.pier h=7   # a pilaster strip
  box 0.55 $h 0.14 at=0,0,0.07 mat=stone
end
define romanesque.base len=10 h=0.6
  box $len+0.2 $h 0.16 at=0,0,0.08 mat=stone
end
define romanesque.cornice len=10   # a corbel table: little arches between corbels under the eaves course
  let n max(2,floor($len/0.7))
  let s $len/$n
  array n=$n step=$s,0,0 at=-$len/2+$s/2,-0.6,0
    order.archband $s*0.3 0.1 0.14 at=0,0.1,0.07 mat=stone
    extrude 0.16 0,0 0.16,0.12 0.16,0.3 0,0.3 at=-$s/2,0,0 rot=0,-90,0 mat=stone
  end
  box $len+0.4 0.18 0.34 at=0,0,0.12 mat=stone
end
define romanesque.roof w=12 d=9 h=3   # a low gable of tiles along the longer side
  if $d>$w
    extrude $d+0.5 -$w/2-0.4,0 $w/2+0.4,0 0,$h at=0,0.18,0 mat=rooftiles
    mirror z
      extrude 0.9 -$w/2,0 $w/2,0 0,$h*0.95 at=0,0.18,$d/2-0.45 mat=stone
    end
  else
    extrude $w+0.5 -$d/2-0.4,0 $d/2+0.4,0 0,$h at=0,0.18,0 rot=0,90,0 mat=rooftiles
    mirror x
      extrude 0.9 -$d/2,0 $d/2,0 0,$h*0.95 at=$w/2-0.45,0.18,0 rot=0,90,0 mat=stone
    end
  end
end
define romanesque.column h=4 r=0.4   # a stout shaft, a cushion capital, a square abacus
  box $r*2.6 $h*0.06 $r*2.6 mat=stone
  cyl $r $h*0.82 at=0,$h*0.06,0 mat=stone
  cone $r*1.3 $r $h*0.09 at=0,$h*0.88,0 sides=4 rot=0,45,0 mat=stone
  box $r*2.4 $h*0.03 $r*2.4 at=0,$h*0.97,0 mat=stone
end
define romanesque.tower r=2.5 h=18   # a square belfry: stages marked by arcades, paired lights at the top, a low pyramid
  box $r*2 $h $r*2 mat=stone
  for i 3
    romanesque.cornice $r*2 at=0,$h*(0.4+$i*0.3),$r
    romanesque.cornice $r*2 at=0,$h*(0.4+$i*0.3),-$r rot=180
    romanesque.cornice $r*2 at=$r,$h*(0.4+$i*0.3),0 rot=90
    romanesque.cornice $r*2 at=-$r,$h*(0.4+$i*0.3),0 rot=-90
  end
  sub group
    for k 2
      arch $r*0.45 $h*0.13 $r*3 at=-$r*0.32+$k*$r*0.64,$h*0.8,0
      arch $r*0.45 $h*0.13 $r*3 at=0,$h*0.8,-$r*0.32+$k*$r*0.64 rot=90
    end
  end
  pyramid $r*2.5 $r*1.2 at=0,$h+0.18,0 mat=rooftiles
end
let romanesque_floor stone
define romanesque.wainscot len=10   # a stone bench along the wall
  box $len 0.45 0.4 at=0,0,0.2 mat=stone
end
define romanesque.ipier h=5   # a half-column carrying a transverse arch
  cyl 0.28 $h at=0,0,0.1 mat=stone
  box 0.7 0.3 0.5 at=0,$h-0.3,0.2 mat=stone
end
define romanesque.icornice len=10
  box $len 0.2 0.16 at=0,-0.2,0.08 mat=stone
end
define romanesque.ceiling w=10 d=14 h=5   # a round barrel vault along the longer side on transverse arches, all exact: the half rings of round arches
  let a min($w,$d)
  let l max($w,$d)
  let n max(2,round($l/($a*0.7)))
  group rot=0,if($d>=$w,0,90),0
    structure.barrel $a $l+0.3 $a/2 0.4 mat=stone
    pointed.band $a+0.2 $a/2+0.1 0 0.3 0.3 at=0,0,-$l/2-0.15 mat=stone
    pointed.band $a+0.2 $a/2+0.1 0 0.3 0.3 at=0,0,$l/2+0.15 mat=stone
    for i $n+1
      pointed.band $a-0.6 $a/2-0.3 0 0.35 0.5 at=0,0,-$l/2+$l/$n*$i mat=stone
    end
  end
end
)LIB";
}

}  // namespace sg::sculpt
