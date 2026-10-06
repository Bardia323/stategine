// Trees and plants, grown: each a `grow` of its own L-system (ModelerGrow.cpp)
// - an oak's decurrent crown, a pine's whorls, a willow's drooping shoots,
// a swamp tree's roots. `use trees`.
//
//   tree.oak h seed low          broad and spreading: three-way forks, leaves along every branch
//   tree.pine h seed low         excurrent: one leader, whorls of five shortening up it, drooping
//   tree.birch h seed low        slender: one stem, small branches at the golden angle
//   tree.dead h seed low         bare and gnarled: forks at random, no leaves
//   tree.willow h seed low       a weeping crown: shoots bent down by their own weight
//   tree.swamp h seed low        leaning, twisted, on roots that reach out into the water
//   tree.palm h seed low         a curved trunk and a crown of fronds
//   bush h seed low              stems from the ground, leafy all over
//   plant.grass h seed           a tuft of blades
//   plant.reeds h seed           tall blades, bent
//
// h is its height; seed which of its kind (the same seed, the same tree);
// low=1 its low-poly self (fewer steps, fewer sides, leaves of eight faces,
// twigs pruned) for a wood seen far off or a thousand times. Leaves are
// leaves (blades on stalks, folded, drooping), a pine's needles tufts of them. Materials: the
// wood `bark` (`birch`, `deadwood`), the leaves `leaf` (`needles`, `frond`),
// `grass`, `reed`. Each stands on y = 0 at its foot.
#include "ModelerLibrary.hpp"

