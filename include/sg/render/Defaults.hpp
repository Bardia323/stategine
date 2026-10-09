// Backend-independent fallback settings for the existing Look passes.
#pragma once
#include "sg/domains/Look.hpp"
namespace sg::render {
struct Quality {
    int shadow_size=2048,msaa=4;
    float bloom_strength=0.55f,bloom_threshold=1.05f,exposure=1.15f;
    int bloom_passes=3;
    bool instancing=true;
    // What things wear sent to the card packed (sg/render/Pack.hpp), where
    // the card takes it: a quarter of the bytes, as warmed before the first frame.
    bool pack=true;
    // How much of the card the views' shadow maps may hold, in megabytes:
    // past it, the maps of the views asked for longest ago are let go (and
    // laid again, whole, the frame they are next asked for) - only ever a
    // copy of what the lamps and casters say, so nothing is lost but the
    // laying. 0: as the card allows (a sixteenth of it, 512 MB to 2 GB; 1 GB
    // where the card does not say); below 0: every map kept, as many as
    // there are.
    int shadow_budget_mb=0;
};
void standard_look(LookState& look,const Quality& quality={});
}
