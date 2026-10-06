// The architect: buildings composed from parts, in a style. `use arch` and a
// style (`use gothic`), then `building style=gothic ...`.
//
// A style is a library that says these words, each its own way - and only
// these, so any style goes in any composition, and a new style is a new file:
//
//   <s>.wall len h t       the wall's body along x, its face at z = 0, back to -t
//   <s>.opening w h d      the cutter of a window: foot at y = 0, through z
//   <s>.window w h         what frames a window, on the face (z >= 0)
//   <s>.doorway w h d      the cutter of a door
//   <s>.door w h t         what frames a door, and its leaf (t: the wall's depth)
//   <s>.band len           a course along the face where a floor is
//   <s>.pier h             what stands between bays (pilaster, buttress, fin)
//   <s>.base len h         the plinth along the foot
//   <s>.cornice len        the crown along the top
//   <s>.roof w d h         over a footprint w x d centred on the origin, foot at y = 0
//   <s>.column h r         a column standing free
//   <s>.tower r h          a tower's body and top, foot at y = 0
//
// and its proportions, as variables: `<s>_ww` a window's width in a bay,
// `<s>_wh` its height in a floor, `<s>_sill` its sill in a floor, `<s>_roof`
// its roof's height in the building's depth, `<s>_head` its arches (`round`, `pointed` or
// `square`: what an opening's head is).
//
// Everything is along x, facing +z, standing on y = 0; turn and place it with
// `at=` and `rot=`. The numbers are parameters: every composition below is
// one line with what differs said.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_arch() {
    return R"LIB(
use mould
use pointed
define facade style=classical w=12 floors=2 fh=3.6 bays=5 t=0.5 door=1 base=0.6 dw=0 dh=0   # a wall of bays and floors: windows, a door (door=2: standing open; dw, dh its size, 0: the bay says), piers, courses, a crown
  let bw $w/$bays
  let ww $bw*$${style}_ww
  let wh $fh*$${style}_wh
  let sill $fh*$${style}_sill
  let dw if($dw>0,$dw,min($bw*0.62,1.8))
  let dh if($dh>0,$dh,min($fh*0.82,max(2.6,$dw*2.2)))
  let mid floor($bays/2)
  $style.wall $w $base+$floors*$fh $t
  sub for f $floors
    for b $bays
      let x -$w/2+$bw*($b+0.5)
      if $door*($f==0)*($b==$mid)
        $style.doorway $dw $dh $t*3 at=$x,$base,0
      else
        $style.opening $ww $wh $t*3 at=$x,$base+$f*$fh+$sill,0
      end
    end
  end
  for f $floors
    for b $bays
      let x -$w/2+$bw*($b+0.5)
      if $door*($f==0)*($b==$mid)
        $style.door $dw $dh $t at=$x,$base,0
        opening $dw $dh head=$${style}_head walk=1 at=$x,$base,0
      else
        $style.window $ww $wh at=$x,$base+$f*$fh+$sill,0
        # and the window asked of whatever wall is behind this one: a room
        # inside opens its wall there, so the two walls agree
        opening $ww $wh head=$${style}_head recess=$t*0.4 at=$x,$base+$f*$fh+$sill,0
      end
    end
    if $f>0
      $style.band $w at=0,$base+$f*$fh,0
    end
  end
  for b $bays+1
    $style.pier $floors*$fh at=-$w/2+$bw*$b,$base,0
  end
  # door=2: the door stands open - its leaf, and anything in the way, cut out again
  if $door>=2
    sub $style.doorway $dw*0.999 $dh*0.999 $t*3 at=-$w/2+$bw*($mid+0.5),$base,0
  end
  $style.base $w $base
  $style.cornice $w at=0,$base+$floors*$fh,0
