// The parametric style: a point in a continuous space of architecture,
// rather than one of a list. `use param`, then `building style=param ...`.
//
// Eleven axes, each a number (0..1 unless said), read by every word of the
// style and nothing else:
//
//   param_arch     0 a flat head, 0..1 a segmental arch of that rise, 1 round,
//                  1..2 pointed, the centres moving out to the equilateral at 2, past it a lancet
//   param_orn      how much is moulded: 0 bare, 1 every edge carries a profile
//   param_mass     how heavy: wall thickness, pier size, stone or plaster
//   param_vert     the vertical against the horizontal: buttresses, pinnacles, fins against courses
//   param_pitch    the roof: 0 flat, 0.5 hipped, 1 steep gables
//   param_rustic   the base rusticated, quoined
//   param_glaze    window to wall
//   param_trace    tracery in the windows' heads
//   param_dome     a dome on the roof, and on the ceiling inside
//   param_pattern  surfaces patterned (star-and-cross, strapwork, muqarnas)
//   param_order    which classical order carries it: 0 tuscan .. 4 composite
//
// and two for towers: param_twist (degrees a tower turns over its height),
// param_taper (how much it narrows, 0..1).
//
// Every known style is a point here (`<style>_p_<axis>`), and the style starts
// as a mix: `let pa gothic`, `let pb islamic`, `let pt 0.4` before `use param`
// gives a building forty percent of the way from one to the other; then any
// axis may be moved alone (`let param_dome 1` after the `use`). The words are
// composed of the libraries that carry each tradition's own geometry (orders,
// pointed, girih, mould, structure), so wherever the point is, what comes out
// is built as buildings are built, not drawn.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_param() {
    return R"LIB(
