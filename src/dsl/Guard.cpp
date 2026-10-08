// Stategine DSL - a transition's guard: what it is in Guard.hpp.
#include "sg/dsl/Guard.hpp"

#include <cctype>
#include <charconv>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "sg/dsl/Parse.hpp"

namespace sg::dsl {

namespace {

using Operand = GuardAst::Operand;

std::string num(double d) {
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof buf, d);
    return std::string(buf, r.ptr);
}

// Words that read back as one name: a letter or _ first, then letters,
// digits and _, with single dots between.
bool plain(const std::string& s) {
    if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalnum(c) || c == '_') continue;
        if (c == '.' && i + 1 < s.size() && (std::isalnum(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == '_')) continue;
        return false;
    }
    return true;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\', out += c;
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out + "\"";
}

std::string name(const std::string& s) { return plain(s) ? s : quoted(s); }

std::string operand_text(const Operand& o) {
    switch (o.kind) {
        case Operand::Kind::Number: return num(o.number);
        case Operand::Kind::Text: return quoted(o.text);
        case Operand::Kind::Id: return "from";
        case Operand::Kind::Param: return "from." + o.key;
        case Operand::Kind::ElementParam: return "from[" + name(o.element) + "]." + name(o.key);
        case Operand::Kind::Arg: return "arg." + o.key;
    }
    return "?";
}

// --- what it reads, and how it compares ---------------------------------------------------
// A value as a guard sees it: a number, a text, or nothing. A text is seen
// where it lies (the state's params, the event's, the guard's own words), not
// copied: a guard asked every time its trigger is heard makes nothing.
struct Seen {
    enum class Kind { Nothing, Number, Text };
    Kind kind = Kind::Nothing;
    double number = 0.0;
    std::string_view text;
};

Seen seen(const Value& v) {
    if (const bool* b = std::get_if<bool>(&v)) return {Seen::Kind::Number, *b ? 1.0 : 0.0, {}};
    if (const int64_t* i = std::get_if<int64_t>(&v)) return {Seen::Kind::Number, static_cast<double>(*i), {}};
    if (const double* d = std::get_if<double>(&v)) return {Seen::Kind::Number, *d, {}};
    if (const std::string* s = std::get_if<std::string>(&v)) return {Seen::Kind::Text, 0.0, *s};
    return {};
}

Seen seen_in(const Params& p, Key key) { return p.has(key) ? seen(p.get(key)) : Seen{}; }

// One side of a comparison, its names made once, when the guard is made.
struct Read {
    Operand::Kind kind = Operand::Kind::Number;
    double number = 0.0;
    std::string text;
    Key element, key;

    explicit Read(const Operand& o) : kind(o.kind), number(o.number), text(o.text), element(o.element), key(o.key) {}

    Seen operator()(const State& from, const Event& e) const {
        switch (kind) {
            case Operand::Kind::Number: return {Seen::Kind::Number, number, {}};
            case Operand::Kind::Text: return {Seen::Kind::Text, 0.0, text};
            case Operand::Kind::Id: return {Seen::Kind::Text, 0.0, from.id().str()};
            case Operand::Kind::Param: return seen_in(from.params(), key);
            case Operand::Kind::ElementParam: {
                const Element* el = from.find(element);
                return el ? seen_in(el->params, key) : Seen{};
            }
            case Operand::Kind::Arg: return seen_in(e.args, key);
        }
        return {};
    }
};

enum class Op { Eq, Ne, Lt, Le, Gt, Ge };

Op op_of(const std::string& s) {
    if (s == "==") return Op::Eq;
    if (s == "!=") return Op::Ne;
    if (s == "<") return Op::Lt;
    if (s == "<=") return Op::Le;
    if (s == ">") return Op::Gt;
    if (s == ">=") return Op::Ge;
    throw std::invalid_argument("not a comparison: " + s);
}

bool holds(const Seen& a, Op op, const Seen& b) {
    int order = 0;
    if (a.kind == Seen::Kind::Number && b.kind == Seen::Kind::Number) {
        order = a.number < b.number ? -1 : a.number > b.number ? 1 : 0;
        if (a.number != a.number || b.number != b.number) return op == Op::Ne;  // NaN is equal to nothing
    } else if (a.kind == Seen::Kind::Text && b.kind == Seen::Kind::Text) {
        const int c = a.text.compare(b.text);
        order = c < 0 ? -1 : c > 0 ? 1 : 0;
    } else {
        return op == Op::Ne;  // nothing, or a number against a text: unequal, and neither less nor more
    }
    switch (op) {
        case Op::Eq: return order == 0;
        case Op::Ne: return order != 0;
        case Op::Lt: return order < 0;
        case Op::Le: return order <= 0;
        case Op::Gt: return order > 0;
        case Op::Ge: return order >= 0;
    }
    return false;
}

struct Node {
    GuardAst::Kind kind = GuardAst::Kind::None;
    std::vector<Read> sides;  // Compare: the two
    Op op = Op::Eq;
    std::vector<Node> parts;

    explicit Node(const GuardAst& g) : kind(g.kind) {
        if (kind == GuardAst::Kind::Compare) {
            sides.emplace_back(g.lhs);
            sides.emplace_back(g.rhs);
            op = op_of(g.op);
        }
        for (const GuardAst& p : g.parts) parts.emplace_back(p);
    }

    bool operator()(const State& from, const Event& e) const {
        switch (kind) {
            case GuardAst::Kind::None: return true;
            case GuardAst::Kind::Compare: return holds(sides[0](from, e), op, sides[1](from, e));
            case GuardAst::Kind::Not: return !parts.front()(from, e);
            case GuardAst::Kind::And:
                for (const Node& p : parts)
                    if (!p(from, e)) return false;
                return true;
            case GuardAst::Kind::Or:
                for (const Node& p : parts)
                    if (p(from, e)) return true;
                return false;
        }
        return false;
    }
};

}  // namespace

std::string guard_text(const GuardAst& g) {
    switch (g.kind) {
        case GuardAst::Kind::None: return {};
        case GuardAst::Kind::Compare: return operand_text(g.lhs) + " " + g.op + " " + operand_text(g.rhs);
        case GuardAst::Kind::Not: {
            const GuardAst& p = g.parts.front();
            const bool joined = p.kind == GuardAst::Kind::And || p.kind == GuardAst::Kind::Or;
            return "not " + (joined ? "(" + guard_text(p) + ")" : guard_text(p));
        }
        case GuardAst::Kind::And:
        case GuardAst::Kind::Or: {
            const bool is_and = g.kind == GuardAst::Kind::And;
            std::string out;
            for (const GuardAst& p : g.parts) {
                const bool bracket = is_and && p.kind == GuardAst::Kind::Or;  // `and` binds first
                if (!out.empty()) out += is_and ? " and " : " or ";
                out += bracket ? "(" + guard_text(p) + ")" : guard_text(p);
            }
            return out;
        }
    }
    return {};
}

Transition::Guard make_guard(const GuardAst& g) {
    if (g.kind == GuardAst::Kind::None) return {};
    auto node = std::make_shared<const Node>(g);
    return Transition::Guard{[node](const State& from, const Event& e) { return (*node)(from, e); }, guard_text(g)};
}

Transition::Guard guard(const std::string& text) {
    const ParsedGuard p = parse_guard(text);
    if (!p.ok()) {
        std::string why;
        for (const Diagnostic& d : p.errors) why += (why.empty() ? "" : "; ") + d.message;
        throw std::invalid_argument("not a guard: " + text + " (" + why + ")");
    }
    return make_guard(p.guard);
}

}  // namespace sg::dsl
