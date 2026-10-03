// Stategine DSL - a program, lowered: the engine's own declarations, in order.
//
// A Plan is a list of steps, and each step is one call of the engine's API -
// `graph.add`, `State::affine`, `graph.embed`, `sg::drive` - written as data.
// It is not a language and it has no semantics of its own: there is nothing in
// it to run except the calls it names. Three things read it, and only these:
//
//   emit  (Emit.hpp)   writes the calls as C++, for a normal compiler
//   apply (Apply.hpp)  makes the calls on a graph - a world rewriting itself,
//                      by `graph.edit`
//   facts (Facts.hpp)  says what it declares, in the words the graph is
//                      described in, to hold beside a graph built by hand
//
// The engine's structs are used as they are (Transition, Embedding, Drive,
// Affine) so that a step cannot say what the engine cannot.
#pragma once

#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "sg/core/StateGraph.hpp"
#include "sg/dsl/Ast.hpp"

namespace sg::dsl::plan {

// graph.add<Class>(id)
struct State {
    Key id;
    std::string kind;
};
// a state built elsewhere, that the graph must hold (expect_state)
struct Extern {
    Key id;
    std::string kind;
};
// State::add_element - or, where the state's own class makes it, expect_element
struct Element {
    Key state, id, kind;
    bool own = false;
};
// a parameter of the state (element empty) or of an element
struct Param {
    Key state, element, key;
    Value value;
    std::string from_file;  // set when the value was read from file("...")
};
// State::arrow / loop / affine - or, where the class makes it, expect_arrow
struct Arrow {
    enum class Body { None, Affine, Native };
    Key state, name, from, to, trigger;  // to empty: a loop
    Body body = Body::None;
    sg::Affine affine;
    std::string native;
    bool own = false;
};
// State::compose
struct Compose {
    Key state, name, f, g, trigger;
};
// State::says
struct Says {
    Key state, event;
};
// graph.add_functor - or, for `extern functor`, expect_functor
struct Functor {
    Key name, from, to;
    bool built_elsewhere = false;
};
// Functor::on_object
struct Object {
    enum class Transport { Copy, Only, Swizzle, Affine, Native };
    Key functor, src, dst;
    Transport transport = Transport::Copy;
    std::vector<Key> names;
    std::vector<std::pair<Key, Key>> pairs;
    bool rest = false;
    sg::Affine affine;
    std::string native;
};
// Functor::on_event / on_morphism
struct EventMap {
    Key functor, src, dst;
};
struct ArrowMap {
    Key functor, src, dst;
};
// graph.compose_functors
struct ComposeFunctors {
    Key name;
    std::vector<Key> chain;
};
// graph.lens
struct Lens {
    Key get, put;
};
// graph.connect
struct Connect {
    sg::Transition t;  // what the state entered is told is `t.enter`: data, not a lambda
};
// graph.embed
struct Embed {
    sg::Embedding e;
};
// sg::glue_doorway
struct Glue {
    Key name, a, pa, b, pb;
    std::vector<std::pair<Key, Key>> also;
    bool wraps = false;
};
// sg::drive
struct Drive {
    sg::Drive d;
};
// graph.port
struct Port {
    Key state, event;
};
// graph.keep
struct Keep {
    Key functor;
};
// sg::wear
struct Wear {
    Key host, look;
};
// sg::film
struct Film {
    Key camera, world, rig;
};
// graph.set_initial
struct Initial {
    Key state;
};
// graph.edit
struct Edit {
    Key state, event, reply;
    std::string native;
};
// an input device's table (Runtime.hpp: Bindings), for an adapter to fire
struct Bind {
    struct Entry {
        std::string key;
        Key event;
        Params args;
    };
    std::string device;
    std::vector<Entry> entries;
};

}  // namespace sg::dsl::plan

namespace sg::dsl {

using Step = std::variant<plan::State, plan::Extern, plan::Element, plan::Param, plan::Arrow, plan::Compose, plan::Says,
                          plan::Functor, plan::Object, plan::EventMap, plan::ArrowMap, plan::ComposeFunctors, plan::Lens,
                          plan::Connect, plan::Embed, plan::Glue, plan::Drive, plan::Port, plan::Keep, plan::Wear,
                          plan::Film, plan::Edit, plan::Initial, plan::Bind>;

struct Plan {
    std::vector<Step> steps;
    std::vector<std::string> sources;  // the files it was made from, and the files it read
};

}  // namespace sg::dsl
