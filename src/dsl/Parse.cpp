#include "sg/dsl/Parse.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace sg::dsl {

std::string Diagnostic::str() const {
    std::string out = at.file + ":" + std::to_string(at.line) + ":" + std::to_string(at.col) + ": error: " + message;
    if (!hint.empty()) out += "\n    " + hint;
    return out;
}

namespace {

// --- tokens ---------------------------------------------------------------------
enum class T { Ident, Str, Num, Punct, End };

struct Tok {
    T t = T::End;
    std::string s;
    double num = 0.0;
    int line = 0, col = 0;
};

struct Fail {};  // the item being read cannot go on; its error is already noted

struct Lexer {
    const std::string& src;
    std::string file;
    std::vector<Tok> out;
    std::vector<Diagnostic>& errors;
    std::size_t i = 0;
    int line = 1;
    std::size_t line_start = 0;

    char at(std::size_t k) const { return k < src.size() ? src[k] : '\0'; }
    int col() const { return static_cast<int>(i - line_start) + 1; }
    void error(const std::string& m) { errors.push_back({{file, line, col()}, m, ""}); }

    void push(T t, std::string s, int c, double n = 0.0) {
        Tok k;
        k.t = t;
        k.s = std::move(s);
        k.num = n;
        k.line = line;
        k.col = c;
        out.push_back(std::move(k));
    }

    void run() {
        while (i < src.size()) {
            const char c = src[i];
            if (c == '\n') {
                ++i;
                ++line;
                line_start = i;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                ++i;
            } else if (c == '#' || (c == '/' && at(i + 1) == '/')) {
                while (i < src.size() && src[i] != '\n') ++i;
            } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                const int c0 = col();
                std::size_t j = i;
                while (j < src.size() && (std::isalnum(static_cast<unsigned char>(src[j])) || src[j] == '_' ||
                                          (src[j] == '.' && (std::isalnum(static_cast<unsigned char>(at(j + 1))) || at(j + 1) == '_'))))
                    ++j;
                push(T::Ident, src.substr(i, j - i), c0);
                i = j;
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                const int c0 = col();
                std::size_t j = i;
                while (std::isdigit(static_cast<unsigned char>(at(j)))) ++j;
                if (at(j) == '.' && std::isdigit(static_cast<unsigned char>(at(j + 1)))) {
                    ++j;
                    while (std::isdigit(static_cast<unsigned char>(at(j)))) ++j;
                }
                if ((at(j) == 'e' || at(j) == 'E') &&
                    (std::isdigit(static_cast<unsigned char>(at(j + 1))) ||
                     ((at(j + 1) == '-' || at(j + 1) == '+') && std::isdigit(static_cast<unsigned char>(at(j + 2)))))) {
                    j += 2;
                    while (std::isdigit(static_cast<unsigned char>(at(j)))) ++j;
                }
                const std::string text = src.substr(i, j - i);
                push(T::Num, text, c0, std::strtod(text.c_str(), nullptr));
                i = j;
            } else if (c == '"') {
                const int c0 = col();
                std::string s;
                ++i;
                bool closed = false;
                while (i < src.size()) {
                    if (src[i] == '"') {
                        closed = true;
                        ++i;
                        break;
                    }
                    if (src[i] == '\\' && i + 1 < src.size()) {
                        ++i;
                        s += src[i] == 'n' ? '\n' : src[i] == 't' ? '\t' : src[i];
                    } else {
                        if (src[i] == '\n') {
                            ++line;
                            line_start = i + 1;
                        }
                        s += src[i];
                    }
                    ++i;
                }
                if (!closed) error("a string is not closed");
                push(T::Str, s, c0);
            } else {
                const int c0 = col();
                const auto starts = [&](const char* p) { return src.compare(i, std::strlen(p), p) == 0; };
                for (const char* p : {"<->", "->", "<-", "-[", "]->", "+=", "-=", "==", "!=", "<=", ">="}) {
                    if (starts(p)) {
                        push(T::Punct, p, c0);
                        i += std::strlen(p);
                        goto next;
                    }
                }
                if (std::strchr("{}()[],;=:*+-<>.", c)) {
                    push(T::Punct, std::string(1, c), c0);
                    ++i;
                } else {
                    error(std::string("unexpected character '") + c + "'");
                    ++i;
                }
            }
        next:;
        }
        Tok end;
        end.t = T::End;
        end.line = line;
        end.col = col();
        out.push_back(end);
    }
};

