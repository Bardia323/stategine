// The classical orders, by Vignola's modules (Regola delli cinque ordini,
// 1562): the module M is half the column's lower diameter, and every part is
// so many modules - the column 14 (Tuscan), 16 (Doric), 18 (Ionic), 20
// (Corinthian, Composite) modules tall with its base and capital, the
// entablature a quarter of the column, the pedestal a third. Ask for a
// column of a height and the order gives its module and everything from it.
// `use orders` (it uses `mould`).
//
//   order.column kind h [fluted=0]     base, shaft (entasis, drawing in to the order's top), capital
//   order.pilaster kind h w            the same flat against the wall z = 0, facing +z
//   order.entablature kind len ent     architrave, frieze (Doric: triglyphs and metopes; Ionic: pulvinated), cornice - ent tall
//   order.pedestal kind h w            plinth, die, cap
//   order.pediment w h [seg=0 c=]      tympanum under raking cornices c tall (seg=1: a segmental arc)
//   order.portico kind w d n h         a temple front: three steps, n columns across, the entablature round, pediments, a roof
//   order.aedicule kind w h [seg=0 pediment=1]  a window or door surround: sill on consoles, pilasters, entablature, pediment
//   order.arcade kind len h n          piers with engaged columns, round arches with archivolts and keystones, an entablature
//   order.colonnade kind len n h       columns in a row under their entablature
//   order.archband r t d               the half ring of a round arch, r within, t thick, through z (exact)
//
// kind: tuscan doric ionic corinthian composite. Everything stands on y = 0
// along x and faces +z, placed as any shape is. Nothing is cut: every
// moulding is swept and every flute is lofted, so a column is a few hundred
// faces and a portico a few thousand - and it exports whole.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_orders() {
    return R"LIB(
use mould
let tuscan_n 14
let doric_n 16
let ionic_n 18
let corinthian_n 20
let composite_n 20
let tuscan_cap 1
let doric_cap 1
let ionic_cap 1.1
let corinthian_cap 2.33
let composite_cap 2.33
let tuscan_top 0.78
let doric_top 0.82
let ionic_top 0.85
let corinthian_top 0.86
let composite_top 0.86
let tuscan_k 0
let doric_k 1
let ionic_k 2
let corinthian_k 3
let composite_k 4
define order.archband r=1 t=0.2 d=0.4   # the half ring of a round arch: r within, t thick, d through z, its foot on y = 0 (exact: no cut)
  extrude $d -($r+$t)*1.0000,($r+$t)*0.0000 -($r+$t)*0.9659,($r+$t)*0.2588 -($r+$t)*0.8660,($r+$t)*0.5000 -($r+$t)*0.7071,($r+$t)*0.7071 -($r+$t)*0.5000,($r+$t)*0.8660 -($r+$t)*0.2588,($r+$t)*0.9659 -($r+$t)*0.0000,($r+$t)*1.0000 -($r+$t)*-0.2588,($r+$t)*0.9659 -($r+$t)*-0.5000,($r+$t)*0.8660 -($r+$t)*-0.7071,($r+$t)*0.7071 -($r+$t)*-0.8660,($r+$t)*0.5000 -($r+$t)*-0.9659,($r+$t)*0.2588 -($r+$t)*-1.0000,($r+$t)*0.0000 -$r*-1.0000,$r*0.0000 -$r*-0.9659,$r*0.2588 -$r*-0.8660,$r*0.5000 -$r*-0.7071,$r*0.7071 -$r*-0.5000,$r*0.8660 -$r*-0.2588,$r*0.9659 -$r*0.0000,$r*1.0000 -$r*0.2588,$r*0.9659 -$r*0.5000,$r*0.8660 -$r*0.7071,$r*0.7071 -$r*0.8660,$r*0.5000 -$r*0.9659,$r*0.2588 -$r*1.0000,$r*0.0000
