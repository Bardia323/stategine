#include "sg/core/Laws.hpp"

#include <array>
#include <cmath>

namespace sg {

std::string Violation::str() const {
    std::string s = law + " @ " + where + ": ";
    if (!detail.empty()) return s + detail + "  [" + lhs + "  vs  " + rhs + "]";
    s += state.str() + "." + element.str() + "." + key + " was " + before + "; " + lhs +
         " leaves " + left + ", " + rhs + " leaves " + right;
    if (!args.empty()) s += "  (event args " + args + ")";
    return s;
}

auto Path::arrow(Morphism m) -> Path& {
    const Key n = m.name;
    return push(Step{Step::Kind::Arrow, n, std::make_shared<const Morphism>(std::move(m)),
                     nullptr});
}

auto Path::functor(Functor f) -> Path& {
    const Key n = f.name();
    return push(Step{Step::Kind::Functor, n, nullptr,
                     std::make_shared<const Functor>(std::move(f))});
}

auto Path::transition(Key name) -> Path& {
    return push(Step{Step::Kind::Transition, name, nullptr, nullptr});
}

auto Path::arrow(Key name, Params args) -> Path& {
    return push(Step{Step::Kind::Arrow, name, nullptr, nullptr, std::move(args)});
}

auto Path::event(Key trigger, Params args) -> Path& {
    return push(Step{Step::Kind::Event, trigger, nullptr, nullptr, std::move(args)});
}

auto Path::event(const Event& ev) -> Path& {
    return push(Step{Step::Kind::Event, ev.name, nullptr, nullptr, ev.args, ev.source});
}

auto Path::then(const Path& p) -> Path& {
    for (const Step& s : p.steps_) steps_.push_back(s);
    return *this;
}

std::string Path::str() const {
    std::string s = element_.empty() ? state_.str() : state_.str() + "." + element_.str();
    if (steps_.empty()) return s + " [id]";
    s += " [";
    for (std::size_t i = 0; i < steps_.size(); ++i) {
        if (i) s += " ; ";
        if (steps_[i].kind == Step::Kind::Event) s += "!";
        s += steps_[i].name.str();
        if (steps_[i].args && !steps_[i].args->empty()) {
            std::string a;
            for (const auto& kv : *steps_[i].args)
                a += (a.empty() ? "" : ", ") + kv.first.str() + "=" + to_string(kv.second);
            s += "(" + a + ")";
        }
        if (!steps_[i].source.empty()) s += "@" + steps_[i].source.str();
    }
    return s + "]";
}

auto Diagram::commutes(Path lhs, Path rhs, Params args) -> Diagram& {
    equations_.push_back(Equation{"commutes", name_, std::move(lhs), std::move(rhs),
                                  std::move(args)});
    return *this;
}

std::size_t LawCache::memory() const {
    std::size_t b = 0;
    for (const auto& kv : entries_)
        b += sizeof(Entry) + kv.first.size() + kv.second.deps.size() * 8 +
             kv.second.result.size() * sizeof(Violation);
    for (const auto& kv : sides_) b += sizeof(Side) + kv.first.size() + kv.second.deps.size() * 8;
    return b + side_elements_ * (sizeof(Element) + 64);
}

void LawCache::begin(StateGraph& g) {
    g_ = &g;
    versions_.clear();
    functor_versions_.clear();
    for (const auto& kv : g.functors()) {
        const uint64_t v = kv.second.stamp();
        functor_versions_[kv.second.from()] = mix_stamp(functor_versions_[kv.second.from()], v);
        functor_versions_[kv.second.to()] = mix_stamp(functor_versions_[kv.second.to()], v);
    }
    ++round_;
}

void LawCache::deps_of(const Path& p, std::vector<uint64_t>& out) {
    Key here = p.state();
    std::vector<Key> stack;
    out.push_back(state_version(here));
    for (const Step& s : p.steps()) {
        if (s.kind == Step::Kind::Arrow || s.kind == Step::Kind::Event) continue;
        if (s.kind == Step::Kind::Transition) {
            const Transition* t = g_->transition(s.name);
            if (!t || (t->kind == TransitionKind::Pop && stack.empty())) {
                out.push_back(0);
                continue;
            }
            const Functor* f = t->functor.empty() ? nullptr : g_->functor(t->functor);
            out.push_back(f ? f->stamp() : 0);
            if (t->kind == TransitionKind::Push) stack.push_back(here);
            if (t->kind == TransitionKind::Pop) {
                here = stack.back();
                stack.pop_back();
            } else {
                here = t->to;
            }
        } else if (s.functor) {
            out.push_back(functor_version(s.functor->from()));
            out.push_back(functor_version(s.functor->to()));
            here = s.functor->to();
        } else {
            const Functor* f = g_->functor(s.name);
            out.push_back(f ? f->stamp() : 0);
            if (!f) continue;
            here = f->to();
        }
        out.push_back(state_version(here));
    }
}

bool LawCache::direct(const Entry& e) const {
    if (strategy_ == Strategy::Direct) return true;
    if (strategy_ == Strategy::Incremental) return false;
    return round_ < e.direct_until;
}

void LawCache::judge(Entry& e, bool hit) {
    if (strategy_ != Strategy::Adaptive) return;
    e.misses = hit ? 0 : e.misses + 1;
    // Cheaper to run than to keep, or never the same twice: stop keeping
    // for a while, then look again - the data may have settled.
    if (e.cost_ns < e.keep_ns || e.misses >= 4) {
        e.direct_until = round_ + 16;
        e.misses = 0;
        e.kept = false;
        e.result.clear();
        e.deps.clear();
    }
}

auto LawCache::side(const std::string& key, const std::vector<uint64_t>& deps) const -> const Side* {
    auto it = sides_.find(key);
    if (it == sides_.end() || it->second.deps != deps) return nullptr;
    return &it->second;
}

void LawCache::keep_side(const std::string& key, std::vector<uint64_t> deps, std::shared_ptr<const laws::Outcome> o, std::size_t elements) {
    auto it = sides_.find(key);
    if (it != sides_.end()) {
        side_elements_ -= it->second.elements;
        sides_.erase(it);
    }
    if (side_elements_ + elements > side_budget_) return;
    side_elements_ += elements;
    sides_.emplace(key, Side{std::move(deps), std::move(o), elements});
}

uint64_t LawCache::state_version(Key id) {
    const State* s = g_->find(id);
    if (!s) return 0;
    auto it = versions_.find(s);
    if (it != versions_.end()) return it->second;
    const uint64_t v = s->content_version();
    versions_.emplace(s, v);
    return v;
}

uint64_t LawCache::functor_version(Key state) {
    auto it = functor_versions_.find(state);
    return it == functor_versions_.end() ? 0 : it->second;
}

}  // namespace sg

