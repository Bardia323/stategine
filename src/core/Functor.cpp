#include "sg/core/Functor.hpp"

namespace sg::transport {

Declared only(std::vector<Key> names) {
    Affine a;
    for (Key k : names) a.copy(k, k);
    return Declared::of(std::move(a));
}

Declared swizzle(std::vector<std::pair<Key, Key>> pairs, bool copy_rest) {
    Affine a;
    a.copy_all = copy_rest;
    for (const auto& p : pairs) a.copy(p.first, p.second);
    return Declared::of(std::move(a));
}

Transport swizzle_scaled(std::vector<std::pair<Key, Key>> pairs, std::function<double(double)> fn, bool copy_rest) {
    return [pairs = std::move(pairs), fn = std::move(fn), copy_rest](const Element& s,
                                                                    Element& d) {
        if (copy_rest) copy_all(s, d);
        for (const auto& p : pairs)
            if (s.params.has(p.second)) d.params.set(p.first, fn(s.params.num(p.second)));
    };
}

Transport then(Transport a, Transport b) {
    return [a = std::move(a), b = std::move(b)](const Element& s, Element& d) {
        if (a) a(s, d);
        if (b) b(s, d);
    };
}

}  // namespace sg::transport

namespace sg {

Middle::Middle() : slot_(in_use()++) {
    auto& p = pool();
    if (p.size() <= slot_) p.emplace_back(new Element());
}

Element& Middle::element(const Element& like) {
    Element& e = *pool()[slot_];
    e.remake(like.id, like.kind);
    e.alive = true;
    e.params.clear();
    return e;
}

}  // namespace sg

namespace sg::transport {

void run_stages(const Stages& st, std::size_t i, const Element& s, Element& d) {
    if (i + 1 >= st.size()) {
        if (i < st.size()) sg::run(st[i], s, d);
        return;
    }
    Middle held;
    Element& mid = held.element(s);
    sg::run(st[i], s, mid);
    run_stages(st, i + 1, mid, d);
}

}  // namespace sg::transport