// What has no equivalent here, and why: the patterns the engine's rules refuse.
struct Refused {
    const char* what;
    const char* why;
    const char* instead;
};

const std::map<std::string, Refused>& refused() {
    static const std::map<std::string, Refused> m = {
        {"on_update", {"`on_update`", "behaviour is arrows, and time reaches a state through a drive",
                       "give the state an arrow on an event, and `drive <clock> -> <state> event <event>`"}},
        {"on_render", {"`on_render`", "a state acts only through its arrows", "declare an arrow on an event"}},
        {"callback", {"a callback", "a callback that changes another state is a way between states the graph does not declare",
                      "states meet only through graph-declared interfaces: a functor, transition, embedding, port or edit"}},
        {"listener", {"a listener", "listeners observe; only the world changes the world",
                      "declare a functor, transition, embedding, port or edit"}},
        {"emit", {"`emit`", "emit + dispatch as orchestration is a way between states the graph does not declare",
                  "a state `say`s an event and the graph carries it (functor, transition, edit)"}},
        {"dispatch", {"`dispatch`", "dispatching by hand is orchestration outside the graph",
                      "a state `say`s an event and the graph carries it"}},
        {"timer", {"a timer", "a state's clock is its own line on the Temporal; a timer would be a second one", "use a Temporal drive"}},
        {"tick", {"a tick", "a state's clock is its own line on the Temporal; a tick would be a second one", "use a Temporal drive"}},
        {"sleep", {"a sleep", "there is no wall clock in the model", "use a Temporal drive"}},
        {"schedule", {"a scheduler", "there is one clock, a Temporal state", "use a Temporal drive"}},
        {"mode", {"a mode", "which guest has the input is focus, which the graph already keeps",
                  "declare `embed ... focus true`"}},
        {"call", {"a call", "states are not called: they are reached through the graph", "fire an event, through a declared interface"}},
        {"script", {"a script", "there is no second execution model", "declare states, arrows and graph relations"}},
    };
    return m;
}

struct Parser {
    std::vector<Tok> toks;
    std::vector<Diagnostic>& errors;
    Program& prog;
    std::string file;
    std::size_t p = 0;

    const Tok& cur() const { return toks[p]; }
    const Tok& peek(std::size_t k = 1) const { return toks[std::min(p + k, toks.size() - 1)]; }
    bool end() const { return cur().t == T::End; }
    Loc loc() const { return {file, cur().line, cur().col}; }
    Loc loc_of(const Tok& t) const { return {file, t.line, t.col}; }

    [[noreturn]] void fail(const std::string& msg, const std::string& hint = {}) {
        errors.push_back({loc(), msg, hint});
        throw Fail{};
    }
    [[noreturn]] void fail_at(const Loc& at, const std::string& msg, const std::string& hint = {}) {
        errors.push_back({at, msg, hint});
        throw Fail{};
    }

    bool is_punct(const char* s, std::size_t k = 0) const { return peek(k).t == T::Punct && peek(k).s == s; }
    bool is_word(const char* s, std::size_t k = 0) const { return peek(k).t == T::Ident && peek(k).s == s; }
    bool accept(const char* s) {
        if (is_punct(s)) {
            ++p;
            return true;
        }
        return false;
    }
    bool accept_word(const char* s) {
        if (is_word(s)) {
            ++p;
            return true;
        }
        return false;
    }
    void expect(const char* s, const char* what = nullptr) {
        if (!accept(s)) fail(std::string("expected '") + s + "'" + (what ? std::string(" ") + what : ""), "");
    }
    void expect_word(const char* s) {
        if (!accept_word(s)) fail(std::string("expected `") + s + "`");
    }

    // A name: an identifier, or a string for one that has odd characters.
    std::string name(const char* what) {
        if (cur().t == T::Ident || cur().t == T::Str) return toks[p++].s;
        fail(std::string("expected ") + what);
    }

