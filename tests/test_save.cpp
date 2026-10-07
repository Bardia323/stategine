// A save game: some states kept, the rest played again, and what follows
// from the kept brought back through the links the graph declares - a game
// won is won again when the base that remembers it is loaded, a trophy lit by
// the game is lit, a cartridge in an arcade in the base starts again, and a
// state the save does not reach is left as it is.
#include "sg/domains/Save.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Text.hpp"

#ifdef SG_SAVE_DSL
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Runtime.hpp"
namespace sgen {
void build_save(sg::StateGraph&, const sg::dsl::Natives&, sg::dsl::Bindings&);
}
#endif

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

namespace fs = std::filesystem;
using sg::Key;

static double num(const sg::StateGraph& g, const char* s, const char* e, const char* p) {
    return g.state(Key{s}).element(Key{e}).params.num(Key{p}, -1);
}
static void set(sg::StateGraph& g, const char* s, const char* e, const char* p, double v) {
    g.state(Key{s}).element(Key{e}).params.set(Key{p}, v);
}

// The world: a base the game is played from, which remembers what was won;
// a game, a trophy the game lights; an arcade in the base with a cartridge in
// it; a player, and the weather, which nothing links to the base.
static sg::Save& world(sg::StateGraph& g) {
    sg::State& base = g.add<sg::State>(Key{"base"});
    base.add_element(Key{"games"}, Key{"flags"}).params.set(Key{"game_won"}, 0.0);
    base.add_element(Key{"gold"}, Key{"purse"}).params.set(Key{"n"}, 0.0);
    base.add_element(Key{"ledger"}, Key{"book"}).params.set(Key{"score"}, 0.0);
    base.add_element(Key{"tv"}, Key{"portal"});
    base.add_element(Key{"door"}, Key{"door"});
    base.loop(Key{"reach"}, Key{"door"}, Key{"base.reach"},
              [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit(Key{"base.checkpoint"}); });
    base.says(Key{"base.checkpoint"});

    sg::State& game = g.add<sg::State>(Key{"game"});
    sg::Params run;
    run.set(Key{"won"}, 0.0).set(Key{"level"}, 1.0).set(Key{"hp"}, 10.0);
    game.add_element(Key{"run"}, Key{"run"}).params = run;
    g.add<sg::State>(Key{"trophy"}).add_element(Key{"cup"}, Key{"cup"}).params.set(Key{"lit"}, 0.0);
    sg::State& arcade = g.add<sg::State>(Key{"arcade"});
    arcade.add_element(Key{"screen"}, Key{"screen"}).params.set(Key{"score"}, 0.0);
    arcade.add_element(Key{"slot"}, Key{"portal"});
    g.add<sg::State>(Key{"cart"}).add_element(Key{"rom"}, Key{"rom"}).params.set(Key{"progress"}, 0.0);
    sg::State& player = g.add<sg::State>(Key{"player"});
    player.add_element(Key{"body"}, Key{"body"}).params.set(Key{"x"}, 0.0);
    player.add_element(Key{"camera"}, Key{"camera"}).params.set(Key{"yaw"}, 0.0);
    g.add<sg::State>(Key{"weather"}).add_element(Key{"sky"}, Key{"sky"}).params.set(Key{"rain"}, 0.0);

    sg::Save& save = g.add<sg::Save>(Key{"progress"});
    save.keep(Key{"base"}).keep(Key{"player"}, "camera");
    save.says(save.at_event(Key{"base"}));

    g.set_initial(Key{"base"});
    // The game writes what it won into the base, and lights the trophy: both
    // follow it as it is played.
    g.add_functor(Key{"won"}, Key{"game"}, Key{"base"})
        .on_object(Key{"run"}, Key{"games"}, sg::transport::swizzle({{Key{"game_won"}, Key{"won"}}}));
    g.keep(Key{"won"});
    g.add_functor(Key{"lights"}, Key{"game"}, Key{"trophy"})
        .on_object(Key{"run"}, Key{"cup"}, sg::transport::swizzle({{Key{"lit"}, Key{"won"}}}));
    g.keep(Key{"lights"});
    // And its score, by a native: carried forward, but nothing says how to undo it.
    g.add_functor(Key{"score"}, Key{"game"}, Key{"base"})
        .on_object(Key{"run"}, Key{"ledger"}, [](const sg::Element& s, sg::Element& d) {
            d.params.set(Key{"score"}, s.params.num(Key{"hp"}) * 100);
        });
    g.keep(Key{"score"});
    g.embed(Key{"tv"}, Key{"base"}, Key{"tv"}, Key{"arcade"}, Key{}, Key{});
    g.embed(Key{"cart"}, Key{"arcade"}, Key{"slot"}, Key{"cart"}, Key{}, Key{});
    g.connect(Key{"base"}, Key{"play"}, Key{"game"});
    g.connect(Key{"game"}, Key{"home"}, Key{"base"});
    g.connect(Key{"base"}, Key{"look"}, Key{"weather"});
    g.connect(Key{"weather"}, Key{"back"}, Key{"base"});
    g.connect(Key{"base"}, Key{"walk"}, Key{"player"});
    g.connect(Key{"player"}, Key{"stop"}, Key{"base"});
    // Reaching the door is a checkpoint: the base's word carried to the save.
    g.add_functor(Key{"checkpoint"}, Key{"base"}, Key{"progress"})
        .on_event(Key{"base.checkpoint"}, save.save_event());
    g.port(Key{"progress"}, save.save_event());
    g.port(Key{"progress"}, save.load_event());
    g.port(Key{"progress"}, save.restore_event());
    g.port(Key{"base"}, Key{"base.reach"});
    g.edit(Key{"progress"}, save.restrict_event(), sg::Save::restrict_kept);
    g.edit(Key{"progress"}, save.extend_event(), sg::Save::extend_kept);
    sg::Transition home;
    home.from = sg::StateGraph::any();
    home.trigger = save.at_event(Key{"base"});
    home.to = Key{"base"};
    g.connect(home);
    return save;
}

