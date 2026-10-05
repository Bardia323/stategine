// The text of the modeller's built-in macros, and its libraries by name.
#pragma once

#include <string>

namespace sg::sculpt {
// Always there: what a castle, a village or a wood is made of.
const char* library();
// A library by name (`use <name>`): one a program defined (define_library),
// or one the modeller comes with - each its own file.
bool library_named(const std::string& name, std::string& text);
// What the libraries a program defined say, all of them, as one text: a
// recipe's mesh depends on them (the built-in ones are the code's own).
std::string defined_libraries();
// The ones it comes with: the architect's composer, and its styles - each
// saying the same words (ModelerArch.cpp) its own way.
const char* lib_arch();
const char* lib_classical();
const char* lib_gothic();
const char* lib_modern();
const char* lib_romanesque();
const char* lib_islamic();
const char* lib_japanese();
const char* lib_brutalist();
const char* lib_artdeco();
// And what fills a building: a church's furnishings (`use church`).
const char* lib_church();
}  // namespace sg::sculpt
