// Stategine - work shared out over the machine's cores.
//
// The machine has many hands; a frame need not wait on one. This is the one
// set of them every part of a program shares - the engine stepping states on
// lanes of their own (a drive's `lane`), and a state sharing its own work out
// over its elements (`each`) - so two parts never start a hand per core each
// and fight over the machine.
//
// A hand is machinery, never a state's: nothing it does is kept anywhere but
// where the work it was given writes. Work given out is the same work done in
// another order - each piece writes only what is its own (an element, a slot
// of a result) - so what comes of it is what one hand would have made.
//
// Whoever gives work out does it too: it takes pieces with the hands, so work
// given out from inside a piece (a state on a lane sharing out its elements)
// is never stuck waiting for a hand that is busy with the piece it is in.
#pragma once

#include <cstddef>
#include <functional>

namespace sg::work {

// How many hands there are, the caller's among them: the machine's cores
// unless said (`set_hands`; 1 is the caller alone, nothing shared out).
unsigned hands();
// Says how many hands to keep, the caller's among them; 0 is the machine's
// cores. Hands already started are kept; more are started when asked for.
void set_hands(unsigned n);

// `fn(i)` for every i in [0, n), shared out over the hands, the caller's
// among them; back when all are done. What a piece throws is thrown here
// (the first, once all are done). Pieces should write only what is theirs.
void each(std::size_t n, const std::function<void(std::size_t)>& fn);

// `mine()` on the caller's own thread while `fn(i)` for i in [0, n) is
// shared out over the other hands - the caller joining them when its own is
// done. For work that must stay on the thread it was given on (a GL context,
// a state not given a lane) beside work that need not.
void beside(const std::function<void()>& mine, std::size_t n, const std::function<void(std::size_t)>& fn);

// Whether this thread is one of the hands, at work on a piece.
bool in_hand();

}  // namespace sg::work
