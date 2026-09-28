#include "sg/domains/Console.hpp"

namespace sg {

ConsoleState::ConsoleState(Key id) : State(id) {
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

void ConsoleState::submit(const std::string& text) {
    emit(Event{submit_event(), Params{}.set(keys::text, text)});
}

void ConsoleState::push_line(std::string line) {
    lines_.push_back(std::move(line));
    while (lines_.size() > max_lines_) lines_.pop_front();
}

void ConsoleState::on_enter(const Params& args) {
    if (args.has(Key{"banner"})) push_line(to_string(args.get(Key{"banner"})));
}

}  // namespace sg
