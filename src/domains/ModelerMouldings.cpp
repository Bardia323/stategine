// Mouldings: the profiles every tradition runs along its walls, as the
// handbooks draw them - each a curve struck with compasses, swept along its
// run (exact: no field, a few faces a metre). `use mould`.
//
// A running moulding lies along x with its back on the plane z = 0, standing
// out toward +z by its projection `p`, its foot on y = 0 and its top at `h`:
//
//   mould.fillet len h p        a flat band
//   mould.ovolo len h p         a convex quarter round (an echinus)
//   mould.cavetto len h p       a concave quarter round (a cove)
//   mould.cyma len h p          cyma recta: hollow below, round above - the crown
//   mould.reversa len h p       cyma reversa: round below, hollow above - a bed mould
//   mould.torus len h           a half round, h tall (p = h/2)
//   mould.scotia len h p        a deep hollow between two fillets
//   mould.astragal len h        a bead with a fillet over it
//   mould.run len h p kind=cyma any of them by name
//
// and what is built of them:
//
//   mould.cornice len h p dentils=1 modillions=0   bed mould, dentil course, ovolo, corona, cyma, fillet
//   mould.stringcourse len h p                     a course: fillet, cyma reversa, fillet
//   mould.architrave len h p                       three fascias stepping out under a cyma reversa
//   mould.dentils len h p step                     a row of dentils
//   mould.modillions len h p step                  a row of scrolled brackets
//   mould.frame w h band p                         an architrave round an opening w by h (foot on y = 0)
//   mould.panel w h band p sunk=0                  a raised (or sunk) panel in its frame, upright, facing +z
//   mould.baluster h r kind=0                      a turned baluster (0: vase, 1: double, 2: square)
//   mould.balustrade len h step                    a plinth rail, balusters, a moulded handrail
//   mould.rustication len h p course chamfer       courses of chamfered blocks on a wall's face
//   mould.quoins h size p                          alternating long and short blocks up a corner at x = 0
//   mould.keystone w h p                           a wedge over an arch's crown
//   mould.console h d                              a scrolled bracket, facing +z, hanging from y = h
//   mould.urn h, mould.finial h, mould.ball r      what stands on a pier or a parapet
//
// Everything is placed as any shape is (at=, rot=, mat=). The profiles are
// quarter and half circles sampled at 22.5 degrees - enough for a moulding
// seen at arm's length, four faces a quarter.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_mould() {
    return R"LIB(
default detail 1
define mould.fillet len=1 h=0.1 p=0.1   # a flat band
  box $len $h $p at=0,0,$p/2
end
define mould.ovolo len=1 h=0.2 p=0.2   # convex quarter round: flush at its foot, standing out at its top
  sweep 0,0 -$p*0.3827,$h*0.0761 -$p*0.7071,$h*0.2929 -$p*0.9239,$h*0.6173 -$p,$h 0,$h / -$len/2,0,0 $len/2,0,0
end
define mould.cavetto len=1 h=0.2 p=0.2   # concave quarter round: rising from its foot, flaring out to its top
  sweep 0,0 -$p*0.0761,$h*0.3827 -$p*0.2929,$h*0.7071 -$p*0.6173,$h*0.9239 -$p,$h 0,$h / -$len/2,0,0 $len/2,0,0
end
define mould.cyma len=1 h=0.3 p=0.3   # cyma recta: a hollow below, a round above - the crown of a cornice
  sweep 0,0 -$p*0.038,$h*0.1913 -$p*0.1464,$h*0.3536 -$p*0.3087,$h*0.4619 -$p/2,$h/2 -$p*0.6913,$h*0.5381 -$p*0.8536,$h*0.6464 -$p*0.962,$h*0.8087 -$p,$h 0,$h / -$len/2,0,0 $len/2,0,0
end
define mould.reversa len=1 h=0.2 p=0.2   # cyma reversa: a round below, a hollow above - a bed mould, a capping
  sweep 0,0 -$p*0.1913,$h*0.038 -$p*0.3536,$h*0.1464 -$p*0.4619,$h*0.3087 -$p/2,$h/2 -$p*0.5381,$h*0.6913 -$p*0.6464,$h*0.8536 -$p*0.8087,$h*0.962 -$p,$h 0,$h / -$len/2,0,0 $len/2,0,0
end
define mould.torus len=1 h=0.2   # a half round, h tall
  let r $h/2
  sweep 0,0 -$r*0.7071,$r*0.2929 -$r,$r -$r*0.7071,$r*1.7071 0,$h / -$len/2,0,0 $len/2,0,0