namespace sg::laws {

Trial::~Trial() {
    --detail::trials();
    sealed_.reset();
    for (auto& kv : saved_)
        if (State* s = g_.find(kv.first)) s->restore(std::move(kv.second));
}

State& Trial::touch(Key id) {
    State& s = g_.state(id);
    if (!saved_.count(id)) saved_.emplace(id, s.snapshot());
    return s;
}

Params args_for(const LawOptions& o, Key trigger) {
    auto it = o.args_for.find(trigger);
    return it == o.args_for.end() ? o.args : it->second;
}

Outcome run_steps(StateGraph& g, const Path& p, const Params& args) {
    Outcome out;
    Trial trial(g);
    Key here = p.state();
    Key at = p.element();
    std::vector<Key> stack;  // where the path's pushes came from, for its pops
    if (!g.find(here)) {
        out.error = "starts in unknown state " + here.str();
        return out;
    }
    if (!at.empty() && !g.state(here).find(at)) {
        out.error = "starts at " + at.str() + ", which " + here.str() + " does not have";
        return out;
    }

    for (const Step& step : p.steps()) {
        const std::string tag = "step " + step.name.str() + ": ";
        if (step.kind == Step::Kind::Arrow) {
            State& s = trial.touch(here);
            const Morphism* m = step.arrow ? step.arrow.get() : s.morphism(step.name);
            if (!m) {
                out.error = tag + "no such arrow in " + here.str();
                return out;
            }
            if (!at.empty() && dom(*m) != at) {
                out.error = tag + "starts at " + dom(*m).str() + ", but the path is at " + at.str();
                return out;
            }
            Element* src = s.find(dom(*m));
            Element* dst = m->to.empty() ? nullptr : s.find(m->to);
            if (!src || (!m->to.empty() && !dst)) {
                out.error = tag + "an endpoint is missing from " + here.str();
                return out;
            }
            Event ev{m->trigger, step.args ? *step.args : args};
            ev.source = here;
            if (m->handler) m->handler(s, *src, dst, ev);
            at = cod(*m);
            continue;
        }
        if (step.kind == Step::Kind::Event) {
            // This event and what it sets in motion, apart from what was
            // already queued - which is put back ahead of anything left over.
            State& s = trial.touch(here);
            std::vector<Event> queued = s.bus().queued();
            Event ev{step.name, step.args ? *step.args : args};
            ev.source = step.source.empty() ? here : step.source;
            s.bus().requeue({std::move(ev)});
            s.dispatch_pending();
            for (const Event& left : s.bus().queued()) queued.push_back(left);
            s.bus().requeue(std::move(queued));
            at = Key{};  // an event acts on the state as a whole
            continue;
        }

        const Functor* f = nullptr;
        const Transition* t = nullptr;
        Key target;
        Event crossing;
        if (step.kind == Step::Kind::Transition) {
            // Taken as the engine takes it: by the event that triggers it, only
            // if the engine would choose this one on that event here (its
            // guard passes, and nothing it would prefer does), running its
            // action, carrying the event, and entering where it goes.
            t = g.transition(step.name);
            if (!t) {
                out.error = tag + "no such transition";
                return out;
            }
            if (t->from != StateGraph::any() && t->from != here) {
                out.error = tag + "leaves " + t->from.str() + ", but the path is in " + here.str();
                return out;
            }
            crossing = Event{t->trigger, step.args ? *step.args : args};
            crossing.source = step.source;
            const Transition* taken = g.resolve(trial.touch(here), crossing);
            if (taken != t) {
                out.error = tag + (taken ? "on " + t->trigger.str() + " here the engine takes " + taken->name.str()
                                         : "its guard refuses it here");
                return out;
            }
            if (t->kind == TransitionKind::Pop) {
                if (stack.empty()) {
                    out.error = tag + "a pop goes back where the path came from, and this path was pushed from nowhere";
                    return out;
                }
                target = stack.back();
            } else {
                target = t->to;
            }
            if (!t->functor.empty()) {
                f = g.functor(t->functor);
                if (!f) {
                    out.error = tag + "unknown functor " + t->functor.str();
                    return out;
                }
            }
        } else {
            f = step.functor ? step.functor.get() : g.functor(step.name);
            if (!f) {
                out.error = tag + "no such functor";
                return out;
            }
            target = f->to();
        }

        if (f && (f->from() != here || f->to() != target)) {
            out.error = tag + "is " + f->from().str() + " -> " + f->to().str() +
                        ", but the path is in " + here.str();
            return out;
        }
        if (!g.find(target)) {
            out.error = tag + "lands in unknown state " + target.str();
            return out;
        }
        Key image;
        if (!at.empty()) {
            image = f ? f->image_object(at) : Key{};
            if (f && image.empty()) {
                out.error = tag + "does not map " + at.str();
                return out;
            }
        }
        if (t) {
            State& from = trial.touch(here);
            State& to = trial.touch(target);
            Params entered;
            g.cross(*t, from, &to, crossing, entered);
            switch (t->kind) {
                case TransitionKind::Switch:
                    from.on_exit();
                    to.on_enter(entered);
                    break;
                case TransitionKind::Push:
                    stack.push_back(here);
                    from.on_pause();
                    to.on_enter(entered);
                    break;
                case TransitionKind::Pop:
                    stack.pop_back();
                    from.on_exit();
                    to.on_resume();
                    break;
            }
        } else if (f) {
            f->apply(g.state(here), trial.touch(target));
        }
        here = target;
        at = image;
    }

    out.state = here;
    out.element = at;
    // What the path left, taken rather than copied where the trial is about
    // to put the state back anyway.
    State& end = g.state(here);
    if (trial.touched(here)) {
        out.data.params = end.params();
        out.data.queue = end.bus().queued();
        out.data.said = end.said_out();
        for (Element& e : end.elements()) out.data.elements.push_back(std::move(e));
    } else {
        out.data = end.snapshot();
    }
    return out;
}

Outcome run(StateGraph& g, const Path& p, const Params& args) {
    try {
        return run_steps(g, p, args);
    } catch (const RewriteRefused& refused) {
        Outcome out;
        out.error = refused.what();
        out.refused = true;
        return out;
    }
}

bool same_value(Key k, const Value& a, const Value& b) {
    // Same kind of value: compare it as itself. Formatting both as text, as
    // this once did, made checking a world of a few hundred elements slow.
    if (a.index() == b.index()) {
        if (const double* x = std::get_if<double>(&a)) {
            const double y = std::get<double>(b);
            return *x == y || same_number(k, *x, y);
        }
        return a == b;
    }
    const auto numeric = [](const Value& v, double& out) {
        if (const double* d = std::get_if<double>(&v)) return out = *d, true;
        if (const int64_t* i = std::get_if<int64_t>(&v))
            return out = static_cast<double>(*i), true;
        return false;
    };
    double x = 0, y = 0;
    return numeric(a, x) && numeric(b, y) && same_number(k, x, y);
}

std::string args_str(const Params& p) {
    std::string s;
    for (const auto& kv : p) s += (s.empty() ? "" : ", ") + kv.first.str() + "=" + to_string(kv.second);
    return s.empty() ? s : "{" + s + "}";
}

bool same_params(const Params& a, const Params& b) {
    if (a.stamp() == b.stamp()) return true;  // the same content (see Stamps in Core.hpp)
    if (a.size() != b.size()) return false;
    for (const auto& kv : a)
        if (!b.has(kv.first) || !same_value(kv.first, kv.second, b.get(kv.first))) return false;
    return true;
}

bool same_event(const Event& a, const Event& b) {
    return a.name == b.name && a.source == b.source && same_params(a.args, b.args);
}

std::string event_str(const Event& e) {
    return e.name.str() + args_str(e.args) + (e.source.empty() ? "" : "@" + e.source.str());
}

std::vector<Violation> diff(const Equation& eq, const Outcome& l, const Outcome& r, const State::Snapshot& before) {
    std::vector<Violation> out;
    Violation base;
    base.law = eq.law;
    base.where = eq.where;
    base.lhs = eq.lhs.str();
    base.rhs = eq.rhs.str();
    base.args = args_str(eq.args);

    const auto fail = [&](std::string why) {
        Violation v = base;
        v.detail = std::move(why);
        out.push_back(std::move(v));
    };
    // A side that would change the graph's structure was stopped: the
    // equation is not false, it cannot be checked - said once, as that.
    if (l.refused || r.refused) {
        Violation v = base;
        v.refused = true;
        v.detail = "cannot be checked on trial: " + (l.refused ? l.error : r.error);
        out.push_back(std::move(v));
        return out;
    }
    if (!l.error.empty()) fail("left side does not run: " + l.error);
    if (!r.error.empty()) fail("right side does not run: " + r.error);
    if (!out.empty()) return out;
    if (l.state != r.state) {
        fail("the sides end in different states, " + l.state.str() + " and " + r.state.str());
        return out;
    }
    if (!l.element.empty() && !r.element.empty() && l.element != r.element) {
        fail("the sides end at different objects, " + l.element.str() + " and " +
             r.element.str());
        return out;
    }

    // Most equations hold, and most of the data neither side touched: when the
    // two sides left the same elements, in the same order, each with the same
    // stamp (the same content), and queued the same, they agree - found with
    // no index built and no value compared.
    if (l.data.elements.size() == r.data.elements.size() && l.data.queue.size() == r.data.queue.size() &&
        l.data.said.size() == r.data.said.size()) {
        bool same = true;
        for (std::size_t i = 0; same && i < l.data.elements.size(); ++i) {
            const Element& a = l.data.elements[i];
            const Element& b = r.data.elements[i];
            same = a.id == b.id && a.alive == b.alive && a.params.stamp() == b.params.stamp();
        }
        for (std::size_t i = 0; same && i < l.data.queue.size(); ++i)
            same = same_event(l.data.queue[i], r.data.queue[i]);
        for (std::size_t i = 0; same && i < l.data.said.size(); ++i)
            same = same_event(l.data.said[i], r.data.said[i]);
        if (same) return out;
    }

    // Elements by id, looked up rather than searched for: every element of
    // every snapshot is visited, so a scan per lookup would be quadratic.
    const auto index = [](const State::Snapshot& s) {
        std::unordered_map<Key, const Element*> m;
        m.reserve(s.elements.size());
        for (const auto& e : s.elements) m.emplace(e.id, &e);
        return m;
    };
    const auto in_l = index(l.data), in_r = index(r.data), in_before = index(before);
    const auto find = [&](const State::Snapshot& s, Key id) -> const Element* {
        const auto& m = &s == &l.data ? in_l : (&s == &r.data ? in_r : in_before);
        const auto it = m.find(id);
        return it == m.end() ? nullptr : it->second;
    };
    const auto value = [](const Element* e, Key k) {
        return e && e->params.has(k) ? to_string(e->params.get(k)) : std::string("<unset>");
    };
    const auto at = [&](Key id, std::string key, std::string was, std::string a, std::string b) {
        Violation v = base;
        v.state = l.state;
        v.element = id;
        v.key = std::move(key);
        v.before = std::move(was);
        v.left = std::move(a);
        v.right = std::move(b);
        out.push_back(std::move(v));
    };

    std::vector<Key> ids;
    for (const auto& e : l.data.elements) ids.push_back(e.id);
    for (const auto& e : r.data.elements)
        if (!find(l.data, e.id)) ids.push_back(e.id);

    for (Key id : ids) {
        const Element* a = find(l.data, id);
        const Element* b = find(r.data, id);
        const Element* was = find(before, id);
        if (!a || !b) {
            at(id, "<element>", was ? "present" : "absent", a ? "present" : "absent",
               b ? "present" : "absent");
            continue;
        }
        if (a->alive != b->alive)
            at(id, "<alive>", was ? (was->alive ? "true" : "false") : "absent",
               a->alive ? "true" : "false", b->alive ? "true" : "false");
        // The same stamp is the same content (see Stamps in Core.hpp): an
        // element neither side wrote to needs no comparing.
        if (a->params.stamp() == b->params.stamp()) continue;
        std::vector<Key> keys;
        for (const auto& kv : a->params) keys.push_back(kv.first);
        for (const auto& kv : b->params)
            if (!a->params.has(kv.first)) keys.push_back(kv.first);
        for (Key k : keys) {
            const bool ha = a->params.has(k), hb = b->params.has(k);
            if (ha && hb && same_value(k, a->params.get(k), b->params.get(k))) continue;
            at(id, k.str(), value(was, k), value(a, k), value(b, k));
        }
    }

    // What the paths set in motion is part of what they did: each event, name,
    // arguments and sender, in the order queued - the queue is dispatched in
    // that order, so A then B is not B then A.
    const auto names = [](const std::vector<Event>& q) {
        std::vector<std::string> n;
        for (const auto& e : q) n.push_back(event_str(e));
        return n;
    };
    const auto same_queues = [](const std::vector<Event>& a, const std::vector<Event>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (!same_event(a[i], b[i])) return false;
        return true;
    };
    const auto joined = [](const std::vector<std::string>& n) {
        std::string s;
        for (const auto& x : n) s += (s.empty() ? "" : ", ") + x;
        return "{" + s + "}";
    };
    if (!same_queues(l.data.queue, r.data.queue))
        at(Key{}, "<emitted>", joined(names(before.queue)), joined(names(l.data.queue)),
           joined(names(r.data.queue)));
    // And what they said outward, which the graph's transitions will hear.
    if (!same_queues(l.data.said, r.data.said))
        at(Key{}, "<said>", joined(names(before.said)), joined(names(l.data.said)),
           joined(names(r.data.said)));
    return out;
}

std::vector<Violation> settle(StateGraph& g, const Equation& eq, const Outcome& l, const Outcome& r) {
    static const State::Snapshot nothing;
    std::vector<Violation> out = diff(eq, l, r, nothing);
    if (out.empty() || l.state.empty() || !g.find(l.state)) return out;
    return diff(eq, l, r, g.state(l.state).snapshot());
}

std::vector<Violation> check_direct(StateGraph& g, const Equation& eq) {
    const Outcome l = run(g, eq.lhs, eq.args);
    const Outcome r = run(g, eq.rhs, eq.args);
    return settle(g, eq, l, r);
}

std::vector<Violation> check(StateGraph& g, LawCache* cache, const Equation& eq) {
    if (Accelerator* a = accelerating(); a && a->take(g, eq)) return {};
    if (!cache || cache->strategy() == LawCache::Strategy::Direct) return check_direct(g, eq);
    using clock = std::chrono::steady_clock;
    const auto asked = clock::now();
    LawCache::Stats& st = cache->tally();
    ++st.equations;
    const std::string args = args_str(eq.args);
    const std::string lhs = eq.lhs.str(), rhs = eq.rhs.str();
    const std::string key = eq.law + "|" + eq.where + "|" + lhs + "|" + rhs + "|" + args;
    if (cache->direct(cache->entry(key))) {
        ++st.direct;
        const auto t = clock::now();
        std::vector<Violation> out = check_direct(g, eq);
        st.run_ns += std::chrono::duration<double, std::nano>(clock::now() - t).count();
        return out;
    }
    std::vector<uint64_t> ldeps, rdeps;
    cache->deps_of(eq.lhs, ldeps);
    cache->deps_of(eq.rhs, rdeps);
    std::vector<uint64_t> deps = ldeps;
    deps.insert(deps.end(), rdeps.begin(), rdeps.end());
    return kept(*cache, key, asked, std::move(deps), [&] {
        // One side may be what it was - the same path, on the same data -
        // even though the equation as a whole is not: run only the other.
        const auto side = [&](const Path& p, const std::string& text, std::vector<uint64_t>& d) {
            const std::string k = text + "|" + args;
            if (const LawCache::Side* s = cache->side(k, d)) {
                ++st.sides_reused;
                return s->outcome;
            }
            auto o = std::make_shared<const Outcome>(run(g, p, eq.args));
            cache->keep_side(k, std::move(d), o, o->data.elements.size());
            return std::shared_ptr<const Outcome>(o);
        };
        const std::shared_ptr<const Outcome> l = side(eq.lhs, lhs, ldeps);
        const std::shared_ptr<const Outcome> r = side(eq.rhs, rhs, rdeps);
        return settle(g, eq, *l, *r);
    });
}

std::vector<Violation> check(StateGraph& g, const Equation& eq) {
    if (Accelerator* a = accelerating(); a && a->take(g, eq)) return {};
    return check_direct(g, eq);
}

void append(std::vector<Violation>& to, std::vector<Violation> from) {
    for (auto& v : from) to.push_back(std::move(v));
}

std::vector<Violation> identity(StateGraph& g, const LawOptions& o, LawCache* cache) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (Key sid : g.ids()) {
        const State& s = g.state(sid);
        for (const Morphism& m : s.morphisms()) {
            if (!s.find(dom(m)) || !s.find(cod(m))) continue;  // validate() names it
            const Morphism id_dom{Key{"id." + dom(m).str()}, dom(m), Key{}, m.trigger, nullptr, {}, nullptr};
            const Morphism id_cod{Key{"id." + cod(m).str()}, cod(m), Key{}, m.trigger, nullptr, {}, nullptr};
            const Params args = args_for(o, m.trigger);
            const std::string where = sid.str() + "." + m.name.str();
            const Path f = Path(sid, dom(m)).arrow(m.name);
            append(out, check(g, cache, {"identity", where,
                                  Path(sid, dom(m)).arrow(State::composite(
                                      Key{m.name.str() + ".id"}, id_dom, m, m.trigger)),
                                  f, args}));
            append(out, check(g, cache, {"identity", where,
                                  Path(sid, dom(m)).arrow(State::composite(
                                      Key{"id." + m.name.str()}, m, id_cod, m.trigger)),
                                  f, args}));
        }
    }
    for (const auto& kv : g.functors()) {
        const Functor& f = kv.second;
        if (!g.find(f.from()) || !g.find(f.to())) continue;
        const Path plain = Path(f.from()).functor(f.name());
        append(out, check(g, cache, {"identity", "functor " + f.name().str(),
                              Path(f.from()).functor(Functor::compose(
                                  Functor::identity(f.from()), f, Key{f.name().str() + ".id"})),
                              plain, o.args}));
        append(out, check(g, cache, {"identity", "functor " + f.name().str(),
                              Path(f.from()).functor(Functor::compose(
                                  f, Functor::identity(f.to()), Key{"id." + f.name().str()})),
                              plain, o.args}));
    }
    return out;
}

