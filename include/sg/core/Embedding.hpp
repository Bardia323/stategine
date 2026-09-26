// Stategine - Embedding: a state running inside an element of another state.
//
// A transition replaces the active state; an embedding nests one. The host
// keeps running while a guest state lives inside one of its elements - the
// portal. Two functors bound the portal:
//
//   in  : subject -> guest   what the guest sees (a collapsed view: a 2D map of
//                          the 3D world, an inventory grid, a terminal screen)
//   out : guest -> subject  what edits inside the guest do to that state
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

// When a direction that runs "every frame" (Live's out, View's in) actually
// runs. What the guest and the subject hold is the same under every choice
// but the last two, which say so on purpose:
//
//   OnChange    only for the objects whose source or target changed since it
//               last ran (by their stamps) - with nothing changed it costs a
//               comparison. The same result as running every frame, for
//               transports that are functions of the two elements' params.
//   Continuous  every frame, every object: for a transport that reads
//               anything else - the time, another element, the outside world.
//   OnEvent     only in a frame in which an event crossed into the guest
//               through this embedding (or it was synced by hand).
//   Manual      only when `Engine::sync_embed` says so.
enum class Propagation { OnChange, Continuous, OnEvent, Manual };

struct Embedding {
    Key name;
    Key host;    // state that owns the portal
    Key portal;  // element in the host that displays the guest
    Key guest;   // state running inside it
    // The state the guest is a view *of*. Usually the host - a map on the wall
    // of the room it describes - but not always: a panel can hang in one room
    // and act on another, and saying so is what keeps "where it is displayed"
    // and "what it edits" from being quietly conflated. Empty means the host.
    Key subject;
    Key in;      // functor host -> guest (may be empty)
    Key out;     // functor guest -> host (may be empty)
    EmbedSync sync = EmbedSync::Commit;
    Propagation propagate = Propagation::OnChange;
    // With focus off the guest still ticks, but input keeps going to the host.
    bool focus = true;
    // Runtime flag, owned by the engine.
    bool open = false;
};

}  // namespace sg
