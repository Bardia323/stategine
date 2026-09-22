// An arrow inside a state whose ends live in two different states.
#include "tags.hpp"

int main() {
    sg::StateGraph g;
    auto& shop = shop_state(g);
#ifdef SG_EXPECT_FAIL
    sg::typed::arrow<Shelf, Book>(shop, "misfile", "x", nullptr);
#else
    sg::typed::arrow<Shelf, Till>(shop, "sell", "x", nullptr);
#endif
}
