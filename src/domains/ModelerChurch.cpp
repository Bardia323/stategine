// What a church holds, in the modeller's own language: `use church`, beside
// the architect and a style. Each piece is its own model, facing +z and
// standing on y = 0 as the architect's words do; what is gothic about them
// (a pier) is said in the style's words where the style has one
// (`$style.column`), and in the library's own (`church.lancet`,
// `church.archline`) where it has none.
//
//   church.pew, pews           a pew; a block of them, row behind row
//   church.altar               a marble altar, its cloth, frontal, candles, cross
//   church.candlestick, cross  what stands on an altar
//   church.pulpit              an octagonal pulpit on a shaft, and its stair
//   church.font                a baptismal font
//   church.chandelier          a corona of candles hung on a chain
//   church.lights, rose        stained glass: two lancet lights; a rose window
//   church.organ               a pipe organ in its case
//   church.arcade              a hall church's piers and the arches they carry
//   church.redeemer, plinth    a statue of Christ, arms open in blessing; its pedestal
//
// The glass is made of panes, each of a coloured glass (`ruby` `cobalt`
// `amber` `emerald`), leaded (`lead`): whoever shows it says how each glass
// looks and lets it glow. Mouldings are swept, never cut, so they stay exact
// and cheap; the statue is one smooth surface made again from its field
// (`remesh`), its head finer than its robe.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_church() {
    return R"LIB(
define church.lancet w=1 h=2.5 d=1   # a pointed (equilateral) arch, solid: its foot on y = 0, through z
  let s $h-$w*0.866
  extrude $d -$w/2,0 $w/2,0 $w/2,$s $w/2-$w*cos(20),$s+$w*sin(20) $w/2-$w*cos(40),$s+$w*sin(40) 0,$s+$w*0.866 -$w/2+$w*cos(40),$s+$w*sin(40) -$w/2+$w*cos(20),$s+$w*sin(20) -$w/2,$s
end
define church.archline w=1 h=2.5 t=0.05 d=0.05   # a moulding round a pointed arch, its foot on y = 0: a bar t by d, swept up one side and down the other (exact, so cheap)
  let s $h-$w*0.866
  sweep -$t/2,0 $t/2,0 $t/2,$d -$t/2,$d / -$w/2,0,0 -$w/2,$s,0 $w/2-$w*cos(12),$s+$w*sin(12),0 $w/2-$w*cos(24),$s+$w*sin(24),0 $w/2-$w*cos(36),$s+$w*sin(36),0 $w/2-$w*cos(48),$s+$w*sin(48),0 0,$s+$w*0.866,0 -$w/2+$w*cos(48),$s+$w*sin(48),0 -$w/2+$w*cos(36),$s+$w*sin(36),0 -$w/2+$w*cos(24),$s+$w*sin(24),0 -$w/2+$w*cos(12),$s+$w*sin(12),0 $w/2,$s,0 $w/2,0,0
end
define church.candlestick h=0.6 candle=0.35   # a turned stick of gilt: foot, knops, a drip pan, and its candle
  lathe 0,0 0.11,0 0.11,0.02 0.08,0.04 0.05,0.07 0.025,0.1 0.022,$h*0.35 0.04,$h*0.38 0.04,$h*0.42 0.02,$h*0.45 0.018,$h*0.85 0.06,$h*0.9 0.06,$h*0.93 0,$h*0.93 sides=16 mat=gold
  cyl 0.022 $candle at=0,$h*0.93,0 sides=12 mat=wax
  cone 0.006 0 0.03 at=0,$h*0.93+$candle,0 sides=6 mat=wick
end
define church.cross h=0.8   # a standing cross on a stepped foot, gilt
  lathe 0,0 0.12,0 0.12,0.03 0.06,0.06 0.035,0.11 0,0.11 sides=16 mat=gold
  box 0.045 $h 0.035 at=0,0.11,0 mat=gold
  box $h*0.48 0.045 0.035 at=0,0.11+$h*0.66,0 mat=gold
  sphere 0.032 at=0,0.1+$h,0 sides=10 mat=gold
  sphere 0.032 at=$h*0.24+0.02,0.11+$h*0.66-0.01,0 sides=10 mat=gold
  sphere 0.032 at=-$h*0.24-0.02,0.11+$h*0.66-0.01,0 sides=10 mat=gold