use orders
use pointed
use girih
use structure
let classical_p_arch 0.0
let classical_p_orn 0.7
let classical_p_mass 0.5
let classical_p_vert 0.3
let classical_p_pitch 0.25
let classical_p_rustic 0.7
let classical_p_glaze 0.42
let classical_p_trace 0.0
let classical_p_dome 0.0
let classical_p_pattern 0.0
let classical_p_order 2
let baroque_p_arch 0.6
let baroque_p_orn 1.0
let baroque_p_mass 0.6
let baroque_p_vert 0.5
let baroque_p_pitch 0.3
let baroque_p_rustic 0.5
let baroque_p_glaze 0.45
let baroque_p_trace 0.0
let baroque_p_dome 0.6
let baroque_p_pattern 0.0
let baroque_p_order 3
let gothic_p_arch 2.0
let gothic_p_orn 0.8
let gothic_p_mass 0.6
let gothic_p_vert 1.0
let gothic_p_pitch 0.9
let gothic_p_rustic 0.1
let gothic_p_glaze 0.5
let gothic_p_trace 1.0
let gothic_p_dome 0.0
let gothic_p_pattern 0.0
let gothic_p_order 0
let romanesque_p_arch 1.0
let romanesque_p_orn 0.4
let romanesque_p_mass 0.9
let romanesque_p_vert 0.4
let romanesque_p_pitch 0.35
let romanesque_p_rustic 0.2
let romanesque_p_glaze 0.3
let romanesque_p_trace 0.0
let romanesque_p_dome 0.0
let romanesque_p_pattern 0.0
let romanesque_p_order 1
let byzantine_p_arch 1.0
let byzantine_p_orn 0.6
let byzantine_p_mass 0.8
let byzantine_p_vert 0.3
let byzantine_p_pitch 0.1
let byzantine_p_rustic 0.3
let byzantine_p_glaze 0.3
let byzantine_p_trace 0.0
let byzantine_p_dome 1.0
let byzantine_p_pattern 0.5
let byzantine_p_order 1
let islamic_p_arch 1.6
let islamic_p_orn 0.9
let islamic_p_mass 0.6
let islamic_p_vert 0.5
let islamic_p_pitch 0.0
let islamic_p_rustic 0.0
let islamic_p_glaze 0.45
let islamic_p_trace 0.3
let islamic_p_dome 1.0
let islamic_p_pattern 1.0
let islamic_p_order 0
let japanese_p_arch 0.0
let japanese_p_orn 0.5
let japanese_p_mass 0.2
let japanese_p_vert 0.2
let japanese_p_pitch 0.5
let japanese_p_rustic 0.0
let japanese_p_glaze 0.7
let japanese_p_trace 0.0
let japanese_p_dome 0.0
let japanese_p_pattern 0.4
let japanese_p_order 0
let artdeco_p_arch 0.0
let artdeco_p_orn 0.5
let artdeco_p_mass 0.6
let artdeco_p_vert 0.9
let artdeco_p_pitch 0.4
let artdeco_p_rustic 0.0
let artdeco_p_glaze 0.6
let artdeco_p_trace 0.0
let artdeco_p_dome 0.0
let artdeco_p_pattern 0.5
let artdeco_p_order 0
let modern_p_arch 0.0
let modern_p_orn 0.0
let modern_p_mass 0.2
let modern_p_vert 0.2
let modern_p_pitch 0.0
let modern_p_rustic 0.0
let modern_p_glaze 0.85
let modern_p_trace 0.0
let modern_p_dome 0.0
let modern_p_pattern 0.0
let modern_p_order 0
let brutalist_p_arch 0.0
let brutalist_p_orn 0.1
let brutalist_p_mass 1.0
let brutalist_p_vert 0.3
let brutalist_p_pitch 0.0
let brutalist_p_rustic 0.0
let brutalist_p_glaze 0.5
let brutalist_p_trace 0.0
let brutalist_p_dome 0.0
let brutalist_p_pattern 0.0
let brutalist_p_order 0
default pa classical
default pb classical
default pt 0
default param_arch mix($${pa}_p_arch,$${pb}_p_arch,$pt)
default param_orn mix($${pa}_p_orn,$${pb}_p_orn,$pt)
default param_mass mix($${pa}_p_mass,$${pb}_p_mass,$pt)
default param_vert mix($${pa}_p_vert,$${pb}_p_vert,$pt)
default param_pitch mix($${pa}_p_pitch,$${pb}_p_pitch,$pt)
default param_rustic mix($${pa}_p_rustic,$${pb}_p_rustic,$pt)
default param_glaze mix($${pa}_p_glaze,$${pb}_p_glaze,$pt)
default param_trace mix($${pa}_p_trace,$${pb}_p_trace,$pt)
default param_dome mix($${pa}_p_dome,$${pb}_p_dome,$pt)
default param_pattern mix($${pa}_p_pattern,$${pb}_p_pattern,$pt)
default param_order mix($${pa}_p_order,$${pb}_p_order,$pt)
default param_twist 0
default param_taper 0
let param_ww mix(0.3,0.86,$param_glaze)
let param_wh mix(0.46,0.72,$param_glaze*0.5+$param_vert*0.5)
let param_sill mix(0.3,0.12,$param_glaze)
let param_roof if($param_pitch<0.1,0.05,mix(0.1,0.85,$param_pitch))
let param_head square
if $param_arch>=0.5
  let param_head round
end
if $param_arch>=1.5
  let param_head pointed
end
let param_floor stone
if $param_pattern>0.6
  let param_floor tile
end
if $param_mass<0.35
  let param_floor wood
end
let param_wmat plaster
if $param_mass>0.65
  let param_wmat stone
end
if $param_mass>0.9
  let param_wmat concrete
end
let param_kind tuscan
if $param_order>=0.5
  let param_kind doric
end
if $param_order>=1.5
  let param_kind ionic
end
if $param_order>=2.5
  let param_kind corinthian
end
if $param_order>=3.5
  let param_kind composite
