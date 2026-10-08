// Stategine - the laws, and proof that they bite.
//
// Every law is tested twice: once on something that keeps it, and once on
// something built to break it, where the test reads the counterexample back
// and checks it names the right element, parameter and values. A checker that
// only ever says "clean" proves nothing.
//
// The domain is deliberately not spatial - a shop and its ledger - since
// nothing here is allowed to know what a room is.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/core/Sheaf.hpp"
#include "sg/core/Temporal.hpp"
#include "sg/core/Typed.hpp"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}

void show(const std::vector<sg::Violation>& vs) {
    for (const auto& v : vs) std::printf("        %s\n", v.str().c_str());
}

const sg::Violation* find_law(const std::vector<sg::Violation>& vs, const std::string& law) {
    for (const auto& v : vs)
        if (v.law == law) return &v;
    return nullptr;
}

double num(const sg::State& s, sg::Key e, sg::Key k) { return s.element(e).params.num(k); }
bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

// A shop: a till and a shelf, and arrows that move stock and money.
sg::State& make_shop(sg::StateGraph& g) {
    auto& shop = g.add<sg::State>("shop");
    shop.add_element("shelf", "shelf").params.set("stock", int64_t{30});
    shop.add_element("till", "till").params.set("cash", 0.0);
    shop.loop("restock", "shelf", "restock",
              [](sg::State&, sg::Element& s, sg::Element*, const sg::Event& ev) {
                  s.params.set("stock", s.params.num("stock") + ev.args.num("n", 12.0));
              });
    shop.loop("halve", "shelf", "halve", [](sg::State&, sg::Element& s, sg::Element*,
                                            const sg::Event&) {
        s.params.set("stock", std::floor(s.params.num("stock") / 2.0));
    });
    shop.loop("dust", "shelf", "dust", [](sg::State&, sg::Element& s, sg::Element*,
                                          const sg::Event&) { s.params.set("dusted", true); });
    shop.arrow("sell", "shelf", "till", "sell",
               [](sg::State&, sg::Element& s, sg::Element* t, const sg::Event&) {
                   s.params.set("stock", s.params.num("stock") - 1.0);
                   t->params.set("cash", t->params.num("cash") + 5.0);
               });
    shop.arrow("refund", "till", "shelf", "refund",
               [](sg::State&, sg::Element& t, sg::Element* s, const sg::Event&) {
                   t.params.set("cash", t.params.num("cash") - 5.0);
                   s->params.set("stock", s->params.num("stock") + 1.0);
               });
    return shop;
}

