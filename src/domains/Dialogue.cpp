#include "sg/domains/Dialogue.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <tuple>

namespace sg {

namespace {

const Key kExchange{"exchange"}, kLink{"link"}, kVoice{"voice"};

std::string str(const Params& p, const std::string& k) { return p.get_or<std::string>(Key{k}, ""); }

std::set<std::string> words(const std::string& s) {
    std::set<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.insert(w);
    return out;
}

std::vector<std::string> list(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::string joined(const std::vector<std::string>& ws) {
    std::string out;
    for (const std::string& w : ws) out += (out.empty() ? "" : " ") + w;
    return out;
}

}  // namespace

Dialogue::Dialogue(Key id, int w_px, int h_px) : Surface2D(std::move(id), std::max(1, w_px), std::max(1, h_px), 1) {
    Params& p = params();
    p.set("start", std::string()).set("at", std::string()).set("last", std::string()).set("phase", std::string()).set("line", 0.0);
    p.set("phase_at", 0.0).set("phase_since", 0.0).set("rebase", 0.0).set("choice", 0.0).set("chose", -1.0).set("moved", -1.0);
    p.set("history", std::string()).set("ways_were", std::string());
    // Its timings (seconds of its own time), and how much it keeps and shows.
    p.set("letter", 0.022).set("pause", 0.7).set("hold", 0.25).set("hold_last", 0.3).set("move", 0.45).set("show", 0.65).set("end", 1.4);
    p.set("options", 4.0).set("keep", 6.0);
    add_element(part("bar"), Key{"screen"});
    speaker("you", "you");
    says(out_event());
    loop(Key{"key"}, part("bar"), key_event(), [](State& s, Element&, Element*, const Event& ev) {
        static_cast<Dialogue&>(s).on_key(ev.args.get_or<std::string>("key", ""));
    });
    loop(Key{"tick"}, part("bar"), tick_event(), [](State& s, Element&, Element*, const Event& ev) {
        static_cast<Dialogue&>(s).on_tick(ev.args.num(keys::dt), ev.args.num(keys::time));
    });
}

std::vector<std::string> Dialogue::faults() const {
    std::vector<std::string> out;
    const std::vector<std::string> who = speakers();
    for (const Element& e : elements()) {
        if (e.kind != kExchange) continue;
        const int n = static_cast<int>(e.params.num("lines"));
        for (int i = 0; i < n; ++i)
            if (const std::string w = str(e.params, "who." + std::to_string(i)); std::find(who.begin(), who.end(), w) == who.end())
                out.push_back(id().str() + ": " + str(e.params, "name") + " has a line by " + w + ", who does not speak in it");
        for (const std::string& a : words(str(e.params, "after")))
            if (!exchange_of(a)) out.push_back(id().str() + ": " + str(e.params, "name") + " waits on " + a + ", which is not said");
    }
    if (const std::string s = str(params(), "start"); !s.empty() && !exchange_of(s)) out.push_back(id().str() + ": it starts at " + s + ", which is not said");
    return out;
}

// --- what can be said ------------------------------------------------------------------------

void Dialogue::speaker(const std::string& who, const std::string& name) {
    Element* v = find(voice_id(who));
    if (!v) v = &add_element(voice_id(who), kVoice), v->params.set("who", who).set("mouth", 0.0);
    v->params.set("name", name);
}

void Dialogue::exchange(const std::string& name, const std::vector<Line>& lines, const std::string& topics, const std::string& after, bool ends,
                        const std::string& prompt) {
    Element& e = add_element(part(name), kExchange);
    e.params.set("name", name).set("topics", topics).set("after", after).set("ends", ends ? 1.0 : 0.0).set("heard", 0.0);
    e.params.set("lines", static_cast<double>(lines.size()));
    for (std::size_t i = 0; i < lines.size(); ++i) e.params.set(Key{"who." + std::to_string(i)}, lines[i].who).set(Key{"say." + std::to_string(i)}, lines[i].text);
    // (Chosen by its first line, if that is yours.)
    if (!prompt.empty()) e.params.set("prompt", prompt);
    else if (!lines.empty() && lines.front().who == "you") e.params.set("prompt", lines.front().text).set("prompt_line", 1.0);
}

void Dialogue::start_at(const std::string& name) { params().set("start", name); }

void Dialogue::link(const std::string& from, const std::string& to, double weight) {
    const Key k = part("link." + from + ">" + to);
    Element* l = find(k);
    if (!l) l = &add_element(k, kLink), l->params.set("from", from).set("to", to).set("weight", 0.0).set("extra", 0.0);
    l->params.set("extra", l->params.num("extra") + weight);
}

void Dialogue::connect() {
    std::vector<const Element*> all;
    for (const Element& e : elements())
        if (e.kind == kExchange) all.push_back(&e);
    for (const Element* a : all)
        for (const Element* b : all) {
            if (a == b) continue;
            const std::string from = str(a->params, "name"), to = str(b->params, "name");
            // How related: the topics they share, of all either has; anything
            // that is about everything (`*`) a little related to all.
            const std::set<std::string> ta = words(str(a->params, "topics")), tb = words(str(b->params, "topics"));
            std::size_t shared = 0;
            for (const std::string& w : ta) shared += tb.count(w);
            const std::size_t either = ta.size() + tb.size() - shared;
            double w = either ? static_cast<double>(shared) / static_cast<double>(either) : 0.0;
            if (ta.count("*") || tb.count("*")) w = std::max(w, 0.05);
            const Element* made = find(part("link." + from + ">" + to));
            const double extra = made ? made->params.num("extra") : 0.0;
            if (w + extra <= 0.0) continue;
            link(from, to, 0.0);
            Element& l = element(part("link." + from + ">" + to));
            l.params.set("weight", w + extra);
            // The link is an arrow from the one exchange to the other.
            const std::string name = l.id.str();
            arrow(Key{"go." + from + ">" + to}, a->id, b->id, go_event(), [name, to](State& s, Element& at, Element*, const Event& ev) {
                auto& d = static_cast<Dialogue&>(s);
                if (ev.args.get_or<std::string>("link", "") != name || str(d.params(), "at") != str(at.params, "name")) return;
                d.begin(to);
            });
        }
}

bool Dialogue::read_text(const std::string& text, std::string* why, const std::string& from) {
    struct Ex {
        std::string name, about, after, prompt;
        std::vector<Line> lines;
        bool ends = false;
    };
    std::vector<Ex> said;
    std::vector<std::tuple<std::string, std::string, double>> links;
    const auto trim = [](const std::string& s) {
        const auto a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    const auto run_on = [](std::string& to, const std::string& more) { to += (to.empty() ? "" : " ") + more; };
    const auto fail = [&](int n, const std::string& what) {
        if (why) *why = (from.empty() ? id().str() : from) + ":" + std::to_string(n) + ": " + what;
        return false;
    };
    // A line by `who`: run on from the one before, if theirs too.
    const auto say = [&](int n, const std::string& who, const std::string& words) {
        if (said.empty()) return fail(n, "a line said outside an exchange");
        std::vector<Line>& ls = said.back().lines;
        if (!ls.empty() && ls.back().who == who) run_on(ls.back().text, words);
        else ls.push_back(Line{who, words});
        return true;
    };
    std::istringstream in(text);
    std::string raw;
    int n = 0;
    while (std::getline(in, raw)) {
        ++n;
        const std::string t = trim(raw);
        if (t.empty() || t[0] == '#') continue;
        if (t[0] == '>' || t[0] == '<') {
            if (!say(n, t[0] == '>' ? "you" : "them", trim(t.substr(1)))) return false;
            continue;
        }
        std::istringstream ls(t);
        std::string word;
        ls >> word;
        std::string rest;
        std::getline(ls, rest);
        rest = trim(rest);
        if (word.size() > 1 && word.back() == ':' && find(voice_id(word.substr(0, word.size() - 1)))) {
            if (!say(n, word.substr(0, word.size() - 1), rest)) return false;
        } else if (word == "speaker") {
            std::istringstream sr(rest);
            std::string who;
            sr >> who;
            std::string name;
            std::getline(sr, name);
            if (who.empty()) return fail(n, "a speaker with no name");
            speaker(who, trim(name).empty() ? who : trim(name));
        } else if (word == "start") start_at(rest);
        else if (word == "exchange") said.push_back(Ex{rest, {}, {}, {}, {}, false});
        else if (word == "about" && !said.empty()) run_on(said.back().about, rest);
        else if (word == "after" && !said.empty()) run_on(said.back().after, rest);
        else if (word == "prompt" && !said.empty()) run_on(said.back().prompt, rest);
        else if (word == "ends" && !said.empty()) said.back().ends = true;
        else if (word == "link") {
            std::istringstream lr(rest);
            std::string a, b;
            double w = 0.5;
            lr >> a >> b >> w;
            links.emplace_back(a, b, w);
        } else return fail(n, "what is `" + word + "`?");
    }
    for (const Ex& e : said) {
        for (const Line& l : e.lines)
            if (!find(voice_id(l.who))) speaker(l.who, l.who);  // (`them`, unless said otherwise)
        exchange(e.name, e.lines, e.about, e.after, e.ends, e.prompt);
    }
    for (const auto& [a, b, w] : links) link(a, b, w);
    connect();
    return true;
}

bool Dialogue::read(const std::string& path, std::string* why) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (why) *why = "no such file: " + path;
        return false;
    }
    return read_text(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()), why, path);
}