end
define param.arch w=1 h=2.5 d=1   # the opening's head as the arch axis says: flat, segmental, round, pointed, lancet - one shape, continuous in the parameter
  let a $param_arch
  if $a<0.05
    box $w $h $d
  else
    if $a<1
      let rise $a*$w/2
      let Rs ($w*$w/4+$rise*$rise)/(2*$rise)
      let s $h-$rise
      let cy $s+$rise-$Rs
      let t0 asin($w/2/$Rs)
      extrude $d -$w/2,0 $w/2,0 $w/2,$s $Rs*sin($t0-2*$t0*0/8),$cy+$Rs*cos($t0-2*$t0*0/8) $Rs*sin($t0-2*$t0*1/8),$cy+$Rs*cos($t0-2*$t0*1/8) $Rs*sin($t0-2*$t0*2/8),$cy+$Rs*cos($t0-2*$t0*2/8) $Rs*sin($t0-2*$t0*3/8),$cy+$Rs*cos($t0-2*$t0*3/8) $Rs*sin($t0-2*$t0*4/8),$cy+$Rs*cos($t0-2*$t0*4/8) $Rs*sin($t0-2*$t0*5/8),$cy+$Rs*cos($t0-2*$t0*5/8) $Rs*sin($t0-2*$t0*6/8),$cy+$Rs*cos($t0-2*$t0*6/8) $Rs*sin($t0-2*$t0*7/8),$cy+$Rs*cos($t0-2*$t0*7/8) $Rs*sin($t0-2*$t0*8/8),$cy+$Rs*cos($t0-2*$t0*8/8) -$w/2,$s
    else
      pointed.arch $w $h min(0.9,($a-1)*0.5) $d
    end
  end
end
define param.wall len=10 h=7 t=0.5
  box $len $h $t at=0,0,-$t/2 mat=$param_wmat
end
define param.opening w=1 h=2.5 d=2
  param.arch $w $h $d
end
define param.window w=1 h=2.5   # a window of the point: the glass in the arch's shape, tracery where the trace axis says, an aedicule of the order where ornament is high and the head flat, a hood and shafts where the head is arched, a screen of strapwork where pattern says, a sill
  let a $param_arch
  let c min(0.9,max(0,($a-1)*0.5))
  param.arch $w*0.98 $h*0.99 0.02 at=0,0,-0.22 mat=glass
  if ($param_trace>0.5)*($a>=1.2)
    pointed.tracery $w $h $c max(1,round($w/0.9)) 0.14 mat=stone
  else
    if $param_glaze>0.6
      box 0.05 $h 0.06 at=0,0,-0.2 mat=metal
      box $w 0.05 0.06 at=0,$h*0.55,-0.2 mat=metal
    else
      box 0.05 $h*0.9 0.05 at=0,0,-0.2 mat=wood
      box $w*0.9 0.05 0.05 at=0,$h*0.55,-0.2 mat=wood
    end
  end
  if $param_pattern>0.5
    girih.strap $w*0.9 $h*0.8 clamp($w*0.3,0.2,0.5) 0.04 0.035 at=0,0.05,-0.14 mat=wood
  end
  if $param_orn>0.45
    if $a<0.5
      order.aedicule $param_kind $w $h seg=0 pediment=if($param_orn>0.6,1,0) d=0.14 mat=stone
    else
      if $a<1
        order.aedicule $param_kind $w $h seg=1 pediment=if($param_orn>0.6,1,0) d=0.14 mat=stone
      else
        pointed.band $w+0.12 $h+0.1 $c 0.12 0.1 at=0,-0.05,0.02 mat=stone
        cyl 0.06 $h-($w+0.12)*(0.5+$c) at=-$w/2-0.1,0,0.06 sides=8 mat=stone
        cyl 0.06 $h-($w+0.12)*(0.5+$c) at=$w/2+0.1,0,0.06 sides=8 mat=stone
      end
    end
  else
    if $param_orn>0.15
      mould.frame $w $h 0.12 0.06 mat=stone
    end
  end
  if $param_orn>0.15
    extrude $w+0.3 0,0 0.14,0 0,0.1 at=0,-0.1,0 rot=0,-90,0 mat=stone
  end
