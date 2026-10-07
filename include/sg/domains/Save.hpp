// Stategine - Save: a save game, as a state.
//
// A save keeps some states, not all. Each element of it names one, by its id:
//
//   kept       written out when the game is saved (sg::to_text), and put back
//              as it was written when it is loaded
//   replayed   put back to its start when it is loaded (StateGraph::restore_default):
//              played again, whatever was done in it since
//
// and every other state is the save's only where the graph says it follows
// from one it keeps. Saved, a game is what it keeps - nothing else is written.
// Loaded, it is the least world that agrees with that: every state the save
// reaches starts again, and is given what the declared interfaces say of it
// from the states kept. That is the left Kan extension along the inclusion of
// the kept states into the graph,
//
//     load = Lan_K      save = K*      (K : kept -> graph)
//
// and Lan_K -| K*: saving what was loaded gives what was saved, word for word
// (the unit is the identity - K is one to one), while loading what was saved
// gives the world less what it did not keep (the counit: what is played
// again). Pointwise, a state X the save reaches is its start with what each
// link into it from a nearer state carries - the colimit of the links into X
// over its start - and where two such links say different things of one
// parameter there is no colimit: the save says so (`why`).
//
// The links are the interfaces the graph already declares, and nothing more:
//   - a functor carries its source into its target (its transport, run);
//   - and back, where what it declares proves it can be undone - a whole copy,
//     a renaming (kan::inverse): a game that writes `won` into its base is,
//     on loading the base, a game that was won;
//   - a guest lives in its host's portal: a host the save reaches brings
//     what lives in it (and what lives in that), as restore_default(guests).
// A transition's functor and a seam's travel and glue are movement, not what
// a state is: they are not links. A link that says nothing of how its data
// came (an opaque transport) carries forward, but not back: what the save
// cannot bring back that way is named (`holes`) - keep that state too, or
// declare what the link does. And a link from a state played again into a
// kept one goes on carrying once the game runs: what the hole names is what
// the next save will differ by.
//
// What a link carries is carried outward from the kept states, nearest first,
// each state once: a state is given what its nearer neighbours say of it,
// never what a farther one does. So rings of links end, a world inside a
// world inside a world is reached as deep as it goes, and loading is the same
// however the links go round. The kept states are never written by a link:
// they are what was saved.
//
// Saving and loading change the world from outside any one state, so each is
// an edit (StateGraph::edit) the save asks for, applied at the start of the
// next frame - every state saved at one instant, every state loaded before
// anything moves, all or nothing. Neither touches a file: the text of a save
// leaves the world in what the save says, and comes back in through a port,
// as everything outside does (a device: `SaveFiles`, below).
//
//   <save>.save {slot}      heard (a port, a functor carrying a checkpoint)
//       -> says <save>.restrict  -> edit `restrict.kept` -> <save>.restrict.done
//       -> says <save>.saved {slot, text, at, kept, ok, why}
//   <save>.load {slot}
//       -> says <save>.fetch {slot}                 for the device
//   <save>.restore {slot, text}                    from the device, at its port
//       -> says <save>.extend    -> edit `extend.kept`   -> <save>.extend.done
//       -> says <save>.loaded {slot, ok, at, kept, derived, replayed, holes, why}
//       -> and <save>.at.<state>: where the game was saved, for a declared
//          transition to take whoever plays back there (say it to use it)
//
// What a kept element names in its `without` (state params or elements, by
// name, separated by spaces) is not written - who is looking, what time it is
// there - and on loading is played again from the state's start.
//
// In the notation:
//
//   state progress : save {
//       element base : kept
//       element player : kept { without = "camera" }
//       element arcade : replayed
//   }
//   port progress.save
//   port progress.load
//   port progress.restore
//   edit progress on progress.restrict native restrict.kept
//   edit progress on progress.extend native extend.kept
//
// with the natives registered by the host (`Save::restrict_kept`,
// `Save::extend_kept`), and a SaveFiles watching it.
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "sg/core/Assets.hpp"
#include "sg/core/State.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/core/Store.hpp"

namespace sg {

class Engine;

class Save : public State {
public:
    explicit Save(Key id);

