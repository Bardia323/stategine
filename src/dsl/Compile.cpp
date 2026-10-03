#include "sg/dsl/Compile.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <set>

#include "sg/core/Temporal.hpp"
#include "sg/domains/Atlas.hpp"
#include "sg/domains/Camera.hpp"
#include "sg/domains/Look.hpp"
#include "sg/dsl/Parse.hpp"

namespace sg::dsl {

std::string Compiled::report() const {
    std::string out;
    for (const Diagnostic& d : errors) out += d.str() + "\n";
    return out;
}

namespace {

// What a state is known to hold. A state built elsewhere is `open`: it may
// hold more than is known, and a name of it that is not known is left to the
// graph's own `validate`.
struct ElemInfo {
    std::string kind;
    bool own = false;
};

struct ArrowInfo {
    std::string from, to;  // to empty: a loop
    std::string trigger;
    std::vector<std::string> args;
    bool own = false;
};

struct StInfo {
    std::string kind;
    bool ext = false;
    bool open = false;
    Loc at;
    std::map<std::string, ElemInfo> elems;
    std::map<std::string, ArrowInfo> arrows;
    std::set<std::string> says;
};

struct FInfo {
    std::string from, to;
    bool ext = false;
};

// The word in a native's name that makes it an effect on the outside world, if
// there is one. A name is only a name, but a native that says it reads a file
// says what it is.
std::string io_word(const std::string& native) {
    static const std::set<std::string> words = {"file", "fs",   "io",     "read",  "write",  "open", "load",   "save", "net",
                                                "socket", "http", "exec",  "system", "print", "log",  "sleep",  "thread", "fopen"};
    std::string w;
    const auto hit = [&](const std::string& x) { return !x.empty() && words.count(x) != 0; };
    for (char c : native) {
        if (c == '_' || c == '.') {
            if (hit(w)) return w;
            w.clear();
        } else {
            w += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return hit(w) ? w : std::string{};
}

bool is_filesystem_word(const std::string& w) {
    static const std::set<std::string> fs = {"file", "fs", "read", "write", "open", "load", "save", "fopen"};
    return fs.count(w) != 0;
}

// What a state would keep to tell the time itself. Time is a Temporal's.
bool private_timer(const std::string& key) {
    static const std::set<std::string> names = {"elapsed", "timer", "timers", "tick", "ticks", "countdown", "stopwatch", "dt", "delta_time", "time"};
    return names.count(key) != 0;
}

class Compiler {
public:
    Compiler(const std::vector<Program>& progs, const Options& o)
        : progs_(progs), opt_(o), kinds_(o.kinds ? *o.kinds : Kinds::standard()) {}

    Compiled run() {
        for (const Program& p : progs_) out_.plan.sources.push_back(p.file);
        collect_states();
        for (const Program& p : progs_) aliases_of(p);
        for (const Program& p : progs_) declared_states(p);
        for (const Program& p : progs_) functors_of(p);
        for (const Program& p : progs_) composes_of(p);
        for (const Program& p : progs_) lenses_of(p);
        for (const Program& p : progs_) seams_of(p);  // before what carries their functors
        for (const Program& p : progs_) transitions_of(p);
        for (const Program& p : progs_) embeds_of(p);
        for (const Program& p : progs_) wears_of(p);
        for (const Program& p : progs_) films_of(p);
        for (const Program& p : progs_) drives_of(p);
        for (const Program& p : progs_) ports_of(p);
        for (const Program& p : progs_) keeps_of(p);
        for (const Program& p : progs_) whens_of(p);
        for (const Program& p : progs_) edits_of(p);
        for (const Program& p : progs_) initials_of(p);
        for (const Program& p : progs_) binds_of(p);
        time_rule();
        if (out_.errors.empty()) assemble();
        else out_.plan.steps.clear();
        return std::move(out_);
    }

private:
    const std::vector<Program>& progs_;
    const Options& opt_;
    const Kinds& kinds_;
    Compiled out_;

    std::map<std::string, StInfo> states_;
    // what each declared state's arrows take, known before anything is checked
    // (a direct write into a state names the arrow that should be used instead)
    std::map<std::string, std::vector<std::pair<std::string, std::vector<std::string>>>> signatures_;
    std::vector<std::string> order_;  // states, as declared
    std::map<std::string, FInfo> functors_;
    std::map<std::string, std::string> aliases_;  // transport alias -> native
    std::set<std::string> embed_names_, drive_names_, seam_names_, transition_names_, edit_names_;
    std::set<std::pair<std::string, std::string>> driven_;  // {state, event}
    struct Timed {
        std::string state, arrow, trigger;
        std::vector<std::string> args;
        Loc at;
    };
    std::vector<Timed> timed_;
    bool have_initial_ = false;

    // The plan, in groups: the order a graph has to be made in.
    std::vector<Step> extern_steps_, state_steps_, functor_steps_, compose_steps_, relation_steps_, wear_steps_, glue_steps_,
        drive_steps_, port_steps_, other_steps_;

    void err(const Loc& at, const std::string& message, const std::string& hint = {}) {
        out_.errors.push_back({at, message, hint});
    }

    // --- names ------------------------------------------------------------------------
    std::string known_states() const {
        std::string s;
        int n = 0;
        for (const auto& kv : states_) {
            if (n++ == 8) return s + ", ...";
            s += (s.empty() ? "" : ", ") + kv.first;
        }
        return s.empty() ? "none" : s;
    }

    // The state a dotted name begins with, and what is left: the longest prefix
    // that is a state.
    bool split(const std::string& text, std::string& state, std::string& rest) const {
        if (states_.count(text)) {
            state = text;
            rest.clear();
            return true;
        }
        for (std::size_t i = text.size(); i-- > 0;) {
            if (text[i] != '.') continue;
            const std::string prefix = text.substr(0, i);
            if (states_.count(prefix)) {
                state = prefix;
                rest = text.substr(i + 1);
                return true;
            }
        }
        return false;
    }

    const StInfo* state_named(const std::string& name, const Loc& at, const std::string& what) {
        auto it = states_.find(name);
        if (it == states_.end()) {
            err(at, "no state " + name + " (" + what + ")", "known states: " + known_states() + "; a state built in C++ is named with `extern state`");
            return nullptr;
        }
        return &it->second;
    }

    // `state.element`: resolved by the longest state it begins with.
    // Every way a dotted name splits into a state and one of its elements: state
    // ids have dots too (`void.look`), so `void.look.scene` might be two things.
    bool element_ref(const std::string& text, const Loc& at, std::string& state, std::string& element, const std::string& what) {
        std::vector<std::pair<std::string, std::string>> known, open;
        std::string first_state, first_rest;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '.') continue;
            const std::string prefix = text.substr(0, i), rest = text.substr(i + 1);
            auto it = states_.find(prefix);
            if (it == states_.end()) continue;
            if (first_state.empty()) first_state = prefix, first_rest = rest;
            if (it->second.elems.count(rest)) known.push_back({prefix, rest});
            else if (it->second.open) open.push_back({prefix, rest});
        }
        const auto& pick = !known.empty() ? known : open;
        if (pick.size() > 1) {
            err(at, "cannot tell which state " + text + " names an element of (" + what + "): " + pick[0].first + " or " + pick[1].first,
                "write it so only one reading is a state with that element");
            return false;
        }
        if (pick.empty()) {
            if (first_state.empty()) err(at, "cannot resolve " + text + " as state.element (" + what + ")", "known states: " + known_states());
            else err(at, "state " + first_state + " has no element " + first_rest + " (" + what + ")");
            return false;
        }
        state = pick[0].first;
        element = pick[0].second;
        return true;
    }

    bool has_element(const std::string& state, const std::string& element) const {
        auto it = states_.find(state);
        return it != states_.end() && (it->second.elems.count(element) || it->second.open);
    }

    bool has_trigger(const StInfo& s, const std::string& event) const {
        for (const auto& kv : s.arrows)
            if (kv.second.trigger == event) return true;
        return false;
    }

    // --- values -------------------------------------------------------------------------
    bool values(const Val& v, const std::string& key, std::vector<std::pair<std::string, Value>>& out, std::string& from_file) {
        switch (v.kind) {
            case Val::Kind::Number: out.push_back({key, Value{v.number}}); return true;
            case Val::Kind::Int: out.push_back({key, Value{v.integer}}); return true;
            case Val::Kind::Bool: out.push_back({key, Value{v.flag}}); return true;
            case Val::Kind::Text: out.push_back({key, Value{v.text}}); return true;
            case Val::Kind::Vector: {
                static const char* axes[] = {".x", ".y", ".z", ".w"};
                for (std::size_t i = 0; i < v.vec.size(); ++i) out.push_back({key + axes[i], Value{v.vec[i]}});
                return true;
            }
            case Val::Kind::File: {
                if (!opt_.read_file) {
                    err(v.at, "file(\"" + v.text + "\") reads the filesystem, and this source does none",
                        "external effects must cross a declared device/port boundary: give the text as a string, or compile the file ahead of time");
                    return false;
                }
                std::string body;
                if (!opt_.read_file(v.text, v.at.file, body)) {
                    err(v.at, "cannot read file(\"" + v.text + "\")");
                    return false;
                }
                from_file = v.text;
                out.push_back({key, Value{body}});
                return true;
            }
        }
        return false;
    }

    // A key a state or an element may carry: not a clock of its own, and not a
    // write into another state written as if it were a parameter.
    bool key_ok(const StInfo& s, const std::string& state, const std::string& key, const Loc& at) {
        if (s.kind != "temporal" && private_timer(key)) {
            err(at, "state " + state + " declares private timer " + key, "use a Temporal drive: `drive <clock> -> " + state + " event <event>`");
            return false;
        }
        const std::size_t dot = key.find('.');
        if (dot != std::string::npos) {
            const std::string head = key.substr(0, dot);
            if (head != state && states_.count(head)) {
                std::string via = "an arrow of " + head;
                for (const auto& sig : signatures_[head])
                    for (const std::string& a : sig.second)
                        if (a == key.substr(dot + 1)) via = sig.first;
                err(at, "direct write from " + state + " to " + key, "target " + via + " through a graph-declared interface");
                return false;
            }
        }
        return true;
    }

    void param_steps(const std::string& state, const std::string& element, const std::vector<ParamAst>& params, StInfo& s) {
        for (const ParamAst& p : params) {
            if (!key_ok(s, state, p.key, p.at)) continue;
            std::vector<std::pair<std::string, Value>> vs;
            std::string file;
            if (!values(p.value, p.key, vs, file)) continue;
            for (auto& kv : vs) {
                plan::Param step{Key{state}, Key{element}, Key{kv.first}, std::move(kv.second), file};
                state_steps_.push_back(std::move(step));
            }
        }
    }

    // --- natives ------------------------------------------------------------------------
    bool native_ok(const std::string& native, const Loc& at, const std::string& what) {
        const std::string w = io_word(native);
        if (w.empty()) return true;
        err(at, what + " performs " + (is_filesystem_word(w) ? "filesystem " : "") + "IO: native " + native,
            "external effects must cross a declared device/port boundary");
        return false;
    }

    // --- 1. states, as far as their names and kinds ---------------------------------------
    void collect_states() {
        for (const Program& p : progs_) {
            for (const StateAst& s : p.states) {
                if (states_.count(s.name)) {
                    err(s.at, "state " + s.name + " is declared twice");
                    continue;
                }
                StInfo info;
                info.ext = s.is_extern;
                info.open = s.is_extern;
                info.at = s.at;
                info.kind = s.is_extern && s.kind == "state" && !state_kind_written(s) ? std::string{} : s.kind;
                const KindInfo* k = info.kind.empty() ? nullptr : kinds_.find(info.kind);
                if (!k && !s.is_extern) {
                    std::string known;
                    for (const std::string& n : kinds_.names()) known += (known.empty() ? "" : ", ") + n;
                    err(s.at, "state " + s.name + ": no kind " + s.kind, "kinds: " + known);
                }
                if (s.is_extern && !opt_.live_states.empty()) {
                    auto live = opt_.live_states.find(s.name);
                    if (live == opt_.live_states.end()) {
                        err(s.at, "extern state " + s.name + " is not in the graph");
                    } else if (!info.kind.empty() && live->second != info.kind && info.kind != "state") {
                        err(s.at, "extern state " + s.name + " is " + live->second + ", not " + info.kind);
                    } else if (info.kind.empty()) {
                        info.kind = live->second;
                    }
                }
                for (const ArrowAst& a : s.arrows) signatures_[s.name].push_back({a.trigger.empty() ? a.name : a.trigger, a.args});
                if (k) {
                    // what the kind's own class makes: asked of a scratch instance
                    const std::unique_ptr<State> scratch = k->make(Key{"scratch"});
                    for (const Element& e : scratch->elements()) info.elems[e.id.str()] = ElemInfo{e.kind.str(), true};
                    for (const Morphism& m : scratch->morphisms())
                        info.arrows[m.name.str()] = ArrowInfo{m.from.str(), m.to.str(), m.trigger.str(), {}, true};
                    for (Key say : scratch->said()) info.says.insert(say.str());
                }
                states_[s.name] = std::move(info);
                order_.push_back(s.name);
            }
        }
    }

    static bool state_kind_written(const StateAst& s) { return s.kind != "state"; }

    void aliases_of(const Program& p) {
        for (const TransportAliasAst& t : p.transports) {
            if (aliases_.count(t.name)) {
                err(t.at, "transport " + t.name + " is declared twice");
                continue;
            }
            if (native_ok(t.native, t.at, "a functor transport")) aliases_[t.name] = t.native;
        }
    }

    // --- 2. what a state holds ------------------------------------------------------------
    plan::Arrow::Body lower_body(const BodyAst& b, Affine& a) {
        if (b.kind == BodyAst::Kind::Native) return plan::Arrow::Body::Native;
        if (b.kind == BodyAst::Kind::None) return plan::Arrow::Body::None;
        a = affine_of(b);
        return plan::Arrow::Body::Affine;
    }

    static Affine affine_of(const BodyAst& b) {
        Affine a;
        a.copy_all = b.copy_all;
        for (const RowAst& r : b.rows) {
            Affine::Row row;
            row.param = Key{r.param};
            row.bias = r.bias;
            for (const TermAst& t : r.terms) row.terms.push_back(Affine::Term{Key{t.param}, t.k, Key{t.arg}, t.of_target});
            a.rows.push_back(std::move(row));
        }
        return a;
    }

    void declared_states(const Program& p) {
        for (const StateAst& s : p.states) {
            if (!states_.count(s.name) || states_[s.name].at.line != s.at.line || states_[s.name].at.file != s.at.file) continue;
            StInfo& info = states_[s.name];
            if (s.is_extern) {
                extern_steps_.push_back(plan::Extern{Key{s.name}, info.kind});
                continue;
            }
            state_steps_.push_back(plan::State{Key{s.name}, s.kind});
            param_steps(s.name, "", s.params, info);
            std::set<std::string> seen;
            for (const ElementAst& e : s.elements) {
                if (!seen.insert(e.id).second) {
                    err(e.at, "element " + e.id + " of state " + s.name + " is declared twice");
                    continue;
                }
                auto have = info.elems.find(e.id);
                plan::Element step{Key{s.name}, Key{e.id}, Key{e.kind}, false};
                if (have != info.elems.end() && have->second.own) {
                    if (!e.kind.empty() && have->second.kind != e.kind) {
                        err(e.at, "element " + e.id + " of " + s.name + " is a " + have->second.kind + " (its kind makes it), not a " + e.kind);
                        continue;
                    }
                    step.kind = Key{have->second.kind};
                    step.own = true;
                } else {
                    if (e.kind.empty()) step.kind = Key{"object"};
                    info.elems[e.id] = ElemInfo{step.kind.str(), false};
                }
                state_steps_.push_back(step);
                param_steps(s.name, e.id, e.params, info);
            }
            for (const ArrowAst& a : s.arrows) arrow_of(s.name, info, a);
            for (const ComposeAst& c : s.composes) compose_arrows(s.name, info, c);
            for (const auto& say : s.says) {
                info.says.insert(say.first);
                state_steps_.push_back(plan::Says{Key{s.name}, Key{say.first}});
            }
        }
    }

    void arrow_of(const std::string& state, StInfo& info, const ArrowAst& a) {
        if (!info.elems.count(a.from)) return err(a.at, "state " + state + " has no element " + a.from + " (arrow " + a.name + ")");
        if (!info.elems.count(a.to)) return err(a.at, "state " + state + " has no element " + a.to + " (arrow " + a.name + ")");
        auto have = info.arrows.find(a.name);
        // an arrow its kind makes is declared as it is made: its own trigger, unless another is said
        const std::string trigger = !a.trigger.empty() ? a.trigger : have != info.arrows.end() && have->second.own ? have->second.trigger : a.name;
        const std::string to = a.to == a.from ? std::string{} : a.to;
        plan::Arrow step;
        step.state = Key{state};
        step.name = Key{a.name};
        step.from = Key{a.from};
        step.to = Key{to};
        step.trigger = Key{trigger};
        if (have != info.arrows.end()) {
            ArrowInfo& h = have->second;
            if (!h.own) return err(a.at, "arrow " + a.name + " of state " + state + " is declared twice");
            const std::string h_to = h.to.empty() || h.to == h.from ? std::string{} : h.to;
            if (h.from != a.from || h_to != to || h.trigger != trigger)
                return err(a.at, "arrow " + a.name + " of " + state + " is " + h.from + " -> " + (h_to.empty() ? h.from : h_to) + " on " + h.trigger +
                                     " (its kind makes it), not " + a.from + " -> " + a.to + " on " + trigger);
            if (a.body.kind != BodyAst::Kind::None)
                return err(a.at, "arrow " + a.name + " of " + state + " is made by its kind, with its body: it is declared here, not defined",
                           "remove the body; the declaration says what the arrow takes");
            step.own = true;
            h.args = a.args;
            h.own = true;
            state_steps_.push_back(std::move(step));
            return;
        }
        step.body = lower_body(a.body, step.affine);
        if (a.body.kind == BodyAst::Kind::Native) {
            if (!native_ok(a.body.native, a.at, "an arrow")) return;
            step.native = a.body.native;
        }
        info.arrows[a.name] = ArrowInfo{a.from, to, trigger, a.args, false};
        for (const std::string& arg : a.args)
            if (arg == "dt" || arg == "time" || arg == "frame") timed_.push_back({state, a.name, trigger, a.args, a.at});
        state_steps_.push_back(std::move(step));
    }

    void compose_arrows(const std::string& state, StInfo& info, const ComposeAst& c) {
        if (c.chain.size() != 2)
            return err(c.at, "an arrow composition takes two arrows, first applied first",
                       "compose the first two under a name, and compose that with the third");
        if (info.arrows.count(c.name)) return err(c.at, "arrow " + c.name + " of state " + state + " is declared twice");
        const ArrowInfo* f = nullptr;
        const ArrowInfo* g = nullptr;
        for (int i = 0; i < 2; ++i) {
            auto it = info.arrows.find(c.chain[static_cast<std::size_t>(i)]);
            if (it == info.arrows.end()) return err(c.at, "state " + state + " has no arrow " + c.chain[static_cast<std::size_t>(i)] + " to compose");
            (i == 0 ? f : g) = &it->second;
        }
        const std::string cod_f = f->to.empty() ? f->from : f->to;
        if (cod_f != g->from)
            return err(c.at, "compose: cod(" + c.chain[0] + ")=" + cod_f + " != dom(" + c.chain[1] + ")=" + g->from);
        const std::string cod_g = g->to.empty() ? g->from : g->to;
        ArrowInfo made;
        made.from = f->from;
        made.to = cod_g == f->from ? std::string{} : cod_g;
        made.trigger = c.trigger.empty() ? c.name : c.trigger;
        info.arrows[c.name] = made;
        state_steps_.push_back(plan::Compose{Key{state}, Key{c.name}, Key{c.chain[0]}, Key{c.chain[1]}, Key{made.trigger}});
    }

    // --- 3. functors --------------------------------------------------------------------
    plan::Object object_of(const std::string& functor, const ObjectAst& o) {
        plan::Object step;
        step.functor = Key{functor};
        step.src = Key{o.src};
        step.dst = Key{o.dst};
        const TransportAst& t = o.transport;
        switch (t.kind) {
            case TransportAst::Kind::Copy: break;
            case TransportAst::Kind::Only:
                step.transport = plan::Object::Transport::Only;
                for (const std::string& n : t.names) step.names.push_back(Key{n});
                break;
            case TransportAst::Kind::Swizzle:
                step.transport = plan::Object::Transport::Swizzle;
                for (const auto& pr : t.pairs) step.pairs.push_back({Key{pr.first}, Key{pr.second}});
                step.rest = t.rest;
                break;
            case TransportAst::Kind::Rows:
                step.transport = plan::Object::Transport::Affine;
                step.affine = affine_of(t.rows);
                break;
            case TransportAst::Kind::Native:
                step.transport = plan::Object::Transport::Native;
                step.native = t.native;
                native_ok(t.native, o.at, "a functor transport");
                break;
            case TransportAst::Kind::Via: {
                auto it = aliases_.find(t.native);
                if (it == aliases_.end()) {
                    err(o.at, "no transport " + t.native, "declare it: `transport " + t.native + " native <name>`");
                } else {
                    step.transport = plan::Object::Transport::Native;
                    step.native = it->second;
                }
                break;
            }
        }
        return step;
    }

    void functors_of(const Program& p) {
        for (const FunctorAst& f : p.functors) {
            if (functors_.count(f.name) || (!opt_.conform && opt_.live_functors.count(f.name) > (f.is_extern ? 1u : 0u))) {
                err(f.at, "functor " + f.name + " is declared twice");
                continue;
            }
            const StInfo* from = state_named(f.from, f.at, "the source of functor " + f.name);
            const StInfo* to = state_named(f.to, f.at, "the target of functor " + f.name);
            if (!from || !to) continue;
            if (f.is_extern) {
                auto live = opt_.live_functors.find(f.name);
                if (!opt_.live_functors.empty() && live == opt_.live_functors.end())
                    err(f.at, "extern functor " + f.name + " is not in the graph");
                else if (live != opt_.live_functors.end() && (live->second.first != f.from || live->second.second != f.to))
                    err(f.at, "extern functor " + f.name + " is " + live->second.first + " -> " + live->second.second);
                functors_[f.name] = FInfo{f.from, f.to, true};
                functor_steps_.push_back(plan::Functor{Key{f.name}, Key{f.from}, Key{f.to}, true});
                continue;
            }
            functors_[f.name] = FInfo{f.from, f.to, false};
            functor_steps_.push_back(plan::Functor{Key{f.name}, Key{f.from}, Key{f.to}, false});
            std::set<std::string> mapped;
            for (const ObjectAst& o : f.objects) {
                if (!has_element(f.from, o.src)) {
                    err(o.at, "state " + f.from + " has no element " + o.src + " (functor " + f.name + ")");
                    continue;
                }
                if (!has_element(f.to, o.dst)) {
                    err(o.at, "state " + f.to + " has no element " + o.dst + " (functor " + f.name + ")");
                    continue;
                }
                if (!mapped.insert(o.src).second) {
                    err(o.at, "functor " + f.name + " maps object " + o.src + " twice");
                    continue;
                }
                functor_steps_.push_back(object_of(f.name, o));
            }
            for (const MapAst& m : f.events) functor_steps_.push_back(plan::EventMap{Key{f.name}, Key{m.src}, Key{m.dst}});
            for (const MapAst& m : f.arrows) {
                if (!from->arrows.count(m.src) && !from->open) {
                    err(m.at, "state " + f.from + " has no arrow " + m.src + " (functor " + f.name + ")");
                    continue;
                }
                if (!to->arrows.count(m.dst) && !to->open) {
                    err(m.at, "state " + f.to + " has no arrow " + m.dst + " (functor " + f.name + ")");
                    continue;
                }
                functor_steps_.push_back(plan::ArrowMap{Key{f.name}, Key{m.src}, Key{m.dst}});
            }
        }
    }

    const FInfo* functor_named(const std::string& name, const Loc& at, const std::string& what) {
        auto it = functors_.find(name);
        if (it == functors_.end()) {
            err(at, "no functor " + name + " (" + what + ")", "a functor built in C++ is named with `extern functor " + name + " : <from> -> <to>`");
            return nullptr;
        }
        return &it->second;
    }

    void composes_of(const Program& p) {
        for (const ComposeFunctorsAst& c : p.functor_composes) {
            if (functors_.count(c.name)) {
                err(c.at, "functor " + c.name + " is declared twice");
                continue;
            }
            std::vector<const FInfo*> chain;
            bool ok = true;
            for (const std::string& n : c.chain) {
                const FInfo* f = functor_named(n, c.at, "composed into " + c.name);
                if (!f) ok = false;
                chain.push_back(f);
            }
            if (!ok) continue;
            for (std::size_t i = 0; i + 1 < chain.size(); ++i)
                if (chain[i]->to != chain[i + 1]->from) {
                    err(c.at, "compose: cod(" + c.chain[i] + ")=" + chain[i]->to + " != dom(" + c.chain[i + 1] + ")=" + chain[i + 1]->from);
                    ok = false;
                    break;
                }
            if (!ok) continue;
            functors_[c.name] = FInfo{chain.front()->from, chain.back()->to, false};
            plan::ComposeFunctors step;
            step.name = Key{c.name};
            for (const std::string& n : c.chain) step.chain.push_back(Key{n});
            compose_steps_.push_back(std::move(step));
        }
    }

    void lenses_of(const Program& p) {
        for (const LensAst& l : p.lenses) {
            const FInfo* g = functor_named(l.get, l.at, "the lens's get");
            const FInfo* u = functor_named(l.put, l.at, "the lens's put");
            if (!g || !u) continue;
            if (g->from != u->to || g->to != u->from) {
                err(l.at, "a lens is a functor and its way back: " + l.get + " is " + g->from + " -> " + g->to + ", " + l.put + " is " + u->from + " -> " + u->to);
                continue;
            }
            relation_steps_.push_back(plan::Lens{Key{l.get}, Key{l.put}});
        }
    }

    // --- 4. how states meet -----------------------------------------------------------------
    void transitions_of(const Program& p) {
        for (const TransitionAst& t : p.transitions) {
            if (t.from != "*" && !state_named(t.from, t.at, "the source of a transition")) continue;
            if (!t.pop && !state_named(t.to, t.at, "the target of a transition")) continue;
            sg::Transition tr;
            tr.from = Key{t.from};
            tr.to = Key{t.to};
            tr.trigger = Key{t.trigger};
            tr.kind = t.pop ? TransitionKind::Pop : t.push ? TransitionKind::Push : TransitionKind::Switch;
            tr.name = Key{t.name};
            if (!t.carry.empty()) {
                const FInfo* f = functor_named(t.carry, t.at, "carried by a transition");
                if (!f) continue;
                if (t.from != "*" && f->from != t.from)
                    err(t.at, "transition " + t.from + " -[" + t.trigger + "]-> " + t.to + " carries " + t.carry + ", which starts at " + f->from);
                if (f->to != t.to) err(t.at, "transition " + t.from + " -[" + t.trigger + "]-> " + t.to + " carries " + t.carry + ", which ends at " + f->to);
                tr.functor = Key{t.carry};
            }
            if (!t.name.empty() && !transition_names_.insert(t.name).second) {
                err(t.at, "transition " + t.name + " is declared twice");
                continue;
            }
            plan::Connect step;
            step.t = std::move(tr);
            for (const ParamAst& w : t.with) {
                std::vector<std::pair<std::string, Value>> vs;
                std::string file;
                if (w.value.kind == Val::Kind::File) {
                    err(w.at, "a transition tells the state it enters numbers, flags and words, not a file");
                    continue;
                }
                if (values(w.value, w.key, vs, file))
                    for (auto& kv : vs) step.t.enter.set(Key{kv.first}, std::move(kv.second));
            }
            relation_steps_.push_back(std::move(step));
        }
    }

    void embeds_of(const Program& p) {
        for (const EmbedAst& e : p.embeds) {
            std::string host, portal;
            if (!element_ref(e.host_portal, e.at, host, portal, "the portal of an embedding")) continue;
            if (!state_named(e.guest, e.at, "the guest of an embedding")) continue;
            sg::Embedding em;
            em.host = Key{host};
            em.portal = Key{portal};
            em.guest = Key{e.guest};
            std::string subject_state = host;
            if (!e.subject.empty()) {
                if (!state_named(e.subject, e.at, "the subject of an embedding")) continue;
                if (e.subject != host) em.subject = Key{e.subject};  // the host is the engine's default: said as empty
                subject_state = e.subject;
            }
            bool ok = true;
            if (!e.in.empty()) {
                const FInfo* f = functor_named(e.in, e.at, "the `in` of an embedding");
                if (!f) continue;
                if (f->from != subject_state || f->to != e.guest) {
                    err(e.at, "embedding `in` " + e.in + " should run " + subject_state + " -> " + e.guest + ", not " + f->from + " -> " + f->to);
                    ok = false;
                }
                em.in = Key{e.in};
            }
            if (!e.out.empty()) {
                const FInfo* f = functor_named(e.out, e.at, "the `out` of an embedding");
                if (!f) continue;
                if (f->from != e.guest || f->to != subject_state) {
                    err(e.at, "embedding `out` " + e.out + " should run " + e.guest + " -> " + subject_state + ", not " + f->from + " -> " + f->to);
                    ok = false;
                }
                em.out = Key{e.out};
            }
            if (!e.sync.empty()) {
                if (e.sync == "live") em.sync = EmbedSync::Live;
                else if (e.sync == "commit") em.sync = EmbedSync::Commit;
                else if (e.sync == "view") em.sync = EmbedSync::View;
                else {
                    err(e.at, "sync " + e.sync + ": live, commit or view");
                    ok = false;
                }
            }
            if (!e.propagate.empty()) {
                if (e.propagate == "onchange") em.propagate = Propagation::OnChange;
                else if (e.propagate == "continuous") em.propagate = Propagation::Continuous;
                else if (e.propagate == "onevent") em.propagate = Propagation::OnEvent;
                else if (e.propagate == "manual") em.propagate = Propagation::Manual;
                else {
                    err(e.at, "propagate " + e.propagate + ": onchange, continuous, onevent or manual");
                    ok = false;
                }
            }
            if (e.focus >= 0) em.focus = e.focus == 1;
            if (e.follows >= 0) em.follows = e.follows == 1;
            em.name = Key{e.name.empty() ? host + "/" + portal + ":" + e.guest : e.name};
            if (!embed_names_.insert(em.name.str()).second) {
                err(e.at, "embedding " + em.name.str() + " is declared twice");
                ok = false;
            }
            if (ok) relation_steps_.push_back(plan::Embed{std::move(em)});
        }
    }

    void wears_of(const Program& p) {
        for (const WearAst& w : p.wears) {
            const StInfo* host = state_named(w.host, w.at, "the state that wears a look");
            const StInfo* look = state_named(w.look, w.at, "the look worn");
            if (!host || !look) continue;
            if (look->kind != "look") {
                err(w.at, w.look + " is a " + (look->kind.empty() ? "state" : look->kind) + ", not a look");
                continue;
            }
            const Key name = wear_embedding(Key{w.host}, Key{w.look}).name;
            if (!embed_names_.insert(name.str()).second) {
                err(w.at, w.host + " wears " + w.look + " twice");
                continue;
            }
            wear_steps_.push_back(plan::Wear{Key{w.host}, Key{w.look}});
        }
    }

    void films_of(const Program& p) {
        for (const FilmAst& f : p.films) {
            const StInfo* cam = state_named(f.camera, f.at, "the camera");
            const StInfo* world = state_named(f.world, f.at, "the world filmed");
            if (!cam || !world) continue;
            if (cam->kind != "camera" && !(cam->ext && cam->kind.empty())) {
                err(f.at, f.camera + " is a " + (cam->kind.empty() ? "state" : cam->kind) + ", not a camera");
                continue;
            }
            if (!f.rig.empty() && !world->elems.count(f.rig) && !world->open) {
                err(f.at, "state " + f.world + " has no element " + f.rig + " to rig the camera to");
                continue;
            }
            const Key name = film_name(Key{f.camera}, Key{f.world});
            if (!embed_names_.insert(name.str()).second) {
                err(f.at, f.camera + " films " + f.world + " twice");
                continue;
            }
            wear_steps_.push_back(plan::Film{Key{f.camera}, Key{f.world}, Key{f.rig}});
        }
    }

    void seams_of(const Program& p) {
        for (const SeamAst& s : p.seams) {
            std::string sa, pa, sb, pb;
            if (!element_ref(s.a, s.at, sa, pa, "a side of a seam") || !element_ref(s.b, s.at, sb, pb, "a side of a seam")) continue;
            bool ok = true;
            for (const auto& side : {std::make_pair(sa, pa), std::make_pair(sb, pb)}) {
                auto it = states_[side.first].elems.find(side.second);
                if (it != states_[side.first].elems.end() && it->second.kind != "portal") {
                    err(s.at, side.first + "." + side.second + " is a " + it->second.kind + ", not a portal: a seam identifies two boundaries, doorways");
                    ok = false;
                }
            }
            plan::Glue g;
            g.a = Key{sa};
            g.pa = Key{pa};
            g.b = Key{sb};
            g.pb = Key{pb};
            g.name = Key{s.name.empty() ? s.a + "<->" + s.b : s.name};
            g.wraps = s.wraps;
            if (sa == sb && !s.wraps) {
                err(s.at, "a seam joining " + sa + " to itself is the shape of its space: say it `wraps`");
                ok = false;
            }
            for (const auto& also : s.also) {
                std::string x, y;
                const bool ax = split(also.first, x, y) && x == sa && !y.empty();
                const std::string ex = ax ? y : also.first;
                std::string x2, y2;
                const bool bx = split(also.second, x2, y2) && x2 == sb && !y2.empty();
                const std::string ey = bx ? y2 : also.second;
                if (!has_element(sa, ex) || !has_element(sb, ey)) {
                    err(s.at, "a seam's `also` names " + sa + "." + ex + " <-> " + sb + "." + ey + ", which is not both elements");
                    ok = false;
                    continue;
                }
                g.also.push_back({Key{ex}, Key{ey}});
            }
            if (!seam_names_.insert(g.name.str()).second) {
                err(s.at, "seam " + g.name.str() + " is declared twice");
                ok = false;
            }
            if (ok) {
                // what a seam makes: its travel (a viewer carried across, both
                // ways) and its glue - functors a transition may carry
                const Seam made = doorway_seam(g.name, g.a, g.pa, g.b, g.pb, g.also, g.wraps);
                functors_[made.a_to_b.str()] = FInfo{sa, sb, false};
                functors_[made.b_to_a.str()] = FInfo{sb, sa, false};
                functors_[made.glue_ab.str()] = FInfo{sa, sb, false};
                functors_[made.glue_ba.str()] = FInfo{sb, sa, false};
                glue_steps_.push_back(std::move(g));
            }
        }
    }

    // --- 5. time, the world outside, what follows -----------------------------------------
    // `state.event`: the state an event is named for, and the event as it is named.
    bool state_event(const std::string& text, const Loc& at, std::string& state, const std::string& what) {
        std::string rest;
        if (!split(text, state, rest) || rest.empty()) {
            err(at, "cannot tell which state " + text + " belongs to (" + what + ")",
                "write `<state>.<event>` for an event named for its state, or name the event: `event <event>`; states: " + known_states());
            return false;
        }
        return true;
    }

    void drives_of(const Program& p) {
        for (const DriveAst& d : p.drives) {
            const StInfo* clock = state_named(d.clock, d.at, "the clock of a drive");
            if (!clock) continue;
            if (clock->kind != "temporal") {
                err(d.at, "a drive is kept on a Temporal: " + d.clock + " is " + (clock->kind.empty() ? "a state of no known kind" : "a " + clock->kind),
                    "declare it `state " + d.clock + " : temporal`, or `extern state " + d.clock + " : temporal`");
                continue;
            }
            std::string state, event = d.event;
            if (event.empty()) {
                if (!state_event(d.target, d.at, state, "the state a drive drives")) continue;
                event = d.target;
            } else if (!state_named(d.target, d.at, "the state a drive drives")) {
                continue;
            } else {
                state = d.target;
            }
            const StInfo& target = states_[state];
            if (!target.open && !has_trigger(target, event)) {
                err(d.at, "no arrow of " + state + " is triggered by " + event + ": a drive fires the arrows on its event",
                    "declare one: `<from> -> <to> : <name>(dt) on " + event + " { ... }`");
                continue;
            }
            sg::Drive dr;
            dr.clock = Key{d.clock};
            dr.state = Key{state};
            dr.trigger = Key{event};
            dr.additive = d.additive;
            dr.line = Key{state};
            dr.name = Key{d.clock + ">" + state};
            const std::string keeps = d.keeps.empty() ? "active" : d.keeps;
            if (keeps == "active") dr.keeps = Keeps::WhileActive;
            else if (keeps == "always") dr.keeps = Keeps::Always;
            else if (keeps == "shown") dr.keeps = Keeps::WhileShown;
            else if (keeps == "focused") dr.keeps = Keeps::WhileFocused;
            else if (keeps == "entered") dr.keeps = Keeps::WhileEntered;
            else {
                err(d.at, "keeps " + keeps + ": active, always, shown, focused or entered");
                continue;
            }
            if (!drive_names_.insert(dr.name.str()).second) {
                err(d.at, "state " + state + " is driven twice by " + d.clock + ": it has one line on a clock");
                continue;
            }
            driven_.insert({state, event});
            drive_steps_.push_back(plan::Drive{std::move(dr)});
        }
    }

    void ports_of(const Program& p) {
        for (const PortAst& pt : p.ports) {
            std::string state, event = pt.event;
            if (event.empty()) {
                if (!state_event(pt.target, pt.at, state, "a port")) continue;
                event = pt.target;
            } else if (!state_named(pt.target, pt.at, "the state a port opens")) {
                continue;
            } else {
                state = pt.target;
            }
            const StInfo& s = states_[state];
            if (!s.open && !has_trigger(s, event)) {
                err(pt.at, "nothing in " + state + " answers " + event + ": a port lets the world outside speak to a state's arrows");
                continue;
            }
            port_steps_.push_back(plan::Port{Key{state}, Key{event}});
        }
    }

    void keeps_of(const Program& p) {
        for (const KeepAst& k : p.keeps)
            if (functor_named(k.functor, k.at, "kept")) other_steps_.push_back(plan::Keep{Key{k.functor}});
    }

    // when A B: the event A, said by its state, is B in another - a functor's
    // event map, and nothing that runs.
    void whens_of(const Program& p) {
        std::set<std::string> made;
        for (const WhenAst& w : p.whens) {
            std::string from, to;
            if (!state_event(w.event, w.at, from, "the event a `when` waits for") || !state_event(w.target, w.at, to, "the event a `when` answers with")) continue;
            const StInfo& fs = states_[from];
            const StInfo& ts = states_[to];
            if (!fs.open && !fs.says.count(w.event)) {
                err(w.at, from + " does not say " + w.event + ": a functor carries only what a state says", "declare it in " + from + ": `say " + w.event + "`");
                continue;
            }
            if (!ts.open && !has_trigger(ts, w.target)) {
                err(w.at, "no arrow of " + to + " is triggered by " + w.target, "a `when` answers with an event some arrow of the target state listens for");
                continue;
            }
            const std::string name = "when:" + from + ">" + to;
            if (functors_.count(name) && !made.count(name)) {
                err(w.at, "functor " + name + " already exists: a `when` between " + from + " and " + to + " would be a second one");
                continue;
            }
            if (made.insert(name).second) {
                functors_[name] = FInfo{from, to, false};
                functor_steps_.push_back(plan::Functor{Key{name}, Key{from}, Key{to}, false});
            }
            functor_steps_.push_back(plan::EventMap{Key{name}, Key{w.event}, Key{w.target}});
        }
    }

    void edits_of(const Program& p) {
        for (const EditAst& e : p.edits) {
            const StInfo* s = state_named(e.state, e.at, "the state that asks for an edit");
            if (!s) continue;
            if (!s->open && !s->says.count(e.event)) {
                err(e.at, e.state + " does not say " + e.event + ": an edit is what a state says to ask", "declare it in " + e.state + ": `say " + e.event + "`");
                continue;
            }
            if (!native_ok(e.native, e.at, "an edit")) continue;
            const std::string name = e.state + ":" + e.event;
            if (!edit_names_.insert(name).second) {
                err(e.at, "edit " + name + " is declared twice");
                continue;
            }
            other_steps_.push_back(plan::Edit{Key{e.state}, Key{e.event}, Key{e.reply.empty() ? e.event + ".done" : e.reply}, e.native});
        }
    }

    // initial: the state the graph starts in - graph.set_initial.
    void initials_of(const Program& p) {
        for (const InitialAst& i : p.initials) {
            if (!state_named(i.state, i.at, "the initial state")) continue;
            if (have_initial_) {
                err(i.at, "the graph has one initial state, and it is already named");
                continue;
            }
            have_initial_ = true;
            other_steps_.push_back(plan::Initial{Key{i.state}});
        }
    }

    // bind: a table for an input adapter. Each entry is an event that is fired;
    // it names an arrow's trigger, or it names nothing.
    void binds_of(const Program& p) {
        bool any_open = false;
        for (const auto& kv : states_) any_open = any_open || kv.second.open;
        for (const BindAst& b : p.binds) {
            plan::Bind step;
            step.device = b.device;
            std::set<std::string> keys;
            for (const BindEntryAst& e : b.entries) {
                if (!keys.insert(e.key).second) {
                    err(e.at, "key " + e.key + " is bound twice on " + b.device);
                    continue;
                }
                std::string state, rest;
                bool answered = false;
                if (split(e.event, state, rest) && !rest.empty()) {
                    const StInfo& s = states_[state];
                    answered = s.open || has_trigger(s, e.event);
                    if (!answered)
                        err(e.at, "no arrow of " + state + " is triggered by " + e.event + ": input fires the event of an arrow, it never writes a state");
                } else {
                    for (const auto& kv : states_) answered = answered || kv.second.open || has_trigger(kv.second, e.event);
                    if (!answered && !any_open)
                        err(e.at, "no arrow is triggered by " + e.event, "input fires the event of an arrow: declare the arrow, then bind the key to its event");
                }
                Params args;
                bool ok = true;
                for (const auto& a : e.args) {
                    std::vector<std::pair<std::string, Value>> vs;
                    std::string file;
                    if (a.second.kind == Val::Kind::File) {
                        err(a.second.at, "an input event carries numbers, flags and words, not a file");
                        ok = false;
                        continue;
                    }
                    if (!values(a.second, a.first, vs, file)) {
                        ok = false;
                        continue;
                    }
                    for (auto& kv : vs) args.set(Key{kv.first}, std::move(kv.second));
                }
                if (ok) step.entries.push_back({e.key, Key{e.event}, std::move(args)});
            }
            other_steps_.push_back(std::move(step));
        }
    }

    // An arrow that reads dt (or the time) is a state changing with time: it
    // has a clock's drive, or it is not driven at all.
    void time_rule() {
        for (const Timed& t : timed_) {
            if (driven_.count({t.state, t.trigger})) continue;
            err(t.at, "arrow " + t.state + "." + t.arrow + " reads " + t.args.front() + ", but no drive keeps event " + t.trigger + " going",
                "a state that changes with time is driven: `drive <clock> -> " + t.state + " event " + t.trigger + "`");
        }
    }

    void assemble() {
        for (auto* group : {&extern_steps_, &state_steps_, &functor_steps_, &compose_steps_, &relation_steps_, &wear_steps_, &glue_steps_,
                            &drive_steps_, &port_steps_, &other_steps_})
            for (Step& s : *group) out_.plan.steps.push_back(std::move(s));
    }
};

}  // namespace

Compiled compile(const std::vector<Program>& programs, const Options& o) {
    return Compiler(programs, o).run();
}

Compiled compile_source(const std::string& text, const std::string& file, const Options& o) {
    Parsed parsed = parse(text, file);
    if (!parsed.ok()) {
        Compiled c;
        c.errors = std::move(parsed.errors);
        return c;
    }
    return compile({std::move(parsed.program)}, o);
}

}  // namespace sg::dsl
