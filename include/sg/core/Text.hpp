// Stategine - Text: a state as text, and back.
//
// A state is its data - its own parameters, and its elements, each an id, a
// kind, alive or not, and its parameters - and its arrows, which are code: its
// class's. The data written out is the state's own source: a file it can live
// in apart from anything that shows it, be read back from, diffed, edited by
// hand while the game runs.
//
//   state <id> <kind>
//   param <key> <value>
//   element <id> <kind> [dead]
//     <key> <value>
//
// A value is `b:true`/`b:false`, `i:<integer>`, `d:<number>`, `s:<text>` (with
// \\, \n, \t, \s and \r escaped) or `-` for none. Ids and keys are escaped the
// same way, so any of them survives the round trip.
//
//   std::string src = sg::to_text(room);
//   sg::from_text(copy, src);           // copy now holds what room held
#pragma once

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <variant>

#include "sg/core/State.hpp"

namespace sg {

namespace text_detail {

inline std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            case ' ': out += "\\s"; break;
            default: out += c;
        }
    }
    return out;
}

inline std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        switch (s[++i]) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 's': out += ' '; break;
            default: out += s[i];
        }
    }
    return out;
}

inline std::string value(const Value& v) {
    if (const bool* b = std::get_if<bool>(&v)) return *b ? "b:true" : "b:false";
    if (const int64_t* i = std::get_if<int64_t>(&v)) return "i:" + std::to_string(*i);
    if (const double* d = std::get_if<double>(&v)) {
        // The shortest text that reads back to the very same number.
        char buf[40] = {'d', ':'};
        const auto r = std::to_chars(buf + 2, buf + sizeof buf, *d);
        return std::string(buf, r.ptr);
    }
    if (const std::string* s = std::get_if<std::string>(&v)) return "s:" + escape(*s);
    return "-";
}

inline bool parse(const std::string& t, Value& out) {
    if (t == "-") return out = std::monostate{}, true;
    if (t.size() < 2 || t[1] != ':') return false;
    const std::string body = t.substr(2);
    switch (t[0]) {
        case 'b': out = body == "true"; return body == "true" || body == "false";
        case 'i': {
            int64_t i = 0;
            const auto r = std::from_chars(body.data(), body.data() + body.size(), i);
            out = i;
            return r.ec == std::errc{};
        }
        case 'd': {
            char* end = nullptr;
            out = std::strtod(body.c_str(), &end);
            return end && *end == '\0';
        }
        case 's': out = unescape(body); return true;
        default: return false;
    }
}

}  // namespace text_detail

// The state's data as text (see the top of this file). `keep_element` and
// `keep_param`, if given, leave out what is not part of what the state is -
// who is looking at it, what time it is there.
inline std::string to_text(const State& s, const std::function<bool(const Element&)>& keep_element = {},
                           const std::function<bool(Key)>& keep_param = {}) {
    using namespace text_detail;
    std::string out = "state " + escape(s.id().str()) + " " + escape(s.kind().str()) + "\n";
    for (const auto& [k, v] : s.params())
        if (!keep_param || keep_param(k)) out += "param " + escape(k.str()) + " " + value(v) + "\n";
    for (const Element& e : s.elements()) {
        if (keep_element && !keep_element(e)) continue;
        out += "element " + escape(e.id.str()) + " " + escape(e.kind.str()) + (e.alive ? "" : " dead") + "\n";
        for (const auto& [k, v] : e.params) out += "  " + escape(k.str()) + " " + value(v) + "\n";
    }
    return out;
}

// Read `text` back into `s`: its params set, each element made if it is not
// there yet and its params set (an element's kind is kept if it is). With
// `exact`, elements and params the text does not have are taken away too, so
// `s` holds exactly what the text says. False, with `why`, on a line it
// cannot read - and nothing is changed.
inline bool from_text(State& s, const std::string& text, std::string* why = nullptr, bool exact = false) {
    using namespace text_detail;
    struct Line {
        int what;  // 0 param, 1 element, 2 element param
        std::string a, b;
        Value v;
        bool dead = false;
    };
    std::vector<Line> lines;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    bool in_element = false;
    const auto fail = [&](const std::string& msg) {
        if (why) *why = "line " + std::to_string(n) + ": " + msg;
        return false;
    };
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const bool indented = line[0] == ' ';
        std::istringstream words(line);
        std::string w0, w1, w2, w3;
        words >> w0 >> w1 >> w2 >> w3;
        if (indented) {
            if (!in_element) return fail("a parameter indented under no element");
            Line l{2, unescape(w0), {}, {}};
            if (!parse(w1, l.v)) return fail("cannot read the value " + w1);
            lines.push_back(std::move(l));
        } else if (w0 == "state") {
            continue;
        } else if (w0 == "param") {
            Line l{0, unescape(w1), {}, {}};
            if (!parse(w2, l.v)) return fail("cannot read the value " + w2);
            lines.push_back(std::move(l));
        } else if (w0 == "element") {
            if (w1.empty() || w2.empty()) return fail("an element needs an id and a kind");
            Line l{1, unescape(w1), unescape(w2), {}};
            l.dead = w3 == "dead";
            lines.push_back(std::move(l));
            in_element = true;
        } else {
            return fail("not a line of a state: " + w0);
        }
    }
    // Read whole: now change the state.
    if (exact) s.params().clear();
    std::vector<Key> seen;
    Element* at = nullptr;
    for (Line& l : lines) {
        if (l.what == 0) {
            s.params().set(Key{l.a}, std::move(l.v));
        } else if (l.what == 1) {
            const Key id{l.a};
            at = s.find(id);
            if (!at) at = &s.add_element(id, Key{l.b});
            else if (exact) at->params.clear();
            at->alive = !l.dead;
            seen.push_back(id);
        } else if (at) {
            at->params.set(Key{l.a}, std::move(l.v));
        }
    }
    if (exact) {
        std::vector<Key> gone;
        for (const Element& e : s.elements())
            if (std::find(seen.begin(), seen.end(), e.id) == seen.end()) gone.push_back(e.id);
        for (Key k : gone) s.remove_with_arrows(k);
    }
    return true;
}

}  // namespace sg