// Frames, the device pumped after each, as a program's loop does.
static void frames(sg::Engine& e, sg::SaveFiles& disk, int n) {
    for (int i = 0; i < n; ++i) {
        e.tick(0.01);
        disk.pump(e);
    }
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("sg_save_" + std::to_string(std::rand()));
    fs::remove_all(root);
    sg::SaveFiles disk{sg::Assets{root}};

    {
        sg::StateGraph g;
        sg::Save& save = world(g);
        for (const auto& why : g.validate()) std::printf("  %s\n", why.c_str());
        check(g.validate().empty(), "a save is a state like any other, reached by what is said to it");
        const sg::LawReport laws = sg::verify(g);
        check(laws.ok(), "and keeps the laws");

        sg::Engine e(g);
        e.set_strict(true);
        sg::Params said;
        save.bus().subscribe(save.loaded_event(), [&](const sg::Event& ev) { said = ev.args; });
        sg::Params saved;
        save.bus().subscribe(save.saved_event(), [&](const sg::Event& ev) { saved = ev.args; });
        disk.watch(save);
        e.start();

        // The extension, worked out and not done.
        const sg::Save::Extension x = save.extension(g);
        const auto depth = [&](const char* s) {
            for (const auto& r : x.derived)
                if (r.state == Key{s}) return r.depth;
            return -1;
        };
        check(x.kept.size() == 2 && x.kept[0] == Key{"base"} && x.kept[1] == Key{"player"},
              "it keeps the base and the player");
        check(depth("game") == 1 && depth("arcade") == 1, "the game follows from the base, back along what it wrote; the arcade lives in it");
        check(depth("trophy") == 2 && depth("cart") == 2, "the trophy from the game, the cartridge from the arcade: as deep as it goes");
        check(depth("weather") == -1 && depth("progress") == -1, "the weather is not the save's, nor the save itself");
        bool hole = false;
        for (const auto& h : x.holes) hole = hole || (h.find("score") != std::string::npos && h.find("keep game") != std::string::npos);
        check(hole, "what the game's score carries cannot be brought back: the save names it, and what to do");

        // Played: the game won, at level 5, nearly dead; gold found; a high
        // score in the arcade, a cartridge half done; walked, looked round; rain.
        set(g, "game", "run", "won", 1);
        set(g, "game", "run", "level", 5);
        set(g, "game", "run", "hp", 3);
        set(g, "base", "gold", "n", 7);
        set(g, "arcade", "screen", "score", 99);
        set(g, "cart", "rom", "progress", 42);
        set(g, "player", "body", "x", 5);
        set(g, "player", "camera", "yaw", 90);
        set(g, "weather", "sky", "rain", 1);
        e.tick(0.01);
        check(num(g, "base", "games", "game_won") == 1 && num(g, "trophy", "cup", "lit") == 1,
              "won: the base remembers it and the trophy is lit, as the game is played");

        // Saved at the door.
        e.send(Key{"base"}, sg::Event{Key{"base.reach"}});
        frames(e, disk, 3);
        check(saved.get_or<bool>(Key{"ok"}, false) && saved.get_or<int64_t>(Key{"kept"}, 0) == 2,
              "reaching the door saves: two states written");
        const fs::path file = disk.file(save.id(), "1");
        check(fs::exists(file) && file.parent_path() == root / "progress", "in the save's own folder, by its slot");
        std::string text;
        {
            std::ifstream in(file);
            text.assign(std::istreambuf_iterator<char>(in), {});
        }
        check(text.find("state base") != std::string::npos && text.find("state player") != std::string::npos &&
                  text.find("state game") == std::string::npos && text.find("state weather") == std::string::npos,
              "only what it keeps is written");
        check(text.find("yaw") == std::string::npos, "and not what a kept state leaves out: the camera");
        check(text.find("at base") != std::string::npos, "with where it was made");

        // Played on, badly; then looked at the weather.
        set(g, "base", "gold", "n", 1);
        set(g, "game", "run", "won", 0);
        set(g, "game", "run", "level", 9);
        set(g, "arcade", "screen", "score", 3);
        set(g, "cart", "rom", "progress", 80);
        set(g, "player", "body", "x", 50);
        set(g, "player", "camera", "yaw", 45);
        set(g, "weather", "sky", "rain", 2);
        e.tick(0.01);
        e.fire(Key{"look"});
        e.tick(0.01);
        check(e.current()->id() == Key{"weather"}, "(out looking at the weather)");

        // Loaded.
        e.send(Key{"progress"}, sg::Event{save.load_event()});
        frames(e, disk, 4);
        check(said.get_or<bool>(Key{"ok"}, false), "loaded: " + said.get_or<std::string>(Key{"why"}, ""));
        check(num(g, "base", "gold", "n") == 7 && num(g, "base", "games", "game_won") == 1,
              "the base is as it was saved");
        check(num(g, "player", "body", "x") == 5, "and the player");
        check(num(g, "player", "camera", "yaw") == 0, "what the player left out is played again from the start");
        check(num(g, "game", "run", "won") == 1, "the game the base remembers winning is won");
        check(num(g, "game", "run", "level") == 1 && num(g, "game", "run", "hp") == 10,
              "and the rest of it starts again: it is not kept");
        check(num(g, "trophy", "cup", "lit") == 1, "the trophy the won game lit is lit, two links away");
        check(num(g, "arcade", "screen", "score") == 0 && num(g, "cart", "rom", "progress") == 0,
              "what lives in the base starts again, the cartridge in the arcade too");
        check(num(g, "weather", "sky", "rain") == 2, "the weather, which the save does not reach, is left as it is");
        check(e.current()->id() == Key{"base"}, "and the game goes back to where it was saved");

        // Lan -| K*: what was loaded, saved, is what was saved.
        e.send(Key{"progress"}, sg::Event{save.save_event()});
        frames(e, disk, 3);
        std::string again;
        {
            std::ifstream in(file);
            again.assign(std::istreambuf_iterator<char>(in), {});
        }
        // But for the hole it named: the game's score, carried into the base
        // by a native from a game played again (hp 10, not 3).
        std::string expected = text;
        const std::string was = "score d:300", now = "score d:1000";
        check(expected.find(was) != std::string::npos, "(the score saved was the game's, 3 hp)");
        expected.replace(expected.find(was), was.size(), now);
        check(again == expected,
              "saved again, the save is word for word what it was - the unit is the identity - but for the hole it named");

        // A slot of its own.
        e.send(Key{"progress"}, sg::Event{save.save_event(), sg::Params{}.set(Key{"slot"}, std::string("two"))});
        frames(e, disk, 3);
        check(fs::exists(disk.file(save.id(), "two")), "a slot asked for is a file of its own");
        e.send(Key{"progress"}, sg::Event{save.load_event(), sg::Params{}.set(Key{"slot"}, std::string("none"))});
        frames(e, disk, 3);
        check(!said.get_or<bool>(Key{"ok"}, true), "an empty slot loads nothing, and says so");

        // A damaged save changes nothing.
        { std::ofstream(disk.file(save.id(), "bad")) << "at base\nstate base state\nelement gold purse\n  n zz:9\n"; }
        set(g, "base", "gold", "n", 3);
        set(g, "game", "run", "level", 4);
        e.send(Key{"progress"}, sg::Event{save.load_event(), sg::Params{}.set(Key{"slot"}, std::string("bad"))});
        frames(e, disk, 3);
        check(!said.get_or<bool>(Key{"ok"}, true) && num(g, "base", "gold", "n") == 3 && num(g, "game", "run", "level") == 4,
              "a save that cannot be read changes nothing: all or nothing");
        check(e.problems().empty(), "and the engine's watch found nothing wrong");
    }

    {
        // Two kept states say different things of one parameter of a third:
        // there is no colimit there, and the save says so.
        sg::StateGraph g;
        g.add<sg::State>(Key{"a"}).add_element(Key{"e"}, Key{"n"}).params.set(Key{"v"}, 1.0);
        g.add<sg::State>(Key{"b"}).add_element(Key{"e"}, Key{"n"}).params.set(Key{"v"}, 2.0);
        g.add<sg::State>(Key{"c"}).add_element(Key{"e"}, Key{"n"}).params.set(Key{"v"}, 0.0);
        sg::Save& save = g.add<sg::Save>(Key{"s"});
        save.keep(Key{"a"}).keep(Key{"b"});
        g.add_functor(Key{"ac"}, Key{"a"}, Key{"c"}).on_object(Key{"e"}, Key{"e"}, sg::transport::only({Key{"v"}}));
        g.add_functor(Key{"bc"}, Key{"b"}, Key{"c"}).on_object(Key{"e"}, Key{"e"}, sg::transport::only({Key{"v"}}));
        g.keep_defaults();
        const sg::Params w = sg::Save::restrict_kept(g, sg::Event{save.restrict_event(), sg::Params{}.set(Key{"save"}, std::string("s"))});
        check(w.get_or<bool>(Key{"ok"}, false) && w.text(Key{"text"}), "saved by the edit alone, with no engine and no file");
        const sg::Params r = sg::Save::extend_kept(
            g, sg::Event{save.extend_event(), sg::Params{}.set(Key{"save"}, std::string("s")).set(Key{"text"}, *w.text(Key{"text"}))});
        const std::string why = r.get_or<std::string>(Key{"why"}, "");
        check(why.find("c.e.v") != std::string::npos && why.find("ac says") != std::string::npos,
              "two links that disagree on c.e.v are named: " + why);
    }

#ifdef SG_SAVE_DSL
    {
        // The same, written in the notation (tests/dsl/save.sg).
        sg::dsl::Natives n;
        n.arrow("checkpoint", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) {
            s.emit(Key{"base.checkpoint"});
        });
        n.edit("restrict.kept", sg::Save::restrict_kept).edit("extend.kept", sg::Save::extend_kept);
        sg::StateGraph g;
        sg::dsl::Bindings b;
        sgen::build_save(g, n, b);
        sg::SaveFiles files{sg::Assets{root / "dsl"}};
        sg::SaveFiles& disk = files;
        check(g.validate().empty() && sg::verify(g).ok(), "a save declared in the notation is a lawful state of the graph");
        auto* save = dynamic_cast<sg::Save*>(g.find(Key{"progress"}));
        check(save != nullptr, "of the engine's kind `save`");
        sg::Engine e(g);
        e.set_strict(true);
        disk.watch(*save);
        e.start();
        e.fire(Key{"play"});
        e.tick(0.01);
        set(g, "game", "run", "won", 1);
        set(g, "game", "run", "level", 3);
        e.tick(0.01);
        e.fire(Key{"home"});
        e.tick(0.01);
        e.send(Key{"base"}, sg::Event{Key{"base.reach"}});
        frames(e, disk, 3);
        check(fs::exists(disk.file(save->id(), "1")), "the checkpoint at the door saved it");
        set(g, "game", "run", "won", 0);
        set(g, "game", "run", "level", 8);
        e.fire(Key{"play"});
        e.tick(0.01);
        e.send(Key{"progress"}, sg::Event{save->load_event()});
        frames(e, disk, 4);
        check(num(g, "base", "games", "game_won") == 1 && num(g, "game", "run", "won") == 1 &&
                  num(g, "game", "run", "level") == 1,
              "loaded: the base as saved, the game won again from it and otherwise new");
        check(e.current()->id() == Key{"base"}, "and back at the base, where it was saved");
        check(e.problems().empty(), "nothing wrong in the engine's watch");
    }
#endif

    fs::remove_all(root);
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
