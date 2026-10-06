// The language's words, and whole lands to start from: each a recipe as one
// would write it, to be changed.
#include <map>

#include "sg/domains/Terrain.hpp"

namespace sg::terrain {

namespace {

const std::map<std::string, std::string>& lands() {
    static const std::map<std::string, std::string> l{
        {"hills", R"(# Rolling hills, a wood on the gentler slopes.
size 400 400 cell=1
noise 26 220 oct=5
noise 4 40 oct=3
erode drops=120000
layer grass grass
layer earth earth wet=0.55,1 noise=0.5 patch=12
layer rock rock slope=30,90 noise=0.3
thing oak variants=4 : use trees / tree.oak h=9 seed=$v low=1
thing bush variants=3 : use trees / bush h=1.4 seed=$v low=1
scatter oak density=0.25 slope=0,22 spacing=6 scale=0.8,1.3
scatter bush density=0.6 slope=0,30 scale=0.7,1.3
)"},
        {"mountains", R"(# Mountains: ridged peaks, worn by rain, snow above the treeline.
size 1200 1200 cell=3
noise 260 900 oct=6 ridged=1
noise 30 120 oct=3
erode drops=250000 strength=1.2
thermal 15 talus=38
layer grass grass
layer rock rock slope=32,90 noise=0.4 patch=25
layer scree gravel height=150,190 noise=0.5 patch=30
layer snow snow height=190,999 slope=0,40 noise=0.6 patch=40
thing pine variants=4 : use trees / tree.pine h=13 seed=$v low=1
scatter pine density=0.15 height=-50,150 slope=0,30 spacing=8 scale=0.7,1.3
)"},
        {"lake", R"(# A lake in a bowl of hills, reeds at its edge, a wood round it.
size 360 360 cell=1
noise 18 180 oct=5
hill 0 0 140 -14
erode drops=80000
lake 0 0
layer grass grass
layer earth earth wet=0.6,1 noise=0.5 patch=10
layer sand sand water=0,3
layer rock rock slope=32,90
thing birch variants=4 : use trees / tree.birch h=11 seed=$v low=1
thing reeds variants=3 : use trees / plant.reeds h=1.7 seed=$v
scatter birch density=0.2 water=8, slope=0,25 spacing=5 scale=0.8,1.2
scatter reeds density=6 water=-1.5,1 scale=0.8,1.2 sink=0.3
)"},
        {"swamp", R"(# A swamp: low wet ground, standing pools, twisted trees, dead ones.
size 300 300 cell=1
noise 6 120 oct=5
swamp 0 0 130 level=0 pools=0.6
layer grass grass colour=405030
layer mud mud wet=0.7,1 noise=0.6 patch=8
layer moss grass colour=2f3a1c wet=0.45,0.8 noise=0.5 patch=14
thing swamptree variants=4 : use trees / tree.swamp h=9 seed=$v low=1
thing dead variants=3 : use trees / tree.dead h=7 seed=$v low=1
thing reeds variants=3 : use trees / plant.reeds h=1.5 seed=$v
scatter swamptree density=0.35 water=-0.6,6 spacing=5 scale=0.7,1.3 sink=0.4
scatter dead density=0.08 water=-0.3, scale=0.8,1.2
scatter reeds density=5 water=-1,1.5 scale=0.8,1.2 sink=0.2
)"},
        {"valley road", R"(# A road winding up a valley between wooded slopes.
size 600 600 cell=1.5
noise 40 300 oct=5
ridge -30 160 0,-300 0,300
noise 5 50 oct=3
erode drops=150000
road 7 -40,-300 -10,-150 30,-40 0,80 -30,200 10,300 bank=6 name=road
layer grass grass
layer earth earth wet=0.6,1 noise=0.5 patch=12
layer rock rock slope=30,90 noise=0.3
layer verge gravel road=-1,1.5 noise=0.3 patch=3
thing pine variants=4 : use trees / tree.pine h=12 seed=$v low=1
thing dead variants=2 : use trees / tree.dead h=7 seed=$v low=1
scatter pine density=0.3 road=6, slope=0,30 spacing=5 scale=0.8,1.3
scatter dead density=0.03 road=3,40 scale=0.9,1.2
)"},
        {"island", R"(# An island in the sea: a beach all round, a wooded hill.
size 700 700 cell=2
base -12
hill 0 0 260 30
noise 20 160 oct=5
erode drops=100000
sea 0
layer grass grass
layer sand sand height=-99,2.5 noise=0.3 patch=8
layer rock rock slope=30,90
thing palm variants=4 : use trees / tree.palm h=8 seed=$v low=1
scatter palm density=0.2 height=1.5,10 slope=0,25 spacing=5 scale=0.8,1.2
)"},
        {"moor", R"(# A moor: wide, low, bare - a few dead trees, a path across it.
size 800 800 cell=2
noise 12 400 oct=5
noise 2 30 oct=3
path 2.5 -400,-120 -150,-60 60,-90 250,40 400,10
layer grass grass colour=4a4a32
layer heath grass colour=3a2f2a cover=0.45 noise=0.3 patch=40 wet=0,0.5
layer peat mud wet=0.6,1 noise=0.5 patch=10
layer path earth road=-5,0.4 noise=0.2 patch=2
thing dead variants=4 : use trees / tree.dead h=6 seed=$v low=1
thing grass variants=3 : use trees / plant.grass h=0.5 seed=$v
scatter dead density=0.01 spacing=30 scale=0.8,1.3
scatter grass density=2 road=1, scale=0.8,1.6 sink=0.05
)"},
    };
    return l;
}

}  // namespace

std::string words() {
    return "size w d [cell=m at=x,z]   the extent, a height every cell metres\n"
           "seed n   beyond h [over=m]   base h\n"
           "noise amp scale [oct gain lac ridged billow warp seed]\n"
           "hill x z r h [shape=dome|cone|mesa]   ridge h w x,z ...   flatten x z r [h soft]\n"
           "plateau h [k]   terrace step [sharp]   curve p   tilt dx dz   clamp lo hi   scale k   smooth n\n"
           "erode [drops strength seed]   thermal n [talus]\n"
           "road w x,z ... [bank sink surface colour lines name]   path w x,z ...   river w depth x,z ... [name]\n"
           "lake x z [level name colour deep]   sea level   swamp x z r [level pools]\n"
           "layer name surface [colour height slope wet road water noise scale soft]\n"
           "thing name [variants=n] : <modeller recipe, lines split by / ; $v the variant>\n"
           "scatter name [density|count height slope wet road water scale spacing sink seed most]\n"
           "let name value   $name   # comment\n";
}

std::vector<std::string> presets() {
    std::vector<std::string> out;
    for (const auto& [k, v] : lands()) out.push_back(k);
    return out;
}

std::string preset(const std::string& name) {
    const auto it = lands().find(name);
    return it == lands().end() ? std::string() : it->second;
}

}  // namespace sg::terrain
