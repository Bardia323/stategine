// Shots: many pictures of a running world from one start of its program.
//
// A program that draws a world (a game, the lab's dev room, an example) takes
// a long time to start and a moment to draw. To look at a change from several
// places, start it once and let a shot script steer it: each shot is set up
// by the program's own commands, held a few frames (so fades, lit air and
// streaming settle), read back and written - then the next. At the end, a
// contact sheet of them all.
//
// The script is text, one shot a line ('#' starts a comment):
//
//     far:  go lounge; look -26 -2 33 1.65 49
//     crt:  look -95 -14 40.3 1.45 41.7      frames=90
//
// What the commands mean is the program's (its command language, through
// the `run` it gives); the shots only sequence them. Nothing here writes a
// state: a command goes through the program's own way of changing its world.
//
// In the program's frame loop: `before(run)` before the tick, `after(w, h)`
// once the frame is drawn into the bound framebuffer; `done()` when all are
// written.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace sg::render {

class Shots {
public:
    struct Shot {
        std::string name;
        std::vector<std::string> setup;  // the program's commands, run once, in order
        int frames = 30;                 // held this many frames; the last is written
    };

    // The shots a script says; a line that is not a shot is in `errors`.
    static std::vector<Shot> parse(const std::string& text, int frames = 30, std::vector<std::string>* errors = nullptr);
    // A script from a file, or - if `source` names none - the script itself.
    static std::vector<Shot> load(const std::string& source, int frames = 30, std::vector<std::string>* errors = nullptr);

    // Written to `dir` (<name>.png each, and sheet.png).
    Shots(std::vector<Shot> shots, std::string dir);

    bool done() const { return at_ >= shots_.size(); }
    const Shot* current() const { return done() ? nullptr : &shots_[at_]; }
    // The coming frame is a shot's first: what eases (an eye's exposure) is
    // to be settled there, so the shot does not depend on what came before it.
    bool starting() const { return !done() && frame_ == 0; }
    // Before a frame: a shot's setup, on its first frame. `run` answers in words.
    void before(const std::function<std::string(const std::string&)>& run);
    // After a frame is drawn: its picture, if it is a shot's last; and the
    // contact sheet after the last of all. Says what it wrote.
    void after(int w, int h);

private:
    std::vector<Shot> shots_;
    std::string dir_;
    std::size_t at_ = 0;
    int frame_ = 0;
    struct Picture {
        std::string name;
        int w = 0, h = 0;
        std::vector<unsigned char> rgb;
    };
    std::vector<Picture> taken_;
    void sheet() const;
};

}  // namespace sg::render