Violation bounded(const std::string& law, const std::string& where, std::size_t budget) {
    Violation v;
    v.law = law;
    v.where = where;
    v.detail = "stopped at the budget of " + std::to_string(budget) +
               " triples (LawOptions::max_triples); the rest were not checked";
    v.bounded = true;
    return v;
}

std::vector<Violation> associativity(StateGraph& g, const LawOptions& o, LawCache* cache) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (Key sid : g.ids()) {
        const State& s = g.state(sid);
        std::unordered_map<Key, std::vector<const Morphism*>> leaving;
        for (const Morphism& m : s.morphisms())
            if (s.find(dom(m)) && s.find(cod(m))) leaving[dom(m)].push_back(&m);
        std::size_t budget = o.max_triples;
        const auto triple = [&](const Morphism& f, const Morphism& gm, const Morphism& h) {
            const Key t = f.trigger;
            const Morphism fg =
                State::composite(Key{"(" + f.name.str() + ";" + gm.name.str() + ")"}, f, gm, t);
            const Morphism gh =
                State::composite(Key{"(" + gm.name.str() + ";" + h.name.str() + ")"}, gm, h, t);
            const Morphism left =
                State::composite(Key{fg.name.str() + ";" + h.name.str()}, fg, h, t);
            const Morphism right =
                State::composite(Key{f.name.str() + ";" + gh.name.str()}, f, gh, t);
            const std::string where =
                sid.str() + ": " + f.name.str() + ", " + gm.name.str() + ", " + h.name.str();
            const Params args = args_for(o, t);
            append(out, check(g, cache, {"associativity", where, Path(sid, dom(f)).arrow(left),
                                  Path(sid, dom(f)).arrow(right), args}));
            append(out, check(g, cache, {"associativity", where, Path(sid, dom(f)).arrow(left),
                                  Path(sid, dom(f)).arrow(f.name).arrow(gm.name).arrow(h.name),
                                  args}));
        };
        for (const Morphism& f : s.morphisms()) {
            if (!s.find(dom(f)) || !s.find(cod(f))) continue;
            for (const Morphism* gm : leaving[cod(f)])
                for (const Morphism* h : leaving[cod(*gm)]) {
                    if (budget == 0) {
                        out.push_back(bounded("associativity", sid.str(), o.max_triples));
                        goto next_state;
                    }
                    --budget;
                    triple(f, *gm, *h);
                }
        }
    next_state:;
    }

    std::size_t budget = o.max_triples;
    for (const auto& a : g.functors())
        for (const auto& b : g.functors()) {
            if (a.second.to() != b.second.from()) continue;
            for (const auto& c : g.functors()) {
                if (b.second.to() != c.second.from()) continue;
                if (budget-- == 0) {
                    out.push_back(bounded("associativity", "functors", o.max_triples));
                    return out;
                }
                const Functor& f = a.second;
                const Functor& gf = b.second;
                const Functor& h = c.second;
                const Functor left = Functor::compose(Functor::compose(f, gf), h,
                                                      Key{"(" + f.name().str() + ";" +
                                                          gf.name().str() + ");" + h.name().str()});
                const Functor right = Functor::compose(f, Functor::compose(gf, h),
                                                       Key{f.name().str() + ";(" +
                                                           gf.name().str() + ";" + h.name().str() +
                                                           ")"});
                append(out, check(g, cache, {"associativity",
                                      "functors " + f.name().str() + ", " + gf.name().str() +
                                          ", " + h.name().str(),
                                      Path(f.from()).functor(left), Path(f.from()).functor(right),
                                      o.args}));
            }
        }
    return out;
}