end
define mould.scotia len=1 h=0.2 p=0.12   # a deep hollow between two fillets, each p out
  let r $h/2
  sweep 0,0 -$p,0 -$p+$r*0.7071,$r*0.2929 -$p+$r,$r -$p+$r*0.7071,$r*1.7071 -$p,$h 0,$h / -$len/2,0,0 $len/2,0,0
end
define mould.astragal len=1 h=0.12   # a bead, a fillet over it
  mould.torus $len $h*0.7
  mould.fillet $len $h*0.3 $h*0.3 at=0,$h*0.7,0
end
define mould.run len=1 h=0.2 p=0.2 kind=cyma   # a moulding by name
  mould.$kind $len $h $p
end
define mould.dentils len=1 h=0.15 p=0.12 step=0.24   # a row of dentils, each a block, half a step apart
  let step max($step,0.08/$detail)
  let n max(1,floor($len/$step))
  array n=$n step=$step,0,0 at=-($n-1)*$step/2,0,0
    box $step*0.55 $h $p at=0,0,$p/2
  end
end
define mould.modillions len=1 h=0.2 p=0.35 step=0.5   # scrolled brackets under a corona, a leaf under each
  let step max($step,0.25/$detail)
  let n max(1,floor($len/$step))
  array n=$n step=$step,0,0 at=-($n-1)*$step/2,0,0
    box $step*0.4 $h*0.5 $p at=0,$h*0.5,$p/2
    mould.reversa $step*0.4 $h*0.5 $p*0.85 at=0,0,0
    cyl $h*0.22 $step*0.4 rot=0,0,90 centre=1 at=0,$h*0.25,$p*0.9 sides=8
  end
end
define mould.cornice len=1 h=1 p=0.9 dentils=1 modillions=0   # a full cornice, Vignola's parts: bed mould, dentils or modillions, ovolo, corona, cyma recta, fillet - projecting as it rises
  box $len $h*0.74 $p*0.2 at=0,0,$p*0.1
  mould.reversa $len $h*0.1 $p*0.14 at=0,0,$p*0.2
  if $modillions
    box $len $h*0.2 $p*0.34 at=0,$h*0.1,$p*0.17
    mould.modillions $len $h*0.2 $p*0.45 $p*0.6 at=0,$h*0.1,$p*0.34
  else
    box $len $h*0.2 $p*0.3 at=0,$h*0.1,$p*0.15
    if $dentils
      mould.dentils $len $h*0.18 $p*0.14 $h*0.24 at=0,$h*0.11,$p*0.3
    end
  end
  mould.ovolo $len $h*0.1 $p*0.14 at=0,$h*0.3,$p*0.44
  box $len $h*0.26 $p*0.78 at=0,$h*0.4,$p*0.39
  mould.cyma $len $h*0.26 $p*0.2 at=0,$h*0.66,$p*0.78
  box $len $h*0.08 $p at=0,$h*0.92,$p/2
end
define mould.stringcourse len=1 h=0.2 p=0.15   # a course where a floor is: a fillet, a cyma reversa, a fillet
  mould.fillet $len $h*0.25 $p*0.6
  mould.reversa $len $h*0.5 $p*0.4 at=0,$h*0.25,$p*0.6
  mould.fillet $len $h*0.25 $p at=0,$h*0.75,0
end
define mould.architrave len=1 h=0.4 p=0.2   # three fascias, each standing out a little past the one below, a cyma reversa over them
  mould.fillet $len $h*0.28 $p*0.4
  mould.fillet $len $h*0.3 $p*0.6 at=0,$h*0.28,0
  mould.fillet $len $h*0.3 $p*0.8 at=0,$h*0.58,0
  mould.reversa $len $h*0.12 $p*0.2 at=0,$h*0.88,$p*0.8
