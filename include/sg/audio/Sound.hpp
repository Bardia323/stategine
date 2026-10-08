// Stategine - sound, as declared state.
//
// What a world sounds like is said by its states, as how it looks is: a thing
// that makes a sound says so in its params, a place says how it sounds in a
// sound look it wears, and a one-shot is an event a state says. A mixer
// (sg/audio/Mixer.hpp) reads all of it, as a renderer reads rooms, and owns
// only what it takes to be heard - voices, filters, delay lines, reverbs.
//
// A thing that sounds - any element, in any state - says:
//
//   sound           what it sounds as: a sound's name (the samples' own), or
//                   `stream:<id>` for one that fills itself
//   sound.gain      how loud, as a gain (1 as made)
//   sound.curve     how it is heard with distance: a curve class (Curve.hpp)
//   sound.bus       which bus it goes to: sfx, ambient, music, voice, ui
//   sound.loop      true: it goes round on its own state's time, so it waits
//                   while that state's time does and goes on from there
//   sound.priority  how much it matters when there are more voices than room
//   sound.pitch     faster and higher above 1
//   sound.at        an element of its place it stands at (else its own pose)
//   sound.ramp, sound.shape   how long a change of gain takes, and its shape
//                   (Ramp.hpp)
//   sound.when      day or night: heard by the place's own `hour`
//
// Its place is its state, if that is a place, else the place it is embedded
// in. A one-shot is said: an event carrying `sound` (and `gain`, `pitch`,
// `x y z` or `at`, `place`, and `or`: what it is where its place has no name
// for it); the mixer hears it as an observer may.
//
// A place wears its sound look through an embedding into its `sound` slot,
// as it wears a look into its `look` slot; the slot's `active` says which.
// A sound look says:
//
//   fade                 seconds a change to it takes (on the shell's interval)
//   reverb.size          how big its reverb is (1 as made)
//   reverb.decay         seconds to fall 60 dB (else worked out from the
//                        room's own shape and materials: Paths.hpp, eyring)
//   reverb.damp          how fast the top falls away in it, 0..1
//   reverb.wet           how much of it is heard
//   bus.<bus>            each bus's gain in dB
//   as.<name>            what a sound is called here (`as.step` = step_grass)
//   surface.floor/walls/ceiling   what the room is made of, for its reverb
//   attend.look, attend.uniform, attend.full, attend.bus.<bus>
//                        an attended look: while the host shows that visual
//                        look, its `uniform` over `full` is how far one leans
//                        in, and each bus is lowered by that much of its dB
//   element <bed> : bed  an ambient bed of the place: `sound`, `sound.gain`,
//                        `sound.when`, `sound.at` (else all round the ear,
//                        `sound.distance` off), `sound.through` (how much of
//                        it another place hears from its opening)
//
// An opening lets sound through as its seam says (`admits`; absent, all).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "sg/core/StateGraph.hpp"

namespace sg::audio {

namespace keys {
inline const Key sound{"sound"};
inline const Key gain{"sound.gain"};
inline const Key curve{"sound.curve"};
inline const Key bus{"sound.bus"};
inline const Key loop{"sound.loop"};
inline const Key priority{"sound.priority"};
inline const Key pitch{"sound.pitch"};
inline const Key at{"sound.at"};
inline const Key lift{"sound.lift"};
inline const Key ramp{"sound.ramp"};
inline const Key shape{"sound.shape"};
inline const Key when{"sound.when"};
inline const Key distance{"sound.distance"};
inline const Key through{"sound.through"};

inline const Key fade{"fade"};
inline const Key active{"active"};
inline const Key reverb_size{"reverb.size"};
inline const Key reverb_decay{"reverb.decay"};
inline const Key reverb_damp{"reverb.damp"};
inline const Key reverb_wet{"reverb.wet"};
inline const Key attend_look{"attend.look"};
inline const Key attend_uniform{"attend.uniform"};
inline const Key attend_full{"attend.full"};

inline const Key aperture{"aperture"};
inline const Key admits{"admits"};
}  // namespace keys

namespace kinds {
inline const Key slot{"sound_slot"};  // where a place wears its sound looks
inline const Key bed{"bed"};          // an ambient bed of a sound look
inline const Key curve{"curve"};      // a curve class (Curve.hpp)
}  // namespace kinds

// The buses: every voice goes through one, and a sound look sets each.
enum class Bus { Sfx = 0, Ambient, Music, Voice, Ui };
inline constexpr int kBuses = 5;

const char* bus_name(Bus b);
// The bus a word names, or `fallback`.
Bus bus_of(const std::string& word, Bus fallback);
// A sound look's gain for it, in dB: `bus.<name>`; and what an attended look takes off it.
Key bus_key(Bus b);
Key attend_bus_key(Bus b);

// --- wearing a sound look ---------------------------------------------------------
inline Key sound_slot_id() { return Key{"sound"}; }

// The slot a place wears its sound looks in, made if it has none.
Element& sound_slot(State& host);

// `host` sounds as `look`: an embedding into its slot, the first worn active.
const Embedding& wear_sound(StateGraph& g, Key host, Key look);
Embedding sound_embedding(Key host, Key look);

Key active_sound_look(const State& host);
void set_sound_look(State& host, Key look);
// Everything that lives in its slot: its sound looks, and what else sounds there.
std::vector<Key> in_sound_slot(const StateGraph& g, Key host);

// --- what an opening lets through ------------------------------------------------
// Whether a seam lets `what` through (`view light sound objects`): its own
// `admits`, or either of its doorways'; said by none, everything.
bool admits(const StateGraph& g, const Seam& s, const std::string& what);
// How long a crossing of it takes to fade one thing into another: its `fade`
// (seconds), or either doorway's, else 1.
double seam_fade(const StateGraph& g, const Seam& s);

// --- the laws ----------------------------------------------------------------------
// What stops the world's sound being heard as declared: a `sound` that names
// nothing `known` knows (a sound's name, or a `stream:` id), a curve out of
// order or not finite, a place reachable through a seam that lets sound
// through that wears no sound look.
std::vector<std::string> sound_defects(const StateGraph& g, const std::function<bool(const std::string&)>& known);

}  // namespace sg::audio
