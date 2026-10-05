// The romanesque style: thick walls, round arches, pilaster strips, a blind
// arcade under the eaves, a low tiled roof, a square belfry; stout columns
// with cushion capitals. (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_romanesque() {
    return R"LIB(
let romanesque_ww 0.3
let romanesque_wh 0.5
let romanesque_sill 0.3
let romanesque_roof 0.3
define romanesque.wall len=10 h=7 t=0.9
  box $len $h $t at=0,0,-$t/2 mat=stone
end
define romanesque.opening w=1 h=2 d=2
  arch $w $h $d
end
define romanesque.window w=1 h=2   # a deep splay, a roll moulding round the head
  group
    arch $w+0.36 $h+0.18 0.14 at=0,-0.04,0.07 mat=stone
    sub arch $w+0.04 $h+0.02 0.6
  end
  arch $w*0.9 $h*0.95 0.03 at=0,0,-0.4 mat=glass
end
define romanesque.doorway w=1.8 h=3 d=2
  arch $w $h $d
end
define romanesque.door w=1.8 h=3 t=0.9   # stepped orders of round arches, and the leaves
  for i 3
    group
      arch $w+0.34+$i*0.34 $h+0.17+$i*0.17 0.18 at=0,0,0.09+(2-$i)*0.14 mat=stone
      sub arch $w+$i*0.34 $h+$i*0.17 1.2
    end
  end
  arch $w $h 0.1 at=0,0,-$t*0.5 mat=wood
end
define romanesque.band len=10
  box $len+0.1 0.12 0.1 at=0,-0.06,0.05 mat=stone
end
define romanesque.pier h=7   # a pilaster strip
  box 0.55 $h 0.14 at=0,0,0.07 mat=stone
end
define romanesque.base len=10 h=0.6
  box $len+0.2 $h 0.16 at=0,0,0.08 mat=stone
end
define romanesque.cornice len=10   # a blind arcade of little arches, and the eaves course
  let n max(2,floor($len/0.7))
  let s $len/$n
  group
    box $len 0.55 0.12 at=0,-0.55,0.06 mat=stone
    sub array n=$n step=$s,0,0 at=-$len/2+$s/2,-0.62,0
      arch $s*0.7 0.5 0.3
    end
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
define romanesque.ceiling w=10 d=14 h=5   # a round barrel vault along the longer side, transverse arches across it
  let a min($w,$d)/2
  let l max($w,$d)
  let n max(2,round($l/($a*1.4)))
  group rot=0,if($d>=$w,0,90),0
    group
      cyl $a+0.45 $l rot=90,0,0 centre=1 mat=stone
      sub cyl $a $l+1 rot=90,0,0 centre=1
      sub box $a*3 $a+1 $l+2 at=0,-$a-1,0
    end
    # the end walls closed up into the vault
    mirror z
      group
        cyl $a+0.1 0.3 rot=90,0,0 centre=1 at=0,0,$l/2+0.15 mat=stone
        sub box $a*3 $a+1 1 at=0,-$a-1,$l/2+0.15
      end
    end
    for i $n+1
      group
        cyl $a 0.5 rot=90,0,0 centre=1 at=0,0,-$l/2+$l/$n*$i mat=stone
        sub cyl $a-0.35 1 rot=90,0,0 centre=1 at=0,0,-$l/2+$l/$n*$i
        sub box $a*3 $a+1 2 at=0,-$a-1,-$l/2+$l/$n*$i
      end
    end
  end
end
)LIB";
}

}  // namespace sg::sculpt
