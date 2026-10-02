// stategine_shape: the build keeps the engine's rules.
//
// A module is a state of its own, and the compiler is the interface to the
// machine. So each module may include only itself, the engine, and the
// modules it says it uses - never what uses it, never a neighbour it did not
// name. And its headers say what a thing is, not what it does: a body over
// more than one line and 20 tokens belongs in the header's own .cpp, compiled
// once, not in every file that includes it. A body may stay if it is a
// template, constexpr, or deduced (callers need to see it), or if a comment
// just above it (or its class) says why - which holds for the declarations
// right under it too, until a blank line:
//
//   // inline: every frame reaches for it
//
//   stategine_shape <name>=<dir>[:<uses>,...] ...
//
// <name> is how the module is included ("os", "sg/core"); <dir> where its
// files are. Prints what breaks the rules; exits 1 if anything does.
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Module {
    std::string name;
    fs::path dir;
    std::set<std::string> uses;
};

struct Token {
    std::string s;
    int line;
};

// The file as tokens, with comments, strings and preprocessor lines taken
// out; the lines a comment says `inline:` on are kept aside.
struct Scanned {
    std::vector<Token> tokens;
    std::set<int> says_inline;  // lines of a comment that says "inline:"
    std::set<int> blank;         // lines with nothing on them
    std::set<int> comment_only;  // lines with a comment and nothing else
    std::vector<std::pair<int, std::string>> includes;
};

Scanned scan(const std::string& text) {
    Scanned out;
    std::size_t i = 0;
    int line = 1;
    bool line_has_code = false, line_has_comment = false;
    const auto at = [&](std::size_t k) { return k < text.size() ? text[k] : '\0'; };
    const auto newline = [&] {
        if (!line_has_code && !line_has_comment) out.blank.insert(line);
        if (!line_has_code && line_has_comment) out.comment_only.insert(line);
        ++line;
        line_has_code = line_has_comment = false;
    };
    bool line_start = true;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '\n') {
            newline();
            line_start = true;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (line_start && c == '#') {  // a preprocessor line, continued with '\'
            const std::size_t from = i;
            while (i < text.size() && !(text[i] == '\n' && text[i - 1] != '\\')) {
                if (text[i] == '\n') ++line;
                ++i;
            }
            const std::string directive = text.substr(from, i - from);
            const std::size_t q = directive.find("include \"");
            if (q != std::string::npos) {
                const std::size_t end = directive.find('"', q + 9);
                if (end != std::string::npos) out.includes.push_back({line, directive.substr(q + 9, end - q - 9)});
            }
            line_has_code = true;
            continue;
        }
        line_start = false;
        if (c == '/' && at(i + 1) == '/') {
            const std::size_t end = text.find('\n', i);
            const std::string comment = text.substr(i, end == std::string::npos ? std::string::npos : end - i);
            if (comment.find("inline:") != std::string::npos) out.says_inline.insert(line);
            line_has_comment = true;
            i = end == std::string::npos ? text.size() : end;
            continue;
        }
        if (c == '/' && at(i + 1) == '*') {
            const std::size_t end = text.find("*/", i + 2);
            const std::size_t stop = end == std::string::npos ? text.size() : end + 2;
            line_has_comment = true;
            for (std::size_t k = i; k < stop; ++k)
                if (text[k] == '\n') newline(), line_has_comment = true;
            i = stop;
            continue;
        }
        line_has_code = true;
        if (c == 'R' && at(i + 1) == '"') {  // R"delim( ... )delim"
            const std::size_t open = text.find('(', i + 2);
            const std::string close = ")" + text.substr(i + 2, open - i - 2) + "\"";
            const std::size_t end = text.find(close, open);
            const std::size_t stop = end == std::string::npos ? text.size() : end + close.size();
            for (std::size_t k = i; k < stop; ++k)
                if (text[k] == '\n') ++line;
            out.tokens.push_back({"\"\"", line});
            i = stop;
            continue;
        }
        if (c == '"' || c == '\'') {
            std::size_t k = i + 1;
            while (k < text.size() && text[k] != c) k += text[k] == '\\' ? 2 : 1;
            out.tokens.push_back({"\"\"", line});
            i = k + 1;
            continue;
        }
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t k = i;
            while (k < text.size() && (std::isalnum(static_cast<unsigned char>(text[k])) || text[k] == '_' || text[k] == '.'))
                ++k;
            out.tokens.push_back({text.substr(i, k - i), line});
            i = k;
            continue;
        }
        static const char* const kLong[] = {"<<=", ">>=", "::", "->", "==", "!=", "<=", ">=", "+=", "-=", "*=", "/=",
                                            "%=", "&=", "|=", "^=", "&&", "||", "[[", "]]"};
        std::string op(1, c);
        for (const char* l : kLong)
            if (text.compare(i, std::char_traits<char>::length(l), l) == 0) {
                op = l;
                break;
            }
        out.tokens.push_back({op, line});
        i += op.size();
    }
    return out;
}