namespace sg {

auto Functor::operator=(const Functor& o) -> Functor& {
    if (this != &o) *this = Functor(o);
    return *this;
}

auto Functor::operator=(Functor&& o) -> Functor& {
    if (revision_ && o.name_ != name_)
        throw std::runtime_error("functor " + name_.str() + " is held by a graph under that name; " +
                                 "put " + o.name_.str() + " in with StateGraph::set_functor");
    if (revision_) revision_->rewired("assign");
    name_ = o.name_;
    from_ = o.from_;
    to_ = o.to_;
    identity_ = o.identity_;
    cleanup_ = o.cleanup_;
    stamp_ = o.stamp_;
    obj_ = std::move(o.obj_);
    mor_ = std::move(o.mor_);
    evt_ = std::move(o.evt_);
    return *this;
}

void Functor::rename(Key n) {
    if (revision_ && n != name_)
        throw std::runtime_error("functor " + name_.str() + " is held by a graph, which knows it by that name");
    name_ = n;
    remapped("rename");
}

auto Functor::on_object(Key src_element, Key dst_element, Transport t) -> Functor& {
    refuse_if_identity("on_object");
    // No transport is every parameter, as it is: said so.
    const bool said = !t;
    obj_[src_element] = ObjMap{dst_element, std::move(t), said ? transport::copy_all.stages : nullptr, Key{}};
    remapped("on_object");
    return *this;
}

auto Functor::on_object(Key src_element, Key dst_element, transport::Declared t) -> Functor& {
    refuse_if_identity("on_object");
    obj_[src_element] = ObjMap{dst_element, Transport(t), t.stages, Key{}};
    remapped("on_object");
    return *this;
}

auto Functor::on_object(Key src_element, Key dst_element, Transport t, Key native) -> Functor& {
    refuse_if_identity("on_object");
    obj_[src_element] = ObjMap{dst_element, std::move(t), nullptr, native};
    remapped("on_object");
    return *this;
}

auto Functor::footprint(Key src_element, Footprint fp) -> Functor& {
    auto it = obj_.find(src_element);
    if (it == obj_.end())
        throw std::runtime_error("functor " + name_.str() + ": " + src_element.str() + " is not mapped, so it has no footprint to say");
    it->second.footprint = std::make_shared<const Footprint>(std::move(fp));
    remapped("footprint");
    return *this;
}

const Footprint* Functor::footprint_of(Key src) const {
    auto it = obj_.find(src);
    return it == obj_.end() ? nullptr : it->second.footprint.get();
}

bool Functor::footprinted() const {
    if (identity_ || obj_.empty()) return false;
    for (const auto& kv : obj_)
        if (!kv.second.footprint) return false;
    return true;
}

uint64_t Functor::reads_stamp(const Memo::Pair& p) {
    uint64_t h = p.state_params ? p.state_params->stamp() : 0;
    for (const Element* e : p.reads) h = mix_stamp(h, e->params.stamp());
    return h;
}

Key Functor::native_of(Key src) const {
    auto it = obj_.find(src);
    return it == obj_.end() ? Key{} : it->second.native;
}

auto Functor::on_morphism(Key src_morphism, Key dst_morphism) -> Functor& {
    refuse_if_identity("on_morphism");
    mor_[src_morphism] = dst_morphism;
    remapped("on_morphism");
    return *this;
}

Key Functor::image_object(Key id) const {
    if (identity_) return id;
    auto it = obj_.find(id);
    return it == obj_.end() ? Key{} : it->second.dst;
}

Key Functor::image_morphism(Key id) const {
    if (identity_) return id;
    auto it = mor_.find(id);
    return it == mor_.end() ? Key{} : it->second;
}

Event Functor::carried(const Event& e, const State& src) const {
    Event out = e;
    out.name = image_event(e.name);
    out.source = Key{src.id().str() + "/" + name_.str()};
    return out;
}

Key Functor::image_event(Key name) const {
    auto it = evt_.find(name);
    return it == evt_.end() ? name : it->second;
}

void Functor::apply(const State& src, State& dst) const {
    if (identity_) {
        if (&src == &dst) return;
        for (const auto& s : src.elements()) {
            Element* d = dst.find(s.id);
            if (!d) d = &dst.add_element(s.id, s.kind);
            transport::copy_all(s, *d);
        }
        return;
    }
    for (const auto& kv : obj_) {
        const Element* s = src.find(kv.first);
        if (!s) continue;
        Element* d = dst.find(kv.second.dst);
        if (!d) d = &dst.add_element(kv.second.dst, s->kind);
        if (kv.second.transport) {
            kv.second.transport(*s, *d);
        } else {
            transport::copy_all(*s, *d);
        }
    }
}

std::size_t Functor::apply(const State& src, State& dst, Memo& m) const {
    if (!m.built || m.functor != this || m.src != &src || m.dst != &dst ||
        m.functor_stamp != stamp_ || m.src_structure != src.structure() ||
        m.dst_structure != dst.structure()) {
        apply(src, dst);
        build(src, dst, m);
        return m.pairs.size();
    }
    if (m.seen == last_stamp()) return 0;
    std::size_t carried = 0;
    for (Memo::Pair& p : m.pairs) {
        // Unchanged: its two elements, and whatever else its footprint says it reads.
        const uint64_t reads = p.state_params || !p.reads.empty() ? reads_stamp(p) : 0;
        if (p.src->params.stamp() == p.src_stamp && p.dst->params.stamp() == p.dst_stamp && reads == p.reads_stamp) continue;
        if (p.transport && *p.transport) {
            (*p.transport)(*p.src, *p.dst);
        } else {
            transport::copy_all(*p.src, *p.dst);
        }
        p.src_stamp = p.src->params.stamp();
        p.dst_stamp = p.dst->params.stamp();
        p.reads_stamp = p.state_params || !p.reads.empty() ? reads_stamp(p) : 0;
        ++carried;
    }
    // A transport that wrote a target another pair reads makes that pair
    // look again next time, as it must.
    m.seen = last_stamp();
    return carried;
}

void Functor::apply(const State& src, State& dst, const Event& e) const {
    apply(src, dst);
    dst.hear(carried(e, src));
}

void Functor::apply(const State& src, State& dst, const std::vector<Event>& carry) const {
    apply(src, dst);
    for (const Event& e : carry) {
        Event out = e;
        out.name = image_event(e.name);
        out.source = Key{src.id().str() + "/" + name_.str()};
        dst.emit(std::move(out));
    }
}

auto Functor::compose(const Functor& f, const Functor& g, Key name) -> Functor {
    if (f.to_ != g.from_)
        throw std::runtime_error("functor compose: cod(" + f.name_.str() + ")=" +
                                 f.to_.str() + " != dom(" + g.name_.str() + ")=" +
                                 g.from_.str());
    if (name.empty()) name = Key{g.name_.str() + "." + f.name_.str()};
    // Identities are units for composition by construction, not by luck.
    if (f.identity_ || g.identity_) {
        Functor h = f.identity_ ? g : f;
        h.name_ = name;
        h.evt_ = compose_events(f, g);
        return h;
    }
    Functor h(name, f.from_, g.to_);
    for (const auto& kv : f.obj_) {
        auto mid = g.obj_.find(kv.second.dst);
        if (mid == g.obj_.end()) continue;  // outside G's image: dropped
        Transport tf = kv.second.transport;
        Transport tg = mid->second.transport;
        // Declared if both are: the one's stages, then the other's.
        std::shared_ptr<const Stages> said;
        if (kv.second.declared && mid->second.declared) {
            auto both = std::make_shared<Stages>(*kv.second.declared);
            both->insert(both->end(), mid->second.declared->begin(), mid->second.declared->end());
            said = std::move(both);
        }
        h.obj_[kv.first] = ObjMap{mid->second.dst,
                                  [tf, tg](const Element& s, Element& d) {
                                      Middle held;
                                      Element& scratch = held.element(s);
                                      if (tf) {
                                          tf(s, scratch);
                                      } else {
                                          transport::copy_all(s, scratch);
                                      }
                                      if (tg) {
                                          tg(scratch, d);
                                      } else {
                                          transport::copy_all(scratch, d);
                                      }
                                  },
                                  std::move(said)};
    }
    for (const auto& kv : f.mor_) {
        auto mid = g.mor_.find(kv.second);
        if (mid != g.mor_.end()) h.mor_[kv.first] = mid->second;
    }
    h.evt_ = compose_events(f, g);
    return h;
}

std::unordered_map<Key, Key> Functor::compose_events(const Functor& f, const Functor& g) {
    std::unordered_map<Key, Key> out;
    for (const auto& kv : f.evt_) out[kv.first] = g.image_event(kv.second);
    for (const auto& kv : g.evt_)
        if (!f.evt_.count(kv.first)) out[kv.first] = kv.second;
    for (auto it = out.begin(); it != out.end();)
        it = it->first == it->second ? out.erase(it) : std::next(it);
    return out;
}

auto Functor::identity(Key state, Key name) -> Functor {
    if (name.empty()) name = Key{"id_" + state.str()};
    Functor id(name, state, state);
    id.identity_ = true;
    return id;
}

std::vector<std::string> Functor::check_laws(const State& src, const State& dst) const {
    std::vector<std::string> errors;
    const std::string tag = "functor " + name_.str() + ": ";
    if (identity_) {
        if (src.id() != from_ || dst.id() != to_ || from_ != to_)
            errors.push_back(tag + "an identity must be on one state");
        return errors;
    }
    for (const auto& kv : obj_)
        if (!src.find(kv.first))
            errors.push_back(tag + "object " + kv.first.str() + " missing in " +
                             src.id().str());

    for (const auto& kv : mor_) {
        const Morphism* f = src.morphism(kv.first);
        if (!f) {
            errors.push_back(tag + "arrow " + kv.first.str() + " missing in " +
                             src.id().str());
            continue;
        }
        const Morphism* img = dst.morphism(kv.second);
        if (!img) {
            errors.push_back(tag + "image arrow " + kv.second.str() + " missing in " +
                             dst.id().str());
            continue;
        }
        // Endomorphisms are arrows x -> x like any other: `to` is left empty
        // only as a storage convenience, so compare codomains, not fields.
        const Key want_dom = image_object(dom(*f));
        const Key want_cod = image_object(cod(*f));
        if (want_dom.empty()) {
            errors.push_back(tag + "domain " + dom(*f).str() + " of " + kv.first.str() +
                             " unmapped");
        } else if (dom(*img) != want_dom) {
            errors.push_back(tag + "F(" + kv.first.str() + ") has domain " + dom(*img).str() +
                             ", expected " + want_dom.str());
        }
        if (want_cod.empty()) {
            errors.push_back(tag + "codomain " + cod(*f).str() + " of " + kv.first.str() +
                             " unmapped");
        } else if (cod(*img) != want_cod) {
            errors.push_back(tag + "F(" + kv.first.str() + ") has codomain " + cod(*img).str() +
                             ", expected " + want_cod.str());
        }
    }
    return errors;
}

void Functor::build(const State& src, State& dst, Memo& m) const {
    m.pairs.clear();
    const auto pair = [&](const Element& s, Key to, const Transport* t, const Footprint* fp) {
        Element* d = dst.find(to);
        if (!d) return;
        Memo::Pair p{&s, d, t, s.params.stamp(), d->params.stamp()};
        // What else it says it reads: looked at, with its two elements,
        // to know whether it must run again.
        if (fp) {
            if (!fp->params.empty()) p.state_params = &src.params();
            for (Key k : fp->elements)
                if (const Element* r = src.find(k)) p.reads.push_back(r);
            p.reads_stamp = p.state_params || !p.reads.empty() ? reads_stamp(p) : 0;
        }
        m.pairs.push_back(std::move(p));
    };
    if (identity_) {
        if (&src != &dst)
            for (const auto& s : src.elements()) pair(s, s.id, nullptr, nullptr);
    } else {
        for (const auto& kv : obj_)
            if (const Element* s = src.find(kv.first)) pair(*s, kv.second.dst, &kv.second.transport, kv.second.footprint.get());
    }
    m.functor = this;
    m.src = &src;
    m.dst = &dst;
    m.functor_stamp = stamp_;
    m.src_structure = src.structure();
    m.dst_structure = dst.structure();
    m.seen = last_stamp();
    m.built = true;
}

void Functor::refuse_if_identity(const char* what) const {
    if (identity_)
        throw std::runtime_error("functor " + name_.str() + ": " + what +
                                 " on an identity would make it something else");
}

const Transport* Functor::transport_of(Key src) const {
    auto it = obj_.find(src);
    return identity_ || it == obj_.end() ? nullptr : &it->second.transport;
}

std::shared_ptr<const Stages> Functor::declared_of(Key src) const {
    auto it = obj_.find(src);
    return identity_ || it == obj_.end() ? nullptr : it->second.declared;
}

}  // namespace sg