    // --- values -------------------------------------------------------------------
    Val value() {
        Val v;
        v.at = loc();
        bool neg = false;
        if (accept("-")) neg = true;
        if (cur().t == T::Num) {
            v.kind = Val::Kind::Number;
            v.number = neg ? -cur().num : cur().num;
            ++p;
            return v;
        }
        if (neg) fail("expected a number after '-'");
        if (cur().t == T::Str) {
            v.kind = Val::Kind::Text;
            v.text = toks[p++].s;
            return v;
        }
        if (is_word("true") || is_word("false")) {
            v.kind = Val::Kind::Bool;
            v.flag = cur().s == "true";
            ++p;
            return v;
        }
        if (is_word("int") && is_punct("(", 1)) {
            p += 2;
            bool n = accept("-");
            if (cur().t != T::Num) fail("expected an integer in int(...)");
            v.kind = Val::Kind::Int;
            v.integer = static_cast<int64_t>(n ? -cur().num : cur().num);
            ++p;
            expect(")");
            return v;
        }
        if (is_word("file") && is_punct("(", 1)) {
            p += 2;
            if (cur().t != T::Str) fail("file(...) takes a path in quotes");
            v.kind = Val::Kind::File;
            v.text = toks[p++].s;
            expect(")");
            return v;
        }
        if (accept("[")) {
            v.kind = Val::Kind::Vector;
            while (!is_punct("]")) {
                const bool m = accept("-");
                if (cur().t != T::Num) fail("expected a number in a vector");
                v.vec.push_back(m ? -cur().num : cur().num);
                ++p;
                if (!accept(",")) break;
            }
            expect("]");
            if (v.vec.size() < 2 || v.vec.size() > 4) fail_at(v.at, "a vector has two to four components");
            return v;
        }
        if (cur().t == T::Ident) fail("a word is not a value: put text in quotes", "");
        fail("expected a value");
    }

    // --- a declared step: rows of affine expressions ------------------------------------
    // param = k * param * arg + ... ; each term names at most one parameter and one
    // event argument (the ones in `args`), and numbers.
    void rows(BodyAst& body, const std::vector<std::string>& args, const std::string& target) {
        body.kind = BodyAst::Kind::Rows;
        expect("{");
        if (accept_word("copy")) body.copy_all = true;
        while (!is_punct("}")) {
            if (end()) fail("a '{' is not closed");
            if (accept(";")) continue;
            RowAst row;
            row.at = loc();
            row.param = name("the parameter to set");
            expect("=");
            expression(row, args, target);
            body.rows.push_back(std::move(row));
        }
        expect("}");
    }

    void expression(RowAst& row, const std::vector<std::string>& args, const std::string& target) {
        bool first = true;
        for (;;) {
            double sign = 1.0;
            if (accept("-")) sign = -1.0;
            else if (!first && !accept("+")) break;
            first = false;
            TermAst t;
            t.k = sign;
            bool any = false;
            for (;;) {
                if (cur().t == T::Num) {
                    t.k *= cur().num;
                    ++p;
                } else if (cur().t == T::Ident && !(is_punct("=", 1))) {
                    if (cur().s == "file" && is_punct("(", 1))
                        fail("file(...) reads the filesystem: a step of an arrow or a transport does no IO",
                             "external effects must cross a declared device/port boundary; a file is data of an element's parameter");
                    const std::string w = toks[p++].s;
                    if (std::find(args.begin(), args.end(), w) != args.end()) {
                        if (!t.arg.empty()) fail("a term reads at most one event argument");
                        t.arg = w;
                    } else {
                        if (!t.param.empty()) fail("a term reads at most one parameter (this is affine: a sum of parameters times numbers)");
                        if (!target.empty() && w.rfind(target + ".", 0) == 0) {
                            t.param = w.substr(target.size() + 1);
                            t.of_target = true;
                        } else {
                            t.param = w;
                        }
                    }
                } else {
                    fail("expected a number, a parameter or an event argument");
                }
                any = true;
                if (!accept("*")) break;
            }
            (void)any;
            if (t.param.empty() && t.arg.empty()) {
                row.bias += t.k;  // a number alone
            } else if (t.param.empty()) {
                // only an argument: "k * arg" has no parameter to read; the
                // declared step (sg/core/Declared.hpp) multiplies a parameter
                fail("a term with an event argument needs a parameter to multiply (x = x + vx * dt)",
                     "setting a parameter from an argument as it is has no declared step: bind it with `native`");
            } else {
                row.terms.push_back(std::move(t));
            }
        }
    }