end
define church.pew len=4   # a pew along x, facing +z: seat, raked back, carved ends with a poppy-head, a kneeler, a book ledge behind
  mirror x
    group at=$len/2-0.03,0,0
      extrude 0.07 -0.3,0 0.34,0 0.34,0.98 0.3,1.04 0.22,1.06 0.16,1.0 0.16,0.62 0.0,0.58 -0.2,0.52 -0.3,0.46 rot=0,90,0
      radial n=4 axis=x at=0,0.3,0.02
        torus 0.042 0.009 rot=0,0,90 centre=1 at=0,0.05,0 scale=2.2,1,1 sides=12
      end
      sphere 0.05 at=0,1.04,-0.25 sides=12
    end
  end
  box $len-0.1 0.045 0.44 at=0,0.43,0.04
  cyl 0.03 $len-0.1 rot=0,0,90 centre=1 at=0,0.452,0.26 sides=12
  group at=0,0.47,-0.22 rot=-8,0,0
    box $len-0.1 0.5 0.03
    box $len-0.1 0.07 0.06 at=0,0.48,0
    box $len-0.1 0.05 0.05 at=0,0.0,0
  end
  box $len-0.1 0.025 0.16 at=0,0.82,-0.36
  box $len-0.1 0.06 0.02 at=0,0.82,-0.44
  box $len-0.1 0.08 0.03 at=0,0.15,-0.1
  box $len-0.1 0.1 0.18 at=0,0.04,0.46
end
define church.pews rows=10 len=4 step=1.0   # a block of pews, row behind row along +z, all facing -z (toward the altar)
  for i $rows
    church.pew $len at=0,0,$i*$step rot=0,180,0 mat=wood
  end
end
define church.altar w=2.6 d=1.0 h=1.0   # a marble altar facing +z: a moulded base, blind arcading, an overhanging mensa, its cloth and frontal, a gradine with six candles and the cross
  box $w+0.2 0.12 $d+0.2 mat=marble
  box $w $h-0.2 $d at=0,0.12,0 mat=marble
  for i 5
    let x -$w/2+$w/5*($i+0.5)
    church.archline $w/5*0.7 $h*0.56 0.05 0.04 at=$x,0.2,$d/2 mat=marble
    cyl 0.025 $h*0.56-$w/5*0.6 at=-$w/2+$w/5*$i+0.04,0.2,$d/2+0.02 sides=8 mat=marble
  end
  box $w+0.16 0.08 $d+0.16 at=0,$h-0.08,0 mat=marble
  box $w+0.2 0.012 $d+0.2 at=0,$h,0 mat=linen
  box 0.012 0.38 $d+0.2 at=-$w/2-0.1,$h-0.37,0 mat=linen
  box 0.012 0.38 $d+0.2 at=$w/2+0.1,$h-0.37,0 mat=linen
  box $w+0.22 0.16 0.012 at=0,$h-0.15,$d/2+0.1 mat=linen
  box $w*0.42 $h*0.62 0.014 at=0,$h*0.22,$d/2+0.09 mat=frontal
  box $w*0.42 0.04 0.02 at=0,$h*0.22,$d/2+0.095 mat=gold
  box 0.05 $h*0.4 0.02 at=0,$h*0.3,$d/2+0.1 mat=gold
  box $h*0.26 0.05 0.02 at=0,$h*0.58,$d/2+0.1 mat=gold
  box $w*0.9 0.1 0.22 at=0,$h+0.012,-$d/2+0.13 mat=marble
  mirror x
    for i 3
      church.candlestick 0.62-0.08*$i at=0.42+$i*0.3,$h+0.112,-$d/2+0.13
    end
  end
  church.cross 0.9 at=0,$h+0.112,-$d/2+0.05
