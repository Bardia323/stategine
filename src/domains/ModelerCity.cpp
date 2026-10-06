// The modern city: its buildings from the same rules the frame building is
// made by - a structural grid, floor heights by use, a core and the
// circulation round it, a facade system hung on the slabs - and whole
// districts of them, streets, blocks and lots. `use city` (it uses
// `structure` and `mould`). Everything exact.
//
// Kinds are numbers, so a seed can pick them: facade 0 curtain, 1 fins, 2
// ribbon, 3 punched, 4 brick; use 0 apartments, 1 offices; roof 0 flat (or a
// house's gable), 1 pitched (a house's hip).
//
//   skyscraper w d floors [fh facade setbacks core lobby crown spire inside]
//       a tower: a core, floor plates, a curtain wall (or ribbon, punched, fins) hung
//       round them, tiers set back as it rises, a double-height lobby, a crown of
//       plant and parapet, a spire if asked; inside=1 leaves the plates and core to
//       be seen through the glass
//   block w d floors [fh use facade shops balconies roof court]
//       the perimeter building of a city block: apartments or offices, shops along
//       the ground floor, balconies, a flat roof with its plant or a pitched one
//   house w d [floors fh roof porch garage chimney dormers]
//       a detached house: punched windows in walls of brick, a porch over the door,
//       a gabled or hipped roof with dormers and a chimney
//   rowhouse n w d [floors fh]       a terrace: n houses, stoops, bay windows, bracketed cornices, party walls
//   warehouse w d [h docks saw office]  a shed: a portal frame clad in ribbed steel, a sawtooth or
//                                    low gable roof, loading docks, an office at one corner
//   shop w d [h]                     a one-storey shop with its front and sign
//   city.lot kind w d seed           one of them, by kind (0 tower 1 block 2 house 3 rowhouse 4 warehouse 5 shop), varied by seed
//   city.block w d [zone seed]       a block: a sidewalk, lots along its four faces, buildings by zone
//                                    (0 downtown 1 midtown 2 residential 3 industrial)
//   city.street len [w]              a street along x: asphalt, markings, kerbs, lamps, trees
//   city.district nx nz [bw bd street zone seed]
//       a grid of blocks and the streets between them; zone -1 lays downtown at the
//       middle and houses at the edge
//
// Floor heights: offices 3.8, apartments 3.0, shops 4.5, houses 2.9, as the
// uses ask. Everything stands on y = 0 along x and faces +z (its front),
// placed as any shape is.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_city() {
    return R"LIB(
use structure
use mould
define city.facade kind=0 len=20 floors=5 fh=3.6 bays=0   # one wall's skin by number: 0 curtain, 1 fins, 2 ribbon, 3 punched, 4 brick (punched in brick with stone sills)
  let b if($bays>0,$bays,max(2,round($len/3.2)))
  if $kind==0
    structure.curtain $len $floors $fh mull=clamp($len/$b/2,1.2,2.0)
  end
  if $kind==1
    structure.curtain $len $floors $fh mull=clamp($len/$b,1.5,3.5) spandrel=0.6
    let n max(2,round($len/1.2))
    for i $n+1
      box 0.12 $floors*$fh 0.6 at=-$len/2+$len/$n*$i,0,0.3 mat=metal
    end
  end
  if $kind==2
    structure.ribbon $len $floors $fh mat=concrete
  end
  if $kind==3
    structure.punched $len $floors $fh $b mat=plaster
  end
  if $kind==4
    structure.punched $len $floors $fh $b sill=1.0 wh=$fh*0.45 mat=brick
  end