end
define order.shaft h=4 r=0.3 top=0.85 fluted=0   # the shaft: straight for its lower third, then drawing in to top*r - plain, or with twenty flutes
  if $fluted
    loft $h $r*1.0000,$r*0.0000 $r*0.9*0.9877,$r*0.9*0.1564 $r*0.9511,$r*0.3090 $r*0.9*0.8910,$r*0.9*0.4540 $r*0.8090,$r*0.5878 $r*0.9*0.7071,$r*0.9*0.7071 $r*0.5878,$r*0.8090 $r*0.9*0.4540,$r*0.9*0.8910 $r*0.3090,$r*0.9511 $r*0.9*0.1564,$r*0.9*0.9877 $r*0.0000,$r*1.0000 $r*0.9*-0.1564,$r*0.9*0.9877 $r*-0.3090,$r*0.9511 $r*0.9*-0.4540,$r*0.9*0.8910 $r*-0.5878,$r*0.8090 $r*0.9*-0.7071,$r*0.9*0.7071 $r*-0.8090,$r*0.5878 $r*0.9*-0.8910,$r*0.9*0.4540 $r*-0.9511,$r*0.3090 $r*0.9*-0.9877,$r*0.9*0.1564 $r*-1.0000,$r*0.0000 $r*0.9*-0.9877,$r*0.9*-0.1564 $r*-0.9511,$r*-0.3090 $r*0.9*-0.8910,$r*0.9*-0.4540 $r*-0.8090,$r*-0.5878 $r*0.9*-0.7071,$r*0.9*-0.7071 $r*-0.5878,$r*-0.8090 $r*0.9*-0.4540,$r*0.9*-0.8910 $r*-0.3090,$r*-0.9511 $r*0.9*-0.1564,$r*0.9*-0.9877 $r*-0.0000,$r*-1.0000 $r*0.9*0.1564,$r*0.9*-0.9877 $r*0.3090,$r*-0.9511 $r*0.9*0.4540,$r*0.9*-0.8910 $r*0.5878,$r*-0.8090 $r*0.9*0.7071,$r*0.9*-0.7071 $r*0.8090,$r*-0.5878 $r*0.9*0.8910,$r*0.9*-0.4540 $r*0.9511,$r*-0.3090 $r*0.9*0.9877,$r*0.9*-0.1564 / $r*0.985*1.0000,$r*0.985*0.0000 $r*0.985*0.9*0.9877,$r*0.985*0.9*0.1564 $r*0.985*0.9511,$r*0.985*0.3090 $r*0.985*0.9*0.8910,$r*0.985*0.9*0.4540 $r*0.985*0.8090,$r*0.985*0.5878 $r*0.985*0.9*0.7071,$r*0.985*0.9*0.7071 $r*0.985*0.5878,$r*0.985*0.8090 $r*0.985*0.9*0.4540,$r*0.985*0.9*0.8910 $r*0.985*0.3090,$r*0.985*0.9511 $r*0.985*0.9*0.1564,$r*0.985*0.9*0.9877 $r*0.985*0.0000,$r*0.985*1.0000 $r*0.985*0.9*-0.1564,$r*0.985*0.9*0.9877 $r*0.985*-0.3090,$r*0.985*0.9511 $r*0.985*0.9*-0.4540,$r*0.985*0.9*0.8910 $r*0.985*-0.5878,$r*0.985*0.8090 $r*0.985*0.9*-0.7071,$r*0.985*0.9*0.7071 $r*0.985*-0.8090,$r*0.985*0.5878 $r*0.985*0.9*-0.8910,$r*0.985*0.9*0.4540 $r*0.985*-0.9511,$r*0.985*0.3090 $r*0.985*0.9*-0.9877,$r*0.985*0.9*0.1564 $r*0.985*-1.0000,$r*0.985*0.0000 $r*0.985*0.9*-0.9877,$r*0.985*0.9*-0.1564 $r*0.985*-0.9511,$r*0.985*-0.3090 $r*0.985*0.9*-0.8910,$r*0.985*0.9*-0.4540 $r*0.985*-0.8090,$r*0.985*-0.5878 $r*0.985*0.9*-0.7071,$r*0.985*0.9*-0.7071 $r*0.985*-0.5878,$r*0.985*-0.8090 $r*0.985*0.9*-0.4540,$r*0.985*0.9*-0.8910 $r*0.985*-0.3090,$r*0.985*-0.9511 $r*0.985*0.9*-0.1564,$r*0.985*0.9*-0.9877 $r*0.985*-0.0000,$r*0.985*-1.0000 $r*0.985*0.9*0.1564,$r*0.985*0.9*-0.9877 $r*0.985*0.3090,$r*0.985*-0.9511 $r*0.985*0.9*0.4540,$r*0.985*0.9*-0.8910 $r*0.985*0.5878,$r*0.985*-0.8090 $r*0.985*0.9*0.7071,$r*0.985*0.9*-0.7071 $r*0.985*0.8090,$r*0.985*-0.5878 $r*0.985*0.9*0.8910,$r*0.985*0.9*-0.4540 $r*0.985*0.9511,$r*0.985*-0.3090 $r*0.985*0.9*0.9877,$r*0.985*0.9*-0.1564 / $r*$top*1.0000,$r*$top*0.0000 $r*$top*0.9*0.9877,$r*$top*0.9*0.1564 $r*$top*0.9511,$r*$top*0.3090 $r*$top*0.9*0.8910,$r*$top*0.9*0.4540 $r*$top*0.8090,$r*$top*0.5878 $r*$top*0.9*0.7071,$r*$top*0.9*0.7071 $r*$top*0.5878,$r*$top*0.8090 $r*$top*0.9*0.4540,$r*$top*0.9*0.8910 $r*$top*0.3090,$r*$top*0.9511 $r*$top*0.9*0.1564,$r*$top*0.9*0.9877 $r*$top*0.0000,$r*$top*1.0000 $r*$top*0.9*-0.1564,$r*$top*0.9*0.9877 $r*$top*-0.3090,$r*$top*0.9511 $r*$top*0.9*-0.4540,$r*$top*0.9*0.8910 $r*$top*-0.5878,$r*$top*0.8090 $r*$top*0.9*-0.7071,$r*$top*0.9*0.7071 $r*$top*-0.8090,$r*$top*0.5878 $r*$top*0.9*-0.8910,$r*$top*0.9*0.4540 $r*$top*-0.9511,$r*$top*0.3090 $r*$top*0.9*-0.9877,$r*$top*0.9*0.1564 $r*$top*-1.0000,$r*$top*0.0000 $r*$top*0.9*-0.9877,$r*$top*0.9*-0.1564 $r*$top*-0.9511,$r*$top*-0.3090 $r*$top*0.9*-0.8910,$r*$top*0.9*-0.4540 $r*$top*-0.8090,$r*$top*-0.5878 $r*$top*0.9*-0.7071,$r*$top*0.9*-0.7071 $r*$top*-0.5878,$r*$top*-0.8090 $r*$top*0.9*-0.4540,$r*$top*0.9*-0.8910 $r*$top*-0.3090,$r*$top*-0.9511 $r*$top*0.9*-0.1564,$r*$top*0.9*-0.9877 $r*$top*-0.0000,$r*$top*-1.0000 $r*$top*0.9*0.1564,$r*$top*0.9*-0.9877 $r*$top*0.3090,$r*$top*-0.9511 $r*$top*0.9*0.4540,$r*$top*0.9*-0.8910 $r*$top*0.5878,$r*$top*-0.8090 $r*$top*0.9*0.7071,$r*$top*0.9*-0.7071 $r*$top*0.8090,$r*$top*-0.5878 $r*$top*0.9*0.8910,$r*$top*0.9*-0.4540 $r*$top*0.9511,$r*$top*-0.3090 $r*$top*0.9*0.9877,$r*$top*0.9*-0.1564
  else
    lathe $r,0 $r,$h*0.33 $r*(0.5+$top*0.5),$h*0.7 $r*$top,$h 0,$h
  end
