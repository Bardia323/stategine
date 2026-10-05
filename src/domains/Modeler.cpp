#include "sg/domains/Modeler.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

#include "sg/core/StateGraph.hpp"
#include "sg/domains/Shapes.hpp"

namespace sg {

namespace {
sculpt::Files& files() {
    static sculpt::Files f;
    return f;
}

// Recipes already built, by what they are made from: the same text at the same
// settings is the same mesh, wherever it is asked for. Only ones that read no
// file (a file can change under the same words).
std::shared_ptr<const sculpt::Model> cached(const std::string& key, const std::function<sculpt::Model()>& make) {
    static std::mutex m;
    static std::unordered_map<std::string, std::shared_ptr<const sculpt::Model>> seen;
    {
        std::lock_guard<std::mutex> g(m);
        auto it = seen.find(key);
        if (it != seen.end()) return it->second;
    }
    auto built = std::make_shared<const sculpt::Model>(make());
    if (!built->imports.empty()) return built;
    std::lock_guard<std::mutex> g(m);
    if (seen.size() > 128) seen.clear();
    return seen.emplace(key, built).first->second;
}
}  // namespace

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
        const std::string key = std::to_string(o.cell) + "|" + std::to_string(o.sides) + "|" + std::to_string(o.crease) + "|" + std::to_string(sculpt::library_revision()) + "|" + text();
        const sculpt::Files* f = files().read ? &files() : nullptr;
        const std::string& t = text();
        built_ = cached(key, [&] { return sculpt::build(t, o, f); });
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
