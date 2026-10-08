#include "sg/audio/Mixer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>

#include "sg/core/Engine.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/domains/Light.hpp"
#include "sg/domains/Look.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg::audio {

namespace {

constexpr double kTau = 6.28318530717959;
constexpr int kDelay = 64;           // the longest a sound can reach one ear before the other, in samples
constexpr double kHead = 0.00066;    // seconds between the ears, a sound from one side
constexpr std::size_t kEchoes = 3;   // reverbs at once: the ear's place's and the nearest others'
constexpr double kSteal = 0.010;     // seconds a voice put out of room takes to go
constexpr double kLow = 250.0, kHigh = 4000.0;  // where the three bands part

double length(const Vec3d& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

std::string text_or(const Params& p, Key k, const char* fallback) {
    const std::string* t = p.text(k);
    return t && !t->empty() ? *t : std::string(fallback);
}

// How a sound `rel` from the ear (in the ear's frame) is heard, as its curve
// says at `dist`: louder in the nearer ear and a little sooner there, duller
// from behind (the ear's own shadow), from both sides when it is all round.
void place_in_ears(Voice& v, const Vec3d& rel, double dist, const Vec3d& facing, const Heard& c, int rate) {
    double rx = -facing.z, rz = facing.x;
    const double rl = std::hypot(rx, rz);
    if (rl > 1e-6) rx /= rl, rz /= rl;
    double pan = 0.0, front = 1.0;
    const double r = length(rel);
    if (r > 1e-4) {
        pan = std::clamp((rel.x * rx + rel.z * rz) / r, -1.0, 1.0);
        const double fl = length(facing);
        front = fl > 1e-6 ? (rel.x * facing.x + rel.y * facing.y + rel.z * facing.z) / (r * fl) : 1.0;
    }
    (void)dist;
    pan *= 1.0 - std::clamp(c.spread, 0.0, 1.0);
    double cut = c.cut;
    if (front < 0) cut *= 1.0 + 0.35 * front;
    v.gl = c.gain * std::sqrt(0.5 * (1.0 - 0.85 * pan));
    v.gr = c.gain * std::sqrt(0.5 * (1.0 + 0.85 * pan));
    v.dl = pan > 0 ? pan * kHead * rate : 0.0;
    v.dr = pan < 0 ? -pan * kHead * rate : 0.0;
    v.cut = cut;
    v.send = c.send;
}

// What the program's day says of a sound heard by day or by night: the
// place's own `hour`, through the engine's one sky.
double by_hour(const State& place, const Params& p) {
    const std::string* w = p.text(keys::when);
    if (!w || w->empty()) return 1.0;
    const double day = daylight(place.params().num(Key{"hour"}, 12.0)).day;
    if (*w == "night") return 1.0 - day;
    if (*w == "day") return day;
    return 1.0;
}

// What a state said that sounds.
struct Said {
    std::string name;
    Key state;  // who said it
    Key place;  // where, if it says
    Key at;     // at which thing of it, if it says
    Vec3d pos;
    bool placed = false;
    double gain = 1.0, pitch = 1.0, priority = 1.0;
    std::string bus, curve;
    std::string otherwise;  // what it is, where its place calls it nothing
};

// Where listeners leave what they hear. Shared, so a listener left on a
// state after the mixer has gone hears nothing.
struct Hearer {
    std::vector<Said> said;
};

// A voice's presentation, from one read to the next.
struct Tracked {
    Ramp ramp;
    double clock = -1.0;  // its state's own time at the last read
    Voice last;           // as it was last meant, to fade from when it goes
    double fade = 0.12;   // how long it takes to go
    Bus bus = Bus::Sfx;
    bool fresh = true;
    bool seen = false;
};

struct Shot {
    Voice v;
    Key place, curve;
    Vec3d at;
    Bus bus = Bus::Sfx;
    double gain_db = 0.0;
    double left = 0.0;  // seconds of it still to come
};

struct StreamAt {
    Key place;
    Vec3d at;
    double gain = 0.0, width = 0.6;
    Bus bus = Bus::Music;
};

// A place's reverb as its look has been faded to (the shell's interval).
struct Faded {
    double decay = 0.7, damp = 0.5, wet = 0.3;
    bool fresh = true;
};

struct Frame {
    std::vector<Voice> voices;
    std::vector<Echo> echoes;
    double master = 0.9;
    uint64_t serial = 0;
};

// --- on the sound thread -----------------------------------------------------------

// Places one voice in the ears: its delays, the air's filter, its three
// bands and its gains, each moved smoothly across the block so nothing clicks
// as the head turns or a door swings.
struct Spatial {
    double gl = 0, gr = 0, dl = 0, dr = 0, cut = 20000, send = 0;
    Bands bands;
    bool fresh = true;
    std::array<float, kDelay> ring_l{}, ring_r{};
    int w = 0;
    double lp_l = 0, lp_r = 0;    // the air
    double lo_l = 0, lo_r = 0;    // below the low split
    double mid_l = 0, mid_r = 0;  // below the high split

    void run(const float* in_l, const float* in_r, int n, const Voice& to, double rate, const float* level, float* out,
             float* verb) {
        if (fresh) {
            gl = to.gl, gr = to.gr, dl = to.dl, dr = to.dr, cut = to.cut, send = to.send, bands = to.bands;
            fresh = false;
        }
        const double a0 = 1.0 - std::exp(-kTau * cut / rate), a1 = 1.0 - std::exp(-kTau * to.cut / rate);
        const double alo = 1.0 - std::exp(-kTau * kLow / rate), ahi = 1.0 - std::exp(-kTau * kHigh / rate);
        // A stereo source: each channel leans to its own side, less so far off.
        const double wl = std::clamp(0.5 + 0.5 * to.width, 0.5, 1.0);
        const auto tap = [](const std::array<float, kDelay>& ring, double at) {
            const int i0 = static_cast<int>(std::floor(at));
            const double f = at - i0;
            const float x0 = ring[static_cast<std::size_t>((i0 % kDelay + kDelay) % kDelay)];
            const float x1 = ring[static_cast<std::size_t>(((i0 + 1) % kDelay + kDelay) % kDelay)];
            return x0 + (x1 - x0) * f;
        };
        for (int i = 0; i < n; ++i) {
            const double u = n > 1 ? static_cast<double>(i) / (n - 1) : 1.0;
            const double g_l = gl + (to.gl - gl) * u, g_r = gr + (to.gr - gr) * u;
            const double d_l = dl + (to.dl - dl) * u, d_r = dr + (to.dr - dr) * u;
            const double a = a0 + (a1 - a0) * u, s = send + (to.send - send) * u;
            const double bl = bands.low + (to.bands.low - bands.low) * u;
            const double bm = bands.mid + (to.bands.mid - bands.mid) * u;
            const double bh = bands.high + (to.bands.high - bands.high) * u;
            const double l = in_l[i], r = in_r[i];
            ring_l[static_cast<std::size_t>(w)] = static_cast<float>(l * wl + r * (1 - wl));
            ring_r[static_cast<std::size_t>(w)] = static_cast<float>(r * wl + l * (1 - wl));
            lp_l += a * (tap(ring_l, w - d_l) - lp_l);
            lp_r += a * (tap(ring_r, w - d_r) - lp_r);
            lo_l += alo * (lp_l - lo_l);
            lo_r += alo * (lp_r - lo_r);
            mid_l += ahi * (lp_l - mid_l);
            mid_r += ahi * (lp_r - mid_r);
            // All three at 1: exactly what came in.
            const double yl = bl * lo_l + bm * (mid_l - lo_l) + bh * (lp_l - mid_l);
            const double yr = bl * lo_r + bm * (mid_r - lo_r) + bh * (lp_r - mid_r);
            const double k = level[i];
            out[2 * i] += static_cast<float>(yl * g_l * k);
            out[2 * i + 1] += static_cast<float>(yr * g_r * k);
            if (verb) verb[i] += static_cast<float>((yl * g_l + yr * g_r) * s * k);
            w = (w + 1) % kDelay;
        }
        gl = to.gl, gr = to.gr, dl = to.dl, dr = to.dr, cut = to.cut, send = to.send, bands = to.bands;
    }
};

// Jezar's Freeverb, much as he wrote it: eight combs and four all-passes an
// ear, the right ear's a little longer; `size` stretches them all. Its combs
// feed back as much as makes it fall 60 dB in `decay` seconds.
class Reverb {
public:
    Reverb(double size, double rate) {
        static const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int passes[4] = {556, 441, 341, 225};
        const double k = std::clamp(size, 0.3, 2.5);
        double sum = 0;
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < 8; ++i) {
                const int len = std::max(16, static_cast<int>(combs[i] * k)) + c * 23;
                comb_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)].buf.assign(static_cast<std::size_t>(len), 0.0f);
                sum += len;
            }
            for (int i = 0; i < 4; ++i)
                pass_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)].buf.assign(
                    static_cast<std::size_t>(std::max(8, static_cast<int>(passes[i] * k)) + c * 23), 0.0f);
        }
        loop_ = sum / 16.0 / rate;
    }
    void set(double decay, double damp) {
        feedback_ = std::clamp(std::pow(10.0, -3.0 * loop_ / std::max(0.05, decay)), 0.0, 0.98);
        damp_ = std::clamp(damp, 0.0, 0.95);
    }
    // Adds `wet` of what `in` sets ringing to `out`, interleaved.
    void run(const float* in, float* out, int n, double wet) {
        for (int i = 0; i < n; ++i) {
            const float x = in[i] * 0.015f;
            for (std::size_t c = 0; c < 2; ++c) {
                float s = 0;
                for (Comb& k : comb_[c]) {
                    float& y = k.buf[static_cast<std::size_t>(k.i)];
                    k.store = static_cast<float>(y * (1 - damp_) + k.store * damp_);
                    const float o = y;
                    y = static_cast<float>(x + k.store * feedback_);
                    k.i = (k.i + 1) % static_cast<int>(k.buf.size());
                    s += o;
                }
                for (Pass& p : pass_[c]) {
                    float& y = p.buf[static_cast<std::size_t>(p.i)];
                    const float o = y - s;
                    y = s + y * 0.5f;
                    p.i = (p.i + 1) % static_cast<int>(p.buf.size());
                    s = o;
                }
                out[2 * i + static_cast<int>(c)] += static_cast<float>(s * wet);
            }
        }
    }