end
define order.base kind=doric r=0.3   # an Attic base, one module tall: plinth, torus, scotia between fillets, torus
  let m $r
  box $m*2.4 $m*0.33 $m*2.4
  lathe $m*1.12,0 $m*1.2,$m*0.1 $m*1.12,$m*0.2 $m*1.04,$m*0.24 $m*0.98,$m*0.29 $m*0.96,$m*0.36 $m*0.98,$m*0.43 $m*1.04,$m*0.5 $m*1.04,$m*0.53 $m*1.1,$m*0.6 $m*1.08,$m*0.65 $m*1.02,$m*0.67 $m*0.98,$m*0.67 0,$m*0.67 at=0,$m*0.33,0 sides=24
end
define order.leaf h=0.3 w=0.12   # an acanthus leaf: a blade standing against the bell at the origin, curling out at its tip, facing +z
  let sd max(3,round(6*$detail))
  sphere 1 scale=$w,$h*0.55,$w*0.35 at=0,$h*0.55,$w*0.2 sides=$sd
  sphere 1 scale=$w*0.9,$h*0.3,$w*0.5 at=0,$h*0.9,$w*0.75 rot=-50,0,0 sides=$sd
end
define order.volute r=0.2 t=0.05   # an Ionic volute: a spiral of two and a half turns in to its eye at the origin, in the plane facing +z
  tube $t*0.45 $r,0,0 $r*0.7,$r*0.7,0 0,$r*0.95,0 -$r*0.63,$r*0.6,0 -$r*0.8,0,0 -$r*0.5,-$r*0.5,0 0,-$r*0.64,0 $r*0.4,-$r*0.4,0 $r*0.52,0,0 $r*0.32,$r*0.34,0 0,$r*0.38,0 -$r*0.22,$r*0.12,0 -$r*0.16,-$r*0.12,0 0,-$r*0.18,0 $r*0.1,0,0 sides=6
  sphere $t*0.6 sides=6
