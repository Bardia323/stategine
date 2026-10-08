// Stategine - Save: what it does. What it is is in sg/domains/Save.hpp.
#include "sg/domains/Save.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <unordered_set>

#include "sg/core/Cache.hpp"
#include "sg/core/Engine.hpp"
#include "sg/core/Kan.hpp"
#include "sg/core/Text.hpp"

namespace sg {

namespace {

const Key kSave{"save"}, kSlot{"slot"}, kName{"name"}, kWithout{"without"}, kOk{"ok"}, kWhy{"why"},
    kAt{"at"}, kKept{"kept"}, kDerived{"derived"}, kReplayed{"replayed"}, kHoles{"holes"}, kText{"text"},
    kStamped{"stamped"}, kDiffers{"differs"};

// What a kept element leaves out: names, separated by spaces.
std::unordered_set<Key> without_of(const Element& e) {
    std::unordered_set<Key> out;
    if (const std::string* w = e.params.text(kWithout)) {
        std::istringstream in(*w);
        for (std::string name; in >> name;) out.insert(Key{name});
    }
    return out;
}

std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += (out.empty() ? "" : "\n") + l;
    return out;
}

// A state given what a text of it says, exactly - but for what was left out
// of the text, which stays as it is (its start, on a load).
bool overlay(State& s, const std::string& text, const std::unordered_set<Key>& without, std::string* why) {
    State read(s.id());
    if (!from_text(read, text, why)) return false;
    std::vector<Key> gone;
    for (const auto& [k, v] : s.params())
        if (!without.count(k) && !read.params().has(k)) gone.push_back(k);
    for (Key k : gone) s.params().erase(k);
    for (const auto& [k, v] : read.params()) s.params().set(k, v);
    gone.clear();
    for (const Element& e : s.elements())
        if (!without.count(e.id) && !read.find(e.id)) gone.push_back(e.id);
    for (Key k : gone) s.remove_with_arrows(k);
    for (const Element& r : read.elements()) {
        Element* e = s.find(r.id);
        if (!e) e = &s.add_element(r.id, r.kind);
        e->params = r.params;
        e->alive = r.alive;
    }
    return true;
}

Params refused(const std::string& slot, const std::string& why) {
    return Params{}.set(kSlot, slot).set(kOk, false).set(kWhy, why);
}

}  // namespace

Save::Save(Key id) : State(id) {
    add_element(slot_element(), Key{"slot"}).params.set(kName, std::string("1"));
    for (Key k : {restrict_event(), extend_event(), fetch_event(), saved_event(), loaded_event()}) says(k);
    // Asked to save: the edit that writes the kept states out.
    loop(Key{"save"}, slot_element(), save_event(), [](State& s, Element&, Element*, const Event& e) {
        auto& sv = static_cast<Save&>(s);
        s.emit(Event{sv.restrict_event(), Params{}.set(kSave, s.id().str()).set(kSlot, sv.slot_of(e))});
    });
    loop(Key{"saved"}, slot_element(), Key{restrict_event().str() + ".done"},
         [](State& s, Element&, Element*, const Event& e) { s.emit(Event{static_cast<Save&>(s).saved_event(), e.args}); });
    // Asked to load: the text is outside, so it is asked for there.
    loop(Key{"load"}, slot_element(), load_event(), [](State& s, Element&, Element*, const Event& e) {
        auto& sv = static_cast<Save&>(s);
        s.emit(Event{sv.fetch_event(), Params{}.set(kSave, s.id().str()).set(kSlot, sv.slot_of(e))});
    });
    // And comes back in: the edit that makes the world from it.
    loop(Key{"restore"}, slot_element(), restore_event(), [](State& s, Element&, Element*, const Event& e) {
        auto& sv = static_cast<Save&>(s);
        const std::string slot = sv.slot_of(e);
        if (!e.args.text(kText)) {
            s.emit(Event{sv.loaded_event(), Params{}.set(kSlot, slot).set(kOk, false).set(
                                                kWhy, e.args.get_or<std::string>(kWhy, "nothing came back for slot " + slot))});
            return;
        }
        s.emit(Event{sv.extend_event(),
                     Params{}.set(kSave, s.id().str()).set(kSlot, slot).set(kText, *e.args.text(kText))});
    });
    loop(Key{"loaded"}, slot_element(), Key{extend_event().str() + ".done"},
         [](State& s, Element&, Element*, const Event& e) {
             auto& sv = static_cast<Save&>(s);
             s.emit(Event{sv.loaded_event(), e.args});
             const std::string at = e.args.get_or<std::string>(kAt, "");
             if (e.args.get_or<bool>(kOk, false) && !at.empty()) s.emit(Event{sv.at_event(Key{at})});
         });
}

