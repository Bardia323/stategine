// Claiming two arrows are equal when they do not share both ends.
#include "tags.hpp"

int main() {
    sg::StateGraph g;
    auto& shop = shop_state(g);
    const auto sell = sg::typed::arrow<Shelf, Till>(shop, "sell", "sell", nullptr);
    const auto refund = sg::typed::arrow<Till, Shelf>(shop, "refund", "refund", nullptr);
    sg::Diagram d("claims");
#ifdef SG_EXPECT_FAIL
    sg::typed::commutes(d, sell, refund);
#else
    sg::typed::commutes(d, refund * sell, sg::typed::id<Shelf>());
#endif
}