// --- what it is now --------------------------------------------------------------------------

const Element* Dialogue::exchange_of(const std::string& name) const {
    const Element* e = find(part(name));
    return e && e->kind == kExchange ? e : nullptr;
}

std::vector<Dialogue::Line> Dialogue::lines_of(const std::string& exchange) const {
    std::vector<Line> out;
    if (const Element* e = exchange_of(exchange))
        for (int i = 0, n = static_cast<int>(e->params.num("lines")); i < n; ++i)
            out.push_back(Line{str(e->params, "who." + std::to_string(i)), str(e->params, "say." + std::to_string(i))});
    return out;
}

std::string Dialogue::prompt_of(const std::string& exchange) const {
    const Element* e = exchange_of(exchange);
    return e ? str(e->params, "prompt") : std::string();
}

bool Dialogue::heard(const std::string& exchange) const {
    const Element* e = exchange_of(exchange);
    return e && e->params.num("heard") >= 1.0;
}

bool Dialogue::ends(const std::string& exchange) const {
    const Element* e = exchange_of(exchange);
    return e && e->params.num("ends") > 0.5;
}

std::string Dialogue::to_of(const std::string& link) const {
    const Element* l = find(Key{link});
    return l && l->kind == kLink ? str(l->params, "to") : std::string();
}