end
define param.doorway w=1.8 h=3.2 d=2
  param.arch $w $h $d
end
define param.door w=1.8 h=3.2 t=0.6   # the door of the point: arched heads get archivolts in orders on shafts (a gable over them where vertical is high), flat heads a portal of the order; a frame of strapwork and a muqarnas hood where pattern says; panelled leaves
  let a $param_arch
  let c min(0.9,max(0,($a-1)*0.5))
  let orders 1+round($param_orn*2)
  if $a>=1
    for i $orders
      pointed.band $w+$i*0.3 $h+$i*0.22*(0.5+$c) $c 0.3 0.16 at=0,0,($orders-1-$i)*0.12 mat=stone
      if $param_orn>0.3
        cyl 0.07 $h-($w+$i*0.3)*(0.5+$c)*0.9 at=-$w/2-$i*0.15-0.08,0,($orders-1-$i)*0.12+0.08 sides=8 mat=stone
        cyl 0.07 $h-($w+$i*0.3)*(0.5+$c)*0.9 at=$w/2+$i*0.15+0.08,0,($orders-1-$i)*0.12+0.08 sides=8 mat=stone
      end
    end
    if $param_vert>0.7
      extrude 0.14 -$w/2-0.75,$h+0.5 $w/2+0.75,$h+0.5 0,$h+0.5+($w+0.9)*0.6 at=0,0,0.33 mat=stone
      mould.finial 0.4 0.08 at=0,$h+0.5+($w+0.9)*0.6,0.33 mat=stone
    end
  else
    if $param_orn>0.4
      let ch $h*0.78
      let m $ch/$${param_kind}_n
      order.column $param_kind $ch at=-$w/2-$m*1.8,$h*0.22,$m*1.3 mat=stone
      order.column $param_kind $ch at=$w/2+$m*1.8,$h*0.22,$m*1.3 mat=stone
      order.pedestal $param_kind $h*0.22 $m*2.6 at=-$w/2-$m*1.8,0,$m*1.3 mat=stone
      order.pedestal $param_kind $h*0.22 $m*2.6 at=$w/2+$m*1.8,0,$m*1.3 mat=stone
      order.entablature $param_kind $w+$m*7.2 $h*0.2 at=0,$h,$m*1.3 mat=stone
      order.pediment $w+$m*7.2+0.3 ($w+$m*7.2)*0.2 $m*2.8 seg=if($a>0.3,1,0) c=$h*0.08 at=0,$h+$h*0.19,$m*1.3 mat=stone
      mould.frame $w $h 0.16 0.1 mat=stone
    else
      box $w+0.3 0.14 1.2 at=0,$h+0.1,0.6 mat=$param_wmat
      mould.frame $w $h 0.1 0.04 mat=metal
    end
  end
  if $param_pattern>0.6
    girih.iwan $w $h+0.4 $t*0.9 at=0,0,0.3
  end
  param.arch $w $h 0.1 at=0,0,-$t*0.55 mat=wood
  if $param_orn>0.3
    mould.panel $w*0.36 $h*0.3 0.04 0.03 at=-$w*0.25,$h*0.1,-$t*0.55+0.04 mat=wood
    mould.panel $w*0.36 $h*0.3 0.04 0.03 at=$w*0.25,$h*0.1,-$t*0.55+0.04 mat=wood
    mould.panel $w*0.36 $h*0.3 0.04 0.03 at=-$w*0.25,$h*0.48,-$t*0.55+0.04 mat=wood
    mould.panel $w*0.36 $h*0.3 0.04 0.03 at=$w*0.25,$h*0.48,-$t*0.55+0.04 mat=wood
  end
end
define param.band len=10   # the course at a floor: a plain slab edge, a string course, or a frieze of tile
  if $param_pattern>0.6
    girih.band $len+0.04 0.4 0.03 at=0,-0.4,0 mat=tile
  else
    if $param_orn>0.3
      mould.stringcourse $len+0.2 0.22 0.16 at=0,-0.22,0 mat=stone
    else
      box $len+0.02 0.24 0.1 at=0,-0.12,0.05 mat=$param_wmat
    end
  end
