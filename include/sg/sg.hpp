// Stategine - umbrella header for the engine core and the built-in domains.
//
// Renderers are deliberately not included here: pull in sg/render/Ascii.hpp or
// sg/render/GLWorld.hpp where you need one, so a headless build never pays for
// a backend it does not use.
#pragma once

#include "sg/core/Adjunction.hpp"
#include "sg/core/Core.hpp"
#include "sg/core/Embedding.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Functor.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/State.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/core/Typed.hpp"
#include "sg/domains/Console.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/domains/Surface.hpp"
