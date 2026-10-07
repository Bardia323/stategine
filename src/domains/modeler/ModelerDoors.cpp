// Doors, and what closes like one: drawers, cupboards, cabinets. `use doors`.
//
// A door is not a kind of thing but a few choices, made in any combination:
//
//   what it closes   an opening w by h in a wall t deep - or a cabinet's bay
//   its surround     casing | metal | stone | tile | timber | post | none, round
//                    a head: square | round | segment | pointed | lancet |
//                    persian | tudor | horseshoe, with a transom or a
//                    tympanum over the leaves and sidelights beside them
//   its leaves       each a body: frame (stiles and rails round cols x rows
//                    cells, each filled: panel flat solid glass lattice shoji
//                    paper louvre star diamond boards mirror mesh none - the
//                    rows from `split` up with `upper`) | flush (a vision
//                    panel cut through it) | boards (ledged and braced) |
//                    pickets | glass (frameless) | ribbed | round (a vault's)
//   how they move    op = hinge (a leaf or a pair, `both` ways, `dutch` cut
//                    across) | pivot | slide (mount 0 into the wall, 1 on a
//                    barn track, 2 under an automatic door's header) | bypass
//                    | fold (a bifold, an accordion) | revolve | flap (a fall
//                    front, a lift-up) | tilt (up and over) | roll | sectional
//   its hardware     handle and back (lever knob pull bar plate panic ring
//                    recess hikite button cup card latch none), hinge (butt
//                    strap pivot concealed none), kick plate, viewer, letter
//                    plate, closer, knocker, studs
//
// and every one of these words is a variable of the library (`let fill
// glass` before a door says it for every door after; `fill=glass` on one
// says it for that one): a word not said is the one said round it - which is
// how a cabinet gives its fronts their handles. The doors of the world
// (`door.hotel`, `door.shoji`, `door.persian` ...) and its cabinets
// (`cabinet.kitchen`, `chest`, `wardrobe` ...) are each one line of choices.
//
// Each leaf is made in a `moves` block, so the model says how it moves
// (Model::joints): `<name>.1`, `<name>.2` ... a door's leaves, `<name>.<bay>.<n>`
// a cabinet's. Built `open=0`, a program turns them; `open=` poses them as made.
// The roller's slats and the sectional door's panels each ride a track, together.
//
// Everything stands on y = 0, its opening centred on x = 0, the wall's middle
// at z = 0; the leaves open to +z, hung at -x (hand=-1: at +x, swing=-1: to
// -z). The -z face is the outside: knocker, letter plate, studs, strap
// hinges; `back` is the handle there.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_doors() {
    return R"LIB(
use mould
use pointed
use girih
default detail 1
default name door
default op hinge
default leaves 1
default ratio 0.5
default hand 1
default swing 1
default range 90
default both 0
default open 0
default dutch 0
default piv 0.12
default foot 0.01
default lh 0
default mount 0
default halves 1
default body frame
default lt 0.045
default lmat wood
default cols 1
default rows 1
default fill panel
default upper glass
default split 99
default stile 0.11
default rail 0.11
default brail 0.2
default bar 0.1
default toprow 1
default botrow 1
default gx 0.15
default gy 0.15
default vw 0
default vh 0
default vx 0
default vy 1
default gmat glass
default hw metal
default handle lever
default handle2 same
default back same
default hboth 1
default hx 0.065
default hy 1
default hl 0.3
default hdir 0
default hinge butt
default kick 0
default peep 0
default closer 0
default letter 0
default knock 0
default studs 0
default surround casing
default fmat wood
default head square
default fan glass
default transom 0
default sidelight 0
default sill 1
default hole 1
default band 0.07
default step 0
default wall 0
default wallh 0
default pmat plaster
# a head: how high its arch rises over the span, and the centres it is struck from (pointed.arch's c)
let door_rise_square 0
let door_rise_round 0.5
let door_rise_segment 0.2
let door_rise_pointed 0.866
let door_rise_lancet 1.118
let door_rise_persian 0.6708
let door_rise_tudor 0.3
let door_rise_horseshoe 0.866
let door_c_round 0
let door_c_pointed 0.5
let door_c_lancet 1
let door_c_persian 0.2
let door_hole_square square
let door_hole_round round
let door_hole_segment round
let door_hole_horseshoe round
let door_hole_pointed pointed
let door_hole_lancet pointed
let door_hole_persian pointed
let door_hole_tudor pointed
# what each surround takes of a wall it stands in: so far each side (m), so far over a square head (up), and whether it is a rectangle round all (rect) or follows its arch by a ring m thick
let door_m_casing 0.03
let door_up_casing 0.03
let door_rect_casing 0
let door_m_metal 0.05
let door_up_metal 0.05
let door_rect_metal 1
let door_m_stone 0.28
let door_up_stone 0.34
let door_rect_stone 0
let door_m_tile 0.48
let door_up_tile 0.48
let door_rect_tile 1
let door_m_timber 0.12
let door_up_timber 0.24
let door_rect_timber 1
let door_m_post 0.12
let door_up_post 0
let door_rect_post 1
let door_m_none 0
let door_up_none 0
let door_rect_none 0
# where a leaf's body has its +z face, as a share of half its thickness (its handle stands on it)
let door_skin_frame 1
let door_skin_flush 1
let door_skin_boards 0.1
let door_skin_pickets 1
let door_skin_glass 1
let door_skin_ribbed 1
let door_skin_round 1
# where a kind of leaf shuts: 1 at the face it opens to (hung), 0 in the wall's middle
let door_face_hinge 1
let door_face_flap 1
let door_face_tilt 1
let door_face_pivot 0
let door_face_slide 0
let door_face_bypass 0
let door_face_fold 0
let door_face_revolve 0
let door_face_roll 0
let door_face_sectional 0
let door_square_square 1
let door_square_round 0
let door_square_segment 0
let door_square_pointed 0
let door_square_lancet 0
let door_square_persian 0
let door_square_tudor 0
let door_square_horseshoe 0

define door w=0.9 h=2.1 t=0.15 name=$name op=$op leaves=$leaves ratio=$ratio hand=$hand swing=$swing range=$range both=$both open=$open dutch=$dutch piv=$piv foot=$foot lh=$lh mount=$mount halves=$halves body=$body lt=$lt lmat=$lmat cols=$cols rows=$rows fill=$fill upper=$upper split=$split stile=$stile rail=$rail brail=$brail bar=$bar toprow=$toprow botrow=$botrow gx=$gx gy=$gy vw=$vw vh=$vh vx=$vx vy=$vy gmat=$gmat hw=$hw handle=$handle handle2=$handle2 back=$back hboth=$hboth hx=$hx hy=$hy hl=$hl hdir=$hdir hinge=$hinge kick=$kick peep=$peep closer=$closer letter=$letter knock=$knock studs=$studs surround=$surround fmat=$fmat head=$head fan=$fan transom=$transom sidelight=$sidelight sill=$sill hole=$hole band=$band step=$step wall=$wall wallh=$wallh pmat=$pmat   # a door: leaves w wide and h high (to the spring of an arch, the foot of a transom) in a wall t deep - its surround, fixed lights, leaves moving as `op` says and their hardware; every word the library's (see its head)
  let W $w+2*$sidelight+($sidelight>0)*0.12
  let tr $transom*$door_square_${head}
  let arise $W*$door_rise_${head}
  let H $h+$tr+($tr>0)*0.06+$arise
  # the plane the leaves shut in: a hung leaf's at the face it opens to
  let lz $door_face_${op}*$swing*($t/2-$lt/2)
  door.frame.$surround $W $H $t
  # fixed lights in that plane: either side, over the leaves, in the arch
  if $sidelight>0
    mirror x
      box 0.06 $h $t*0.5 at=$w/2+0.03,0,$lz mat=$fmat
      door.light $sidelight $h $lt fill=$upper at=$w/2+0.06+$sidelight/2,0,$lz mat=$lmat
    end
  end
  if $tr>0
    box $W 0.06 $t*0.5 at=0,$h,$lz mat=$fmat
    door.light $W $tr $lt fill=$fan at=0,$h+0.06,$lz mat=$lmat
  end
  if $arise>0
    box $W 0.05 $lt*1.4 at=0,$h,$lz mat=$fmat
    door.tymp.$fan $W $arise $lt at=0,$h,$lz
  end
  group scale=$hand,1,$swing
    door.op.$op $w $h $t
  end
  if $hole
    opening $W $H head=$door_hole_${head} walk=1
  end
  if $wall>0
    door.wall $W $H $t
  end
end
define door.wall w=1 h=2.1 t=0.15   # a piece of wall `wall` wide and `wallh` high (0: a metre over the door) round an opening w by h: a door shown standing in a wall (`pmat` its material). It stops where the surround begins - at its outer edge, round its arch's ring - so no face of the one lies in a face of the other; an arch is cut from it only round the arch
  let m $door_m_${surround}
  let up $door_up_${surround}
  let arise $w*$door_rise_${head}
  let s $h-$arise
  let square ($arise==0)+$door_rect_${surround}
  let ow $w+2*$m
  let ww max($wall,$ow+0.6)
  let wh if($wallh>0,max($wallh,$h+$up+0.1),$h+$up+1)
  group mat=$pmat
    box ($ww-$ow)/2 $wh $t at=-($ww+$ow)/4,0,0
    box ($ww-$ow)/2 $wh $t at=($ww+$ow)/4,0,0
    if $square
      box $ow $wh-$h-$up $t at=0,$h+$up,0
    else
      group
        box $ow $wh-$s $t at=0,$s,0
        sub door.arch.$head $w $arise $t*2 at=0,$s,0 res=0.02
        if $m>0
          sub door.ring.$head $w $arise $m $t*2 at=0,$s,0 res=0.02
        end
      end
    end
  end
end

# --- what fills a leaf's cells: each w by h, its foot on y = 0, centred on x, in the leaf's middle (t the leaf)
define door.fill.panel w=0.4 h=0.6 t=0.045   # a raised and fielded panel: a thin board in the frame's grooves, its field raised on both faces, the bevel running out wide and shallow
  let b min(min($w,$h)*0.16,0.06)
  box $w $h $t*0.32
  mirror z
    loft $t*0.14 -$w/2,0 $w/2,0 $w/2,-$h -$w/2,-$h / -$w/2+$b,-$b $w/2-$b,-$b $w/2-$b,-$h+$b -$w/2+$b,-$h+$b rot=90,0,0 at=0,0,$t*0.16
  end
