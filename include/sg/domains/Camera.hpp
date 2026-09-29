// Stategine - a camera: a state of its own that sees a world.
//
// A camera is not a part of the room it looks at. It is a small state - one
// object, its lens - that could exist with no world at all, and meets a world
// only through the graph:
//
// Objects:  lens   where it stands and which way it looks (x, y, z, yaw,
//                  pitch, roll, fov), and the picture it makes (feed_w x
//                  feed_h). The lens is a portal with `feed` = 1 and `eye` = 1:
//                  what is embedded in it is seen *from* it.
// Arrows:   lens --aim-->  lens   stand and look somewhere else  {x, y, z, yaw, pitch, roll, fov}
//           lens --zoom--> lens   a wider or narrower view       {fov}
//           lens --roll--> lens   running, or not                {on}
//
// It sees a world by filming it (sg::film): the world embedded in the lens.
// The renderer draws an open embedding in an `eye` portal from the portal's
// own pose, as a picture; a screen shows that picture by `shows` = the
// embedding's name, as it shows a deck's. So a camera can film the very room
// its screen stands in, and pointed at that screen, see itself seeing: each
// frame's picture holds the one before (the video feedback of a camcorder
// turned on its monitor). No state here knows another: the camera does not
// know the room, the room does not know the camera, the screen knows only
// the name of what it shows.
//
// Where it stands is its own (aimed by its arrow), or a world's: filmed with a
// `rig` - an element of the world - the embedding's `out` functor carries the
// rig's pose onto the lens, so a camera set on a shelf, or held, goes where
// that goes.
#pragma once

#include <algorithm>
#include <string>

#include "sg/core/StateGraph.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg {

class Camera : public State {
public:
    static Key lens_id() { return Key{"lens"}; }
    static Key aim_event() { return Key{"camera.aim"}; }
    static Key zoom_event() { return Key{"camera.zoom"}; }

    explicit Camera(Key id, double fov = 60.0, int w = 640, int h = 480);

    Key kind() const override { return Key{"camera"}; }
    static Key roll_event() { return Key{"camera.roll"}; }

    const Element& lens() const { return element(lens_id()); }
};

// The name a camera's filming of `world` goes by.
inline Key film_name(Key camera, Key world) { return Key{camera.str() + ".film." + world.str()}; }

// `camera` films `world`: the world embedded in the camera's lens, taking no
// input. Nothing runs from the camera into the world: the only functor is the
// rig's, from the world onto the lens (Live, so it runs every frame). With a `rig`,
// the world's element whose pose the camera takes - carried onto the lens by
// the embedding's `out` functor (`<camera>.rig`), every frame the rig or what
// it stands on moves. It is open while the camera runs (the lens's `open`,
// `camera.roll`); a screen shows the picture with `shows` = the returned name.
Key film(StateGraph& g, Key camera, Key world, Key rig = Key{});

// The embedding `film` declares, as data: the world in the camera's lens, Live,
// taking no input, open while the camera runs, its `out` the rig's functor.
Embedding film_embedding(Key camera, Key world, Key rig = Key{});

}  // namespace sg