Save& Save::keep(Key state, const std::string& without) {
    Element& e = add_element(state, kept_kind());
    if (!without.empty()) e.params.set(kWithout, without);
    return *this;
}

Save& Save::replay(Key state) {
    add_element(state, replayed_kind());
    return *this;
}

std::string Save::slot_of(const Event& e) const {
    if (const std::string* s = e.args.text(kSlot)) return *s;
    const Element* slot = find(slot_element());
    const std::string* name = slot ? slot->params.text(kName) : nullptr;
    return name ? *name : "1";
}

// --- the links ---------------------------------------------------------------------

std::vector<Save::Link> Save::links(const StateGraph& g, Key save) {
    // Movement: what a transition carries, a seam's travel and glue.
    std::unordered_set<Key> moves;
    for (const Transition& t : g.transitions())
        if (!t.functor.empty()) moves.insert(t.functor);
    for (const Seam& s : g.seams())
        for (Key k : {s.a_to_b, s.b_to_a, s.glue_ab, s.glue_ba}) moves.insert(k);

    std::vector<Link> out;
    for (const auto& [name, f] : g.functors()) {
        if (moves.count(name) || f.is_identity() || f.from() == f.to()) continue;
        if (f.from() == save || f.to() == save || !g.contains(f.from()) || !g.contains(f.to())) continue;
        std::vector<std::pair<Key, Key>> objects;
        f.for_each_object([&](Key a, Key b) { objects.emplace_back(a, b); });
        std::sort(objects.begin(), objects.end());
        Link there{Link::Way::Forward, name, f.from(), f.to(), {}};
        Link back{Link::Way::Back, name, f.to(), f.from(), {}};
        std::vector<std::string> lost;
        for (const auto& [a, b] : objects) {
            there.objects.emplace_back(a, b);
            std::string why;
            if (kan::inverse(f, a, nullptr, &why)) back.objects.emplace_back(b, a);
            else back.lost.push_back(f.from().str() + "." + a.str() + ": " + why);
        }
        if (!there.objects.empty()) out.push_back(std::move(there));
        if (!back.objects.empty() || !back.lost.empty()) out.push_back(std::move(back));
    }
    for (const Embedding& e : g.embeddings())
        if (e.host != save && e.guest != save && e.host != e.guest)
            out.push_back(Link{Link::Way::Lives, Key{}, e.host, e.guest, {}});
    return out;
}

Save::Extension Save::extension(const StateGraph& g) const {
    Extension x;
    std::unordered_map<Key, int> depth;
    std::vector<Key> order;
    for (const Element& e : elements())
        if (e.kind == kept_kind() && e.id != id()) {
            if (!g.contains(e.id)) {
                x.holes.push_back(e.id.str() + ": kept, but there is no such state");
                continue;
            }
            if (depth.emplace(e.id, 0).second) {
                x.kept.push_back(e.id);
                order.push_back(e.id);
            }
        }
    const std::vector<Link> ls = links(g, id());
    std::unordered_map<Key, std::vector<const Link*>> from;
    for (const Link& l : ls) from[l.from].push_back(&l);
    std::unordered_map<Key, std::size_t> at;
    // Outward from the kept, nearest first: a state takes what the states
    // one nearer say of it, and nothing from as near or farther.
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Key s = order[i];
        const int d = depth[s];
        auto out = from.find(s);
        if (out == from.end()) continue;
        for (const Link* l : out->second) {
            if (l->way == Link::Way::Back && l->objects.empty()) continue;
            auto known = depth.find(l->to);
            if (known != depth.end() && known->second <= d) continue;
            if (known == depth.end()) {
                depth.emplace(l->to, d + 1);
                order.push_back(l->to);
                at.emplace(l->to, x.derived.size());
                x.derived.push_back({l->to, d + 1, {}});
            }
            x.derived[at.at(l->to)].by.push_back(*l);
        }
    }
    for (const Element& e : elements())
        if (e.kind == replayed_kind() && e.id != id() && !depth.count(e.id)) {
            if (g.contains(e.id)) x.replayed.push_back(e.id);
            else x.holes.push_back(e.id.str() + ": replayed, but there is no such state");
        }
    // What links into what the save reaches carry and cannot bring back.
    for (const Link& l : ls) {
        if (l.way != Link::Way::Back || l.lost.empty() || !depth.count(l.from)) continue;
        auto d = depth.find(l.to);
        if (d != depth.end() && d->second == 0) continue;  // kept: brought back whole
        for (const std::string& why : l.lost)
            x.holes.push_back(why + " - so what " + l.functor.str() + " carries into " + l.from.str() +
                              " is not brought back: keep " + l.to.str() + ", or declare what it does");
    }
    return x;
}