end
define church.pulpit h=1.5 r=0.62   # an octagonal pulpit of wood on a stone shaft: a corbelled foot, traceried panels, a desk, and its stair up from the -x side
  cyl 0.3 0.12 sides=8 mat=stone
  cyl 0.18 $h-0.4 sides=8 at=0,0.12,0 mat=stone
  cone 0.18 $r $h*0.3 sides=8 at=0,$h-0.3-$h*0.3+0.02,0 mat=stone
  group rot=0,22.5,0
    cyl $r 0.08 sides=8 at=0,$h-0.3,0 mat=wood
    cyl $r*0.97 1.0 sides=8 at=0,$h-0.22,0 mat=wood
    cyl $r+0.04 0.08 sides=8 at=0,$h+0.78,0 mat=wood
  end
  radial n=8
    church.archline $r*0.6 0.75 0.03 0.03 at=0,$h-0.12,$r*0.92 mat=wood
  end
  radial n=8 rot=0,22.5,0
    box 0.05 1.0 0.05 at=0,$h-0.22,$r*0.97 mat=wood
  end
  box 0.6 0.04 0.4 at=0,$h+0.9,$r*0.55 rot=-22,0,0 mat=wood
  # the stair: treads rising to it, a handrail
  for i 6
    box 0.7 0.05 0.32 at=-$r-0.2-(5-$i)*0.3,0.2+$i*($h-0.3)/6,-0.25 mat=wood
    box 0.05 0.2+$i*($h-0.3)/6 0.05 at=-$r-0.2-(5-$i)*0.3,0,-0.25+0.14 mat=wood
  end
  tube 0.025 -$r-1.85,0.95,-0.58 -$r-0.25,$h+0.6,-0.58 sides=8 mat=wood
end
define church.font h=1.0 r=0.48   # a baptismal font of stone: a stepped octagonal plinth, a clustered shaft, a deep octagonal bowl, the water in it, a wooden lid's knob over it
  cyl $r+0.35 0.12 sides=8 rot=0,22.5,0 mat=stone
  cyl $r+0.18 0.12 sides=8 rot=0,22.5,0 at=0,0.12,0 mat=stone
  cyl 0.16 $h*0.55 sides=8 at=0,0.24,0 mat=stone
  radial n=4
    cyl 0.05 $h*0.5 sides=8 at=0.16,0.26,0 mat=stone
  end
  lathe 0,0 0.2,0 0.3,0.06 $r,0.2 $r,$h-0.24-0.02 $r-0.06,$h-0.24-0.02 $r-0.06,0.24 0,0.24 sides=8 rot=0,22.5,0 at=0,0.24+$h*0.5,0 mat=stone
  cyl $r+0.02 0.05 sides=8 rot=0,22.5,0 at=0,$h-0.02+$h*0.5-0.05,0 mat=stone
  cyl $r-0.07 0.01 sides=24 at=0,$h+$h*0.5-0.2,0 mat=water
  radial n=8 rot=0,22.5,0
    church.archline 0.24 0.34 0.025 0.025 at=0,0.24+$h*0.5+0.24,$r*0.93 mat=stone
  end
end
define church.chandelier r=0.9 drop=4 arms=8   # a corona of gilt iron hung on a chain `drop` long: a ring, its arms and candles, a boss below; its top at y = 0, hanging down
  tube 0.012 0,0,0 0,-$drop,0 sides=6 mat=iron
  lathe 0,0 0.05,0 0.08,-0.1 0.05,-0.25 0.12,-0.4 0.06,-0.55 0.02,-0.7 0,-0.75 sides=12 at=0,-$drop,0 mat=gold
  torus $r 0.025 sides=32 at=0,-$drop-0.42,0 mat=gold
  torus $r*0.55 0.018 sides=24 at=0,-$drop-0.42,0 mat=gold
  radial n=$arms
    tube 0.014 0.05,-$drop-0.3,0 $r*0.5,-$drop-0.42,0 $r,-$drop-0.42,0 sides=6 mat=iron
    lathe 0,0 0.045,0 0.05,0.04 0.03,0.05 0,0.05 sides=10 at=$r,-$drop-0.4,0 mat=gold
    cyl 0.018 0.22 sides=8 at=$r,-$drop-0.35,0 mat=wax
    cone 0.006 0 0.03 sides=5 at=$r,-$drop-0.13,0 mat=wick
  end
  radial n=$arms/2
    tube 0.01 0,-$drop+0.4,0 $r,-$drop-0.42,0 sides=6 mat=iron
  end
end
define church.pane w=0.3 h=0.5 pick=0   # one pane of coloured glass, `pick` saying which: 0 ruby, 1 cobalt, 2 amber, 3 emerald; foot on y = 0, facing +z
  if $pick==0
    box $w $h 0.02 mat=ruby
  end
  if $pick==1
    box $w $h 0.02 mat=cobalt
  end
  if $pick==2
    box $w $h 0.02 mat=amber
  end
  if $pick>=3
    box $w $h 0.02 mat=emerald
  end
