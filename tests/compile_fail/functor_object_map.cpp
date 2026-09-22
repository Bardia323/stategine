// A functor Shop -> Ledger mapping an object of the ledger as if it were the shop's.
#include "tags.hpp"

int main() {
    sg::StateGraph g;
    shop_state(g);
    g.add<sg::State>("ledger");
    auto post = sg::typed::functor<Shop, Ledger>(g, "post");
#ifdef SG_EXPECT_FAIL
    post.on<Book, Shelf>();
#else
    post.on<Shelf, Book>();
#endif
}