std::vector<Violation> composition(StateGraph& g, const LawOptions& o, LawCache* cache) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (Key sid : g.ids()) {
        const State& s = g.state(sid);
        for (const Morphism& m : s.morphisms()) {
            if (m.parts.empty() || !s.find(dom(m))) continue;
            // Flatten nested composites into the arrows actually declared.
            std::vector<Key> flat;
            std::vector<Key> todo(m.parts.rbegin(), m.parts.rend());
            while (!todo.empty()) {
                const Key k = todo.back();
                todo.pop_back();
                const Morphism* part = s.morphism(k);
                if (part && !part->parts.empty()) {
                    for (auto it = part->parts.rbegin(); it != part->parts.rend(); ++it)
                        todo.push_back(*it);
                } else {
                    flat.push_back(k);
                }
            }
            Path chain(sid, dom(m));
            for (Key k : flat) chain.arrow(k);
            append(out, check(g, cache, {"composition", sid.str() + "." + m.name.str(),
                                  Path(sid, dom(m)).arrow(m.name), chain,
                                  args_for(o, m.trigger)}));
        }
    }
    for (const auto& kv : g.functors()) {
        const std::vector<Key>* chain = g.composite_chain(kv.first);
        if (!chain) continue;
        Path steps(kv.second.from());
        for (Key k : *chain) steps.functor(k);
        append(out, check(g, cache, {"composition", "functor " + kv.first.str(),
                              Path(kv.second.from()).functor(kv.first), steps, o.args}));
    }
    return out;
}

