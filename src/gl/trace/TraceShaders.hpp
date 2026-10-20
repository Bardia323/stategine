// Stategine - the programs that trace (src/gl/trace): GLSL 3.30, each pass
// a picture drawn over a screen (gl::post_vs), reading the world's tree of
// boxes and its ground from texel buffers and pictures - so a card that
// draws by Vulkan or D3D12 (src/gl/native) traces as GL does.
#pragma once

#include <string>

namespace sg::render::trace {

const std::string& sky_fs();        // the light from all round, by which way a surface faces
const std::string& sun_fs();
const std::string& lamps_fs();        // the sun's light reaching each pixel, through what stands between
const std::string& mirror_fs();     // what a glossy surface mirrors
const std::string& carry_fs();      // a picture kept over frames, where what it shows held still
const std::string& smooth_fs();     // a picture smoothed within its surfaces (a-trous)
const std::string& widen_fs();      // a picture of a part of the pixels laid on all of them
const std::string& heights_vs();    // the ground seen from above: its heights
const std::string& heights_fs();
const std::string& highest_fs();    // the highest of each two by two

}  // namespace sg::render::trace