// --- what the kept states are declared as -----------------------------------------------

namespace {

// What an arrow is declared to do: its ends and trigger, what it is made of,
// the native it names, and the steps it says - every number by its bits.
void arrow_into(Hasher& h, const Morphism& m) {
    h.text(m.from.str()).text(m.to.str()).text(m.trigger.str()).text(m.native.str()).integer(m.handler ? 1 : 0);
    h.integer(static_cast<int64_t>(m.parts.size()));
    for (Key part : m.parts) h.text(part.str());
    h.integer(m.declared ? static_cast<int64_t>(m.declared->size()) : -1);
    if (!m.declared) return;
    for (const DeclaredStep& st : *m.declared) {
        h.text(st.from.str()).text(st.to.str()).integer(st.does.copy_all ? 1 : 0).integer(static_cast<int64_t>(st.does.rows.size()));
        for (const Affine::Row& r : st.does.rows) {
            h.text(r.param.str()).number(r.bias).integer(static_cast<int64_t>(r.terms.size()));
            for (const Affine::Term& t : r.terms) h.text(t.param.str()).number(t.k).text(t.arg.str()).integer(t.of_target ? 1 : 0);
        }
    }
}

}  // namespace

std::vector<Save::Stamp> Save::declarations(const State& s) {
    std::vector<Stamp> out;
    out.push_back({s.id(), "kind", Hasher{}.text(s.kind().str()).digest().hex()});
    for (const Element& e : s.elements()) out.push_back({s.id(), "element " + e.id.str(), Hasher{}.text(e.kind.str()).digest().hex()});
    for (const Morphism& m : s.morphisms()) {
        Hasher h;
        arrow_into(h, m);
        out.push_back({s.id(), "arrow " + m.name.str(), h.digest().hex()});
    }
    for (Key say : s.said()) out.push_back({s.id(), "says " + say.str(), Hasher{}.digest().hex()});
    return out;
}

std::string Save::digest_of(const std::vector<Stamp>& stamps) {
    Hasher h;
    for (const Stamp& st : stamps) h.text(st.state.str()).text(st.declaration).text(st.digest);
    return h.digest().hex();
}

std::vector<std::string> Save::differences(const Text& t, const StateGraph& g) {
    std::vector<std::string> out;
    if (t.facts.empty()) return out;  // written before saves were stamped
    // The states the save was stamped with, in its order, as they are declared now.
    std::vector<Key> states;
    for (const Stamp& st : t.stamps)
        if (states.empty() || states.back() != st.state) states.push_back(st.state);
    std::vector<Stamp> now;
    for (Key id : states)
        if (const State* s = g.find(id))
            for (Stamp& st : declarations(*s)) now.push_back(std::move(st));
    if (digest_of(now) == t.facts) return out;  // as they were: nothing to look at one by one
    std::map<std::pair<std::string, std::string>, std::string> then, here;
    for (const Stamp& st : t.stamps) then[{st.state.str(), st.declaration}] = st.digest;
    for (const Stamp& st : now) here[{st.state.str(), st.declaration}] = st.digest;
    for (Key id : states)
        if (!g.contains(id)) out.push_back(id.str() + ": no longer a state");
    for (const auto& [at, digest] : then) {
        if (!g.contains(Key{at.first})) continue;
        auto it = here.find(at);
        if (it == here.end()) out.push_back(at.first + ": " + at.second + " is no longer declared");
        else if (it->second != digest) out.push_back(at.first + ": " + at.second + " is declared otherwise");
    }
    for (const auto& [at, digest] : here)
        if (!then.count(at)) out.push_back(at.first + ": " + at.second + " is declared since");
    return out;
}

// --- the file ------------------------------------------------------------------------

std::string Save::write_text(const Text& t) {
    std::string out = "# a save (sg::Save): the kept states, as each is written (sg::to_text)\n";
    if (!t.at.empty()) out += "at " + text_detail::escape(t.at.str()) + "\n";
    if (!t.facts.empty()) {
        // What the kept states were declared as: one digest of it all, and each declaration's.
        out += "facts " + t.facts + "\n";
        for (const Stamp& st : t.stamps)
            out += "fact " + text_detail::escape(st.state.str()) + " " + text_detail::escape(st.declaration) + " " + st.digest + "\n";
    }
    for (const auto& [id, text] : t.states) out += text;
    return out;
}