// --- composition has types --------------------------------------------------------
void test_composites_have_the_right_type() {
    sg::State s("s");
    s.add_element("x", "n");
    s.add_element("y", "n");
    s.arrow("f", "x", "y", "t", nullptr);
    s.loop("spin", "y", "t", nullptr);
    s.loop("turn", "x", "t", nullptr);

    const sg::Morphism& a = s.compose("spin.f", "f", "spin", "t");
    check(sg::dom(a) == sg::Key{"x"} && sg::cod(a) == sg::Key{"y"},
          "f then a loop on y is an arrow x -> y, not a loop on x");
    bool loops_compose = true;
    try {
        s.compose("spin.spin", "spin", "spin", "t");
        s.compose("f.turn", "turn", "f", "t");
    } catch (const std::exception&) {
        loops_compose = false;
    }
    check(loops_compose, "a loop composes with whatever starts where it does");
    check(sg::cod(*s.morphism("spin.spin")) == sg::Key{"y"}, "and loop . loop is still a loop");
    bool threw = false;
    try {
        s.compose("bad", "spin", "f", "t");  // cod(spin) = y, dom(f) = x
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a loop still refuses what does not start where it ends");
    check(s.morphism("spin.f")->parts.size() == 2, "a composite remembers what it was made of");
}

// --- identity -------------------------------------------------------------------------
void test_identity_is_a_law_not_a_table() {
    sg::StateGraph g;
    auto& a = g.add<sg::State>("a");
    g.add<sg::State>("b");
    a.add_element("x", "n").params.set("v", 2.0);
    g.add_functor("f", "a", "b").on_object("x", "y");  // y does not exist yet: f would create it
    g.connect("a", "go", "b");

    // A trial never changes what the graph is made of, so an equation whose
    // side would add an element is not checked - and says so, as unchecked,
    // not as broken - and the graph is as it was.
    const auto unchecked = sg::laws::identity(g);
    bool all_refused = !unchecked.empty();
    for (const auto& v : unchecked) all_refused = all_refused && v.refused && v.detail.find("add_element") != std::string::npos;
    check(all_refused && !g.state("b").find("y"),
          "id ; F is not checked while F would create what it maps to - reported as unchecked, nothing added");
    const sg::LawReport r = sg::verify(g);
    check(r.ok() && !r.all_checked() && r.unchecked.size() == unchecked.size(), "verify keeps them apart from counterexamples");

    // What the identity used to be: a copy of the object list, taken once -
    // here while b was still empty.
    sg::Functor snapshot_id("snapshot_id", "b", "b");
    for (const auto& e : g.state("b").elements()) snapshot_id.on_object(e.id, e.id);
    g.state("b").add_element("y", "n");  // b grows; now f has somewhere to land
    const auto clean = sg::laws::identity(g);
    show(clean);
    check(clean.empty(), "id ; F == F == F ; id, once what F maps to is there");
    sg::Diagram d("the old identity");
    d.commutes(sg::Path("a").functor(sg::Functor::compose(*g.functor("f"), snapshot_id)),
               sg::Path("a").functor("f"));
    const auto broken = sg::laws::diagram(g, d);
    show(broken);
    check(broken.size() == 1 && broken[0].state == sg::Key{"b"} && broken[0].element == sg::Key{"y"} &&
              broken[0].key == "v" && broken[0].left == "<unset>" && !broken[0].refused,
          "a snapshot identity is caught: it knows nothing of b.y, which grew after it was taken");

    bool refused = false;
    try {
        sg::Functor::identity(a).on_object("x", "z");
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "an identity cannot be edited into something else");
}

// --- counterexamples ---------------------------------------------------------------------
void test_a_counterexample_is_concrete() {
    sg::StateGraph g;
    auto& shop = make_shop(g);

    sg::Diagram d("shelf work");
    d.commutes(sg::Path("shop", "shelf").arrow("restock").arrow("dust"),
               sg::Path("shop", "shelf").arrow("dust").arrow("restock"));
    check(sg::laws::diagram(g, d).empty(), "restocking and dusting commute");

    sg::Diagram bad("halve and restock");
    bad.commutes(sg::Path("shop", "shelf").arrow("restock").arrow("halve"),
                 sg::Path("shop", "shelf").arrow("halve").arrow("restock"));
    const auto vs = sg::laws::diagram(g, bad);
    show(vs);
    check(vs.size() == 1, "halving and restocking do not");
    if (!vs.empty()) {
        const sg::Violation& v = vs[0];
        check(v.state == sg::Key{"shop"} && v.element == sg::Key{"shelf"} && v.key == "stock",
              "the counterexample names the element and the parameter");
        check(v.before == "30",
              "and what it held before either side ran");
        check(v.left == "21.000000" && v.right == "27.000000",
              "and what each side left there: (30 + 12) / 2 against 30 / 2 + 12");
    }

    // Arguments are part of the data: restock by nothing and the two commute.
    bad = sg::Diagram("halve and restock by nothing");
    bad.commutes(sg::Path("shop", "shelf").arrow("restock").arrow("halve"),
                 sg::Path("shop", "shelf").arrow("halve").arrow("restock"),
                 sg::Params{}.set("n", 0.0));
    check(sg::laws::diagram(g, bad).empty(), "and the same diagram holds for other data");
    shop.element("shelf").params.set("stock", int64_t{31});
    check(sg::laws::diagram(g, bad).empty(), "restocking nothing commutes with anything");

    // Paths that do not even meet are reported as such, not run.
    sg::Diagram nonsense("sell twice");
    nonsense.commutes(sg::Path("shop", "shelf").arrow("sell").arrow("sell"),
                      sg::Path("shop", "shelf").arrow("sell"));
    const auto ill = sg::laws::diagram(g, nonsense);
    check(ill.size() == 1 && ill[0].detail.find("starts at shelf") != std::string::npos,
          "a path that does not type is named at the step where it breaks");
}

// --- functoriality ------------------------------------------------------------------------
void test_functors_carry_the_action_not_just_the_arrow() {
    sg::StateGraph g;
    make_shop(g);
    auto& ledger = g.add<sg::State>("ledger");
    ledger.add_element("book", "entry");
    ledger.loop("order", "book", "restock",
                [](sg::State&, sg::Element& b, sg::Element*, const sg::Event& ev) {
                    b.params.set("stock", b.params.num("stock") + ev.args.num("n", 12.0));
                });
    ledger.loop("order_wrong", "book", "restock",
                [](sg::State&, sg::Element& b, sg::Element*, const sg::Event&) {
                    b.params.set("stock", b.params.num("stock") + 10.0);
                });
    g.add_functor("post", "shop", "ledger")
        .on_object("shelf", "book", sg::transport::only({"stock"}))
        .on_morphism("restock", "order");
    g.connect("shop", "close", "ledger", "post");

    check(g.validate().empty(), "the ledger graph is well formed");
    const auto good = sg::laws::functoriality(g);
    show(good);
    check(good.empty(), "restock then post == post then order");

    // Structurally perfect - order_wrong is a loop on book like order - so
    // check_laws has nothing to say. Only the data does.
    g.functor("post")->on_morphism("restock", "order_wrong");
    check(g.validate().empty(), "a functor onto the wrong arrow still validates");
    const auto bad = sg::laws::functoriality(g);
    show(bad);
    check(bad.size() == 1 && bad[0].element == sg::Key{"book"} && bad[0].key == "stock" &&
              bad[0].left == "42.000000" && bad[0].right == "40.000000",
          "but the laws say post(restock) adds 10 where restock added 12");
}

// --- composition and associativity ------------------------------------------------------
void test_composites_cannot_drift() {
    sg::StateGraph g;
    auto& shop = make_shop(g);
    shop.compose("sell.refund", "sell", "refund", "cycle");
    shop.compose("restock.sell.refund", "restock", "sell.refund", "cycle");
    auto& ledger = g.add<sg::State>("ledger");
    ledger.add_element("book", "entry");
    g.add_functor("post", "shop", "ledger").on_object("shelf", "book", sg::transport::copy_all);
    g.add_functor("audit", "ledger", "ledger")
        .on_object("book", "book", sg::transport::swizzle({{"audited", "stock"}}, true));
    g.compose_functors("post_audited", {"post", "audit"});
    g.connect("shop", "close", "ledger", "post_audited");

    const auto clean = sg::laws::composition(g);
    show(clean);
    check(clean.empty(), "every composite does what its parts do in order");
    const auto assoc = sg::laws::associativity(g);
    show(assoc);
    check(assoc.empty(), "and bracketing does not matter, for arrows or functors");

    // A part is rebuilt - as a derived transition would be - and the composite,
    // built from the old one, now says something the chain does not.
    g.set_functor(sg::Functor{"audit", "ledger", "ledger"})
        .on_object("book", "book", sg::transport::swizzle_scaled({{"audited", "stock"}},
                                                                 [](double s) { return s * 2; },
                                                                 true));
    const auto drift = sg::laws::composition(g);
    show(drift);
    const sg::Violation* v = find_law(drift, "composition");
    check(v && v->element == sg::Key{"book"} && v->key == "audited" && v->left == "30" &&
              v->right == "60.000000",
          "a composite left behind by its parts is caught, with the value it gets wrong");
}

// --- lenses --------------------------------------------------------------------------------
struct Desk {
    sg::StateGraph g;
    sg::State* library = nullptr;
    sg::State* sheet = nullptr;

    Desk(sg::Transport to_sheet, sg::Transport to_shelf) {
        library = &g.add<sg::State>("library");
        library->add_element("dune", "book").params.set("copies", int64_t{30});
        library->add_element("desk", "portal");
        sheet = &g.add<sg::State>("sheet");
        sheet->add_element("row", "row");
        g.add_lens("show", "put", "library", "sheet", {{"dune", "row"}}, std::move(to_sheet),
                   std::move(to_shelf));
        g.embed("desk", "library", "desk", "sheet", "show", "put", sg::EmbedSync::Commit);
        g.set_initial("library");
    }
};

sg::Transport in_dozens() {
    return sg::transport::swizzle_scaled({{"dozens", "copies"}},
                                         [](double c) { return std::floor(c / 12.0); });
}

void test_lens_laws() {
    {
        Desk d(in_dozens(), sg::transport::swizzle_scaled({{"copies", "dozens"}},
                                                          [](double n) { return n * 12.0; }));
        const auto vs = sg::laws::lenses(d.g);
        show(vs);
        check(vs.empty(), "a view in whole dozens is a lawful lens, though a lossy one");

        // Open it and type a value the view cannot hold: 2.5 dozen.
        sg::Engine e(d.g);
        e.start();
        e.open_embed("desk");
        d.sheet->element("row").params.set("dozens", 2.5);
        const auto live = sg::laws::lenses(d.g);
        show(live);
        const sg::Violation* v = find_law(live, "put-get");
        check(v && v->element == sg::Key{"row"} && v->key == "dozens" && v->before == "2.500000" &&
                  v->left == "2.000000",
              "an edit the view cannot hold is named: you wrote 2.5 and it reads back 2");
        check(num(*d.sheet, "row", "dozens") == 2.5, "and checking it did not write anything");
    }
    {
        // Reads a fraction, writes one more than it read.
        Desk d(sg::transport::swizzle_scaled({{"n", "copies"}}, [](double c) { return c / 12.0; }),
               sg::transport::swizzle_scaled({{"copies", "n"}},
                                             [](double n) { return n * 12.0 + 1.0; }));
        const auto vs = sg::laws::lenses(d.g);
        show(vs);
        check(find_law(vs, "put-get") != nullptr, "a write-back that adds one breaks put-get");
        check(find_law(vs, "settles") != nullptr, "and never settles");
    }
    {
        // Sets correctly on the first write, adds on every write after.
        Desk d(in_dozens(), [](const sg::Element& s, sg::Element& dst) {
            dst.params.set("copies", s.params.num("dozens") * 12.0 +
                                         (dst.params.has("written") ? 12.0 : 0.0));
            dst.params.set("written", true);
        });
        const auto vs = sg::laws::lenses(d.g);
        show(vs);
        const sg::Violation* v = find_law(vs, "put-put");
        check(v && v->element == sg::Key{"dune"} && v->key == "copies",
              "a write-back that accumulates breaks put-put, on the subject's own element");
    }
}

// --- checking changes nothing ---------------------------------------------------------------
void test_checking_is_invisible() {
    sg::StateGraph g;
    auto& shop = make_shop(g);
    shop.compose("sell.refund", "sell", "refund", "cycle");
    shop.emit("pending_event");
    sg::Element* shelf = &shop.element("shelf");
    const double stock = num(shop, "shelf", "stock");
    const std::size_t arrows = shop.morphisms().size();

    sg::LawOptions o;
    o.args.set("n", 7.0);
    const sg::LawReport r = sg::verify(g, {}, o);
    show(r.violations);
    check(r.violations.empty(), "the shop keeps every law");
    check(&shop.element("shelf") == shelf, "elements stay where callers hold them");
    check(num(shop, "shelf", "stock") == stock && !shop.element("shelf").params.has("dusted"),
          "their data is untouched");
    check(shop.morphisms().size() == arrows, "no arrow was left registered");
    check(shop.bus().queued().size() == 1, "and the queue is as it was");
}

void test_enforce_refuses_a_lie() {
    sg::StateGraph g;
    make_shop(g);
    sg::Diagram d("halve and restock");
    d.commutes(sg::Path("shop", "shelf").arrow("restock").arrow("halve"),
               sg::Path("shop", "shelf").arrow("halve").arrow("restock"));
    bool threw = false;
    std::string what;
    try {
        sg::enforce(g, {d});
    } catch (const sg::LawError& e) {
        threw = true;
        what = e.what();
    }
    check(threw && what.find("shop.shelf.stock") != std::string::npos,
          "enforce throws, and the message carries the counterexample");

    // What cannot be checked is not taken for lawful.
    sg::StateGraph g2;
    auto& a = g2.add<sg::State>("a");
    g2.add<sg::State>("b");
    a.add_element("x", "n").params.set("v", 2.0);
    g2.add_functor("f", "a", "b").on_object("x", "y");  // would create b.y: not checkable
    g2.connect("a", "go", "b");
    const sg::LawReport r = sg::verify(g2);
    check(r.ok() && !r.all_checked() && !r.holds(), "no counterexample is not the laws holding");
    threw = false;
    try {
        sg::enforce(g2);
    } catch (const sg::LawError& e) {
        threw = true;
        what = e.what();
    }
    check(threw && what.find("cannot be shown") != std::string::npos,
          "enforce refuses an equation it could not check, and says it could not");
}

// A search cut short by its budget says so.
void test_budgets_are_visible() {
    sg::StateGraph g;
    make_shop(g);
    g.set_initial("shop");
    sg::LawOptions o;
    o.max_triples = 1;
    const sg::LawReport r = sg::verify(g, {}, o);
    check(!r.complete() && r.bounded.front().detail.find("budget of 1") != std::string::npos,
          "associativity stopped at its budget is reported as bounded");
    check(sg::verify(g).complete(), "and with room enough it is complete");
}

// --- transitions are arrows too ----------------------------------------------------------------
void test_transitions_in_paths() {
    sg::StateGraph g;
    make_shop(g);
    auto& ledger = g.add<sg::State>("ledger");
    ledger.add_element("book", "entry");
    g.add_functor("post", "shop", "ledger").on_object("shelf", "book", sg::transport::only({"stock"}));
    g.add_functor("reopen", "ledger", "shop").on_object("book", "shelf", sg::transport::only({"stock"}));
    g.connect("shop", "close", "ledger", "post");
    g.connect("ledger", "open", "shop", "reopen");
    g.pop("ledger", "back");

    sg::Diagram d("close and reopen");
    d.commutes(sg::Path("shop", "shelf").transition("shop-close->ledger").transition("ledger-open->shop"),
               sg::Path("shop", "shelf"));
    // A transition carries the event that took it into where it goes, as the
    // engine does: the shelf comes back as it was, with the reopening queued.
    const auto vs = sg::laws::diagram(g, d);
    bool only_carried = !vs.empty();
    for (const auto& v : vs) only_carried = only_carried && v.key == "<emitted>" && v.left.find("open@ledger/reopen") != std::string::npos;
    check(only_carried, "closing and reopening leaves the shelf as it was, and the event that reopened it carried in");

    sg::Diagram pop("pop");
    pop.commutes(sg::Path("ledger").transition("ledger-back->"), sg::Path("ledger"));
    const auto pv = sg::laws::diagram(g, pop);
    check(pv.size() == 1 && pv[0].detail.find("pop") != std::string::npos,
          "a pop has no target, so it is refused as a step");
}

// --- the typed layer ---------------------------------------------------------------------------
struct Shop {
    static constexpr const char* name = "shop";
};
struct Ledger {
    static constexpr const char* name = "ledger";
};
struct Sheet {
    static constexpr const char* name = "sheet";
};
struct ShelfT {
    using state = Shop;
    static constexpr const char* name = "shelf";
};
struct TillT {
    using state = Shop;
    static constexpr const char* name = "till";
};
struct CounterT {
    using state = Shop;
    static constexpr const char* name = "counter";
};
struct BookT {
    using state = Ledger;
    static constexpr const char* name = "book";
};
struct RowT {
    using state = Sheet;
    static constexpr const char* name = "row";
};

using sg::typed::Arrow;
using sg::typed::composable;
static_assert(composable<Arrow<ShelfT, TillT>, Arrow<TillT, ShelfT>>::value,
              "arrows that meet compose");
static_assert(!composable<Arrow<ShelfT, TillT>, Arrow<ShelfT, TillT>>::value,
              "arrows that do not meet do not");
static_assert(!composable<Arrow<Shop, Ledger>, Arrow<Shop, Ledger>>::value,
              "nor do functors that do not meet");
static_assert(!composable<Arrow<Shop, Ledger>, Arrow<BookT, BookT>>::value,
              "and a functor on a whole state does not meet an object of the target");

void test_typed() {
    namespace t = sg::typed;
    sg::StateGraph g;
    auto& shop = g.add<sg::State>("shop");
    shop.add_element("shelf", "shelf").params.set("stock", 30.0);
    shop.add_element("till", "till").params.set("cash", 0.0);
    shop.add_element("counter", "portal");
    auto& ledger = g.add<sg::State>("ledger");
    ledger.add_element("book", "entry");
    g.add<sg::State>("sheet").add_element("row", "row");

    const auto sell = t::arrow<ShelfT, TillT>(
        shop, "sell", "sell", [](sg::State&, sg::Element& s, sg::Element* till, const sg::Event&) {
            s.params.set("stock", s.params.num("stock") - 1);
            till->params.set("cash", till->params.num("cash") + 5);
        });
    const auto refund = t::arrow<TillT, ShelfT>(
        shop, "refund", "refund",
        [](sg::State&, sg::Element& till, sg::Element* s, const sg::Event&) {
            till.params.set("cash", till.params.num("cash") - 5);
            s->params.set("stock", s->params.num("stock") + 1);
        });
    const auto restock = t::arrow<ShelfT, ShelfT>(
        shop, "restock", "restock", [](sg::State&, sg::Element& s, sg::Element*, const sg::Event&) {
            s.params.set("stock", s.params.num("stock") + 12);
        });
    const auto order = t::arrow<BookT, BookT>(
        ledger, "order", "restock", [](sg::State&, sg::Element& b, sg::Element*, const sg::Event&) {
            b.params.set("stock", b.params.num("stock") + 12);
        });

    // Registered composite, typed: Shelf -> Till -> Shelf.
    const auto round = t::compose(shop, "sell.refund", refund, sell, "cycle");
    const Arrow<ShelfT, ShelfT> also_round = refund * sell;  // a path, same type

    auto post = t::functor<Shop, Ledger>(g, "post");
    post.on<ShelfT, BookT>(sg::transport::only({"stock"})).on(restock, order);
    t::connect(g, "close", post);

    auto view = t::lens<Shop, Sheet>(g, "show", "put");
    view.pair<ShelfT, RowT>(sg::transport::swizzle({{"n", "stock"}}),
                            sg::transport::swizzle({{"stock", "n"}}));
    t::embed<CounterT>(g, "counter", view.in, view.out);
    g.set_initial("shop");

    sg::Diagram d("typed");
    t::commutes(d, round, sg::typed::id<ShelfT>());
    t::commutes(d, also_round, round);
    t::commutes(d, post.at<ShelfT, BookT>() * restock, order * post.at<ShelfT, BookT>());
    const sg::LawReport r = sg::verify(g, {d});
    std::printf("%s", r.str().c_str());
    check(r.ok(), "a graph built from typed handles validates and keeps every law");
    check(g.transition("shop-close->ledger")->functor == sg::Key{"post"},
          "the typed transition carries the functor it was typed by");
    check(g.embedding("counter")->subject.empty(),
          "an embedding onto its own host leaves the subject implicit");

    bool bind_refused = false;
    try {
        (void)t::bind<TillT, TillT>(shop, "sell");  // sell is Shelf -> Till
    } catch (const std::logic_error&) {
        bind_refused = true;
    }
    check(bind_refused, "binding a name under the wrong type is refused where it happens");
    check(t::bind<ShelfT, TillT>(shop, "sell").name() == sg::Key{"sell"}, "and the right one binds");

    bool at_refused = false;
    try {
        (void)post.at<TillT, BookT>();  // post does not map the till
    } catch (const std::logic_error&) {
        at_refused = true;
    }
    check(at_refused, "restricting a functor to an object it does not map is refused");

    bool wrong_state = false;
    try {
        (void)t::arrow<BookT, BookT>(shop, "misfiled", "x", nullptr);
    } catch (const std::logic_error&) {
        wrong_state = true;
    }
    check(wrong_state, "an arrow typed for the ledger cannot be registered in the shop");
}

// --- what is queued is compared whole ---------------------------------------------------
// An event is its name, its arguments and who sent it: two paths that queue
// damage(5) and damage(500) did not leave the same result.
void test_events_are_compared_in_order() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("say");
    s.add_element("m", "mouth");
    g.set_initial("say");
    const auto say = [](const char* first, const char* second) {
        return [=](sg::State& st, sg::Element&, sg::Element*, const sg::Event&) {
            st.emit(sg::Key{first});
            st.emit(sg::Key{second});
        };
    };
    s.loop("ab", "m", "ab", say("a", "b"));
    s.loop("ab2", "m", "ab2", say("a", "b"));
    s.loop("ba", "m", "ba", say("b", "a"));
    sg::Diagram same("a then b, twice");
    same.commutes(sg::Path("say", "m").arrow("ab"), sg::Path("say", "m").arrow("ab2"));
    check(sg::laws::diagram(g, same).empty(), "the same events in the same order agree");
    sg::Diagram bad("a then b, b then a");
    bad.commutes(sg::Path("say", "m").arrow("ab"), sg::Path("say", "m").arrow("ba"));
    const auto vs = sg::laws::diagram(g, bad);
    show(vs);
    check(vs.size() == 1 && vs[0].key == "<emitted>",
          "a then b is not b then a: the queue is dispatched in order");
}