private:
    struct Comb {
        std::vector<float> buf;
        int i = 0;
        float store = 0;
    };
    struct Pass {
        std::vector<float> buf;
        int i = 0;
    };
    std::array<std::array<Comb, 8>, 2> comb_;
    std::array<std::array<Pass, 4>, 2> pass_;
    double feedback_ = 0.84, damp_ = 0.4, loop_ = 0.03;
};

struct Live {
    Voice v;
    Spatial sp;
    double at = 0.0;           // where in its take
    double level = kSilent;    // dB, where the last block left it
    double steal = 1.0;        // 1 heard, 0 put out of room
    bool present = true;       // meant by the last frame
    bool keep = true;          // among the voices there is room for
    uint64_t synced = 0;       // the frame its place in its take was last held to its clock
};

struct EchoLive {
    std::unique_ptr<Reverb> verb;
    double size = 1.0;
    double level = kSilent;
    double lo[2] = {0, 0}, mid[2] = {0, 0};
    Echo e;
    bool present = false;
    double idle = 0.0;  // seconds since anything was sent to it
};

}  // namespace

struct Mixer::Impl {
    Samples samples;
    std::size_t cap = 32;

    // --- the game's thread --------------------------------------------------------
    std::shared_ptr<Hearer> hearer = std::make_shared<Hearer>();
    std::vector<Said> told;
    std::set<std::pair<const void*, const void*>> listening;  // (state, event) heard already
    uint64_t listened = ~uint64_t{0};
    uint64_t indexed = ~uint64_t{0};
    struct Source {
        Key state, element;
    };
    std::vector<Source> sources;
    std::vector<Key> slotted;                      // states with a sound slot
    std::unordered_map<Key, Key> host_of, portal_of;  // a guest's host, and the portal it lives in
    std::unordered_map<Key, std::pair<Key, Key>> line_of;  // a driven state's clock and line
    Curves curves;
    Paths paths;
    std::unordered_map<std::string, Tracked> tracked;
    std::vector<Shot> shots;
    uint64_t serial = 0;
    std::vector<StreamAt> streams;
    std::unordered_map<Key, Faded> faded;
    std::unordered_map<Key, Reverberation> shaped;
    std::unordered_map<Key, uint64_t> shaped_at;
    struct Occluded {
        Vec3d from, to;
        Bands bands;
        uint64_t structure = 0;
        bool fresh = true;
    };
    std::unordered_map<std::string, Occluded> occluded;
    double bus_now[kBuses] = {0, 0, 0, 0, 0};
    double attend_now[kBuses] = {0, 0, 0, 0, 0};
    bool bus_fresh = true;
    Key last_place;
    double crossing = 1.0, crossing_left = 0.0;
    std::vector<Voice> voices;
    std::vector<Echo> echoes;
    double master = 0.9;
    uint64_t frames = 0;