    // --- state --------------------------------------------------------------------------
    void refuse_word() {
        if (cur().t != T::Ident) return;
        auto it = refused().find(cur().s);
        if (it == refused().end()) return;
        if (is_punct("->", 1) || is_punct("=", 1)) return;  // a name that happens to be that word
        fail(std::string(it->second.what) + " is not in the language: " + it->second.why, std::string("use: ") + it->second.instead);
    }

    void state_body(StateAst& s) {
        expect("{");
        while (!is_punct("}")) {
            if (end()) fail("a '{' is not closed");
            if (accept(";")) continue;
            refuse_word();
            const bool plain = !(is_punct("->", 1) || is_punct("=", 1) || is_punct("+=", 1) || is_punct("-=", 1));
            if (plain && is_word("element")) {
                ++p;
                ElementAst e;
                e.at = loc();
                e.id = name("an element name");
                if (accept(":")) e.kind = name("an element kind");
                if (accept("{")) {
                    while (!is_punct("}")) {
                        if (end()) fail("a '{' is not closed");
                        if (accept(";")) continue;
                        ParamAst pa;
                        pa.at = loc();
                        pa.key = name("a parameter name");
                        expect("=");
                        pa.value = value();
                        e.params.push_back(std::move(pa));
                    }
                    expect("}");
                }
                s.elements.push_back(std::move(e));
            } else if (plain && is_word("say")) {
                ++p;
                const Loc at = loc();
                s.says.push_back({name("an event name"), at});
            } else if (plain && is_word("compose")) {
                ++p;
                ComposeAst c;
                c.at = loc();
                c.name = name("a name for the composite");
                expect("=");
                c.chain.push_back(name("an arrow"));
                while (accept(";")) c.chain.push_back(name("an arrow"));
                if (accept_word("on")) c.trigger = name("an event");
                s.composes.push_back(std::move(c));
            } else if (cur().t == T::Ident || cur().t == T::Str) {
                if (is_punct("->", 1)) {
                    s.arrows.push_back(arrow());
                } else if (is_punct("=", 1)) {
                    ParamAst pa;
                    pa.at = loc();
                    pa.key = toks[p].s;
                    p += 2;
                    pa.value = value();
                    s.params.push_back(std::move(pa));
                } else if (is_punct("+=", 1) || is_punct("-=", 1)) {
                    fail("a state's parameter is set, not added to, from outside an arrow",
                         "declare an arrow with a step: `x -> x : name(dt) { x = x + vx * dt }`");
                } else {
                    fail("expected `element`, `say`, `compose`, `<from> -> <to> : <arrow>` or `<param> = <value>`");
                }
            } else {
                fail("expected a statement of a state");
            }
        }
        expect("}");
    }

    ArrowAst arrow() {
        ArrowAst a;
        a.at = loc();
        a.from = name("an element");
        expect("->");
        a.to = name("an element");
        expect(":");
        a.name = name("an arrow name");
        if (accept("(")) {
            while (!is_punct(")")) {
                a.args.push_back(name("an event argument"));
                if (!accept(",")) break;
            }
            expect(")");
        }
        for (;;) {
            if (accept_word("on")) {
                a.trigger = name("an event");
            } else if (accept_word("native")) {
                if (a.body.kind != BodyAst::Kind::None) fail("an arrow has one body");
                a.body.kind = BodyAst::Kind::Native;
                a.body.native = name("a native computation");
            } else if (is_punct("{")) {
                if (a.body.kind != BodyAst::Kind::None) fail("an arrow has one body");
                rows(a.body, a.args, a.to == a.from ? std::string{} : a.to);
            } else {
                break;
            }
        }
        return a;
    }

    void state_item(bool is_extern) {
        StateAst s;
        s.at = loc();
        s.is_extern = is_extern;
        s.name = name("a state name");
        s.kind = "state";
        if (accept(":")) s.kind = name("a state kind");
        if (is_extern) {
            if (is_punct("{")) fail("an extern state is built elsewhere: it has no body here");
            prog.states.push_back(std::move(s));
            return;
        }
        if (is_punct("{")) state_body(s);
        prog.states.push_back(std::move(s));
    }

