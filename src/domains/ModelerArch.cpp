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
// its roof's height in the building's depth.
//
// Everything is along x, facing +z, standing on y = 0; turn and place it with
// `at=` and `rot=`. The numbers are parameters: every composition below is
// one line with what differs said.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_arch() {
    return R"LIB(
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
      else
        $style.window $ww $wh at=$x,$base+$f*$fh+$sill,0
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
    interior style=$style w=$w-$t*2 d=$d-$t*2 h=$fh-0.35 bays=$bays sbays=$sbays walls=0 at=0,$base,0
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
define inner style=classical len=10 h=5 bays=4   # the inside of one wall, facing +z: a wainscot, a pier between bays, a crown
  $style.wainscot $len
  for b $bays+1
    $style.ipier $h at=-$len/2+$len/$bays*$b,0,0
  end
  $style.icornice $len at=0,$h,0
end
define interior style=classical w=10 d=14 h=6 bays=4 sbays=0 walls=1 door=1 aisles=0 floor=1 t=0.3   # a room in the style, seen from inside: its floor, walls' insides, ceiling; its own walls or none
  let sb if($sbays>0,$sbays,max(2,round($bays*$d/$w)))
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
        sub $style.doorway min($w/$bays*0.6,1.8) min($h*0.6,3) $t*3 at=0,0,$d/2
      end
    end
  end
  inner style=$style len=$w h=$h bays=$bays at=0,0,-$d/2
  inner style=$style len=$w h=$h bays=$bays at=0,0,$d/2 rot=180
  inner style=$style len=$d h=$h bays=$sb at=-$w/2,0,0 rot=90
  inner style=$style len=$d h=$h bays=$sb at=$w/2,0,0 rot=-90
  $style.ceiling $w $d $h at=0,$h,0
  if $aisles
    for i $sb-1
      $style.column $h*0.8 min($w,$d)*0.025 at=-$w/4,0,-$d/2+$d/$sb*($i+1)
      $style.column $h*0.8 min($w,$d)*0.025 at=$w/4,0,-$d/2+$d/$sb*($i+1)
    end
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