end
define skyscraper w=30 d=30 floors=40 fh=3.8 facade=0 setbacks=2 core=1 lobby=1 crown=1 spire=0 inside=0   # a tower in tiers: each set back from the one below and a sixth smaller; a core up the middle, the plates, the skin round every tier; a double-height lobby in storefront glass under a canopy; a crown of parapet and plant; a spire if asked
  let tiers $setbacks+1
  let per floor($floors/$tiers)
  let lf 2
  for t $tiers
    let k 1-$t*0.17
    let tw $w*$k
    let td $d*$k
    let f0 if($t==0,$lf,0)
    let nf if($t==$tiers-1,$floors-$per*$t,$per)-$f0
    let y0 ($per*$t+$f0)*$fh
    if $inside
      structure.floors $tw $td $nf $fh at=0,$y0-$fh,0 mat=concrete
      structure.grid $tw-2 $td-2 max(2,round($tw/8)) max(2,round($td/8)) $nf*$fh at=0,$y0,0 mat=concrete
    else
      box $tw-0.6 $nf*$fh $td-0.6 at=0,$y0,0 mat=concrete
    end
    city.facade $facade $tw $nf $fh at=0,$y0,$td/2
    city.facade $facade $tw $nf $fh at=0,$y0,-$td/2 rot=0,180,0
    city.facade $facade $td $nf $fh at=$tw/2,$y0,0 rot=0,-90,0
    city.facade $facade $td $nf $fh at=-$tw/2,$y0,0 rot=0,90,0
    structure.parapet $tw 0.9 0.3 at=0,$y0+$nf*$fh,$td/2 mat=concrete
    structure.parapet $tw 0.9 0.3 at=0,$y0+$nf*$fh,-$td/2 rot=0,180,0 mat=concrete
    structure.parapet $td 0.9 0.3 at=$tw/2,$y0+$nf*$fh,0 rot=0,-90,0 mat=concrete
    structure.parapet $td 0.9 0.3 at=-$tw/2,$y0+$nf*$fh,0 rot=0,90,0 mat=concrete
    box $tw $fh*0.08 $td at=0,$y0+$nf*$fh-$fh*0.08,0 mat=concrete
  end
  if $core
    structure.core min(8,$w*0.3) min(6,$d*0.25) $floors*$fh+2 $floors $fh at=0,0,0 mat=concrete
  end
  if $lobby
    let lh $lf*$fh
    box $w-0.4 $lh $d-0.4 at=0,0,0 mat=concrete
    structure.storefront $w-1 $lh sign=0.4 awning=0 at=0,0,$d/2 mat=metal
    structure.storefront $d-1 $lh sign=0.4 awning=0 door=0 at=$w/2,0,0 rot=0,-90,0 mat=metal
    structure.storefront $d-1 $lh sign=0.4 awning=0 door=0 at=-$w/2,0,0 rot=0,90,0 mat=metal
    structure.storefront $w-1 $lh sign=0.4 awning=0 door=0 at=0,0,-$d/2 rot=0,180,0 mat=metal
    structure.canopy min(10,$w*0.4) 4 3.4 at=0,0,$d/2 mat=metal
  end
  let kt 1-($tiers-1)*0.17
  if $crown
    structure.penthouse $w*$kt*0.45 $d*$kt*0.4 $fh*0.9 at=0,$floors*$fh,0 mat=concrete
  end
  if $spire
    cyl $w*$kt*0.04 $floors*$fh*0.12 at=0,$floors*$fh+$fh*0.9,0 sides=12 mat=metal
    cone $w*$kt*0.03 0 $floors*$fh*0.08 at=0,$floors*$fh+$fh*0.9+$floors*$fh*0.12,0 sides=12 mat=metal
  end