std::vector<std::string> Dialogue::history() const { return list(str(params(), "history")); }

std::string Dialogue::name_of(const std::string& who) const {
    const Element* v = find(voice_id(who));
    return v ? str(v->params, "name") : who;
}

std::vector<std::string> Dialogue::speakers() const {
    std::vector<std::string> out{"you"};
    for (const Element& e : elements())
        if (e.kind == kVoice && str(e.params, "who") != "you") out.push_back(str(e.params, "who"));
    return out;
}

double Dialogue::mouth(const std::string& who) const {
    const Element* v = find(voice_id(who));
    return v ? v->params.num("mouth") : 0.0;
}

double Dialogue::saying(const std::string& text) const { return static_cast<double>(text.size()) * params().num("letter"); }

std::vector<std::string> Dialogue::options() const { return options_from(at()); }

std::vector<std::string> Dialogue::options_from(const std::string& here) const {
    struct Way {
        std::string link;
        double score;
    };
    std::vector<Way> ways;
    for (const Element& l : elements()) {
        if (l.kind != kLink || str(l.params, "from") != here || l.params.num("weight") <= 0.0) continue;
        const Element* to = exchange_of(str(l.params, "to"));
        if (!to) continue;
        // Hidden until what it waits on has been heard.
        bool open = true;
        for (const std::string& w : words(str(to->params, "after")))
            if (const Element* need = exchange_of(w); !need || need->params.num("heard") < 1.0) open = false;
        if (!open) continue;
        // The strongest first; what has not been heard before what has.
        ways.push_back({l.id.str(), l.params.num("weight") + (to->params.num("heard") < 1.0 ? 1.0 : 0.0)});
    }
    std::stable_sort(ways.begin(), ways.end(), [](const Way& a, const Way& b) { return a.score > b.score; });
    std::vector<std::string> out;
    const auto most = static_cast<std::size_t>(std::max(0.0, params().num("options")));
    for (std::size_t i = 0; i < ways.size() && i < most; ++i) out.push_back(ways[i].link);
    return out;
}

