// g * f where cod(f) != dom(g): sell ends at the till, sell starts at the shelf.
#include "tags.hpp"

int main() {
    sg::StateGraph g;
    auto& shop = shop_state(g);
    const auto sell = sg::typed::arrow<Shelf, Till>(shop, "sell", "sell", nullptr);
    const auto refund = sg::typed::arrow<Till, Shelf>(shop, "refund", "refund", nullptr);
#ifdef SG_EXPECT_FAIL
    const auto bad = sell * sell;
#else
    const auto bad = refund * sell;
#endif
    (void)bad;
    (void)refund;
}