bool Save::read_text(const std::string& src, Text& out, std::string* why) {
    out = Text{};
    std::istringstream in(src);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("state ", 0) == 0) {
            std::istringstream words(line.substr(6));
            std::string id;
            words >> id;
            out.states.emplace_back(Key{text_detail::unescape(id)}, std::string{});
        }
        if (!out.states.empty()) {
            out.states.back().second += line + "\n";
        } else if (line.rfind("at ", 0) == 0) {
            out.at = Key{text_detail::unescape(line.substr(3))};
        } else if (line.rfind("facts ", 0) == 0) {
            out.facts = line.substr(6);
        } else if (line.rfind("fact ", 0) == 0) {
            std::istringstream words(line.substr(5));
            std::string state, declaration, digest;
            if (!(words >> state >> declaration >> digest)) {
                if (why) *why = "line " + std::to_string(n) + ": a fact is a state, a declaration and a digest: " + line;
                return false;
            }
            out.stamps.push_back({Key{text_detail::unescape(state)}, text_detail::unescape(declaration), digest});
        } else if (!line.empty() && line[0] != '#') {
            if (why) *why = "line " + std::to_string(n) + ": not a line of a save: " + line;
            return false;
        }
    }
    return true;
}

// --- the edits -----------------------------------------------------------------------

Params Save::restrict_kept(StateGraph& g, const Event& asked) {
    const std::string slot = asked.args.get_or<std::string>(kSlot, "1");
    auto* sv = dynamic_cast<Save*>(g.find(Key{asked.args.get_or<std::string>(kSave, "")}));
    if (!sv) return refused(slot, "no save asked for this");
    Text t;
    if (const Engine* e = sv->engine())
        if (const State* here = e->current()) t.at = here->id();
    std::vector<std::string> missing;
    for (const Element& e : sv->elements()) {
        if (e.kind != kept_kind() || e.id == sv->id()) continue;
        const State* s = g.find(e.id);
        if (!s) {
            missing.push_back(e.id.str() + ": kept, but there is no such state");
            continue;
        }
        const std::unordered_set<Key> without = without_of(e);
        t.states.emplace_back(e.id, to_text(*s, [&](const Element& x) { return !without.count(x.id); },
                                            [&](Key k) { return !without.count(k); }));
        for (Stamp& st : declarations(*s)) t.stamps.push_back(std::move(st));
    }
    t.facts = digest_of(t.stamps);
    return Params{}
        .set(kSlot, slot)
        .set(kOk, true)
        .set(kText, write_text(t))
        .set(kAt, t.at.str())
        .set(kKept, static_cast<int64_t>(t.states.size()))
        .set(kWhy, joined(missing));
}