end
define block w=40 d=24 floors=6 fh=0 use=0 facade=3 shops=1 balconies=1 roof=0 court=0 seed=1   # the building of a city block: a perimeter of punched, ribbon or curtain wall over a ground floor of shops (if asked), balconies on the front, a flat roof with its plant room and parapet or a hipped roof of tiles; use says the floor height (apartments 3.0, offices 3.8)
  let fh if($fh>0,$fh,if($use==1,3.8,3.0))
  let gh if($shops,4.5,$fh)
  let uf $floors-1
  box $w-0.5 $gh+$uf*$fh $d-0.5 at=0,0,0 mat=plaster
  if $shops
    let n max(1,floor($w/9))
    for i $n
      structure.storefront $w/$n-0.4 $gh sign=0.7 at=-$w/2+$w/$n*($i+0.5),0,$d/2 door=mod($i,2)
    end
    structure.punched $d $gh+0.001 $gh max(2,round($d/4)) sill=1.0 wh=2.0 at=$w/2,0,0 rot=0,-90,0 mat=plaster
    structure.punched $d $gh+0.001 $gh max(2,round($d/4)) sill=1.0 wh=2.0 at=-$w/2,0,0 rot=0,90,0 mat=plaster
    structure.punched $w $gh+0.001 $gh max(2,round($w/4)) sill=1.0 wh=2.0 at=0,0,-$d/2 rot=0,180,0 mat=plaster
    mould.fillet $w+0.1 0.3 0.2 at=0,$gh-0.3,$d/2 mat=concrete
  else
    structure.punched $w $gh+0.001 $gh max(2,round($w/4)) door=1 at=0,0,$d/2 mat=plaster
    structure.punched $d $gh+0.001 $gh max(2,round($d/4)) at=$w/2,0,0 rot=0,-90,0 mat=plaster
    structure.punched $d $gh+0.001 $gh max(2,round($d/4)) at=-$w/2,0,0 rot=0,90,0 mat=plaster
    structure.punched $w $gh+0.001 $gh max(2,round($w/4)) at=0,0,-$d/2 rot=0,180,0 mat=plaster
  end
  city.facade $facade $w $uf $fh at=0,$gh,$d/2
  city.facade $facade $d $uf $fh at=$w/2,$gh,0 rot=0,-90,0
  city.facade $facade $d $uf $fh at=-$w/2,$gh,0 rot=0,90,0
  city.facade 3 $w $uf $fh at=0,$gh,-$d/2 rot=0,180,0
  if $balconies
    let nb max(1,floor($w/8))
    for i $uf
      for j $nb
        if mod($i+$j+$seed,2)
          structure.balcony min(3.2,$w/$nb*0.5) 1.4 at=-$w/2+$w/$nb*($j+0.5),$gh+$i*$fh,$d/2 mat=concrete
        end
      end
    end
  end
  let top $gh+$uf*$fh
  if $roof==0
    structure.parapet $w 0.8 0.3 at=0,$top,$d/2 mat=plaster
    structure.parapet $w 0.8 0.3 at=0,$top,-$d/2 rot=0,180,0 mat=plaster
    structure.parapet $d 0.8 0.3 at=$w/2,$top,0 rot=0,-90,0 mat=plaster
    structure.parapet $d 0.8 0.3 at=-$w/2,$top,0 rot=0,90,0 mat=plaster
    structure.penthouse min(6,$w*0.2) min(4,$d*0.2) 2.6 at=$w*0.2,$top,-$d*0.15 mat=concrete
  else
    let r max(0.05,abs($w-$d))
    mould.cornice $w+0.3 0.5 0.4 dentils=0 at=0,$top-0.5,$d/2 mat=plaster
    mould.cornice $w+0.3 0.5 0.4 dentils=0 at=0,$top-0.5,-$d/2 rot=0,180,0 mat=plaster
    mould.cornice $d+0.3 0.5 0.4 dentils=0 at=$w/2,$top-0.5,0 rot=0,-90,0 mat=plaster
    mould.cornice $d+0.3 0.5 0.4 dentils=0 at=-$w/2,$top-0.5,0 rot=0,90,0 mat=plaster
    if $w>=$d
      loft min($w,$d)*0.3 -$w/2-0.6,-$d/2-0.6 $w/2+0.6,-$d/2-0.6 $w/2+0.6,$d/2+0.6 -$w/2-0.6,$d/2+0.6 / -$r/2,-0.02 $r/2,-0.02 $r/2,0.02 -$r/2,0.02 at=0,$top,0 mat=rooftiles
    else
      loft min($w,$d)*0.3 -$w/2-0.6,-$d/2-0.6 $w/2+0.6,-$d/2-0.6 $w/2+0.6,$d/2+0.6 -$w/2-0.6,$d/2+0.6 / -0.02,-$r/2 0.02,-$r/2 0.02,$r/2 -0.02,$r/2 at=0,$top,0 mat=rooftiles
    end
    let nd max(1,floor($w/6))
    for i $nd
      city.dormer 1.4 1.6 at=-$w/2+$w/$nd*($i+0.5),$top+0.8,$d/2-1.2
    end
  end
