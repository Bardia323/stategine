// Stategine - Embedding: a state running inside an element of another state.
//
// A transition replaces the active state; an embedding nests one. The host
// keeps running while a guest state lives inside one of its elements - the
// portal. Two functors bound the portal:
//
//   in  : host  -> guest   what the guest sees (a collapsed view: a 2D map of
//                          the 3D world, an inventory grid, a terminal screen)
//   out : guest -> host    what edits inside the guest do to the host
//
// `out . in` is the round trip; when it is the identity the portal is a
// lossless view. Sync decides which direction runs when:
//
//   Live    out every frame - an editable mirror (a map that moves the crates)
//   Commit  out on close     - a transactional editor (cancel by not committing)
//   View    in every frame   - a read-only window (a doorway into another room,
//                              where `in` carries the viewer's pose through the
//                              portal and becomes the guest's camera)
#pragma once

#include <string>

#include "sg/core/Core.hpp"

namespace sg {

enum class EmbedSync {
    Live,   // out runs every frame: host and guest stay in lockstep
    Commit, // out runs on close: edits land only when the portal is confirmed
    View    // in runs every frame, out never: a read-only window into the guest
};

struct Embedding {
    Key name;
    Key host;    // state that owns the portal
    Key portal;  // element in the host that displays the guest
    Key guest;   // state running inside it
    Key in;      // functor host -> guest (may be empty)
    Key out;     // functor guest -> host (may be empty)
    EmbedSync sync = EmbedSync::Commit;
    // With focus off the guest still ticks, but input keeps going to the host.
    bool focus = true;
    // Runtime flag, owned by the engine.
    bool open = false;
};

}  // namespace sg