// --- its arrows ------------------------------------------------------------------------------

void Dialogue::begun(const std::string&, const std::string&, bool) {}
void Dialogue::ticked(double, double) {}
bool Dialogue::other_key(const std::string&, const std::vector<std::string>&, int&) { return false; }

void Dialogue::on_enter(const Params& args) {
    Surface2D::on_enter(args);
    // Taken up: it begins where it always begins - what has been heard, heard.
    const std::string start = str(params(), "start");
    params().set("history", std::string()).set("at", std::string());
    if (!start.empty() && exchange_of(start)) begin(start);
}

void Dialogue::begin(const std::string& name) {
    Params& p = params();
    // Chosen from what could follow (`chose`, which of `ways_were`): what it
    // was chosen by goes to where it is said; else its first line is said.
    const bool chosen = p.num("chose", -1.0) >= 0.0;
    const std::string was = str(p, "at");
    // What was said goes to the history (the last `keep` of it).
    if (!was.empty()) {
        std::vector<std::string> h = history();
        h.push_back(was);
        const auto keep = static_cast<std::size_t>(std::max(0.0, p.num("keep")));
        if (h.size() > keep) h.erase(h.begin(), h.end() - static_cast<std::ptrdiff_t>(keep));
        p.set("history", joined(h));
    }
    begun(was, name, chosen);
    p.set("last", was).set("at", name).set("phase", std::string(chosen ? "moving" : "saying")).set("line", 0.0);
    p.set("phase_since", 0.0).set("rebase", 1.0).set("choice", 0.0);
    if (!chosen) p.set("ways_were", std::string());
    p.set("moved", chosen ? p.num("chose") : -1.0).set("chose", -1.0);
    if (Element* e = find(part(name))) e->params.set("heard", e->params.num("heard") + 1.0);
    invalidate();
}

void Dialogue::go(const std::string& link) {
    // Which of what could follow it was, as they stood.
    const std::vector<std::string> ways = options();
    const auto at = std::find(ways.begin(), ways.end(), link);
    params().set("ways_were", joined(ways)).set("chose", at == ways.end() ? -1.0 : static_cast<double>(at - ways.begin()));
    // Along it: the arrow from where it is to where the link leads.
    hear(Event{go_event(), Params{}.set("link", link)});
}

void Dialogue::on_key(const std::string& k) {
    Params& p = params();
    invalidate();
    const std::string ph = phase();
    const Element* here = exchange_of(at());
    if (k == "esc") {
        emit(Event{out_event()});
        return;
    }
    if (!here) return;
    // (How far into its phase it is, as its own last step had it; a moment
    // asked for here is stamped by its own next step, from its own time.)
    const double since = p.num("phase_since");
    const auto from = [&p](double t) { p.set("phase_since", t).set("rebase", 1.0); };
    if (ph == "moving" || ph == "waiting" || ph == "saying") {
        // Enter: the words that are coming, come.
        if (k != "enter") return;
        const std::vector<Line> lines = lines_of(at());
        const int i = line();
        const bool last = i + 1 >= static_cast<int>(lines.size());
        if (ph == "moving") from(p.num("move"));
        else if (ph == "waiting") p.set("phase", std::string("saying")), from(0.0);
        else if (i < static_cast<int>(lines.size()) && since < saying(lines[static_cast<std::size_t>(i)].text)) from(saying(lines[static_cast<std::size_t>(i)].text));
        else if (last && !ends(at())) p.set("phase", std::string("choose")), from(p.num("show"));
        return;
    }
    if (ph != "choose") return;
    // What can follow still coming in: Enter (a click) brings it all, and
    // takes none of it - only what has been seen is chosen.
    // (A moment restamped from its own time comes back a hair short of itself.)
    if (since + 1e-9 < p.num("show") && (k == "enter" || (k.size() == 1 && k[0] >= '1' && k[0] <= '9'))) {
        from(p.num("show"));
        return;
    }
    const std::vector<std::string> ways = options();
    if (ways.empty()) return;
    const int n = static_cast<int>(ways.size());
    int c = static_cast<int>(p.num("choice"));
    if (k == "up") c = (c + n - 1) % n;
    else if (k == "down") c = (c + 1) % n;
    else if (k.size() == 1 && k[0] >= '1' && k[0] <= '9') {
        if (const int pick = k[0] - '1'; pick < n) go(ways[static_cast<std::size_t>(pick)]);
        return;
    } else if (k == "enter") {
        go(ways[static_cast<std::size_t>(std::clamp(c, 0, n - 1))]);
        return;
    } else if (!other_key(k, ways, c)) return;
    p.set("choice", static_cast<double>(c));
}