end
define mould.frame w=1 h=2 band=0.18 p=0.08   # an architrave round an opening w by h whose foot is on y = 0: two jambs and a head, each stepping out toward the opening's edge
  group
    box $band $h+$band 0.4*$p at=-$w/2-$band/2,0,0.2*$p
    box $band*0.7 $h+$band*0.7 0.7*$p at=-$w/2-$band*0.35,0,0.35*$p
    box $band*0.4 $h+$band*0.4 $p at=-$w/2-$band*0.2,0,$p/2
  end
  mirror x
    box $band $h+$band 0.4*$p at=-$w/2-$band/2,0,0.2*$p
    box $band*0.7 $h+$band*0.7 0.7*$p at=-$w/2-$band*0.35,0,0.35*$p
    box $band*0.4 $h+$band*0.4 $p at=-$w/2-$band*0.2,0,$p/2
  end
  box $w+$band*2 $band 0.4*$p at=0,$h,0.2*$p
  box $w+$band*1.4 $band*0.7 0.7*$p at=0,$h,0.35*$p
  box $w+$band*0.8 $band*0.4 $p at=0,$h,$p/2
end
define mould.panel w=1 h=1.5 band=0.08 p=0.04 sunk=0   # a panel w by h, foot on y = 0, facing +z: a moulded frame round a field, the field raised (or sunk) within it
  if $sunk
    box $w+$band*2 $h+$band*2 $p*0.5 at=0,-$band,$p*0.25
    mould.reversa $w+$band*2 $band $p*0.5 at=0,-$band,$p*0.5
    mould.reversa $w+$band*2 $band $p*0.5 at=0,$h+$band,$p*0.5 rot=0,0,180
    mould.reversa $h+$band*2 $band $p*0.5 at=-$w/2-$band,$h/2,$p*0.5 rot=0,0,-90
    mould.reversa $h+$band*2 $band $p*0.5 at=$w/2+$band,$h/2,$p*0.5 rot=0,0,90
  else
    box $w+$band*2 $h+$band*2 $p*0.3 at=0,-$band,$p*0.15
    box $w $h $p*0.6 at=0,0,$p*0.3
    mould.reversa $w $band*0.6 $p*0.4 at=0,-$band*0.6,$p*0.6
    mould.reversa $w $band*0.6 $p*0.4 at=0,$h+$band*0.6,$p*0.6 rot=0,0,180
    mould.reversa $h $band*0.6 $p*0.4 at=-$w/2-$band*0.6,$h/2,$p*0.6 rot=0,0,-90
    mould.reversa $h $band*0.6 $p*0.4 at=$w/2+$band*0.6,$h/2,$p*0.6 rot=0,0,90
    box $w*0.84 $h*0.84 $p at=0,$h*0.08,$p/2
  end
end
define mould.baluster h=0.9 r=0.08 kind=0   # a turned baluster on its plinth: a vase (0), a double vase (1), or a square one (2); square too where detail is low
  let sd max(4,round(8*$detail))
  if ($kind==2)+($detail<0.4)
    box $r*2 $h*0.1 $r*2 mat=stone
    box $r*1.4 $h*0.8 $r*1.4 at=0,$h*0.1,0
    box $r*2 $h*0.1 $r*2 at=0,$h*0.9,0
  else
    if $kind==1
      lathe 0,0 $r*1.1,0 $r*1.1,$h*0.06 $r*0.6,$h*0.12 $r,$h*0.3 $r*0.55,$h*0.5 $r,$h*0.7 $r*0.6,$h*0.88 $r*1.1,$h*0.94 $r*1.1,$h 0,$h sides=$sd
    else
      lathe 0,0 $r*1.2,0 $r*1.2,$h*0.07 $r*0.75,$h*0.13 $r*1.05,$h*0.24 $r*0.7,$h*0.52 $r*0.45,$h*0.8 $r*0.7,$h*0.87 $r*1.1,$h*0.93 $r*1.1,$h 0,$h sides=$sd
    end
  end
end
define mould.balustrade len=4 h=1.0 step=0.32 kind=0   # a balustrade along x: a plinth rail, balusters a step apart, a moulded handrail; a die at each end
  mould.fillet $len $h*0.12 0.26 at=0,0,-0.13
  let step max($step,0.3/$detail)
  let n max(2,floor(($len-0.4)/$step))
  array n=$n step=$step,0,0 at=-($n-1)*$step/2,$h*0.12,0
    mould.baluster $h*0.7 $h*0.085 $kind
  end
  box 0.26 $h*0.82 0.26 at=-$len/2+0.13,0,0
  box 0.26 $h*0.82 0.26 at=$len/2-0.13,0,0
  mould.reversa $len 0.05 0.03 at=0,$h*0.82,-0.14
  mould.reversa $len 0.05 0.03 at=0,$h*0.82,0.14 rot=0,180,0
  box $len $h*0.18-0.05 0.3 at=0,$h*0.82+0.05,0
  mould.torus $len 0.06 at=0,$h-0.06,-0.15
  mould.torus $len 0.06 at=0,$h-0.06,0.15 rot=0,180,0