std::vector<Violation> functoriality(StateGraph& g, const LawOptions& o, LawCache* cache) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (const auto& kv : g.functors()) {
        const Functor& F = kv.second;
        const State* a = g.find(F.from());
        const State* b = g.find(F.to());
        if (!a || !b || F.is_identity()) continue;
        F.for_each_morphism([&](Key src, Key dst) {
            const Morphism* f = a->morphism(src);
            const Morphism* Ff = b->morphism(dst);
            if (!f || !Ff) return;  // validate() names it
            if (!a->find(dom(*f)) || !a->find(cod(*f))) return;
            if (F.image_object(dom(*f)) != dom(*Ff) || F.image_object(cod(*f)) != cod(*Ff))
                return;  // so does check_laws
            append(out, check(g, cache, {"functoriality",
                                  "functor " + F.name().str() + " on " + src.str(),
                                  Path(F.from(), dom(*f)).arrow(src).functor(F.name()),
                                  Path(F.from(), dom(*f)).functor(F.name()).arrow(dst),
                                  args_for(o, f->trigger)}));
        });
    }
    return out;
}

std::vector<Violation> drives(StateGraph& g, const LawOptions& o, LawCache* cache) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (const Drive& d : g.drives()) {
        const State* c = g.find(d.clock);
        const Element* now = c ? c->find(timeline_of(d)) : nullptr;
        if (!now || !g.find(d.state)) continue;  // validate() names it
        const double t0 = now->params.num(keys::time);
        const int64_t f0 = now->params.get_or<int64_t>(keys::frame, 0);
        // Built as the engine builds it (drive_event), with any probe
        // arguments the caller gives the trigger beneath.
        const auto when = [&](double dt, double t, int64_t f) {
            Event ev = drive_event(d, dt, t, f);
            Params p = args_for(o, d.trigger);
            for (const auto& kv : ev.args) p.set(kv.first, kv.second);
            ev.args = std::move(p);
            return ev;
        };
        const std::string where = "drive " + d.name.str();
        append(out, check(g, cache, {"drive", where, Path(d.state).event(when(0.0, t0, f0 + 1)),
                                     Path(d.state), {}}));
        if (!d.additive) continue;
        const double a = o.drive_dt;
        append(out, check(g, cache, {"drive", where,
                                     Path(d.state)
                                         .event(when(a, t0 + a, f0 + 1))
                                         .event(when(a, t0 + 2 * a, f0 + 2)),
                                     Path(d.state).event(when(2 * a, t0 + 2 * a, f0 + 1)), {}}));
    }
    return out;
}