end
define param.pier h=7   # what stands between bays: a buttress (pinnacled when vertical is high), a pilaster of the order, a heavy pier, a fin - by vertical and mass
  if $param_vert>0.7
    pointed.buttress $h 0.55 0.9 2 pin=if($param_vert>0.85,1,0) mat=stone
  else
    if $param_orn>0.45
      order.pilaster $param_kind $h max(0.4,$param_mass*0.7) mat=stone
    else
      if $param_mass>0.7
        box 0.5 $h 0.5 at=0,0,0.25 mat=$param_wmat
      else
        box 0.08 $h 0.3 at=0,0,0.15 mat=metal
      end
    end
  end
end
define param.base len=10 h=0.6   # the plinth: rusticated where rustic says, else a moulded course, else a plain one
  if $param_rustic>0.5
    mould.fillet $len+0.2 $h*0.25 0.14 mat=stone
    mould.rustication $len+0.12 $h*0.75 0.1 min(0.45,$h*0.375) at=0,$h*0.25,0 mat=stone
  else
    if $param_orn>0.3
      mould.fillet $len+0.24 $h*0.8 0.14 mat=stone
      mould.reversa $len+0.24 $h*0.2 0.1 at=0,$h*0.8,0.04 mat=stone
    else
      box $len $h 0.06 at=0,0,0.03 mat=$param_wmat
    end
  end
end
define param.cornice len=10   # the crown: a parapet when bare; a corbel table and battlements when vertical; a muqarnas under merlons when patterned; else a full cornice, modillioned and balustraded as ornament rises
  if $param_orn<0.2
    structure.parapet $len+0.04 0.6 0.2 at=0,0,0.2 mat=$param_wmat
  else
    if $param_vert>0.7
      let n floor($len/0.6)
      array n=$n step=0.6,0,0 at=-($n-1)*0.3,-0.3,0.12
        extrude 0.18 0,0 0.24,0.2 0.24,0.3 0,0.3 at=0,0,0 rot=0,-90,0 mat=stone
      end
      battlement len=$len+0.4 t=0.3 h=0.9 w=0.6 gap=0.42 at=0,0.16,0.12 mat=stone
    else
      if $param_pattern>0.6
        girih.muqarnas $len+0.2 0.6 3 0.4 at=0,-0.6,0 mat=plaster
        girih.band $len+0.2 0.4 0.03 at=0,0,0.12 mat=tile
        box $len+0.2 0.4 0.12 at=0,0,0.06 mat=tile
        let n floor($len/0.8)
        array n=$n step=0.8,0,0 at=-($n-1)*0.4,0.4,0
          box 0.5 0.3 0.25 mat=stone
          pyramid 0.32 0.2 at=0,0.3,0 mat=stone
        end
      else
        mould.cornice $len+0.4 0.9 0.8 dentils=1 modillions=if($param_orn>0.6,1,0) at=0,0,0 mat=stone
        if $param_orn>0.8
          mould.balustrade $len+0.4 0.95 at=0,0.9,-0.1 mat=stone
        end
      end
    end
  end