end
define church.lights w=1.6 h=5 bands=9 seed=1   # the glass of two lancet lights side by side under a pointed head, coloured pane by pane, leaded, in a stone frame: facing +z, its foot on y = 0
  let lw $w*0.44
  for side 2
    let x ($side-0.5)*$w*0.52
    let bh ($h-$lw)/$bands
    for b $bands
      let y $b*$bh
      if $b==floor($bands/2)
        # a medallion: a roundel of gold on blue, a ruby heart
        church.pane $lw $bh 1 at=$x,$y,0
        cyl min($lw,$bh)*0.42 0.02 sides=20 rot=90,0,0 centre=1 at=$x,$y+$bh/2,0.004 mat=amber
        cyl min($lw,$bh)*0.18 0.02 sides=16 rot=90,0,0 centre=1 at=$x,$y+$bh/2,0.008 mat=ruby
        torus min($lw,$bh)*0.42 0.012 sides=20 rot=90,0,0 at=$x,$y+$bh/2,0.01 mat=lead
      else
        for c 3
          church.pane $lw/3 $bh floor(rand($seed,$side,$b,$c)*4) at=$x+($c-1)*$lw/3,$y,0
        end
        box 0.012 $bh 0.025 at=$x-$lw/6,$y,0 mat=lead
        box 0.012 $bh 0.025 at=$x+$lw/6,$y,0 mat=lead
      end
      box $lw 0.025 0.03 at=$x,$y-0.012,0 mat=lead
    end
    church.lancet $lw $lw*0.866+0.02 0.02 at=$x,$h-$lw,0 mat=cobalt
    church.archline $lw $h 0.06 0.08 at=$x,0,-0.02 mat=stone
    box 0.02 $h-$lw 0.025 at=$x,0,0 mat=lead
  end
  # the head: a foiled roundel of gold glass with a red cross in it
  cyl $w*0.2 0.02 sides=24 rot=90,0,0 centre=1 at=0,$h+$w*0.12,0 mat=amber
  box 0.05 $w*0.3 0.025 at=0,$h+$w*0.12-$w*0.15,0.005 mat=ruby
  box $w*0.3 0.05 0.025 at=0,$h+$w*0.12-0.025,0.005 mat=ruby
  torus $w*0.2 0.04 sides=24 rot=90,0,0 at=0,$h+$w*0.12,0 mat=stone
  church.archline $w $h+$w*0.866*0.6 0.12 0.14 at=0,0,-0.04 mat=stone
  box $w+0.3 0.12 0.3 at=0,-0.12,0.05 mat=stone
end
define church.rose r=2.4   # a rose window: twelve petals of glass round a roundel, in rings of tracery; facing +z, its middle at y = 0
  cyl $r*0.22 0.02 sides=24 rot=90,0,0 centre=1 mat=amber
  radial n=12 axis=z
    church.lancet $r*0.2 $r*0.62 0.02 at=0,$r*0.26,0 mat=ruby
    church.lancet $r*0.12 $r*0.4 0.025 at=0,$r*0.36,0.003 mat=cobalt
    cyl $r*0.07 0.02 sides=12 rot=90,0,0 centre=1 at=0,$r*0.88,0 mat=emerald
    box 0.05 $r*0.72 0.06 at=0,$r*0.22,0 rot=0,0,15 mat=stone
  end
  torus $r*0.22 0.05 sides=32 rot=90,0,0 mat=stone
  torus $r 0.12 sides=48 rot=90,0,0 mat=stone
  cyl $r 0.02 sides=48 rot=90,0,0 centre=1 at=0,0,-0.03 mat=cobalt