    // --- functor --------------------------------------------------------------------------
    TransportAst transport() {
        TransportAst t;
        if (accept_word("only")) {
            t.kind = TransportAst::Kind::Only;
            expect("(");
            while (!is_punct(")")) {
                t.names.push_back(name("a parameter"));
                if (!accept(",")) break;
            }
            expect(")");
        } else if (accept_word("swizzle")) {
            t.kind = TransportAst::Kind::Swizzle;
            expect("(");
            while (!is_punct(")")) {
                std::string to = name("a parameter");
                expect("=");
                t.pairs.push_back({std::move(to), name("a parameter")});
                if (!accept(",")) break;
            }
            expect(")");
            if (accept_word("rest")) t.rest = true;
        } else if (accept_word("native")) {
            t.kind = TransportAst::Kind::Native;
            t.native = name("a native transport");
        } else if (accept_word("via")) {
            t.kind = TransportAst::Kind::Via;
            t.native = name("a transport");
        } else if (is_punct("{")) {
            t.kind = TransportAst::Kind::Rows;
            rows(t.rows, {}, {});
        }
        return t;
    }

    void functor_item(bool is_extern = false) {
        FunctorAst f;
        f.at = loc();
        f.is_extern = is_extern;
        f.name = name("a functor name");
        expect(":");
        f.from = name("the source state");
        expect("->");
        f.to = name("the target state");
        if (is_extern) {
            prog.functors.push_back(std::move(f));
            return;
        }
        expect("{");
        while (!is_punct("}")) {
            if (end()) fail("a '{' is not closed");
            if (accept(";")) continue;
            if (accept_word("object")) {
                ObjectAst o;
                o.at = loc();
                o.src = name("an element of the source");
                expect("->");
                o.dst = name("an element of the target");
                o.transport = transport();
                f.objects.push_back(std::move(o));
            } else if (accept_word("event")) {
                MapAst m;
                m.at = loc();
                m.src = name("an event");
                expect("->");
                m.dst = name("an event");
                f.events.push_back(std::move(m));
            } else if (accept_word("arrow")) {
                MapAst m;
                m.at = loc();
                m.src = name("an arrow of the source");
                expect("->");
                m.dst = name("an arrow of the target");
                f.arrows.push_back(std::move(m));
            } else {
                refuse_word();
                fail("expected `object`, `event` or `arrow` in a functor");
            }
        }
        expect("}");
        prog.functors.push_back(std::move(f));
    }

    // --- graph relations --------------------------------------------------------------------
    void compose_item() {
        ComposeFunctorsAst c;
        c.at = loc();
        c.name = name("a name for the composite");
        expect("=");
        c.chain.push_back(name("a functor"));
        while (accept(";")) c.chain.push_back(name("a functor"));
        if (c.chain.size() < 2) fail_at(c.at, "a composition needs two or more");
        prog.functor_composes.push_back(std::move(c));
    }

    void lens_item() {
        LensAst l;
        l.at = loc();
        l.get = name("the functor that shows");
        expect("<->");
        l.put = name("the functor that writes back");
        prog.lenses.push_back(std::move(l));
    }

    void transition_item() {
        TransitionAst t;
        t.at = loc();
        if (accept("*")) t.from = "*";
        else t.from = name("a state (or * for any)");
        expect("-[");
        t.trigger = name("an event");
        expect("]->");
        if (is_word("pop") && !(is_punct("->", 1))) {
            ++p;
            t.pop = true;
        } else {
            t.to = name("a state (or pop)");
        }
        for (;;) {
            if (accept_word("carry")) t.carry = name("a functor");
            else if (accept_word("name")) t.name = name("a name");
            else if (accept_word("push")) t.push = true;
            else if (accept_word("with")) {
                do {
                    ParamAst pa;
                    pa.at = loc();
                    pa.key = name("an argument the entered state is told");
                    expect("=");
                    pa.value = value();
                    t.with.push_back(std::move(pa));
                } while (cur().t == T::Ident && is_punct("=", 1));
            } else if (is_word("when") && guard_follows()) {
                if (t.guard.kind != GuardAst::Kind::None) fail("a transition has one guard: join its comparisons with `and` or `or`");
                ++p;
                t.guard = guard_or();
            } else break;
        }
        if (t.push && t.pop) fail_at(t.at, "a transition is a push or a pop, not both");
        prog.transitions.push_back(std::move(t));
    }

