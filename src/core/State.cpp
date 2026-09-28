#include "sg/core/State.hpp"

namespace sg {

void State::step(const Tick& t, bool watch) {
    watching_ = watch;
    watched("on_update", [&] { on_update(t); });
    dispatch_pending();
    watched("on_render", [&] { on_render(t); });
    watching_ = false;
}

Element& State::add_element(Element e) {
    if (index_.count(e.id)) throw std::runtime_error("duplicate element " + e.id.str());
    restructured("add_element");
    index_.emplace(e.id, elements_.size());
    elements_.push_back(std::move(e));
    return elements_.back();
}

void State::remove_element(Key id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;
    restructured("remove_element");
    const std::size_t at = it->second;
    elements_.erase(elements_.begin() + static_cast<std::ptrdiff_t>(at));
    index_.erase(it);
    // Only those after it moved: their places are updated, the rest stand.
    for (std::size_t i = at; i < elements_.size(); ++i) index_[elements_[i].id] = i;
    ++removals_;
}

void State::remove_with_arrows(Key id) {
    remove_element(id);
    bool arrows = false;
    for (const Morphism& m : morphisms_) arrows = arrows || m.from == id || m.to == id;
    if (arrows) restructured("remove_with_arrows");
    const std::size_t had = morphisms_.size();
    for (std::size_t i = morphisms_.size(); i-- > 0;)
        if (morphisms_[i].from == id || morphisms_[i].to == id)
            morphisms_.erase(morphisms_.begin() + static_cast<std::ptrdiff_t>(i));
    if (morphisms_.size() != had) {
        ++removals_;
        index_arrows();
    }
}

const Morphism& State::add_morphism(Morphism m) {
    if (m.name.empty()) throw std::runtime_error("morphism needs a name");
    // A name is what a law, a functor and a path call the arrow by: two
    // arrows under one name would be one arrow to them, two to dispatch.
    if (by_name_.count(m.name))
        throw std::runtime_error("duplicate arrow " + m.name.str() + " in state " + id_.str());
    restructured("add_morphism");
    by_trigger_[m.trigger].push_back(morphisms_.size());
    by_name_.emplace(m.name, morphisms_.size());
    morphisms_.push_back(std::move(m));
    return morphisms_.back();
}

const Morphism& State::arrow(Key name, Key from, Key to, Key trigger, Morphism::Handler fn) {
    return add_morphism(Morphism{name, from, to, trigger, std::move(fn), {}, nullptr});
}

const Morphism& State::loop(Key name, Key on, Key trigger, Morphism::Handler fn) {
    return add_morphism(Morphism{name, on, Key{}, trigger, std::move(fn), {}, nullptr});
}

const Morphism& State::affine(Key name, Key from, Key to, Key trigger, Affine a) {
    auto steps = std::make_shared<const DeclaredSteps>(DeclaredSteps{DeclaredStep{from, to, std::move(a)}});
    Morphism m{name, from, to, trigger,
               [steps](State&, Element& src, Element* dst, const Event& ev) {
                   sg::run((*steps)[0].does, src, dst ? *dst : src, &ev.args);
               },
               {},
               nullptr};
    m.declared = steps;
    return add_morphism(std::move(m));
}

const Morphism* State::morphism(Key name) const {
    auto it = by_name_.find(name);
    return it == by_name_.end() ? nullptr : &morphisms_[it->second];
}

const Morphism& State::compose(Key name, Key f_name, Key g_name, Key trigger) {
    const Morphism* f = morphism(f_name);
    const Morphism* g = morphism(g_name);
    if (!f || !g) throw std::runtime_error("compose: unknown morphism");
    return add_morphism(composite(name, *f, *g, trigger));
}

Morphism State::composite(Key name, const Morphism& f, const Morphism& g, Key trigger) {
    if (cod(f) != dom(g))
        throw std::runtime_error("compose: cod(" + f.name.str() + ")=" + cod(f).str() +
                                 " != dom(" + g.name.str() + ")=" + dom(g).str());
    auto fh = f.handler;
    auto gh = g.handler;
    const Key f_to = f.to;
    const Key mid_id = cod(f);
    const Key g_to = g.to;
    const Key end_id = cod(g);
    Morphism m{name,
               f.from,
               end_id == f.from ? Key{} : end_id,
               trigger,
               [fh, gh, f_to, mid_id, g_to](State& s, Element& from, Element*,
                                            const Event& ev) {
                   if (fh) fh(s, from, f_to.empty() ? nullptr : s.find(f_to), ev);
                   Element* mid = s.find(mid_id);
                   if (!mid) return;
                   if (gh) gh(s, *mid, g_to.empty() ? nullptr : s.find(g_to), ev);
               },
               {f.name, g.name},
               nullptr};
    // Declared if both parts are: what each says, one after the other (an
    // arrow with no handler - an identity - says it does nothing).
    const auto says = [](const Morphism& x) -> const DeclaredSteps* {
        static const DeclaredSteps nothing;
        return x.declared ? x.declared.get() : x.handler ? nullptr : &nothing;
    };
    if (const DeclaredSteps* a = says(f); a) {
        if (const DeclaredSteps* b = says(g); b) {
            auto both = std::make_shared<DeclaredSteps>(*a);
            both->insert(both->end(), b->begin(), b->end());
            m.declared = std::move(both);
        }
    }
    return m;
}

auto State::snapshot() const -> Snapshot {
    return Snapshot{elements_, params_, bus_.queued(), said_out_, morphisms_.size(), structure_, removals_};
}

void State::restore(Snapshot s) {
    bool in_place = s.elements.size() <= elements_.size();
    for (std::size_t i = 0; in_place && i < s.elements.size(); ++i)
        in_place = elements_[i].id == s.elements[i].id;
    const bool same_structure =
        in_place && removals_ == s.removals && morphisms_.size() >= s.morphisms;
    if (in_place) {
        for (std::size_t i = 0; i < s.elements.size(); ++i)
            elements_[i] = std::move(s.elements[i]);
        while (elements_.size() > s.elements.size()) {
            index_.erase(elements_.back().id);
            elements_.pop_back();
        }
    } else {
        elements_ = std::move(s.elements);
        reindex();
        ++removals_;
    }
    params_ = std::move(s.params);
    bus_.requeue(std::move(s.queue));
    said_out_ = std::move(s.said);
    if (morphisms_.size() > s.morphisms) {
        morphisms_.erase(morphisms_.begin() + static_cast<std::ptrdiff_t>(s.morphisms),
                         morphisms_.end());
        index_arrows();
    }
    if (same_structure) structure_ = s.structure;
    else restructured("restore");
    on_restored();
}

uint64_t State::content_version() const {
    uint64_t h = data_version();
    const std::vector<Event>& q = bus_.queued();
    h = mix_stamp(h, q.size());
    for (const Event& e : q) {
        h = mix_stamp(h, std::hash<Key>{}(e.name));
        h = mix_stamp(h, std::hash<Key>{}(e.source));
        h = mix_stamp(h, e.args.stamp());
    }
    h = mix_stamp(h, said_out_.size());
    for (const Event& e : said_out_) {
        h = mix_stamp(h, std::hash<Key>{}(e.name));
        h = mix_stamp(h, e.args.stamp());
    }
    return h;
}

uint64_t State::data_version() const {
    uint64_t h = mix_stamp(0x9e3779b97f4a7c15ull, structure_);
    h = mix_stamp(h, params_.stamp());
    for (const Element& e : elements_) h = mix_stamp(h, (e.params.stamp() << 1) | (e.alive ? 1u : 0u));
    return h;
}

void State::emit(Event e) {
    if (detail::observing() > 0) detail::refused_to_observer(std::string("sent an event to a state: ") + e.name.str() + " to " + id_.str());
    if (e.source.empty()) e.source = id_;
    if (e.source == id_ && !said_.empty() && std::find(said_.begin(), said_.end(), e.name) != said_.end())
        said_out_.push_back(e);
    bus_.emit(std::move(e));
}

void State::hear(Event e) {
    if (detail::observing() > 0) detail::refused_to_observer(std::string("sent an event to a state: ") + e.name.str() + " to " + id_.str());
    if (e.source.empty()) e.source = id_;
    bus_.emit(std::move(e));
}

void State::says(Key event) {
    if (std::find(said_.begin(), said_.end(), event) == said_.end()) said_.push_back(event);
}

void State::dispatch_pending(int max_rounds) {
    for (int round = 0; round < max_rounds && !bus_.empty(); ++round) {
        bus_.drain_into(inbox_);
        for (const Event& ev : inbox_) {
            bool consumed = false;
            watched("on_event", [&] { consumed = on_event(ev); });
            if (consumed) continue;
            apply_morphisms(ev);
        }
    }
}

void State::apply_morphisms(const Event& ev) {
    auto it = by_trigger_.find(ev.name);
    if (it == by_trigger_.end()) return;
    // Copy the bucket: a handler may add morphisms mid-dispatch.
    scratch_ = it->second;
    for (std::size_t i : scratch_) {
        const Morphism& m = morphisms_[i];
        if (!m.handler) continue;
        Element* src = find(m.from);
        if (!src) continue;
        Element* dst = m.to.empty() ? nullptr : find(m.to);
        if (!m.to.empty() && !dst) continue;  // dangling arrow: skip
        m.handler(*this, *src, dst, ev);
    }
}

std::vector<std::string> State::validate() const {
    std::vector<std::string> errors;
    for (const auto& m : morphisms_) {
        if (!find(m.from))
            errors.push_back(id_.str() + "." + m.name.str() + ": domain " + m.from.str() +
                             " missing");
        if (!m.to.empty() && !find(m.to))
            errors.push_back(id_.str() + "." + m.name.str() + ": codomain " + m.to.str() +
                             " missing");
    }
    return errors;
}

void State::index_arrows() {
    by_trigger_.clear();
    by_name_.clear();
    for (std::size_t i = 0; i < morphisms_.size(); ++i) {
        by_trigger_[morphisms_[i].trigger].push_back(i);
        by_name_.emplace(morphisms_[i].name, i);
    }
}

void State::reindex() {
    index_.clear();
    for (std::size_t i = 0; i < elements_.size(); ++i) index_.emplace(elements_[i].id, i);
}

}  // namespace sg
