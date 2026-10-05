#include "sg/domains/Modeler.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include "ModelerLibrary.hpp"
#include "SG_MODELER_CODE.hpp"
#include "sg/core/Cache.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/domains/Shapes.hpp"

namespace sg {

namespace {
// Bumped by hand when what a recipe makes changes for a reason the code's own
// digest (SG_MODELER_CODE, of the modeller's sources) cannot see.
constexpr int kModelerCode = 1;

sculpt::Files& files() {
    static sculpt::Files f;
    return f;
}

// A model as bytes, and back: what the disk keeps (sg::cache).
void put_bytes(std::string& s, const void* p, std::size_t n) { s.append(static_cast<const char*>(p), n); }
template <class T>
void put(std::string& s, T v) {
    put_bytes(s, &v, sizeof v);
}
void put_text(std::string& s, const std::string& t) {
    put<uint64_t>(s, t.size());
    s += t;
}
std::string to_bytes(const sculpt::Model& m) {
    std::string s;
    put<uint32_t>(s, 1);
    put_text(s, m.errors);
    for (double v : {m.lo.x, m.lo.y, m.lo.z, m.hi.x, m.hi.y, m.hi.z}) put(s, v);
    put<uint64_t>(s, m.triangles);
    put<uint64_t>(s, m.parts.size());
    for (const sculpt::Part& p : m.parts) {
        put_text(s, p.material);
        put<uint64_t>(s, p.corners.size());
        put_bytes(s, p.corners.data(), p.corners.size() * sizeof(float));
    }
    return s;
}
struct Reader {
    const std::string& s;
    std::size_t at = 0;
    bool ok = true;
    bool take(void* p, std::size_t n) {
        if (!ok || n > s.size() - at) return ok = false;
        std::memcpy(p, s.data() + at, n);
        at += n;
        return true;
    }
    template <class T>
    T get() {
        T v{};
        take(&v, sizeof v);
        return v;
    }
    std::string text() {
        const uint64_t n = get<uint64_t>();
        if (!ok || n > s.size() - at) return ok = false, std::string();
        std::string t = s.substr(at, n);
        at += n;
        return t;
    }
};
bool from_bytes(const std::string& s, sculpt::Model& m) {
    Reader r{s};
    if (r.get<uint32_t>() != 1) return false;
    m.errors = r.text();
    double v[6];
    for (double& d : v) d = r.get<double>();
    m.lo = {v[0], v[1], v[2]}, m.hi = {v[3], v[4], v[5]};
    m.triangles = r.get<uint64_t>();
    const uint64_t parts = r.get<uint64_t>();
    if (!r.ok || parts > s.size()) return false;
    m.parts.resize(parts);
    for (sculpt::Part& p : m.parts) {
        p.material = r.text();
        const uint64_t n = r.get<uint64_t>();
        if (!r.ok || n > (s.size() - r.at) / sizeof(float)) return false;
        p.corners.resize(n);
        r.take(p.corners.data(), n * sizeof(float));
    }
    return r.ok && r.at == s.size();
}

// What a recipe's mesh is made from, for the disk: the text, the settings, the
// libraries a program defined, and the code that makes it.
Digest disk_key(const std::string& text, const sculpt::Options& o) {
    return Hasher{}
        .text("sg.modeler")
        .text(SG_MODELER_CODE)
        .integer(kModelerCode)
        .number(o.cell)
        .integer(o.sides)
        .number(o.crease)
        .integer(o.max_grid)
        .text(sculpt::defined_libraries())
        .text(text)
        .digest();
}

// A recipe built, or read back from where it was kept: a recipe that reads no
// file is a function of its key, so what was kept is what it would make.
sculpt::Model build_kept(const std::string& text, const sculpt::Options& o, const sculpt::Files* f) {
    const Digest key = disk_key(text, o);
    std::string bytes;
    sculpt::Model m;
    if (cache::load("modeler", key, bytes) && from_bytes(bytes, m)) return m;
    m = sculpt::build(text, o, f);
    if (m.imports.empty()) cache::store("modeler", key, to_bytes(m));
    return m;
}

std::string memory_key(const std::string& text, const sculpt::Options& o) {
    return std::to_string(o.cell) + "|" + std::to_string(o.sides) + "|" + std::to_string(o.crease) + "|" + std::to_string(o.max_grid) + "|" +
           std::to_string(sculpt::library_revision()) + "|" + text;
}

// Recipes already built, by what they are made from: the same text at the same
// settings is the same mesh, wherever it is asked for. Only ones that read no
// file (a file can change under the same words).
std::mutex& seen_mutex() {
    static std::mutex m;
    return m;
}
std::unordered_map<std::string, std::shared_ptr<const sculpt::Model>>& seen() {
    static std::unordered_map<std::string, std::shared_ptr<const sculpt::Model>> s;
    return s;
}
std::shared_ptr<const sculpt::Model> seen_before(const std::string& key) {
    std::lock_guard<std::mutex> g(seen_mutex());
    auto it = seen().find(key);
    return it == seen().end() ? nullptr : it->second;
}
std::shared_ptr<const sculpt::Model> cached(const std::string& key, const std::function<sculpt::Model()>& make) {
    if (auto was = seen_before(key)) return was;
    auto built = std::make_shared<const sculpt::Model>(make());
    if (!built->imports.empty()) return built;
    std::lock_guard<std::mutex> g(seen_mutex());
    if (seen().size() > 128) seen().clear();
    return seen().emplace(key, built).first->second;
}
}  // namespace

namespace sculpt {

void prepare(const std::vector<std::string>& recipes, const Options& o) {
    // Each recipe once, and only those not made already; then all at once, a
    // recipe to a core - each is a function of its own text alone.
    std::vector<const std::string*> due;
    std::set<std::string> asked;
    for (const std::string& r : recipes)
        if (asked.insert(r).second && !seen_before(memory_key(r, o))) due.push_back(&r);
    if (due.empty()) return;
    const sculpt::Files* f = files().read ? &files() : nullptr;
    std::atomic<std::size_t> next{0};
    const auto work = [&] {
        for (std::size_t i; (i = next++) < due.size();) {
            const std::string& t = *due[i];
            cached(memory_key(t, o), [&] { return build_kept(t, o, f); });
        }
    };
    const unsigned cores = cache::hands();
    std::vector<std::thread> hands;
    for (unsigned k = 1; k < std::min<std::size_t>(cores, due.size()); ++k)
        hands.emplace_back([&] {
            cache::background_priority();
            work();
        });
    work();
    for (std::thread& t : hands) t.join();
}

}  // namespace sculpt

Modeler::Modeler(Key id) : State(std::move(id)) {
    Element& r = add_element(recipe_id(), Key{"recipe"});
    r.params.set("ops", std::string()).set("cell", 0.1).set("sides", 24.0).set("crease", 40.0);
    // recipe --op--> recipe: a line added.
    loop(Key{"op"}, recipe_id(), op_event(), [](State&, Element& e, Element*, const Event& ev) {
        std::string line = ev.args.get_or<std::string>("line", "");
        if (line.empty()) return;
        if (line.back() != '\n') line += '\n';
        e.params.set("ops", e.params.get_or<std::string>("ops", "") + line);
    });
    // recipe --set--> recipe: the whole recipe.
    loop(Key{"set"}, recipe_id(), set_event(), [](State&, Element& e, Element*, const Event& ev) {
        e.params.set("ops", ev.args.get_or<std::string>("ops", ""));
    });
    // recipe --undo--> recipe: the last n lines taken back.
    loop(Key{"undo"}, recipe_id(), undo_event(), [](State&, Element& e, Element*, const Event& ev) {
        std::string s = e.params.get_or<std::string>("ops", "");
        for (int n = std::max(1, int(ev.args.num("n", 1.0))); n > 0 && !s.empty(); --n) {
            s.pop_back();
            const std::size_t cut = s.rfind('\n');
            s = cut == std::string::npos ? std::string() : s.substr(0, cut + 1);
        }
        e.params.set("ops", s);
    });
    loop(Key{"clear"}, recipe_id(), clear_event(), [](State&, Element& e, Element*, const Event&) { e.params.set("ops", std::string()); });
    // recipe --select--> recipe: the line being worked on (-1: none) - as a
    // cursor is the document's, this is the recipe's.
    r.params.set("selected", -1.0);
    loop(Key{"select"}, recipe_id(), select_event(), [](State&, Element& e, Element*, const Event& ev) {
        e.params.set("selected", std::floor(ev.args.num("line", -1.0)));
    });
}

void Modeler::set_files(sculpt::Files f) { files() = std::move(f); }

const std::string& Modeler::text() const {
    static const std::string none;
    const std::string* s = element(recipe_id()).params.text(Key{"ops"});
    return s ? *s : none;
}

int Modeler::count() const { return int(std::count(text().begin(), text().end(), '\n')); }

void Modeler::ports(StateGraph& g) const {
    for (Key e : {op_event(), set_event(), undo_event(), clear_event(), select_event()})
        if (!g.has_port(id(), e)) g.port(id(), e);
}

bool Modeler::fresh() {
    if (!built_) return false;
    if (element(recipe_id()).params.stamp() != built_stamp_) return false;
    for (std::size_t i = 0; i < built_->imports.size(); ++i)
        if (files().stamp && files().stamp(built_->imports[i]) != import_stamps_[i]) return false;
    return true;
}

const sculpt::Model& Modeler::model() {
    if (!fresh()) {
        const Params& p = element(recipe_id()).params;
        sculpt::Options o;
        o.cell = std::max(0.005, p.num(Key{"cell"}, 0.1));
        o.sides = int(p.num(Key{"sides"}, 24.0));
        o.crease = p.num(Key{"crease"}, 40.0);
        const std::string& t = text();
        const sculpt::Files* f = files().read ? &files() : nullptr;
        built_ = cached(memory_key(t, o), [&] { return build_kept(t, o, f); });
        built_stamp_ = p.stamp();
        import_stamps_.clear();
        for (const std::string& path : built_->imports) import_stamps_.push_back(files().stamp ? files().stamp(path) : 0);
    }
    return *built_;
}

std::vector<float> Modeler::fitted(Vec3d& size) {
    const sculpt::Model& m = model();
    return shapes::fit(m.all(), size);
}

std::vector<float> Modeler::fitted_part(std::size_t part, Vec3d& size) {
    const sculpt::Model& m = model();
    if (part >= m.parts.size()) return {};
    return shapes::fit(m.parts[part].corners, size, m.lo, m.hi);
}

}  // namespace sg