    // --- a transition's guard ------------------------------------------------------------------
    // Comparisons of what the state left holds and what the event says, joined
    // by `and`, `or`, `not` and brackets; `and` binds before `or`.
    static bool comparison(const Tok& t) {
        return t.t == T::Punct && (t.s == "==" || t.s == "!=" || t.s == "<" || t.s == "<=" || t.s == ">" || t.s == ">=");
    }

    // Whether the `when` here is the guard of the transition just read, and not
    // a `when` of its own (an event's mapping, which names two events and
    // compares nothing): on the same line, or plainly a comparison.
    bool guard_follows() const {
        if (p > 0 && cur().line == toks[p - 1].line) return true;
        return is_word("not", 1) || is_punct("(", 1) || is_punct("[", 2) || comparison(peek(2));
    }

    GuardAst guard_or() {
        GuardAst first = guard_and();
        if (!is_word("or")) return first;
        GuardAst g;
        g.kind = GuardAst::Kind::Or;
        g.at = first.at;
        g.parts.push_back(std::move(first));
        while (accept_word("or")) g.parts.push_back(guard_and());
        return g;
    }

    GuardAst guard_and() {
        GuardAst first = guard_unary();
        if (!is_word("and")) return first;
        GuardAst g;
        g.kind = GuardAst::Kind::And;
        g.at = first.at;
        g.parts.push_back(std::move(first));
        while (accept_word("and")) g.parts.push_back(guard_unary());
        return g;
    }

    GuardAst guard_unary() {
        GuardAst g;
        g.at = loc();
        if (accept_word("not")) {
            g.kind = GuardAst::Kind::Not;
            g.parts.push_back(guard_unary());
            return g;
        }
        if (accept("(")) {
            g = guard_or();
            expect(")");
            return g;
        }
        g.kind = GuardAst::Kind::Compare;
        g.lhs = guard_operand();
        if (!comparison(cur()))
            fail("expected a comparison: ==, !=, <, <=, > or >=",
                 "a guard compares what the state it leaves holds, or what the event says, with something");
        g.op = toks[p++].s;
        g.rhs = guard_operand();
        return g;
    }

    GuardAst::Operand guard_operand() {
        using K = GuardAst::Operand::Kind;
        GuardAst::Operand o;
        if (accept("-")) {
            if (cur().t != T::Num) fail("expected a number after '-'");
            o.number = -cur().num;
            ++p;
            return o;
        }
        if (cur().t == T::Num) {
            o.number = cur().num;
            ++p;
            return o;
        }
        if (cur().t == T::Str) {
            o.kind = K::Text;
            o.text = toks[p++].s;
            return o;
        }
        if (cur().t != T::Ident)
            fail("expected what a guard reads: from, from.<param>, from[<element>].<param>, arg.<name>, a number or words in quotes");
        const std::string w = cur().s;
        if (w == "true" || w == "false") {
            o.number = w == "true" ? 1.0 : 0.0;
            ++p;
            return o;
        }
        if (w == "from") {
            ++p;
            if (accept("[")) {
                o.kind = K::ElementParam;
                o.element = name("an element of the state left");
                expect("]");
                expect(".", "and a parameter of the element: from[<element>].<param>");
                o.key = name("a parameter of the element");
                return o;
            }
            o.kind = K::Id;
            return o;
        }
        if (w.rfind("from.", 0) == 0) {
            o.kind = K::Param;
            o.key = w.substr(5);
            ++p;
            return o;
        }
        if (w.rfind("arg.", 0) == 0) {
            o.kind = K::Arg;
            o.key = w.substr(4);
            ++p;
            return o;
        }
        fail("a guard reads only the state it leaves and the event that asks: `" + w + "` is neither",
             "write from, from.<param>, from[<element>].<param> or arg.<name>; put words in quotes");
    }

    void seam_item() {
        SeamAst s;
        s.at = loc();
        s.a = name("a portal, as state.element");
        expect("<->");
        s.b = name("a portal, as state.element");
        for (;;) {
            if (accept_word("also")) {
                std::string x = name("an element of the first side");
                expect("<->");
                s.also.push_back({std::move(x), name("an element of the second side")});
            } else if (accept_word("name")) {
                s.name = name("a name");
            } else if (accept_word("wraps")) {
                s.wraps = true;
            } else {
                break;
            }
        }
        prog.seams.push_back(std::move(s));
    }

