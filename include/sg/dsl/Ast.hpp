// Stategine DSL - what a source document says, as it is read.
//
// The DSL is notation, not a state and not a runtime: a program is a textual
// presentation of a StateGraph. Reading it gives this - plain data, one node
// for each thing the text declares - and nothing here runs. Every node names
// the existing Stategine primitive it lowers to (see Plan.hpp); a construct
// with no such primitive has no node.
//
//   state  -> a State (and its elements, params, arrows, what it says)
//   functor, compose, lens, transition, embed, seam, drive, port, keep, edit
//          -> the graph declaration of the same name
//   wear, film -> sg::wear, sg::film
//   when   -> an event mapping of a functor
//   bind   -> a table an input adapter turns into Engine::fire
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sg::dsl {

struct Loc {
    std::string file;
    int line = 0;
    int col = 0;
};

// One thing wrong, where, and - if there is one - what to do instead.
struct Diagnostic {
    Loc at;
    std::string message;
    std::string hint;
    std::string str() const;
};

// A value as written: a number is a number (a double, as every parameter of a
// world is), `int(5)` an integer, and so on.
struct Val {
    enum class Kind { Number, Int, Bool, Text, Vector, File };
    Kind kind = Kind::Number;
    double number = 0.0;
    int64_t integer = 0;
    bool flag = false;
    std::string text;  // Text: the text; File: the path as written
    std::vector<double> vec;
    Loc at;
};

struct ParamAst {
    Loc at;
    std::string key;
    Val value;
};

// One term of a declared step: k * param * arg (see sg/core/Declared.hpp).
struct TermAst {
    double k = 1.0;
    std::string param;  // empty: only a number (and maybe an argument)
    std::string arg;
    bool of_target = false;
};

struct RowAst {
    Loc at;
    std::string param;
    std::vector<TermAst> terms;
    double bias = 0.0;
};

// What an arrow does: nothing said (an identity), what it says (rows: an
// Affine), or a computation that belongs in C++ (native).
struct BodyAst {
    enum class Kind { None, Rows, Native };
    Kind kind = Kind::None;
    bool copy_all = false;
    std::vector<RowAst> rows;
    std::string native;
};

struct ArrowAst {
    Loc at;
    std::string from, to, name;
    std::vector<std::string> args;  // the event arguments it reads
    std::string trigger;            // empty: its name
    BodyAst body;
};

struct ComposeAst {
    Loc at;
    std::string name;
    std::vector<std::string> chain;  // first applied first
    std::string trigger;             // arrows only; empty: the name
};

struct ElementAst {
    Loc at;
    std::string id, kind;
    std::vector<ParamAst> params;
};

struct StateAst {
    Loc at;
    std::string name, kind;
    bool is_extern = false;
    std::vector<ParamAst> params;
    std::vector<ElementAst> elements;
    std::vector<ArrowAst> arrows;
    std::vector<ComposeAst> composes;
    std::vector<std::pair<std::string, Loc>> says;
};

// How one object's parameters cross a functor.
struct TransportAst {
    enum class Kind { Copy, Only, Swizzle, Rows, Native, Via };
    Kind kind = Kind::Copy;
    std::vector<std::string> names;                         // Only
    std::vector<std::pair<std::string, std::string>> pairs;  // Swizzle: {destination, source}
    bool rest = false;                                       // Swizzle: and the rest as they are
    BodyAst rows;                                            // Rows: what it says
    std::string native;                                      // Native / Via: the name
};

struct ObjectAst {
    Loc at;
    std::string src, dst;
    TransportAst transport;
};

struct MapAst {
    Loc at;
    std::string src, dst;
};

struct FunctorAst {
    Loc at;
    bool is_extern = false;  // built elsewhere: named here, not made
    std::string name, from, to;
    std::vector<ObjectAst> objects;
    std::vector<MapAst> events;
    std::vector<MapAst> arrows;
};

struct ComposeFunctorsAst {
    Loc at;
    std::string name;
    std::vector<std::string> chain;
};

struct LensAst {
    Loc at;
    std::string get, put;
};

struct TransitionAst {
    Loc at;
    std::string from;  // a state, or "*"
    std::string trigger;
    std::string to;  // a state; empty for pop
    bool push = false;
    bool pop = false;
    std::string carry;
    std::string name;
    std::vector<ParamAst> with;  // what the state entered is told (Transition::action)
};

struct SeamAst {
    Loc at;
    std::string a, pa, b, pb;  // state, portal, state, portal (as written: split by the checker)
    std::vector<std::pair<std::string, std::string>> also;
    std::string name;
};

struct EmbedAst {
    Loc at;
    std::string host_portal;  // "host.portal", split by the checker
    std::string guest;
    std::string in, out, subject, name;
    std::string sync;       // live | commit | view; empty: the engine's default
    std::string propagate;  // onchange | continuous | onevent | manual
    int focus = -1;         // -1: the engine's default
    int follows = -1;
};

struct DriveAst {
    Loc at;
    std::string clock, target, event, keeps;
    bool additive = false;
};

struct PortAst {
    Loc at;
    std::string target;  // "state.event", split by the checker; or the state, with `event`
    std::string event;
};

struct KeepAst {
    Loc at;
    std::string functor;
};

struct WearAst {
    Loc at;
    std::string host, look;
};

struct FilmAst {
    Loc at;
    std::string camera, world, rig;
};

struct WhenAst {
    Loc at;
    std::string event, target;
};

struct BindEntryAst {
    Loc at;
    std::string key, event;
    std::vector<std::pair<std::string, Val>> args;
};

struct BindAst {
    Loc at;
    std::string device;
    std::vector<BindEntryAst> entries;
};

struct TransportAliasAst {
    Loc at;
    std::string name, native;
};

struct EditAst {
    Loc at;
    std::string state, event, native, reply;
};

struct InitialAst {
    Loc at;
    std::string state;
};

struct Program {
    std::string file;
    std::vector<InitialAst> initials;
    std::vector<StateAst> states;
    std::vector<FunctorAst> functors;
    std::vector<ComposeFunctorsAst> functor_composes;
    std::vector<LensAst> lenses;
    std::vector<TransitionAst> transitions;
    std::vector<SeamAst> seams;
    std::vector<EmbedAst> embeds;
    std::vector<DriveAst> drives;
    std::vector<PortAst> ports;
    std::vector<KeepAst> keeps;
    std::vector<WearAst> wears;
    std::vector<FilmAst> films;
    std::vector<WhenAst> whens;
    std::vector<BindAst> binds;
    std::vector<TransportAliasAst> transports;
    std::vector<EditAst> edits;
};

}  // namespace sg::dsl
