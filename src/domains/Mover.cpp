// A mover: a body's motion made by MotionBricks as it is steered (sg/domains/Mover.hpp).
#include "sg/domains/Mover.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "sg/domains/Being.hpp"
#include "sg/domains/Bricks.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg {

namespace {
using bricks::Pose;
using bricks::Quat;
using bricks::Xf;
const bricks::Skeleton& soma() { return bricks::Skeleton::soma(); }

// How far before a brick's end the next is asked for, and how long ahead of
// that its making is begun.
constexpr int kEarly = 6;
constexpr double kAhead = 0.4;
// How soon a brick is cut short once it is told something new.
constexpr double kCut = 0.2;

// --- the motion made, kept by what it was made from ----------------------------
// The model and what it made, for the program: one model, one hand of its
// own to make with (as ONNX Runtime keeps its own threads) - a brick asked
// for before it is needed is made while the frames go on; the same brick
// whether it was waited for or not, so nothing depends on which came first.
class Maker {
public:
    static Maker& get() {
        // (Kept for the program's life, and its hand stopped as the program
        // ends - before ONNX Runtime is taken down under it.)
        static Maker* m = new Maker();
        return *m;
    }
    bool ready(std::string* why) {
        std::lock_guard<std::mutex> lock(m_);
        if (!tried_) {
            tried_ = true;
            model_ = bricks::Model::open({}, &why_);
            if (model_) std::atexit([] { get().shutdown(); });
        }
        if (!model_ && why) *why = why_;
        return model_ != nullptr;
    }
    // Begun now, if it is not made or being made.
    void ask(std::uint64_t key, std::array<Pose, 4> context, Pose target, bricks::Settings s) {
        std::lock_guard<std::mutex> lock(m_);
        if (!model_ || made_.count(key) || asked_.count(key)) return;
        asked_[key] = Job{std::move(context), std::move(target), std::move(s)};
        if (std::getenv("SG_MOVER_SAY")) std::fprintf(stderr, "mover: brick %016llx asked\n", static_cast<unsigned long long>(key));
        queue_.push_back(key);
        if (!worker_.joinable()) worker_ = std::thread([this] { work(); });
        cv_.notify_all();
    }
    // Made (waited for, or made here and now).
    std::shared_ptr<const bricks::Motion> take(std::uint64_t key, const std::array<Pose, 4>& context, const Pose& target, const bricks::Settings& s) {
        std::unique_lock<std::mutex> lock(m_);
        if (!model_) return nullptr;
        static const bool say = std::getenv("SG_MOVER_SAY") != nullptr;
        for (;;) {
            if (auto it = made_.find(key); it != made_.end()) {
                if (say) std::fprintf(stderr, "mover: brick %016llx made\n", static_cast<unsigned long long>(key));
                return it->second;
            }
            if (!asked_.count(key)) break;
            if (say) std::fprintf(stderr, "mover: brick %016llx waited for\n", static_cast<unsigned long long>(key));
            cv_.wait(lock);
        }
        if (say) std::fprintf(stderr, "mover: brick %016llx made now\n", static_cast<unsigned long long>(key));
        auto made = std::make_shared<bricks::Motion>();
        std::string why;
        std::lock_guard<std::mutex> one(run_);
        if (!model_->generate(soma(), context, {target}, s, *made, &why)) {
            std::fprintf(stderr, "mover: %s\n", why.c_str());
            return nullptr;
        }
        keep(key, made);
        return made;
    }
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

private:
    struct Job {
        std::array<Pose, 4> context;
        Pose target;
        bricks::Settings s;
    };
    void keep(std::uint64_t key, std::shared_ptr<const bricks::Motion> m) {
        made_[key] = std::move(m);
        order_.push_back(key);
        while (order_.size() > 64) made_.erase(order_.front()), order_.pop_front();
    }
    void work() {
        std::unique_lock<std::mutex> lock(m_);
        while (!stop_) {
            if (queue_.empty()) {
                cv_.wait(lock);
                continue;
            }
            const std::uint64_t key = queue_.front();
            queue_.pop_front();
            const Job job = asked_[key];
            lock.unlock();
            auto made = std::make_shared<bricks::Motion>();
            std::string why;
            bool ok = false;
            {
                std::lock_guard<std::mutex> one(run_);
                ok = model_->generate(soma(), job.context, {job.target}, job.s, *made, &why);
            }
            lock.lock();
            if (ok) keep(key, made);
            asked_.erase(key);
            cv_.notify_all();
        }
    }
    std::mutex m_, run_;  // (run_: one making at a time)
    std::condition_variable cv_;
    std::unique_ptr<bricks::Model> model_;
    std::string why_;
    bool tried_ = false, stop_ = false;
    std::unordered_map<std::uint64_t, std::shared_ptr<const bricks::Motion>> made_;
    std::deque<std::uint64_t> order_;
    std::map<std::uint64_t, Job> asked_;
    std::deque<std::uint64_t> queue_;
    std::thread worker_;
};

// --- a brick as text ------------------------------------------------------------
// The model's 30 joints of a pose, each place and turn (the rest are SOMA's reference).
void write_pose(std::ostringstream& o, const Pose& p) {
    for (const std::string& n : bricks::model_joints()) {
        const Xf& x = p[static_cast<std::size_t>(soma().index(n))];
        o << x.p.x << ' ' << x.p.y << ' ' << x.p.z << ' ' << x.q.w << ' ' << x.q.x << ' ' << x.q.y << ' ' << x.q.z << ' ';
    }
}
bool read_pose(std::istringstream& in, Pose& p) {
    p = soma().reference;
    for (const std::string& n : bricks::model_joints()) {
        Xf& x = p[static_cast<std::size_t>(soma().index(n))];
        if (!(in >> x.p.x >> x.p.y >> x.p.z >> x.q.w >> x.q.x >> x.q.y >> x.q.z)) return false;
    }
    return true;
}
std::string text_of(const std::array<Pose, 4>& context, const Pose& target) {
    std::ostringstream o;
    o.precision(9);
    for (const Pose& p : context) write_pose(o, p);
    write_pose(o, target);
    return o.str();
}
bool from_text(const std::string& s, std::array<Pose, 4>& context, Pose& target) {
    std::istringstream in(s);
    for (Pose& p : context)
        if (!read_pose(in, p)) return false;
    return read_pose(in, target);
}

// Which way a pose faces (a yaw: x toward z), from where its legs stand; and where it stands on the ground.
double yaw_of(const std::vector<Xf>& w) {
    const auto& sk = soma();
    const Vec3d r = w[static_cast<std::size_t>(sk.index("RightLeg"))].p, l = w[static_cast<std::size_t>(sk.index("LeftLeg"))].p;
    // (Ahead is square to the line from the left hip to the right, on the ground.)
    const Vec3d across{r.x - l.x, 0.0, r.z - l.z};
    const Vec3d ahead = cross({0.0, 1.0, 0.0}, across);
    return std::atan2(ahead.z, ahead.x);
}

// SOMA standing at ease at (x, z) facing `yaw`.
Pose standing(double x, double z, double yaw) {
    static const double own = yaw_of(bricks::world_of(soma(), bricks::at_ease()));
    Pose out = bricks::at_ease();
    out[0] = {{x, out[0].p.y, z}, bricks::normalized(bricks::mul(bricks::about_y(-(yaw - own)), out[0].q))};
    return out;
}

bricks::Settings settings_for(double step, double speed) {
    bricks::Settings s;
    if (speed > 0.05 && step > 0.05) {
        // As long as the step takes at that speed: its frames, in tokens (four
        // frames each, three dropped at each end), and one either side.
        const double frames = step / speed * bricks::kRate + 2 * bricks::kBoundary;
        const int n = std::clamp(static_cast<int>(std::lround(frames / bricks::kPerToken)), bricks::kMinTokens + 1, bricks::kMaxTokens - 1);
        s.tokens = {n - 1, n, n + 1};
    }
    return s;
}

std::uint64_t key_of(const std::string& brick, const bricks::Settings& s) {
    std::string k = brick + (s.root_only ? " r" : "");
    for (int t : s.tokens) k += ' ' + std::to_string(t);
    return std::hash<std::string>{}(k);
}

// The brick that follows from where it stands at the end of `context`, toward what it was told.
void next_target(const Params& self, const std::array<Pose, 4>& context, Pose& target, bricks::Settings& s) {
    const std::vector<Xf> w = bricks::world_of(soma(), context[3]);
    const double hx = w[0].p.x, hz = w[0].p.z, yaw = yaw_of(w);
    const double gx = self.num("goal_x", hx), gz = self.num("goal_z", hz), speed = self.num("speed");
    const double dx = gx - hx, dz = gz - hz, dist = std::hypot(dx, dz);
    if (speed > 0.05 && dist > 0.35) {
        const double step = std::min(dist, std::max(0.6, speed * 1.1));
        // (Facing the way it goes - or, held, the way it was told to face:
        // stepping back or aside to get there.)
        const double way = self.num("hold") > 0.5 && self.has(Key{"face"}) ? self.num("face") : std::atan2(dz, dx);
        target = standing(hx + dx / dist * step, hz + dz / dist * step, way);
        s = settings_for(step, speed);
        // (On the way, only where it is to be and how it faces: its stride its
        // own, never brought to stand at each target.)
        s.root_only = true;
        return;
    }
    const bool arrived = speed > 0.05;
    const double face = self.has(Key{"face"}) ? self.num("face") : yaw;
    target = standing(arrived ? gx : hx, arrived ? gz : hz, face);
    s = bricks::Settings{};
}

// The last four poses at 30 a second up to `t` in `m` (before its start, its first).
std::array<Pose, 4> context_at(const bricks::Motion& m, double t) {
    std::array<Pose, 4> c;
    for (int k = 0; k < 4; ++k) m.sample(std::max(0.0, t - (3 - k) / bricks::kRate), c[static_cast<std::size_t>(k)]);
    return c;
}

double transition_of(const bricks::Motion& m) { return std::max(0.0, (static_cast<double>(m.frames.size()) - 1 - kEarly) / bricks::kRate); }

// A pose written as the mover's elements: each joint's turn in its own frame
// (where it stands on the ground, facing its yaw), and where it stands.
void write(State& s, const Pose& pose) {
    const std::vector<Xf> w = bricks::world_of(soma(), pose);
    const double yaw = yaw_of(w);
    const Vec3d ground{w[0].p.x, 0.0, w[0].p.z};
    const Quat in_own = bricks::about_y(yaw);
    Params& self = s.element(Mover::self_id()).params;
    self.set(keys::x, ground.x).set(keys::z, ground.z).set(keys::yaw, yaw);
    for (std::size_t j = 0; j < w.size(); ++j) {
        Element* e = s.find(Key{"j." + soma().names[j]});
        if (!e) continue;
        const Quat q = bricks::normalized(bricks::mul(in_own, w[j].q));
        e->params.set("pqw", q.w).set("pqx", q.x).set("pqy", q.y).set("pqz", q.z);
        if (j == 0) {
            const Vec3d p = bricks::rotate(in_own, w[0].p - ground);
            e->params.set("px", p.x).set("py", p.y).set("pz", p.z);
        }
    }
}

std::string tokens_text(const bricks::Settings& set) {
    std::string out = set.root_only ? "r " : "";
    for (int t : set.tokens) out += std::to_string(t) + ' ';
    return out;
}
bricks::Settings tokens_from(const std::string& text) {
    bricks::Settings set;
    std::istringstream in(text);
    std::vector<int> t;
    for (std::string w; in >> w;)
        if (w == "r") set.root_only = true;
        else t.push_back(std::atoi(w.c_str()));
    if (!t.empty()) set.tokens = t;
    return set;
}

void advance(State& s, double dt) {
    Params& self = s.element(Mover::self_id()).params;
    Maker& maker = Maker::get();
    if (!maker.ready(nullptr)) return;
    std::array<Pose, 4> context;
    Pose target;
    bricks::Settings set;
    const std::string* brick = self.text(Key{"brick"});
    double t = self.num("t") + std::max(0.0, dt);
    if (!brick || brick->empty() || !from_text(*brick, context, target)) {
        // The first: standing as it was put.
        const Pose here = standing(self.num(keys::x), self.num(keys::z), self.num(keys::yaw));
        context = {here, here, here, here};
        next_target(self, context, target, set);
        t = 0.0;
    } else {
        set = tokens_from(self.get_or<std::string>("tokens", ""));
    }
    std::string text = text_of(context, target);
    std::shared_ptr<const bricks::Motion> m = maker.take(key_of(text, set), context, target, set);
    // Told something new (to go, to stop, another way): the brick cut short
    // a little on from now, at a moment set as it was told - the one after
    // it asked for at once, from the poses there, and taken then (so the
    // motion is the same however fast it is made).
    if (m && !m->frames.empty() && self.num("cut") > 0.5) {
        self.set("cut", 0.0);
        const double at = std::min(t + kCut, transition_of(*m));
        if (at > t) {
            self.set("cut_at", at);
            const std::array<Pose, 4> c = context_at(*m, at);
            Pose next;
            bricks::Settings ns;
            next_target(self, c, next, ns);
            maker.ask(key_of(text_of(c, next), ns), c, next, ns);
        }
    }
    if (m && !m->frames.empty() && self.has(Key{"cut_at"}) && t >= self.num("cut_at")) {
        const double at = self.num("cut_at");
        self.erase(Key{"cut_at"});
        context = context_at(*m, at);
        next_target(self, context, target, set);
        text = text_of(context, target);
        if (const std::shared_ptr<const bricks::Motion> next = maker.take(key_of(text, set), context, target, set)) m = next, t -= at;
    }
    // On into the next brick at the moment the last turns over (exactly then:
    // the one asked for ahead is the one taken).
    for (int guard = 0; m && !m->frames.empty() && guard < 8 && t >= transition_of(*m); ++guard) {
        const double at = transition_of(*m);
        context = context_at(*m, at);
        next_target(self, context, target, set);
        text = text_of(context, target);
        const std::shared_ptr<const bricks::Motion> next = maker.take(key_of(text, set), context, target, set);
        if (!next) break;
        m = next;
        t -= at;
    }
    if (!m || m->frames.empty()) return;
    self.set("brick", text).set("tokens", tokens_text(set)).set("t", t);
    // The next asked for a little before it is needed.
    const double at = transition_of(*m);
    if (t >= at - kAhead) {
        const std::array<Pose, 4> c = context_at(*m, at);
        Pose next;
        bricks::Settings ns;
        next_target(self, c, next, ns);
        maker.ask(key_of(text_of(c, next), ns), c, next, ns);
    }
    Pose pose;
    m->sample(t, pose);
    write(s, pose);
}
}  // namespace