void test_events_are_compared_whole() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("fight");
    s.add_element("foe", "foe").params.set("hp", 10.0);
    g.set_initial("fight");
    const auto hit = [](double amount) {
        return [amount](sg::State& st, sg::Element&, sg::Element*, const sg::Event&) {
            st.emit(sg::Event{"damage", sg::Params{}.set("amount", amount)});
        };
    };
    s.loop("jab", "foe", "jab", hit(5.0));
    s.loop("poke", "foe", "poke", hit(5.0));
    s.loop("smash", "foe", "smash", hit(500.0));

    sg::Diagram same("jab or poke");
    same.commutes(sg::Path("fight", "foe").arrow("jab"), sg::Path("fight", "foe").arrow("poke"));
    check(sg::laws::diagram(g, same).empty(), "two paths that queue the same event agree");

    sg::Diagram bad("jab or smash");
    bad.commutes(sg::Path("fight", "foe").arrow("jab"), sg::Path("fight", "foe").arrow("smash"));
    const auto vs = sg::laws::diagram(g, bad);
    show(vs);
    check(vs.size() == 1 && vs[0].key == "<emitted>" &&
              vs[0].left.find("amount=5") != std::string::npos &&
              vs[0].right.find("amount=500") != std::string::npos,
          "damage(5) and damage(500) are not the same result, and the arguments are named");
}