end
define building style=classical w=12 d=9 floors=2 fh=3.6 bays=5 sbays=4 t=0.5 base=0.6 door=1 inside=0 core=1 dw=0 dh=0   # four facades round a footprint, and its roof; `inside=1`: hollow, its ground floor a room in the style; `core=0`: a shell, nothing inside (a building gone into, whose inside is a room of its own)
  facade style=$style w=$w floors=$floors fh=$fh bays=$bays t=$t base=$base door=$door dw=$dw dh=$dh at=0,0,$d/2
  facade style=$style w=$w floors=$floors fh=$fh bays=$bays t=$t base=$base door=0 at=0,0,-$d/2 rot=180
  facade style=$style w=$d floors=$floors fh=$fh bays=$sbays t=$t base=$base door=0 at=$w/2,0,0 rot=90
  facade style=$style w=$d floors=$floors fh=$fh bays=$sbays t=$t base=$base door=0 at=-$w/2,0,0 rot=-90
  if $inside
    box $w-$t*2 0.05 $d-$t*2 at=0,$base+$fh-0.05,0 mat=plaster
    interior style=$style w=$w-$t*2 d=$d-$t*2 h=$fh-0.35 bays=$bays sbays=$sbays walls=0 holes=0 at=0,$base,0
  else
    if $core
      box $w-$t $base+$floors*$fh-0.05 $d-$t
    end
  end
  $style.roof $w $d min($w,$d)*$${style}_roof at=0,$base+$floors*$fh,0
end
define wing style=classical w=8 d=7 floors=2 fh=3.6 bays=3 t=0.5 base=0.6   # a building with no door, to set beside another
  building style=$style w=$w d=$d floors=$floors fh=$fh bays=$bays sbays=max(2,floor($d/2.6)) t=$t base=$base door=0
end
define tower style=classical r=2.5 h=14   # a tower in the style
  $style.tower $r $h
end
define church style=gothic len=24 w=10 h=11 towers=2 base=0.6 door=1 core=1 dw=0 dh=0   # a nave with its door to +z, towers either side of it, an apse at the far end
  building style=$style w=$w d=$len floors=1 fh=$h bays=3 sbays=max(3,floor($len/3.2)) base=$base door=$door core=$core dw=$dw dh=$dh
  if $towers>=1
    tower style=$style r=$w*0.2 h=$h*2.1 at=-$w/2-$w*0.1,0,$len/2-$w*0.2
  end
  if $towers>=2
    tower style=$style r=$w*0.2 h=$h*2.1 at=$w/2+$w*0.1,0,$len/2-$w*0.2
  end
  group
    cyl $w*0.42 $h*0.85 at=0,0,-$len/2 sides=10
    sub box $w $h*2 $w at=0,-1,-$len/2+$w/2
  end
  cone $w*0.48 $h*0.4 sides=10 at=0,$h*0.85,-$len/2
end
define temple style=classical w=12 d=20 h=6 n=6   # a cella in a peristyle of columns on a stepped base, under a pediment
  let r $w/($n*3.4)
  let sn max(3,round($n*$d/$w))
  for i 3
    box $w+2.4-$i*0.8 0.35 $d+2.4-$i*0.8 at=0,$i*0.35,0 mat=stone
  end
  colonnade style=$style len=$w n=$n h=$h r=$r at=0,1.05,$d/2-$r*1.5
  colonnade style=$style len=$w n=$n h=$h r=$r at=0,1.05,-$d/2+$r*1.5
  colonnade style=$style len=$d-$r*3 n=$sn h=$h r=$r at=$w/2-$r*1.5,1.05,0 rot=90
  colonnade style=$style len=$d-$r*3 n=$sn h=$h r=$r at=-$w/2+$r*1.5,1.05,0 rot=-90
  box $w*0.6 $h $d*0.7 at=0,1.05,0 mat=plaster
  extrude $d+0.9 -$w/2-0.6,0 $w/2+0.6,0 0,$w*0.2 at=0,1.05+$h+$r*2.5,0 mat=rooftiles
end
define street style=classical n=5 w=9 d=10 seed=1   # houses in a row along x, each its own: floors, bays and width by rand
  let x 0
  for i $n
    let hw $w*(0.75+0.5*rand($seed,$i))
    let fl 2+floor(rand($seed,$i,7)*3)
    building style=$style w=$hw d=$d floors=$fl fh=3.3 bays=max(2,floor($hw/2.4)) sbays=3 at=$x+$hw/2,0,0
    let x $x+$hw
  end
