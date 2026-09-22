// Registering G . F when F lands in the ledger and G starts in the shop.
#include "tags.hpp"

int main() {
    sg::StateGraph g;
    shop_state(g);
    g.add<sg::State>("ledger");
    const auto post = sg::typed::functor<Shop, Ledger>(g, "post");
    const auto reopen = sg::typed::functor<Ledger, Shop>(g, "reopen");
#ifdef SG_EXPECT_FAIL
    sg::typed::compose(g, "twice", post, post);
#else
    sg::typed::compose(g, "round", reopen, post);
#endif
}