end
define param.roof w=12 d=9 h=3   # the roof by pitch: flat (a slab, a plant room if bare), hipped in tiles, or steep gables of slate with stone gable walls; a dome on a drum over the middle where the dome axis says
  if $param_pitch<0.1
    box $w-0.3 0.2 $d-0.3 mat=$param_wmat
  else
    if $param_pitch<0.55
      let ww $w+1.0
      let dd $d+1.0
      let r max(0.05,abs($ww-$dd))
      if $ww>=$dd
        loft $h -$ww/2,-$dd/2 $ww/2,-$dd/2 $ww/2,$dd/2 -$ww/2,$dd/2 / -$r/2,-0.02 $r/2,-0.02 $r/2,0.02 -$r/2,0.02 at=0,0.3,0 mat=rooftiles
      else
        loft $h -$ww/2,-$dd/2 $ww/2,-$dd/2 $ww/2,$dd/2 -$ww/2,$dd/2 / -0.02,-$r/2 0.02,-$r/2 0.02,$r/2 -0.02,$r/2 at=0,0.3,0 mat=rooftiles
      end
    else
      if $d>$w
        extrude $d+0.2 -$w/2-0.35,0 $w/2+0.35,0 0,$h mat=slate
        pointed.gable $w $h*0.96 0.45 at=0,0,$d/2-0.22 mat=stone
        pointed.gable $w $h*0.96 0.45 at=0,0,-$d/2+0.22 mat=stone
      else
        extrude $w+0.2 -$d/2-0.35,0 $d/2+0.35,0 0,$h rot=0,90,0 mat=slate
        pointed.gable $d $h*0.96 0.45 at=$w/2-0.22,0,0 rot=0,90,0 mat=stone
        pointed.gable $d $h*0.96 0.45 at=-$w/2+0.22,0,0 rot=0,90,0 mat=stone
      end
    end
  end
  if $param_dome>0.5
    let r min($w,$d)*0.3
    if $param_pattern>0.5
      girih.dome $r drum=1 at=0,$h*if($param_pitch<0.1,0,0.5)+0.2,0
    else
      cyl $r*1.02 $r*0.45 sides=24 at=0,$h*if($param_pitch<0.1,0,0.5)+0.2,0 mat=stone
      structure.dome $r 0.2 at=0,$h*if($param_pitch<0.1,0,0.5)+0.2+$r*0.45,0 sides=32 mat=tile
      mould.finial $r*0.3 $r*0.08 at=0,$h*if($param_pitch<0.1,0,0.5)+0.2+$r*1.45,0 mat=gold
    end
  end
end
define param.column h=5 r=0.35   # a column of the order, fluted where ornament is high; a heavy pier where mass is
  if $param_mass>0.85
    structure.pier $r*2.2 $h $r*2.2 mat=stone
  else
    order.column $param_kind $h fluted=if($param_orn>0.5,1,0) mat=stone
  end