    void embed_item() {
        EmbedAst e;
        e.at = loc();
        e.host_portal = name("a portal, as state.element");
        expect("->");
        e.guest = name("the guest state");
        for (;;) {
            if (accept_word("in")) e.in = name("a functor");
            else if (accept_word("out")) e.out = name("a functor");
            else if (accept_word("subject")) e.subject = name("a state");
            else if (accept_word("name")) e.name = name("a name");
            else if (accept_word("sync")) e.sync = name("live, commit or view");
            else if (accept_word("propagate")) e.propagate = name("onchange, continuous, onevent or manual");
            else if (accept_word("focus")) e.focus = boolean();
            else if (accept_word("follows")) e.follows = boolean();
            else if (accept_word("recurses")) e.recurses = true;
            else break;
        }
        prog.embeds.push_back(std::move(e));
    }

    int boolean() {
        if (accept_word("true")) return 1;
        if (accept_word("false")) return 0;
        fail("expected true or false");
    }

    void drive_item() {
        DriveAst d;
        d.at = loc();
        d.clock = name("the clock state");
        expect("->");
        d.target = name("the driven state");
        for (;;) {
            if (accept_word("event")) d.event = name("an event");
            else if (accept_word("keeps")) d.keeps = name("always, active, shown, focused or entered");
            else if (accept_word("additive")) d.additive = true;
            else break;
        }
        prog.drives.push_back(std::move(d));
    }

    void port_item() {
        PortAst pt;
        pt.at = loc();
        pt.target = name("state.event");
        if (accept_word("event")) pt.event = name("an event");
        prog.ports.push_back(std::move(pt));
    }

    void keep_item() {
        KeepAst k;
        k.at = loc();
        k.functor = name("a functor");
        prog.keeps.push_back(std::move(k));
    }

    void wear_item() {
        WearAst w;
        w.at = loc();
        w.host = name("the state that wears");
        expect("<-");
        w.look = name("a look");
        prog.wears.push_back(std::move(w));
    }

    void film_item() {
        FilmAst f;
        f.at = loc();
        f.camera = name("a camera");
        expect("->");
        f.world = name("the world it films");
        if (accept_word("rig")) f.rig = name("an element of the world");
        prog.films.push_back(std::move(f));
    }

    void when_item() {
        WhenAst w;
        w.at = loc();
        w.event = name("an event");
        if (cur().t == T::Ident && (is_punct("=", 1) || is_punct("+=", 1) || is_punct("-=", 1)))
            fail("direct write from " + w.event + " to " + cur().s,
                 "target an arrow of that state through a graph-declared interface: `when " + w.event + " <state>.<event>`");
        w.target = name("an event of an arrow");
        if (is_punct("(")) {
            fail("a functor relabels an event and passes its arguments as they are: it cannot add `(...)`",
                 "say the argument in the state that says the event, or use an arrow with a step");
        }
        prog.whens.push_back(std::move(w));
    }

    void bind_item() {
        BindAst b;
        b.at = loc();
        b.device = name("an input device");
        expect("{");
        while (!is_punct("}")) {
            if (end()) fail("a '{' is not closed");
            if (accept(";")) continue;
            BindEntryAst e;
            e.at = loc();
            e.key = name("a key or button");
            expect("->");
            {
                const std::string target = cur().s.substr(0, cur().s.rfind('.') == std::string::npos ? cur().s.size() : cur().s.rfind('.'));
                if (cur().t == T::Ident && (is_punct("=", 1) || is_punct("+=", 1) || is_punct("-=", 1)))
                    fail("input never changes a state: `" + e.key + " -> " + cur().s + " " + peek().s + " ...` writes into it",
                         "fire an event of a real arrow: `" + e.key + " -> " + target + ".step(forward: 1)`");
            }
            e.event = name("an event");
            if (accept("(")) {
                while (!is_punct(")")) {
                    std::string k = name("an argument name");
                    expect(":");
                    e.args.push_back({std::move(k), value()});
                    if (!accept(",")) break;
                }
                expect(")");
            }
            b.entries.push_back(std::move(e));
        }
        expect("}");
        prog.binds.push_back(std::move(b));
    }