std::vector<Violation> lenses(StateGraph& g, const LawOptions& o, LawCache* cache) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    // Every embedding with both ways, and every pair declared a lens on its
    // own (StateGraph::lens) - as a closed embedding of the one in the other.
    std::deque<Embedding> pairs(g.embeddings().begin(), g.embeddings().end());
    for (const auto& l : g.lenses()) {
        const Functor* get = g.functor(l.get);
        if (!get) continue;
        Embedding e;
        e.name = Key{"lens " + l.get.str() + "/" + l.put.str()};
        e.host = get->from();
        e.guest = get->to();
        e.in = l.get;
        e.out = l.put;
        pairs.push_back(e);
    }
    for (const Embedding& e : pairs) {
        if (e.in.empty() || e.out.empty()) continue;
        const Functor* in = g.functor(e.in);
        const Functor* put = g.functor(e.out);
        const Key subject = e.subject.empty() ? e.host : e.subject;
        if (!in || !put || !g.find(subject) || !g.find(e.guest)) continue;
        if (in->from() != subject || in->to() != e.guest || put->from() != e.guest ||
            put->to() != subject)
            continue;  // validate() names it
        const std::string where = "embedding " + e.name.str();

        append(out, check(g, cache, {"put-get", where,
                              Path(subject).functor(e.in).functor(e.out).functor(e.in),
                              Path(subject).functor(e.in), o.args}));
        if (e.open)
            append(out, check(g, cache, {"put-get", where + " (live edits)",
                                  Path(e.guest).functor(e.out).functor(e.in), Path(e.guest),
                                  o.args}));
        append(out, check(g, cache, {"settles", where,
                              Path(subject).functor(e.in).functor(e.out).functor(e.in).functor(
                                  e.out),
                              Path(subject).functor(e.in).functor(e.out), o.args}));

        // put ; put against put, from the same guest: not a path, since the
        // second write starts from the guest again, so it is run by hand.
        Equation eq{"put-put", where, Path(e.guest).functor(e.out).functor(e.out),
                    Path(e.guest).functor(e.out), o.args};
        const auto put_put = [&] {
        Outcome twice, once;
        try {
        {
            Trial t(g);
            State& guest = g.state(e.guest);
            State& subj = t.touch(subject);
            put->apply(guest, subj);
            put->apply(guest, subj);
            twice.state = subject;
            twice.data = subj.snapshot();
        }
        {
            Trial t(g);
            State& subj = t.touch(subject);
            put->apply(g.state(e.guest), subj);
            once.state = subject;
            once.data = subj.snapshot();
        }
        } catch (const RewriteRefused& refused) {
            twice.error = refused.what();
            twice.refused = true;
        }
        return settle(g, eq, twice, once);
        };
        if (!cache) {
            append(out, put_put());
            continue;
        }
        if (cache->strategy() == LawCache::Strategy::Direct) {
            append(out, put_put());
            continue;
        }
        const auto asked = std::chrono::steady_clock::now();
        ++cache->tally().equations;
        const std::string key = eq.law + "|" + where + "|" + eq.lhs.str();
        if (cache->direct(cache->entry(key))) {
            ++cache->tally().direct;
            append(out, put_put());
            continue;
        }
        std::vector<uint64_t> deps;
        cache->deps_of(eq.lhs, deps);
        append(out, kept(*cache, key, asked, std::move(deps), put_put));
    }
    return out;
}

std::vector<Violation> diagram(StateGraph& g, const Diagram& d, LawCache* cache) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    for (const Equation& eq : d.equations()) append(out, check(g, cache, eq));
    return out;
}

std::vector<Violation> adjunction(StateGraph& g, const Adjunction& adj, const LawOptions& o, LawCache* cache) {
    if (cache) cache->begin(g);
    std::vector<Violation> out;
    const State* a = g.find(adj.left().from());
    const State* b = g.find(adj.left().to());
    if (!a || !b) {
        out.push_back(Violation{"adjunction", adj.name().str(), "", "", {}, {}, "", "", "", "",
                                "", "a state it joins is not in the graph"});
        return out;
    }
    const auto path = [&](Key state, Key at, const Adjunction::Word& w) {
        Path p(state, at);
        for (Key k : w)
            if (!adj.is_identity(k)) p.arrow(k);
        return p;
    };
    for (const auto& e : adj.equations(*a, *b)) {
        const Key sid = e.in_a ? a->id() : b->id();
        append(out, check(g, cache, Equation{"adjunction", adj.name().str() + ", " + e.law,
                                      path(sid, e.at, e.lhs), path(sid, e.at, e.rhs), o.args}));
    }
    return out;
}

}  // namespace sg::laws

namespace sg::laws::seam_detail {

void report(std::vector<Violation>& out, const std::string& where, const std::string& lhs, const std::string& rhs, const std::string& detail) {
    Violation v;
    v.law = "seam";
    v.where = where;
    v.lhs = lhs;
    v.rhs = rhs;
    v.detail = detail;
    out.push_back(std::move(v));
}

namespace {
// A turn, yaw, pitch and roll (R = Ry Rz Rx), as the rotation it is: the
// same turn has more than one name (upside down is pitch half round, or yaw
// and roll half round), and a seam carries the turn, not the name.
std::array<double, 9> turn_of(const Element& e) {
    const double y = e.params.num(keys::yaw), p = e.params.num(keys::pitch), r = e.params.num(keys::roll);
    const double cy = std::cos(y), sy = std::sin(y), cp = std::cos(p), sp = std::sin(p), cr = std::cos(r), sr = std::sin(r);
    // Ry * Rz * Rx, row by row.
    return {cy * cp, -cy * sp * cr + sy * sr, cy * sp * sr + sy * cr,
            sp,      cp * cr,                 -cp * sr,
            -sy * cp, sy * sp * cr + cy * sr, -sy * sp * sr + cy * cr};
}
bool turned(const Element& e) { return e.params.has(keys::yaw) || e.params.has(keys::pitch) || e.params.has(keys::roll); }
}  // namespace

void agree(std::vector<Violation>& out, const std::string& where, const std::string& lhs, const std::string& rhs, const Element& image, const Element* have, const std::string& what) {
    if (!have) {
        report(out, where, lhs, rhs, what + " is missing on the far side");
        return;
    }
    for (const auto& kv : image.params) {
    // Its turn, as a rotation.
    const bool turn = turned(image);
    if (turn) {
        const auto a = turn_of(image), b = turn_of(*have);
        double worst = 0;
        for (int i = 0; i < 9; ++i) worst = std::max(worst, std::fabs(a[i] - b[i]));
        if (worst > 1e-6)
            report(out, where, lhs, rhs,
                   what + ".yaw " + std::to_string(image.params.num(keys::yaw)) + " pitch " + std::to_string(image.params.num(keys::pitch)) + " roll " +
                       std::to_string(image.params.num(keys::roll)) + " is carried across, but another turn is there: yaw " +
                       std::to_string(have->params.num(keys::yaw)) + " pitch " + std::to_string(have->params.num(keys::pitch)) + " roll " +
                       std::to_string(have->params.num(keys::roll)));
    }
        if (turn && (kv.first == keys::yaw || kv.first == keys::pitch || kv.first == keys::roll)) continue;
        if (!have->params.has(kv.first)) {
            report(out, where, lhs, rhs, what + "." + kv.first.str() + " is " + to_string(kv.second) +
                                             " carried across, and not there at all");
            continue;
        }
        if (same_value(kv.first, kv.second, have->params.get(kv.first))) continue;
        report(out, where, lhs, rhs,
               what + "." + kv.first.str() + " is " + to_string(kv.second) + " carried across, but " +
                   to_string(have->params.get(kv.first)) + " there");
    }
}

void round_trip(std::vector<Violation>& out, const std::string& where, const Functor& there, const Functor& back, const State& side) {
    const Functor loop = Functor::compose(there, back);
    State scratch(Key{"seam.scratch"});
    loop.apply(side, scratch);
    const std::string lhs = there.name().str() + " ; " + back.name().str(), rhs = "id";
    for (const auto& e : side.elements()) {
        const Key image = loop.image_object(e.id);
        if (image.empty()) continue;
        if (image != e.id) {
            report(out, where, lhs, rhs, e.id.str() + " goes across and comes back as " + image.str());
            continue;
        }
        if (const Element* r = scratch.find(image)) agree(out, where, lhs, rhs, *r, &e, side.id().str() + "." + e.id.str());
    }
}

bool travels(const StateGraph& g, Key f) {
    for (const Seam& s : g.seams())
        if (s.a_to_b == f || s.b_to_a == f) return true;
    return false;
}

}  // namespace sg::laws::seam_detail

