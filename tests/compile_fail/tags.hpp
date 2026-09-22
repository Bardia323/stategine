// Tags shared by the compile-fail cases. Each case compiles cleanly as a
// control; with SG_EXPECT_FAIL defined it adds exactly one ill-typed line,
// and the test passes only if the compiler rejects it with Stategine's own
// message - not merely with some error.
#pragma once

#include "sg/core/Typed.hpp"

struct Shop {
    static constexpr const char* name = "shop";
};
struct Ledger {
    static constexpr const char* name = "ledger";
};
struct Shelf {
    using state = Shop;
    static constexpr const char* name = "shelf";
};
struct Till {
    using state = Shop;
    static constexpr const char* name = "till";
};
struct Counter {
    using state = Shop;
    static constexpr const char* name = "counter";
};
struct Book {
    using state = Ledger;
    static constexpr const char* name = "book";
};

inline sg::State& shop_state(sg::StateGraph& g) {
    auto& s = g.add<sg::State>("shop");
    s.add_element("shelf", "shelf");
    s.add_element("till", "till");
    s.add_element("counter", "portal");
    return s;
}