// A kept answer is kept on a version of the data; what is queued is part of
// that data, arguments and all, not only how much of it there is.
void test_cache_sees_what_is_queued() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("echo");
    s.add_element("x", "n").params.set("v", 0.0);
    g.set_initial("echo");
    s.loop("copy", "x", "copy", [](sg::State& st, sg::Element& x, sg::Element*, const sg::Event&) {
        const auto& q = st.bus().queued();
        x.params.set("v", q.empty() ? 0.0 : q.front().args.num("n"));
    });
    s.loop("one", "x", "one", [](sg::State&, sg::Element& x, sg::Element*, const sg::Event&) {
        x.params.set("v", 1.0);
    });
    s.emit(sg::Event{"hit", sg::Params{}.set("n", 1.0)});

    sg::Diagram d("copy what is queued");
    d.commutes(sg::Path("echo", "x").arrow("copy"), sg::Path("echo", "x").arrow("one"));
    sg::LawCache cache;
    const uint64_t before = s.content_version();
    check(sg::verify(g, cache, {d}).ok(), "with hit(1) queued, copying it is setting one");

    s.bus().requeue({sg::Event{"hit", sg::Params{}.set("n", 100.0)}});
    check(s.content_version() != before, "another event queued is another version, however many there are");
    check(!sg::verify(g, {d}).ok(), "with hit(100) queued it is not");
    check(!sg::verify(g, cache, {d}).ok(), "and the cache does not answer from hit(1)");
}

// --- time is a state ----------------------------------------------------------------------
// A pond whose ripple spreads at two metres a second, driven by a clock.
sg::StateGraph& make_pond(sg::StateGraph& g, bool additive) {
    auto& clock = g.add<sg::Temporal>("clock");
    auto& pond = g.add<sg::State>("pond");
    pond.add_element("ripple", "ripple").params.set("r", 0.0);
    pond.loop("spread", "ripple", "tick", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        e.params.set("r", e.params.num("r") + 2.0 * ev.args.num("dt"));
    });
    sg::drive(g, clock, "pond", "tick", additive);
    g.set_initial("pond");
    return g;
}

void test_time_is_a_state() {
    sg::StateGraph g;
    make_pond(g, true);
    const sg::LawReport r = sg::verify(g);
    show(r.violations);
    check(r.structure.empty() && r.holds(), "a pond driven by a clock is reachable, clock and all, and lawful");

    sg::Engine e(g);
    e.run_fixed(0.25, 4);
    const auto& clock = static_cast<const sg::Temporal&>(g.state("clock"));
    check(near(num(g.state("pond"), "ripple", "r"), 2.0) && near(clock.time("pond"), 1.0) && clock.frame("pond") == 4,
          "the clock moves by its own arrow, and the pond by the drive: 2 m in a second");
    g.restore_default("clock");
    check(clock.time("pond") == 0.0 && clock.frame("pond") == 0, "time goes back to its start like any state");

    // A clock that drives nothing is reached by nothing.
    sg::StateGraph lone;
    lone.add<sg::Temporal>("clock");
    lone.add<sg::State>("room").add_element("x", "n");
    lone.set_initial("room");
    bool unreachable = false;
    for (const auto& p : lone.validate()) unreachable = unreachable || p.find("clock unreachable") != std::string::npos;
    check(unreachable, "a clock nothing is driven by is refused like any unlinked state");

    sg::StateGraph wrong;
    make_pond(wrong, false);
    wrong.drive(sg::Drive{"idle", "clock", "pond", "nothing", false, "idle"});
    static_cast<sg::Temporal&>(wrong.state("clock")).timeline("idle");
    bool named = false;
    for (const auto& p : wrong.validate()) named = named || p.find("no arrow of pond is fired by nothing") != std::string::npos;
    check(named, "a drive that moves no arrow is named");

    // Two drives on one line name each other, the first named by the second;
    // and what is kept between validations answers as one made fresh, also
    // when an arrow arrives that the drive was waiting for.
    wrong.drive(sg::Drive{"twin", "clock", "pond", "nothing", false, "idle"});
    const auto said = wrong.validate(false);
    const auto kept = wrong.validate(true);
    const auto kept_again = wrong.validate(true);
    check(said == kept && said == kept_again, "validation kept between calls says what a fresh one says");
    bool idle_names_twin = false, twin_names_idle = false;
    for (const auto& p : said) {
        idle_names_twin = idle_names_twin || p == "drive idle: timeline idle of clock also keeps time for drive twin - a line keeps one state's time; give each its own";
        twin_names_idle = twin_names_idle || p == "drive twin: timeline idle of clock also keeps time for drive idle - a line keeps one state's time; give each its own";
    }
    check(idle_names_twin && twin_names_idle, "two drives on one line name each other");
    wrong.state("pond").loop("hears", "ripple", "nothing", [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {});
    bool still = false;
    for (const auto& p : wrong.validate(true)) still = still || p.find("no arrow of pond is fired by nothing") != std::string::npos;
    check(!still && wrong.validate(true) == wrong.validate(false), "an arrow added later answers the drive that waited for it, kept or fresh");
}

void test_time_acts_as_time() {
    // Interest compounded per step: two half steps are not one whole step.
    sg::StateGraph g;
    make_pond(g, true);
    auto& pond = g.state("pond");
    pond.add_element("money", "account").params.set("v", 100.0);
    pond.loop("interest", "money", "tick", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        e.params.set("v", e.params.num("v") * (1.0 + ev.args.num("dt")));
    });
    const auto vs = sg::laws::drives(g);
    show(vs);
    check(vs.size() == 1 && vs[0].law == "drive" && vs[0].element == sg::Key{"money"},
          "a step that compounds is caught claiming step(a) ; step(b) == step(a + b)");
    g.drop_drive("clock>pond");
    sg::drive(g, static_cast<sg::Temporal&>(g.state("clock")), "pond", "tick", false);
    check(sg::laws::drives(g).empty(), "and is lawful when it does not claim it: time as steps");

    // A step that moves without time.
    pond.loop("age", "money", "tick", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event&) {
        e.params.set("ticks", e.params.num("ticks") + 1.0);
    });
    const auto zero = sg::laws::drives(g);
    show(zero);
    check(zero.size() == 1 && zero[0].key == "ticks", "step(0) that changes something is not the identity");
}

// A clock keeps one state's time, moved only when that state steps: set the
// state aside and come back, and its time and its motion still agree.
void test_time_is_kept_by_its_state() {
    sg::StateGraph g;
    make_pond(g, true);
    auto& pond = g.state("pond");
    pond.loop("note", "ripple", "tick", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        if (ev.args.num("dt") == 0.0) return;  // no time, no change
        e.params.set("seen", ev.args.num("time"));
        e.params.set("has_frame", ev.args.has("frame"));
        e.params.set("from", ev.source.str());
    });
    g.add<sg::State>("menu").add_element("m", "n");
    g.push("pond", "pause", "menu");
    g.pop("menu", "back");
    check(sg::verify(g).holds(), "a pond with a menu over it is lawful");

    sg::Engine e(g);
    e.run_fixed(0.25, 2);  // pond steps twice
    e.fire("pause");
    e.run_fixed(0.25, 3);  // the menu is on top: the pond is idle
    e.fire("back");
    e.run_fixed(0.25, 2);  // the first of these is taken by the pop, then the pond steps
    const auto& clock = static_cast<const sg::Temporal&>(g.state("clock"));
    const auto& r = pond.element("ripple").params;
    check(near(clock.time("pond"), 0.25 * clock.frame("pond")) && near(r.num("r"), 2.0 * clock.time("pond")) &&
              near(r.num("seen"), clock.time("pond")),
          "the clock moved only with the pond: time, steps and motion agree after a pause");
    check(!r.get_or<bool>("has_frame", true) && r.get_or<std::string>("from", "") == "clock",
          "an additive drive hands on no frame to count, and the event comes from the clock");

    // One clock keeps many states' time, each on a line of its own - and a
    // line kept for two is refused.
    g.add<sg::State>("brook").add_element("b", "n");
    g.state("brook").loop("flow", "b", "tick", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        e.params.set("t", e.params.num("t") + ev.args.num("dt"));
    });
    sg::drive(g, static_cast<sg::Temporal&>(g.state("clock")), "brook", "tick");
    g.embed("pond", "ripple", "brook");
    check(g.validate().empty(), "a second state keeps its time on its own line of the same clock");
    g.drive(sg::Drive{"stolen", "clock", "brook", "tick", false, "pond"});
    bool refused = false;
    for (const auto& p : g.validate()) refused = refused || p.find("a line keeps one state's time") != std::string::npos;
    check(refused, "a line shared by two drives is refused");
}

