// A portal whose write-back does not run back along its view.
#include "tags.hpp"

int main() {
    sg::StateGraph g;
    shop_state(g);
    g.add<sg::State>("ledger");
    const auto post = sg::typed::functor<Shop, Ledger>(g, "post");
    const auto reopen = sg::typed::functor<Ledger, Shop>(g, "reopen");
#ifdef SG_EXPECT_FAIL
    sg::typed::embed<Counter>(g, "desk", post, post);
#else
    sg::typed::embed<Counter>(g, "desk", post, reopen);
#endif
}