namespace sg::sculpt {

const char* lib_trees() {
    return R"LIB(
define tree.oak h=9 seed=1 low=0   # a broad oak: three-way forks, leaves along every branch
  grow FA A:[&(38)F'L'A]/(94)[&(38)F'L'A]/(132)[&(38)F'L'A] n=if($low,5,6) angle=30 shorten=0.82 branch=0.9 height=$h width=$h*0.038 jitter=9 bend=0.08 toward=0,1,0 leaves=3 leafy=if($low,5,7) leaf=$h*if($low,0.055,0.045) seed=$seed sides=if($low,5,8) min=$h*if($low,0.003,0.0006) merge=if($low,12,4) mat=bark leafmat=leaf
end
define tree.pine h=12 seed=1 low=0   # a pine: one leader, whorls of five shortening up it, the boughs drooping
  grow T T:F'[&(70)B]/(72)[&(70)B]/(72)[&(70)B]/(72)[&(70)B]/(72)[&(70)B]/(40)T B:FL'[+(30)C][-(30)C]FL'C C:FLFL n=if($low,7,9) shorten=0.86 branch=1 height=$h width=$h*0.025 jitter=4 bend=0.25 toward=0,-1,0 leaves=4 leafy=if($low,1,2) leaf=$h*0.058 seed=$seed sides=if($low,5,7) min=$h*if($low,0.002,0.0003) merge=if($low,12,4) mat=bark leafmat=needles
end
define tree.birch h=11 seed=1 low=0   # a birch: a slender stem, small branches round it at the golden angle
  grow FA A:F[&(25)'A]/(137)[&(25)'A]/(137)'A n=if($low,6,7) shorten=0.85 branch=0.85 height=$h width=$h*0.016 jitter=6 bend=0.05 toward=0,1,0 leaves=3 leafy=if($low,4,6) leaf=$h*0.022 leafwidth=0.5 seed=$seed sides=if($low,4,6) min=$h*if($low,0.002,0.0003) merge=if($low,12,4) mat=birch leafmat=leaf
end
define tree.dead h=7 seed=1 low=0   # a dead tree: bare, gnarled, forking at random
  grow FA A:F[&(40)'A]/(120)[&(55)'A]/(97)A F:0.7:F F:0.3:F/(20)F n=if($low,5,6) shorten=0.8 branch=0.8 height=$h width=$h*0.043 jitter=18 leaves=0 seed=$seed sides=if($low,4,6) min=$h*if($low,0.004,0.001) merge=if($low,12,4) mat=deadwood
end
define tree.willow h=9 seed=1 low=0   # a weeping willow: shoots bent down by their own weight
  grow FFFA A:F[&(75)'B]/(72)[&(65)'B]/(72)[&(80)'B]/(72)[&(70)'B]/(72)[&(60)'B]/(36)A B:FL[+(20)C]FL[-(20)C]FLB C:FLFLFL n=if($low,5,6) shorten=0.88 branch=1 height=$h width=$h*0.033 jitter=6 bend=0.3 toward=0,-1,0 leaves=3 leafy=if($low,3,4) leaf=$h*0.03 leafwidth=0.18 seed=$seed sides=if($low,5,7) min=$h*if($low,0.002,0.0003) merge=if($low,12,4) mat=bark leafmat=leaf
end
define tree.swamp h=8 seed=1 low=0   # a swamp tree: leaning and twisted, on roots that reach out into the water
  grow [!&(110)R]/(120)[!&(115)R]/(120)[!&(105)R]/(60)[!&(112)R]FA R:F'F A:F[&(45)'A]/(150)[&(30)'A]'A n=if($low,5,6) shorten=0.8 branch=0.85 height=$h width=$h*0.044 jitter=22 bend=0.2 toward=0,-1,0 leaves=3 leafy=if($low,5,7) leaf=$h*0.04 seed=$seed sides=if($low,4,6) min=$h*if($low,0.003,0.0006) merge=if($low,12,4) mat=bark leafmat=leaf
end
define tree.palm h=7 seed=1 low=0   # a palm: a curved trunk, a crown of drooping fronds
  grow FFFFFFFFP P:[&(80)B]/(60)[&(80)B]/(60)[&(80)B]/(60)[&(80)B]/(60)[&(80)B]/(60)[&(80)B] B:F'LF'LF'LF'L n=2 shorten=0.9 branch=0.45 height=$h width=$h*0.023 jitter=3 bend=0.22 toward=0,-1,0 leaves=2 leaf=$h*0.05 seed=$seed sides=if($low,5,7) min=0.0001 mat=bark leafmat=frond
end
define bush h=1.6 seed=1 low=0   # a bush: stems from the ground, leafy all over
  grow [&(55)A]/(120)[&(50)A]/(120)[&(60)A] A:F[&(30)'A]/(137)[-(25)'A] n=if($low,4,5) shorten=0.8 branch=0.85 height=$h width=$h*0.03 jitter=10 bend=0.05 toward=0,1,0 leaves=3 leafy=if($low,5,7) leaf=$h*0.11 seed=$seed sides=4 min=$h*0.002 merge=if($low,12,4) mat=bark leafmat=leaf
end
define plant.grass h=0.4 seed=1   # a tuft of grass: seven blades, bent
  grow [&(12)B]/(73)[&(22)B]/(73)[&(30)B]/(73)[&(18)B]/(73)[&(26)B]/(73)[&(9)B]/(73)[&(34)B] B:F'F'F n=1 shorten=0.8 height=$h width=$h*0.08 sides=3 jitter=8 bend=0.3 toward=0,-1,0 leaves=0 seed=$seed merge=0 mat=grass
end
define plant.reeds h=1.6 seed=1   # reeds: tall blades, a few bent over
  grow [&(4)B]/(73)[&(10)B]/(73)[&(16)B]/(73)[&(7)B]/(73)[&(13)B]/(73)[&(2)B]/(73)[&(19)B]/(73)[&(9)B]/(73)[&(12)B] B:F'F'F'F n=1 shorten=0.9 height=$h width=$h*0.03 sides=3 jitter=5 bend=0.12 toward=0,-1,0 leaves=0 seed=$seed merge=0 mat=reed
end
)LIB";
}

}  // namespace sg::sculpt