    // --- between the two ----------------------------------------------------------------
    std::mutex m;
    std::shared_ptr<const Frame> published;
    std::mutex streams_m;
    std::vector<Fill> fills;

    // --- the sound thread ---------------------------------------------------------------
    std::unordered_map<std::string, Live> live;
    std::unordered_map<Key, EchoLive> echo_live;
    uint64_t started = 0;
    std::vector<float> mono, left, right, scratch, levels;
    std::vector<std::vector<float>> wet;
    std::vector<Live*> order;

    Impl(Samples s, int voices_cap) : samples(std::move(s)), cap(static_cast<std::size_t>(std::max(1, voices_cap))) {}

    double bus_db(Bus b) const { return bus_now[static_cast<int>(b)] + attend_now[static_cast<int>(b)]; }

    // --- the world, indexed when the graph changes ------------------------------------
    void reindex(const StateGraph& g) {
        sources.clear();
        slotted.clear();
        host_of.clear();
        portal_of.clear();
        line_of.clear();
        for (Key id : g.ids()) {
            const State* s = g.find(id);
            if (!s) continue;
            if (s->find(sound_slot_id())) slotted.push_back(id);
            for (const Element& e : s->elements()) {
                if (e.kind == kinds::bed || e.kind == kinds::curve || e.kind == kinds::slot) continue;
                if (e.params.text(keys::sound)) sources.push_back(Source{id, e.id});
            }
        }
        for (const Embedding& e : g.embeddings())
            if (!host_of.count(e.guest)) {
                host_of[e.guest] = e.host;
                portal_of[e.guest] = e.portal;
            }
        for (const Drive& d : g.drives()) line_of[d.state] = {d.clock, d.line.empty() ? d.state : d.line};
    }

    // The place a state sounds in - itself, if it is a place, else the place
    // it lives in - and how its frame sits in that place's.
    bool place_of(const StateGraph& g, Key state, Key& place, Pose& frame) const {
        Key cur = state;
        frame = Pose{};
        for (int i = 0; i < 8; ++i) {
            const State* s = g.find(cur);
            if (!s) return false;
            if (dynamic_cast<const SpatialState*>(s)) {
                place = cur;
                return true;
            }
            auto it = host_of.find(cur);
            if (it == host_of.end()) return false;
            const State* host = g.find(it->second);
            if (!host) return false;
            if (const Element* portal = host->find(portal_of.at(cur))) frame = compose_pose(world_pose(*host, *portal), frame);
            cur = it->second;
        }
        return false;
    }

    // A state's own time: its line on its clock, if it is driven.
    bool clock_of(const StateGraph& g, Key state, double& t) const {
        auto it = line_of.find(state);
        if (it == line_of.end()) return false;
        const auto* clock = dynamic_cast<const Temporal*>(g.find(it->second.first));
        if (!clock || !clock->has_timeline(it->second.second)) return false;
        t = clock->time(it->second.second);
        return true;
    }

    const State* look_of(const StateGraph& g, const State& place) const {
        const Key k = active_sound_look(place);
        return k.empty() ? nullptr : g.find(k);
    }

    // --- how the place one is in sounds --------------------------------------------------
    void read_buses(const StateGraph& g, const Ear& ear, double dt) {
        double want[kBuses] = {0, 0, 0, 0, 0};
        double fade = 0.5;
        if (const State* place = g.find(ear.place))
            if (const State* look = look_of(g, *place)) {
                for (int b = 0; b < kBuses; ++b) want[b] = look->params().num(bus_key(static_cast<Bus>(b)), 0.0);
                fade = look->params().num(keys::fade, 0.5);
            }
        if (bus_fresh) {
            std::copy(want, want + kBuses, bus_now);
            bus_fresh = false;
        } else {
            const double k = fade > 0 ? std::min(1.0, dt / fade) : 1.0;
            for (int b = 0; b < kBuses; ++b) bus_now[b] += (want[b] - bus_now[b]) * k;
        }
        // What is attended: leaning in to it, the rest goes down by as much as
        // the lean dims the room - read from the visual look that dims it.
        std::fill(attend_now, attend_now + kBuses, 0.0);
        for (Key id : slotted) {
            const State* host = g.find(id);
            const State* look = host ? look_of(g, *host) : nullptr;
            if (!look) continue;
            const std::string* vis = look->params().text(keys::attend_look);
            const std::string* uniform = look->params().text(keys::attend_uniform);
            if (!vis || vis->empty() || !uniform || active_look(*host) != Key{*vis}) continue;
            const State* seen = g.find(Key{*vis});
            const Element* scene = seen ? seen->find(passes::scene) : nullptr;
            const double full = look->params().num(keys::attend_full, 1.0);
            if (!scene || full <= 0) continue;
            const double lean = std::clamp(scene->params.num(Key{*uniform}, 0.0) / full, 0.0, 1.0);
            for (int b = 0; b < kBuses; ++b) attend_now[b] += lean * look->params().num(attend_bus_key(static_cast<Bus>(b)), 0.0);
        }
    }