Mover::Mover(Key id, double x, double z, double yaw) : State(id) {
    Element& self = add_element(self_id(), Key{"self"});
    self.params.set(keys::x, x).set(keys::z, z).set(keys::yaw, yaw).set("goal_x", x).set("goal_z", z).set("speed", 0.0).set("t", 0.0);
    for (const std::string& n : soma().names) add_element(Key{"j." + n}, Key{"joint"});
    write(*this, standing(x, z, yaw));
    loop(Key{"live"}, self_id(), live_event(), [](State& s, Element&, Element*, const Event& ev) { advance(s, ev.args.num(keys::dt)); });
    loop(Key{"go"}, self_id(), go_event(), [](State&, Element& e, Element*, const Event& ev) {
        // (Something new - going from standing, stopping, another way, another
        // facing - cuts the brick it is in short: `advance`.)
        const double gx = ev.args.num(keys::x, e.params.num(keys::x)), gz = ev.args.num(keys::z, e.params.num(keys::z)), speed = std::max(0.0, ev.args.num("speed", 1.2));
        const double hx = e.params.num(keys::x), hz = e.params.num(keys::z);
        const double was_way = std::atan2(e.params.num("goal_z") - hz, e.params.num("goal_x") - hx), way = std::atan2(gz - hz, gx - hx);
        const bool going = speed > 0.05 && std::hypot(gx - hx, gz - hz) > 0.35, went = e.params.num("speed") > 0.05 && std::hypot(e.params.num("goal_x") - hx, e.params.num("goal_z") - hz) > 0.35;
        const double face = ev.args.num("face", e.params.num("face", e.params.num(keys::yaw)));
        if (going != went || (going && std::fabs(std::remainder(way - was_way, 2 * 3.14159265358979)) > 0.5) ||
            std::fabs(std::remainder(face - e.params.num("face", face), 2 * 3.14159265358979)) > 0.5)
            e.params.set("cut", 1.0);
        e.params.set("goal_x", gx).set("goal_z", gz);
        e.params.set("speed", speed);
        if (ev.args.has(Key{"face"})) e.params.set("face", ev.args.num("face"));
        else e.params.erase(Key{"face"});
        e.params.set("hold", ev.args.num("hold"));
    });
    loop(Key{"stand"}, self_id(), stand_event(), [](State&, Element& e, Element*, const Event&) {
        e.params.set("goal_x", e.params.num(keys::x)).set("goal_z", e.params.num(keys::z)).set("speed", 0.0);
    });
}

