// People and what they meet: a conversation of any number of speakers
// (sg/domains/Dialogue.hpp), the face that speaks with it (Face.hpp), and a
// door between two rooms (Door.hpp) - each a state of its own, joined only by
// functors, on lines of their own.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Dialogue.hpp"
#include "sg/domains/Door.hpp"
#include "sg/domains/Face.hpp"
#include "sg/domains/Spatial.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

const char* kTalk = R"(
# Three at a table: you, Ada and Ben.
speaker ada Ada
speaker ben Ben
start hello

exchange hello
  about greeting
  > Evening.
  ada: You came.
  ben: We said you would not.
  ada: I said you would.

exchange table
  about greeting table
  > Is this seat taken?
  ben: It was. Sit.

exchange cards
  about table cards
  after table
  prompt Deal me in.
  ben: Cards?
  > Deal me in.
  ada: He cheats.
  ends
)";

// Every line of where it is said, the clock run until it waits on a choice
// (or is over): the speakers in the order they spoke.
std::string run(sg::Engine& e, sg::Dialogue& d, sg::Face* face = nullptr, int* opened = nullptr) {
    std::string who;
    int was_line = -1;
    std::string was_at;
    for (int i = 0; i < 2000 && d.phase() != "choose" && d.phase() != "over"; ++i) {
        e.tick(1.0 / 60);
        if (d.phase() == "saying" && (d.line() != was_line || d.at() != was_at)) {
            was_line = d.line(), was_at = d.at();
            who += (who.empty() ? "" : " ") + d.lines_of(d.at())[static_cast<std::size_t>(d.line())].who;
        }
        if (face && opened && face->element(sg::Face::face_id()).params.num("shown") > 0) ++*opened;
    }
    return who;
}

void test_dialogue() {
    sg::StateGraph g;
    auto& clock = g.add<sg::Temporal>("clock");
    auto& d = g.add<sg::Dialogue>("talk");
    std::string why;
    check(d.read_text(kTalk, &why, "three.talk"), "the notation reads (" + why + ")");
    check(d.faults().empty(), "it says nothing it does not have");
    check(d.speakers() == std::vector<std::string>{"you", "ada", "ben"} && d.name_of("ada") == "Ada", "three speak in it, you among them");
    check(d.lines_of("hello").size() == 4 && d.lines_of("hello")[2].who == "ben", "an exchange is as many lines as it has, by whoever says them");
    check(d.prompt_of("table") == "Is this seat taken?" && d.prompt_of("cards") == "Deal me in.", "chosen by its first line of yours, or what it says");
    // A face that speaks with Ben.
    auto& face = g.add<sg::Face>("ben_face", 1, 2);
    sg::voices(g, face, d.id(), d.voice_id("ben"));
    sg::drive(g, clock, "talk", d.tick_event());
    sg::drive(g, clock, "ben_face", face.tick_event(), false, sg::Keeps::Always);
    g.port("talk", d.key_event());
    g.set_initial("talk");
    const auto problems = g.validate();
    check(problems.empty(), "the graph validates");
    const sg::LawReport laws = sg::verify(g);
    if (!laws.ok()) std::printf("%s\n", laws.str().c_str());
    check(laws.ok(), "the laws hold");

    sg::Engine e(g);
    e.start();
    int opened = 0;
    check(run(e, d, &face, &opened) == "you ada ben ada", "it begins where it begins: each line in turn, by whoever says it");
    check(opened > 0, "and the face of whoever speaks opens its mouth as they do");
    check(d.mouth("ada") == 0.0 && d.mouth("ben") == 0.0, "every mouth shut once it is said");
    const auto ways = d.options();
    check(ways.size() == 1 && d.to_of(ways[0]) == "table", "what can follow: what shares what it is about, and is not waiting");
    e.send(d.id(), d.key("1"));
    e.tick(1.0 / 60);
    check(d.phase() == "choose", "a choice before what can follow is all in brings it all, and takes nothing");
    e.send(d.id(), d.key("1"));
    e.tick(1.0 / 60);
    check(d.at() == "table" && d.phase() == "moving", "chosen: along the link, what it was chosen by going to where it is said");
    check(run(e, d) == "ben", "and then the rest of it - your line already said");
    check(d.history() == std::vector<std::string>{"hello"}, "what was said goes to the history");
    // Cards waited on the table; it is open now, and it ends it.
    std::vector<std::string> to;
    for (const std::string& l : d.options()) to.push_back(d.to_of(l));
    check(std::find(to.begin(), to.end(), "cards") != to.end(), "a way that waited opens once what it waited on is heard");
    for (int i = 0; i < 60; ++i) e.tick(1.0 / 60);
    const auto opts = d.options();
    for (std::size_t i = 0; i < opts.size(); ++i)
        if (d.to_of(opts[i]) == "cards") e.send(d.id(), d.key(std::to_string(i + 1)));
    e.tick(1.0 / 60);
    check(run(e, d) == "ben you ada" && d.phase() == "over", "a line of yours anywhere in it is said in its turn; an ending ends it");
}

void test_door() {
    sg::StateGraph g;
    auto& clock = g.add<sg::Temporal>("clock");
    auto& door = g.add<sg::Door>("door", 1.5, 0.25);
    auto& hall = g.add<sg::State>("hall");
    hall.add_element("hinge", "thing");
    hall.add_element("way", "doorway").params.set("leave", 1.0);
    hall.loop("idle", "hinge", "hall.tick", [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {});
    sg::Functor hung("door.hung", "door", "hall");
    hung.on_object(sg::Door::leaf_id(), "hinge", sg::door_hinge);
    hung.on_object(sg::Door::way_id(), "way", sg::door_near);
    g.set_functor(std::move(hung));
    g.keep("door.hung");
    // (Whoever is at it says so in the room, and it is carried to the door.)
    hall.says("hall.swing");
    sg::Functor by("door.by", "hall", "door");
    by.on_event("hall.swing", door.swing_key());
    g.set_functor(std::move(by));
    sg::drive(g, clock, "door", door.step_key(), false, sg::Keeps::Always);
    sg::drive(g, clock, "hall", "hall.tick");
    g.port("door", door.swing_key());
    g.set_initial("hall");
    check(g.validate().empty() && sg::verify(g).ok(), "a door hung in a room: the graph validates and the laws hold");
    sg::Engine e(g);
    e.start();
    e.tick(1.0 / 60);
    check(!door.open() && hall.element("way").params.num("leave") == 0.0 && hall.element("hinge").params.num(sg::keys::yaw) == 0.25,
          "shut: its doorway is not walked through, its hinge turned as shut");
    e.send(door.id(), sg::Event{door.swing_key(), sg::Params{}.set("open", 1.0)});
    for (int i = 0; i < 240; ++i) e.tick(1.0 / 60);
    check(door.open() && std::fabs(door.angle() - 1.5) < 1e-3 && hall.element("way").params.num("leave") == 1.0, "swung open, it is open, and walked through");
    check(std::fabs(hall.element("hinge").params.num(sg::keys::yaw) - 1.75) < 1e-3, "its hinge turned as far");
}

}  // namespace

int main() {
    test_dialogue();
    test_door();
    std::printf(failures ? "%d FAILED\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