    // --- the reverbs: the ear's place's, and the nearest others' -------------------------
    bool shape_of_room(const State& place, const State* look, Reverberation& r) {
        const uint64_t at = place.structure() ^ (place.params().stamp() * 1315423911u) ^ (look ? look->params().stamp() : 0);
        auto it = shaped_at.find(place.id());
        if (it != shaped_at.end() && it->second == at) {
            auto jt = shaped.find(place.id());
            if (jt == shaped.end()) return false;
            r = jt->second;
            return true;
        }
        shaped_at[place.id()] = at;
        const Params empty;
        const Params& p = look ? look->params() : empty;
        const bool sky = place.params().num(Key{"sky"}, 0.0) > 0.5;
        Reverberation made;
        if (!reverberation_of(place, text_or(p, Key{"surface.floor"}, "planks"), text_or(p, Key{"surface.walls"}, "plaster"),
                              sky ? std::string("open") : text_or(p, Key{"surface.ceiling"}, "plaster"), made)) {
            shaped.erase(place.id());
            return false;
        }
        shaped[place.id()] = made;
        r = made;
        return true;
    }

    void read_echoes(const StateGraph& g, const Ear& ear, double dt) {
        echoes.clear();
        std::vector<std::pair<double, Key>> near{{0.0, ear.place}};
        for (const auto& kv : paths.reached())
            if (kv.first != ear.place) near.push_back({kv.second, kv.first});
        std::sort(near.begin(), near.end(), [](const std::pair<double, Key>& a, const std::pair<double, Key>& b) {
            return a.first != b.first ? a.first < b.first : a.second.str() < b.second.str();
        });
        if (near.size() > kEchoes) near.resize(kEchoes);
        for (const auto& kv : near) {
            const State* s = g.find(kv.second);
            if (!s) continue;
            const State* look = look_of(g, *s);
            const Params empty;
            const Params& p = look ? look->params() : empty;
            Reverberation r;
            const bool room = shape_of_room(*s, look, r);
            // What the look says; else the room's own shape and materials.
            const double decay = p.has(keys::reverb_decay) ? p.num(keys::reverb_decay, 0.7) : room ? r.t60.mid : 0.7;
            const double damp = p.has(keys::reverb_damp) ? p.num(keys::reverb_damp, 0.5)
                                : room ? std::clamp(1.0 - r.t60.high / std::max(1e-3, r.t60.mid), 0.0, 0.9)
                                       : 0.5;
            const double wet = p.num(keys::reverb_wet, 0.3);
            const double fade = p.num(keys::fade, 0.5);
            Faded& f = faded[kv.second];
            if (f.fresh) {
                f.decay = decay, f.damp = damp, f.wet = wet;
                f.fresh = false;
            } else {
                const double k = fade > 0 ? std::min(1.0, dt / fade) : 1.0;
                f.decay += (decay - f.decay) * k;
                f.damp += (damp - f.damp) * k;
                f.wet += (wet - f.wet) * k;
            }
            Echo e;
            e.place = kv.second;
            e.size = p.num(keys::reverb_size, 1.0);
            e.decay = f.decay, e.damp = f.damp, e.wet = f.wet;
            if (kv.second != ear.place) {
                // Another place's answer comes the way its sounds come.
                const Route route = paths.to(kv.second);
                if (!route.heard) continue;
                e.level = gain_to_db(curves.of(Key{"ambient"}).at(route.length).gain);
                e.bands = route.bands * air(route.length);
            }
            echoes.push_back(e);
        }
    }

    // Which reverb a sound of `place` sends to: its own place's, or, past
    // those there is room for, the furthest one kept.
    int echo_for(Key place) const {
        for (std::size_t i = 0; i < echoes.size(); ++i)
            if (echoes[i].place == place) return static_cast<int>(i);
        return echoes.empty() ? -1 : static_cast<int>(echoes.size()) - 1;
    }

    Bands occlude(const StateGraph& g, Key place, const Vec3d& from, const Vec3d& to, const std::string& key) {
        const State* s = g.find(place);
        if (!s) return {};
        Occluded& o = occluded[key];
        // Cast again only when the sound, the ear or the walls have moved.
        if (o.fresh || o.structure != s->structure() || length(o.from - from) > 0.25 || length(o.to - to) > 0.25) {
            o.bands = occlusion(*s, from, to);
            o.from = from, o.to = to, o.structure = s->structure(), o.fresh = false;
        }
        return o.bands;
    }

    // How a point `at` of `place` is heard; false if no way reaches the ear.
    bool hear(const StateGraph& g, const Ear& ear, Key place, const Vec3d& at, const Curve& curve, Voice& v,
              const std::string& occluded_as, double* dist_out = nullptr) {
        Vec3d rel;
        double dist = 0.0;
        Bands bands;
        if (place == ear.place) {
            rel = at - ear.pos;
            dist = length(rel);
            bands = air(dist);
            if (!occluded_as.empty()) bands = bands * occlude(g, place, at, ear.pos, occluded_as);
        } else {
            const Route r = paths.to(place, at);
            if (!r.heard) return false;
            dist = r.length;
            rel = r.toward * dist;
            bands = r.bands * air(dist);
        }
        place_in_ears(v, rel, dist, ear.facing, curve.at(dist), samples.rate);
        v.bands = bands;
        v.echo = echo_for(place);
        if (dist_out) *dist_out = dist;
        return true;
    }

    // `v`, its hearing worked out, at `gain`: eased there in dB on `clock`
    // (its state's own time; below 0, the shell's `dt`), on its bus.
    void ease(Voice& v, double gain, const Params* p, double clock, double dt, double secs, Bus bus) {
        Tracked& t = tracked[v.id];
        Shape shape = Shape::S;
        if (p) {
            secs = p->num(keys::ramp, secs);
            if (const std::string* s = p->text(keys::shape)) shape = shape_of(*s);
        }
        if (t.fresh) {
            t.ramp.set(kSilent);
            t.clock = clock;
            t.fresh = false;
        }
        t.ramp.toward(gain_to_db(gain), secs, shape);
        t.ramp.advance(clock >= 0 && t.clock >= 0 ? std::max(0.0, clock - t.clock) : dt);
        t.clock = clock;
        t.seen = true;
        t.fade = secs;
        t.bus = bus;
        if (t.ramp.settled() && t.ramp.now() <= kSilent) {
            tracked.erase(v.id);
            return;
        }
        v.level = t.ramp.now() + bus_db(bus);
        t.last = v;
        voices.push_back(v);
    }