std::vector<std::string> Mover::faults() const {
    std::vector<std::string> out = State::faults();
    std::string why;
    if (!Maker::get().ready(&why)) out.push_back("it has no model to move by (" + why + "): it stands as it was put");
    return out;
}

// --- carried onto a body and a place ---------------------------------------------

std::vector<std::pair<std::string, std::string>> bricks_joints(const Being& b) {
    // SOMA's names, then MHR's and Mixamo's for the same joints.
    static const std::vector<std::array<const char*, 3>> table = {
        {"Hips", "root", "Hips"}, {"Spine1", "c_spine0", "Spine"}, {"Spine2", "c_spine1", "Spine1"}, {"Chest", "c_spine2", "Spine2"},
        {"Neck1", "c_neck", "Neck"}, {"Head", "c_head", "Head"}, {"LeftShoulder", "l_clavicle", "LeftShoulder"},
        {"LeftArm", "l_uparm", "LeftArm"}, {"LeftForeArm", "l_lowarm", "LeftForeArm"}, {"LeftHand", "l_wrist", "LeftHand"},
        {"RightShoulder", "r_clavicle", "RightShoulder"}, {"RightArm", "r_uparm", "RightArm"}, {"RightForeArm", "r_lowarm", "RightForeArm"},
        {"RightHand", "r_wrist", "RightHand"}, {"LeftLeg", "l_upleg", "LeftUpLeg"}, {"LeftShin", "l_lowleg", "LeftLeg"},
        {"LeftFoot", "l_foot", "LeftFoot"}, {"LeftToeBase", "l_ball", "LeftToeBase"}, {"RightLeg", "r_upleg", "RightUpLeg"},
        {"RightShin", "r_lowleg", "RightLeg"}, {"RightFoot", "r_foot", "RightFoot"}, {"RightToeBase", "r_ball", "RightToeBase"}};
    // (A joint by its name, or its name without the namespace a rig gave it.)
    std::unordered_map<std::string, std::string> theirs;
    for (Key j : b.joints()) {
        const std::string n = j.str();
        const std::size_t k = n.find_last_of(":|");
        theirs.emplace(n, n);
        if (k != std::string::npos) theirs.emplace(n.substr(k + 1), n);
    }
    std::vector<std::pair<std::string, std::string>> best;
    for (int col = 0; col < 3; ++col) {
        std::vector<std::pair<std::string, std::string>> got;
        for (const auto& row : table)
            if (auto it = theirs.find(row[static_cast<std::size_t>(col)]); it != theirs.end()) got.emplace_back(row[0], it->second);
        if (got.size() > best.size()) best = std::move(got);
    }
    return best;
}