end
define door.fill.flat w=0.4 h=0.6 t=0.045   # a flat panel, sunk in its frame (a Shaker door)
  box $w $h $t*0.36
end
define door.fill.solid w=0.4 h=0.6 t=0.045   # filled flush with the frame
  box $w $h $t*0.92
end
define door.fill.glass w=0.4 h=0.6 t=0.045   # a pane, held by beads on both faces
  box $w $h 0.006 mat=$gmat
  mirror z
    box $w 0.014 0.012 at=0,0,0.009
    box $w 0.014 0.012 at=0,$h-0.014,0.009
    box 0.014 $h-0.028 0.012 at=-$w/2+0.007,0.014,0.009
    box 0.014 $h-0.028 0.012 at=$w/2-0.007,0.014,0.009
  end
end
define door.fill.mirror w=0.4 h=0.6 t=0.045   # a mirror on a backing board
  box $w $h $t*0.36
  box $w-0.01 $h-0.01 0.004 at=0,0.005,$t*0.18+0.002 mat=$gmat
end
define door.fill.mesh w=0.4 h=0.6 t=0.045   # an insect screen
  box $w $h 0.002 mat=$gmat
end
define door.fill.none w=0.4 h=0.6 t=0.045   # open
end
define door.fill.lattice w=0.4 h=0.6 t=0.045   # kumiko: thin bars half-lapped in a grid, gx by gy apart
  let nx max(1,round($w/$gx))
  let ny max(1,round($h/$gy))
  for i $nx-1
    box 0.012 $h $t*0.6 at=-$w/2+$w*($i+1)/$nx,0,0
  end
  for j $ny-1
    box $w 0.012 $t*0.6 at=0,$h*($j+1)/$ny-0.006,0
  end
end
define door.fill.shoji w=0.4 h=0.6 t=0.045   # the lattice, its paper on the outside face, lit through
  door.fill.lattice $w $h $t
  box $w $h 0.002 at=0,0,-$t*0.31 mat=paper
end
define door.fill.paper w=0.4 h=0.6 t=0.045   # paper stretched both sides over a core (a fusuma)
  box $w $h $t*0.86 mat=paper
end
define door.fill.louvre w=0.4 h=0.6 t=0.045   # slats at forty-five degrees, each over the next, shedding rain and sight
  let n max(2,floor($h/0.045))
  for i $n
    box $w 0.06 0.008 centre=1 rot=45,0,0 at=0,($i+0.5)*$h/$n,0
  end
end
define door.fill.star w=0.4 h=0.6 t=0.045   # a carved panel: an eight-point star, its strapwork raised on both faces
  box $w $h $t*0.36
  mirror z
    girih.star 8 min($w,$h)*0.42 0.008 at=0,$h/2,$t*0.18
  end
end
define door.fill.diamond w=0.4 h=0.6 t=0.045   # a panel with a raised lozenge, bevelled
  box $w $h $t*0.36
  extrude $t*0.6 0,$h*0.08 $w*0.42,$h/2 0,$h*0.92 -$w*0.42,$h/2 chamfer=$t*0.12
end
define door.fill.boards w=0.4 h=0.6 t=0.045   # boards on edge, V-jointed
  let n max(1,round($w/0.1))
  let bw $w/$n
  for i $n
    extrude $t*0.5 -$bw/2,0 $bw/2,0 $bw/2,$h -$bw/2,$h chamfer=0.003 at=-$w/2+$bw*($i+0.5),0,0
  end
end
define door.light w=0.5 h=2 t=0.045 fill=glass rows=1 cols=1 split=99 stile=0.05 rail=0.05 brail=0.05   # a light that does not open: a slim frame round one fill (sidelights, a transom)
  door.body.frame $w $h $t
end

# --- bodies: what a leaf is, w by h, t thick, foot on y = 0, centred on x, its middle on z = 0
define door.body.frame w=0.9 h=2 t=0.045   # stiles and rails round cols by rows cells, each filled (fill; from row `split` up, upper), its rows weighted by botrow and toprow; bar the rails and mullions between
  let iw $w-2*$stile
  let ih $h-$brail-$rail
  let tw if($rows>1,$toprow,1)
  let bw if($rows>1,$botrow,1)
  let u ($ih-($rows-1)*$bar)/($rows-2+$tw+$bw)
  let cw ($iw-($cols-1)*$bar)/$cols
  box $stile $h $t at=-$w/2+$stile/2,0,0
  box $stile $h $t at=$w/2-$stile/2,0,0
  box $iw $brail $t
  box $iw $rail $t at=0,$h-$rail,0
  for r $rows
    let y $brail+($r+($r>0)*($bw-1))*$u+$r*$bar
    let rh $u*(1+($r==0)*($bw-1)+($r==$rows-1)*($tw-1))
    if $r>0
      box $iw $bar $t*0.9 at=0,$y-$bar,0
    end
    for c $cols
      let x -$iw/2+$cw/2+$c*($cw+$bar)
      if $c>0
        box $bar $rh $t*0.9 at=$x-$cw/2-$bar/2,$y,0
      end
      if $r>=$split
        door.fill.$upper $cw $rh $t at=$x,$y,0
      else
        door.fill.$fill $cw $rh $t at=$x,$y,0
      end
    end
  end
end
define door.body.flush w=0.9 h=2 t=0.045   # one slab; a vision panel vw by vh, its sill at vy, vx off the middle, glazed through it
  if $vw*$vh>0
    let x0 $vx-$vw/2
    let x1 $vx+$vw/2
    box $w $vy $t
    box $w $h-$vy-$vh $t at=0,$vy+$vh,0
    box $x0+$w/2 $vh $t at=(-$w/2+$x0)/2,$vy,0
    box $w/2-$x1 $vh $t at=($x1+$w/2)/2,$vy,0
    door.fill.glass $vw $vh $t at=$vx,$vy,0
  else
    box $w $h $t
  end
end
define door.body.boards w=0.9 h=2 t=0.05   # boards on edge, V-jointed, on three ledges behind, braced between them - each brace rising to the hinge side, where the load goes
  let n max(2,round($w/0.14))
  let bw $w/$n
  let ld $t*0.45
  for i $n
    extrude $t*0.55 -$bw/2,0 $bw/2,0 $bw/2,$h -$bw/2,$h chamfer=0.004 at=-$w/2+$bw*($i+0.5),0,-$t*0.225
  end
  let gap ($h-0.38)/2
  for i 3
    box $w-0.06 0.14 $ld at=0,0.12+$i*$gap,$t/2-$ld/2
  end
  for i 2
    let dy $gap-0.14
    let dx $w-0.2
    box hypot($dx,$dy) 0.12 $ld centre=1 rot=0,0,-atan2($dy,$dx) at=0,0.26+$i*$gap+$dy/2,$t/2-$ld/2
  end
end
define door.body.pickets w=0.9 h=1.1 t=0.04   # a garden gate: pickets with pointed heads, a hand apart, on two rails and a brace
  let n max(3,round($w/0.12))
  let pw $w/$n-0.04
  for i $n
    extrude $t*0.5 -$pw/2,0 $pw/2,0 $pw/2,$h-$pw*0.6 0,$h -$pw/2,$h-$pw*0.6 at=-$w/2+$w/$n*($i+0.5),0,-$t*0.25
  end
  box $w 0.09 $t*0.5 at=0,0.15,$t*0.25
  box $w 0.09 $t*0.5 at=0,$h-0.32,$t*0.25
  box hypot($w-0.1,$h-0.56) 0.08 $t*0.5 centre=1 rot=0,0,-atan2($h-0.56,$w-0.1) at=0,$h/2-0.08,$t*0.25
end
define door.body.glass w=0.9 h=2.1 t=0.012   # toughened glass, no frame: patch fittings at its corners on the hinge side, a lock at its foot
  box $w $h 0.012 mat=$gmat
  box 0.18 0.07 0.03 at=-$w/2+0.09,0,0 mat=$hw
  box 0.18 0.07 0.03 at=-$w/2+0.09,$h-0.07,0 mat=$hw
  box 0.16 0.06 0.03 at=$w/2-0.08,0,0 mat=$hw
end
define door.body.ribbed w=2.4 h=0.5 t=0.04   # pressed steel, ribbed across (a garage door's section): a plate t thick, its ribs standing proud of both faces
  box $w $h $t
  let n max(1,round($h/0.11))
  for i $n
    box $w 0.016 $t*1.3 at=0,($i+0.5)*$h/$n-0.008,0
  end
end
define door.body.round w=1.8 h=1.8 t=0.5   # a vault's: a round slab stepped back in rings, its bolts round its edge
  let r min($w,$h)/2
  cyl $r $t rot=90,0,0 centre=1 at=0,$h/2,0 sides=48
  cyl $r*0.9 $t*1.06 rot=90,0,0 centre=1 at=0,$h/2,0 sides=48
  radial n=12 at=0,$h/2,0 axis=z
    cyl 0.035 0.12 rot=0,0,-90 at=$r-0.02,0,0 sides=12 mat=$hw
  end
end

# --- a leaf, and what is on it
define door.leaf w=0.9 h=2 t=0.045 grip=$handle knock=$knock   # one leaf as the spec says - its body and its hardware; foot on y = 0, centred on x, its middle on z = 0, hung at -x, opening to +z
  door.body.$body $w $h $t mat=$lmat
  door.hinge.$hinge $w $h $t mat=$hw
  door.handle $w $h $t $grip mat=$hw
  if $kick>0
    mirror z
      box $w-0.04 $kick 0.0015 at=0,0.012,$t/2+0.001 mat=$hw
    end
  end
  if $peep>0
    cyl 0.011 $t+0.012 rot=90,0,0 centre=1 at=0,$peep,0 mat=$hw
  end
  if $letter>0
    box 0.3 0.075 0.004 at=0,$letter,-$t/2-0.002 mat=$hw
    box 0.25 0.03 0.006 at=0,$letter+0.022,-$t/2-0.005 mat=$hw
  end
  if $closer
    box 0.28 0.065 0.055 at=-$w/2+0.3,$h-0.12,$t/2+0.0275 mat=$hw
    box 0.26 0.02 0.02 at=-$w/2+0.38,$h-0.035,$t/2+0.065 mat=$hw
  end
  if $studs>0
    door.studs $w $h $t mat=$hw
  end
  if $knock==1
    door.knocker $w $h $t mat=$hw
  end
  if $knock==2
    door.hammer $w $h $t mat=$hw
  end
  if $knock==3
    door.chime $w $h $t mat=$hw
  end
