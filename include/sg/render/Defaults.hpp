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
};
void standard_look(LookState& look,const Quality& quality={});
}