namespace {
// The shortest turn that takes direction `a` to `b`.
Quat turn_between(const Vec3d& a0, const Vec3d& b0) {
    const Vec3d a = unit(a0), b = unit(b0);
    const double d = dot(a, b);
    if (d < -0.999999) {
        Vec3d axis = cross({1.0, 0.0, 0.0}, a);
        if (length(axis) < 1e-6) axis = cross({0.0, 1.0, 0.0}, a);
        axis = unit(axis);
        return {0.0, axis.x, axis.y, axis.z};
    }
    const Vec3d c = cross(a, b);
    return bricks::normalized({1.0 + d, c.x, c.y, c.z});
}

// The limbs whose bones are laid along SOMA's before a motion is carried:
// each a joint and the next down it.
const std::vector<std::pair<const char*, const char*>>& limbs() {
    static const std::vector<std::pair<const char*, const char*>> l = {
        {"LeftArm", "LeftForeArm"},  {"LeftForeArm", "LeftHand"}, {"RightArm", "RightForeArm"}, {"RightForeArm", "RightHand"},
        {"LeftLeg", "LeftShin"},     {"LeftShin", "LeftFoot"},    {"RightLeg", "RightShin"},     {"RightShin", "RightFoot"}};
    return l;
}
}  // namespace

Key move_body(StateGraph& g, const Mover& m, Being& b) {
    const Key name{m.id().str() + ".moves." + b.id().str()};
    // SOMA's own bind: its reference's turns and places, it facing ahead (+x).
    const std::vector<Xf> ref = bricks::world_of(soma(), soma().reference);
    const Quat own = bricks::about_y(yaw_of(ref));
    const auto pairs = bricks_joints(b);
    std::unordered_map<std::string, std::string> to_soma;
    for (const auto& [s, t] : pairs) to_soma[t] = s;
    // The body as it is bound - each joint's place and turn in its own frame,
    // parents first - and then each limb laid along SOMA's (as an animator
    // poses a character into the actor's reference before carrying the
    // motion): the bind the motion is carried onto.
    struct Bound {
        Vec3d p;
        Quat q, laid;  // its turn, and the turn the laying gave it and all below it
    };
    std::unordered_map<std::string, Bound> bound;
    for (Key j : b.joints()) {
        const Element& e = b.element(j);
        const std::string up = e.params.get_or<std::string>("parent_joint", "");
        const Quat q = bricks::normalized({e.params.num("bqw", 1.0), e.params.num("bqx"), e.params.num("bqy"), e.params.num("bqz")});
        const Vec3d off{e.params.num(keys::x), e.params.num(keys::y), e.params.num(keys::z)};
        const auto pit = bound.find(up);
        Bound me{off, q, Quat{}};
        if (pit != bound.end()) me.p = pit->second.p + bricks::rotate(pit->second.q, off), me.laid = pit->second.laid;
        bound[j.str()] = me;
    }
    // (Laid limb by limb, parents first: each joint's turn the bones above it
    // were given, then its own to lie as SOMA's lies.)
    std::unordered_map<std::string, Quat> laid;
    for (Key j : b.joints()) {
        const std::string up = b.element(j).params.get_or<std::string>("parent_joint", "");
        Quat d = laid.count(up) ? laid[up] : Quat{};
        const auto si = to_soma.find(j.str());
        if (si != to_soma.end())
            for (const auto& [a, c] : limbs()) {
                if (si->first != a && si->second != a) continue;
                const auto child = std::find_if(pairs.begin(), pairs.end(), [&](const auto& pr) { return pr.first == c; });
                if (child == pairs.end() || !bound.count(child->second)) continue;
                const Vec3d have = bricks::rotate(d, bound[child->second].p - bound[j.str()].p);
                const int sa = soma().index(a), sc = soma().index(c);
                const Vec3d want = bricks::rotate(own, ref[static_cast<std::size_t>(sc)].p - ref[static_cast<std::size_t>(sa)].p);
                d = bricks::normalized(bricks::mul(turn_between(have, want), d));
            }
        laid[j.str()] = d;
    }
    // How high its hips are, against SOMA's (how far to carry them).
    double body_hips = 0.0;
    Functor f(name, m.id(), b.id());
    for (const auto& [from, to] : pairs) {
        const int j = soma().index(from);
        const Quat bind = bricks::normalized(bricks::mul(own, ref[static_cast<std::size_t>(j)].q));
        const Quat target_bind = bricks::normalized(bricks::mul(laid[to], bound[to].q));
        const bool root = j == 0;
        if (root) body_hips = std::max(0.3, bound[to].p.y);
        const double k = root ? body_hips / std::max(0.3, ref[0].p.y) : 1.0;
        f.on_object(Key{"j." + from}, Key{to}, [bind, target_bind, root, k](const Element& src, Element& dst) {
            const Quat pq{src.params.num("pqw", 1.0), src.params.num("pqx"), src.params.num("pqy"), src.params.num("pqz")};
            const Quat lead = bricks::normalized(bricks::mul(bricks::mul(pq, bricks::conj(bind)), target_bind));
            dst.params.set("lead_qw", lead.w).set("lead_qx", lead.x).set("lead_qy", lead.y).set("lead_qz", lead.z).set("lead_at", 1.0);
            if (root) {
                dst.params.set("lead_tx", src.params.num("px") * k).set("lead_ty", src.params.num("py") * k).set("lead_tz", src.params.num("pz") * k);
                dst.params.set("lead_moves", 1.0);
            } else {
                dst.params.set("lead_moves", 0.0);
            }
        });
    }
    g.set_functor(std::move(f));
    g.keep(name);
    Params& self = b.element(Being::self_id()).params;
    self.set("lead", 1.0).set("lead_to", 1.0);
    return name;
}

Key move_spot(StateGraph& g, const Mover& m, const Spatial3D& host, Key spot) {
    const Key name{m.id().str() + ".stands." + host.id().str()};
    Functor f(name, m.id(), host.id());
    f.on_object(Mover::self_id(), spot, transport::only({keys::x, keys::z, keys::yaw}));
    g.set_functor(std::move(f));
    g.keep(name);
    return name;
}

}  // namespace sg