end
define courtyard style=classical w=30 d=24 depth=8 floors=2   # four ranges round an open court, the door to +z
  building style=$style w=$w d=$depth floors=$floors bays=max(3,floor($w/3)) at=0,0,$d/2-$depth/2
  wing style=$style w=$w d=$depth floors=$floors bays=max(3,floor($w/3)) at=0,0,-$d/2+$depth/2 rot=180
  wing style=$style w=$d-$depth*2 d=$depth floors=$floors bays=max(2,floor(($d-$depth*2)/3)) at=$w/2-$depth/2,0,0 rot=90
  wing style=$style w=$d-$depth*2 d=$depth floors=$floors bays=max(2,floor(($d-$depth*2)/3)) at=-$w/2+$depth/2,0,0 rot=-90
end
define inner style=classical len=10 h=5 bays=4 wainscot=1   # the inside of one wall, facing +z: a wainscot, a pier between bays, a crown
  if $wainscot
    $style.wainscot $len
  end
  for b $bays+1
    $style.ipier $h at=-$len/2+$len/$bays*$b,0,0
  end
  $style.icornice $len at=0,$h,0
end
define interior style=classical w=10 d=14 h=6 bays=4 sbays=0 walls=1 door=1 aisles=0 floor=1 t=0.3 dw=0 holes=1 arcade=0 ww=0 wh=0 wsill=0   # a room in the style, seen from inside: its floor, walls' insides, ceiling; its own walls or none; the front (+z) wall's inside open at its door, dw wide (0: the bay says). With walls=0 the walls are a room's own, laid by its rule: its windows down both sides (ww, wh, wsill: 0, the style says) and, arcade=1, a blind arcade low along them are asked of those walls as openings
  let sb if($sbays>0,$sbays,max(2,round($bays*$d/$w)))
  let dw if($dw>0,$dw,min($w/$bays*0.6,1.8))
  let open $holes*($walls==0)
  let bay $d/$sb
  let ww if($ww>0,$ww,$bay*$${style}_ww)
  let wh if($wh>0,$wh,$h*$${style}_wh)
  let wsill if($wsill>0,$wsill,$h*$${style}_sill)
  if $open
    # the windows, a bay each, as holes in the walls; their glass is someone else's
    for i $sb
      opening $ww $wh head=$${style}_head recess=1 at=-$w/2,$wsill,-$d/2+$bay*($i+0.5) rot=90
      opening $ww $wh head=$${style}_head recess=1 at=$w/2,$wsill,-$d/2+$bay*($i+0.5) rot=-90
    end
    if $arcade
      # under them, arches let a little way into the wall
      let n max(1,floor($bay/2.4))
      let ah min(2,$wsill-0.4)
      for i $sb*$n
        opening $bay/$n*0.6 $ah head=$${style}_head recess=0.12 at=-$w/2,0.15,-$d/2+$bay/$n*($i+0.5) rot=90
        opening $bay/$n*0.6 $ah head=$${style}_head recess=0.12 at=$w/2,0.15,-$d/2+$bay/$n*($i+0.5) rot=-90
      end
    end
  end
  if $floor
    box $w 0.12 $d at=0,-0.12,0 mat=$${style}_floor
  end
  if $walls
    group
      box $w+$t*2 $h $t at=0,0,-$d/2-$t/2 mat=plaster
      box $w+$t*2 $h $t at=0,0,$d/2+$t/2 mat=plaster
      box $t $h $d at=-$w/2-$t/2,0,0 mat=plaster
      box $t $h $d at=$w/2+$t/2,0,0 mat=plaster
      if $door
        sub $style.doorway $dw min($h*0.6,3) $t*3 at=0,0,$d/2
      end
    end
  end
  inner style=$style len=$w h=$h bays=$bays at=0,0,-$d/2
  if $door
    # the front wall's inside either side of the door, its crown carried on over it
    let side ($w-$dw)/2-0.1
    inner style=$style len=$side h=$h bays=max(1,round($bays*$side/$w)) at=$w/2-$side/2,0,$d/2 rot=180
    inner style=$style len=$side h=$h bays=max(1,round($bays*$side/$w)) at=-$w/2+$side/2,0,$d/2 rot=180
    $style.icornice $dw+0.2 at=0,$h,$d/2 rot=180
  else
    inner style=$style len=$w h=$h bays=$bays at=0,0,$d/2 rot=180
  end
  inner style=$style len=$d h=$h bays=$sb wainscot=1-$open*$arcade at=-$w/2,0,0 rot=90
  inner style=$style len=$d h=$h bays=$sb wainscot=1-$open*$arcade at=$w/2,0,0 rot=-90
  $style.ceiling $w $d $h at=0,$h,0
  if $aisles
    for i $sb-1
      $style.column $h*0.8 min($w,$d)*0.025 at=-$w/4,0,-$d/2+$d/$sb*($i+1)
      $style.column $h*0.8 min($w,$d)*0.025 at=$w/4,0,-$d/2+$d/$sb*($i+1)
    end
  end
