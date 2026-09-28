// Stategine - Text: a state as text, and back.
//
// A state is its data - its own parameters, and its elements, each an id, a
// kind, alive or not, and its parameters - and its arrows, which are code: its
// class's. The data written out is the state's own source: a file it can live
// in apart from anything that shows it, be read back from, diffed, edited by
// hand while the game runs.
//
//   state <id> <kind>
//   param <key> <value>
//   element <id> <kind> [dead]
//     <key> <value>
//
// A value is `b:true`/`b:false`, `i:<integer>`, `d:<number>`, `s:<text>` (with
// \\, \n, \t, \s and \r escaped) or `-` for none. Ids and keys are escaped the
// same way, so any of them survives the round trip.
//
//   std::string src = sg::to_text(room);
//   sg::from_text(copy, src);           // copy now holds what room held
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <variant>

#include "sg/core/State.hpp"

namespace sg {

namespace text_detail {

std::string escape(const std::string& s);

std::string unescape(const std::string& s);

std::string value(const Value& v);

bool parse(const std::string& t, Value& out);

}  // namespace text_detail

// The state's data as text (see the top of this file). `keep_element` and
// `keep_param`, if given, leave out what is not part of what the state is -
// who is looking at it, what time it is there.
std::string to_text(const State& s, const std::function<bool(const Element&)>& keep_element = {},
                           const std::function<bool(Key)>& keep_param = {});

// Read `text` back into `s`: its params set, each element made if it is not
// there yet and its params set (an element's kind is kept if it is). With
// `exact`, elements and params the text does not have are taken away too, so
// `s` holds exactly what the text says. False, with `why`, on a line it
// cannot read - and nothing is changed.
bool from_text(State& s, const std::string& text, std::string* why = nullptr, bool exact = false);

}  // namespace sg