    Key kind() const override { return Key{"save"}; }

    static Key kept_kind() { return Key{"kept"}; }
    static Key replayed_kind() { return Key{"replayed"}; }
    // The element its arrows are on, whose `name` is the slot used when none is asked.
    static Key slot_element() { return Key{"slot"}; }

    // Heard: asked for by whoever tells it, and the device's answer.
    Key save_event() const { return Key{id().str() + ".save"}; }
    Key load_event() const { return Key{id().str() + ".load"}; }
    Key restore_event() const { return Key{id().str() + ".restore"}; }
    // Said: for its edits, for the device, and when done.
    Key restrict_event() const { return Key{id().str() + ".restrict"}; }
    Key extend_event() const { return Key{id().str() + ".extend"}; }
    Key fetch_event() const { return Key{id().str() + ".fetch"}; }
    Key saved_event() const { return Key{id().str() + ".saved"}; }
    Key loaded_event() const { return Key{id().str() + ".loaded"}; }
    Key at_event(Key state) const { return Key{id().str() + ".at." + state.str()}; }

    // Keep a state, or play it again on loading: an element, as the notation
    // declares one.
    Save& keep(Key state, const std::string& without = {});
    Save& replay(Key state);

    // --- the extension ------------------------------------------------------------
    // A way one state's data reaches another: a functor run forward, or undone
    // back (only its objects that can be), or a guest living in its host.
    struct Link {
        enum class Way { Forward, Back, Lives };
        Way way = Way::Forward;
        Key functor;  // empty for Lives
        Key from, to;
        std::vector<std::pair<Key, Key>> objects;  // {element of `from`, element of `to`} it carries
        std::vector<std::string> lost;             // Back: the objects it cannot undo, and why
    };
    // What a load does, worked out and not done: which states it puts back as
    // saved, which it brings to their start and gives what follows (and by
    // which links, in the order they are carried), which it only plays again,
    // and what it cannot bring back.
    struct Extension {
        std::vector<Key> kept;
        struct Reached {
            Key state;
            int depth = 0;         // links from the nearest kept state
            std::vector<Link> by;  // the links into it from the nearer ones
        };
        std::vector<Reached> derived;  // nearest first
        std::vector<Key> replayed;
        std::vector<std::string> holes;
    };
    // The graph's links, as the save reads them (those to or from the save
    // excepted), in the graph's order.
    static std::vector<Link> links(const StateGraph& g, Key save);
    Extension extension(const StateGraph& g) const;

    // --- the edits ----------------------------------------------------------------
    // K*: the kept states' texts, as one (`text` in the reply).
    static Params restrict_kept(StateGraph& g, const Event& asked);
    // Lan_K: the world from a save's text (`text` in what was asked).
    static Params extend_kept(StateGraph& g, const Event& asked);

    // The text of one save: the kept states' texts, and where the game was.
    struct Text {
        Key at;
        std::vector<std::pair<Key, std::string>> states;  // in the order written
    };
    static std::string write_text(const Text& t);
    static bool read_text(const std::string& src, Text& out, std::string* why = nullptr);

private:
    std::string slot_of(const Event& e) const;
};

// The device that keeps saves on disk, outside the world: it watches what a
// save says - writes a save's text to its file when it is saved, and notes
// what is asked to be loaded - and, when the program pumps it, reads what
// was asked for and sends it to the save through its port (`<save>.restore`).
// A listener only observes, so the reading waits for the pump: call it once
// a frame, from the program's loop. Each save's files are its own
// (sg::Assets): `<root>/<save>/<slot>.save`.
class SaveFiles {
public:
    explicit SaveFiles(Assets assets) : assets_(std::move(assets)) {}

    void watch(Save& save);
    void pump(Engine& engine);

    // The file a slot of a save is kept in (throws for a slot that is not
    // one plain name).
    std::filesystem::path file(Key save, const std::string& slot) const;

private:
    Assets assets_;
    TextStore store_;
    std::vector<std::pair<Key, std::string>> asked_;  // {save, slot}, to read at the next pump
};

}  // namespace sg
