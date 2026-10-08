#include "sg/dsl/Facts.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>

#include "sg/core/Text.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/domains/Camera.hpp"
#include "sg/domains/Look.hpp"
#include "sg/domains/Spatial.hpp"
#include "sg/dsl/Kinds.hpp"

namespace sg::dsl {

namespace {

std::string num(double d) {
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof buf, d);
    return std::string(buf, r.ptr);
}

std::string dash(Key k) { return k.empty() ? "-" : k.str(); }

// One declared step, as words: what it copies, and what it sets from what.
std::string affine_text(const Affine& a) {
    std::string out = a.copy_all ? "copy_all" : "";
    for (const Affine::Row& r : a.rows) {
        if (!out.empty()) out += ";";
        out += r.param.str() + "=";
        bool first = true;
        for (const Affine::Term& t : r.terms) {
            out += (first ? "" : "+") + num(t.k) + "*" + (t.of_target ? "@" : "") + t.param.str();
            if (!t.arg.empty()) out += "*arg:" + t.arg.str();
            first = false;
        }
        out += (first ? "" : "+") + num(r.bias);
    }
    return out.empty() ? "nothing" : out;
}

std::string stages_text(const Stages* st) {
    if (!st) return "native";
    std::string out;
    for (const Affine& a : *st) out += (out.empty() ? "" : " | ") + affine_text(a);
    return out;
}

// What the state entered is told, as data, by name.
std::string enter_text(const Params& p) {
    std::vector<std::string> kv;
    for (const auto& [k, v] : p) kv.push_back(k.str() + "=" + text_detail::value(v));
    std::sort(kv.begin(), kv.end());
    std::string out;
    for (const std::string& s : kv) out += (out.empty() ? "" : ",") + s;
    return "[" + out + "]";
}

// A native computation: by the name a source bound it by, or opaque C++ (which
// says nothing of what it is, and is not pretended to).
std::string native_text(Key id) { return id.empty() ? "native" : "native:" + id.str(); }

// A guard: the comparison it is, when a source said it (`when(...)`), or opaque
// C++, which says nothing of itself.
std::string guard_fact(const Transition::Guard& g) {
    if (!g) return "none";
    return g.text.empty() ? "opaque" : "when(" + g.text + ")";
}

std::string transition_fact(const Transition& t, Key name) {
    const char* kind = t.kind == TransitionKind::Push ? "push" : t.kind == TransitionKind::Pop ? "pop" : "switch";
    return "transition " + name.str() + " " + t.from.str() + " -[" + t.trigger.str() + "]-> " + dash(t.to) + " kind=" + kind +
           " carry=" + dash(t.functor) + " enter=" + enter_text(t.enter) + " guard=" + guard_fact(t.guard) +
           " action=" + (t.action ? "opaque" : "none");
}

std::string embed_fact(const Embedding& e) {
    const char* sync = e.sync == EmbedSync::Live ? "live" : e.sync == EmbedSync::View ? "view" : "commit";
    const char* prop = e.propagate == Propagation::Continuous ? "continuous" : e.propagate == Propagation::OnEvent ? "onevent"
                       : e.propagate == Propagation::Manual ? "manual" : "onchange";
    return "embed " + e.name.str() + " host=" + e.host.str() + " portal=" + e.portal.str() + " guest=" + e.guest.str() +
           " subject=" + dash(e.subject) + " in=" + dash(e.in) + " out=" + dash(e.out) + " sync=" + sync + " propagate=" + prop +
           " focus=" + (e.focus ? "1" : "0") + " follows=" + (e.follows ? "1" : "0") + (e.recurses ? " recurses=1" : "");
}

std::string list_text(const std::vector<Key>& ks) {
    std::string out;
    for (Key k : ks) out += (out.empty() ? "" : ",") + k.str();
    return out;
}

std::string seam_fact(const Seam& s) {
    return "seam " + s.name.str() + " a=" + s.a.str() + " b=" + s.b.str() + " a_to_b=" + s.a_to_b.str() + " b_to_a=" + s.b_to_a.str() +
           " glue_ab=" + s.glue_ab.str() + " glue_ba=" + s.glue_ba.str() + " boundary_a=" + list_text(s.boundary_a) +
           " boundary_b=" + list_text(s.boundary_b) + (s.wraps ? " wraps=1" : "");
}

std::string drive_fact(const Drive& d) {
    const char* keeps = d.keeps == Keeps::Always ? "always" : d.keeps == Keeps::WhileShown ? "shown" : d.keeps == Keeps::WhileFocused ? "focused"
                        : d.keeps == Keeps::WhileEntered ? "entered" : "active";
    return "drive " + d.name.str() + " clock=" + d.clock.str() + " state=" + d.state.str() + " trigger=" + d.trigger.str() +
           " additive=" + (d.additive ? "1" : "0") + " line=" + dash(d.line) + " keeps=" + keeps;
}

std::string arrow_fact(Key state, Key name, Key from, Key to, Key trigger, const std::string& body) {
    return "arrow " + state.str() + " " + name.str() + " " + from.str() + " -> " + (to.empty() ? "=" : to.str()) + " on " + trigger.str() + " body=" + body;
}

std::string edit_fact(Key name, Key state, Key event, Key reply, const std::string& apply) {
    return "edit " + name.str() + " state=" + state.str() + " event=" + event.str() + " reply=" + reply.str() + " apply=" + apply;
}

std::string object_fact(Key f, Key src, Key dst, const std::string& how) {
    return "object " + f.str() + " " + src.str() + " -> " + dst.str() + " " + how;
}

void finish(std::vector<std::string>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

}  // namespace

std::vector<std::string> facts(const StateGraph& g, Scope scope) {
    std::vector<std::string> out;
    if (scope == Scope::Whole) {
        for (Key id : g.ids()) {
            const State& s = g.state(id);
            out.push_back("state " + id.str() + " " + s.kind().str());
            for (const auto& [k, v] : s.params()) out.push_back("param " + id.str() + " " + k.str() + " " + text_detail::value(v));
            for (const Element& e : s.elements()) {
                out.push_back("element " + id.str() + " " + e.id.str() + " " + e.kind.str() + (e.alive ? "" : " dead"));
                for (const auto& [k, v] : e.params)
                    out.push_back("eparam " + id.str() + " " + e.id.str() + " " + k.str() + " " + text_detail::value(v));
            }
            for (const Morphism& m : s.morphisms()) {
                if (!m.parts.empty()) {
                    out.push_back("compose " + id.str() + " " + m.name.str() + " = " + m.parts.front().str() + " ; " + m.parts.back().str() +
                                  " on " + m.trigger.str());
                    continue;
                }
                std::string body = m.handler ? native_text(m.native) : "none";
                if (m.declared) {
                    body.clear();
                    for (const DeclaredStep& st : *m.declared) body += (body.empty() ? "" : " | ") + affine_text(st.does);
                }
                out.push_back(arrow_fact(id, m.name, m.from, m.to, m.trigger, body));
            }
            for (Key say : s.said()) out.push_back("says " + id.str() + " " + say.str());
        }
    }
    for (const Transition& t : g.transitions()) out.push_back(transition_fact(t, t.name));
    for (const auto& [name, f] : g.functors()) {
        out.push_back("functor " + name.str() + " " + f.from().str() + " -> " + f.to().str());
        if (f.is_identity()) out.push_back("identity " + name.str());
        f.for_each_declared([&](Key src, Key dst, const Stages* st) {
            out.push_back(object_fact(name, src, dst, st ? stages_text(st) : native_text(f.native_of(src))));
        });
        f.for_each_event([&](Key src, Key dst) { out.push_back("event " + name.str() + " " + src.str() + " -> " + dst.str()); });
        f.for_each_morphism([&](Key src, Key dst) { out.push_back("arrowmap " + name.str() + " " + src.str() + " -> " + dst.str()); });
        if (const std::vector<Key>* chain = g.composite_chain(name)) {
            std::string c;
            for (Key k : *chain) c += (c.empty() ? "" : " ; ") + k.str();
            out.push_back("composite " + name.str() + " = " + c);
        }
    }
    for (const auto& l : g.lenses()) out.push_back("lens " + l.get.str() + " " + l.put.str());
    for (const Embedding& e : g.embeddings()) out.push_back(embed_fact(e));
    for (const Seam& s : g.seams()) out.push_back(seam_fact(s));
    for (const Drive& d : g.drives()) out.push_back(drive_fact(d));
    for (const auto& p : g.ports()) out.push_back("port " + p.first.str() + " " + p.second.str());
    for (Key k : g.kept()) out.push_back("keep " + k.str());
    for (const Edit& e : g.edits()) out.push_back(edit_fact(e.name, e.state, e.event, e.reply, e.apply ? native_text(e.native) : "nothing"));
    if (!g.initial().empty()) out.push_back("initial " + g.initial().str());
    finish(out);
    return out;
}

std::vector<std::string> facts(const Plan& plan) {
    std::vector<std::string> out;
    std::set<Key> made, worn;  // the states the plan makes; those whose first look is worn
    for (const Step& s : plan.steps) {
        if (const auto* made_state = std::get_if<plan::State>(&s)) made.insert(made_state->id);
        std::visit(
            [&](const auto& st) {
                using S = std::decay_t<decltype(st)>;
                if constexpr (std::is_same_v<S, plan::State>) {
                    const KindInfo* k = Kinds::standard().find(st.kind);
                    out.push_back("state " + st.id.str() + " " + (k ? k->make(st.id)->kind().str() : st.kind));
                } else if constexpr (std::is_same_v<S, plan::Element>) {
                    out.push_back("element " + st.state.str() + " " + st.id.str() + " " + st.kind.str());
                } else if constexpr (std::is_same_v<S, plan::Param>) {
                    if (st.element.empty()) out.push_back("param " + st.state.str() + " " + st.key.str() + " " + text_detail::value(st.value));
                    else out.push_back("eparam " + st.state.str() + " " + st.element.str() + " " + st.key.str() + " " + text_detail::value(st.value));
                } else if constexpr (std::is_same_v<S, plan::Arrow>) {
                    if (!st.own) {
                        const std::string body = st.body == plan::Arrow::Body::Native ? "native:" + st.native : st.body == plan::Arrow::Body::Affine ? affine_text(st.affine) : "none";
                        out.push_back(arrow_fact(st.state, st.name, st.from, st.to, st.trigger, body));
                    }
                } else if constexpr (std::is_same_v<S, plan::Compose>) {
                    out.push_back("compose " + st.state.str() + " " + st.name.str() + " = " + st.f.str() + " ; " + st.g.str() + " on " + st.trigger.str());
                } else if constexpr (std::is_same_v<S, plan::Says>) {
                    out.push_back("says " + st.state.str() + " " + st.event.str());
                } else if constexpr (std::is_same_v<S, plan::Functor>) {
                    out.push_back("functor " + st.name.str() + " " + st.from.str() + " -> " + st.to.str());
                } else if constexpr (std::is_same_v<S, plan::Object>) {
                    std::string how = "native:" + st.native;
                    switch (st.transport) {
                        case plan::Object::Transport::Copy: how = stages_text(transport::copy_all.stages.get()); break;
                        case plan::Object::Transport::Only: how = stages_text(transport::only(st.names).stages.get()); break;
                        case plan::Object::Transport::Swizzle: how = stages_text(transport::swizzle(st.pairs, st.rest).stages.get()); break;
                        case plan::Object::Transport::Affine: how = stages_text(transport::affine(st.affine).stages.get()); break;
                        case plan::Object::Transport::Native: break;
                    }
                    out.push_back(object_fact(st.functor, st.src, st.dst, how));
                } else if constexpr (std::is_same_v<S, plan::EventMap>) {
                    out.push_back("event " + st.functor.str() + " " + st.src.str() + " -> " + st.dst.str());
                } else if constexpr (std::is_same_v<S, plan::ArrowMap>) {
                    out.push_back("arrowmap " + st.functor.str() + " " + st.src.str() + " -> " + st.dst.str());
                } else if constexpr (std::is_same_v<S, plan::ComposeFunctors>) {
                    std::string c;
                    for (Key k : st.chain) c += (c.empty() ? "" : " ; ") + k.str();
                    out.push_back("composite " + st.name.str() + " = " + c);
                } else if constexpr (std::is_same_v<S, plan::Lens>) {
                    out.push_back("lens " + st.get.str() + " " + st.put.str());
                } else if constexpr (std::is_same_v<S, plan::Connect>) {
                    Key name = st.t.name;
                    if (name.empty()) name = Key{st.t.from.str() + "-" + st.t.trigger.str() + "->" + st.t.to.str()};
                    out.push_back(transition_fact(st.t, name));
                } else if constexpr (std::is_same_v<S, plan::Embed>) {
                    out.push_back(embed_fact(st.e));
                } else if constexpr (std::is_same_v<S, plan::Glue>) {
                    const Seam seam = doorway_seam(st.name, st.a, st.pa, st.b, st.pb, st.also, st.wraps);
                    out.push_back(seam_fact(seam));
                    // what it makes with it: the travel and the glue, each carrying its objects by a computation
                    // that reads the doorways' poses (opaque: those are the portals' own params)
                    const auto functor = [&](Key name, Key from, Key to, std::vector<std::pair<Key, Key>> objects) {
                        out.push_back("functor " + name.str() + " " + from.str() + " -> " + to.str());
                        for (const auto& o : objects) out.push_back(object_fact(name, o.first, o.second, "native"));
                    };
                    std::vector<std::pair<Key, Key>> glue_ab{{st.pa, st.pb}}, glue_ba{{st.pb, st.pa}};
                    for (const auto& x : st.also) {
                        glue_ab.push_back(x);
                        glue_ba.push_back({x.second, x.first});
                    }
                    functor(seam.a_to_b, st.a, st.b, {{SpatialState::camera_id(), SpatialState::camera_id()}});
                    functor(seam.b_to_a, st.b, st.a, {{SpatialState::camera_id(), SpatialState::camera_id()}});
                    functor(seam.glue_ab, st.a, st.b, glue_ab);
                    functor(seam.glue_ba, st.b, st.a, glue_ba);
                } else if constexpr (std::is_same_v<S, plan::Drive>) {
                    out.push_back(drive_fact(st.d));
                } else if constexpr (std::is_same_v<S, plan::Port>) {
                    out.push_back("port " + st.state.str() + " " + st.event.str());
                } else if constexpr (std::is_same_v<S, plan::Keep>) {
                    out.push_back("keep " + st.functor.str());
                } else if constexpr (std::is_same_v<S, plan::Wear>) {
                    out.push_back(embed_fact(wear_embedding(st.host, st.look)));
                    // a host the plan makes gets its look slot, showing the first look it wears
                    if (made.count(st.host)) {
                        out.push_back("element " + st.host.str() + " look look_slot");
                        if (worn.insert(st.host).second)
                            out.push_back("eparam " + st.host.str() + " look active " + text_detail::value(Value{st.look.str()}));
                    }
                } else if constexpr (std::is_same_v<S, plan::Film>) {
                    const Embedding e = film_embedding(st.camera, st.world, st.rig);
                    out.push_back(embed_fact(e));
                    if (!st.rig.empty()) {  // the rig's pose onto the lens, by a computation of the world's own poses
                        out.push_back("functor " + e.out.str() + " " + st.world.str() + " -> " + st.camera.str());
                        out.push_back(object_fact(e.out, st.rig, Camera::lens_id(), "native"));
                    }
                } else if constexpr (std::is_same_v<S, plan::Initial>) {
                    out.push_back("initial " + st.state.str());
                } else if constexpr (std::is_same_v<S, plan::Edit>) {
                    out.push_back(edit_fact(Key{st.state.str() + ":" + st.event.str()}, st.state, st.event, st.reply, "native:" + st.native));
                }
            },
            s);
    }
    finish(out);
    return out;
}

namespace {

// A fact with the name of its native computation left out: what a graph built
// by hand can be held to, since its C++ says nothing of what it is.
std::string opaque(std::string line) {
    // a guard's comparison, likewise: a lambda that may be it, or may not
    if (line.rfind("transition ", 0) == 0) {
        const std::size_t at = line.find(" guard=when("), end = line.rfind(" action=");
        if (at != std::string::npos && end != std::string::npos && end > at) line.replace(at, end - at, " guard=opaque");
    }
    for (std::size_t at = line.find("native:"); at != std::string::npos; at = line.find("native:", at + 1)) {
        std::size_t end = line.find_first_of(" ,]", at);
        if (end == std::string::npos) end = line.size();
        line.erase(at + 6, end - (at + 6));  // "native:name" -> "native"
    }
    return line;
}

// What the plan declares that the graph does not have, and what it has only as
// an unnamed native.
void compare(const Plan& plan, const StateGraph& g, std::vector<std::string>* absent, std::vector<std::string>* unnamed) {
    const std::vector<std::string> want = facts(plan);
    const std::vector<std::string> have = facts(g, Scope::Whole);
    const std::set<std::string> in_graph(have.begin(), have.end());
    for (const std::string& line : want) {
        if (in_graph.count(line)) continue;
        const std::string blind = opaque(line);
        if (blind != line && in_graph.count(blind)) {
            if (unnamed) unnamed->push_back(line);
        } else if (absent) {
            absent->push_back(line);
        }
    }
}

}  // namespace

std::vector<std::string> missing(const Plan& plan, const StateGraph& g) {
    std::vector<std::string> out;
    compare(plan, g, &out, nullptr);
    return out;
}

std::vector<std::string> unverified(const Plan& plan, const StateGraph& g) {
    std::vector<std::string> out;
    compare(plan, g, nullptr, &out);
    return out;
}

std::vector<std::string> difference(const std::vector<std::string>& before, const std::vector<std::string>& after) {
    std::vector<std::string> a = before, b = after, out;
    finish(a);
    finish(b);
    std::vector<std::string> gone, added;
    std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(gone));
    std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(added));
    for (const std::string& s : gone) out.push_back("- " + s);
    for (const std::string& s : added) out.push_back("+ " + s);
    return out;
}

std::string to_text(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

std::vector<std::string> from_text(const std::string& text) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(at, nl - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
        at = nl + 1;
    }
    return out;
}

}  // namespace sg::dsl