end
define param.tower r=2.5 h=14   # a tower of the point: a shaft (lofted, so it may twist and taper), stages marked by bands, belfry openings of the arch's shape, and a top by dome, pitch and vertical: a dome, a spire among pinnacles, or a parapet
  let k 1-$param_taper*0.5
  let tw $param_twist
  let sh $h*0.72
  loft $sh -$r,-$r $r,-$r $r,$r -$r,$r / $r*(1-$param_taper*0.125)*1.4142*cos(225+$tw*0.25),$r*(1-$param_taper*0.125)*1.4142*sin(225+$tw*0.25) $r*(1-$param_taper*0.125)*1.4142*cos(315+$tw*0.25),$r*(1-$param_taper*0.125)*1.4142*sin(315+$tw*0.25) $r*(1-$param_taper*0.125)*1.4142*cos(45+$tw*0.25),$r*(1-$param_taper*0.125)*1.4142*sin(45+$tw*0.25) $r*(1-$param_taper*0.125)*1.4142*cos(135+$tw*0.25),$r*(1-$param_taper*0.125)*1.4142*sin(135+$tw*0.25) / $r*(1-$param_taper*0.25)*1.4142*cos(225+$tw*0.5),$r*(1-$param_taper*0.25)*1.4142*sin(225+$tw*0.5) $r*(1-$param_taper*0.25)*1.4142*cos(315+$tw*0.5),$r*(1-$param_taper*0.25)*1.4142*sin(315+$tw*0.5) $r*(1-$param_taper*0.25)*1.4142*cos(45+$tw*0.5),$r*(1-$param_taper*0.25)*1.4142*sin(45+$tw*0.5) $r*(1-$param_taper*0.25)*1.4142*cos(135+$tw*0.5),$r*(1-$param_taper*0.25)*1.4142*sin(135+$tw*0.5) / $r*(1-$param_taper*0.375)*1.4142*cos(225+$tw*0.75),$r*(1-$param_taper*0.375)*1.4142*sin(225+$tw*0.75) $r*(1-$param_taper*0.375)*1.4142*cos(315+$tw*0.75),$r*(1-$param_taper*0.375)*1.4142*sin(315+$tw*0.75) $r*(1-$param_taper*0.375)*1.4142*cos(45+$tw*0.75),$r*(1-$param_taper*0.375)*1.4142*sin(45+$tw*0.75) $r*(1-$param_taper*0.375)*1.4142*cos(135+$tw*0.75),$r*(1-$param_taper*0.375)*1.4142*sin(135+$tw*0.75) / $r*$k*1.4142*cos(225+$tw),$r*$k*1.4142*sin(225+$tw) $r*$k*1.4142*cos(315+$tw),$r*$k*1.4142*sin(315+$tw) $r*$k*1.4142*cos(45+$tw),$r*$k*1.4142*sin(45+$tw) $r*$k*1.4142*cos(135+$tw),$r*$k*1.4142*sin(135+$tw) mat=$param_wmat
  if $tw==0
    for i 3
      param.band $r*2*(1-$param_taper*0.5*($i+1)/4) at=0,$sh*($i+1)/4,$r*(1-$param_taper*0.5*($i+1)/4)
      param.band $r*2*(1-$param_taper*0.5*($i+1)/4) at=0,$sh*($i+1)/4,-$r*(1-$param_taper*0.5*($i+1)/4) rot=180
      param.band $r*2*(1-$param_taper*0.5*($i+1)/4) at=$r*(1-$param_taper*0.5*($i+1)/4),$sh*($i+1)/4,0 rot=90
      param.band $r*2*(1-$param_taper*0.5*($i+1)/4) at=-$r*(1-$param_taper*0.5*($i+1)/4),$sh*($i+1)/4,0 rot=-90
    end
    radial n=4
      param.window $r*$k*0.6 $sh*0.15 at=0,$sh*0.78,$r*$k
    end
  end
  box $r*$k*2.2 $sh*0.04 $r*$k*2.2 at=0,$sh,0 mat=stone
  if $param_dome>0.5
    if $param_pattern>0.5
      girih.dome $r*$k*0.9 drum=1 at=0,$sh+$sh*0.04,0
    else
      cyl $r*$k*0.95 $h*0.08 sides=24 at=0,$sh+$sh*0.04,0 mat=stone
      structure.dome $r*$k*0.9 0.2 at=0,$sh+$sh*0.04+$h*0.08,0 sides=32 mat=tile
    end
  else
    if $param_pitch>0.5
      radial n=4
        pointed.pinnacle $r*0.3 $h*0.1 at=$r*$k*0.85,$sh+$sh*0.04,$r*$k*0.85 mat=stone
      end
      cone $r*$k*1.15 $h*(0.24+$param_pitch*0.3) sides=8 rot=0,22.5,0 at=0,$sh+$sh*0.04,0 mat=slate
    else
      param.cornice $r*$k*2.2 at=0,$sh+$sh*0.04,$r*$k*1.1
      param.cornice $r*$k*2.2 at=0,$sh+$sh*0.04,-$r*$k*1.1 rot=180
      param.cornice $r*$k*2.2 at=$r*$k*1.1,$sh+$sh*0.04,0 rot=90
      param.cornice $r*$k*2.2 at=-$r*$k*1.1,$sh+$sh*0.04,0 rot=-90
    end
  end
