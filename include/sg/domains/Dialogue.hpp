// Stategine - a conversation, as a state of its own: who speaks in it,
// everything that can be said, and where it is now. It knows no room and no
// one in it; whoever shows it binds it to a glass (it is a Surface2D, and
// paints nothing of its own - a game's conversation paints itself by
// overriding `paint`), whoever has it in focus sends it keys, and whatever
// speaks with a face follows a voice of it (`voice_id`, by a functor).
//
// It is a small category, as every state is: its objects are exchanges, its
// arrows the ways one exchange leads on to another.
//
//   a speaker    someone who speaks in it: `you` (always - whoever has it in
//                focus) and as many others as it says, each with a name it is
//                shown by, and a voice: an element of its own
//                (`<id>.voice.<who>`, {mouth}: how open the mouth is as the
//                words come, 0 shut .. 1 wide, eased as a jaw moves)
//   an exchange  what is said once it is chosen: lines, as many as it has,
//                each by any speaker (`<id>.<name>`, kind `exchange`: `lines`,
//                `who.<i>`, `say.<i>`); what it is chosen by (`prompt`: its
//                first line, if that is yours, unless it says otherwise);
//                what it is about (`topics`, words). Heard is counted
//                (`heard`); it can be said again.
//   a link       one exchange leading on to another: an arrow from the one
//                to the other (`<id>.go`, {link}), as strong as the two are
//                related - the topics they share, of all either has - and
//                more where it says so (`link a b weight`). A link into an
//                exchange that waits on others (`after`) is hidden until
//                those have been heard: paths that open, and loops, no tree.
//
// Where it is (`at`, an exchange; `phase`; `line`, the line of it):
//
//   moving   chosen: what it was chosen by goes to where it is said
//            (`move` seconds; the first line, if that is what it was)
//   waiting  the pause before a line (`pause` seconds)
//   saying   a line coming, letter by letter (`letter` seconds each), held a
//            moment once all said (`hold`; the last line `hold_last`)
//   choose   what can follow: the links out of where it is, strongest first,
//            not yet heard before heard, at most `options` - coming in over
//            `show` seconds (a choice before they are all in brings them all)
//   ending   an exchange that ends it, said: a moment (`end` s), then `over`
//
// What has been said, as it went (`history`: the exchanges, oldest first,
// the last `keep`). Its timings are its params, and can be said otherwise.
//
// Its arrows:
//
//   <id>.key   {key}: "up" "down" "enter" (the words that are coming, come;
//              or the one picked, chosen) "esc" (over), "1".."9" (that one);
//              any other key is the picture's to read (`other_key`)
//   <id>.go    {link}: along a link, to the exchange it leads to
//   <id>.tick  {dt, time}: its own line on a Temporal - the words come
//
// What it says, for the graph to take:
//
//   <id>.out   it is over (Esc, or an exchange that ends it)
//
// Everything it is, is in its params and its elements' (the laws re-run its
// arrows on them); a picture of it is a function of them.
//
// Written down (`read`), one line a thing (`#` a remark):
//
//   speaker <who> <name ...>        someone who speaks in it, as they are shown
//   start <exchange>                where every conversation begins
//   exchange <name>                 an exchange; what follows is its, until the next
//     about <topic> ...             what it is about (`*`: everything, a little)
//     after <exchange> ...          heard first, or it does not come up
//     ends                          said, the conversation is over
//     prompt <words>                what it is chosen by, if not its first line
//     > words                       a line of yours
//     < words                       a line of `them` (the one talked to)
//     <who>: words                  a line of a speaker it has
//                                   (a line by whoever said the line before runs on)
//   link <from> <to> <weight>       more related than their topics make them
#pragma once

#include <string>
#include <vector>

#include "sg/domains/Surface.hpp"

namespace sg {

class Dialogue : public Surface2D {
public:
    struct Line {
        std::string who, text;
    };

    // A conversation shown `w_px` by `h_px` (its picture its painter's).
    explicit Dialogue(Key id, int w_px = 1, int h_px = 1);