Params Save::extend_kept(StateGraph& g, const Event& asked) {
    const std::string slot = asked.args.get_or<std::string>(kSlot, "1");
    auto* sv = dynamic_cast<Save*>(g.find(Key{asked.args.get_or<std::string>(kSave, "")}));
    if (!sv) return refused(slot, "no save asked for this");
    const std::string* src = asked.args.text(kText);
    if (!src) return refused(slot, "no text to load");
    Text t;
    std::string why;
    if (!read_text(*src, t, &why)) return refused(slot, why);
    // What the save was written against, beside what the world is now.
    const std::vector<std::string> differs = differences(t, g);
    std::unordered_map<Key, const std::string*> texts;
    for (const auto& [id, text] : t.states) texts.emplace(id, &text);

    const Extension x = sv->extension(g);
    // All or nothing: what it touches, as it was, to go back to.
    std::vector<std::pair<State*, State::Snapshot>> before;
    const auto touch = [&](Key id) {
        State& s = g.state(id);
        before.emplace_back(&s, s.snapshot());
    };
    for (Key k : x.kept) touch(k);
    for (const auto& r : x.derived) touch(r.state);
    for (Key k : x.replayed) touch(k);
    std::vector<std::string> problems = x.holes;

    // Everything it touches starts again; the kept, so that what they leave
    // out of the save is played again with the rest.
    for (Key k : x.kept) g.restore_default(k);
    for (const auto& r : x.derived)
        if (!g.restore_default(r.state)) problems.push_back(r.state.str() + ": has no start to play again from");
    for (Key k : x.replayed)
        if (!g.restore_default(k)) problems.push_back(k.str() + ": has no start to play again from");

    // The kept, as saved.
    int64_t kept = 0;
    for (Key k : x.kept) {
        auto it = texts.find(k);
        if (it == texts.end()) {
            problems.push_back(k.str() + ": kept, but not in this save - played again from its start");
            continue;
        }
        State& s = g.state(k);
        std::string bad;
        if (!overlay(s, *it->second, without_of(sv->element(k)), &bad)) {
            for (auto& [st, snap] : before) st->restore(std::move(snap));
            return refused(slot, k.str() + ": " + bad);
        }
        s.on_restored();
        ++kept;
    }

    // What follows from them, nearest first. Two links that say different
    // things of one parameter: no colimit there, and the save says so.
    struct Said {
        Value v;
        Key by;
    };
    std::unordered_map<std::string, Said> said;
    for (const auto& r : x.derived) {
        State& dst = g.state(r.state);
        for (const Link& l : r.by) {
            if (l.way == Link::Way::Lives) continue;
            const State& src = g.state(l.from);
            const Functor& f = *g.functor(l.functor);
            for (const auto& [a, b] : l.objects) {
                const Element* se = src.find(a);
                if (!se) continue;
                Element* de = dst.find(b);
                if (!de) de = &dst.add_element(b, se->kind);
                const Params was = de->params;
                if (l.way == Link::Way::Forward) {
                    const Transport* run = f.transport_of(a);
                    if (run && *run) (*run)(*se, *de);
                    else transport::copy_all(*se, *de);
                } else {
                    kan::inverse(f, b)->back(*se, *de);
                }
                for (const auto& [k, v] : de->params) {
                    if (was.has(k) && was.get(k) == v) continue;
                    const std::string where = r.state.str() + "." + b.str() + "." + k.str();
                    auto [it, fresh] = said.emplace(where, Said{v, l.functor});
                    if (!fresh && it->second.v != v)
                        problems.push_back(where + ": " + it->second.by.str() + " says " + to_string(it->second.v) +
                                           ", " + l.functor.str() + " says " + to_string(v));
                }
            }
        }
        dst.on_restored();
    }
    return Params{}
        .set(kSlot, slot)
        .set(kOk, true)
        .set(kAt, t.at.str())
        .set(kKept, kept)
        .set(kDerived, static_cast<int64_t>(x.derived.size()))
        .set(kReplayed, static_cast<int64_t>(x.replayed.size()))
        .set(kHoles, static_cast<int64_t>(x.holes.size()))
        .set(kWhy, joined(problems))
        .set(kStamped, !t.facts.empty())
        .set(kDiffers, joined(differs));
}

}  // namespace sg

// --- the device ------------------------------------------------------------------------

namespace sg {

void SaveFiles::watch(Save& save) {
    const Key id = save.id();
    save.bus().subscribe(save.saved_event(), [this, id](const Event& e) {
        const std::string* text = e.args.text(kText);
        if (!text || !e.args.get_or<bool>(kOk, false)) return;
        const std::string slot = e.args.get_or<std::string>(kSlot, "1");
        try {
            const std::string key = id.str() + "/" + slot;
            store_.bind(key, file(id, slot), *text);
            store_.put(key, *text);
        } catch (const std::exception&) {
            // A slot that is not a plain name has no file: nothing written.
        }
    });
    save.bus().subscribe(save.fetch_event(), [this, id](const Event& e) {
        asked_.emplace_back(id, e.args.get_or<std::string>(kSlot, "1"));
    });
}

void SaveFiles::pump(Engine& engine) {
    std::vector<std::pair<Key, std::string>> asked;
    asked.swap(asked_);
    for (const auto& [id, slot] : asked) {
        Params p;
        p.set(kSlot, slot);
        try {
            const std::filesystem::path f = file(id, slot);
            std::error_code ec;
            if (std::filesystem::exists(f, ec)) p.set(kText, store_.bind(id.str() + "/" + slot, f));
            else p.set(kWhy, "nothing is saved in slot " + slot);
        } catch (const std::exception& ex) {
            p.set(kWhy, std::string(ex.what()));
        }
        engine.send(id, Event{Key{id.str() + ".restore"}, std::move(p)});
    }
}

std::filesystem::path SaveFiles::file(Key save, const std::string& slot) const {
    return assets_.file(save.str(), slot + ".save");
}

}  // namespace sg