// What keeps its time always goes on in a room you stepped out of - stepped
// once a frame, never twice, and heard where it is shown Live.
void test_time_kept_always() {
    sg::StateGraph g;
    make_pond(g, true);
    auto& clock = static_cast<sg::Temporal&>(g.state("clock"));
    auto& brook = g.add<sg::State>("brook");
    brook.add_element("b", "n").params.set("t", 0.0);
    brook.loop("flow", "b", "flow", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        e.params.set("t", e.params.num("t") + ev.args.num("dt"));
    });
    sg::drive(g, clock, "brook", "flow", true, sg::Keeps::Always);
    g.add_functor("brook.out", "brook", "pond").on_object("b", "ripple", [](const sg::Element& s, sg::Element& d) {
        d.params.set("brook_t", s.params.num("t"));
    });
    g.embed("brook", "pond", "ripple", "brook", sg::Key{}, "brook.out", sg::EmbedSync::Live);
    g.add<sg::State>("menu").add_element("m", "n");
    g.push("pond", "pause", "menu");
    g.pop("menu", "back");
    check(sg::verify(g).holds(), "a brook that always flows is lawful");

    sg::Engine e(g);
    e.start();
    e.open_embed("brook");
    e.run_fixed(0.25, 2);
    const auto& b = brook.element("b").params;
    check(near(b.num("t"), 0.5) && near(clock.time("brook"), 0.5),
          "shown in the pond, it steps once a frame - as a guest, not again");
    e.fire("pause");
    e.run_fixed(0.25, 3);  // the menu is on top
    check(near(b.num("t"), 1.25) && near(clock.time("brook"), 1.25) && near(clock.time("pond"), 0.5),
          "with the pond set aside the brook still flows, and the pond's time waits");
    check(near(g.state("pond").element("ripple").params.num("brook_t"), 1.25),
          "and the pond hears of it through the Live embedding, as if it had stepped there");
}

// on_update that writes, rather than emits, is behaviour outside the arrows.
struct Sneaky : sg::State {
    using sg::State::State;
    void on_update(const sg::Tick& t) override { element("x").params.set("v", element("x").params.num("v") + t.dt); }
};
struct Honest : sg::State {
    using sg::State::State;
    void on_update(const sg::Tick& t) override { emit(sg::Event{"tick", sg::Params{}.set("dt", t.dt)}); }
};

void test_updates_are_watched() {
    for (bool sneaky : {true, false}) {
        sg::StateGraph g;
        sg::State& s = sneaky ? static_cast<sg::State&>(g.add<Sneaky>("s")) : g.add<Honest>("s");
        s.add_element("x", "n").params.set("v", 0.0);
        s.loop("move", "x", "tick", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
            e.params.set("v", e.params.num("v") + ev.args.num("dt"));
        });
        g.set_initial("s");
        sg::Engine e(g);
        std::vector<std::string> heard;
        e.on_problem = [&](const std::string& p) { heard.push_back(p); };
        e.set_watch_updates(true);
        e.run_fixed(0.5, 3);
        const bool said = heard.size() == 1 && heard[0].find("on_update") != std::string::npos;
        check(sneaky ? said : heard.empty(),
              sneaky ? "an on_update that writes the state's data is reported, once"
                     : "one that only emits for an arrow is not");
    }
}


// --- what a state says, and nothing else ----------------------------------------------------------
// A state declares what it says outward (State::says); the engine hands that
// to the graph's transitions, and nothing else carries control between states.
void test_what_a_state_says_moves_the_graph() {
    sg::StateGraph g;
    auto& battle = g.add<sg::State>("battle");
    battle.add_element("foe", "foe").params.set("hp", int64_t{8});
    battle.says("victory");
    battle.loop("hit", "foe", "attack", [](sg::State& s, sg::Element& e, sg::Element*, const sg::Event&) {
        e.params.set("hp", e.params.num("hp") - 8);
        if (e.params.num("hp") <= 0) s.emit("victory");
    });
    g.add<sg::State>("town").add_element("gate", "gate");
    g.connect("battle", "victory", "town");
    g.set_initial("battle");
    sg::Engine e(g);
    e.start();
    e.fire("attack");
    e.run_fixed(0.1, 3);
    check(e.current() && e.current()->id() == sg::Key{"town"},
          "a state that says victory takes the transition on it, with no listener in between");

    // Something it does not say stays its own.
    sg::StateGraph h;
    auto& quiet = h.add<sg::State>("battle");
    quiet.add_element("foe", "foe");
    quiet.loop("hit", "foe", "attack", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit("victory"); });
    h.add<sg::State>("town").add_element("gate", "gate");
    h.connect("battle", "victory", "town");
    h.set_initial("battle");
    sg::Engine q(h);
    q.start();
    q.fire("attack");
    q.run_fixed(0.1, 3);
    check(q.current() && q.current()->id() == sg::Key{"battle"}, "what a state does not say does not leave it");

    // What an arrow says is part of what it does, as the laws see it.
    sg::StateGraph l;
    auto& a = l.add<sg::State>("a");
    a.add_element("x", "n");
    a.says("done");
    a.loop("f", "x", "go", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit("done"); });
    a.loop("g", "x", "go", [](sg::State&, sg::Element&, sg::Element*, const sg::Event&) {});
    sg::Diagram d("says");
    d.commutes(sg::Path("a", "x").arrow("f"), sg::Path("a", "x").arrow("g"));
    check(!sg::laws::diagram(l, d).empty(), "an arrow that says something is not one that says nothing");
    check(a.said_out().empty(), "and a trial says nothing to the world");
}

// A listener on a bus is outside the graph: a law's trial never runs it.
void test_trials_do_not_reach_listeners() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("s");
    s.add_element("x", "n");
    s.loop("f", "x", "go", [](sg::State& st, sg::Element&, sg::Element*, const sg::Event&) { st.emit("rang"); });
    int rang = 0;
    s.bus().subscribe("rang", [&rang](const sg::Event&) { ++rang; });
    sg::Diagram d("bell");
    d.commutes(sg::Path("s").event("go"), sg::Path("s").event("go"));
    sg::laws::diagram(g, d);
    check(rang == 0, "checking a law rings no bell outside the graph");
    s.emit("rang");
    s.dispatch_pending();
    check(rang == 1, "outside a trial, the listener hears");
}

// --- the callbacks, watched ---------------------------------------------------------------------
struct Sly : sg::State {
    using sg::State::State;
    bool on_event(const sg::Event& e) override {
        if (e.name == sg::Key{"hurt"}) {
            params().set("hp", int64_t{0});
            return true;
        }
        return false;
    }
};
struct Painter : sg::State {
    using sg::State::State;
    void on_render(const sg::Tick& t) override { params().set("drawn", t.time); }
};
struct Greeter : sg::State {
    using sg::State::State;
    void on_enter(const sg::Params&) override { params().set("hello", true); }
};

void test_every_hook_is_watched() {
    const auto heard_of = [](sg::StateGraph& g, const char* hook, bool fire) {
        g.set_initial("s");
        sg::Engine e(g);
        std::vector<std::string> heard;
        e.on_problem = [&](const std::string& p) { heard.push_back(p); };
        e.set_watch_updates(true);
        e.start();
        if (fire) e.fire("hurt");
        e.run_fixed(0.5, 2);
        return heard.size() == 1 && heard[0].find(hook) != std::string::npos;
    };
    {
        sg::StateGraph g;
        g.add<Sly>("s").add_element("x", "n");
        check(heard_of(g, "on_event", true), "an on_event that writes, bypassing every arrow, is reported");
    }
    {
        sg::StateGraph g;
        g.add<Painter>("s").add_element("x", "n");
        check(heard_of(g, "on_render", false), "so is an on_render that writes");
    }
    {
        sg::StateGraph g;
        g.add<Greeter>("s").add_element("x", "n");
        check(heard_of(g, "on_enter", false), "and an on_enter");
    }
}