end
define city.dormer w=1.4 h=1.6   # a gabled dormer standing on a roof slope, its window facing +z
  box $w $h*0.75 1.2 at=0,0,-0.4 mat=plaster
  extrude 1.4 -$w/2-0.1,0 $w/2+0.1,0 0,$h*0.45 at=0,$h*0.75,-0.4 mat=rooftiles
  box $w*0.6 $h*0.5 0.02 at=0,$h*0.12,0.21 mat=glass
  box 0.04 $h*0.5 0.04 at=0,$h*0.12,0.22 mat=wood
  box $w*0.6+0.08 0.04 0.04 at=0,$h*0.12,0.22 mat=wood
end
define house w=10 d=9 floors=2 fh=2.9 roof=0 porch=1 garage=0 chimney=1 dormers=1 seed=1   # a detached house: walls of brick with punched windows, a door under a porch on posts and three steps, a gabled or hipped roof of tiles over eaves, dormers on the front slope, a chimney; a garage to one side if asked
  let bays max(2,round($w/3.2))
  box $w-0.3 $floors*$fh $d-0.3 at=0,0,0 mat=brick
  structure.punched $w $floors $fh $bays sill=0.9 wh=1.4 door=1 at=0,0,$d/2 mat=brick
  structure.punched $d $floors $fh max(2,round($d/3.5)) sill=0.9 wh=1.4 at=$w/2,0,0 rot=0,-90,0 mat=brick
  structure.punched $d $floors $fh max(2,round($d/3.5)) sill=0.9 wh=1.4 at=-$w/2,0,0 rot=0,90,0 mat=brick
  structure.punched $w $floors $fh $bays sill=0.9 wh=1.4 at=0,0,-$d/2 rot=0,180,0 mat=brick
  if $porch
    structure.canopy 2.4 1.6 2.5 at=0,0,$d/2 mat=wood
    extrude 1.4 -1.3,0 1.3,0 0,0.5 at=0,2.5,$d/2+0.8 mat=rooftiles
    for i 3
      box 2.2 0.15 0.3 at=0,-0.45+$i*0.15,$d/2+0.15+$i*0.3+0.0 mat=stone
    end
  end
  let top $floors*$fh
  let rh min($w,$d)*0.42
  if $roof==0
    extrude $w+0.8 -$d/2-0.4,0 $d/2+0.4,0 0,$rh rot=0,90,0 at=0,$top,0 mat=rooftiles
    box $w+0.8 0.2 $d+0.8 at=0,$top-0.2,0 mat=wood
    extrude 0.3 -$d/2+0.15,0 $d/2-0.15,0 0,$rh*0.96 rot=0,90,0 at=-$w/2+0.15,$top,0 mat=brick
    extrude 0.3 -$d/2+0.15,0 $d/2-0.15,0 0,$rh*0.96 rot=0,90,0 at=$w/2-0.15,$top,0 mat=brick
  else
    let r max(0.05,abs($w-$d))
    if $w>=$d
      loft $rh -$w/2-0.4,-$d/2-0.4 $w/2+0.4,-$d/2-0.4 $w/2+0.4,$d/2+0.4 -$w/2-0.4,$d/2+0.4 / -$r/2,-0.02 $r/2,-0.02 $r/2,0.02 -$r/2,0.02 at=0,$top,0 mat=rooftiles
    else
      loft $rh -$w/2-0.4,-$d/2-0.4 $w/2+0.4,-$d/2-0.4 $w/2+0.4,$d/2+0.4 -$w/2-0.4,$d/2+0.4 / -0.02,-$r/2 0.02,-$r/2 0.02,$r/2 -0.02,$r/2 at=0,$top,0 mat=rooftiles
    end
  end
  if $dormers
    let nd max(1,floor($w/4))
    for i $nd
      city.dormer 1.2 1.4 at=-$w/2+$w/$nd*($i+0.5),$top+0.9,$d/2-1.0
    end
  end
  if $chimney
    box 0.7 $rh+1.0 0.5 at=$w*0.3,$top,-$d*0.2 mat=brick
    box 0.9 0.12 0.7 at=$w*0.3,$top+$rh+1.0,-$d*0.2 mat=stone
    cyl 0.12 0.3 at=$w*0.3,$top+$rh+1.1,-$d*0.2 sides=10 mat=stone
  end
  if $garage
    box 5 2.8 6 at=$w/2+2.5,0,$d/2-3 mat=brick
    box 3.6 2.2 0.1 at=$w/2+2.5,0,$d/2-0.05 mat=metal
    for i 5
      box 3.6 0.04 0.03 at=$w/2+2.5,0.4*$i+0.2,$d/2 mat=metal
    end
    extrude 6.6 -3,0 3,0 0,1.2 at=$w/2+2.5,2.8,$d/2-3 mat=rooftiles
  end
