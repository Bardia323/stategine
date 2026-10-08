// Sound as declared state (sg/audio): a curve of rows heard as the formula it
// replaced, ramps in dB along their shapes, the way through a doorway and
// what a shut door keeps of it, a room's reverb from its own shape, the
// laws, and a mixer reading a small world - a hum in the next room heard
// through the door, a one-shot said and heard, and what it renders finite.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sg/audio/Mixer.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/domains/Room.hpp"
#include "sg/domains/Spatial.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

double dist(const sg::Vec3d& a, const sg::Vec3d& b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }

sg::audio::Take tone(double seconds, double hz) {
    auto t = std::make_shared<std::vector<float>>(static_cast<std::size_t>(seconds * 48000));
    for (std::size_t i = 0; i < t->size(); ++i) (*t)[i] = static_cast<float>(0.3 * std::sin(6.283185307 * hz * i / 48000.0));
    return t;
}

const sg::audio::Voice* voice(const sg::audio::Mixer& m, const std::string& id) {
    for (const sg::audio::Voice& v : m.voices())
        if (v.id == id) return &v;
    return nullptr;
}

}  // namespace

int main() {
    using namespace sg;
    using namespace sg::audio;

    // --- the standard curve is the formula it replaced --------------------------------
    {
        double db = 0, cut = 0, send = 0, spread = 0;
        for (double d = 0.0; d <= 100.0; d += 0.05) {
            const Heard h = standard_curve().at(d);
            const double g = 1.0 / std::pow(std::max(0.6, d), 0.95), c = 18000.0 / (1.0 + d * 0.04);
            db = std::max(db, std::fabs(20 * std::log10(h.gain) - 20 * std::log10(g)));
            cut = std::max(cut, std::fabs(h.cut - c) / c);
            send = std::max(send, std::fabs(h.send - (0.35 + 0.3 * std::min(1.0, d / 5.0))));
            spread = std::max(spread, std::fabs((1.0 - h.spread) - std::min(1.0, d / 0.5)));
        }
        check(db < 0.35, "the standard curve is within a third of a dB of 1/d^0.95 (worst " + std::to_string(db) + ")");
        check(cut < 0.03, "and the air's cut within 3%");
        check(send < 1e-3 && spread < 1e-3, "and the reverb's send and the spread close by, exactly");
    }

    // --- a curve from its columns ----------------------------------------------------
    {
        Element e{Key{"small"}, audio::kinds::curve};
        e.params.set("d", std::string("0 1 10")).set("db", std::string("0 -6 -20")).set("send", std::string("0.2 0.3 0.6"));
        e.params.set("cut", std::string("18000 17000 9000")).set("spread", std::string("1 0 0"));
        Curve c;
        check(curve_of(e, c) && c.rows.size() == 3, "a curve's rows are read from its columns");
        check(std::fabs(20 * std::log10(c.at(5.5).gain) - (-13.0)) < 1e-9, "between two rows it is a straight line in dB");
        check(std::fabs(c.at(50).cut - 9000) < 1e-9 && c.at(-1).spread == 1.0, "past either end, the end row");
        e.params.set("d", std::string("0 10 1"));
        std::string why;
        check(!curve_of(e, c, &why) && !why.empty(), "distances that go backwards are refused: " + why);
        e.params.set("d", std::string("0 1 nan"));
        check(!curve_of(e, c, &why), "a number that is not finite is refused: " + why);
    }

    // --- a ramp in dB, along its shape -----------------------------------------------
    {
        Ramp r;
        r.set(-60.0);
        r.toward(0.0, 1.0, Shape::S);
        r.advance(0.5);
        check(std::fabs(r.now() - (-30.0)) < 1e-9, "an S ramp is half way at half time");
        r.toward(0.0, 1.0, Shape::S);
        check(std::fabs(r.now() - (-30.0)) < 1e-9, "asked again for where it is going, it keeps going");
        r.advance(0.25);
        const double a = r.now();
        r.advance(0.0);
        check(r.now() == a, "no time, no change: a paused world's ramp waits with it");
        r.advance(10.0);
        check(r.settled() && r.now() == 0.0, "and it arrives");
        check(std::fabs(ease(Shape::Log, 1.0) - 1.0) < 1e-12 && ease(Shape::Exp, 0.5) < 0.5 && ease(Shape::Log, 0.5) > 0.5,
              "log is quick at first, exp slow");
    }

    // --- a room's reverb, from its own shape -----------------------------------------
    {
        Room room(Key{"hall"}, 5.0, 4.0, 3.0);
        Reverberation hard, soft;
        check(reverberation_of(room, "plaster", "plaster", "plaster", hard), "a room's reverb is worked out from its outline");
        const double t = 0.161 * 60.0 / (-94.0 * std::log(1.0 - 0.03));
        check(std::fabs(hard.volume - 60.0) < 1e-6 && std::fabs(hard.surface - 94.0) < 1e-6 && std::fabs(hard.t60.mid - t) < 1e-6,
              "Eyring's T60 from its volume and surfaces");
        reverberation_of(room, "planks", "fabric", "plaster", soft);
        check(soft.t60.high < hard.t60.high && soft.t60.high < soft.t60.low, "cloth on the walls takes the top off first");
    }

    // --- a small world: two rooms and a door between -------------------------------------
    StateGraph g;
    auto& a = g.add<Spatial3D>(Key{"a"});
    auto& b = g.add<Spatial3D>(Key{"b"});
    a.portal(Key{"door"}, {5, 0, 0}, 1.0, 2.0, 0.0);
    b.portal(Key{"door"}, {0, 0, 0}, 1.0, 2.0, 3.14159265358979);
    glue_doorway(g, Key{"gate"}, Key{"a"}, Key{"door"}, Key{"b"}, Key{"door"});
    Element& hum = b.add_element(Key{"hum"}, Key{"mesh"});
    hum.params.set(audio::keys::sound, std::string("hum")).set(sg::keys::x, -3.0).set(sg::keys::y, 1.0).set(sg::keys::z, 0.0);

    const Vec3d ear{0, 1.6, 0};
    Paths paths;
    paths.build(g, Key{"a"}, ear);
    const Route open = paths.to(Key{"b"}, {-3.0, 1.0, 0.0});
    const double want = dist(ear, sg::position_of(a.element(Key{"door"}))) + dist({-3.0, 1.0, 0.0}, sg::position_of(b.element(Key{"door"})));
    check(open.heard && std::fabs(open.length - want) < 1e-9, "the next room is heard through the door, as far as the way there");
    a.element(Key{"door"}).params.set(Key{"opening"}, 0.0);
    paths.build(g, Key{"a"}, ear);
    const Route shut = paths.to(Key{"b"}, {-3.0, 1.0, 0.0});
    check(shut.heard && shut.bands.high < 0.1 && shut.bands.low > shut.bands.high * 5,
          "through the shut door, a little of the low and almost none of the top");
    a.element(Key{"door"}).params.set(Key{"admits"}, std::string("view light"));
    paths.build(g, Key{"a"}, ear);
    check(!paths.to(Key{"b"}, {-3.0, 1.0, 0.0}).heard, "a doorway that does not let sound through carries none");
    a.element(Key{"door"}).params.set(Key{"admits"}, std::string("view light sound objects")).set(Key{"opening"}, 1.0);

    // --- the laws -----------------------------------------------------------------------
    const auto known = [](const std::string& n) { return n == "hum" || n == "ding" || n.rfind("stream:", 0) == 0; };
    {
        const auto wrong = sound_defects(g, known);
        check(wrong.size() == 2, "two places heard through a seam wear no sound look (" + std::to_string(wrong.size()) + ")");
    }
    auto& quiet = g.add<State>(Key{"a.sound_look"});
    quiet.params().set(audio::keys::reverb_wet, 0.2).set(bus_key(Bus::Ambient), -6.0);
    Element& bed = quiet.add_element(Key{"tone"}, audio::kinds::bed);
    bed.params.set(audio::keys::sound, std::string("hum")).set(audio::keys::gain, 0.5);
    wear_sound(g, Key{"a"}, Key{"a.sound_look"});
    wear_sound(g, Key{"b"}, Key{"a.sound_look"});
    check(sound_defects(g, known).empty(), "every place heard through a seam wears a sound look");
    hum.params.set(audio::keys::sound, std::string("hmm"));
    check(sound_defects(g, known).size() == 1, "a thing that sounds as nothing is named");
    hum.params.set(audio::keys::sound, std::string("hum"));

    // --- the mixer reads it ---------------------------------------------------------------
    Samples samples;
    const Take hum_take = tone(2.0, 110.0), ding_take = tone(0.3, 880.0);
    samples.loop = [&](const std::string& n) { return n == "hum" ? hum_take : nullptr; };
    samples.shot = [&](const std::string& n) { return n == "ding" ? ding_take : nullptr; };
    Mixer mixer(samples);
    const Ear in_a{Key{"a"}, ear, {1, 0, 0}};
    for (int i = 0; i < 20; ++i) mixer.read(g, in_a, 0.05);
    const Voice* v = voice(mixer, "b/hum");
    check(v != nullptr, "the hum in the next room is a voice");
    if (v) {
        const double heard = std::max(v->gl, v->gr), direct = standard_curve().at(dist(ear, {-3, 1, 0})).gain;
        check(heard < direct && std::fabs(v->level) < 0.01, "heard as far off as the way round, at its gain, eased there");
    }
    const Voice* left = voice(mixer, "a#tone:l");
    check(left && std::fabs(left->level - (20 * std::log10(0.5 * 0.7071) - 6.0)) < 0.05,
          "the place's bed is all round the ear, on its bus, lowered as its look says");
    check(std::fabs(mixer.bus_db(Bus::Ambient) + 6.0) < 1e-9, "the ambient bus is the look's");
    check(!mixer.echoes().empty() && mixer.echoes().front().place == Key{"a"} && std::fabs(mixer.echoes().front().wet - 0.2) < 1e-9,
          "the ear's own place answers first, as its look says");

    // A one-shot, said.
    auto& bell = g.add<State>(Key{"bell"});
    bell.says(Key{"bell.ring"});
    mixer.listen(g);
    bell.emit(Event{Key{"bell.ring"}, Params{}.set("sound", std::string("ding")).set("place", std::string("a"))
                                          .set(sg::keys::x, 1.0).set(sg::keys::y, 1.6).set(sg::keys::z, 0.0)});
    bell.dispatch_pending();
    mixer.read(g, in_a, 0.05);
    bool rang = false;
    for (const Voice& s : mixer.voices()) rang = rang || (s.kind == Voice::Kind::Shot && s.take == ding_take);
    check(rang, "what a state says it sounds as is heard, where it says");

    std::vector<float> out(2 * 512);
    bool finite = true, any = false;
    for (int k = 0; k < 8; ++k) {
        mixer.render(out.data(), 512);
        for (float x : out) finite = finite && std::isfinite(x), any = any || std::fabs(x) > 1e-6;
    }
    check(finite && any, "and it all renders, finite");

    // Out of the way: the hum stops being meant, and fades out on the shell's interval.
    hum.params.set(audio::keys::gain, 0.0);
    for (int i = 0; i < 40; ++i) mixer.read(g, in_a, 0.05);
    check(voice(mixer, "b/hum") == nullptr, "turned down, it fades and is gone");

    std::printf("%s\n", failures ? "sound: something is wrong" : "sound: as declared");
    return failures ? 1 : 0;
}
