// Stategine - every crossing seen, frame by frame.
//
// The overlap law (laws::overlaps) holds what the two sides of a seam say of
// it to one; this holds what is *seen* there to one. For every seam of a
// graph, each way it is walked through, an eye at standing height goes
// through the doorway a few centimetres a frame: drawn in the world it is in
// - before the doorway in the one it leaves, past it in the one beyond,
// carried there by the seam's own travel - as a walker would see it. Two
// things must never be seen:
//
//   a jump     the frame the doorway is crossed changing more than the walk's
//              own pace changes a frame: the picture before and after are not
//              one picture
//   a flicker  a part of the picture that changes for one frame and back
//
// It is a check, for tests and tools, run on the graph as declared: it moves
// the worlds' cameras to walk them, and puts them back.
#pragma once

#include <string>
#include <vector>

#include "sg/gl/World.hpp"

namespace sg::render {

struct CrossingOptions {
    int w = 160, h = 90;     // the picture each frame is drawn at
    double before = 1.2;     // how far before the doorway the walk starts, metres
    double after = 0.8;      // and past it, where it ends
    double step = 0.04;      // how far a frame
    double eye = 1.65;       // how high the eye is carried above the doorway's foot
    std::string dump;        // a folder to write each frame to (<way>-<frame>.ppm), to look at
};

// What is wrong with crossing each seam of `g`, seen through `view` (prepared
// on `g`): one line each, naming the seam, the way and the frame. Empty when
// every crossing is one picture.
std::vector<std::string> check_crossings(GLWorldView& view, StateGraph& g, const CrossingOptions& o = {});

}  // namespace sg::render
