// The gothic style: pointed arches, tracery, buttresses with pinnacles,
// archivolts round the door, a crenellated parapet, a steep slate roof, spires.
// (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_gothic() {
    return R"LIB(
use pointed
let gothic_ww 0.5
let gothic_wh 0.66
let gothic_sill 0.16
let gothic_roof 0.75
let gothic_head pointed
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
define gothic.window w=1 h=2.5   # bar tracery under an equilateral head: as many lights as the width takes, a foiled roundel over them, a hood mould with its stops, a weathered sill
  pointed.window $w $h 0.5 max(1,round($w/0.9)) 0.14 mat=stone
end
define gothic.doorway w=1.8 h=3.2 d=2
  gothic.arch $w $h $d
end
define gothic.door w=1.8 h=3.2 t=0.6   # receding orders of pointed archivolts, each on a shaft, a crocketed gable over the outermost, and the leaves with their strap hinges
  for i 3
    pointed.band $w+$i*0.3 $h+$i*0.22 0.5 0.3 0.16 at=0,0,(2-$i)*0.12 mat=stone
    cyl 0.07 $h-($w+$i*0.3)*0.866 at=-$w/2-$i*0.15-0.08,0,(2-$i)*0.12+0.08 sides=10 mat=stone
    cyl 0.07 $h-($w+$i*0.3)*0.866 at=$w/2+$i*0.15+0.08,0,(2-$i)*0.12+0.08 sides=10 mat=stone
  end
  extrude 0.14 -$w/2-0.75,$h+0.5 $w/2+0.75,$h+0.5 0,$h+0.5+($w+0.9)*0.6 at=0,0,0.33 mat=stone
  for i 3
    pointed.crocket 0.14 at=-$w*0.3+$i*$w*0.3,$h+0.55+($w+0.9)*0.6*(1-abs($i-1)*0.5)-0.1,0.4 mat=stone
  end
  mould.finial 0.4 0.08 at=0,$h+0.5+($w+0.9)*0.6,0.33 mat=stone
  pointed.arch $w $h 0.5 0.1 at=0,0,-$t*0.55 mat=wood
  for i 3
    box $w*0.42 0.05 0.03 at=-$w*0.26,$h*0.2+$i*$h*0.25,-$t*0.55+0.06 mat=metal
    box $w*0.42 0.05 0.03 at=$w*0.26,$h*0.2+$i*$h*0.25,-$t*0.55+0.06 mat=metal
  end
end
define gothic.band len=10   # a string course, weathered on top: a fillet and a chamfer
  extrude $len+0.1 0,0 0.14,0 0.14,0.06 0,0.16 at=0,-0.16,0 rot=0,-90,0 mat=stone
end
define gothic.pier h=7   # a buttress in two weathered stages under a crocketed pinnacle
  pointed.buttress $h 0.55 0.9 2 mat=stone
end
define gothic.base len=10 h=0.6   # a plinth with a weathered top
  box $len+0.24 $h 0.14 at=0,0,0.07 mat=stone
  extrude $len+0.24 0,0 0.14,0 0,0.14 at=0,$h,0 rot=0,90,0 mat=stone
end
define gothic.cornice len=10   # a corbel table carrying a string, and a crenellated parapet
  let n floor($len/0.6)
  array n=$n step=0.6,0,0 at=-($n-1)*0.3,-0.3,0.12
    extrude 0.18 0,0 0.24,0.2 0.24,0.3 0,0.3 at=0,0,0 rot=0,-90,0 mat=stone
  end
  gothic.band $len+0.4 at=0,0.16,0.12
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
define gothic.tower r=2.5 h=24   # a square tower, buttressed at its corners under pinnacles, belfry lights of two lights each, a spire
  box $r*2 $h*0.55 $r*2 mat=stone
  radial n=4
    box 0.7 $h*0.5 0.7 at=$r,0,$r mat=stone
    pointed.pinnacle 0.6 $h*0.1 at=$r,$h*0.55,$r mat=stone
    pointed.window $r*0.6 $h*0.12 0.5 2 at=0,$h*0.4,$r mat=stone
    gothic.band $r*1.6 at=0,$h*0.3,$r
  end
  sub radial n=4
    gothic.arch $r*0.6 $h*0.12 $r*3 at=0,$h*0.4,0
  end
  battlement len=$r*2+0.2 t=0.3 h=0.8 w=0.5 gap=0.4 at=0,$h*0.55,$r-0.1 mat=stone
  battlement len=$r*2+0.2 t=0.3 h=0.8 w=0.5 gap=0.4 at=0,$h*0.55,-$r+0.1 mat=stone
  cone $r*1.15 $h*0.45 sides=8 rot=0,22.5,0 at=0,$h*0.55,0 mat=slate
end
let gothic_floor stone
define gothic.wainscot len=10   # a blind arcade low along the wall: pointed arches in relief on shafts with a string over them
  let n max(2,floor($len/1.2))
  array n=$n step=$len/$n,0,0 at=-$len/2+$len/$n/2,0,0
    pointed.band $len/$n*0.7 1.6 0.5 0.1 0.08 at=0,0.2,0 mat=stone
    cyl 0.05 1.6-$len/$n*0.7*0.866 at=-$len/$n*0.35-0.05,0.2,0.05 sides=8 mat=stone
    cyl 0.05 1.6-$len/$n*0.7*0.866 at=$len/$n*0.35+0.05,0.2,0.05 sides=8 mat=stone
  end
  mould.fillet $len 0.2 0.1 at=0,0,0 mat=stone
  gothic.band $len at=0,2.0,0
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
define gothic.ceiling w=10 d=14 h=6   # quadripartite rib vaults down the longer side, a bay about as long as the room is wide: round diagonal ribs, transverse and wall ribs struck to meet them at one crown, a boss at every crossing; the web their groins; a string where they spring
  let a min($w,$d)
  let l max($w,$d)
  let n max(1,round($l/($a*0.85)))
  let hc hypot($a,$l/$n)/2
  group rot=0,if($d>=$w,0,90),0
    pointed.vaults $a $l $hc $n res=max(0.1,$a/45) rib=clamp($a*0.022,0.1,0.2) mat=stone
    mirror x
      gothic.band $l at=$a/2,0.1,0 rot=0,-90,0
    end
  end
end
)LIB";
}

}  // namespace sg::sculpt
