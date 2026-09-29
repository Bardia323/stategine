#include "sg/domains/Look.hpp"

namespace sg::passes {

const std::vector<Key>& all() {
    static const std::vector<Key> list{shadow, scene, bright, blur, composite};
    return list;
}

}  // namespace sg::passes

namespace sg {

LookState::LookState(Key id) : State(id) {
    for (Key p : passes::all()) add_element(p, kinds::pass);
    params().set(look_keys::fade, 0.6);
}

auto LookState::uniform(Key pass, const std::string& name, double v) -> LookState& {
    element(pass).params.set(Key{name}, v);
    return *this;
}

auto LookState::uniform(Key pass, const std::string& name, double x, double y, double z) -> LookState& {
    Params& p = element(pass).params;
    p.set(Key{name + ".x"}, x).set(Key{name + ".y"}, y).set(Key{name + ".z"}, z);
    return *this;
}

auto LookState::setting(Key pass, const std::string& name, double v) -> LookState& {
    element(pass).params.set(Key{name}, v);
    return *this;
}

auto LookState::shader(Key pass, std::string fs, std::string vs) -> LookState& {
    element(pass).params.set(look_keys::fs, std::move(fs));
    if (!vs.empty()) element(pass).params.set(look_keys::vs, std::move(vs));
    return *this;
}

const Embedding& wear(StateGraph& g, Key host, Key look) {
    State& h = g.state(host);
    Element* slot = h.find(look_slot_id());
    if (!slot) slot = &h.add_element(look_slot_id(), kinds::look_slot);
    if (!slot->params.has(look_keys::active)) slot->params.set(look_keys::active, look.str());
    return g.embed(wear_embedding(host, look));
}

Embedding wear_embedding(Key host, Key look) {
    Embedding e;
    e.name = Key{host.str() + "/look:" + look.str()};
    e.host = host;
    e.portal = look_slot_id();
    e.guest = look;
    e.sync = EmbedSync::Commit;
    return e;
}

void set_look(State& host, Key look) {
    host.element(look_slot_id()).params.set(look_keys::active, look.str());
}

Key active_look(const State& s) {
    const Element* slot = s.find(look_slot_id());
    if (!slot) return Key{};
    return Key{slot->params.get_or<std::string>(look_keys::active, "")};
}

std::vector<Key> worn_looks(const StateGraph& g, Key host) {
    std::vector<Key> out;
    for (const Embedding& e : g.embeddings())
        if (e.host == host && e.portal == look_slot_id()) out.push_back(e.guest);
    return out;
}

float LookMix::weight(const LookState* l) const {
    for (const Part& p : parts)
        if (p.look == l) return p.weight;
    return 0.0f;
}

const LookState& LookMix::shown() const {
    const Part* best = &parts.front();
    for (const Part& p : parts)
        if (p.weight > best->weight) best = &p;
    return *best->look;
}

void LookMix::toward(const LookState& target, float step) {
    float held = weight(&target);
    if (held == 0.0f) parts.push_back(Part{&target, 0.0f});
    const float next = std::min(1.0f, held + step);
    const float rest = 1.0f - held;
    const float scale = rest > 1e-6f ? (1.0f - next) / rest : 0.0f;
    for (Part& p : parts) p.weight = p.look == &target ? next : p.weight * scale;
    parts.erase(std::remove_if(parts.begin(), parts.end(),
                               [](const Part& p) { return p.weight < 1e-5f; }),
                parts.end());
    if (parts.empty() || next >= 1.0f) {
        parts.assign(1, Part{&target, 1.0f});
        return;
    }
    float sum = 0.0f;
    for (const Part& p : parts) sum += p.weight;
    for (Part& p : parts) p.weight /= sum;  // keep the sum at one against rounding
}

const LookState& LookFader::look_of(const StateGraph* g, const State& s) const {
    if (g) {
        const Key id = active_look(s);
        if (!id.empty())
            if (const auto* l = dynamic_cast<const LookState*>(g->find(id))) return *l;
    }
    return *standard_;
}

const LookMix& LookFader::mix(Key who, const LookState& target) {
    auto it = fades_.find(who);
    if (it == fades_.end()) {
        Fade f;
        f.mix.parts.assign(1, LookMix::Part{&target, 1.0f});
        f.frame = frame_;
        return fades_.emplace(who, std::move(f)).first->second.mix;
    }
    Fade& f = it->second;
    if (f.frame != frame_) {
        f.frame = frame_;
        // A look that fades in at once (fade 0) is left at once too: what
        // it is shown as - a painting become its world - holds only in it,
        // and blended into another it would show as neither.
        double fade = target.fade_seconds();
        for (const LookMix::Part& p : f.mix.parts)
            if (p.look != &target && p.look->fade_seconds() <= 0.0) fade = 0.0;
        f.mix.toward(target, fade <= 0.0 ? 1.0f : static_cast<float>(dt_ / std::max(1e-3, fade)));
    }
    return f.mix;
}

void LookFader::settle(Key who, const LookState& target) {
    Fade& f = fades_[who];
    f.mix.parts.assign(1, LookMix::Part{&target, 1.0f});
    f.frame = frame_;
}

double LookFader::value(const LookState& l, Key pass, Key k, double fallback) const {
    if (const Element* e = l.find(pass))
        if (e->params.has(k)) return e->params.num(k, fallback);
    if (const Element* e = standard_->find(pass))
        if (e->params.has(k)) return e->params.num(k, fallback);
    return fallback;
}

double LookFader::value(const LookMix& m, Key pass, Key k, double fallback) const {
    double v = 0.0;
    for (const LookMix::Part& p : m.parts) v += p.weight * value(*p.look, pass, k, fallback);
    return v;
}

std::vector<std::string> look_defects(const StateGraph& g) {
    std::vector<std::string> out;
    for (const Embedding& e : g.embeddings()) {
        if (e.portal != look_slot_id()) continue;
        const State* guest = g.find(e.guest);
        if (guest && guest->kind() != kinds::look)
            out.push_back(e.host.str() + " wears " + e.guest.str() + ", which is not a look");
    }
    for (Key id : g.ids()) {
        const State& s = g.state(id);
        if (s.kind() == kinds::look) {
            for (const auto& el : s.elements()) {
                bool known = false;
                for (Key p : passes::all()) known = known || el.id == p;
                if (!known)
                    out.push_back("look " + id.str() + ": no pass named " + el.id.str());
            }
            continue;
        }
        if (!s.find(look_slot_id())) continue;
        const Key active = active_look(s);
        bool worn = false;
        for (Key k : worn_looks(g, id)) worn = worn || k == active;
        if (!worn)
            out.push_back(id.str() + " shows look " + (active.empty() ? "<none>" : active.str()) +
                          ", which it does not wear");
    }
    return out;
}

}  // namespace sg