namespace sg::laws {

std::vector<Violation> seams(const StateGraph& g) {
    using namespace seam_detail;
    std::vector<Violation> out;

    // Two-way: nothing crosses between like states except along a seam. A
    // state of no particular kind (plain `State`) is not like anything; an
    // embedding with both directions is a lens, and owes the lens laws
    // instead - it already has its way back.
    const Key untyped{"state"};
    const auto crossing = [&](Key fname, const std::string& through) {
        const Functor* f = g.functor(fname);
        if (!f) return;
        const State* a = g.find(f->from());
        const State* b = g.find(f->to());
        if (!a || !b || a == b || a->kind() != b->kind() || a->kind() == untyped || travels(g, fname)) return;
        report(out, through, f->from().str() + " -> " + f->to().str(), "a way back",
               "one way: " + fname.str() + " carries " + f->from().str() + " into " + f->to().str() +
                   ", both " + a->kind().str() + ", and no seam says how to come back - glue them with a seam");
    };
    for (const Embedding& e : g.embeddings()) {
        if (!e.in.empty() && !e.out.empty()) continue;
        if (!e.in.empty()) crossing(e.in, "embedding " + e.name.str());
        if (!e.out.empty()) crossing(e.out, "embedding " + e.name.str());
    }
    for (const Transition& t : g.transitions())
        if (!t.functor.empty()) crossing(t.functor, "transition " + t.name.str());

    for (const Seam& sm : g.seams()) {
        const std::string where = "seam " + sm.name.str();
        const State* a = g.find(sm.a);
        const State* b = g.find(sm.b);
        if (!a || !b) continue;  // validate() says so
        const auto get = [&](Key name, Key from, Key to, const char* role) -> const Functor* {
            const Functor* f = g.functor(name);
            if (!f) {
                report(out, where, role, from.str() + " -> " + to.str(), std::string(role) + " " + name.str() + " is missing");
                return nullptr;
            }
            if (f->from() != from || f->to() != to) {
                report(out, where, role, from.str() + " -> " + to.str(),
                       std::string(role) + " " + name.str() + " runs " + f->from().str() + " -> " + f->to().str());
                return nullptr;
            }
            return f;
        };
        const Functor* ab = get(sm.a_to_b, sm.a, sm.b, "travel");
        const Functor* ba = get(sm.b_to_a, sm.b, sm.a, "travel");
        const Functor* gab = get(sm.glue_ab, sm.a, sm.b, "glue");
        const Functor* gba = get(sm.glue_ba, sm.b, sm.a, "glue");

        const auto in = [](const std::vector<Key>& set, Key k) {
            return std::find(set.begin(), set.end(), k) != set.end();
        };
        if (ab && ba) {
            round_trip(out, where, *ab, *ba, *a);
            round_trip(out, where, *ba, *ab, *b);
            for (Key x : sm.boundary_a)
                if (!ab->image_object(x).empty())
                    report(out, where, ab->name().str(), "travel", "travel writes " + x.str() + ", part of the boundary");
            for (Key y : sm.boundary_b)
                if (!ba->image_object(y).empty())
                    report(out, where, ba->name().str(), "travel", "travel writes " + y.str() + ", part of the boundary");
        }
        // A glue is a map of boundaries: all of one onto all of the other,
        // one to one, and nothing else.
        const auto bijection = [&](const Functor& f, const std::vector<Key>& from, Key from_state,
                                   const std::vector<Key>& to, Key to_state) {
            std::vector<Key> hit;
            for (Key x : from) {
                const Key y = f.image_object(x);
                if (y.empty()) {
                    report(out, where, f.name().str(), "the boundary",
                           "the glue leaves " + from_state.str() + "." + x.str() + " unglued");
                } else if (!in(to, y)) {
                    report(out, where, f.name().str(), "the boundary",
                           "the glue takes " + from_state.str() + "." + x.str() + " off the far boundary, to " + y.str());
                } else if (in(hit, y)) {
                    report(out, where, f.name().str(), "the boundary",
                           "the glue takes two things to " + to_state.str() + "." + y.str());
                } else {
                    hit.push_back(y);
                }
            }
            for (Key y : to)
                if (!in(hit, y)) report(out, where, f.name().str(), "the boundary", "nothing is glued to " + to_state.str() + "." + y.str());
            f.for_each_object([&](Key x, Key) {
                if (!in(from, x))
                    report(out, where, f.name().str(), "the boundary",
                           "the glue reaches past the boundary, to " + from_state.str() + "." + x.str());
            });
        };
        if (gab && gba) {
            bijection(*gab, sm.boundary_a, sm.a, sm.boundary_b, sm.b);
            bijection(*gba, sm.boundary_b, sm.b, sm.boundary_a, sm.a);
            round_trip(out, where, *gab, *gba, *a);
            round_trip(out, where, *gba, *gab, *b);
            // Agreement, from each side.
            State from_a(Key{"seam.from_a"}), from_b(Key{"seam.from_b"});
            gab->apply(*a, from_a);
            gba->apply(*b, from_b);
            for (Key x : sm.boundary_a) {
                const Key y = gab->image_object(x);
                if (const Element* r = y.empty() ? nullptr : from_a.find(y))
                    agree(out, where, gab->name().str(), sm.b.str(), *r, b->find(y), sm.b.str() + "." + y.str());
            }
            for (Key y : sm.boundary_b) {
                const Key x = gba->image_object(y);
                if (const Element* r = x.empty() ? nullptr : from_b.find(x))
                    agree(out, where, gba->name().str(), sm.a.str(), *r, a->find(x), sm.a.str() + "." + x.str());
            }
        }
    }
    return out;
}

std::vector<Violation> overlaps(const StateGraph& g) { return overlaps(g, 0, g.seams().size()); }

std::vector<Violation> overlaps(const StateGraph& g, std::size_t first, std::size_t count) {
    using namespace seam_detail;
    std::vector<Violation> out;
    // How far apart two accounts may be and still be one: a few degrees of
    // pull, a couple of centimetres of ground.
    constexpr double kDown = 0.05, kFloor = 0.02;
    const Key walk{"walk"}, dx{"down.x"}, dy{"down.y"}, dz{"down.z"}, floor{"floor"};
    const std::size_t last = std::min(g.seams().size(), first + count);
    for (std::size_t n = first; n < last; ++n) {
        const Seam& sm = g.seams()[n];
        const State* a = g.find(sm.a);
        const State* b = g.find(sm.b);
        if (!a || !b) continue;
        const std::string where = "seam " + sm.name.str();
        for (std::size_t i = 0; i < sm.boundary_a.size() && i < sm.boundary_b.size(); ++i) {
            const Key pa = sm.boundary_a[i], pb = sm.boundary_b[i];
            const Params A = a->overlap(pa), B = b->overlap(pb);
            if (A.empty() || B.empty()) continue;
            std::string differs = " ";
            for (const auto& [s, k] : {std::pair{a, pa}, std::pair{b, pb}})
                if (const Element* e = s->find(k)) differs += e->params.get_or<std::string>(Key{"differs"}, "") + " ";
            const auto lets = [&](const char* what) { return differs.find(std::string(" ") + what + " ") != std::string::npos; };
            const std::string sides = sm.a.str() + "." + pa.str() + " / " + sm.b.str() + "." + pb.str();
            const auto say = [&](const std::string& what) { report(out, where, sides, "one overlap", what); };
            if (!lets("walk") && (A.num(walk, 0.0) > 0.5) != (B.num(walk, 0.0) > 0.5))
                say("walked through from one side only (" + (A.num(walk, 0.0) > 0.5 ? sm.a : sm.b).str() +
                    "): make both walked, or say `differs walk`");
            if (!lets("down") && A.has(dx) && B.has(dx)) {
                // The far side's account turned through the doorway: out of
                // it is into this one, and its across is the other way.
                const double ex = A.num(dx) + B.num(dx), ey = A.num(dy) - B.num(dy), ez = A.num(dz) + B.num(dz);
                const double off = std::sqrt(ex * ex + ey * ey + ez * ez);
                if (off > kDown)
                    say("down is " + std::to_string(std::lround(2.0 * std::asin(std::min(1.0, off * 0.5)) * 57.29578)) +
                        " degrees apart on its two sides, and whoever crosses is turned: stand its doorways on like ground, or say `differs down`");
            }
            if (!lets("floor") && A.has(floor) && B.has(floor) && std::fabs(A.num(floor) - B.num(floor)) > kFloor)
                say("the ground is " + std::to_string(A.num(floor)) + " m below the opening's foot in " + sm.a.str() + " and " +
                    std::to_string(B.num(floor)) + " m in " + sm.b.str() + ", and whoever crosses steps or drops: or say `differs floor`");
            if (!lets("eye") && A.has(Key{"eye"}) && B.has(Key{"eye"}) && std::fabs(A.num(Key{"eye"}) - B.num(Key{"eye"})) > kFloor)
                say("the eye is carried " + std::to_string(A.num(Key{"eye"})) + " m up in " + sm.a.str() + " and " +
                    std::to_string(B.num(Key{"eye"})) + " m in " + sm.b.str() + ", and whoever crosses rises or sinks: or say `differs eye`");
            // Walked through, a side that gives no account of what it is
            // checked against cannot be held to it: said, not passed.
            if (A.num(walk, 0.0) > 0.5 && B.num(walk, 0.0) > 0.5)
                for (const auto& [what, key] : {std::pair{"down", dx}, std::pair{"floor", floor}})
                    if (!lets(what) && (!A.has(key) || !B.has(key))) {
                        say(std::string("unchecked: ") + (A.has(key) ? sm.b : sm.a).str() + " gives no account of its " + what +
                            " at the doorway (State::overlap), so whether it agrees is not known");
                        out.back().refused = true;
                    }
            if (lets("crossing")) continue;
            for (const bool from_a : {true, false}) {
                const Params& side = from_a ? A : B;
                if (side.num(walk, 0.0) < 0.5 || side.num(Key{"leave"}, 1.0) < 0.5) continue;
                const Key travel = from_a ? sm.a_to_b : sm.b_to_a, from = from_a ? sm.a : sm.b;
                const bool way = std::any_of(g.transitions().begin(), g.transitions().end(), [&](const Transition& t) {
                    return t.functor == travel && (t.from == from || t.from == Key{"*"});
                });
                if (!way) say("walked into from " + from.str() + ", it leads nowhere - no transition carries " + travel.str() + ": or say `differs crossing`");
            }
        }
    }
    return out;
}

}  // namespace sg::laws