end
define door.handle w=0.9 h=2 t=0.045 grip=lever   # its handle, hx in from the latch edge (+x; below 0, in the middle) and hy up: the grip on the +z face, `back` on the -z face (hboth=0: nothing there)
  set sides=12
  let x if($hx<0,0,$w/2-$hx)
  # on the body's own +z face (a ledged door's boards stand behind its ledges)
  door.grip.$grip $w at=$x,$hy,$t/2*$door_skin_${body} rot=0,0,$hdir*90
  if $hboth
    door.grip.$back $w at=$x,$hy,-$t/2 scale=1,1,-1 rot=0,0,$hdir*90
  end
end
define door.studs w=0.9 h=2 t=0.05   # nail heads in rows over the outside face, `studs` apart
  let nx max(1,floor(($w-0.1)/$studs))
  let ny max(1,floor(($h-0.1)/$studs))
  array n=$nx+1 step=($w-0.1)/$nx,0,0 at=-$w/2+0.05,0.05,-$t/2
    array n=$ny+1 step=0,($h-0.1)/$ny,0
      cone 0.016 0.004 0.012 rot=-90,0,0 sides=8
    end
  end
end
define door.knocker w=0.9 h=2 t=0.05   # a ring knocker on a boss, in the middle of the outside face
  cyl 0.045 0.01 rot=-90,0,0 at=0,1.45,-$t/2
  torus 0.06 0.009 rot=90,0,0 at=0,1.37,-$t/2-0.025
  cyl 0.02 0.012 rot=-90,0,0 at=0,1.3,-$t/2
end
define door.hammer w=0.9 h=2 t=0.05   # the heavy knocker (Persian kubeh, a man's knock, low-voiced): a bar hanging from a pivot, its striker under it
  cyl 0.03 0.012 rot=-90,0,0 at=$w/2-0.13,1.55,-$t/2
  cone 0.016 0.03 0.2 rot=180,0,0 at=$w/2-0.13,1.55,-$t/2-0.03
  cyl 0.03 0.012 rot=-90,0,0 at=$w/2-0.13,1.31,-$t/2
end
define door.chime w=0.9 h=2 t=0.05   # the light knocker (Persian halgheh, a woman's knock, high): a slender ring, its striker under it
  cyl 0.02 0.01 rot=-90,0,0 at=$w/2-0.13,1.6,-$t/2
  torus 0.055 0.006 rot=90,0,0 at=$w/2-0.13,1.545,-$t/2-0.015
  cyl 0.018 0.01 rot=-90,0,0 at=$w/2-0.13,1.48,-$t/2
end

# --- grips: on a face at the origin, standing out to +z
define door.grip.lever   # a lever on a round rose, pointing to the hinge; the key's escutcheon under it
  cyl 0.026 0.008 rot=90,0,0
  cyl 0.009 0.055 rot=90,0,0
  capsule 0.0095 0.13 rot=0,0,90 at=0.012,0,0.06
  cyl 0.016 0.006 rot=90,0,0 at=0,-0.075,0
end
define door.grip.knob   # a round knob on its rose
  cyl 0.028 0.008 rot=90,0,0
  cyl 0.008 0.045 rot=90,0,0
  sphere 0.029 centre=1 at=0,0,0.07 sides=20
end
define door.grip.pull   # a D-handle, hl long
  tube 0.013 0,-$hl/2,0 0,-$hl/2,0.06 0,$hl/2,0.06 0,$hl/2,0 bend=0.025
end
define door.grip.bar   # a bar pull on two legs, hl long (a cabinet's, a drawer's)
  tube 0.006 0,-$hl/2,0 0,-$hl/2,0.032 0,$hl/2,0.032 0,$hl/2,0 bend=0.008 sides=10
end
define door.grip.plate   # a push plate
  box 0.1 0.32 0.002 at=0,-0.16,0.001
end
define door.grip.panic w=0.9   # a push bar across a leaf w wide, its latch at the edge (an exit's)
  let a 0-$w+$hx+0.15
  box 0.0-$a+0.03 0.05 0.045 at=($a+0.03)/2,-0.025,0.05
  box 0.08 0.1 0.05 at=$a,-0.05,0.025
  box 0.08 0.1 0.05 at=0,-0.05,0.025
end
define door.grip.ring   # a drop ring on a round back-plate (a church door's, a cottage's latch, a Chinese door's)
  cyl 0.045 0.006 rot=90,0,0
  torus 0.065 0.009 rot=90,0,0 at=0,-0.07,0.022
end
define door.grip.recess   # a flush pull let into the face (a sliding door's)
  box 0.035 0.16 0.003 at=0,-0.08,0
end
define door.grip.hikite   # a fusuma's round pull, flush
  cyl 0.032 0.003 rot=90,0,0
  cyl 0.022 0.004 rot=90,0,0
end
define door.grip.button   # a small knob (a drawer's, a cupboard's)
  cyl 0.006 0.025 rot=90,0,0
  sphere 0.015 centre=1 at=0,0,0.032 sides=16
end
define door.grip.cup   # a cup pull: a hood to hook the fingers under
  box 0.1 0.034 0.003 at=0,-0.017,0.0015
  extrude 0.09 0,0.017 0.026,0.012 0.03,-0.004 0.024,-0.016 0.02,-0.016 0.023,-0.004 0.02,0.008 0,0.012 rot=0,-90,0
end
define door.grip.card   # an electronic lock: its body, the card's slot and light, a lever on it (a hotel's)
  box 0.07 0.29 0.022 at=0,-0.16,0.011
  box 0.04 0.004 0.004 at=0,0.07,0.022
  cyl 0.004 0.004 rot=90,0,0 at=0.02,0.09,0.022
  door.grip.lever at=0,0,0.022
end
define door.grip.latch   # a thumb latch: a handle and the thumb's lift over it (a cottage's)
  tube 0.009 0,-0.1,0 0,-0.08,0.04 0,0.08,0.04 0,0.1,0 bend=0.02 sides=10
  box 0.02 0.006 0.07 at=0,0.11,0.035
end
define door.grip.none
end
define door.grip.same w=0.9   # the door's own handle (what `back` and `handle2` say unless told otherwise)
  door.grip.$handle $w
end

# --- hinges, at the leaf's edge -x on the face it opens to (+z)
define door.hinge.butt w=0.9 h=2 t=0.045   # butt hinges: three, or four on a tall leaf - their knuckles on the axis
  let n if($h>2.25,4,3)
  for i $n
    cyl 0.0075 0.1 at=-$w/2,0.18+$i*($h-0.46)/($n-1),$t/2
  end
end
define door.hinge.strap w=0.9 h=2 t=0.05   # strap hinges across the outside face, flaring to a disc, nailed; their pintles on the axis
  let n if($h>2.3,3,2)
  for i $n
    let y 0.22+$i*($h-0.6)/($n-1)
    extrude 0.006 0,-0.035 $w*0.7,-0.014 $w*0.7,0.014 0,0.035 at=-$w/2,$y,-$t/2-0.003
    cyl 0.035 0.006 rot=-90,0,0 at=-$w/2+$w*0.7,$y,-$t/2
    cyl 0.014 0.12 at=-$w/2,$y-0.06,$t/2
  end
end
define door.hinge.pivot w=0.9 h=2 t=0.045   # pins in sockets at head and foot (an old Persian leaf's, a pivot door's)
  cyl 0.02 0.03 at=-$w/2+0.03,-0.02,0
  cyl 0.02 0.03 at=-$w/2+0.03,$h-0.01,0
end
define door.hinge.concealed w=0.9 h=2 t=0.045   # cup hinges behind a cabinet's front: nothing shows
end
define door.hinge.none w=0.9 h=2 t=0.045
end

# --- how the leaves move: each op lays them in an opening w wide, from `foot` up to h (or lh), in a wall t deep
define door.op.hinge w=0.9 h=2 t=0.15   # side-hung: one leaf, or a pair (ratio: the first's share - 0.67 a leaf and a half), on hinges at the jambs, opening to +z up to `range` degrees (both=1: either way, double-acting); dutch: each leaf cut across at that height, its halves hung apart
  let one if($leaves>1,$w*$ratio,$w)
  door.hung ${name}.1 $one $h $t $handle $knock at=-$w/2,0,0
  if $leaves>1
    group scale=-1,1,1
      door.hung ${name}.2 $w-$one $h $t $handle2 if($knock==2,3,$knock) at=-$w/2,0,0
    end
  end
end
define door.hung n=door.1 w=0.9 h=2 t=0.15 grip=lever knock=0   # one leaf on its hinges: the axis at its edge, on the face it opens to
  let top if($lh>0,$lh,$h)
  if $dutch>0
    door.hang $n $w-0.004 $foot $dutch-$foot $t $grip $knock
    door.hang ${n}.top $w-0.004 $dutch+0.004 $top-$dutch-0.007 $t none $knock
  else
    door.hang $n $w-0.004 $foot $top-$foot-0.003 $t $grip $knock
  end
end
define door.hang n=door.1 w=0.9 y=0.01 h=2 t=0.15 grip=lever knock=0   # (a leaf from y, h high, turning on its hinge)
  let a $open*$range
  moves $n turn if($both,0-$range,0)-$a $range-$a axis=-y at=0.002,0,$t/2
    group rot=0,-$a,0
      door.leaf $w $h $lt $grip $knock at=$w/2,$y,-$lt/2
    end
  end
end
define door.op.pivot w=1.2 h=2.4 t=0.2   # a leaf on a pivot set `piv` in from its edge, turning either way about it - a pivot door, a heavy entrance
  let a $open*$range
  let top if($lh>0,$lh,$h)
  moves ${name}.1 turn 0-$range-$a $range-$a axis=-y at=-$w/2+$piv,0,0
    group rot=0,-$a,0
      door.leaf $w-0.004 $top-$foot-0.003 $lt at=$w/2-$piv,$foot,0
    end
  end