end
define order.capital kind=doric r=0.3   # the capital, cap modules tall: Tuscan and Doric an echinus under a square abacus; Ionic a cushion with a volute to either side; Corinthian a bell of two rows of acanthus under a hollowed abacus; Composite the two together
  let m $r
  let k $${kind}_k
  let ch $${kind}_cap*$m
  let top $${kind}_top
  if $k<=1
    cyl $m*$top*1.02 $ch*0.1 at=0,0,0 sides=24
    cyl $m*$top $ch*0.3 at=0,$ch*0.1,0 sides=24
    cyl $m*$top*1.08 $ch*0.05 at=0,$ch*0.38,0 sides=24
    cyl $m*$top*1.08 $ch*0.05 at=0,$ch*0.44,0 sides=24
    lathe $m*$top*0.95,0 $m*1.12,$ch*0.08 $m*1.22,$ch*0.2 $m*1.25,$ch*0.3 0,$ch*0.3 at=0,$ch*0.46,0 sides=24
    box $m*2.6 $ch*0.24 $m*2.6 at=0,$ch*0.76,0
  end
  if $k==2
    cyl $m*$top*1.02 $ch*0.08 at=0,0,0 sides=24
    lathe $m*$top*0.95,0 $m*1.05,$ch*0.12 $m*1.14,$ch*0.26 $m*1.16,$ch*0.3 0,$ch*0.3 at=0,$ch*0.08,0 sides=24
    radial n=16
      sphere $m*0.085 at=$m*1.1,$ch*0.26,0 scale=0.7,1,0.5 sides=4
    end
    box $m*2.3 $ch*0.3 $m*1.3 at=0,$ch*0.36,0
    cyl $m*0.3 $m*2.3 rot=0,0,90 centre=1 at=0,$ch*0.56,0 sides=16
    mirror x
      order.volute $m*0.38 $m*0.14 at=$m*1.15,$ch*0.42,$m*0.7
      order.volute $m*0.38 $m*0.14 at=$m*1.15,$ch*0.42,-$m*0.7 rot=0,180,0
      box $m*0.2 $ch*0.5 $m*0.5 at=$m*1.1,$ch*0.3,0
    end
    box $m*2.5 $ch*0.14 $m*2.5 at=0,$ch*0.66,0
    mould.reversa $m*2.5 $ch*0.2 $m*0.12 at=0,$ch*0.8,$m*1.25
    mould.reversa $m*2.5 $ch*0.2 $m*0.12 at=0,$ch*0.8,-$m*1.25 rot=0,180,0
    mould.reversa $m*2.5 $ch*0.2 $m*0.12 at=$m*1.25,$ch*0.8,0 rot=0,-90,0
    mould.reversa $m*2.5 $ch*0.2 $m*0.12 at=-$m*1.25,$ch*0.8,0 rot=0,90,0
  end
  if $k>=3
    cyl $m*$top*1.02 $ch*0.04 at=0,0,0 sides=24
    lathe $m*$top*0.95,0 $m*0.9,$ch*0.3 $m*1.0,$ch*0.6 $m*1.2,$ch*0.8 $m*1.3,$ch*0.86 0,$ch*0.86 at=0,$ch*0.04,0 sides=24
    radial n=8
      order.leaf $ch*0.36 $m*0.42 at=0,$ch*0.04,$m*0.86
    end
    radial n=8
      order.leaf $ch*0.36 $m*0.42 at=0,$ch*0.34,$m*0.95 rot=0,22.5,0
    end
    if $k==4
      radial n=4
        order.volute $m*0.3 $m*0.1 at=$m*1.05,$ch*0.74,$m*1.05 rot=0,-45,0
      end
    else
      radial n=4
        tube $m*0.05 $m*1.0,$ch*0.56,$m*0.6 $m*1.15,$ch*0.68,$m*0.9 $m*1.2,$ch*0.78,$m*1.15 $m*1.1,$ch*0.8,$m*1.3 sides=5
        tube $m*0.05 $m*0.6,$ch*0.56,$m*1.0 $m*0.9,$ch*0.68,$m*1.15 $m*1.15,$ch*0.78,$m*1.2 $m*1.3,$ch*0.8,$m*1.1 sides=5
      end
    end
    box $m*2.4 $ch*0.14 $m*2.4 at=0,$ch*0.86,0
    radial n=4
      sphere $m*0.12 at=0,$ch*0.86,$m*1.35 scale=1,0.8,0.4 sides=6
    end
  end
