#include "sg/dsl/Compiler.hpp"

namespace sg::dsl {

namespace {

const Key kText{"text"};

std::string text_of(State& s, const char* element) { return s.element(Key{element}).params.get_or<std::string>(kText, ""); }

}  // namespace

Options options_for(const StateGraph& g, const Kinds& kinds) {
    Options o;
    o.kinds = &kinds;
    for (Key id : g.ids()) o.live_states[id.str()] = g.state(id).kind().str();
    for (const auto& kv : g.functors()) o.live_functors[kv.first.str()] = {kv.second.from().str(), kv.second.to().str()};
    return o;
}

void register_compiler(Natives& into, const Natives& available, Bindings* bindings, const Kinds& kinds) {
    // source: document --set_text--> document
    into.arrow("source_set_text", [](State&, Element& doc, Element*, const Event& ev) {
        if (ev.args.has(kText)) doc.params.set(kText, ev.args.get_or<std::string>(kText, ""));
    });
    // source: document --request--> document : the source says it is to be compiled
    into.arrow("source_request", [](State& s, Element&, Element*, const Event&) { s.emit(Event{Key{"source.compile"}}); });

    // compiler: idle --begin--> compiling. Reads the text and says whether it
    // is a lawful presentation of a construction; changes nothing outside.
    into.arrow("compile_begin", [&available, &kinds](State& s, Element&, Element*, const Event&) {
        // what the live graph holds is read through the engine, const
        Options o = s.engine() ? options_for(s.engine()->graph(), kinds) : Options{};
        o.kinds = &kinds;
        const Compiled c = compile_source(text_of(s, "input"), "<source>", o);
        Element& report = s.element(Key{"report"});
        report.params.set(Key{"why"}, c.report());
        s.params().set(Key{"phase"}, std::string(c.ok() ? "compiling" : "error"));
        s.emit(Event{Key{c.ok() ? "compiler.valid" : "compiler.invalid"}});
    });
    // compiler: compiling --valid--> success : asks the graph to change, by saying so
    into.arrow("compile_valid", [](State& s, Element&, Element*, const Event&) {
        s.emit(Event{Key{"compiler.change"}, Params{}.set(kText, text_of(s, "input"))});
    });
    // compiler: compiling --invalid--> error
    into.arrow("compile_invalid", [](State& s, Element&, Element*, const Event&) { s.params().set(Key{"phase"}, std::string("error")); });
    // compiler: report --settled--> report : the graph's answer to the edit
    into.arrow("compile_settled", [](State& s, Element& report, Element*, const Event& ev) {
        const bool ok = ev.args.get_or<bool>(Key{"ok"}, false);
        report.params.set(Key{"ok"}, ok);
        report.params.set(Key{"why"}, ev.args.get_or<std::string>(Key{"why"}, ""));
        s.params().set(Key{"phase"}, std::string(ok ? "success" : "error"));
    });

    // The edit: the one place the world rewrites itself. It compiles the text
    // it was asked with, against what the graph holds now, and makes the plan.
    into.edit("compile_apply", [&available, bindings, &kinds](StateGraph& g, const Event& asked) {
        Params answer;
        Options o = options_for(g, kinds);
        const Compiled c = compile_source(asked.args.get_or<std::string>(kText, ""), "<source>", o);
        if (!c.ok()) return answer.set(Key{"ok"}, false).set(Key{"why"}, c.report());
        const Applied a = apply(c.plan, g, available, bindings, kinds);
        return answer.set(Key{"ok"}, a.ok).set(Key{"why"}, a.why);
    });

    // The same source edited, made again: asked with what it says now
    // (`text`) and what it said when it was last made (`before`), it replaces
    // only what changed (Apply.hpp: reload), and answers which declarations.
    into.edit("compile_reload", [&available, bindings, &kinds](StateGraph& g, const Event& asked) {
        Params answer;
        Options o = options_for(g, kinds);
        o.conform = true;  // what the source declared is in the graph: it is what is replaced
        const Compiled was = compile_source(asked.args.get_or<std::string>(Key{"before"}, ""), "<before>", o);
        const Compiled now = compile_source(asked.args.get_or<std::string>(kText, ""), "<source>", o);
        if (!now.ok()) return answer.set(Key{"ok"}, false).set(Key{"why"}, now.report());
        if (!was.ok()) return answer.set(Key{"ok"}, false).set(Key{"why"}, "what it said before does not compile now: " + was.report());
        const Reloaded r = reload(was.plan, now.plan, g, available, bindings, kinds);
        std::string changed;
        for (const std::string& c : r.changed) changed += (changed.empty() ? "" : "\n") + c;
        return answer.set(Key{"ok"}, r.ok).set(Key{"why"}, r.why).set(Key{"changed"}, changed);
    });
}

}  // namespace sg::dsl