end
define door.op.slide w=0.9 h=2 t=0.15   # leaves that slide: one (to -x), or a pair parting at the middle (leaves=2); mount 0 in the wall's own plane (a pocket door, into the wall), 1 hung from a track on the +z face (a barn door), 2 before the frame under a header (an automatic door, a lift's)
  let n min(max($leaves,1),2)
  let tr $w/$n
  let lw $tr+if($mount==1,0.1,0.02)
  let top if($lh>0,$lh,$h)
  let lh2 $top-$foot-0.003+($mount==1)*0.05
  let z if($mount==0,0,if($mount==1,$t/2+0.035+$lt/2,$t/2+$lt/2+0.012))
  door.glide ${name}.1 $lw $lh2 $tr at=-$w/2+$tr/2,$foot,$z
  if $n>1
    group scale=-1,1,1
      door.glide ${name}.2 $lw $lh2 $tr at=-$w/2+$tr/2,$foot,$z
    end
  end
  if $mount==1
    box 2*$w+0.3 0.05 0.03 at=0-($n==1)*$w/2,$top+0.09,$z+$lt/2+0.03 mat=$hw
  end
  if $mount==2
    box $w*2+0.2 0.22 0.2 at=0,$top,$z mat=$hw
    box 0.16 0.05 0.06 at=0,$top-0.05,$z+0.1 mat=$hw
  end
end
define door.glide n=door.1 w=0.9 h=2 tr=0.9   # (a leaf on its runners, sliding to -x up to tr; on a barn track, hung from its wheels)
  let o $open*$tr
  moves $n slide 0-$o $tr-$o axis=-x
    group at=0-$o,0,0
      door.leaf $w $h $lt
      if $mount==1
        mirror x
          box 0.05 0.16 0.008 at=$w*0.32,$h-0.06,$lt/2+0.004 mat=$hw
          cyl 0.05 0.025 rot=90,0,0 centre=1 at=$w*0.32,$h+0.13,$lt/2+0.03 mat=$hw
        end
      end
    end
  end
end
define door.op.bypass w=1.6 h=2 t=0.15   # leaves on parallel tracks, each passing the next: a closet's sliding doors, a patio's, shoji and fusuma (leaves: how many, front and back by turns)
  let n max(2,$leaves)
  let pw $w/$n+0.03
  let pitch ($w-$pw)/($n-1)
  let top if($lh>0,$lh,$h)
  for i $n
    let k $i+1
    let x -$w/2+$pw/2+$i*$pitch
    let z if(mod($i,2),-1,1)*($lt/2+0.004)
    let lo 0-if($i>=2,2*$pitch-$pw,$i*$pitch)
    let hi if($i<=$n-3,2*$pitch-$pw,($n-1-$i)*$pitch)
    let o ($i==0)*$open*$hi
    moves ${name}.${k} slide $lo-$o $hi-$o axis=x at=$x,0,$z
      group at=$o,0,0
        door.leaf $pw $top-$foot-0.003 $lt at=0,$foot,0
      end
    end
  end
end
define door.op.fold w=1.6 h=2 t=0.15   # a folding door: leaves hinged edge to edge folding to the jamb (halves=2: half of them to each), the first hung at the jamb, each after it on the one before, turning back twice as far - a bifold, an accordion
  let k max(1,floor($leaves/$halves))
  let p $w/$halves/$k
  let top if($lh>0,$lh,$h)
  door.fold ${name}.1 1 $k $p $top at=-$w/2,0,0
  if $halves>1
    group scale=-1,1,1
      door.fold ${name}.2 1 $k $p $top at=-$w/2,0,0
    end
  end
end
define door.fold n=fold i=1 k=2 p=0.4 h=2   # (the i-th of k folds, p wide, hung on the one before)
  let f if($i==1,0,if($i==2,-2,-1))
  let a $open*$range
  let b if($i==1,$a,if($i==2,-2*$a,2*$a))
  let ax if($i==1,0.002,$p)
  moves ${n}.${i} turn if($i==1,0-$a,0) if($i==1,$range-$a,0) axis=-y follow=$f at=$ax,0,0
    group rot=0,-$b,0
      if $i==$k
        door.leaf $p-0.004 $h-$foot-0.003 $lt at=$p/2,$foot,0
      else
        door.leaf $p-0.004 $h-$foot-0.003 $lt none at=$p/2,$foot,0
        door.fold $n $i+1 $k $p $h
      end
    end
  end
end
define door.op.revolve w=2.2 h=2.2 t=0.2   # a revolving door: wings (leaves: 3 or 4) about a post, each its own part, pushed round a turn between wings at a time (step) in a drum of curved glass between the two quadrants it opens on, under a canopy; w the drum's diameter
  let r $w/2
  let top if($lh>0,$lh,$h)
  let q 360/$leaves
  let a0 45-$open*$q
  for i $leaves
    let kk $i+1
    if $i==0
      moves ${name}.1 turn -360 360 axis=y step=$q
        cyl 0.05 $top sides=16 mat=$hw
        group rot=0,$a0,0
          door.leaf $r-0.08 $top-$foot-0.01 $lt at=$r/2+0.03,$foot,0
        end
      end
    else
      moves ${name}.${kk} turn -360 360 axis=y with=${name}.1
        group rot=0,$a0+$i*$q,0
          door.leaf $r-0.08 $top-$foot-0.01 $lt at=$r/2+0.03,$foot,0
        end
      end
    end
  end
  mirror x
    group rot=0,-39.375,0
      radial n=8 arc=90
        box 0.012 $top-0.02 $r*0.2 at=$r+0.03,0.01,0 mat=$gmat
      end
    end
  end
  group rot=0,45,0
    radial n=4
      box 0.07 $top 0.07 at=$r+0.03,0,0 mat=$hw
    end
  end
  cyl $r+0.1 0.3 at=0,$top,0 sides=48 mat=$hw
  cyl $r+0.1 0.008 sides=48 mat=$hw
end
define door.op.flap w=0.6 h=0.4 t=0.019   # a leaf hinged along its foot, falling open to +z (a bureau's fall front); hand=-1: along its head, lifting up and out (a wall cabinet's flap)
  let a $open*$range
  let lh3 if($lh>0,$lh,$h)-0.004
  if $hand>0
    moves ${name}.1 turn 0-$a $range-$a axis=x at=0,0.002,$t/2
      group rot=$a,0,0
        door.leaf $w-0.004 $lh3 $lt at=0,0,-$lt/2
      end
    end
  else
    moves ${name}.1 turn 0-$a $range-$a axis=-x at=0,$lh3+0.002,$t/2
      group rot=0-$a,0,0
        door.leaf $w-0.004 $lh3 $lt at=0,0-$lh3,-$lt/2
      end
    end
  end
end
define door.op.tilt w=2.4 h=2.1 t=0.2   # an up-and-over door: one panel on arms, its head swinging in (+z) and up as its foot swings out, about a pivot two thirds up
  let a $open*$range
  let top if($lh>0,$lh,$h)
  let py $top*0.66
  moves ${name}.1 turn 0-$a $range-$a axis=x at=0,$py,$t/2
    group rot=$a,0,0
      door.leaf $w-0.01 $top-$foot $lt at=0,$foot-$py,-$lt/2
    end
  end
  mirror x
    box 0.05 $top 0.06 at=$w/2+0.03,0,$t/2+0.03 mat=$hw
    box 0.05 0.05 $top*0.8 at=$w/2+0.03,$top+0.02,$t/2+$top*0.4 mat=$hw
    # its pivot: an axle from the guide to the panel's edge, on the line it turns about
    cyl 0.016 0.05 rot=0,0,90 centre=1 at=$w/2+0.01,$py,$t/2 sides=12 mat=$hw
  end