    Vec3d where(const StateGraph& g, const State& place, const State& s, const Element& e, const Pose& frame, bool& found) const {
        found = true;
        const double lift = e.params.num(keys::lift, 0.0);
        if (const std::string* at = e.params.text(keys::at); at && !at->empty()) {
            const Element* thing = place.find(Key{*at});
            if (!thing || !thing->alive) {
                found = false;
                return {};
            }
            return world_pose(place, *thing).position + Vec3d{0, lift, 0};
        }
        (void)g;
        const Pose own = world_pose(s, e);
        return (&s == &place ? own : compose_pose(frame, own)).position + Vec3d{0, lift, 0};
    }

    // --- what sounds ------------------------------------------------------------------
    void read_sources(const StateGraph& g, const Ear& ear, double dt) {
        for (const Source& src : sources) {
            const State* s = g.find(src.state);
            const Element* e = s ? s->find(src.element) : nullptr;
            if (!e || !e->alive) continue;
            const std::string* name = e->params.text(keys::sound);
            // What fills itself is placed by the device that fills it.
            if (!name || name->empty() || name->rfind("stream:", 0) == 0) continue;
            Key place;
            Pose frame;
            if (!place_of(g, src.state, place, frame)) continue;
            const State* ps = g.find(place);
            bool found = false;
            const Vec3d at = where(g, *ps, *s, *e, frame, found);
            if (!found) continue;
            Voice v;
            v.id = src.state.str() + "/" + src.element.str();
            v.kind = Voice::Kind::Loop;
            v.take = samples.loop ? samples.loop(*name) : nullptr;
            if (!v.take || v.take->empty()) continue;
            v.pitch = std::max(0.1, e->params.num(keys::pitch, 1.0));
            v.priority = e->params.num(keys::priority, 1.0);
            const Curve& curve = curves.of(Key{text_or(e->params, keys::curve, "machine")});
            if (!hear(g, ear, place, at, curve, v, v.id)) continue;
            double clock = -1.0;
            if (!clock_of(g, src.state, clock)) clock_of(g, place, clock);
            if (e->params.get_or<bool>(keys::loop, false) && clock >= 0) v.sync = clock;
            ease(v, e->params.num(keys::gain, 1.0) * by_hour(*ps, e->params), &e->params, clock, dt, 0.12,
                 bus_of(text_or(e->params, keys::bus, "sfx"), Bus::Sfx));
        }
    }

    // A place's beds: all round the ear in the place one is in, from the
    // opening they come through in any other.
    void read_beds(const StateGraph& g, const Ear& ear, double dt) {
        beds_of(g, ear, ear.place, dt);
        for (const auto& kv : paths.reached())
            if (kv.first != ear.place) beds_of(g, ear, kv.first, dt);
    }

    void beds_of(const StateGraph& g, const Ear& ear, Key place, double dt) {
        const State* ps = g.find(place);
        const State* look = ps ? look_of(g, *ps) : nullptr;
        if (!look) return;
        const bool here = place == ear.place;
        double clock = -1.0;
        clock_of(g, place, clock);
        // Just after a crossing, what is heard of both places fades across over the seam's own fade.
        const double secs = crossing_left > 0 ? crossing : 0.5;
        for (const Element& e : look->elements()) {
            if (e.kind != kinds::bed) continue;
            const std::string* name = e.params.text(keys::sound);
            if (!name || name->empty()) continue;
            Take take = samples.loop ? samples.loop(*name) : nullptr;
            if (!take || take->empty()) continue;
            const double gain = e.params.num(keys::gain, 1.0) * by_hour(*ps, e.params);
            const Curve& curve = curves.of(Key{text_or(e.params, keys::curve, "ambient")});
            const Bus bus = bus_of(text_or(e.params, keys::bus, "ambient"), Bus::Ambient);
            const double pitch = std::max(0.1, e.params.num(keys::pitch, 1.0));
            const bool synced = e.params.get_or<bool>(keys::loop, false) && clock >= 0;
            const std::string base = place.str() + "#" + e.id.str();
            Voice v;
            v.kind = Voice::Kind::Loop;
            v.take = take;
            v.pitch = pitch;
            v.priority = e.params.num(keys::priority, 0.8);
            if (synced) v.sync = clock;
            const std::string* at_name = e.params.text(keys::at);
            if (at_name && !at_name->empty()) {
                // A bed that stands somewhere: heard from there, as a thing is.
                const Element* thing = ps->find(Key{*at_name});
                if (!thing || !thing->alive) continue;
                const Vec3d at = world_pose(*ps, *thing).position + Vec3d{0, e.params.num(keys::lift, 0.0), 0};
                v.id = base;
                if (!hear(g, ear, place, at, curve, v, "")) continue;
                ease(v, gain, &e.params, clock, dt, secs, bus);
            } else if (here) {
                // All round: a take each side, a little apart in pitch, so it
                // is wide and not in the middle of the head.
                const double d = e.params.num(keys::distance, 4.0);
                Heard c = curve.at(d);
                c.spread = 0.0;
                Vec3d side{-ear.facing.z, 0.0, ear.facing.x};
                const double sl = length(side);
                side = sl > 1e-6 ? side * (d / sl) : Vec3d{d, 0, 0};
                for (int k = 0; k < 2; ++k) {
                    Voice lr = v;
                    lr.id = base + (k ? ":r" : ":l");
                    lr.pitch = pitch * (k ? 0.95 : 1.0);
                    place_in_ears(lr, k ? side : side * -1.0, d, ear.facing, c, samples.rate);
                    lr.bands = air(d);
                    lr.echo = echo_for(place);
                    ease(lr, gain * 0.7071, &e.params, clock, dt, secs, bus);
                }
            } else {
                // Another place's: from the opening it comes through, as far
                // as the way there and a little way on.
                const Route r = paths.to(place);
                if (!r.heard) continue;
                const double d = r.length + 0.25 * e.params.num(keys::distance, 4.0);
                v.id = base;
                place_in_ears(v, r.toward * d, d, ear.facing, curve.at(d), samples.rate);
                v.bands = r.bands * air(d);
                v.echo = echo_for(place);
                ease(v, gain * e.params.num(keys::through, 1.0), &e.params, clock, dt, secs, bus);
            }
        }
    }