// --- names are identities -------------------------------------------------------------------------
void test_names_are_identities() {
    sg::StateGraph g;
    auto& s = g.add<sg::State>("s");
    s.add_element("x", "n");
    s.loop("f", "x", "go", nullptr);
    bool threw = false;
    try {
        s.loop("f", "x", "stop", nullptr);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw && s.morphisms().size() == 1, "two arrows cannot share a name");

    g.add<sg::State>("t").add_element("y", "n");
    const sg::Key first = g.connect("s", "go", "t").name;
    sg::Transition alt;
    alt.from = "s";
    alt.trigger = "go";
    alt.to = "t";
    alt.guard = [](const sg::State&, const sg::Event&) { return false; };
    const sg::Key second = g.connect(alt).name;
    check(first != second && g.transition(second)->guard, "two guarded alternatives get names of their own");
    threw = false;
    try {
        sg::Transition dup;
        dup.name = first;
        dup.from = "t";
        dup.trigger = "back";
        dup.to = "s";
        g.connect(dup);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a name given twice is refused");

    g.add_functor("F", "s", "t").on_object("x", "y");
    threw = false;
    try {
        g.functor(sg::Key{"F"})->rename("G");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw && g.functor(sg::Key{"F"})->name() == sg::Key{"F"}, "a functor the graph holds keeps its name");
    const uint64_t rev = g.revision();
    threw = false;
    try {
        *g.functor(sg::Key{"F"}) = sg::Functor("G", "s", "t");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "nor is it put in place under another name");
    *g.functor(sg::Key{"F"}) = sg::Functor("F", "s", "t");
    check(g.revision() != rev, "and put in place under its own, the graph counts it");
}

// --- composition keeps what an identity leaves -------------------------------------------------
void test_composition_keeps_unmapped_events() {
    sg::Functor f("F", "a", "b");  // F(x) = x: not in its table
    sg::Functor g("G", "b", "c");
    g.on_event("x", "y");
    const sg::Functor h = sg::Functor::compose(f, g);
    check(h.image_event("x") == sg::Key{"y"}, "(G . F)(x) = G(F(x)) = G(x) = y");
    sg::Functor id = sg::Functor::identity(sg::Key{"b"}, sg::Key{"I"});
    id.on_event("p", "q");
    sg::Functor k("K", "b", "c");
    k.on_event("q", "r");
    check(sg::Functor::compose(id, k).image_event("p") == sg::Key{"r"}, "an identity's own events are kept too");
}

// --- a transition means one thing -----------------------------------------------------------------
void test_a_transition_in_a_law_is_the_one_the_engine_takes() {
    sg::StateGraph g;
    auto& shop = g.add<sg::State>("shop");
    shop.add_element("shelf", "shelf").params.set("stock", int64_t{3});
    g.add<sg::State>("ledger").add_element("book", "entry");
    g.add<sg::State>("closed").add_element("sign", "sign");
    g.add_functor("post", "shop", "ledger").on_object("shelf", "book", sg::transport::only({"stock"}));
    sg::Transition t;
    t.name = "count";
    t.from = "shop";
    t.trigger = "close";
    t.to = "ledger";
    t.functor = "post";
    t.guard = [](const sg::State& s, const sg::Event&) { return s.find("shelf")->params.num("stock") > 0; };
    t.action = [](sg::State& s, const sg::Event&, sg::Params&) { s.params().set("counted", true); };
    g.connect(t);
    sg::Transition shut;
    shut.name = "shut";
    shut.from = "shop";
    shut.trigger = "close";
    shut.to = "closed";
    g.connect(shut);
    g.push("ledger", "peek", "shop");

    // The action runs in a law, as it does in the engine.
    sg::Diagram d("count");
    d.commutes(sg::Path("shop").transition("count"), sg::Path("shop").transition("count"));
    check(sg::laws::diagram(g, d).empty(), "a transition is a path");
    const sg::laws::Outcome o = sg::laws::run(g, sg::Path("shop").transition("count").transition("ledger-peek->shop"), {});
    check(o.error.empty() && o.data.params.get_or<bool>("counted", false), "its action runs in a law, as in the engine");

    // With no stock the guard refuses it, and the engine would take `shut`.
    shop.find("shelf")->params.set("stock", int64_t{0});
    const sg::laws::Outcome r = sg::laws::run(g, sg::Path("shop").transition("count"), {});
    check(!r.error.empty() && r.error.find("shut") != std::string::npos,
          "a transition its guard refuses is not a step: the engine would take another");

    // A pop goes back where the path came from.
    shop.find("shelf")->params.set("stock", int64_t{3});
    g.pop("shop", "back");
    const sg::laws::Outcome p =
        sg::laws::run(g, sg::Path("ledger").transition("ledger-peek->shop").transition("shop-back->"), {});
    check(p.error.empty() && p.state == sg::Key{"ledger"}, "a pop after a push in the same path goes back");
}


// --- listeners observe; only the world changes the world -------------------------------------------
void test_listeners_only_observe() {
    sg::StateGraph g;
    auto& a = g.add<sg::State>("a");
    a.add_element("x", "n");
    a.says("rang");
    a.loop("ring", "x", "go", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit("rang"); });
    auto& b = g.add<sg::State>("b");
    b.add_element("y", "n").params.set("heard", 0.0);
    b.loop("hear", "y", "rung", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event&) {
        e.params.set("heard", e.params.num("heard") + 1);
    });
    a.add_element("port", sg::kinds::portal);
    g.add_functor("a.to.b", "a", "b").on_event("rang", "rung");
    g.set_focus(g.embed("bell", "a", "port", "b", "a.to.b", sg::Key{}, sg::EmbedSync::Commit).name, false);
    g.set_initial("a");
    sg::Engine e(g);
    e.start();
    e.open_embed("bell");

    // An observer: reads, and keeps what it saw outside the world.
    int seen = 0;
    a.bus().subscribe("rang", [&seen](const sg::Event&) { ++seen; });
    e.fire("go");
    e.run_fixed(0.1, 2);
    check(seen == 1, "a listener that only looks is let be");
    check(b.find("y")->params.num("heard") == 1.0,
          "what a says reaches b across the embedding whose functor names it - declared, not forwarded");

    // A cause: refused.
    bool refused = false;
    a.bus().subscribe("rang", [&e](const sg::Event&) { e.fire("go"); });
    try {
        e.fire("go");
        e.run_fixed(0.1, 2);
    } catch (const sg::ObserverError&) {
        refused = true;
    }
    check(refused, "a listener that fires the engine is refused");

    sg::StateGraph h;
    auto& c = h.add<sg::State>("c");
    c.add_element("x", "n");
    auto& d = h.add<sg::State>("d");
    d.add_element("y", "n");
    c.bus().subscribe("poke", [&d](const sg::Event& ev) { d.emit(ev); });
    refused = false;
    try {
        c.emit("poke");
        c.dispatch_pending();
    } catch (const sg::ObserverError&) {
        refused = true;
    }
    check(refused && d.bus().queued().empty(), "one that sends another state an event is refused");

    // Report, for a project moving its listeners into the graph: said once, let through.
    std::vector<std::string> heard;
    sg::set_observers(sg::Observers::Report, [&heard](const std::string& p) { heard.push_back(p); });
    c.emit("poke");
    c.dispatch_pending();
    c.emit("poke");
    c.dispatch_pending();
    sg::set_observers(sg::Observers::Strict);
    check(heard.size() == 1 && d.bus().queued().size() == 2, "under Report it is said once, and let through");
}


// --- the world rewrites itself only by a declared edit ------------------------------------------------
void test_edits_are_declared() {
    sg::StateGraph g;
    auto& ed = g.add<sg::State>("editor");
    ed.add_element("line", "text");
    ed.says("cmd");
    ed.loop("enter", "line", "type", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event& ev) {
        s.emit(sg::Event{"cmd", ev.args});
    });
    ed.loop("answered", "line", "cmd.done", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        e.params.set("answer", ev.args.get_or<std::string>("text", ""));
    });
    int applied = 0;
    g.edit("editor", "cmd", [&applied](sg::StateGraph& graph, const sg::Event& asked) {
        ++applied;
        const std::string name = asked.args.get_or<std::string>("name", "");
        graph.add<sg::State>(sg::Key{name}).add_element("floor", "floor");
        graph.push("editor", sg::Key{"visit." + name}, sg::Key{name});
        return sg::Params{}.set("text", std::string("made ") + name);
    });
    g.set_initial("editor");
    check(g.validate().empty(), "an edit asked for by what the editor says is a lawful interface");

    // Checking the laws runs the arrow that asks, and applies nothing.
    sg::Diagram d("ask");
    d.commutes(sg::Path("editor").event("type", sg::Params{}.set("name", std::string("den"))),
               sg::Path("editor").event("type", sg::Params{}.set("name", std::string("den"))));
    sg::laws::diagram(g, d);
    check(applied == 0 && !g.find("den"), "a law's trial asks for nothing");

    sg::Engine e(g);
    e.start();
    e.fire(sg::Event{"type", sg::Params{}.set("name", std::string("den"))});
    e.run_fixed(0.1, 3);
    check(applied == 1 && g.find("den") && g.transition("editor-visit.den->den"),
          "said, the edit is applied once, at the start of the next frame, by the engine");
    check(ed.find("line")->params.get_or<std::string>("answer", "") == "made den",
          "and its answer is heard back by the editor's own arrow");

    sg::StateGraph h;
    h.add<sg::State>("mute").add_element("x", "n");
    h.edit("mute", "cmd", [](sg::StateGraph&, const sg::Event&) { return sg::Params{}; });
    bool named = false;
    for (const auto& p : h.validate()) named = named || p.find("does not say cmd") != std::string::npos;
    check(named, "an edit no state asks for is refused");
}