end
define door.op.roll w=2.4 h=2.2 t=0.2   # a roller shutter on the outside face: steel slats interlocked, each riding up its guides into a box over the opening and round the drum in it; all go together (shut as made)
  let top if($lh>0,$lh,$h)
  let s 0.077
  let n max(1,floor(($top-0.06)/$s))
  let z -$t/2-0.05
  let rc 0.12
  let cy $top+0.21
  let cz $z+$rc
  box $w+0.16 0.42 0.42 at=0,$top,$z+0.05 mat=$hw
  mirror x
    box 0.07 $top 0.09 at=$w/2+0.035,0,$z mat=$hw
  end
  for i $n
    let kk $i+1
    if $i==0
      moves ${name}.1 track 0 $top 0,-1,$z 0,$cy,$z 0,$cy+$rc*sin(30),$cz-$rc*cos(30) 0,$cy+$rc*sin(60),$cz-$rc*cos(60) 0,$cy+$rc*sin(90),$cz-$rc*cos(90) 0,$cy+$rc*sin(120),$cz-$rc*cos(120) 0,$cy+$rc*sin(150),$cz-$rc*cos(150) 0,$cy+$rc*sin(180),$cz-$rc*cos(180) 0,$cy+$rc*sin(210),$cz-$rc*cos(210) 0,$cy+$rc*sin(240),$cz-$rc*cos(240) 0,$cy+$rc*sin(270),$cz-$rc*cos(270) 0,$cy+$rc*sin(300),$cz-$rc*cos(300) 0,$cy+$rc*sin(330),$cz-$rc*cos(330) 0,$cy+$rc*sin(360),$cz-$rc*cos(360) 0,$cy+$rc*sin(30),$cz-$rc*cos(30) 0,$cy+$rc*sin(60),$cz-$rc*cos(60) 0,$cy+$rc*sin(90),$cz-$rc*cos(90) 0,$cy+$rc*sin(120),$cz-$rc*cos(120) 0,$cy+$rc*sin(150),$cz-$rc*cos(150) 0,$cy+$rc*sin(180),$cz-$rc*cos(180) 0,$cy+$rc*sin(210),$cz-$rc*cos(210) 0,$cy+$rc*sin(240),$cz-$rc*cos(240) 0,$cy+$rc*sin(270),$cz-$rc*cos(270) 0,$cy+$rc*sin(300),$cz-$rc*cos(300) 0,$cy+$rc*sin(330),$cz-$rc*cos(330) 0,$cy+$rc*sin(360),$cz-$rc*cos(360) 0,$cy+$rc*sin(30),$cz-$rc*cos(30) 0,$cy+$rc*sin(60),$cz-$rc*cos(60) 0,$cy+$rc*sin(90),$cz-$rc*cos(90) 0,$cy+$rc*sin(120),$cz-$rc*cos(120) 0,$cy+$rc*sin(150),$cz-$rc*cos(150) 0,$cy+$rc*sin(180),$cz-$rc*cos(180) 0,$cy+$rc*sin(210),$cz-$rc*cos(210) 0,$cy+$rc*sin(240),$cz-$rc*cos(240) 0,$cy+$rc*sin(270),$cz-$rc*cos(270) 0,$cy+$rc*sin(300),$cz-$rc*cos(300) 0,$cy+$rc*sin(330),$cz-$rc*cos(330) 0,$cy+$rc*sin(360),$cz-$rc*cos(360) span=$s from=1.06
        box $w 0.06 0.05 at=0,0,$z mat=$hw
        box $w $s-0.008 0.012 at=0,0.064,$z mat=$lmat
        cyl 0.006 $w rot=0,0,90 centre=1 at=0,0.06+$s-0.002,$z sides=8 mat=$lmat
      end
    else
      moves ${name}.${kk} track 0 $top 0,-1,$z 0,$cy,$z 0,$cy+$rc*sin(30),$cz-$rc*cos(30) 0,$cy+$rc*sin(60),$cz-$rc*cos(60) 0,$cy+$rc*sin(90),$cz-$rc*cos(90) 0,$cy+$rc*sin(120),$cz-$rc*cos(120) 0,$cy+$rc*sin(150),$cz-$rc*cos(150) 0,$cy+$rc*sin(180),$cz-$rc*cos(180) 0,$cy+$rc*sin(210),$cz-$rc*cos(210) 0,$cy+$rc*sin(240),$cz-$rc*cos(240) 0,$cy+$rc*sin(270),$cz-$rc*cos(270) 0,$cy+$rc*sin(300),$cz-$rc*cos(300) 0,$cy+$rc*sin(330),$cz-$rc*cos(330) 0,$cy+$rc*sin(360),$cz-$rc*cos(360) 0,$cy+$rc*sin(30),$cz-$rc*cos(30) 0,$cy+$rc*sin(60),$cz-$rc*cos(60) 0,$cy+$rc*sin(90),$cz-$rc*cos(90) 0,$cy+$rc*sin(120),$cz-$rc*cos(120) 0,$cy+$rc*sin(150),$cz-$rc*cos(150) 0,$cy+$rc*sin(180),$cz-$rc*cos(180) 0,$cy+$rc*sin(210),$cz-$rc*cos(210) 0,$cy+$rc*sin(240),$cz-$rc*cos(240) 0,$cy+$rc*sin(270),$cz-$rc*cos(270) 0,$cy+$rc*sin(300),$cz-$rc*cos(300) 0,$cy+$rc*sin(330),$cz-$rc*cos(330) 0,$cy+$rc*sin(360),$cz-$rc*cos(360) 0,$cy+$rc*sin(30),$cz-$rc*cos(30) 0,$cy+$rc*sin(60),$cz-$rc*cos(60) 0,$cy+$rc*sin(90),$cz-$rc*cos(90) 0,$cy+$rc*sin(120),$cz-$rc*cos(120) 0,$cy+$rc*sin(150),$cz-$rc*cos(150) 0,$cy+$rc*sin(180),$cz-$rc*cos(180) 0,$cy+$rc*sin(210),$cz-$rc*cos(210) 0,$cy+$rc*sin(240),$cz-$rc*cos(240) 0,$cy+$rc*sin(270),$cz-$rc*cos(270) 0,$cy+$rc*sin(300),$cz-$rc*cos(300) 0,$cy+$rc*sin(330),$cz-$rc*cos(330) 0,$cy+$rc*sin(360),$cz-$rc*cos(360) span=$s from=1.06+$i*$s with=${name}.1
        box $w $s-0.008 0.012 at=0,0.064+$i*$s,$z mat=$lmat
        cyl 0.006 $w rot=0,0,90 centre=1 at=0,0.06+($i+1)*$s-0.002,$z sides=8 mat=$lmat
      end
    end
  end
end
define door.op.sectional w=2.4 h=2.1 t=0.2   # an overhead sectional door inside the opening: panels hinged edge to edge, each riding a track by its top and its foot - straight up the jambs to the head, round a bend over it, back level under the ceiling (+z); all go together (open: tracks are ridden as made, shut)
  let top if($lh>0,$lh,$h)
  let k max(3,round($top/0.55))
  let ph $top/$k
  let z $t/2+0.05
  let r 0.3
  for i $k
    let kk $i+1
    if $i==0
      moves ${name}.1 track 0 $top+0.7 0,-1,$z 0,$top,$z 0,$top+$r*sin(15),$z+$r-$r*cos(15) 0,$top+$r*sin(30),$z+$r-$r*cos(30) 0,$top+$r*sin(45),$z+$r-$r*cos(45) 0,$top+$r*sin(60),$z+$r-$r*cos(60) 0,$top+$r*sin(75),$z+$r-$r*cos(75) 0,$top+$r,$z+$r 0,$top+$r,$z+$r+$top+1 span=$ph from=1
        door.body.ribbed $w-0.01 $ph-0.004 0.04 at=0,0,$z mat=$lmat
        box $w*0.3 0.04 0.03 at=0,0.08,$z+0.035 mat=$hw
        door.rollers $w 0.02 $z
      end
    else
      moves ${name}.${kk} track 0 $top+0.7 0,-1,$z 0,$top,$z 0,$top+$r*sin(15),$z+$r-$r*cos(15) 0,$top+$r*sin(30),$z+$r-$r*cos(30) 0,$top+$r*sin(45),$z+$r-$r*cos(45) 0,$top+$r*sin(60),$z+$r-$r*cos(60) 0,$top+$r*sin(75),$z+$r-$r*cos(75) 0,$top+$r,$z+$r 0,$top+$r,$z+$r+$top+1 span=$ph from=1+$i*$ph with=${name}.1
        door.body.ribbed $w-0.01 $ph-0.004 0.04 at=0,$i*$ph,$z mat=$lmat
        door.rollers $w $i*$ph $z
        if $i==$k-1
          door.rollers $w $top-0.02 $z
        end
      end
    end
  end
  mirror x
    let x $w/2+0.04
    # its track, the one the panels ride: up the jamb, round the bend, back under the ceiling
    sweep -0.025,-0.025 0.025,-0.025 0.025,0.025 -0.025,0.025 / $x,0,$z $x,$top,$z $x,$top+$r*sin(15),$z+$r-$r*cos(15) $x,$top+$r*sin(30),$z+$r-$r*cos(30) $x,$top+$r*sin(45),$z+$r-$r*cos(45) $x,$top+$r*sin(60),$z+$r-$r*cos(60) $x,$top+$r*sin(75),$z+$r-$r*cos(75) $x,$top+$r,$z+$r $x,$top+$r,$z+$r+$top+0.6 mat=$hw
  end
  cyl 0.03 $w rot=0,0,90 centre=1 at=0,$top+$r+0.15,$z sides=12 mat=$hw
end
define door.rollers w=2.4 y=0 z=0.15   # a pair of rollers on stems from a panel's edges into its tracks (x = +-(w/2+0.04)), at height y on the line z
  mirror x
    cyl 0.008 0.05 rot=0,0,90 centre=1 at=$w/2+0.015,$y,$z sides=8 mat=$hw
    cyl 0.022 0.02 rot=0,0,90 centre=1 at=$w/2+0.04,$y,$z sides=12 mat=$hw
  end
end
define door.frame.casing w=1 h=2.1 t=0.15   # a timber lining through the wall, an architrave on both faces - moulded round a square head, banded round an arch; a threshold
  let arise $w*$door_rise_${head}
  let s $h-$arise
  box 0.03 $s $t at=-$w/2-0.015,0,0 mat=$fmat
  box 0.03 $s $t at=$w/2+0.015,0,0 mat=$fmat
  if $arise>0
    door.ring.$head $w $arise 0.03 $t at=0,$s,0 mat=$fmat
    mirror z
      box $band $s 0.022 at=-$w/2-0.03-$band/2,0,$t/2+0.011 mat=$fmat
      box $band $s 0.022 at=$w/2+0.03+$band/2,0,$t/2+0.011 mat=$fmat
      door.ring.$head $w+0.06 $arise+0.03*$arise/$w*2 $band 0.022 at=0,$s,$t/2+0.011 mat=$fmat
    end
  else
    box $w+0.06 0.03 $t at=0,$h,0 mat=$fmat
    mirror z
      mould.frame $w+0.06 $h+0.03 $band 0.022 at=0,0,$t/2 mat=$fmat
    end
  end
  if $sill
    box $w+0.06 0.02 $t+0.03 mat=$fmat
  end
end
define door.frame.metal w=1 h=2.1 t=0.15   # a pressed steel frame: its jambs and head wrapping the wall, a flat face each side
  box 0.05 $h $t+0.02 at=-$w/2-0.025,0,0 mat=$fmat
  box 0.05 $h $t+0.02 at=$w/2+0.025,0,0 mat=$fmat
  box $w+0.1 0.05 $t+0.02 at=0,$h,0 mat=$fmat
  if $sill
    box $w 0.012 $t mat=$fmat
  end
end
define door.frame.stone w=1 h=2.1 t=0.3   # dressed stone: jambs standing proud on the outside (-z), a lintel with its keystone over a square head, a ring of voussoirs round an arch; a sill, and a step if said
  let arise $w*$door_rise_${head}
  let s $h-$arise
  let p 0.06
  box 0.28 $s $t+$p at=-$w/2-0.14,0,-$p/2 mat=$fmat
  box 0.28 $s $t+$p at=$w/2+0.14,0,-$p/2 mat=$fmat
  if $arise>0
    door.ring.$head $w $arise 0.28 $t+$p at=0,$s,-$p/2 mat=$fmat
    mould.keystone 0.3 0.42 0.05 at=0,$h-0.1,-$t/2-$p mat=$fmat rot=0,180,0
  else
    box $w+0.56 0.34 $t+$p at=0,$h,-$p/2 mat=$fmat
    mould.keystone 0.26 0.4 0.04 at=0,$h-0.02,-$t/2-$p rot=0,180,0 mat=$fmat
  end
  if $sill
    box $w+0.56 0.04 $t+$p+0.04 at=0,0,-$p/2-0.02 mat=$fmat
  end
  if $step>0
    box $w+0.8 $step 0.4 at=0,-$step,-$t/2-0.2 mat=$fmat
  end