end
define order.column kind=doric h=5 fluted=0   # a column of the order, h tall with its base and capital: its module is h over the order's number
  let m $h/$${kind}_n
  let ch $${kind}_cap*$m
  order.base $kind $m
  order.shaft $h-$m-$ch $m $${kind}_top $fluted at=0,$m,0
  order.capital $kind $m at=0,$h-$ch,0
end
define order.pilaster kind=doric h=5 w=0.5   # the order flattened against the wall z = 0, facing +z: base, a shaft drawing in, a capital of the order's mouldings
  let m $w/2
  let ch $${kind}_cap*$m
  let top $${kind}_top
  box $w*1.2 $m*0.33 $m*0.5 at=0,0,$m*0.25
  mould.torus $w*1.2 $m*0.3 at=0,$m*0.33,$m*0.5
  box $w*1.1 $m*0.37 $m*0.45 at=0,$m*0.63,$m*0.225
  loft $h-$m-$ch -$m,-0.001 $m,-0.001 $m,$m*0.5 -$m,$m*0.5 / -$m*0.99,-0.001 $m*0.99,-0.001 $m*0.99,$m*0.5 -$m*0.99,$m*0.5 / -$m*$top,-0.001 $m*$top,-0.001 $m*$top,$m*0.5 -$m*$top,$m*0.5 at=0,$m,0
  if $${kind}_k<=2
    mould.fillet $w*$top*1.02 $ch*0.3 $m*0.5 at=0,$h-$ch,0
    mould.ovolo $w*1.2 $ch*0.3 $m*0.6 at=0,$h-$ch*0.7,0
    box $w*1.5 $ch*0.3 $m*0.65 at=0,$h-$ch*0.4,$m*0.325
    mould.reversa $w*1.5 $ch*0.1 $m*0.12 at=0,$h-$ch*0.1,$m*0.65
  else
    box $w*$top $ch*0.86 $m*0.5 at=0,$h-$ch,$m*0.25
    for i 3
      order.leaf $ch*0.36 $m*0.42 at=-$m*0.5+$i*$m*0.5,$h-$ch*0.96,$m*0.5
    end
    for i 2
      order.leaf $ch*0.36 $m*0.42 at=-$m*0.25+$i*$m*0.5,$h-$ch*0.66,$m*0.56
    end
    box $w*1.3 $ch*0.14 $m*0.7 at=0,$h-$ch*0.14,$m*0.35
  end