// A functor maps events too: what a state says crosses each functor out of it that names it.
void test_functors_carry_what_is_said() {
    sg::StateGraph g;
    auto& desk = g.add<sg::State>("desk");
    desk.add_element("pen", "pen");
    desk.says("chalk");
    desk.loop("write", "pen", "press", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) { s.emit("chalk"); });
    auto& board = g.add<sg::State>("board");
    board.add_element("slate", "slate").params.set("lines", 0.0);
    board.loop("write", "slate", "write", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event&) {
        e.params.set("lines", e.params.num("lines") + 1);
    });
    g.add_functor("desk.board", "desk", "board").on_event("chalk", "write");
    g.set_initial("desk");
    sg::Engine e(g);
    e.start();
    e.fire("press");
    e.run_fixed(0.1, 1);
    check(board.find("slate")->params.num("lines") == 1.0,
          "the board, stepped by no one, writes at once what the desk says - carried by the functor between them");
}


// Two functors declared a lens on their own - a window onto a board - are held to the lens laws.
void test_a_declared_lens_is_checked() {
    for (bool honest : {true, false}) {
        sg::StateGraph g;
        g.add<sg::State>("board").add_element("ink", "ink").params.set("n", 3.0);
        g.add<sg::State>("window").add_element("copy", "copy").params.set("n", 3.0);
        g.add_functor("get", "board", "window").on_object("ink", "copy", sg::transport::only({"n"}));
        g.add_functor("put", "window", "board").on_object("copy", "ink", [honest](const sg::Element& s, sg::Element& d) {
            d.params.set("n", s.params.num("n") * (honest ? 1.0 : 2.0));
        });
        g.lens("get", "put");
        bool named = false;
        const auto vs = sg::laws::lenses(g);
        for (const auto& v : vs) named = named || v.where.find("lens get/put") != std::string::npos;
        check(honest ? vs.empty() : named, honest ? "a declared lens that keeps the laws passes"
                                                  : "one whose put doubles is named, with no embedding in sight");
    }
}


// The world outside reaches a state only through a port the graph declares.
void test_ports_are_declared() {
    sg::StateGraph g;
    auto& gauge = g.add<sg::State>("gauge");
    gauge.add_element("needle", "needle").params.set("v", 0.0);
    gauge.loop("read", "needle", "reading", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        e.params.set("v", ev.args.num("v"));
    });
    g.set_initial("gauge");
    sg::Engine e(g);
    e.start();
    bool refused = false;
    try {
        e.send("gauge", sg::Event{"reading", sg::Params{}.set("v", 3.0)});
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "nothing comes in through a port the graph does not declare");
    g.port("gauge", "reading");
    e.send("gauge", sg::Event{"reading", sg::Params{}.set("v", 3.0)});
    e.run_fixed(0.1, 1);
    check(gauge.find("needle")->params.num("v") == 3.0, "through a declared one, it reaches the state's arrow");
}


// A game on a set runs while it is shown; one on a computer only while it is played; a host opens its own portal.
void test_shown_focused_and_portals() {
    sg::StateGraph g;
    auto& room = g.add<sg::State>("room");
    room.add_element("desk", "desk");
    room.add_element("set", sg::kinds::portal);
    room.add_element("pc_port", sg::kinds::portal);
    auto& pc = g.add<sg::State>("pc");
    pc.add_element("win", sg::kinds::portal);
    pc.says("portal.open");
    pc.says("portal.focus");
    pc.loop("launch", "win", "launch", [](sg::State& s, sg::Element&, sg::Element*, const sg::Event&) {
        s.emit(sg::Event{"portal.open", sg::Params{}.set("portal", std::string("win"))});
        s.emit(sg::Event{"portal.focus", sg::Params{}.set("portal", std::string("win")).set("on", true)});
    });
    const auto game = [&](const char* id) -> sg::State& {
        auto& s = g.add<sg::State>(id);
        s.add_element("hero", "hero").params.set("t", 0.0);
        s.loop("run", "hero", "tick", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
            e.params.set("t", e.params.num("t") + ev.args.num("dt"));
        });
        return s;
    };
    auto& on_set = game("on_set");
    auto& on_pc = game("on_pc");
    auto& clock = g.add<sg::Temporal>("clock");
    sg::drive(g, clock, on_set.id(), "tick", false, sg::Keeps::WhileShown);
    sg::drive(g, clock, on_pc.id(), "tick", false, sg::Keeps::WhileFocused);
    g.set_focus(g.embed("set", "room", "set", "on_set", sg::Key{}, sg::Key{}, sg::EmbedSync::Commit).name, false);
    g.set_focus(g.embed("pc", "room", "pc_port", "pc", sg::Key{}, sg::Key{}, sg::EmbedSync::Commit).name, false);
    g.set_focus(g.embed("app", "pc", "win", "on_pc", sg::Key{}, sg::Key{}, sg::EmbedSync::Commit).name, false);
    g.set_initial("room");
    sg::Engine e(g);
    e.start();
    e.run_fixed(0.5, 2);
    check(on_set.find("hero")->params.num("t") == 0.0, "a game on a set that is not on keeps no time");
    e.open_embed("set");
    e.run_fixed(0.5, 2);
    check(on_set.find("hero")->params.num("t") == 1.0, "shown, it keeps its time");

    e.open_embed("pc");
    pc.emit("launch");
    pc.dispatch_pending();
    e.run_fixed(0.5, 1);  // said now, opened and focused at the start of the next frame
    e.run_fixed(0.5, 2);
    check(e.embed_open("app") && e.focused() == &on_pc, "the computer opens and focuses its own window by saying so");
    check(on_pc.find("hero")->params.num("t") > 0.0, "played, the game on it keeps its time");
    const double t = on_pc.find("hero")->params.num("t");
    e.close_embed("pc");
    e.run_fixed(0.5, 2);
    check(e.focused() == nullptr && on_pc.find("hero")->params.num("t") == t,
          "with nobody at the computer, its game has no input and waits");
}

// How deep a state is open is counted, not guessed at: a chain of any length
// is followed to its end, and embeddings that go round come to an end too.
void test_depth_has_no_limit_and_cycles_end() {
    sg::StateGraph g;
    const int n = 12;
    for (int i = 0; i <= n; ++i) g.add<sg::State>("s" + std::to_string(i)).add_element("in", sg::kinds::portal);
    for (int i = 0; i < n; ++i)
        g.set_focus(g.embed(sg::Key{"e" + std::to_string(i)}, sg::Key{"s" + std::to_string(i)}, "in",
                            sg::Key{"s" + std::to_string(i + 1)}, sg::Key{}, sg::Key{}, sg::EmbedSync::Commit)
                        .name,
                    false);
    // And the last holds the first again: round and round.
    g.set_focus(g.embed("back", sg::Key{"s" + std::to_string(n)}, "in", "s0", sg::Key{}, sg::Key{}, sg::EmbedSync::Commit).name, false);
    g.set_initial("s0");
    sg::Engine e(g);
    e.start();
    for (int i = 0; i < n; ++i) e.open_embed(sg::Key{"e" + std::to_string(i)});
    check(e.depth(sg::Key{"s" + std::to_string(n)}) == n && e.live(sg::Key{"s" + std::to_string(n)}),
          "a state twelve embeddings deep is live, and twelve deep");
    e.open_embed("back");
    check(e.depth("s0") == 0 && e.depth("s5") == 5, "an embedding that goes round changes no one's depth, and the count ends");
    e.close_embed("e3", false);
    check(!e.live("s4") && e.depth("s4") == -1, "and one closed on the way cuts off all beyond it, round or not");
    e.tick(1.0 / 60.0);
    check(true, "and a frame of it ends");
}

// An embedding that follows its portal is open exactly while the portal says:
// the host's own arrow decides, the engine only does it.
void test_an_embedding_follows_its_portal() {
    sg::StateGraph g;
    auto& set = g.add<sg::State>("set");
    set.add_element("glass", sg::kinds::portal);
    set.loop("power", "glass", "power", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
        e.params.set(sg::keys::open, ev.args.get_or<bool>("on", false));
    });
    g.add<sg::State>("show").add_element("picture", "picture");
    g.set_follows(g.set_focus(g.embed("on", "set", "glass", "show", sg::Key{}, sg::Key{}, sg::EmbedSync::Commit).name, false), true);
    g.set_initial("set");
    check(sg::verify(g).ok(), "a set that shows while it is on keeps every law");
    sg::Engine e(g);
    e.start();
    check(!e.embed_open("on"), "off, it shows nothing");
    set.emit(sg::Event{"power", sg::Params{}.set("on", true)});
    e.tick(1.0 / 60.0);
    check(e.embed_open("on"), "switched on by its own arrow, it shows - by the end of the frame");
    set.emit(sg::Event{"power", sg::Params{}.set("on", false)});
    e.tick(1.0 / 60.0);
    check(!e.embed_open("on"), "and off again, not");
}