    // One said, or told: a fresh take of it, called what its place calls it.
    void start(const StateGraph& g, const Said& s) {
        Key place = s.place;
        Vec3d at = s.pos;
        if (place.empty() || !g.find(place)) {
            Pose frame;
            if (!place_of(g, s.state, place, frame)) return;
            if (!s.placed) at = frame.position;
            else at = compose_pose(frame, Pose{s.pos, 0.0}).position;
        }
        const State* ps = g.find(place);
        if (!ps) return;
        if (!s.at.empty()) {
            const Element* thing = ps->find(s.at);
            if (!thing) return;
            at = world_pose(*ps, *thing).position;
        }
        std::string name = s.name;
        if (const State* look = look_of(g, *ps))
            if (const std::string* as = look->params().text(Key{"as." + name}); as && !as->empty()) name = *as;
        Take take = samples.shot ? samples.shot(name) : nullptr;
        if ((!take || take->empty()) && !s.otherwise.empty() && samples.shot) take = samples.shot(s.otherwise);
        if (!take || take->empty()) return;
        Shot sh;
        sh.v.kind = Voice::Kind::Shot;
        sh.v.serial = ++serial;
        sh.v.id = "shot#" + std::to_string(sh.v.serial);
        sh.v.take = take;
        sh.v.pitch = std::max(0.1, s.pitch);
        sh.v.priority = s.priority;
        sh.place = place;
        sh.at = at;
        sh.curve = Key{s.curve.empty() ? std::string("small") : s.curve};
        sh.bus = bus_of(s.bus, Bus::Sfx);
        sh.gain_db = gain_to_db(s.gain);
        sh.left = static_cast<double>(take->size()) / samples.rate / sh.v.pitch + 0.05;
        if (shots.size() >= 64) shots.erase(shots.begin());
        shots.push_back(std::move(sh));
    }

    void read_shots(const StateGraph& g, const Ear& ear, double dt) {
        for (const Said& s : hearer->said) start(g, s);
        hearer->said.clear();
        for (const Said& s : told) start(g, s);
        told.clear();
        for (auto it = shots.begin(); it != shots.end();) {
            Shot& sh = *it;
            Voice v = sh.v;
            // Heard from where it is now, as the head turns; out of reach, silent.
            v.level = hear(g, ear, sh.place, sh.at, curves.of(sh.curve), v, "") ? sh.gain_db + bus_db(sh.bus) : kSilent;
            voices.push_back(v);
            sh.left -= dt;
            it = sh.left <= 0 ? shots.erase(it) : it + 1;
        }
    }

    void read_streams(const StateGraph& g, const Ear& ear, double dt) {
        for (std::size_t i = 0; i < streams.size(); ++i) {
            const StreamAt& p = streams[i];
            Voice v;
            v.id = "stream#" + std::to_string(i);
            v.kind = Voice::Kind::Stream;
            v.stream = static_cast<int>(i);
            v.priority = 2.0;
            if (p.gain <= 0 && !tracked.count(v.id)) continue;
            double dist = 1.0;
            if (!g.find(p.place) || !hear(g, ear, p.place, p.at, curves.of(Key{"machine"}), v, v.id, &dist)) continue;
            // As wide as it was recorded from a metre away, narrower further off.
            v.width = p.width / std::max(1.0, dist);
            ease(v, p.gain, nullptr, -1.0, dt, 0.05, p.bus);
        }
    }

    // What was heard and is no longer meant: faded out over its own time, on the shell's interval.
    void fade_gone(double dt) {
        for (auto it = tracked.begin(); it != tracked.end();) {
            Tracked& t = it->second;
            if (t.seen) {
                t.seen = false;
                ++it;
                continue;
            }
            t.ramp.toward(kSilent, crossing_left > 0 ? std::max(t.fade, crossing) : t.fade, Shape::S);
            t.ramp.advance(dt);
            if (t.ramp.now() <= kSilent) {
                it = tracked.erase(it);
                continue;
            }
            Voice v = t.last;
            v.level = t.ramp.now() + bus_db(t.bus);
            voices.push_back(v);
            ++it;
        }
    }

    void publish() {
        auto f = std::make_shared<Frame>();
        f->voices = voices;
        f->echoes = echoes;
        f->master = master;
        f->serial = frames;
        std::lock_guard<std::mutex> lk(m);
        published = std::move(f);
    }