    Key kind() const override { return Key{"dialogue"}; }
    std::vector<std::string> faults() const override;

    Key key_event() const { return Key{id().str() + ".key"}; }
    Key go_event() const { return Key{id().str() + ".go"}; }
    Key tick_event() const { return Key{id().str() + ".tick"}; }
    Key out_event() const { return Key{id().str() + ".out"}; }
    Event key(const std::string& k) const { return Event{key_event(), Params{}.set("key", k)}; }

    // --- what can be said (before it starts) -----------------------------------
    // Someone who speaks in it, shown as `name` (`you` is always there).
    void speaker(const std::string& who, const std::string& name);
    // An exchange: its lines, what it is about; `after`: the exchanges that
    // must have been heard before it can come up; `ends`: said, it is over;
    // `prompt`: what it is chosen by, if not its first line.
    void exchange(const std::string& name, const std::vector<Line>& lines, const std::string& topics, const std::string& after = "",
                  bool ends = false, const std::string& prompt = "");
    // Where every conversation begins.
    void start_at(const std::string& name);
    // A link more than their topics make it (`weight` added to it).
    void link(const std::string& from, const std::string& to, double weight);
    // Every link, from what each exchange is about and what was added: an
    // arrow each (once all the exchanges are said).
    void connect();
    // Read from the notation (and connected): false, and why, if it cannot be.
    bool read_text(const std::string& text, std::string* why = nullptr, const std::string& from = "");
    bool read(const std::string& path, std::string* why = nullptr);

    // --- what it is now ---------------------------------------------------------
    std::string at() const { return params().get_or<std::string>("at", ""); }
    std::string phase() const { return params().get_or<std::string>("phase", ""); }
    // The line it is at (being said, or waited for).
    int line() const { return static_cast<int>(params().num("line")); }
    // How far into its phase it is (seconds of its own time).
    double since() const { return params().num("phase_since"); }
    // An exchange's lines, and what it is chosen by.
    std::vector<Line> lines_of(const std::string& exchange) const;
    std::string prompt_of(const std::string& exchange) const;
    bool heard(const std::string& exchange) const;
    bool ends(const std::string& exchange) const;
    // Where a link goes.
    std::string to_of(const std::string& link) const;
    // What has been said, oldest first.
    std::vector<std::string> history() const;
    // The name a speaker is shown by.
    std::string name_of(const std::string& who) const;
    // Everyone who speaks in it, `you` first.
    std::vector<std::string> speakers() const;
    // Where a speaker's voice is kept (`<id>.voice.<who>`, {mouth}).
    Key voice_id(const std::string& who = "them") const { return Key{id().str() + ".voice." + who}; }
    double mouth(const std::string& who = "them") const;
    // The links out of where it is that can be taken now, in the order shown;
    // the same, out of any exchange.
    std::vector<std::string> options() const;
    std::vector<std::string> options_from(const std::string& here) const;
    // How long a line takes to be said, all of it.
    double saying(const std::string& text) const;

    void on_enter(const Params& args) override;

protected:
    // For whoever paints it: an exchange begun - `from` where it was (`at`
    // still says so), `chosen` from what could follow (`chose`: which of
    // `ways_were`) - before it moves on; a step of its own time taken; a key
    // it does not know (true if it took it, with the one now picked in `pick`).
    virtual void begun(const std::string& from, const std::string& to, bool chosen);
    virtual void ticked(double dt, double time);
    virtual bool other_key(const std::string& key, const std::vector<std::string>& ways, int& pick);
    void paint() override {}

    Key part(const std::string& name) const { return Key{id().str() + "." + name}; }
    const Element* exchange_of(const std::string& name) const;

private:
    void on_key(const std::string& k);
    void on_tick(double dt, double time);
    void go(const std::string& link);
    void begin(const std::string& name);
    // What follows a line said (or what it was chosen by, moved): the next
    // line waited for, or what can follow, or the end.
    void after_line(int next, double time);
};

}  // namespace sg