end
define inwindow style=classical w=1 h=2 recess=0.2 t=0.5   # a window as it is seen from inside a room whose wall is someone else's: the glass in the opening's own shape, set `recess` back from the wall's face at z = 0 (the room side is +z), a lining round the reveal, a sill board
  $style.opening $w*0.99 $h*0.99 0.03 at=0,0.005,-$recess mat=glass
  box 0.05 $h 0.05 at=0,0,-$recess mat=wood
  box $w 0.05 0.05 at=0,$h*0.55,-$recess mat=wood
  box $w+0.16 0.06 $recess+0.12 at=0,-0.06,-$recess/2+0.06 mat=wood
  box 0.06 $h $recess at=-$w/2-0.03,0,-$recess/2 mat=plaster
  box 0.06 $h $recess at=$w/2+0.03,0,-$recess/2 mat=plaster
end
define cathedral style=gothic len=40 w=12 h=16 aisle=5 ah=8 transept=1 towers=2 base=0.6 door=1 core=1 dw=0 dh=0   # a cathedral: the nave high between lower aisles, flying buttresses over the aisle roofs to the nave's clerestory, a transept across it, a rose over the door between two towers, an apse
  let th $h
  building style=$style w=$w d=$len floors=1 fh=$h bays=3 sbays=max(3,floor($len/4.5)) base=$base door=$door core=$core dw=$dw dh=$dh
  wing style=$style w=$aisle d=$len-4 floors=1 fh=$ah bays=max(3,floor(($len-4)/4.5)) base=$base at=$w/2+$aisle/2,0,-2 rot=90
  wing style=$style w=$aisle d=$len-4 floors=1 fh=$ah bays=max(3,floor(($len-4)/4.5)) base=$base at=-$w/2-$aisle/2,0,-2 rot=-90
  let nb max(3,floor($len/4.5))
  for i $nb+1
    let z $len/2-2-($len-4)/$nb*$i
    if ($z<$len/2-3)*($z>-$len/2+3)
      pointed.flyer $aisle*0.9 $base+$h*0.95 0.3 0.45 at=$w/2+0.2,0,$z rot=0,0,0 mat=stone
      pointed.flyer $aisle*0.9 $base+$h*0.95 0.3 0.45 at=-$w/2-0.2,0,$z rot=0,180,0 mat=stone
      pointed.pinnacle 0.6 $ah*0.3 at=$w/2+$aisle*0.95,$base+$ah+$aisle*0.22,$z mat=stone
      pointed.pinnacle 0.6 $ah*0.3 at=-$w/2-$aisle*0.95,$base+$ah+$aisle*0.22,$z mat=stone
    end
  end
  if $transept
    wing style=$style w=$w*2.6 d=$w*0.9 floors=1 fh=$h bays=max(3,floor($w*2.6/4.5)) base=$base at=0,0,-$len*0.2
  end
  if $towers>=1
    tower style=$style r=$w*0.22 h=$h*2.2 at=-$w/2-$aisle*0.5,0,$len/2-$w*0.22
  end
  if $towers>=2
    tower style=$style r=$w*0.22 h=$h*2.2 at=$w/2+$aisle*0.5,0,$len/2-$w*0.22
  end
  pointed.rose $w*0.28 12 0.2 at=0,$base+$h*0.72,$len/2+0.1 mat=stone
  group
    cyl $w*0.42 $h*0.85 at=0,0,-$len/2 sides=10 mat=stone
    sub box $w $h*2 $w at=0,-1,-$len/2+$w/2
  end
  cone $w*0.48 $h*0.4 sides=10 at=0,$h*0.85,-$len/2 mat=slate
