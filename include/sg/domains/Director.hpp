// Stategine - a director: a state of its own that stages others.
//
// A director is not a part of any world it stages. It is a small state that
// could exist with nothing to direct: a shot list (its `board`, the script as
// text) and its marks - where it wants each thing it directs to be, and how.
// It meets the states it stages only through the graph:
//
// Objects:  board  the script, one line a key or a cue (below)
//           <mark> what the director wants of one thing now: the params its
//                  script gives it at the director's own time (x, y, z, yaw,
//                  pitch, fov, ... - any number a thing has)
// Arrows:   board --<id>.tick--> board   the script played to the director's
//                                        own time: each mark set (and said
//                                        moved, `<id>.<mark>`), each cue
//                                        passed said
//           board --<id>.roll--> board   {on}: rolled, the scene starts from
//                                        its top; cut, it holds
//
// A mark is bound to a thing of another state (`bind`): a functor carries
// what the mark holds onto it with the word the director says when the mark
// moves - a camera set at a place, a character walked to another, a lamp
// dimmed. When the script has no more to say of a mark, it says nothing, and
// the thing is its own again: the director holds only what it moves, while
// it moves it. A world rolls the director by an event of its own (`action`),
// which is also how the director is reached. A cue (`cue`) is a word the director
// says at a time, carried by a functor to an event of another state - the
// lights put out, a door opened - and that state's own arrow does it.
//
// Its time is its own line on a Temporal (`sg::drive`, trigger `<id>.tick`):
// stopped, the scene holds; driven again, it goes on where it was.
//
// The script, a line each (`#` to the end of a line is a comment):
//
//   <t> <mark> <key>=<value> ... [ease=linear|smooth|in|out|cut]
//   <t> say <word> [<arg>=<value> ...]
//
// `t` in seconds of the director's time; a cue's args go with its word (a
// value that is not a number is text). Each key of each mark is a track of
// its own: its value at a line's time is what the line says, reached from its
// key before by the line's ease (`cut` jumps at `t`); before its first key a
// track says nothing, after its last it holds. A mark that has `aim_x aim_y
// aim_z` (where to look) is given the `yaw` and `pitch` that look from its
// `x y z` there, by the engine's own heading (`sg::forward_of`); the aim
// itself is not carried.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "sg/core/StateGraph.hpp"

namespace sg {

class Director : public State {
public:
    static Key board_id() { return Key{"board"}; }
    Key tick_event() const { return Key{id().str() + ".tick"}; }
    Key roll_event() const { return Key{id().str() + ".roll"}; }
    // What it says when `mark` moved: its binding carries the mark with it.
    Key moved_event(Key mark) const { return Key{id().str() + "." + mark.str()}; }

    explicit Director(Key id, const std::string& script = {});

    Key kind() const override { return Key{"director"}; }
    // What the script gets wrong, and marks it moves that the director has not.
    std::vector<std::string> faults() const override;

    // The shot list. Words it says (`say`) are declared as said.
    void set_script(const std::string& text);
    std::string script() const;
    // Where the script ends: its last key or cue.
    double length() const;

    // A mark of the director's own, made if new.
    Element& mark(Key name);

    // Each mark's params at time `t`, as the script says (pure in the script).
    std::map<Key, Params> at(double t) const;

    struct Point {
        double t = 0;
        double v = 0;
        int ease = 0;  // 0 linear, 1 smooth, 2 in, 3 out, 4 cut
    };
    struct Parsed {
        std::map<Key, std::map<Key, std::vector<Point>>> tracks;  // mark -> key -> keys in time
        struct Cue {
            double t = 0;
            Key word;
            Params args;
        };
        std::vector<Cue> cues;  // in time
        std::vector<std::string> errors;
        double length = 0;
    };
    // The script read (memoised on its text): what it says, and what it got wrong.
    const Parsed& parsed() const;

private:
    void play(double from, double to);
    mutable std::shared_ptr<const Parsed> parsed_;
    mutable std::string parsed_text_;
};

// The name of the functor that carries `mark` of `director`.
inline Key bind_name(Key director, Key mark) { return Key{director.str() + ".bind." + mark.str()}; }

// `mark` of `director` bound to `element` of `state`: a functor carries what
// the mark holds onto it, every frame the mark moves (with the word
// `<director>.<mark>`, heard by `state` as `<state>.directed`).
Functor& bind(StateGraph& g, Director& d, Key mark, Key state, Key element);

// `event` of `state` rolls the director: the scene starts from its top.
Functor& action(StateGraph& g, Director& d, Key state, Key event);

// The word `word` the director says carried to `event` of `state`.
Functor& cue(StateGraph& g, Director& d, Key word, Key state, Key event);

}  // namespace sg