    // --- the sound thread ---------------------------------------------------------------
    void render(const Frame& f, float* out, int n) {
        const std::size_t N = static_cast<std::size_t>(n);
        const double rate = samples.rate;
        mono.resize(N);
        left.resize(N);
        right.resize(N);
        levels.resize(N);
        scratch.resize(2 * N);
        if (wet.size() < f.echoes.size()) wet.resize(f.echoes.size());
        for (std::size_t i = 0; i < f.echoes.size(); ++i) wet[i].assign(N, 0.0f);

        // Who is meant now.
        for (auto& kv : live) kv.second.present = false;
        for (const Voice& v : f.voices) {
            auto it = live.find(v.id);
            if (it == live.end()) {
                // A one-shot plays once: one already begun is never begun again.
                if (v.kind == Voice::Kind::Shot && v.serial <= started) continue;
                if (v.kind != Voice::Kind::Stream && (!v.take || v.take->empty())) continue;
                Live l;
                l.v = v;
                l.level = v.level;
                if (v.kind == Voice::Kind::Loop) l.at = place_in_take(v);
                if (v.kind == Voice::Kind::Shot) started = std::max(started, v.serial);
                l.synced = f.serial;
                it = live.emplace(v.id, std::move(l)).first;
            }
            Live& l = it->second;
            if (v.kind == Voice::Kind::Loop && v.take && v.take != l.v.take) l.at = place_in_take(v);
            // A loop kept with its clock: put back if it has wandered more
            // than a moment from where the clock says (either way round).
            if (v.kind == Voice::Kind::Loop && v.sync >= 0 && l.synced != f.serial && v.take) {
                const double size = static_cast<double>(v.take->size()), want = place_in_take(v);
                double off = std::fabs(l.at - want);
                off = std::min(off, size - off);
                if (off > 0.12 * rate) l.at = want;
                l.synced = f.serial;
            }
            l.v = v;
            l.present = true;
        }

        // Room for so many: the least heard of the rest go, quickly.
        order.clear();
        for (auto& kv : live) order.push_back(&kv.second);
        std::sort(order.begin(), order.end(), [](const Live* a, const Live* b) {
            const double sa = a->v.priority * db_to_gain(a->v.level) * std::max(a->v.gl, a->v.gr);
            const double sb = b->v.priority * db_to_gain(b->v.level) * std::max(b->v.gl, b->v.gr);
            return sa != sb ? sa > sb : a->v.id < b->v.id;
        });
        for (std::size_t i = 0; i < order.size(); ++i) order[i]->keep = i < cap;

        const double steal_step = 1.0 / (kSteal * rate);
        for (Live* lp : order) {
            Live& l = *lp;
            const Voice& v = l.v;
            bool done = false;
            const float* in_l = mono.data();
            const float* in_r = mono.data();
            if (v.kind == Voice::Kind::Shot) {
                const auto& b = *v.take;
                for (int i = 0; i < n; ++i) {
                    const double at = l.at + i * v.pitch;
                    const std::size_t i0 = static_cast<std::size_t>(at);
                    mono[static_cast<std::size_t>(i)] =
                        i0 + 1 < b.size() ? static_cast<float>(b[i0] + (b[i0 + 1] - b[i0]) * (at - static_cast<double>(i0))) : 0.0f;
                }
                l.at += n * v.pitch;
                done = l.at + 1 >= static_cast<double>(b.size());
            } else if (v.kind == Voice::Kind::Loop) {
                const auto& b = *v.take;
                const double size = static_cast<double>(b.size());
                for (int i = 0; i < n; ++i) {
                    const std::size_t i0 = static_cast<std::size_t>(l.at);
                    const std::size_t i1 = (i0 + 1) % b.size();
                    mono[static_cast<std::size_t>(i)] = static_cast<float>(b[i0] + (b[i1] - b[i0]) * (l.at - static_cast<double>(i0)));
                    l.at += v.pitch;
                    while (l.at >= size) l.at -= size;
                }
            } else {
                std::fill(scratch.begin(), scratch.end(), 0.0f);
                {
                    std::lock_guard<std::mutex> lk(streams_m);
                    if (v.stream >= 0 && static_cast<std::size_t>(v.stream) < fills.size() && fills[static_cast<std::size_t>(v.stream)])
                        fills[static_cast<std::size_t>(v.stream)](scratch.data(), n);
                }
                for (std::size_t i = 0; i < N; ++i) {
                    left[i] = scratch[2 * i];
                    right[i] = scratch[2 * i + 1];
                }
                in_l = left.data();
                in_r = right.data();
            }
            // Its level, sample by sample: in dB from where it was to where it
            // is meant; gone from the frame, it goes now.
            const double to = l.present || v.kind == Voice::Kind::Shot ? v.level : kSilent;
            const double steal_to = l.keep ? 1.0 : 0.0;
            for (int i = 0; i < n; ++i) {
                const double u = static_cast<double>(i + 1) / n;
                l.steal += std::clamp(steal_to - l.steal, -steal_step, steal_step);
                levels[static_cast<std::size_t>(i)] = static_cast<float>(db_to_gain(l.level + (to - l.level) * u) * l.steal);
            }
            l.level = to;
            float* verb = v.echo >= 0 && static_cast<std::size_t>(v.echo) < f.echoes.size() ? wet[static_cast<std::size_t>(v.echo)].data() : nullptr;
            if (l.steal > 0.0 || steal_to > 0.0) l.sp.run(in_l, in_r, n, v, rate, levels.data(), out, verb);
            if (done || (!l.present && v.kind != Voice::Kind::Shot && l.level <= kSilent)) l.v.id.clear();
        }
        for (auto it = live.begin(); it != live.end();) it = it->second.v.id.empty() ? live.erase(it) : std::next(it);

        // Each place answers, and its answer comes the way its sounds come.
        for (auto& kv : echo_live) kv.second.present = false;
        for (std::size_t i = 0; i < f.echoes.size(); ++i) {
            const Echo& e = f.echoes[i];
            EchoLive& el = echo_live[e.place];
            if (!el.verb || std::fabs(el.size - e.size) > 1e-6) {
                el.verb = std::make_unique<Reverb>(e.size, rate);
                el.size = e.size;
                el.level = e.level;
            }
            el.e = e;
            el.present = true;
            // A place nothing sounds in rings out and then costs nothing.
            const float* in = wet[i].data();
            const bool sent = std::any_of(in, in + n, [](float x) { return x != 0.0f; });
            el.idle = sent ? 0.0 : el.idle + n / rate;
            if (el.idle > 1.5 * std::max(0.1, e.decay) + 0.5) {
                el.level = e.level;
                continue;
            }
            ring(el, in, e.level, out, n);
        }
        // One no longer meant rings on out of the block, and is gone.
        std::fill(mono.begin(), mono.end(), 0.0f);
        for (auto it = echo_live.begin(); it != echo_live.end();) {
            if (it->second.present) {
                ++it;
                continue;
            }
            ring(it->second, mono.data(), kSilent, out, n);
            it = echo_live.erase(it);
        }

        // Everything, and a soft knee so a pile-up never clips hard.
        for (std::size_t i = 0; i < 2 * N; ++i) {
            const double x = out[i] * f.master;
            out[i] = static_cast<float>(std::fabs(x) < 0.7 ? x : std::copysign(0.7 + 0.3 * std::tanh((std::fabs(x) - 0.7) / 0.3), x));
        }
    }

    void ring(EchoLive& el, const float* in, double level_to, float* out, int n) {
        const double rate = samples.rate;
        std::fill(scratch.begin(), scratch.end(), 0.0f);
        el.verb->set(el.e.decay, el.e.damp);
        el.verb->run(in, scratch.data(), n, el.e.wet);
        const double alo = 1.0 - std::exp(-kTau * kLow / rate), ahi = 1.0 - std::exp(-kTau * kHigh / rate);
        const Bands& b = el.e.bands;
        for (int i = 0; i < n; ++i) {
            const double u = static_cast<double>(i + 1) / n;
            const double g = db_to_gain(el.level + (level_to - el.level) * u);
            for (int c = 0; c < 2; ++c) {
                const double x = scratch[static_cast<std::size_t>(2 * i + c)];
                el.lo[c] += alo * (x - el.lo[c]);
                el.mid[c] += ahi * (x - el.mid[c]);
                const double y = b.low * el.lo[c] + b.mid * (el.mid[c] - el.lo[c]) + b.high * (x - el.mid[c]);
                out[2 * i + c] += static_cast<float>(y * g);
            }
        }
        el.level = level_to;
    }