end
define mosque style=islamic w=20 d=20 h=8 minarets=2 court=1 base=0.4 door=1 core=1 dw=0 dh=0   # a mosque: the prayer hall under its dome (the style's roof), an iwan at its door, minarets at its front corners, and a courtyard before it ringed by an arcade of pointed arches
  building style=$style w=$w d=$d floors=1 fh=$h bays=5 sbays=5 base=$base door=$door core=$core dw=$dw dh=$dh
  if $minarets>=1
    tower style=$style r=2.4 h=$h*3.6 at=-$w/2-2.2,0,$d/2-2
  end
  if $minarets>=2
    tower style=$style r=2.4 h=$h*3.6 at=$w/2+2.2,0,$d/2-2
  end
  if $court
    let cw $w
    let cd $w*0.8
    let bay 3.2
    let n max(3,round($cw/$bay))
    let m max(2,round($cd/$bay))
    box $cw+2 0.3 $cd+2 at=0,0,$d/2+$cd/2+1 mat=stone
    for side 2
      let sx if($side==0,-1,1)*($cw/2+0.6)
      for j $m
        let z $d/2+1+$cd/$m*($j+0.5)
        box 0.5 $h*0.45 0.5 at=$sx,0.3,$z-$cd/$m/2 mat=plaster
        pointed.band $cd/$m-0.5 $h*0.45 0.2 0.25 0.5 at=$sx,0.3,$z rot=0,90,0 mat=plaster
      end
      box 0.5 $h*0.45 0.5 at=$sx,0.3,$d/2+1+$cd mat=plaster
      box 0.6 $h*0.08 $cd+0.6 at=$sx,0.3+$h*0.45,$d/2+1+$cd/2 mat=plaster
    end
    for i $n
      let x -$cw/2+$cw/$n*($i+0.5)
      box 0.5 $h*0.45 0.5 at=$x-$cw/$n/2,0.3,$d/2+1+$cd mat=plaster
      pointed.band $cw/$n-0.5 $h*0.45 0.2 0.25 0.5 at=$x,0.3,$d/2+1+$cd mat=plaster
    end
    box $cw+1.2 $h*0.08 0.6 at=0,0.3+$h*0.45,$d/2+1+$cd mat=plaster
    lathe 0,0 1.6,0 1.6,0.3 0.3,0.4 0.3,0.9 1.2,1.0 1.2,1.15 0,1.15 at=0,0.3,$d/2+1+$cd/2 sides=16 mat=stone
  end