end
define order.triglyph h=0.6 w=0.4 d=0.06   # a Doric triglyph: three bars (two channels between, a half channel each side), six guttae hanging under it
  box $w*0.14 $h $d at=-$w*0.28,0,$d/2
  box $w*0.14 $h $d at=$w*0.28,0,$d/2
  box $w*0.14 $h $d at=0,0,$d/2
  box $w*1.1 $h*0.06 $d*1.4 at=0,-$h*0.06,$d*0.7
  for i 6
    cone $w*0.05 $w*0.07 $h*0.08 at=-$w*0.4+$i*$w*0.16,-$h*0.14,$d*0.9 sides=4
  end
end
define order.entablature kind=doric len=6 ent=1.25   # architrave, frieze and cornice, ent tall (a quarter of the column): Tuscan plain; Doric triglyphs over the taenia; Ionic a dentil cornice over a pulvinated frieze; Corinthian and Composite modillions
  let k $${kind}_k
  let a $ent*0.28
  let f $ent*0.33
  let c $ent*0.39
  box $len $a+$f 0.2 at=0,0,-0.1
  if $k<=1
    mould.fillet $len $a*0.86 0.06
    mould.fillet $len $a*0.14 0.1 at=0,$a*0.86,0
  else
    mould.architrave $len $a 0.14
  end
  if $k==1
    let step $ent*0.5
    let n max(2,round($len/$step))
    mould.fillet $len $f 0.02 at=0,$a,0
    array n=$n step=$len/$n,0,0 at=-$len/2+$len/$n/2,$a+$f*0.08,0.02
      order.triglyph $f*0.92 $f*0.55 $f*0.14
    end
    mould.cornice $len $c 0.9*$c dentils=0 at=0,$a+$f,0
  end
  if $k==0
    mould.fillet $len $f 0.02 at=0,$a,0
    mould.cornice $len $c 0.9*$c dentils=0 at=0,$a+$f,0
  end
  if $k==2
    sweep 0,0 -$f*0.2,$f*0.2 -$f*0.3,$f*0.5 -$f*0.2,$f*0.8 0,$f / -$len/2,0,0 $len/2,0,0 at=0,$a,0
    mould.cornice $len $c 0.9*$c dentils=1 at=0,$a+$f,0
  end
  if $k>=3
    mould.fillet $len $f 0.02 at=0,$a,0
    mould.cornice $len $c 1.0*$c modillions=1 at=0,$a+$f,0
  end
end
define order.pedestal kind=doric h=1.5 w=0.9   # plinth, die and cap, a third of the column it carries
  box $w*1.3 $h*0.12 $w*1.3
  mould.reversa $w*1.3 $h*0.08 $w*0.12 at=0,$h*0.12,$w*0.65
  mould.reversa $w*1.3 $h*0.08 $w*0.12 at=0,$h*0.12,-$w*0.65 rot=0,180,0
  mould.reversa $w*1.3 $h*0.08 $w*0.12 at=$w*0.65,$h*0.12,0 rot=0,-90,0
  mould.reversa $w*1.3 $h*0.08 $w*0.12 at=-$w*0.65,$h*0.12,0 rot=0,90,0
  box $w $h*0.68 $w at=0,$h*0.2,0
  box $w*1.2 $h*0.06 $w*1.2 at=0,$h*0.88,0
  mould.cyma $w*1.3 $h*0.06 $w*0.05 at=0,$h*0.94,$w*0.6
  mould.cyma $w*1.3 $h*0.06 $w*0.05 at=0,$h*0.94,-$w*0.6 rot=0,180,0
  mould.cyma $w*1.3 $h*0.06 $w*0.05 at=$w*0.6,$h*0.94,0 rot=0,-90,0
  mould.cyma $w*1.3 $h*0.06 $w*0.05 at=-$w*0.6,$h*0.94,0 rot=0,90,0
  box $w*1.2 $h*0.06 $w*1.2 at=0,$h*0.94,0
