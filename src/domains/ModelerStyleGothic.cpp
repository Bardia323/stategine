// The gothic style: pointed arches, tracery, buttresses with pinnacles,
// archivolts round the door, a crenellated parapet, a steep slate roof, spires.
// (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_gothic() {
    return R"LIB(
let gothic_ww 0.5
let gothic_wh 0.66
let gothic_sill 0.16
let gothic_roof 0.75
define gothic.arch w=1 h=2.5 d=1   # a pointed (equilateral) arch, solid: its foot on y = 0, through z
  let s $h-$w*0.866
  extrude $d -$w/2,0 $w/2,0 $w/2,$s -$w/2+$w*cos(20),$s+$w*sin(20) -$w/2+$w*cos(40),$s+$w*sin(40) 0,$s+$w*0.866 $w/2-$w*cos(40),$s+$w*sin(40) $w/2-$w*cos(20),$s+$w*sin(20) -$w/2,$s
end
define gothic.wall len=10 h=7 t=0.6
  box $len $h $t at=0,0,-$t/2 mat=stone
end
define gothic.opening w=1 h=2.5 d=2
  gothic.arch $w $h $d
end
define gothic.window w=1 h=2.5   # a hood mould, a mullion, and a foiled light in the head
  group
    gothic.arch $w+0.3 $h+0.18 0.12 at=0,-0.06,0.06 mat=stone
    sub gothic.arch $w $h 0.6
  end
  box $w+0.36 0.1 0.24 at=0,-0.1,0.1 mat=stone
  box 0.08 $h-$w*0.95 0.12 at=0,0,-0.15 mat=stone
  torus $w*0.19 0.035 at=0,$h-$w*0.62,-0.15 rot=90,0,0 mat=stone
  gothic.arch $w*0.98 $h*0.98 0.03 at=0,0,-0.22 mat=glass
end
define gothic.doorway w=1.8 h=3.2 d=2
  gothic.arch $w $h $d
end
define gothic.door w=1.8 h=3.2 t=0.6   # receding orders of arches round it, and the leaves
  for i 3
    group
      gothic.arch $w+0.3+$i*0.3 $h+0.2+$i*0.22 0.16 at=0,0,0.08+(2-$i)*0.12 mat=stone
      sub gothic.arch $w+$i*0.3 $h+$i*0.22 1.2
    end
  end
  gothic.arch $w $h 0.1 at=0,0,-$t*0.55 mat=wood
end
define gothic.band len=10
  box $len+0.1 0.14 0.12 at=0,-0.07,0.06 mat=stone
end
define gothic.pier h=7   # a buttress in two stages, weathered, with a pinnacle
  box 0.55 $h*0.62 0.9 at=0,0,0.45 mat=stone
  extrude 0.55 0,0 0.9,0 0,0.5 at=0,$h*0.62,0 rot=0,-90,0 mat=stone
  box 0.45 $h*0.3 0.55 at=0,$h*0.62,0.28 mat=stone
  box 0.3 0.6 0.3 at=0,$h*0.92,0.28 mat=stone
  cone 0.24 1.1 sides=4 rot=0,45,0 at=0,$h*0.92+0.6,0.28 mat=stone
end
define gothic.base len=10 h=0.6
  box $len+0.24 $h 0.14 at=0,0,0.07 mat=stone
  extrude $len+0.24 0,0 0.14,0 0,0.14 at=0,$h,0 rot=0,90,0 mat=stone
end
define gothic.cornice len=10   # a corbel table and a crenellated parapet
  let n floor($len/0.6)
  array n=$n step=0.6,0,0 at=-($n-1)*0.3,-0.3,0.12
    box 0.18 0.3 0.24 mat=stone
  end
  box $len+0.4 0.16 0.34 at=0,0,0.1 mat=stone
  battlement len=$len+0.4 t=0.3 h=0.9 w=0.6 gap=0.42 at=0,0.16,0.12 mat=stone
end
define gothic.roof w=12 d=9 h=6   # steep, of slate, its ridge along the longer side, its gables walls of stone with a cross
  if $d>$w
    extrude $d+0.2 -$w/2-0.35,0 $w/2+0.35,0 0,$h mat=slate
    mirror z
      extrude 0.45 -$w/2,0 $w/2,0 0,$h*0.96 at=0,0,$d/2-0.22 mat=stone
      box 0.16 1.4 0.16 at=0,$h,$d/2-0.22 mat=stone
      box 0.8 0.16 0.16 at=0,$h+0.9,$d/2-0.22 mat=stone
    end
  else
    extrude $w+0.2 -$d/2-0.35,0 $d/2+0.35,0 0,$h rot=0,90,0 mat=slate
    mirror x
      extrude 0.45 -$d/2,0 $d/2,0 0,$h*0.96 at=$w/2-0.22,0,0 rot=0,90,0 mat=stone
    end
  end