end
define rowhouse n=4 w=6 d=12 floors=3 fh=3.0 seed=1   # a terrace along x: n houses of w, each with a stoop up to its door at one side, a bay window beside it, punched windows above, a bracketed cornice; the party walls stand a little proud; the first floor is up the stoop
  let stoop 1.2
  for i $n
    let x -$n*$w/2+$w*($i+0.5)
    let m mod($i+$seed,3)
    group at=$x,0,0
      box $w-0.1 $stoop+$floors*$fh $d-0.3 at=0,0,0 mat=brick
      structure.punched $w*0.96 $floors $fh 2 sill=0.9 wh=1.6 at=0,$stoop,$d/2 mat=brick
      box 1.0 2.3 0.06 at=-$w*0.25,$stoop,$d/2-0.03 mat=wood
      mould.frame 1.0 2.3 0.12 0.08 at=-$w*0.25,$stoop,$d/2 mat=stone
      for s 6
        box 1.3 $stoop/6 0.3 at=-$w*0.25,$stoop-$stoop/6*($s+1),$d/2+0.15+$s*0.3 mat=stone
      end
      box 0.08 0.9 1.8 at=-$w*0.25-0.6,$stoop-0.2,$d/2+0.9 mat=metal
      box 0.08 0.9 1.8 at=-$w*0.25+0.6,$stoop-0.2,$d/2+0.9 mat=metal
      if $m<2
        box $w*0.4 $fh*0.9 0.8 at=$w*0.22,$stoop+0.05,$d/2+0.4 mat=brick
        box $w*0.3 $fh*0.55 0.02 at=$w*0.22,$stoop+0.9,$d/2+0.8 mat=glass
        box 0.05 $fh*0.55 0.04 at=$w*0.22,$stoop+0.9,$d/2+0.81 mat=wood
        box $w*0.42 0.1 0.9 at=$w*0.22,$stoop+$fh*0.95,$d/2+0.4 mat=stone
      end
      mould.cornice $w+0.1 0.6 0.5 modillions=1 at=0,$stoop+$floors*$fh-0.6,$d/2 mat=stone
      structure.parapet $w 0.5 0.3 at=0,$stoop+$floors*$fh,$d/2 mat=brick
      box 0.12 $stoop+$floors*$fh+0.6 $d at=-$w/2+0.06,0,0.1 mat=brick
      structure.punched $w*0.96 $floors $fh 2 sill=0.9 wh=1.5 at=0,$stoop,-$d/2 rot=0,180,0 mat=brick
      structure.punched $d*0.96 1 $stoop 2 sill=0.3 wh=0.6 at=0,0,$d/2 mat=brick
    end
  end
  box 0.12 $stoop+$floors*$fh+0.6 $d at=$n*$w/2-0.06,0,0.1 mat=brick
end
define warehouse w=24 d=40 h=7 docks=3 saw=0 office=1   # a shed: a portal frame clad in ribbed steel, a sawtooth or low gable roof, loading docks along the front, a roller door, a low office block at one corner with its own windows
  structure.portal $w $h $d max(3,round($d/6)) mat=metal
  box $w-0.4 $h $d-0.4 at=0,0,0 mat=metal
  structure.cladding $w $h at=0,0,$d/2 mat=metal
  structure.cladding $w $h at=0,0,-$d/2 rot=0,180,0 mat=metal
  structure.cladding $d $h at=$w/2,0,0 rot=0,-90,0 mat=metal
  structure.cladding $d $h at=-$w/2,0,0 rot=0,90,0 mat=metal
  box $w+0.3 0.6 $d+0.3 at=0,-0.3,0 mat=concrete
  if $saw
    structure.sawtooth $w+0.4 $d+0.4 max(2,round($w/6)) $w/max(2,round($w/6))*0.45 at=0,$h,0
  else
    extrude $d+0.6 -$w/2-0.3,0 $w/2+0.3,0 0,$w*0.12 at=0,$h,0 mat=metal
    for i 3
      cyl 0.3 0.6 at=-$w/4+$i*$w/4,$h+$w*0.12*(1-abs(-$w/4+$i*$w/4)/($w/2)),-$d*0.2 sides=12 mat=metal
    end
  end
  for i $docks
    structure.dock 3.2 4 at=-$w/2+$w*0.15+$i*4.2,0,$d/2 mat=concrete
  end
  if $office
    box 8 3.4 6 at=$w/2-4,0,$d/2+3 mat=plaster
    structure.punched 8 1 3.4 3 sill=0.9 wh=1.6 door=1 at=$w/2-4,0,$d/2+6 mat=plaster
    structure.punched 6 1 3.4 2 sill=0.9 wh=1.6 at=$w/2,0,$d/2+3 rot=0,-90,0 mat=plaster
    structure.parapet 8 0.4 0.2 at=$w/2-4,3.4,$d/2+6 mat=plaster
  end