end
define order.pediment w=6 h=1.2 d=0.6 seg=0 c=0.3   # a pediment over a span w: its tympanum set back, and the cornice c tall swept up each slope to the apex and mitred there (seg=1: swept along a segmental arc instead)
  let p $c*0.8
  if $seg
    extrude $d $w/2*-1.0000,$h*0.0000 $w/2*-0.9659,$h*0.2588 $w/2*-0.8660,$h*0.5000 $w/2*-0.7071,$h*0.7071 $w/2*-0.5000,$h*0.8660 $w/2*-0.2588,$h*0.9659 $w/2*0.0000,$h*1.0000 $w/2*0.2588,$h*0.9659 $w/2*0.5000,$h*0.8660 $w/2*0.7071,$h*0.7071 $w/2*0.8660,$h*0.5000 $w/2*0.9659,$h*0.2588 $w/2*1.0000,$h*0.0000 at=0,0,-$d/2
    box $w $c*0.6 $d*0.4 at=0,0,-$d*0.2
    sweep 0,0 -$p*0.3,0 -$p*0.3,$c*0.1 -$p*0.55,$c*0.15 -$p*0.6,$c*0.5 -$p*0.8,$c*0.55 -$p*0.95,$c*0.8 -$p,$c 0,$c / $w/2*-1.0000,$h*0.0000,0 $w/2*-0.9659,$h*0.2588,0 $w/2*-0.8660,$h*0.5000,0 $w/2*-0.7071,$h*0.7071,0 $w/2*-0.5000,$h*0.8660,0 $w/2*-0.2588,$h*0.9659,0 $w/2*0.0000,$h*1.0000,0 $w/2*0.2588,$h*0.9659,0 $w/2*0.5000,$h*0.8660,0 $w/2*0.7071,$h*0.7071,0 $w/2*0.8660,$h*0.5000,0 $w/2*0.9659,$h*0.2588,0 $w/2*1.0000,$h*0.0000,0
  else
    extrude $d -$w/2,0 $w/2,0 0,$h at=0,0,-$d/2
    let e $c*0.8*$w/2/hypot($w/2,$h)
    sweep 0,0 -$p*0.3,0 -$p*0.3,$c*0.1 -$p*0.55,$c*0.15 -$p*0.6,$c*0.5 -$p*0.8,$c*0.55 -$p*0.95,$c*0.8 -$p,$c 0,$c / -$w/2-$e,-$e*$h/($w/2),0 0,$h,0 $w/2+$e,-$e*$h/($w/2),0
  end
end
define order.colonnade kind=doric len=12 n=6 h=5 fluted=0   # columns in a row along x under their entablature
  let step $len/($n-1)
  let m $h/$${kind}_n
  array n=$n step=$step,0,0 at=-$len/2,0,0
    order.column $kind $h $fluted
  end
  order.entablature $kind $len+$m*2.6 $h/4 at=0,$h,$m*1.3
end
define order.portico kind=ionic w=12 d=20 n=6 h=6 fluted=0   # a temple: three steps, n columns across the front and back and as many as fit down the sides, the cella within, the entablature round, a pediment front and back, a roof of tiles
  let m $h/$${kind}_n
  let sn max(3,round($n*$d/$w))
  let ent $h/4
  let steph $m*0.5
  for i 3
    box $w+$m*8-$i*$m*2 $steph $d+$m*8-$i*$m*2 at=0,$i*$steph,0 mat=stone
  end
  let y $steph*3
  let iw $w-$m*2
  let id $d-$m*2
  array n=$n step=$iw/($n-1),0,0 at=-$iw/2,$y,$id/2
    order.column $kind $h $fluted
  end
  array n=$n step=$iw/($n-1),0,0 at=-$iw/2,$y,-$id/2
    order.column $kind $h $fluted
  end
  array n=$sn-2 step=0,0,$id/($sn-1) at=$iw/2,$y,-$id/2+$id/($sn-1)
    order.column $kind $h $fluted
  end
  array n=$sn-2 step=0,0,$id/($sn-1) at=-$iw/2,$y,-$id/2+$id/($sn-1)
    order.column $kind $h $fluted
  end
  box $w-$m*4 $h $d-$m*4 at=0,$y,0 mat=plaster
  order.entablature $kind $w+$m*1.4 $ent at=0,$y+$h,$d/2+$m*0.7
  order.entablature $kind $w+$m*1.4 $ent at=0,$y+$h,-$d/2-$m*0.7 rot=0,180,0
  order.entablature $kind $d+$m*1.4 $ent at=$w/2+$m*0.7,$y+$h,0 rot=0,-90,0
  order.entablature $kind $d+$m*1.4 $ent at=-$w/2-$m*0.7,$y+$h,0 rot=0,90,0
  box $w+$m*1.4 $ent*0.92 $d+$m*1.4 at=0,$y+$h,0
  let pw $w+$m*1.4+$ent*1.6
  let ph $pw*0.18
  order.pediment $pw $ph $ent*1.7 c=$ent*0.39 at=0,$y+$h+$ent*0.92,$d/2+$m*0.7
  order.pediment $pw $ph $ent*1.7 c=$ent*0.39 at=0,$y+$h+$ent*0.92,-$d/2-$m*0.7 rot=0,180,0
  extrude $d+$m*1.4 -$pw/2,0 $pw/2,0 0,$ph at=0,$y+$h+$ent*0.92,0 mat=rooftiles