    // Where in its take a loop is: each starts somewhere of its own, so two
    // of the same are not in step; on a clock, from there that far on.
    double place_in_take(const Voice& v) const {
        const double size = static_cast<double>(v.take->size());
        const double from = static_cast<double>((std::hash<std::string>{}(v.id) * 7919u) % v.take->size());
        return v.sync < 0 ? from : std::fmod(from + v.sync * samples.rate * v.pitch, size);
    }
};

Mixer::Mixer(Samples samples, int voices) : impl_(std::make_unique<Impl>(std::move(samples), voices)) {}
Mixer::~Mixer() = default;

void Mixer::listen(StateGraph& g) {
    Impl& m = *impl_;
    if (g.revision() == m.listened) return;
    m.listened = g.revision();
    const std::weak_ptr<Hearer> heard = m.hearer;
    for (Key id : g.ids()) {
        State* s = g.find(id);
        if (!s) continue;
        for (Key ev : s->said()) {
            if (!m.listening.insert({static_cast<const void*>(s), ev.handle()}).second) continue;
            // Heard as an observer: a one-shot it says is played, and nothing in the world is touched.
            s->bus().subscribe(ev, [heard, id](const Event& e) {
                const std::shared_ptr<Hearer> h = heard.lock();
                if (!h || h->said.size() >= 256) return;
                const std::string* name = e.args.text(keys::sound);
                if (!name || name->empty()) return;
                Said said;
                said.name = *name;
                said.state = id;
                if (const std::string* p = e.args.text(Key{"place"})) said.place = Key{*p};
                if (const std::string* a = e.args.text(Key{"at"})) said.at = Key{*a};
                said.placed = e.args.has(sg::keys::x) || e.args.has(sg::keys::y) || e.args.has(sg::keys::z);
                said.pos = Vec3d{e.args.num(sg::keys::x), e.args.num(sg::keys::y), e.args.num(sg::keys::z)};
                said.gain = e.args.num(Key{"gain"}, 1.0);
                said.pitch = e.args.num(Key{"pitch"}, 1.0);
                said.priority = e.args.num(Key{"priority"}, 1.0);
                said.bus = text_or(e.args, Key{"bus"}, "sfx");
                said.curve = text_or(e.args, Key{"curve"}, "small");
                said.otherwise = text_or(e.args, Key{"or"}, "");
                h->said.push_back(std::move(said));
            });
        }
    }
}

Ear Mixer::ear_of(const Engine& engine) {
    Ear e;
    const State* s = engine.current();
    if (!s) return e;
    e.place = s->id();
    if (const Element* cam = s->find(SpatialState::camera_id())) {
        e.pos = world_pose(*s, *cam).position;
        e.facing = forward_of(*cam);
    }
    return e;
}

void Mixer::read(const Engine& engine, double dt) { read(engine.graph(), ear_of(engine), dt); }

void Mixer::read(const StateGraph& g, const Ear& ear, double dt) {
    Impl& m = *impl_;
    dt = std::max(0.0, dt);
    if (g.revision() != m.indexed) {
        m.reindex(g);
        m.indexed = g.revision();
    }
    m.curves.read(g);
    m.paths.build(g, ear.place, ear.pos);
    ++m.frames;
    // A crossing: what is heard of the place left and the place come to
    // fades across over the seam's own fade.
    if (ear.place != m.last_place) {
        if (!m.last_place.empty()) {
            m.crossing = m.paths.fade_between(m.last_place, ear.place);
            m.crossing_left = m.crossing;
        }
        m.last_place = ear.place;
    }
    m.crossing_left = std::max(0.0, m.crossing_left - dt);

    m.read_buses(g, ear, dt);
    m.read_echoes(g, ear, dt);
    m.voices.clear();
    m.read_sources(g, ear, dt);
    m.read_beds(g, ear, dt);
    m.read_shots(g, ear, dt);
    m.read_streams(g, ear, dt);
    m.fade_gone(dt);
    m.publish();
}

void Mixer::shot(const std::string& name, Key place, const Vec3d& at, double gain, double pitch, Bus bus) {
    Said s;
    s.bus = bus_name(bus);
    s.name = name;
    s.place = place;
    s.pos = at;
    s.placed = true;
    s.gain = gain;
    s.pitch = pitch;
    if (impl_->told.size() < 256) impl_->told.push_back(std::move(s));
}

int Mixer::stream(Fill fill) {
    Impl& m = *impl_;
    {
        std::lock_guard<std::mutex> lk(m.streams_m);
        m.fills.push_back(std::move(fill));
    }
    m.streams.push_back(StreamAt{});
    return static_cast<int>(m.streams.size()) - 1;
}

void Mixer::place(int stream, Key place, const Vec3d& at, double gain, double width, Bus bus) {
    Impl& m = *impl_;
    if (stream < 0 || stream >= static_cast<int>(m.streams.size())) return;
    m.streams[static_cast<std::size_t>(stream)] = StreamAt{place, at, std::max(0.0, gain), width, bus};
}

void Mixer::master(double gain) { impl_->master = std::max(0.0, gain); }

const std::vector<Voice>& Mixer::voices() const { return impl_->voices; }
const std::vector<Echo>& Mixer::echoes() const { return impl_->echoes; }
double Mixer::bus_db(Bus b) const { return impl_->bus_db(b); }

void Mixer::render(float* lr, int frames) {
    std::fill(lr, lr + 2 * static_cast<std::size_t>(std::max(0, frames)), 0.0f);
    if (frames <= 0) return;
    std::shared_ptr<const Frame> f;
    {
        std::lock_guard<std::mutex> lk(impl_->m);
        f = impl_->published;
    }
    if (f) impl_->render(*f, lr, frames);
}

}  // namespace sg::audio