namespace sg {

std::string LawReport::str() const {
    std::string s;
    for (const auto& e : structure) s += "structure: " + e + "\n";
    for (const auto& v : violations) s += v.str() + "\n";
    for (const auto& v : unchecked) s += "unchecked: " + v.str() + "\n";
    for (const auto& v : bounded) s += "bounded: " + v.str() + "\n";
    return s;
}

}  // namespace sg

namespace sg::laws {

void sort_into(LawReport& r, std::vector<Violation> from) {
    for (auto& v : from)
        (v.bounded ? r.bounded : v.refused ? r.unchecked : r.violations).push_back(std::move(v));
}

}  // namespace sg::laws

namespace sg {

LawReport verify(StateGraph& g, const std::vector<Diagram>& diagrams, const LawOptions& o) {
    LawReport r;
    r.structure = g.validate();
    {
        laws::Accelerating fast(o.accelerate);
        laws::sort_into(r, laws::identity(g, o));
        laws::sort_into(r, laws::associativity(g, o));
        laws::sort_into(r, laws::composition(g, o));
        laws::sort_into(r, laws::functoriality(g, o));
        laws::sort_into(r, laws::lenses(g, o));
        laws::sort_into(r, laws::drives(g, o));
        laws::sort_into(r, laws::seams(g));
        laws::sort_into(r, laws::overlaps(g));
        for (const Diagram& d : diagrams) laws::sort_into(r, laws::diagram(g, d));
    }
    if (o.accelerate) laws::sort_into(r, o.accelerate->finish(g));
    return r;
}

LawReport verify(StateGraph& g, LawCache& cache, const std::vector<Diagram>& diagrams, const LawOptions& o) {
    LawReport r;
    r.structure = g.validate();
    {
        laws::Accelerating fast(o.accelerate);
        laws::sort_into(r, laws::identity(g, o, &cache));
        laws::sort_into(r, laws::associativity(g, o, &cache));
        laws::sort_into(r, laws::composition(g, o, &cache));
        laws::sort_into(r, laws::functoriality(g, o, &cache));
        laws::sort_into(r, laws::lenses(g, o, &cache));
        laws::sort_into(r, laws::drives(g, o, &cache));
        laws::sort_into(r, laws::seams(g));
        laws::sort_into(r, laws::overlaps(g));
        for (const Diagram& d : diagrams) laws::sort_into(r, laws::diagram(g, d, &cache));
    }
    if (o.accelerate) laws::sort_into(r, o.accelerate->finish(g));
    return r;
}

void enforce(StateGraph& g, const std::vector<Diagram>& diagrams, const LawOptions& o) {
    LawReport r = verify(g, diagrams, o);
    if (!r.holds()) throw LawError(std::move(r));
}

}  // namespace sg