// Arriving is an event of the state arrived at: `state.entered {from, by}`,
// for its own arrows - the same in the engine and in a law's path.
void test_arriving_is_heard() {
    sg::StateGraph g;
    g.add<sg::State>("hall").add_element("door", "door");
    auto& annex = g.add<sg::State>("annex");
    annex.add_element("mat", "mat").params.set("from", std::string());
    annex.loop("arrive", "mat", sg::StateGraph::entered_event(), [](sg::State&, sg::Element& m, sg::Element*, const sg::Event& ev) {
        m.params.set("from", ev.args.get_or<std::string>("from", ""));
    });
    g.connect("hall", "go", "annex");
    g.set_initial("hall");
    sg::Engine e(g);
    e.start();
    e.fire("go");
    e.tick(1.0 / 60.0);
    check(e.current()->id() == sg::Key{"annex"} && annex.element("mat").params.get_or<std::string>("from", "") == "hall",
          "the annex hears it was entered, and from the hall");
    check(sg::verify(g).ok(), "and the laws take the transition the same way");
    annex.element("mat").params.set("from", std::string());
    e.switch_to("hall");
    e.switch_to("annex");
    e.tick(1.0 / 60.0);
    check(annex.element("mat").params.get_or<std::string>("from", "") == "hall", "and entered by hand, it hears it all the same");
}

// A kept functor: its target follows its source whenever it changes, with no
// portal between them - and nothing is carried when nothing changed.
void test_a_kept_functor_follows() {
    sg::StateGraph g;
    auto& room = g.add<sg::State>("room");
    room.add_element("desk", "desk").params.set("x", 1.0);
    auto& model = g.add<sg::State>("model");
    model.add_element("block", "block").params.set("x", 0.0);
    g.add_functor("model.in", "room", "model").on_object("desk", "block", sg::transport::copy_all);
    g.keep("model.in");
    g.set_initial("room");
    check(g.validate().empty(), "a kept functor reaches what it keeps, and is sound");
    sg::Engine e(g);
    e.start();
    e.tick(1.0 / 60.0);
    check(model.element("block").params.num("x") == 1.0, "the model follows the room");
    room.element("desk").params.set("x", 2.5);
    e.tick(1.0 / 60.0);
    check(model.element("block").params.num("x") == 2.5, "and follows it as it changes");
    const uint64_t was = model.element("block").params.stamp();
    e.tick(1.0 / 60.0);
    check(model.element("block").params.stamp() == was, "and nothing changed, nothing is carried");
    g.drop_functor("model.in");
    check(g.kept().empty() && !g.validate().empty(), "dropped, it is kept no more - and the model, reached by nothing now, is named");
}

// Many states, checked at once: each state's own arrows on a thread of its
// own. What is found is what one thread finds, in the same order - the
// broken laws, and those that cannot be checked - and every state is as it
// was after.
void test_many_states_are_checked_at_once() {
    sg::StateGraph g;
    const int kStates = 96, kItems = 48;
    for (int i = 0; i < kStates; ++i) {
        auto& s = g.add<sg::State>(sg::Key{"stall" + std::to_string(i)});
        for (int k = 0; k < kItems; ++k) s.add_element(sg::Key{"item" + std::to_string(k)}, "item").params.set("n", double(k));
        s.loop("count", "item0", "count", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
            e.params.set("n", e.params.num("n") + ev.args.num("by", 1.0));
        });
        s.arrow("pass", "item0", "item1", "pass", [](sg::State&, sg::Element& a, sg::Element* b, const sg::Event&) {
            b->params.set("n", a.params.num("n") * 2.0);
        });
        s.loop("again", "item1", "again", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event&) {
            e.params.set("n", e.params.num("n") - 3.0);
        });
        s.arrow("back", "item1", "item0", "back", [](sg::State&, sg::Element& a, sg::Element* b, const sg::Event&) {
            b->params.set("n", a.params.num("n") + 1.0);
        });
        // Every few stalls one that reads which event ran it: run as part of
        // a composite it hears the composite's, and associativity breaks.
        if (i % 7 == 0)
            s.loop("heard", "item0", "heard", [](sg::State&, sg::Element& e, sg::Element*, const sg::Event& ev) {
                e.params.set("heard", ev.name.str());
            });
        // And one that adds a thing: what the state is made of, which no
        // trial may change - so its laws cannot be checked, and are said to.
        if (i % 11 == 0)
            s.loop("grow", "item1", "grow", [](sg::State& st, sg::Element&, sg::Element*, const sg::Event&) {
                st.add_element(sg::Key{"sprout"}, "item");
            });
    }
    g.set_initial("stall0");
    for (int i = 1; i < kStates; ++i) g.connect("stall0", sg::Key{"go" + std::to_string(i)}, sg::Key{"stall" + std::to_string(i)});
    std::vector<uint64_t> before;
    for (sg::Key id : g.ids()) before.push_back(g.state(id).content_version());
    const uint64_t revision = g.revision();

    sg::LawOptions o;
    o.args.set("by", 2.0);
    o.max_triples = 32;
    const sg::LawReport many = sg::verify(g, {}, o);
    // One thread: a cache's checks are made in order, one after another.
    sg::LawCache one_by_one(sg::LawCache::Strategy::Direct);
    const sg::LawReport one = sg::verify(g, one_by_one, {}, o);
    check(!many.violations.empty() && !many.unchecked.empty(), "the broken laws are found, and the ones that cannot be checked are said");
    check(many.str() == one.str(), "checked at once, the report is the one one thread makes, in its order");
    std::vector<uint64_t> after;
    for (sg::Key id : g.ids()) after.push_back(g.state(id).content_version());
    check(before == after, "and every state is as it was");
    check(g.revision() == revision && !g.sealed(), "and the graph too: nothing rewritten, nothing left sealed");
}

// Names, stamps and shared params made on many threads at once: one name is
// one name on all of them, no two changes share a stamp, and params copied and
// let go everywhere keep their entries.
void test_threads_share_names_stamps_and_params() {
    const int kThreads = 8, kNames = 4000;
    std::vector<std::vector<const void*>> seen(kThreads);
    std::vector<std::vector<uint64_t>> stamps(kThreads);
    sg::Params shared;
    shared.set("kept", 42.0).set("text", std::string("a text long enough to be on the heap, not in the string"));
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&, t] {
            for (int i = 0; i < kNames; ++i) {
                // In another order on every thread, so they race for each name.
                const int n = (i * 7 + t * 997) % kNames;
                seen[t].push_back(nullptr);
                seen[t][static_cast<std::size_t>(i)] = sg::Key{"threaded." + std::to_string(n)}.handle();
                stamps[t].push_back(sg::next_stamp());
                sg::Params copy = shared;
                sg::Params other = copy;
                other.set("mine", double(i));
            }
        });
    for (auto& th : threads) th.join();
    bool same = true;
    for (int t = 0; t < kThreads; ++t)
        for (int i = 0; i < kNames; ++i) {
            const int n = (i * 7 + t * 997) % kNames;
            same = same && seen[t][static_cast<std::size_t>(i)] == sg::Key{"threaded." + std::to_string(n)}.handle();
        }
    check(same, "a name made on many threads at once is one name");
    std::vector<uint64_t> all;
    for (auto& v : stamps) all.insert(all.end(), v.begin(), v.end());
    std::sort(all.begin(), all.end());
    check(std::adjacent_find(all.begin(), all.end()) == all.end() && sg::last_stamp() >= all.back(), "no two changes share a stamp");
    check(shared.num("kept") == 42.0 && !shared.has("mine") && shared.size() == 2, "params copied and let go on every thread keep their entries");
}

}  // namespace

int main() {
    test_composites_have_the_right_type();
    test_identity_is_a_law_not_a_table();
    test_a_counterexample_is_concrete();
    test_functors_carry_the_action_not_just_the_arrow();
    test_composites_cannot_drift();
    test_lens_laws();
    test_checking_is_invisible();
    test_enforce_refuses_a_lie();
    test_transitions_in_paths();
    test_typed();
    test_events_are_compared_whole();
    test_events_are_compared_in_order();
    test_budgets_are_visible();
    test_time_is_a_state();
    test_time_acts_as_time();
    test_time_is_kept_by_its_state();
    test_time_kept_always();
    test_updates_are_watched();
    test_cache_sees_what_is_queued();
    test_what_a_state_says_moves_the_graph();
    test_trials_do_not_reach_listeners();
    test_every_hook_is_watched();
    test_names_are_identities();
    test_composition_keeps_unmapped_events();
    test_a_transition_in_a_law_is_the_one_the_engine_takes();
    test_listeners_only_observe();
    test_edits_are_declared();
    test_functors_carry_what_is_said();
    test_a_declared_lens_is_checked();
    test_ports_are_declared();
    test_shown_focused_and_portals();
    test_depth_has_no_limit_and_cycles_end();
    test_an_embedding_follows_its_portal();
    test_arriving_is_heard();
    test_a_kept_functor_follows();
    test_many_states_are_checked_at_once();
    test_threads_share_names_stamps_and_params();
    std::printf("\n%s\n", failures == 0 ? "all laws hold, and every broken one is named"
                                        : "FAILURES");
    return failures == 0 ? 0 : 1;
}