void Dialogue::after_line(int next, double time) {
    Params& p = params();
    const int n = static_cast<int>(lines_of(at()).size());
    if (next < n) p.set("phase", std::string("waiting")).set("line", static_cast<double>(next));
    else p.set("phase", std::string(ends(at()) ? "ending" : "choose"));
    p.set("phase_at", time).set("phase_since", 0.0);
}

void Dialogue::on_tick(double dt, double time) {
    Params& p = params();
    // A moment asked for since its last step (a phase begun, or brought on):
    // stamped now, in its own time.
    if (p.num("rebase") > 0.5) p.set("phase_at", time - p.num("phase_since")).set("rebase", 0.0);
    const Element* here = exchange_of(at());
    if (!here) return;
    const std::string ph = phase();
    const double since = time - p.num("phase_at");
    if (p.num("phase_since") != since) p.set("phase_since", since);
    const std::vector<Line> lines = lines_of(at());
    const int i = line();
    const Line* now = ph == "saying" && i < static_cast<int>(lines.size()) ? &lines[static_cast<std::size_t>(i)] : nullptr;
    // Each voice: as open as the letters being said now are vowels (a few of
    // them: a syllable's worth, the letters coming faster than a voice), shut
    // over a gap between words, at the end and while another speaks; eased,
    // as a jaw moves.
    for (Element& v : elements()) {
        if (v.kind != kVoice) continue;
        double want = 0.0;
        if (now && now->who == str(v.params, "who")) {
            const std::string& s = now->text;
            const auto at_letter = static_cast<std::ptrdiff_t>(since / p.num("letter"));
            int vowels = 0, letters = 0;
            for (std::ptrdiff_t j = at_letter - 2; j <= at_letter + 2; ++j) {
                if (j < 0 || j >= static_cast<std::ptrdiff_t>(s.size())) continue;
                const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[static_cast<std::size_t>(j)])));
                if (std::isalpha(static_cast<unsigned char>(c))) ++letters, vowels += std::string("aeiouy").find(c) != std::string::npos;
            }
            if (at_letter < static_cast<std::ptrdiff_t>(s.size()) && letters > 0) want = std::min(1.0, 0.25 + 1.3 * vowels / 5.0);
        }
        const double m = v.params.num("mouth");
        const double eased = m + (want - m) * std::min(1.0, std::max(0.0, dt) / 0.06);
        if (std::fabs(eased - m) > 1e-4 || (want == 0.0 && m != 0.0 && eased < 0.02)) v.params.set("mouth", eased < 0.02 && want == 0.0 ? 0.0 : eased);
    }
    ticked(dt, time);
    if (ph == "saying" && now) {
        const bool last = i + 1 >= static_cast<int>(lines.size());
        if (since > saying(now->text) + p.num(last ? "hold_last" : "hold")) after_line(i + 1, time);
    } else if (ph == "moving" && since > p.num("move")) after_line(here->params.num("prompt_line") > 0.5 ? 1 : 0, time);
    else if (ph == "waiting" && since > p.num("pause")) p.set("phase", std::string("saying")).set("phase_at", time).set("phase_since", 0.0);
    else if (ph == "saying" && !now) after_line(i + 1, time);
    else if (ph == "ending" && since > p.num("end")) {
        p.set("phase", std::string("over"));
        emit(Event{out_event()});
    }
}

}  // namespace sg