end
define gothic.column h=6 r=0.4   # a clustered pier: four shafts round a core, a moulded capital and base
  cyl $r*1.3 $h*0.06
  cyl $r $h at=0,0,0
  radial n=4
    cyl $r*0.38 $h*0.9 at=$r,$h*0.05,0
  end
  cyl $r*1.25 $h*0.05 at=0,$h*0.92,0
end
define gothic.tower r=2.5 h=24   # a square tower, buttressed at its corners, belfry lights, a spire
  box $r*2 $h*0.55 $r*2 mat=stone
  radial n=4
    box 0.7 $h*0.5 0.7 at=$r,0,$r mat=stone
    cone 0.3 1.6 sides=4 rot=0,45,0 at=$r,$h*0.55,$r mat=stone
  end
  sub radial n=4
    gothic.arch $r*0.6 $h*0.12 $r*3 at=0,$h*0.4,0
  end
  battlement len=$r*2+0.2 t=0.3 h=0.8 w=0.5 gap=0.4 at=0,$h*0.55,$r-0.1 mat=stone
  battlement len=$r*2+0.2 t=0.3 h=0.8 w=0.5 gap=0.4 at=0,$h*0.55,-$r+0.1 mat=stone
  cone $r*1.15 $h*0.45 sides=8 rot=0,22.5,0 at=0,$h*0.55,0 mat=slate
end
let gothic_floor stone
define gothic.wainscot len=10   # a blind arcade low along the wall
  let n max(2,floor($len/1.2))
  array n=$n step=$len/$n,0,0 at=-$len/2+$len/$n/2,0,0.05
    group
      gothic.arch $len/$n*0.9 1.7 0.1 mat=stone
      sub gothic.arch $len/$n*0.7 1.5 1
    end
  end
end
define gothic.ipier h=6   # a shaft running up to the springing of the vault
  cyl 0.14 $h at=0,0,0.14 mat=stone
  cyl 0.22 0.25 at=0,$h-0.25,0.14 mat=stone
end
define gothic.icornice len=10
  box $len 0.14 0.12 at=0,-0.14,0.06 mat=stone
end
define gothic.archband w=4 h=4 t=0.4 d=1   # the band between a pointed arch and one t outside it, exact (no cut, so no field): a vault's shell, a rib; foot on y = 0, through z
  let s $h-$w*0.866
  let R $w+$t
  extrude $d -$w/2-$t,0 -$w/2,0 -$w/2,$s $w/2-$w*cos(10),$s+$w*sin(10) $w/2-$w*cos(20),$s+$w*sin(20) $w/2-$w*cos(30),$s+$w*sin(30) $w/2-$w*cos(40),$s+$w*sin(40) $w/2-$w*cos(50),$s+$w*sin(50) 0,$s+$w*0.866 -$w/2+$w*cos(50),$s+$w*sin(50) -$w/2+$w*cos(40),$s+$w*sin(40) -$w/2+$w*cos(30),$s+$w*sin(30) -$w/2+$w*cos(20),$s+$w*sin(20) -$w/2+$w*cos(10),$s+$w*sin(10) $w/2,$s $w/2,0 $w/2+$t,0 $w/2+$t,$s -$w/2+$R*cos(10),$s+$R*sin(10) -$w/2+$R*cos(20),$s+$R*sin(20) -$w/2+$R*cos(30),$s+$R*sin(30) -$w/2+$R*cos(40),$s+$R*sin(40) -$w/2+$R*cos(50),$s+$R*sin(50) 0,$s+sqrt($R*$R-$w*$w/4) $w/2-$R*cos(50),$s+$R*sin(50) $w/2-$R*cos(40),$s+$R*sin(40) $w/2-$R*cos(30),$s+$R*sin(30) $w/2-$R*cos(20),$s+$R*sin(20) $w/2-$R*cos(10),$s+$R*sin(10) -$w/2-$t,$s
end
define gothic.ceiling w=10 d=14 h=6   # a pointed vault along the longer side: its shell, a rib across it at every bay, the ridge rib, a moulding where it springs - all exact
  let a min($w,$d)
  let l max($w,$d)
  let n max(2,round($l/($a*0.6)))
  group rot=0,if($d>=$w,0,90),0
    gothic.archband $a $a*0.866 0.4 $l mat=stone
    # the end walls closed up into the vault
    gothic.arch $a+0.2 $a*0.866+0.1 0.3 at=0,0,-$l/2-0.15 mat=stone
    gothic.arch $a+0.2 $a*0.866+0.1 0.3 at=0,0,$l/2+0.15 mat=stone
    for i $n+1
      gothic.archband $a-0.5 ($a-0.5)*0.866 0.25 0.3 at=0,0,-$l/2+$l/$n*$i mat=stone
    end
    box 0.24 0.18 $l at=0,$a*0.866-0.16,0 mat=stone
    mirror x
      box 0.3 0.22 $l at=$a/2-0.15,0,0 mat=stone
    end
  end
end
)LIB";
}

}  // namespace sg::sculpt