end
define order.aedicule kind=ionic w=1.2 h=2.2 seg=0 pediment=1 d=0.16   # a surround for an opening w by h whose foot is on y = 0: a sill on consoles, a pilaster either side, an entablature, and a pediment over it
  let pw max(0.16,$w*0.18)
  let m $pw/2
  let ent $h*0.19
  mould.fillet $w+$pw*2.6 $m*0.6 $d*1.6 at=0,-$m*0.6,0
  if $detail>0.5
    mould.console $m*1.6 $d*1.6 at=-$w/2-$pw*0.8,-$m*0.6-$m*1.6,0
    mould.console $m*1.6 $d*1.6 at=$w/2+$pw*0.8,-$m*0.6-$m*1.6,0
  end
  order.pilaster $kind $h $pw at=-$w/2-$pw*0.6,0,0
  order.pilaster $kind $h $pw at=$w/2+$pw*0.6,0,0
  mould.frame $w $h $pw*0.5 $d*0.5
  order.entablature $kind $w+$pw*2.4 $ent at=0,$h,$d
  if $pediment
    order.pediment $w+$pw*2.4+$ent*0.6 ($w+$pw*2.4)*0.22 $d+$ent*0.4 seg=$seg c=$ent*0.42 at=0,$h+$ent*0.95,$d/2+$ent*0.2
  end
end
define order.arcade kind=doric len=12 h=6 n=4 fluted=0   # an arcade along x facing +z: piers carrying round arches (an archivolt round each, a keystone at the crown, an impost where it springs), a column of the order engaged in each pier, and the entablature over all
  let bay $len/$n
  let ent $h/4
  let ch $h-$ent
  let m $ch/$${kind}_n
  let pw max($bay*0.2,$m*2.6)
  let aw $bay-$pw
  let spring $ch-$aw/2-$pw*0.5
  for i $n+1
    let x -$len/2+$bay*$i
    box $pw $spring $pw*1.2 at=$x,0,-$pw*0.6
    mould.stringcourse $pw*1.3 $pw*0.3 $pw*0.12 at=$x,$spring-$pw*0.3,0
    order.column $kind $ch $fluted at=$x,0,-$m*0.1
  end
  for i $n
    let x -$len/2+$bay*($i+0.5)
    order.archband $aw/2 $pw*0.3 $pw*1.2 at=$x,$spring,-$pw*0.6
    order.archband $aw/2+$pw*0.3 $pw*0.14 $pw*1.3 at=$x,$spring,-$pw*0.6
    box $aw+$pw $ch-$spring-$aw/2-$pw*0.44 $pw*1.2 at=$x,$spring+$aw/2+$pw*0.44,-$pw*0.6
    box $aw*0.5+$pw*0.5 $ch-$spring-$aw/2+0.02 $pw*1.2 at=$x-$aw/4-$pw/4,$spring+$aw/2-0.02,-$pw*0.6
    box $aw*0.5+$pw*0.5 $ch-$spring-$aw/2+0.02 $pw*1.2 at=$x+$aw/4+$pw/4,$spring+$aw/2-0.02,-$pw*0.6
    mould.keystone $pw*0.5 $pw*0.7 $pw*0.2 at=$x,$spring+$aw/2-$pw*0.05,$pw*0.05
  end
  order.entablature $kind $len+$pw $ent at=0,$ch,$pw*0.05
end
)LIB";
}

}  // namespace sg::sculpt