end
define shop w=12 d=10 h=4.5   # a one-storey shop: a storefront under its sign, cladding round the sides, a flat roof with a parapet and a unit on it
  box $w-0.4 $h $d-0.4 at=0,0,0 mat=plaster
  structure.storefront $w $h at=0,0,$d/2
  structure.cladding $d $h at=$w/2,0,0 rot=0,-90,0 mat=plaster
  structure.cladding $d $h at=-$w/2,0,0 rot=0,90,0 mat=plaster
  structure.cladding $w $h at=0,0,-$d/2 rot=0,180,0 mat=plaster
  structure.parapet $w 0.6 0.25 at=0,$h,$d/2 mat=plaster
  box 1.6 1.0 1.6 at=$w*0.2,$h,-$d*0.2 mat=metal
end
define city.lot kind=1 w=20 d=20 seed=1   # one building on a lot w across and d deep, its front to +z at z = d/2: by kind, with what differs from the seed
  let r1 rand($seed,1)
  let r2 rand($seed,2)
  let r3 rand($seed,3)
  if $kind==0
    let fl 12+floor($r1*30)
    let fac if($r2<0.4,0,if($r2<0.7,1,2))
    skyscraper min($w,$d)*0.85 min($w,$d)*0.85 $fl facade=$fac setbacks=floor($r3*3) spire=if($r1>0.7,1,0) at=0,0,$d/2-min($w,$d)*0.425
  end
  if $kind==1
    let fl 3+floor($r1*5)
    let fac if($r2<0.4,3,if($r2<0.6,2,if($r2<0.8,4,0)))
    block $w-1 $d-1 $fl use=if($r3<0.5,0,1) facade=$fac shops=if($r3<0.7,1,0) roof=if($r1<0.3,1,0) seed=$seed at=0,0,0
  end
  if $kind==2
    house min($w-4,11) min($d-6,10) floors=1+floor($r1*2) roof=if($r2<0.5,0,1) garage=if($r3<0.4,1,0) seed=$seed at=0,0,$d/2-min($d-6,10)/2-3
  end
  if $kind==3
    let n max(2,floor($w/6))
    rowhouse $n $w/$n min($d-4,13) floors=2+floor($r1*2) seed=$seed at=0,0,$d/2-min($d-4,13)/2-1
  end
  if $kind==4
    warehouse $w-2 $d-8 h=6+$r1*3 docks=max(1,floor(($w-2)/7)) saw=if($r2<0.5,1,0) at=0,0,$d/2-($d-8)/2-6
  end
  if $kind==5
    shop $w-1 min($d-3,12) h=4+$r1 at=0,0,$d/2-min($d-3,12)/2-1
  end
