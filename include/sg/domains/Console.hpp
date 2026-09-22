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
    explicit ConsoleState(Key id = Key{"console"}) : State(id) {
        add_element(Key{"log"}, kinds::textbuffer).params.set(Key{"count"}, int64_t{0});
        add_element(Key{"input"}, kinds::textline).params.set(keys::text, std::string{});
        add_element(Key{"prompt"}, Key{"label"}).params.set(keys::text, std::string("> "));

        // input --submit--> log
        arrow(Key{"submit"}, Key{"input"}, Key{"log"}, submit_event(),
              [](State& s, Element& in, Element* log, const Event& ev) {
                  std::string text = ev.args.get_or<std::string>(
                      keys::text, in.params.get_or<std::string>(keys::text, ""));
                  if (text.empty()) return;
                  auto& self = static_cast<ConsoleState&>(s);
                  self.push_line(text);
                  in.params.set(keys::text, std::string{});
                  log->params.set(Key{"count"}, static_cast<int64_t>(self.lines_.size()));
                  s.emit(Event{command_event(), Params{}.set(keys::text, text)});
              });

        // log --clear--> log
        loop(Key{"clear"}, Key{"log"}, clear_event(),
             [](State& s, Element& log, Element*, const Event&) {
                 static_cast<ConsoleState&>(s).lines_.clear();
                 log.params.set(Key{"count"}, int64_t{0});
             });
    }

    Key kind() const override { return Key{"console"}; }

    static Key submit_event() { return Key{"console.submit"}; }
    static Key command_event() { return Key{"console.command"}; }
    static Key clear_event() { return Key{"console.clear"}; }

    void submit(const std::string& text) {
        emit(Event{submit_event(), Params{}.set(keys::text, text)});
    }

    void push_line(std::string line) {
        lines_.push_back(std::move(line));
        while (lines_.size() > max_lines_) lines_.pop_front();
    }

    const std::deque<std::string>& lines() const { return lines_; }
    std::deque<std::string>& lines() { return lines_; }
    void set_max_lines(std::size_t n) { max_lines_ = n; }

    void on_enter(const Params& args) override {
        if (args.has(Key{"banner"})) push_line(to_string(args.get(Key{"banner"})));
    }

private:
    std::deque<std::string> lines_;
    std::size_t max_lines_ = 256;
};

}  // namespace sg