end
define church.organ w=3.6 d=1.2 h=7   # a pipe organ in a case of oak: the console and its keyboards, flats and towers of pipes rising to the middle; facing +z, its foot on y = 0
  box $w 2.4 $d mat=wood
  box $w+0.1 0.1 $d+0.1 at=0,2.4,0 mat=wood
  # the console: keyboards, stops, a bench
  box 1.3 0.08 0.4 at=0,0.95,$d/2+0.15 mat=wood
  box 1.2 0.03 0.14 at=0,1.03,$d/2+0.25 mat=ivory
  box 1.2 0.03 0.14 at=0,1.1,$d/2+0.17 mat=ivory
  box 1.3 0.5 0.06 at=0,1.15,$d/2+0.05 mat=wood
  radial n=1
    box 1.1 0.06 0.35 at=0,0.5,$d/2+0.75 mat=wood
  end
  # the pipes: three towers and two flats between them, tallest in the middle
  for t 3
    let x ($t-1)*$w*0.36
    let tall if($t==1,$h-2.6,($h-2.6)*0.78)
    box 0.95 $tall+0.4 0.12 at=$x,2.5,-$d/2+0.2 mat=wood
    for p 5
      let px $x+($p-2)*0.17
      let ph $tall*(1-abs($p-2)*0.12)
      cyl 0.07 $ph sides=12 at=$px,2.5,0.1 mat=tin
      cone 0.07 0.0 0.18 sides=12 at=$px,2.75,0.12 scale=1,1,0.35 mat=lead
    end
    box 1.0 0.15 0.5 at=$x,2.5+$tall,0 mat=wood
    cone 0.5 0.1 0.5 sides=4 rot=0,45,0 at=$x,2.65+$tall,0 mat=wood
  end
  for f 2
    let x ($f-0.5)*$w*0.36
    for p 4
      let px $x+($p-1.5)*0.15
      let ph ($h-2.6)*(0.5+0.06*abs($p-1.5)*if($f==0,-1,1)+0.1)
      cyl 0.055 $ph sides=12 at=$px,2.5,0.1 mat=tin
    end
  end
end
define church.arcade len=24 h=8 bays=6 r=0.32 style=gothic   # the arcade of a hall church along z: piers in the style, pointed arches springing from them, and the ends against the walls
  let bw $len/$bays
  for i $bays+1
    $style.column $h $r at=0,0,-$len/2+$bw*$i mat=stone
  end
  for i $bays
    let z -$len/2+$bw*($i+0.5)
    church.archline $bw-$r*1.4 ($bw-$r*1.4)*0.866 $r*0.9 $r*1.1 at=0,$h*0.97,$z rot=0,90,0 mat=stone
    church.archline $bw-$r*2.2 ($bw-$r*2.2)*0.866 $r*0.4 $r*1.3 at=0,$h*0.97,$z rot=0,90,0 mat=stone
  end