bool word(const std::string& s) { return !s.empty() && (std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_'); }

enum class Scope { File, Namespace, Class, Function, Other };

// A body this short, or on one line, says what a thing is where it is declared.
constexpr std::size_t kShort = 20;

struct Body {
    std::string name;
    int line;
};

// Function bodies at namespace or class scope that span lines and have no
// reason to be in a header.
std::vector<Body> bodies_in(const Scanned& sc) {
    std::vector<Body> found;
    struct Level {
        Scope scope;
        bool templated;
        std::size_t head_from;  // where the current head starts, in tokens
        int open_line;
        std::string name;
        bool report;
        bool says_why = false;  // a comment above it says why its bodies stay
        int why_through = -1;   // the last line a comment's why still covers here
    };
    std::vector<Level> stack{{Scope::File, false, 0, 0, "", false}};
    const auto& t = sc.tokens;
    // a comment just above a declaration that says why its body stays
    const auto says_why = [&](std::size_t from) {
        if (sc.says_inline.count(t[from].line)) return true;
        for (int l = t[from].line - 1; l > 0 && sc.comment_only.count(l); --l)
            if (sc.says_inline.count(l)) return true;
        return false;
    };
    // ... and for the declarations right under it, until a blank line
    const auto unbroken = [&](int from_line, int to_line) {
        for (int l = from_line + 1; l < to_line; ++l)
            if (sc.blank.count(l)) return false;
        return true;
    };
    for (std::size_t i = 0; i < t.size(); ++i) {
        Level& top = stack.back();
        const std::string& s = t[i].s;
        const bool declaring = top.scope == Scope::File || top.scope == Scope::Namespace || top.scope == Scope::Class;
        if (!declaring) {
            if (s == "{") stack.push_back({Scope::Other, false, i + 1, t[i].line, "", false});
            if (s == "}") stack.pop_back();
            continue;
        }
        if (s == ";") {
            top.head_from = i + 1;
            continue;
        }
        if (s == ":" && i > 0 && (t[i - 1].s == "public" || t[i - 1].s == "private" || t[i - 1].s == "protected")) {
            top.head_from = i + 1;
            continue;
        }
        if (s == "}") {
            stack.pop_back();
            if (stack.empty()) return found;  // unbalanced: give up quietly
            stack.back().head_from = i + 1;
            continue;
        }
        if (s != "{") continue;
        // What opens here? Read the head: the tokens since the last boundary.
        std::vector<std::string> head;
        for (std::size_t k = top.head_from; k < i; ++k) head.push_back(t[k].s);
        std::size_t h = 0;
        bool templated = top.scope == Scope::Class && top.templated;
        while (h < head.size() && (head[h] == "template" || head[h] == "[[")) {
            if (head[h] == "[[") {
                while (h < head.size() && head[h] != "]]") ++h;
                ++h;
                continue;
            }
            templated = true;
            ++h;
            if (h < head.size() && head[h] == "<") {
                int depth = 0;
                for (; h < head.size(); ++h) {
                    if (head[h] == "<") ++depth;
                    if (head[h] == ">") --depth;
                    if (head[h] == ">>") depth -= 2;
                    if (depth <= 0) break;
                }
                ++h;
            }
        }
        const auto has = [&](const char* w) {
            for (std::size_t k = h; k < head.size(); ++k)
                if (head[k] == w) return true;
            return false;
        };
        Scope opens = Scope::Other;
        std::string name;
        bool report = false, why = false;
        if (has("namespace") || (h < head.size() && head[h] == "extern")) {
            opens = Scope::Namespace;
        } else if (h < head.size() && (head[h] == "class" || head[h] == "struct" || head[h] == "union") && !has("(") &&
                   !has("=")) {
            opens = Scope::Class;
            name = h + 1 < head.size() ? head[h + 1] : "";
        } else if (!has("enum")) {
            // A function: a name, its parameters, qualifiers, then the body.
            std::size_t p = h;
            bool eq = false;
            for (; p < head.size(); ++p) {
                if (head[p] == "=" && !(p > 0 && head[p - 1] == "operator")) eq = true;
                if (head[p] == "(") break;
            }
            if (!eq && p < head.size() && p > h) {
                if (head[p - 1] == "operator" && p + 1 < head.size() && head[p + 1] == ")") p += 2;  // operator()
                std::size_t q = p;
                int depth = 0;
                for (; q < head.size(); ++q) {
                    if (head[q] == "(") ++depth;
                    if (head[q] == ")" && --depth == 0) break;
                }
                bool is_function = q < head.size();
                bool arrow = false, init = false;
                for (std::size_t k = q + 1; is_function && k < head.size(); ++k) {
                    const std::string& a = head[k];
                    if (a == "->") arrow = true;
                    if (a == ":") init = true;
                    if (arrow || init) break;
                    if (a == "(" ) {  // noexcept(...), throw(...)
                        int d = 0;
                        for (; k < head.size(); ++k) {
                            if (head[k] == "(") ++d;
                            if (head[k] == ")" && --d == 0) break;
                        }
                        continue;
                    }
                    if (!(a == "const" || a == "volatile" || a == "noexcept" || a == "override" || a == "final" || a == "&" ||
                          a == "&&" || a == "throw" || a == "[[" || a == "]]" || a == "requires" || word(a)))
                        is_function = false;
                    if (a == "requires") templated = true;
                }
                if (is_function && init && !(head.back() == ")" || head.back() == "}")) is_function = false;
                if (is_function) {
                    opens = Scope::Function;
                    name = head[p - 1];
                    if (name == ")" && p >= 3) name = "operator()";
                    bool exempt = templated;
                    bool deduced = false;
                    for (std::size_t k = h; k < p; ++k) {
                        if (head[k] == "constexpr" || head[k] == "consteval" || head[k] == "friend") exempt = true;
                        if (head[k] == "auto") deduced = true;
                    }
                    if (deduced && !arrow) exempt = true;
                    for (const Level& l : stack)
                        if (l.says_why) exempt = true;
                    const std::size_t from = top.head_from < i ? top.head_from : i;
                    why = says_why(from) || (top.why_through >= 0 && unbroken(top.why_through, t[from].line));
                    report = !exempt && !why;
                }
            }
        }
        stack.push_back({opens, opens == Scope::Class && templated, i + 1, t[i].line, name, report,
                         opens == Scope::Class && says_why(top.head_from < i ? top.head_from : i)});
        if (opens == Scope::Function) {
            // skip to its end: nothing inside a body is declared at our level
            int depth = 1;
            std::size_t k = i + 1;
            for (; k < t.size() && depth > 0; ++k) {
                if (t[k].s == "{") ++depth;
                if (t[k].s == "}") --depth;
            }
            if (report && k - i - 2 > kShort && t[k - 1].line != t[i].line) found.push_back({name, t[i].line});
            stack.pop_back();
            i = k - 1;
            stack.back().head_from = k;
            stack.back().why_through = why ? t[k - 1].line : -1;
        }
    }
    return found;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<Module> modules;
    for (int a = 1; a < argc; ++a) {
        std::string spec = argv[a];
        const std::size_t eq = spec.find('=');
        if (eq == std::string::npos) continue;
        Module m;
        m.name = spec.substr(0, eq);
        std::string rest = spec.substr(eq + 1);
        const std::size_t colon = rest.find(':', 2);  // past a drive letter
        m.dir = rest.substr(0, colon);
        if (colon != std::string::npos) {
            std::stringstream uses(rest.substr(colon + 1));
            for (std::string u; std::getline(uses, u, ',');)
                if (!u.empty()) m.uses.insert(u);
        }
        modules.push_back(m);
    }
    // the module an include names: the longest registered name it starts with
    const auto owner = [&](const std::string& inc) -> const Module* {
        const Module* best = nullptr;
        for (const Module& m : modules)
            if (inc.compare(0, m.name.size() + 1, m.name + "/") == 0 && (!best || m.name.size() > best->name.size())) best = &m;
        return best;
    };
    int problems = 0;
    for (const Module& m : modules) {
        if (!fs::is_directory(m.dir)) {
            std::printf("%s: no folder %s\n", m.name.c_str(), m.dir.string().c_str());
            ++problems;
            continue;
        }
        for (const auto& entry : fs::recursive_directory_iterator(m.dir)) {
            const std::string ext = entry.path().extension().string();
            const bool header = ext == ".hpp" || ext == ".h" || ext == ".hh";
            if (!header && ext != ".cpp" && ext != ".cc" && ext != ".c") continue;
            std::ifstream in(entry.path(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            const Scanned sc = scan(text.str());
            const std::string file = entry.path().generic_string();
            for (const auto& [line, inc] : sc.includes) {
                const Module* o = owner(inc);
                if (o && o->name != m.name && !m.uses.count(o->name)) {
                    std::printf("%s:%d: %s reaches into %s, which it does not use\n", file.c_str(), line, m.name.c_str(),
                                o->name.c_str());
                    ++problems;
                }
            }
            if (!header) continue;
            for (const Body& b : bodies_in(sc)) {
                std::printf("%s:%d: %s has a body in its header - put it in the .cpp, or say why it stays (// inline: ...)\n",
                            file.c_str(), b.line, b.name.c_str());
                ++problems;
            }
        }
    }
    if (problems) {
        std::printf("%d thing%s not in shape\n", problems, problems == 1 ? "" : "s");
        return 1;
    }
    std::printf("every module is its own, and its headers say what it is\n");
    return 0;
}