end
define param.wainscot len=10   # the dado inside: panelling where ornament is, star-and-cross tile where pattern is, a blind arcade where the head is pointed, a plain skirting else
  if $param_pattern>0.6
    box $len 1.4 0.03 at=0,0,0.015 mat=tile
    girih.starcross $len 1.3 0.42 0.02 at=0,0.05,0.03 mat=plaster
  else
    if ($param_arch>=1.5)*($param_vert>0.6)
      let n max(2,floor($len/1.2))
      array n=$n step=$len/$n,0,0 at=-$len/2+$len/$n/2,0,0
        pointed.band $len/$n*0.7 1.6 0.5 0.1 0.08 at=0,0.2,0 mat=stone
      end
      mould.fillet $len 0.2 0.1 at=0,0,0 mat=stone
    else
      if $param_orn>0.4
        box $len 0.9 0.04 at=0,0,0.02 mat=wood
        mould.fillet $len 0.14 0.07 at=0,0,0 mat=wood
        let n max(1,round($len/0.9))
        array n=$n step=$len/$n,0,0 at=-$len/2+$len/$n/2,0.22,0.04
          mould.panel $len/$n*0.7 0.5 0.05 0.03 mat=wood
        end
        mould.reversa $len 0.06 0.05 at=0,0.9,0.04 mat=wood
      else
        box $len 0.1 0.02 at=0,0,0.01 mat=metal
      end
    end
  end
end
define param.ipier h=5   # between bays inside: a shaft to the vault where pointed, a pilaster of the order where ornament is, nothing where bare
  if $param_arch>=1.5
    cyl 0.14 $h at=0,0,0.14 mat=stone
    cyl 0.22 0.25 at=0,$h-0.25,0.14 mat=stone
  else
    if $param_orn>0.4
      order.pilaster $param_kind $h 0.44 mat=plaster
    else
      box 0.02 0.02 0.02 at=0,0,0.01 mat=plaster
    end
  end
end
define param.icornice len=10   # the crown inside: a muqarnas, a cornice of plaster, or a shadow gap
  if $param_pattern>0.6
    girih.muqarnas $len 0.5 3 0.35 at=0,-0.5,0 mat=plaster
  else
    if $param_orn>0.3
      mould.cornice $len 0.42 0.36 dentils=1 at=0,-0.42,0 mat=plaster
    else
      box $len 0.04 0.02 at=0,-0.04,0.01 mat=metal
    end
  end
end
define param.ceiling w=10 d=14 h=6   # the ceiling by the arch and the dome: rib vaults where pointed, a barrel on transverse arches where round, a dome on pendentives where the dome axis says, coffers where ornament is, a flat soffit else
  let a min($w,$d)
  let l max($w,$d)
  if $param_dome>0.5
    let s $a*0.8
    group
      box $w 0.3 $d mat=plaster
      sub box $s $s*0.9 $s at=0,-$s*0.7,0 res=max(0.1,$s/40)
    end
    structure.pendentives $s 0 0.3 max(0.1,$s/40) at=0,-$s*0.7071,0 mat=plaster
    structure.dome $s/2 0.25 at=0,-$s*0.7071+$s/2-0.01,0 sides=32 mat=tile
  else
    if $param_arch>=1.5
      let n max(1,round($l/($a*0.85)))
      let hc hypot($a,$l/$n)/2
      group rot=0,if($d>=$w,0,90),0
        pointed.vaults $a $l $hc $n res=max(0.1,$a/45) rib=clamp($a*0.022,0.1,0.2) mat=stone
      end
    else
      if $param_arch>=0.5
        let n max(2,round($l/($a*0.7)))
        group rot=0,if($d>=$w,0,90),0
          structure.barrel $a $l+0.3 $a/2 0.4 mat=stone
          for i $n+1
            pointed.band $a-0.6 $a/2-0.3 0 0.35 0.5 at=0,0,-$l/2+$l/$n*$i mat=stone
          end
        end
      else
        box $w 0.2 $d mat=plaster
        if $param_orn>0.4
          let nx max(2,round($w/1.6))
          let nz max(2,round($d/1.6))
          for i $nx+1
            box 0.16 0.24 $d at=-$w/2+$w/$nx*$i,-0.24,0 mat=plaster
          end
          for j $nz+1
            box $w 0.24 0.16 at=0,-0.24,-$d/2+$d/$nz*$j mat=plaster
          end
        end
      end
    end
  end
end
)LIB";
}

}  // namespace sg::sculpt