end
define city.block w=80 d=60 zone=1 seed=1 side=3   # a city block: a sidewalk round it, lots along its faces, a building on each of the zone's kinds - downtown (0) towers and offices, midtown (1) blocks with shops, residential (2) houses and terraces, industrial (3) sheds and shops; `side` metres of sidewalk
  box $w+$side*2 0.15 $d+$side*2 at=0,0,0 mat=concrete
  let depth min($d/2,if($zone==0,$w/2,if($zone==2,24,28)))
  let lotw if($zone==0,30,if($zone==2,16,if($zone==3,34,22)))
  let n max(1,round($w/$lotw))
  let lw $w/$n
  for i $n
    let s $seed*100+$i
    let kind if($zone==0,if(rand($s,9)<0.6,0,1),if($zone==1,if(rand($s,9)<0.85,1,5),if($zone==2,if(rand($s,9)<0.55,2,3),if(rand($s,9)<0.7,4,5))))
    city.lot $kind $lw $depth $s at=-$w/2+$lw*($i+0.5),0.15,$d/2-$depth/2
    city.lot $kind $lw $depth $s+50 at=-$w/2+$lw*($i+0.5),0.15,-$d/2+$depth/2 rot=0,180,0
  end
  if $zone!=0
    let m max(1,round(($d-$depth*2)/$lotw))
    if $d-$depth*2>12
      let sw ($d-$depth*2)/$m
      for j $m
        let s $seed*100+40+$j
        let kind if($zone==1,1,if($zone==2,2,5))
        city.lot $kind $sw min($depth,$w/2) $s at=$w/2-min($depth,$w/2)/2,0.15,-$d/2+$depth+$sw*($j+0.5) rot=0,-90,0
        city.lot $kind $sw min($depth,$w/2) $s+50 at=-$w/2+min($depth,$w/2)/2,0.15,-$d/2+$depth+$sw*($j+0.5) rot=0,90,0
      end
    end
  end
end
define city.street len=80 w=16 lamps=1 trees=1   # a street along x, its middle at the origin: asphalt between kerbs, a dashed centre line, lamps and trees along both sides
  box $len 0.12 $w at=0,-0.02,0 mat=asphalt
  box $len 0.15 0.3 at=0,0,$w/2-0.15 mat=concrete
  box $len 0.15 0.3 at=0,0,-$w/2+0.15 mat=concrete
  let n max(1,floor($len/4))
  array n=$n step=4,0,0 at=-($n-1)*2,0.1,0
    box 2 0.01 0.14 mat=paint
  end
  if $lamps
    let nl max(1,floor($len/25))
    for i $nl
      let x -$len/2+25*($i+0.5)
      cyl 0.08 6 at=$x,0,$w/2-0.8 sides=8 mat=metal
      box 0.4 0.15 1.4 at=$x,6,$w/2-1.5 mat=metal
      cyl 0.08 6 at=$x,0,-$w/2+0.8 sides=8 mat=metal
      box 0.4 0.15 1.4 at=$x,6,-$w/2+1.5 mat=metal
    end
  end
  if $trees
    let nt max(1,floor($len/12))
    for i $nt
      let x -$len/2+12*($i+0.5)+6*rand($i,$len)
      cyl 0.14 2.6 at=$x,0,$w/2-2 sides=6 mat=bark
      sphere 1.6 at=$x,3.8,$w/2-2 sides=7 mat=leaves
      cyl 0.14 2.6 at=$x,0,-$w/2+2 sides=6 mat=bark
      sphere 1.6 at=$x,3.8,-$w/2+2 sides=7 mat=leaves
    end
  end
end
define city.district nx=3 nz=3 bw=80 bd=60 street=16 zone=-1 seed=1   # a grid of nx by nz blocks with streets between: zone -1 puts downtown at the middle, midtown round it, houses at the edge; else every block the zone
  let px $bw+$street
  let pz $bd+$street
  for i $nx
    for j $nz
      let cx -($nx-1)*$px/2+$i*$px
      let cz -($nz-1)*$pz/2+$j*$pz
      let dist max(abs($i-($nx-1)/2)/max(1,($nx-1)/2),abs($j-($nz-1)/2)/max(1,($nz-1)/2))
      let z if($zone>=0,$zone,if($dist<0.34,0,if($dist<0.67,1,if(rand($seed,$i,$j)<0.25,3,2))))
      city.block $bw $bd $z $seed*31+$i*7+$j at=$cx,0,$cz side=($street-10)/2
    end
  end
  for j $nz+1
    city.street $nx*$px $street at=0,0,-($nz)*$pz/2+$j*$pz lamps=1
  end
  for i $nx+1
    city.street $nz*$pz $street at=-($nx)*$px/2+$i*$px,0,0 rot=0,90,0 lamps=0 trees=0
  end
end
)LIB";
}

}  // namespace sg::sculpt