end
define palace style=classical w=40 d=14 floors=3 fh=4.2 bays=11 wings=1 base=1.2 door=1 core=1 dw=0 dh=0   # a palace: a corps de logis with a pavilion at either end standing a storey higher under its own roof, wings coming forward to +z round a cour d'honneur (if asked), a terrace with a balustrade and a flight of steps before the door
  let pw $w*0.22
  building style=$style w=$w d=$d floors=$floors fh=$fh bays=$bays base=$base door=$door core=$core dw=$dw dh=$dh
  wing style=$style w=$pw d=$d+2 floors=$floors+1 fh=$fh bays=max(2,floor($pw/3)) base=$base at=-$w/2+$pw/2-0.5,0,0
  wing style=$style w=$pw d=$d+2 floors=$floors+1 fh=$fh bays=max(2,floor($pw/3)) base=$base at=$w/2-$pw/2+0.5,0,0
  if $wings
    let wl $d*1.6
    wing style=$style w=$wl d=$d*0.7 floors=$floors-1 fh=$fh bays=max(3,floor($wl/3.5)) base=$base at=-$w/2+$d*0.35,0,$d/2+$wl/2 rot=90
    wing style=$style w=$wl d=$d*0.7 floors=$floors-1 fh=$fh bays=max(3,floor($wl/3.5)) base=$base at=$w/2-$d*0.35,0,$d/2+$wl/2 rot=-90
  end
  box $w-$pw*2 $base 6 at=0,0,$d/2+3 mat=stone
  mould.balustrade $w-$pw*2 1.0 at=0,$base,$d/2+6 mat=stone
  mould.balustrade 6 1.0 at=-$w/2+$pw,$base,$d/2+3 rot=0,90,0 mat=stone
  mould.balustrade 6 1.0 at=$w/2-$pw,$base,$d/2+3 rot=0,-90,0 mat=stone
  let n ceil($base/0.16)
  for i $n
    box 6+$i*0.7 $base/$n 0.4+$i*0.35 at=0,$base-$base/$n*($i+1),$d/2+6+$i*0.35*0.5+0.2 mat=stone
  end
end
define castle style=romanesque w=40 d=30 h=8 t=1.6 keep=1 base=0   # a castle: curtain walls with their wall-walk and battlements round a ward, round towers at the corners, a gatehouse in the front wall, and a keep in the ward - a building of the style under battlements
  wall $w $h $t at=0,0,$d/2
  sub arch 3.2 4.5 $t*3 at=0,0,$d/2
  wall $w $h $t at=0,0,-$d/2
  wall $d $h $t at=-$w/2,0,0 rot=90
  wall $d $h $t at=$w/2,0,0 rot=90
  radial n=4
    cyl 3 $h*1.4 at=$w/2,0,$d/2 mat=stone
    cyl 3.3 0.3 at=$w/2,$h*1.4-0.3,$d/2 mat=stone
    merlons n=14 r=3.3 w=0.6 h=0.8 d=0.4 at=$w/2,$h*1.4,$d/2 mat=stone
    cone 3.6 3.5 at=$w/2,$h*1.4+0.8,$d/2 sides=16 mat=slate
  end
  gatehouse 8 $h*1.3 $t*3 gw=3.2 gh=4.5 at=0,0,$d/2
  if $keep
    building style=$style w=$w*0.35 d=$d*0.35 floors=3 fh=$h*0.45 bays=4 sbays=3 door=1 base=0.8 at=0,0,-$d*0.15
    battlement len=$w*0.35+0.6 t=0.4 h=0.9 w=0.6 gap=0.42 at=0,0.8+3*$h*0.45,-$d*0.15+$d*0.175+0.1 mat=stone
    battlement len=$w*0.35+0.6 t=0.4 h=0.9 w=0.6 gap=0.42 at=0,0.8+3*$h*0.45,-$d*0.15-$d*0.175-0.1 mat=stone
    battlement len=$d*0.35+0.6 t=0.4 h=0.9 w=0.6 gap=0.42 at=$w*0.175+0.1,0.8+3*$h*0.45,-$d*0.15 rot=90 mat=stone
    battlement len=$d*0.35+0.6 t=0.4 h=0.9 w=0.6 gap=0.42 at=-$w*0.175-0.1,0.8+3*$h*0.45,-$d*0.15 rot=90 mat=stone
  end
end
define colonnade style=classical len=12 n=6 h=5 r=0.35   # columns in a row along x, and what they carry
  let step $len/($n-1)
  array n=$n step=$step,0,0 at=-$len/2,0,0
    $style.column $h $r
  end
  box $len+$r*4 $r*1.6 $r*3 at=0,$h,0
  $style.cornice $len+$r*2 at=0,$h+$r*1.6,$r*1.2
end
)LIB";
}

}  // namespace sg::sculpt