end
define mould.rustication len=6 h=2 p=0.1 course=0.45 chamfer=0.035   # courses of blocks on a wall's face, every joint a V: their chamfered edges catch the light
  let n max(1,round($h/$course))
  let ch $h/$n
  for i $n
    let odd mod($i,2)
    let bw min($len,$ch*2.1)
    let m max(1,round($len/$bw))
    let bw $len/$m
    for j $m
      let x -$len/2+$bw*($j+0.5)+$odd*$bw/2
      if $x+$bw/2<=$len/2+0.001
        extrude $p -$bw/2+0.004,0.004 $bw/2-0.004,0.004 $bw/2-0.004,$ch-0.004 -$bw/2+0.004,$ch-0.004 chamfer=$chamfer at=$x,$i*$ch,$p/2
      end
    end
    if $odd
      extrude $p -$bw/4+0.004,0.004 $bw/4-0.004,0.004 $bw/4-0.004,$ch-0.004 -$bw/4+0.004,$ch-0.004 chamfer=$chamfer at=-$len/2+$bw/4,$i*$ch,$p/2
      extrude $p -$bw/4+0.004,0.004 $bw/4-0.004,0.004 $bw/4-0.004,$ch-0.004 -$bw/4+0.004,$ch-0.004 chamfer=$chamfer at=$len/2-$bw/4,$i*$ch,$p/2
    end
  end
end
define mould.quoins h=6 size=0.45 p=0.06   # dressed blocks up the corner at the origin (the wall faces +z and runs to -x): long and short alternately
  let n max(1,round($h/$size))
  let c $h/$n
  for i $n
    let long mod($i,2)
    box $c*(1.2+$long*0.8)+0.02 $c-0.02 $p at=-($c*(1.2+$long*0.8))/2+0.01,$i*$c+0.01,$p/2
    box $p $c-0.02 $c*(2.0-$long*0.8)+0.02 at=$p/2,$i*$c+0.01,-($c*(2.0-$long*0.8))/2+0.01
  end
end
define mould.keystone w=0.3 h=0.5 p=0.1   # a wedge standing proud over an arch's crown: narrow below, wide above, its foot on y = 0, facing +z
  extrude $p -$w*0.36,0 $w*0.36,0 $w/2,$h -$w/2,$h at=0,0,$p/2
end
define mould.console h=0.5 d=0.3   # a scrolled bracket (an S of two volutes), hanging from y = h against the wall z = 0, facing +z, carrying what rests on it at y = h
  extrude $d*0.5 0,0 $d*0.3,$h*0.1 $d*0.55,$h*0.35 $d*0.85,$h*0.7 $d,$h*0.92 $d,$h 0,$h at=0,0,$d*0.25 rot=0,-90,0
  cyl $d*0.14 $d*0.5 rot=0,0,90 centre=1 at=0,$h*0.82,$d*0.9 sides=10
  cyl $d*0.08 $d*0.5 rot=0,0,90 centre=1 at=0,$h*0.12,$d*0.22 sides=8
  box $d*0.5 $h*0.08 $d*1.1 at=0,$h*0.92,$d*0.55
end
define mould.urn h=1 r=0.3   # an urn on a square foot: a turned body, a lid, a knob
  box $r*2 $h*0.08 $r*2
  lathe 0,0 $r*0.6,0 $r*0.6,$h*0.04 $r*0.35,$h*0.1 $r*0.45,$h*0.2 $r*0.9,$h*0.36 $r,$h*0.5 $r*0.8,$h*0.66 $r*0.95,$h*0.7 $r*0.7,$h*0.78 $r*0.35,$h*0.9 $r*0.18,$h*0.93 $r*0.18,$h*0.98 0,$h at=0,$h*0.08,0 sides=18
end
define mould.finial h=0.6 r=0.12   # a tapering finial: a knop, a ball, a point
  lathe 0,0 $r,0 $r,$h*0.1 $r*0.6,$h*0.2 $r*0.9,$h*0.45 $r*0.6,$h*0.62 $r*0.25,$h*0.7 $r*0.15,$h*0.85 0,$h sides=12
end
define mould.ball r=0.25   # a ball on a plinth
  box $r*2 $r*0.3 $r*2
  sphere $r at=0,$r*0.3,0 sides=16
end
)LIB";
}

}  // namespace sg::sculpt