end
define door.frame.tile w=1.6 h=3 t=0.4   # a portal of glazed tile (an alfiz, a pishtaq's frame): a broad band of tile standing round the opening, its spandrels over the arch filled, a ring of stone round the arch itself; a step of stone
  let arise $w*$door_rise_${head}
  let s $h-$arise
  let b 0.36
  box $b $h+$b $t+0.08 at=-$w/2-$b/2-0.12,0,-0.04 mat=tile
  box $b $h+$b $t+0.08 at=$w/2+$b/2+0.12,0,-0.04 mat=tile
  box $w+0.24+2*$b $b $t+0.08 at=0,$h+0.12,-0.04 mat=tile
  box 0.12 $s $t+0.04 at=-$w/2-0.06,0,-0.02 mat=stone
  box 0.12 $s $t+0.04 at=$w/2+0.06,0,-0.02 mat=stone
  if $arise>0
    door.ring.$head $w $arise 0.12 $t+0.04 at=0,$s,-0.02 mat=stone
    group mat=tile
      box $w+0.24 $arise+0.12 $t*0.5 at=0,$s,-$t/4-0.02
      sub door.arch.$head $w+0.2 $arise+0.1 $t*2 at=0,$s,0
    end
  else
    box $w+0.24 0.12 $t+0.04 at=0,$h,-0.02 mat=stone
  end
  box $w+0.24+2*$b 0.14 0.5 at=0,-0.14,-$t/2-0.25 mat=stone
end
define door.frame.timber w=1.8 h=1.8 t=0.15   # posts and beams standing proud of the wall (Japanese hashira, a kamoi over the opening, the grooved shikii under it; a Chinese frame), the arch ignored
  box 0.12 $h+0.14 $t+0.04 at=-$w/2-0.06,0,0 mat=$fmat
  box 0.12 $h+0.14 $t+0.04 at=$w/2+0.06,0,0 mat=$fmat
  box $w+0.24 0.06 $t+0.02 at=0,$h,0 mat=$fmat
  box $w+0.24 0.08 $t at=0,$h+0.06,0 mat=$fmat
  box $w+0.24 0.1 $t+0.06 at=0,$h+0.14,0 mat=$fmat
  if $sill
    box $w 0.03 $t at=0,-0.03,0 mat=$fmat
  end
end
define door.frame.post w=1 h=1.1 t=0.1   # gate posts, capped
  box 0.12 $h+0.15 0.12 at=-$w/2-0.06,0,0 mat=$fmat
  box 0.12 $h+0.15 0.12 at=$w/2+0.06,0,0 mat=$fmat
  pyramid 0.16 0.08 at=-$w/2-0.06,$h+0.15,0 mat=$fmat
  pyramid 0.16 0.08 at=$w/2+0.06,$h+0.15,0 mat=$fmat
end
define door.frame.none w=1 h=2 t=0.1   # nothing round it (a cabinet's front, a door in a frame of its own)
end

# --- the head: an arch w across, rising over its spring (y = 0) to `rise`, through z (`arch`); the ring t round it (`ring`)
define door.arch.square w=1 rise=0 d=0.1   # (no arch)
end
define door.ring.square w=1 rise=0 t=0.1 d=0.1
end
define door.arch.round w=1 rise=0.5 d=0.1
  pointed.arch $w $rise 0 $d
end
define door.arch.pointed w=1 rise=0.87 d=0.1
  pointed.arch $w $rise 0.5 $d
end
define door.arch.lancet w=1 rise=1.1 d=0.1
  pointed.arch $w $rise 1 $d
end
define door.arch.persian w=1 rise=0.67 d=0.1
  pointed.arch $w $rise 0.2 $d
end
define door.arch.tudor w=1 rise=0.3 d=0.1
  pointed.tudor $w $rise $d
end
define door.arch.horseshoe w=1 rise=0.87 d=0.1
  girih.horseshoe $w $rise $d
end
define door.arch.segment w=1 rise=0.2 d=0.1   # a segment of a circle through the springers and the crown
  let R ($w*$w/4+$rise*$rise)/(2*$rise)
  let p asin($w/2/$R)
  let cy $rise-$R
  extrude $d -$w/2,0 $w/2,0 $R*sin($p*0.75),$cy+$R*cos($p*0.75) $R*sin($p*0.5),$cy+$R*cos($p*0.5) $R*sin($p*0.25),$cy+$R*cos($p*0.25) 0,$rise $R*sin(0-$p*0.25),$cy+$R*cos($p*0.25) $R*sin(0-$p*0.5),$cy+$R*cos($p*0.5) $R*sin(0-$p*0.75),$cy+$R*cos($p*0.75)
end
define door.ring.round w=1 rise=0.5 t=0.1 d=0.1
  pointed.band $w $rise+0.0005 0 $t $d at=0,-0.0005,0
end
define door.ring.pointed w=1 rise=0.87 t=0.1 d=0.1
  pointed.band $w $rise+0.0005 0.5 $t $d at=0,-0.0005,0
end
define door.ring.lancet w=1 rise=1.1 t=0.1 d=0.1
  pointed.band $w $rise+0.0005 1 $t $d at=0,-0.0005,0
end
define door.ring.persian w=1 rise=0.67 t=0.1 d=0.1
  pointed.band $w $rise+0.0005 0.2 $t $d at=0,-0.0005,0
end
define door.ring.segment w=1 rise=0.2 t=0.1 d=0.1   # (a voussoir ring: the arch t larger, less the arch)
  group
    door.arch.segment $w+2*$t $rise+$t $d at=0,0,0
    box $w+2*$t 0.0001 $d at=0,0,0
    sub door.arch.segment $w $rise $d*2
  end
end
define door.ring.tudor w=1 rise=0.3 t=0.1 d=0.1
  group
    box $w+2*$t $rise+$t $d
    sub door.arch.tudor $w $rise $d*2
  end
end
define door.ring.horseshoe w=1 rise=0.87 t=0.1 d=0.1
  group
    box $w*1.16+2*$t $rise+$t $d
    sub door.arch.horseshoe $w $rise $d*2
  end
end

# --- what fills the arch over the leaves
define door.tymp.glass w=1 rise=0.5 t=0.045   # a fanlight: glass, its bars radiating from the middle of its foot, a transom bar under it
  door.arch.$head $w $rise 0.006 mat=$gmat
  box $w 0.05 $t at=0,0,0 mat=$lmat
  for i 5
    box 0.018 $rise*0.96 0.03 rot=0,0,90-($i+1)*30 at=0,0.05,0 mat=$lmat
  end
end
define door.tymp.lattice w=1 rise=0.5 t=0.045   # a fanlight of many bars
  door.arch.$head $w $rise 0.006 mat=$gmat
  box $w 0.05 $t at=0,0,0 mat=$lmat
  for i 11
    box 0.014 $rise*0.96 0.025 rot=0,0,90-($i+1)*15 at=0,0.05,0 mat=$lmat
  end
end
define door.tymp.panel w=1 rise=0.5 t=0.045   # a tympanum of boards, set back
  door.arch.$head $w $rise $t*0.6 mat=$lmat
end
define door.tymp.star w=1 rise=0.5 t=0.045   # a carved tympanum, a star at its middle
  door.arch.$head $w $rise $t*0.6 mat=$lmat
  mirror z
    girih.star 8 min($w*0.3,$rise*0.4) 0.01 at=0,$rise*0.45,$t*0.3 mat=$lmat
  end
end
define door.tymp.tile w=1 rise=0.5 t=0.045   # a tympanum of tile, a star of it
  door.arch.$head $w $rise $t mat=tile
  mirror z
    girih.star 10 min($w*0.32,$rise*0.42) 0.01 at=0,$rise*0.45,$t/2 mat=gold
  end
end
define door.tymp.none w=1 rise=0.5 t=0.045
end

# --- drawers and cabinets: their fronts are leaves, the spec's, made 19 mm thick
define drawer w=0.5 h=0.18 d=0.5 name=$name open=$open reach=0.75 lt=0.019 handle=bar hboth=0 hdir=1 hx=-1 hy=$h/2 hl=0.16 hinge=none kick=0 peep=0 closer=0 letter=0 knock=0   # a drawer: its front (a leaf of the spec), its box behind, running out to +z on its runners up to `reach` of its depth; its front's back on z = 0, foot on y = 0
  let tr $reach*$d
  let o $open*$tr
  let bh $h*0.72
  let bd $d-0.03
  moves $name slide 0-$o $tr-$o axis=z
    group at=0,0,$o
      door.leaf $w $h $lt at=0,0,$lt/2
      group mat=$lmat
        box 0.012 $bh $bd at=-$w/2+0.04,0.03,-$bd/2
        box 0.012 $bh $bd at=$w/2-0.04,0.03,-$bd/2
        box $w-0.08 $bh 0.012 at=0,0.03,-$bd+0.006
        box $w-0.08 0.008 $bd at=0,0.03,-$bd/2
      end
    end
  end