    void transport_item() {
        TransportAliasAst t;
        t.at = loc();
        t.name = name("a name for the transport");
        expect_word("native");
        t.native = name("a native transport");
        prog.transports.push_back(std::move(t));
    }

    void initial_item() {
        InitialAst i;
        i.at = loc();
        i.state = name("a state");
        prog.initials.push_back(std::move(i));
    }

    void edit_item() {
        EditAst e;
        e.at = loc();
        e.state = name("the state that asks");
        expect_word("on");
        e.event = name("the event it says to ask");
        expect_word("native");
        e.native = name("a native edit");
        if (accept_word("reply")) e.reply = name("an event");
        prog.edits.push_back(std::move(e));
    }

    // --- program ----------------------------------------------------------------------------
    static const std::set<std::string>& heads() {
        static const std::set<std::string> h = {"state", "extern", "functor", "compose", "lens", "transition", "seam", "embed",
                                                "drive", "port", "keep", "wear", "film", "when", "bind", "transport", "edit", "initial"};
        return h;
    }

    void skip_item() {
        int depth = 0;
        while (!end()) {
            if (is_punct("{")) ++depth;
            else if (is_punct("}")) {
                if (depth > 0) --depth;
            } else if (depth == 0 && cur().t == T::Ident && heads().count(cur().s) && p > 0 &&
                       cur().line != toks[p - 1].line)
                return;
            ++p;
        }
    }

    void item() {
        if (cur().t != T::Ident) fail("expected a declaration (state, functor, transition, ...)");
        const std::string head = cur().s;
        // a write into a state from outside: never a statement
        if (is_punct("=", 1) || is_punct("+=", 1) || is_punct("-=", 1)) {
            fail("direct write to " + head + ": no statement writes into a state from outside it",
                 "declare an arrow on that state and route the cause to its event through a functor, transition, port or edit");
        }
        auto ref = refused().find(head);
        if (ref != refused().end())
            fail(std::string(ref->second.what) + " is not in the language: " + ref->second.why, std::string("use: ") + ref->second.instead);
        ++p;
        if (head == "state") state_item(false);
        else if (head == "extern") {
            if (accept_word("functor")) functor_item(true);
            else {
                expect_word("state");
                state_item(true);
            }
        } else if (head == "functor") functor_item();
        else if (head == "compose") compose_item();
        else if (head == "lens") lens_item();
        else if (head == "transition") transition_item();
        else if (head == "seam") seam_item();
        else if (head == "embed") embed_item();
        else if (head == "drive") drive_item();
        else if (head == "port") port_item();
        else if (head == "keep") keep_item();
        else if (head == "wear") wear_item();
        else if (head == "film") film_item();
        else if (head == "when") when_item();
        else if (head == "bind") bind_item();
        else if (head == "transport") transport_item();
        else if (head == "edit") edit_item();
        else if (head == "initial") initial_item();
        else {
            --p;
            fail("`" + head + "` begins nothing in the language",
                 "declarations: state, extern state, functor, compose, lens, transition, seam, embed, drive, port, keep, wear, film, when, bind, transport, edit, initial");
        }
    }

    void run() {
        while (!end()) {
            try {
                item();
            } catch (const Fail&) {
                skip_item();
            }
        }
    }
};

}  // namespace

ParsedGuard parse_guard(const std::string& text, const std::string& file) {
    ParsedGuard out;
    Program none;
    Lexer lex{text, file, {}, out.errors};
    lex.run();
    if (!out.errors.empty()) return out;
    Parser ps{std::move(lex.out), out.errors, none, file};
    try {
        out.guard = ps.guard_or();
        if (!ps.end()) ps.fail("a guard ends before this");
    } catch (const Fail&) {
        // noted in errors
    }
    return out;
}

Parsed parse(const std::string& text, const std::string& file) {
    Parsed out;
    out.program.file = file;
    Lexer lex{text, file, {}, out.errors};
    lex.run();
    if (!out.errors.empty()) return out;
    Parser ps{std::move(lex.out), out.errors, out.program, file};
    ps.run();
    return out;
}

}  // namespace sg::dsl
