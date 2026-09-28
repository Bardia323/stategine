// Stategine - the console domain: a scrollback and an input line.
//
// Objects: "log", "input", "prompt". Morphisms: input --submit--> log, and
// log --clear--> log. No printing happens here; render it with a view.
#pragma once

#include <deque>
#include <string>

#include "sg/core/State.hpp"

namespace sg {

class ConsoleState : public State {
public:
    explicit ConsoleState(Key id = Key{"console"});

    Key kind() const override { return Key{"console"}; }

    static Key submit_event() { return Key{"console.submit"}; }
    static Key command_event() { return Key{"console.command"}; }
    static Key clear_event() { return Key{"console.clear"}; }

    void submit(const std::string& text);

    void push_line(std::string line);

    const std::deque<std::string>& lines() const { return lines_; }
    std::deque<std::string>& lines() { return lines_; }
    void set_max_lines(std::size_t n) { max_lines_ = n; }

    void on_enter(const Params& args) override;

private:
    std::deque<std::string> lines_;
    std::size_t max_lines_ = 256;
};

}  // namespace sg