end
define cabinet w=0.6 h=0.87 d=0.58 name=$name bays=1 drawers=1 doors=2 dh=0.15 dfirst=1 grad=0 shelves=1 plinth=0.1 worktop=0.03 ct=0.018 cmat=$lmat wmat=stone cop=hinge hpos=1 open=$open lt=0.019 handle=bar dhandle=$handle hboth=0 hx=0.045 hl=0.16 hinge=concealed range=110 foot=0.0015 kick=0 peep=0 closer=0 letter=0 knock=0 sill=0   # a carcase w by h by d (fronts and all) on a plinth under a worktop, in `bays` side by side: in each, `drawers` drawers (on top, or dfirst=0 below) and under them `doors` doors (1 or 2; cop=bypass slides them, flap lets one fall) round `shelves` shelves; with no doors the drawers fill the bay, deepening down by `grad`
  let H $h-$plinth-$worktop
  let bw $w/$bays
  let cd $d-$lt
  let z0 $d/2-$lt
  let hi $h-$worktop
  group mat=$cmat
    box $ct $H $cd at=-$w/2+$ct/2,$plinth,-$lt/2
    box $ct $H $cd at=$w/2-$ct/2,$plinth,-$lt/2
    box $w-2*$ct $ct $cd at=0,$plinth,-$lt/2
    box $w-2*$ct $ct $cd at=0,$hi-$ct,-$lt/2
    box $w-2*$ct $H-2*$ct 0.006 at=0,$plinth+$ct,-$d/2+0.003
    for b $bays-1
      box $ct $H-2*$ct $cd at=-$w/2+$bw*($b+1),$plinth+$ct,-$lt/2
    end
    if $plinth>0
      box $w $plinth $cd-0.06 at=0,0,-$lt/2-0.03
    end
  end
  if $worktop>0
    box $w+0.02 $worktop $d+0.02 at=0,$hi,0.01 mat=$wmat
  end
  let dz if($doors>0,$drawers*$dh,$H)
  let dzy if($dfirst,$hi-$dz,$plinth)
  let zh $H-$dz
  let zy if($dfirst,$plinth,$plinth+$dz)
  let W $drawers+$grad*$drawers*($drawers-1)/2
  for b $bays
    let x -$w/2+$bw*($b+0.5)
    let bn $b+1
    for j $drawers
      let jn $j+1
      let u $dz/$W
      let dj if($doors>0,$dh,$u*(1+$grad*$j))
      let yj if($doors>0,$dzy+$dz-($j+1)*$dh,$dzy+$dz-$u*($j+1+$grad*($j+1)*$j/2))
      drawer $bw-0.003 $dj-0.003 $cd-0.04 name=${name}.${bn}.d${jn} handle=$dhandle hl=$hl at=$x,$yj+0.0015,$z0
    end
    if $doors*($zh>0.05)
      for i $shelves
        box $bw-2*$ct $ct $cd-0.03 at=$x,$zy+($i+1)*$zh/($shelves+1),-$lt/2-0.015 mat=$cmat
      end
      door $bw-0.003 $zh-0.003 $lt name=${name}.${bn} op=$cop leaves=$doors hand=if(mod($b,2),-1,1)*$hand swing=1 surround=none hole=0 hy=if($hpos,$zh-0.08,0.08) at=$x,$zy+0.0015,$z0+$lt/2
    end
  end
end

# ===== the doors of the world: each a set of choices, any of them said again to change it =====
define door.panel w=0.84 h=2.04 t=0.14 cols=2 rows=3 toprow=0.55 botrow=1.1 fill=panel stile=0.1 rail=0.1 brail=0.2 bar=0.1 lt=0.04 handle=knob hy=0.95   # an English interior door: six raised and fielded panels (the Georgian pattern), its top row short, a knob, a moulded architrave
  door $w $h $t
end
define door.shaker w=0.84 h=2.04 t=0.14 rows=2 fill=flat lt=0.04 handle=lever   # a plain interior door: two flat panels sunk in stiles and rails, a lever
  door $w $h $t
end
define door.victorian w=0.9 h=2.1 t=0.3 cols=2 rows=2 toprow=1.5 fill=panel lt=0.05 handle=knob letter=0.95 knock=1 transom=0.45 fan=glass step=0.15 band=0.11   # a Victorian front door: four panels, the upper pair taller, a fanlight in a transom over it, letter plate and knocker
  door $w $h $t
end
define door.georgian w=0.95 h=2.2 t=0.4 cols=2 rows=3 toprow=0.55 fill=panel lt=0.05 handle=knob knock=1 letter=0.9 head=round fan=glass surround=stone fmat=stone step=0.16   # a Georgian front door: six panels under a round fanlight in a stone surround with its keystone; a knocker, a step
  door $w $h $t
end
define door.french w=1.4 h=2.15 t=0.25 leaves=2 cols=3 rows=5 fill=glass stile=0.075 rail=0.075 brail=0.18 bar=0.025 handle=lever hx=0.04 transom=0.4 fan=glass   # French doors: a pair, each glazed in fifteen lights between slender bars, a transom light over them
  door $w $h $t
end
define door.cottage w=0.8 h=1.95 t=0.35 body=boards lt=0.05 hinge=strap handle=latch hy=1.05 surround=stone fmat=stone   # a cottage door: boards, ledged and braced, strap hinges, a thumb latch, in stone
  door $w $h $t
end
define door.stable w=0.95 h=2.0 t=0.3 body=boards lt=0.05 dutch=1.05 hinge=strap handle=latch hy=0.95   # a stable (Dutch) door: boards cut across, the halves hung apart
  door $w $h $t
end
define door.barn w=2.2 h=2.4 t=0.3 op=slide mount=1 body=boards lt=0.05 handle=pull hl=0.3 hx=0.15 hboth=0 surround=none sill=0   # a barn door: boards on a Z of ledges and braces, hung from wheels on a track over the opening, sliding aside
  door $w $h $t
end
define door.pocket w=0.82 h=2.04 t=0.15 op=slide mount=0 body=flush lt=0.04 handle=recess hx=0.05 hinge=none   # a pocket door: a flush leaf sliding into the wall, its pull let into its face
  door $w $h $t
end
define door.hotel w=0.915 h=2.134 t=0.2 body=flush lt=0.045 handle=lever back=card hy=1.0 peep=1.52 closer=1 kick=0.25 surround=metal fmat=paintedmetal   # a hotel room's door: flush and solid, a card lock and lever outside and a lever in, a viewer, a closer on the room side, a kick plate, butt hinges in a steel frame
  door $w $h $t
end
define door.hospital w=1.24 h=2.1 t=0.2 leaves=2 ratio=0.67 body=flush lt=0.054 vw=0.15 vh=0.75 vy=1.0 vx=0.12 handle=pull back=plate handle2=none hl=0.3 hx=0.08 hy=1.05 kick=0.9 closer=1 surround=metal fmat=paintedmetal   # a hospital's ward door: a leaf and a half, flush, wide enough for a bed; vision panels, pull handles one side and push plates the other, armour plates against trolleys, closers, a steel frame
  door $w $h $t
end
define door.exit w=1.8 h=2.1 t=0.25 leaves=2 body=flush lt=0.054 lmat=paintedmetal vw=0.2 vh=0.3 vy=1.35 handle=none back=panic hy=1.0 hx=0.07 closer=1 surround=metal fmat=paintedmetal swing=-1   # a fire exit: a pair of steel leaves with vision panels, panic bars on the side they are pushed from, closers
  door $w $h $t
end
define door.office w=0.926 h=2.1 t=0.15 body=flush lt=0.044 vw=0.12 vh=1.2 vy=0.6 vx=0.25 handle=lever closer=1 surround=metal fmat=metal   # an office door: flush, a vision strip by the lock, a lever, a closer, a steel frame
  door $w $h $t
end
define door.glass w=1.0 h=2.3 t=0.2 op=pivot piv=0.06 both=1 body=glass lt=0.012 handle=pull hl=0.8 hx=0.1 hy=1.05 hinge=none surround=metal fmat=metal sill=0   # a shop's glass door: toughened glass on a floor pivot, long bar pulls both sides
  door $w $h $t
end
define door.automatic w=1.8 h=2.2 t=0.15 op=slide mount=2 leaves=2 fill=glass upper=glass stile=0.05 rail=0.05 brail=0.1 lmat=metal hinge=none handle=none surround=metal fmat=metal sill=0   # an automatic door: two leaves of glass in slim aluminium frames parting from the middle before two fixed lights, a header with its drive and its eye
  door $w $h $t sidelight=$w/2
end
define door.revolving w=2.2 h=2.3 t=0.3 op=revolve leaves=4 fill=glass stile=0.05 rail=0.05 brail=0.15 lmat=metal hinge=none handle=bar hdir=1 hx=-1 hl=0.5 surround=none   # a revolving door: four wings of glass in a drum, under a canopy
  door $w $h $t
end
define door.lift w=1.1 h=2.1 t=0.12 op=slide mount=2 leaves=2 body=flush lt=0.03 lmat=metal hinge=none handle=none surround=metal fmat=metal   # a lift's landing doors: two flush steel leaves parting at the middle under a header, in a steel surround
  door $w $h $t
end
define door.bifold w=1.2 h=2.04 t=0.12 op=fold leaves=4 halves=2 range=85 fill=louvre stile=0.06 rail=0.06 brail=0.09 lt=0.03 handle=button hx=0.03 hboth=0 hinge=none sill=0   # a closet's bifold doors: four louvred leaves, two folding to each jamb
  door $w $h $t
end
define door.accordion w=1.6 h=2.04 t=0.12 op=fold leaves=6 halves=1 range=80 body=flush lt=0.02 handle=recess hx=0.04 hinge=none sill=0   # an accordion door: narrow flush leaves folding one on the next to one side
  door $w $h $t
end
define door.closet w=1.8 h=2.1 t=0.12 op=bypass leaves=2 fill=mirror stile=0.04 rail=0.04 brail=0.06 lt=0.03 lmat=metal handle=recess hx=0.03 hinge=none sill=0   # a wardrobe's sliding doors: two mirrored leaves passing on two tracks
  door $w $h $t
end
define door.patio w=2.4 h=2.1 t=0.15 op=bypass leaves=2 fill=glass stile=0.07 rail=0.07 brail=0.1 lt=0.05 lmat=metal handle=pull hl=0.25 hx=0.04 hinge=none surround=metal fmat=metal   # a patio door: two great panes in aluminium frames, one sliding past the other
  door $w $h $t
end
define door.saloon w=1.0 h=2.1 t=0.15 leaves=2 both=1 foot=0.35 lh=1.6 rows=2 fill=louvre upper=louvre stile=0.08 rail=0.08 brail=0.1 lt=0.035 handle=none sill=0   # saloon doors: a pair of short louvred leaves at waist height, swinging both ways
  door $w $h $t
end
define door.screen w=0.9 h=2.04 t=0.15 rows=2 fill=mesh upper=mesh stile=0.07 rail=0.07 bar=0.09 brail=0.2 lt=0.03 handle=pull hl=0.15 closer=1 swing=-1   # a screen door: a light frame of mesh opening out, a bar across its middle, a spring to shut it
  door $w $h $t
end
define door.shoji w=1.8 h=1.8 t=0.15 op=bypass leaves=2 rows=2 botrow=0.22 fill=flat upper=shoji split=1 gx=0.2 gy=0.27 stile=0.03 rail=0.03 brail=0.05 bar=0.03 lt=0.03 handle=recess hx=0.04 hinge=none surround=timber   # shoji: sliding screens of paper on a lattice of kumiko, a board at their foot, in the grooves of a shikii under a kamoi
  door $w $h $t
