// Stategine - the laws, and proof that they bite.
//
// Every law is tested twice: once on something that keeps it, and once on
// something built to break it, where the test reads the counterexample back
// and checks it names the right element, parameter and values. A checker that
// only ever says "clean" proves nothing.
//
// The domain is deliberately not spatial - a shop and its ledger - since
// nothing here is allowed to know what a room is.
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
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
    g.add_functor("f", "a", "b").on_object("x", "y");  // y does not exist yet: f creates it
    g.connect("a", "go", "b");

    const auto clean = sg::laws::identity(g);
    show(clean);
    check(clean.empty(), "id ; F == F == F ; id, even when F creates what it maps to");

    // What the identity used to be: a copy of the object list, taken once.
    sg::Functor snapshot_id("snapshot_id", "b", "b");
    for (const auto& e : g.state("b").elements()) snapshot_id.on_object(e.id, e.id);
    sg::Diagram d("the old identity");
    d.commutes(sg::Path("a").functor(sg::Functor::compose(*g.functor("f"), snapshot_id)),
               sg::Path("a").functor("f"));
    const auto broken = sg::laws::diagram(g, d);
    show(broken);
    check(broken.size() == 1 && broken[0].state == sg::Key{"b"} &&
              broken[0].element == sg::Key{"y"} && broken[0].key == "<element>" &&
              broken[0].left == "absent" && broken[0].right == "present",
          "a snapshot identity is caught: the counterexample is b.y, absent on one side only");

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
    g.connect("shop", "close", "ledger").functor = "post";

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
    g.connect("shop", "close", "ledger").functor = "post_audited";

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
}

// --- transitions are arrows too ----------------------------------------------------------------
void test_transitions_in_paths() {
    sg::StateGraph g;
    make_shop(g);
    auto& ledger = g.add<sg::State>("ledger");
    ledger.add_element("book", "entry");
    g.add_functor("post", "shop", "ledger").on_object("shelf", "book", sg::transport::only({"stock"}));
    g.add_functor("reopen", "ledger", "shop").on_object("book", "shelf", sg::transport::only({"stock"}));
    g.connect("shop", "close", "ledger").functor = "post";
    g.connect("ledger", "open", "shop").functor = "reopen";
    g.pop("ledger", "back");

    sg::Diagram d("close and reopen");
    d.commutes(sg::Path("shop", "shelf").transition("shop-close->ledger").transition("ledger-open->shop"),
               sg::Path("shop", "shelf"));
    const auto vs = sg::laws::diagram(g, d);
    show(vs);
    check(vs.empty(), "closing and reopening the shop is the identity on the shelf");

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
    std::printf("\n%s\n", failures == 0 ? "all laws hold, and every broken one is named"
                                        : "FAILURES");
    return failures == 0 ? 0 : 1;
}
