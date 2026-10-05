// The japanese style: a raised stone base, dark posts and rails framing white
// panels, lattice screens, deep eaves on brackets, a curved hipped roof of
// tiles; a pagoda for a tower. (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_japanese() {
    return R"LIB(
let japanese_ww 0.7
let japanese_wh 0.62
let japanese_sill 0.22
let japanese_roof 0.42
let japanese_head square
define japanese.eaves w=10 d=8 h=3   # a hipped roof whose slopes sweep up at the eaves, its ridge along the longer side
  let a max(0.05,abs($w-$d)*0.5)
  if $w>=$d
    loft $h -$w/2,-$d/2 $w/2,-$d/2 $w/2,$d/2 -$w/2,$d/2 / -$w*0.4,-$d*0.3 $w*0.4,-$d*0.3 $w*0.4,$d*0.3 -$w*0.4,$d*0.3 / -$a*0.9-$d*0.1,-$d*0.12 $a*0.9+$d*0.1,-$d*0.12 $a*0.9+$d*0.1,$d*0.12 -$a*0.9-$d*0.1,$d*0.12 / -$a,-0.03 $a,-0.03 $a,0.03 -$a,0.03 mat=rooftiles
  else
    loft $h -$w/2,-$d/2 $w/2,-$d/2 $w/2,$d/2 -$w/2,$d/2 / -$w*0.3,-$d*0.4 $w*0.3,-$d*0.4 $w*0.3,$d*0.4 -$w*0.3,$d*0.4 / -$w*0.12,-$a*0.9-$w*0.1 $w*0.12,-$a*0.9-$w*0.1 $w*0.12,$a*0.9+$w*0.1 -$w*0.12,$a*0.9+$w*0.1 / -0.03,-$a 0.03,-$a 0.03,$a -0.03,$a mat=rooftiles
  end
end
define japanese.wall len=10 h=7 t=0.25
  box $len $h $t at=0,0,-$t/2 mat=plaster
end
define japanese.opening w=1.6 h=1.8 d=2
  box $w $h $d
end
define japanese.window w=1.6 h=1.8   # a shoji: paper in a frame, and its lattice
  box $w $h 0.02 at=0,0,-0.12 mat=paper
  for i 4
    box 0.03 $h 0.05 at=-$w/2+$w*($i+1)/5,0,-0.1 mat=wood
  end
  for i 5
    box $w 0.03 0.05 at=0,$h*($i+1)/6,-0.1 mat=wood
  end
  box $w+0.1 0.08 0.1 at=0,-0.08,0.02 mat=wood
  box $w+0.1 0.08 0.1 at=0,$h,0.02 mat=wood
end
define japanese.doorway w=1.8 h=2.2 d=2
  box $w $h $d
end
define japanese.door w=1.8 h=2.2 t=0.25   # sliding panels, and a step
  box $w/2 $h 0.04 at=-$w/4,0,-0.06 mat=wood
  box $w/2 $h 0.04 at=$w/4,0,-0.1 mat=wood
  box $w+0.6 0.15 0.6 at=0,-0.15,0.3 mat=stone
end
define japanese.band len=10   # a rail
  box $len 0.14 0.1 at=0,-0.07,0.05 mat=wood
end
define japanese.pier h=7   # a post
  box 0.22 $h 0.22 at=0,0,0.02 mat=wood
end
define japanese.base len=10 h=0.6   # a stone platform, a veranda edge
  box $len+0.9 $h 0.9 at=0,0,0.2 mat=stone
end
define japanese.cornice len=10   # a rail, and brackets under the eaves
  box $len+0.2 0.2 0.16 at=0,-0.2,0.08 mat=wood
  let n floor($len/1.2)
  array n=$n step=1.2,0,0 at=-($n-1)*0.6,-0.1,0.3
    box 0.16 0.16 0.6 mat=wood
  end
end
define japanese.roof w=12 d=9 h=4   # deep eaves, swept up, and a ridge
  japanese.eaves $w+2.4 $d+2.4 $h
  box max($w,$d)*0.5 0.25 0.3 at=0,$h-0.05,0 rot=if($w>=$d,0,90) mat=rooftiles
end
define japanese.column h=3 r=0.15   # a round post on a stone
  cyl $r*1.8 0.2 mat=stone
  cyl $r $h at=0,0.2,0 mat=wood
end
define japanese.tower r=3 h=18   # a pagoda: five storeys, each smaller, each under its eaves, a finial
  for i 5
    let k 1-$i*0.12
    let y $i*$h*0.15
    box $r*1.6*$k $h*0.1 $r*1.6*$k at=0,$y,0 mat=plaster
    japanese.eaves $r*2.8*$k $r*2.8*$k $h*0.05 at=0,$y+$h*0.1,0
  end
  cyl $r*0.08 $h*0.22 at=0,$h*0.8,0 mat=metal
  for j 6
    torus $r*0.16 $r*0.03 at=0,$h*0.82+$j*$h*0.025,0 mat=metal
  end
end
let japanese_floor wood
define japanese.wainscot len=10   # a low board, and a rail at the height of the sliding screens
  box $len 0.3 0.03 at=0,0,0.015 mat=wood
  box $len 0.08 0.06 at=0,1.9,0.03 mat=wood
end
define japanese.ipier h=3   # a post
  box 0.2 $h 0.2 at=0,0,0.1 mat=wood
end
define japanese.icornice len=10   # a rail under the boards
  box $len 0.1 0.08 at=0,-0.1,0.04 mat=wood
end
define japanese.ceiling w=10 d=10 h=3   # boards, and the beams that carry them, open
  box $w 0.06 $d mat=wood
  let n max(2,round($w/1.8))
  for i $n+1
    box 0.18 0.28 $d at=-$w/2+$w/$n*$i,-0.28,0 mat=wood
  end
  box $w 0.3 0.24 at=0,-0.58,0 mat=wood
end
)LIB";
}

}  // namespace sg::sculpt