end
define door.fusuma w=1.8 h=1.8 t=0.15 op=bypass leaves=2 fill=paper stile=0.02 rail=0.02 brail=0.02 lt=0.02 handle=hikite hx=0.07 hy=0.85 hinge=none surround=timber   # fusuma: sliding panels of paper in thin lacquered frames, a round pull let into each
  door $w $h $t
end
define door.chinese w=1.5 h=2.4 t=0.3 leaves=2 rows=3 botrow=1.1 toprow=2.6 fill=flat upper=lattice split=2 gx=0.1 gy=0.1 stile=0.07 rail=0.07 bar=0.06 lt=0.05 handle=ring hy=1.1 hinge=pivot surround=timber   # a Chinese pair (geshan): each leaf a lattice above, a waist panel and a skirt panel below, ring pulls, in a frame of posts and beams
  door $w $h $t
end
define door.persian w=1.5 h=2.3 t=0.5 leaves=2 rows=4 fill=star stile=0.1 rail=0.1 bar=0.09 lt=0.07 studs=0.16 knock=2 handle=none hinge=pivot head=persian fan=tile surround=tile   # a Persian pair: carved panels of eight-point stars, rows of studs, and two knockers - the heavy kubeh on the left leaf, the slender halgheh on the right, each knock its own sound - turning on pivots under a pointed arch, its tympanum a star of tile, in a portal of tile
  door $w $h $t
end
define door.moorish w=1.6 h=2.5 t=0.5 leaves=2 body=boards lt=0.07 studs=0.12 handle=ring hy=1.2 hinge=pivot head=horseshoe fan=panel surround=tile   # a Moorish (Moroccan) door: boards studded with iron in rows, rings to pull, under a horseshoe arch in a portal of tile
  door $w $h $t
end
define door.gothic w=1.8 h=2.6 t=0.6 leaves=2 body=boards lt=0.07 hinge=strap handle=ring hy=1.15 head=pointed fan=panel surround=stone fmat=stone step=0.15   # a church's west door: a pair of oak boards ledged behind, strap hinges, ring handles, under a pointed arch of stone over a tympanum
  door $w $h $t
end
define door.romanesque w=1.3 h=2.2 t=0.7 body=boards lt=0.08 studs=0.15 hinge=strap handle=ring hy=1.1 head=round fan=panel surround=stone fmat=stone   # a Romanesque door: boards studded with iron, strap hinges, under a round arch of voussoirs
  door $w $h $t
end
define door.tudor w=1.0 h=2.0 t=0.5 body=boards lt=0.06 studs=0.11 hinge=strap handle=ring hy=1.05 head=tudor fan=panel surround=stone fmat=stone   # a Tudor door: boards studded with nails under a four-centred arch of stone
  door $w $h $t
end
define door.gate w=1.0 h=1.1 t=0.1 body=pickets lt=0.04 handle=latch hy=0.8 hinge=strap surround=post fmat=wood sill=0 hole=0   # a garden gate: pickets on rails, braced, between capped posts, a thumb latch
  door $w $h $t
end
define door.vault w=1.8 h=1.8 t=0.8 body=round lt=0.5 lmat=metal handle=ring hx=-1 hy=0.9 hboth=0 surround=metal fmat=metal sill=0 range=120   # a bank vault's door: a round slab half a metre thick, its ring of bolts, a wheel to throw them, on heavy hinges in a steel frame
  door $w $h $t
end
define door.garage w=2.4 h=2.1 t=0.25 op=sectional lmat=paintedmetal surround=metal fmat=metal sill=0   # a sectional garage door: ribbed steel panels running up and back under the ceiling
  door $w $h $t
end
define door.roller w=2.4 h=2.4 t=0.25 op=roll lmat=metal surround=none sill=0   # a roller shutter: steel slats rolling up into a box over the opening
  door $w $h $t
end
define door.upandover w=2.3 h=2.0 t=0.25 op=tilt range=88 body=ribbed lmat=paintedmetal handle=pull hx=-1 hy=0.5 hdir=1 hboth=0 hinge=none surround=metal fmat=metal sill=0   # an up-and-over garage door: one ribbed panel swinging up on its arms
  door $w $h $t
end

# ===== cabinets: carcases whose fronts are leaves (the cabinet's own words passed to it, the leaves' reaching them) =====
define cabinet.kitchen w=0.6 h=0.9 d=0.6 fill=flat stile=0.06 rail=0.06 brail=0.06   # a kitchen's base unit: a drawer over a pair of Shaker doors round a shelf, on a plinth under a stone worktop, bar handles
  cabinet $w $h $d drawers=1 doors=2
end
define cabinet.wall w=0.6 h=0.72 d=0.33 fill=flat stile=0.06 rail=0.06 brail=0.06   # a kitchen's wall unit: two Shaker doors on two shelves, the handles at their foot
  cabinet $w $h $d drawers=0 doors=2 shelves=2 plinth=0 worktop=0 hpos=0
end
define cabinet.display w=0.9 h=1.9 d=0.4 fill=glass stile=0.05 rail=0.05 brail=0.06   # a display cabinet: glazed doors over three shelves, a drawer under them, knobs
  cabinet $w $h $d drawers=1 doors=2 dfirst=0 shelves=3 worktop=0.025 wmat=wood handle=button dhandle=button hpos=0
end
define cabinet.larder w=0.6 h=2.1 d=0.6 rows=2 fill=flat stile=0.06 rail=0.06 brail=0.06 bar=0.06   # a tall larder: one door on five shelves, on a plinth
  cabinet $w $h $d drawers=0 doors=1 shelves=5 worktop=0 hpos=0 hl=0.3
end
define cabinet.medicine w=0.5 h=0.65 d=0.15 fill=mirror stile=0.02 rail=0.02 brail=0.02   # a bathroom's wall cabinet: one mirrored door on two shelves
  cabinet $w $h $d drawers=0 doors=1 shelves=2 plinth=0 worktop=0 handle=none
end
define cabinet.flap w=0.8 h=0.36 d=0.33 fill=flat stile=0.06 rail=0.06 brail=0.06   # a wall unit over a cooker: one flap lifting up and out
  cabinet $w $h $d drawers=0 doors=1 cop=flap hand=-1 range=95 shelves=0 plinth=0 worktop=0 hx=-1 hdir=1 hpos=0
end
define chest w=0.9 h=0.85 d=0.5 fill=panel stile=0.04 rail=0.04 brail=0.04   # a chest of drawers: five, deepening downward, panelled fronts, knobs
  cabinet $w $h $d drawers=5 doors=0 grad=0.18 plinth=0.08 worktop=0.025 wmat=wood dhandle=knob
end
define nightstand w=0.45 h=0.6 d=0.4 fill=flat stile=0.04 rail=0.04 brail=0.04   # a bedside table: a drawer over a cupboard, button knobs
  cabinet $w $h $d drawers=1 doors=1 dh=0.14 plinth=0.12 worktop=0.02 wmat=wood handle=button dhandle=button
end
define sideboard w=1.8 h=0.85 d=0.5 fill=panel stile=0.05 rail=0.05 brail=0.05   # a sideboard: three bays, a drawer over a door in each
  cabinet $w $h $d bays=3 drawers=1 doors=1 dh=0.16 plinth=0.12 worktop=0.03 wmat=wood handle=knob dhandle=knob
end
define apothecary w=0.9 h=1.2 d=0.4 fill=flat stile=0.02 rail=0.02 brail=0.02   # an apothecary's chest: four bays of six small drawers, button knobs
  cabinet $w $h $d bays=4 drawers=6 doors=0 plinth=0.06 worktop=0.02 wmat=wood dhandle=button
end
define filing w=0.47 h=1.32 d=0.62 body=flush lmat=paintedmetal   # a filing cabinet: four steel drawers running right out, cup pulls
  cabinet $w $h $d drawers=4 doors=0 plinth=0.03 worktop=0 cmat=paintedmetal dhandle=cup
end
define wardrobe w=1.0 h=2.0 d=0.6 rows=3 toprow=1.3 fill=panel stile=0.06 rail=0.06 brail=0.08 bar=0.06   # a wardrobe: two tall panelled doors over a pair of drawers
  cabinet $w $h $d drawers=2 doors=2 dfirst=0 dh=0.2 plinth=0.08 worktop=0.03 wmat=wood handle=knob dhandle=knob hpos=0
end
define wardrobe.sliding w=1.8 h=2.2 d=0.65 fill=mirror stile=0.04 rail=0.04 brail=0.06 lmat=metal   # a fitted wardrobe: two mirrored doors sliding past each other
  cabinet $w $h $d drawers=0 doors=2 cop=bypass plinth=0.05 worktop=0 lt=0.03 handle=recess hx=0.03
end
define bureau w=0.9 h=1.0 d=0.5 fill=panel stile=0.05 rail=0.05 brail=0.05   # a bureau: a fall front over three drawers - let down, a desk
  cabinet $w $h $d drawers=3 doors=1 dfirst=0 dh=0.17 cop=flap hand=1 range=90 shelves=0 plinth=0.08 worktop=0.025 wmat=wood handle=button hx=-1 dhandle=knob hpos=1
end
define tansu w=1.0 h=0.9 d=0.42 name=tansu fill=solid stile=0.02 rail=0.02 brail=0.02 hw=iron   # a Japanese chest (tansu): a stack of drawers beside a sliding pair over a deep drawer, iron pulls
  cabinet $w/2 $h $d name=${name}.l drawers=4 doors=0 plinth=0.05 worktop=0.02 wmat=wood dhandle=ring at=-$w/4,0,0
  cabinet $w/2 $h $d name=${name}.r drawers=1 doors=2 dh=0.2 cop=bypass plinth=0.05 worktop=0.02 wmat=wood handle=recess dhandle=ring at=$w/4,0,0
end
define locker w=0.4 h=1.8 d=0.5 rows=3 toprow=0.2 botrow=0.2 fill=louvre upper=louvre split=2 stile=0.03 rail=0.03 brail=0.03 bar=0.03 lmat=paintedmetal   # a steel locker: one door louvred at its head and foot, a pull
  cabinet $w $h $d drawers=0 doors=1 plinth=0 worktop=0 cmat=paintedmetal handle=pull hl=0.12 hpos=0
end
)LIB";
}

}  // namespace sg::sculpt