end
define church.redeemer h=2 faces=26000   # a statue of Christ, robed, his arms open in blessing, in the manner of the Redeemer: one smooth surface of stone, feet on y = 0, facing +z
  group scale=$h/2 mat=marble
    # the robe, falling from the shoulders to the feet, flaring a little at the hem, elliptical in plan
    lathe 0,0 0.285,0 0.3,0.04 0.28,0.12 0.25,0.5 0.225,0.85 0.2,1.06 0.195,1.14 0.205,1.3 0.205,1.42 0.185,1.5 0.13,1.56 0.06,1.58 0,1.58 scale=1,1,0.6 sides=32
    # its folds: ridges coming out of the cloth below the girdle and the breast, falling to the hem
    for i 7
      let a -54+$i*18
      let top 1.0+0.12*rand(3,$i)
      blend=0.07 tube 0.02+0.012*rand(5,$i) 0.15*sin($a),$top,0.08*cos($a) 0.225*sin($a),0.7,0.135*cos($a) 0.28*sin($a),0.15,0.17*cos($a) 0.3*sin($a),0.015,0.18*cos($a) sides=10
    end
    for i 4
      let a 150+$i*20
      blend=0.07 tube 0.026 0.15*sin($a),1.2,0.08*cos($a) 0.25*sin($a),0.55,0.15*cos($a) 0.3*sin($a),0.015,0.18*cos($a) sides=10
    end
    # the girdle, and the mantle's fold across the breast from the right shoulder to the left hip
    blend=0.03 torus 0.198 0.018 at=0,1.07,0 scale=1,1,0.62 sides=24
    blend=0.06 tube 0.034 -0.12,1.52,0.0 -0.1,1.44,0.1 0.02,1.3,0.13 0.14,1.17,0.1 0.17,1.09,0.0 sides=12
    # the feet, below the hem
    blend=0.025 capsule 0.042 0.19 rot=90,0,0 centre=1 at=-0.085,0.035,0.19
    blend=0.025 capsule 0.042 0.19 rot=90,0,0 centre=1 at=0.085,0.035,0.19
    # the shoulders and the neck
    blend=0.05 tube 0.075 -0.17,1.49,-0.01 0.17,1.49,-0.01 sides=16
    blend=0.04 cyl 0.056 0.14 at=0,1.55,0.0
    mirror x
      # the arm, open and a little lowered, in a wide sleeve
      blend=0.05 tube 0.064 0.16,1.48,0 0.46,1.42,0.03 0.62,1.39,0.05 sides=14
      blend=0.06 tube 0.05 0.5,1.41,0.035 0.75,1.36,0.07 sides=12
      # the sleeve's cloth hanging below the forearm, back toward the side, its hem rolled
      blend=0.05 extrude 0.06 0.2,1.44 0.46,1.39 0.72,1.33 0.725,1.28 0.71,1.22 0.66,1.16 0.6,1.12 0.53,1.08 0.46,1.055 0.39,1.05 0.32,1.065 0.26,1.1 0.21,1.17 at=0,0,0.03
      blend=0.03 tube 0.022 0.71,1.3,0.06 0.62,1.15,0.06 0.47,1.06,0.05 0.3,1.07,0.04 sides=8
      # the hand, open, its palm forward, fingers together, the thumb up
      blend=0.012 sphere 0.04 scale=1.9,1.15,0.45 at=0.8,1.335,0.075 rot=0,0,-8
      blend=0.008 capsule 0.012 0.07 at=0.775,1.37,0.085 rot=0,0,-35
    end
    remesh faces=$faces*0.7 blend=0.02
  end
  # the head, a little bowed, made finer than the robe: the face, the hair
  # parted and falling to the shoulders, the beard
  group scale=$h/2 mat=marble
    group at=0,1.62,0.0 rot=7,0,0
      cyl 0.05 0.1 at=0,-0.06,-0.005
      # the face, and the beard round its jaw and chin, the moustache into it
      blend=0.025 sphere 0.1 scale=0.82,1.14,1.0 at=0,0.035,0.028
      blend=0.03 sphere 0.056 scale=1.18,1.05,0.8 at=0,-0.025,0.058
      # the nose, and the brow over the eyes
      blend=0.012 sphere 0.018 scale=0.75,1.8,1.2 at=0,0.094,0.115
      blend=0.03 sphere 0.03 scale=2.4,0.7,0.8 at=0,0.135,0.098
      carve=0.025 sphere 0.016 at=-0.034,0.122,0.124
      carve=0.025 sphere 0.016 at=0.034,0.122,0.124
      # the hair, parted in the middle, falling either side of the face to the shoulders
      blend=0.02 sphere 0.09 scale=0.75,1.0,1.08 at=-0.036,0.108,-0.022
      blend=0.02 sphere 0.09 scale=0.75,1.0,1.08 at=0.036,0.108,-0.022
      blend=0.03 capsule 0.037 0.22 scale=1,1,0.8 at=-0.079,-0.075,-0.014
      blend=0.03 capsule 0.037 0.22 scale=1,1,0.8 at=0.079,-0.075,-0.014
      blend=0.04 capsule 0.06 0.22 scale=1.55,1,0.6 at=0,-0.15,-0.065
    end
    remesh faces=$faces*0.35 blend=0.012
  end
end
define church.plinth w=0.9 h=1.1   # a marble pedestal: a stepped foot, a die with a sunk panel and a cross, a moulded cap
  box $w+0.3 0.12 $w+0.3 mat=marble
  box $w+0.16 0.1 $w+0.16 at=0,0.12,0 mat=marble
  box $w+0.06 0.08 $w+0.06 at=0,0.22,0 mat=marble
  box $w $h-0.5 $w at=0,0.3,0 mat=marble
  box $w*0.7 ($h-0.5)*0.7 0.02 at=0,0.3+($h-0.5)*0.15,$w/2 mat=marble
  box 0.05 ($h-0.5)*0.5 0.02 at=0,0.3+($h-0.5)*0.22,$w/2+0.02 mat=gold
  box ($h-0.5)*0.3 0.05 0.02 at=0,0.3+($h-0.5)*0.52,$w/2+0.02 mat=gold
  box $w+0.08 0.06 $w+0.08 at=0,$h-0.2,0 mat=marble
  box $w+0.18 0.14 $w+0.18 at=0,$h-0.14,0 mat=marble
end
)LIB";
}

}  // namespace sg::sculpt
