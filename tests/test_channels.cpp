// What passes through a seam (Channel): a doorway that says nothing lets
// everything through, as it always did; one that says `admits` lets only what
// it names; a door's opening lets light through as far as it is open and
// muffles sound as it shuts; the pieces each channel joins follow the seams,
// made again when what decides them moves; and the laws hold a doorway to
// naming only what there is, a walked seam to admitting things, and the
// pieces to their seams.
#include "sg/core/Laws.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/domains/Spatial.hpp"

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

namespace {
using sg::Channel;
using sg::Key;

sg::Spatial3D& room(sg::StateGraph& g, const char* name) {
    auto& r = g.add<sg::Spatial3D>(Key{name});
    r.camera();
    return r;
}

bool has(const std::vector<sg::Violation>& vs, const std::string& word) {
    for (const auto& v : vs)
        if (v.detail.find(word) != std::string::npos) return true;
    return false;
}
}  // namespace

int main() {
    sg::StateGraph g;
    auto& a = room(g, "a");
    auto& b = room(g, "b");
    auto& c = room(g, "c");
    a.portal(Key{"to_b"}, {2, 1, 0}, 1, 2, 0.0);
    b.portal(Key{"to_a"}, {-2, 1, 0}, 1, 2, 3.14159265358979);
    b.portal(Key{"to_c"}, {2, 1, 0}, 1, 2, 0.0);
    c.portal(Key{"to_b"}, {-2, 1, 0}, 1, 2, 3.14159265358979);
    g.set_initial(a.id());
    const sg::Seam& ab = sg::glue_doorway(g, Key{"ab"}, a.id(), Key{"to_b"}, b.id(), Key{"to_a"});
    const sg::Seam& bc = sg::glue_doorway(g, Key{"bc"}, b.id(), Key{"to_c"}, c.id(), Key{"to_b"});

    for (int i = 0; i < sg::kChannels; ++i) {
        const auto ch = static_cast<Channel>(i);
        check(g.admits(ab, ch) && g.passes(ab, ch) == 1.0, std::string("a doorway that says nothing admits ") + sg::channel_name(ch));
        check(g.connected(ch, a.id(), c.id()), std::string("and the ") + sg::channel_name(ch) + " reaches through two of them");
    }
    check(g.fade(ab) == 1.0, "a crossfade across it takes a second unless it says");
    check(sg::laws::channels(g).empty(), "the pieces agree with their seams");

    // A window: seen and lit through, never walked or heard.
    b.element(Key{"to_c"}).params.set(Key{"admits"}, std::string("view light"));
    check(g.admits(bc, Channel::View) && g.admits(bc, Channel::Light), "a doorway admits what it names");
    check(!g.admits(bc, Channel::Sound) && !g.admits(bc, Channel::Objects), "and nothing it does not");
    check(g.connected(Channel::Light, a.id(), c.id()) && !g.connected(Channel::Sound, a.id(), c.id()),
          "the pieces follow at once: light reaches c, sound does not");
    check(g.connected(Channel::Sound, a.id(), b.id()), "and what the other doorway admits it still does");
    check(sg::laws::channels(g).empty(), "made again, the pieces still agree with their seams");

    // A door: light as far as it is open, sound muffled as it shuts.
    a.element(Key{"to_b"}).params.set(Key{"opening"}, 0.0);
    check(g.passes(ab, Channel::Light) == 0.0 && !g.connected(Channel::Light, a.id(), b.id()), "shut, a door lets no light through");
    check(std::fabs(g.passes(ab, Channel::Sound) - 0.25) < 1e-12 && g.connected(Channel::Sound, a.id(), b.id()),
          "and sound only muffled");
    a.element(Key{"to_b"}).params.set(Key{"opening"}, 0.5);
    check(std::fabs(g.passes(ab, Channel::Light) - 0.5) < 1e-12 && g.connected(Channel::Light, a.id(), b.id()),
          "ajar, light as far as it is open");
    check(std::fabs(g.passes(ab, Channel::Sound) - 0.625) < 1e-12, "and sound between muffled and clear");
    a.element(Key{"to_b"}).params.set(Key{"fade"}, 2.5);
    check(g.fade(ab) == 2.5, "a seam says how long a crossfade across it takes");

    // What a picture is: seen in, nothing else - unless walked.
    c.element(Key{"to_b"}).params.set(Key{"feed"}, 1.0);
    b.element(Key{"to_c"}).params.erase(Key{"admits"});
    check(g.admits(bc, Channel::View) && !g.admits(bc, Channel::Light), "a screen that says nothing admits only view");
    c.element(Key{"to_b"}).params.erase(Key{"feed"});
    c.element(Key{"to_b"}).params.set(Key{"ball_out"}, 1.0);
    check(g.admits(bc, Channel::Objects) && !g.admits(bc, Channel::Sound), "the sky of a world in a glass admits all but sound");
    c.element(Key{"to_b"}).params.erase(Key{"ball_out"});

    // The laws.
    b.element(Key{"to_c"}).params.set(Key{"admits"}, std::string("view smell"));
    check(has(sg::laws::channels(g), "smell"), "a doorway naming what no seam lets through is a defect");
    b.element(Key{"to_c"}).params.set(Key{"admits"}, std::string("view light sound"));
    b.element(Key{"to_c"}).params.set(Key{"walk"}, 1.0);
    c.element(Key{"to_b"}).params.set(Key{"walk"}, 1.0);
    check(has(sg::laws::overlaps(g), "admits no things"), "a walked seam that admits no things is a defect");
    b.element(Key{"to_c"}).params.set(Key{"admits"}, std::string("view light sound objects"));
    check(!has(sg::laws::overlaps(g), "admits no things"), "and admitting them, it is not");
    check(sg::laws::channels(g).empty(), "and every piece agrees with its seams to the end");

    // A seam glued again under its name to another room (a corridor lent):
    // which seams pass is as it was, what they join is not.
    {
        sg::StateGraph h;
        auto& p = room(h, "p");
        auto& q = room(h, "q");
        auto& r = room(h, "r");
        p.portal(Key{"door"}, {2, 1, 0}, 1, 2, 0.0);
        q.portal(Key{"door"}, {-2, 1, 0}, 1, 2, 3.14159265358979);
        r.portal(Key{"door"}, {-2, 1, 0}, 1, 2, 3.14159265358979);
        h.set_initial(p.id());
        sg::glue_doorway(h, Key{"lent"}, p.id(), Key{"door"}, q.id(), Key{"door"});
        check(h.connected(Channel::View, p.id(), q.id()) && !h.connected(Channel::View, p.id(), r.id()), "glued to q, p sees q");
        sg::glue_doorway(h, Key{"lent"}, p.id(), Key{"door"}, r.id(), Key{"door"});
        check(h.connected(Channel::View, p.id(), r.id()) && !h.connected(Channel::View, p.id(), q.id()),
              "glued again to r under the same name, p sees r and no longer q");
        check(sg::laws::channels(h).empty(), "and the pieces agree with the seams as they are now");
    }

    std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
