// The classical style: a palazzo's language - a rusticated base, pilasters,
// windows under pediments, string courses, a deep cornice, a low hipped roof
// of tiles; columns with entasis, capital and base. (The words: ModelerArch.cpp.)
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_classical() {
    return R"LIB(
use orders
let classical_ww 0.42
let classical_wh 0.52
let classical_sill 0.26
let classical_roof 0.22
let classical_head square
define classical.wall len=10 h=7 t=0.5
  box $len $h $t at=0,0,-$t/2 mat=plaster
end
define classical.opening w=1 h=1.8 d=2
  box $w $h $d
end
define classical.window w=1 h=1.8   # an aedicule of the Ionic order: a sill on consoles, pilasters, an entablature and a pediment (triangular, or segmental where the bay says), the sash deep in the reveal
  let seg mod(round($h*7),2)
  order.aedicule ionic $w $h seg=$seg d=0.14 mat=stone
  box $w $h 0.02 at=0,0,-0.24 mat=glass
  box $w 0.04 0.04 at=0,$h*0.55,-0.22 mat=wood
  box 0.04 $h 0.04 at=0,0,-0.22 mat=wood
  box $w+0.04 0.05 0.05 at=0,0,-0.22 mat=wood
end
define classical.doorway w=1.6 h=3 d=2
  box $w $h $d
end
define classical.door w=1.6 h=3 t=0.5   # a portal of the Corinthian order: engaged columns on pedestals either side, an entablature, a pediment, panelled leaves
  let ch $h*0.78
  let m $ch/20
  order.pedestal corinthian $h*0.22 $m*2.6 at=-$w/2-$m*1.8,0,$m*1.3 mat=stone
  order.pedestal corinthian $h*0.22 $m*2.6 at=$w/2+$m*1.8,0,$m*1.3 mat=stone
  order.column corinthian $ch at=-$w/2-$m*1.8,$h*0.22,$m*1.3 mat=stone
  order.column corinthian $ch at=$w/2+$m*1.8,$h*0.22,$m*1.3 mat=stone
  mould.frame $w $h 0.16 0.1 mat=stone
  order.entablature corinthian $w+$m*7.2 $h*0.2 at=0,$h,$m*1.3 mat=stone
  order.pediment $w+$m*7.2+0.3 ($w+$m*7.2)*0.2 $m*2.8 c=$h*0.08 at=0,$h+$h*0.19,$m*1.3 mat=stone
  box $w $h 0.08 at=0,0,-$t*0.55 mat=wood
  box 0.04 $h 0.1 at=0,0,-$t*0.5 mat=wood
  mould.panel $w*0.36 $h*0.3 0.04 0.03 at=-$w*0.25,$h*0.1,-$t*0.55+0.04 mat=wood
  mould.panel $w*0.36 $h*0.3 0.04 0.03 at=$w*0.25,$h*0.1,-$t*0.55+0.04 mat=wood
  mould.panel $w*0.36 $h*0.38 0.04 0.03 at=-$w*0.25,$h*0.5,-$t*0.55+0.04 mat=wood
  mould.panel $w*0.36 $h*0.38 0.04 0.03 at=$w*0.25,$h*0.5,-$t*0.55+0.04 mat=wood
end
define classical.band len=10   # a string course: fillet, cyma reversa, fillet
  mould.stringcourse $len+0.2 0.22 0.16 at=0,-0.22,0 mat=stone
end
define classical.pier h=7   # a pilaster of the Ionic order, the full height of the floors it stands through
  order.pilaster ionic $h 0.5 mat=stone
end
define classical.base len=10 h=0.6   # rusticated: courses of chamfered blocks, a moulded plinth under them
  mould.fillet $len+0.2 $h*0.25 0.14 mat=stone
  mould.rustication $len+0.12 $h*0.75 0.1 min(0.45,$h*0.375) at=0,$h*0.25,0 mat=stone
end
define classical.cornice len=10   # the Corinthian entablature's cornice, with modillions, and a balustrade over it
  mould.cornice $len+0.4 0.9 0.8 modillions=1 at=0,0,0 mat=stone
  mould.balustrade $len+0.4 0.95 at=0,0.9,-0.1 mat=stone
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
define classical.column h=5 r=0.35   # a column of the order its slenderness asks for: Doric when stout, Ionic, Corinthian when slender (Vignola's modules give the rest)
  let k $h/(2*$r)
  if $k<7.5
    order.column doric $h fluted=1
  else
    if $k<9.5
      order.column ionic $h fluted=1
    else
      order.column corinthian $h fluted=1
    end
  end
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
define classical.wainscot len=10   # panelling to the dado: a skirting, raised panels in their frames, a chair rail
  box $len 0.9 0.04 at=0,0,0.02 mat=wood
  mould.fillet $len 0.14 0.07 at=0,0,0 mat=wood
  let n max(1,round($len/0.9))
  array n=$n step=$len/$n,0,0 at=-$len/2+$len/$n/2,0.22,0.04
    mould.panel $len/$n*0.7 0.5 0.05 0.03 mat=wood
  end
  mould.reversa $len 0.06 0.05 at=0,0.9,0.04 mat=wood
end
define classical.ipier h=5   # a pilaster of the Corinthian order on the inside
  order.pilaster corinthian $h 0.44 mat=plaster
end
define classical.icornice len=10   # a crown: a cornice of plaster with dentils
  mould.cornice $len 0.42 0.36 dentils=1 at=0,-0.42,0 mat=plaster
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
