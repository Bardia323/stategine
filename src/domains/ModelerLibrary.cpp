// The macros the modeller comes with, written in its own language: what a
// castle, a village or a wood is made of. A line `define name params  # what it
// is` is listed by `sculpt::recipes()`.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* library() {
    return R"LIB(
define column r=0.3 h=3   # a column on a plinth, with a capital; base on y = 0
  cyl $r*1.5 $h*0.07
  cyl $r $h*0.86 at=0,$h*0.07,0
  cyl $r*1.4 $h*0.07 at=0,$h*0.93,0
end
define merlons n=8 r=2 w=0.45 h=0.6 d=0.4   # a ring of merlons about y, their outer faces at r
  radial n=$n
    box $d $h $w at=$r-$d/2,0,0
  end
end
define battlement len=6 t=0.5 h=0.55 w=0.5 gap=0.4   # a crenellated parapet along x, base on y = 0
  let n floor(($len+$gap)/($w+$gap))
  box $len $h*0.35 $t
  array n=$n step=$w+$gap,0,0 at=-($n*$w+($n-1)*$gap)/2+$w/2,$h*0.35,0
    box $w $h*0.65 $t
  end
end
define wall len=8 h=4 t=0.8   # a wall along x with a crenellated walk on top; cut a gate with `sub arch`
  box $len $h $t
  mirror z
    battlement len=$len t=$t*0.3 h=0.6 w=0.55 gap=0.45 at=0,$h,$t*0.35
  end
end
define tower r=2 h=8 roof=3 slits=4   # a round tower with a ledge, arrow slits and a conical roof
  cyl $r $h
  cyl $r*1.12 0.3 at=0,$h-0.3,0
  sub radial n=$slits
    box 0.8 0.7 0.14 at=$r,$h*0.5,0
  end
  cone $r*1.25 $roof at=0,$h,0
end
define turret r=1.6 h=6 slits=3   # a round tower topped with merlons
  cyl $r $h
  merlons n=floor($r*5) r=$r w=$r*0.28 h=0.7 d=0.4 at=0,$h,0
  sub radial n=$slits
    box 0.8 0.7 0.14 at=$r,$h*0.55,0
  end
end
define arch w=1.4 h=2.4 d=2   # a cutter, for `sub arch`: a doorway with a round top, through z, its sill on y = 0
  box $w $h-$w/2 $d
  cyl $w/2 $d at=0,$h-$w/2,0 rot=90,0,0 centre=1
end
define window w=0.6 h=1.1 d=2   # a cutter, for `sub window`: a small arched opening through z
  arch $w $h $d
end
define gatehouse w=6 h=6 d=3.2 gw=2 gh=3   # a gate tower: a block, a crenellated top and an arched way through z
  box $w $h $d
  battlement len=$w t=$d*0.3 h=0.6 w=0.55 gap=0.45 at=0,$h,$d*0.35
  battlement len=$w t=$d*0.3 h=0.6 w=0.55 gap=0.45 at=0,$h,-$d*0.35
  sub arch $gw $gh $d*2
end
define stairs n=8 w=1.2 rise=0.2 run=0.3   # a flight rising along +z, base on y = 0
  for i $n
    box $w $rise*($i+1) $run at=0,0,$i*$run
  end
end
define roof w=6 len=8 h=2.5 over=0.3   # a pitched roof, its ridge along z, eaves on y = 0
  extrude $len+$over*2 -$w/2-$over,0 $w/2+$over,0 0,$h
end
define pyramid w=4 h=3   # a four-sided roof, its eaves on y = 0
  cone $w*0.7071 0 $h sides=4 rot=0,45,0
end
define pine h=4 r=1   # a pine: trunk and three tiers of cone
  cyl $r*0.12 $h*0.25
  cone $r $h*0.45 at=0,$h*0.18,0
  cone $r*0.75 $h*0.4 at=0,$h*0.4,0
  cone $r*0.5 $h*0.35 at=0,$h*0.62,0
end
define rock r=0.6 flat=0.7   # a boulder: three blended stones
  sphere $r scale=1.25,$flat,1
  blend=$r*0.5 sphere $r*0.7 at=$r*0.7,0,$r*0.3 scale=1,$flat,1
  blend=$r*0.5 sphere $r*0.55 at=-$r*0.5,0,-$r*0.5 scale=1,$flat,1
end
)LIB";
}

}  // namespace sg::sculpt
