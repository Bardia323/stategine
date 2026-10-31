// Physics as a state of its own (sg/domains/Physics.hpp): loose things fall,
// land and come to rest on what is under them, stepped by the state's own
// arrows; solid ones knock into each other.
#include <cmath>
#include <cstdio>
#include <string>

#include "sg/domains/Physics.hpp"

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what.c_str());
    if (!ok) ++failures;
}
}  // namespace

int main() {
    using sg::physics::Physics;
    namespace mode = sg::physics::mode;
    {
        // A book let go 1.4 m up: it falls, lands on the floor and lies there.
        Physics p(sg::Key{"physics"}, 10, 10, 3);
        sg::Element& book = p.add_body(sg::Key{"book"}, 0.2, 0.15, 0.03, 0.0, "box");
        Physics::place(book, {5.0, 1.4, 5.0}, 0, 0, 0, mode::fall);
        for (int i = 0; i < 180; ++i) {
            p.hear(sg::Event{Physics::step_event(), sg::Params{}.set(sg::keys::dt, 1.0 / 60.0)});
            p.dispatch_pending();
        }
        const double y = book.params.num(sg::keys::y);
        check(y > -0.01 && y < 0.05, "a book let go lands on the floor (" + std::to_string(y) + " m)");
        check(sg::physics::text_of(book, sg::Key{"mode"}) == mode::rest, "and comes to rest there");
    }
    {
        // A sheet let go from the same height takes longer to come down: the air holds it up.
        Physics p(sg::Key{"physics"}, 10, 10, 3);
        sg::Element& book = p.add_body(sg::Key{"book"}, 0.2, 0.15, 0.03, 0.0, "box");
        sg::Element& sheet = p.add_body(sg::Key{"sheet"}, 0.21, 0.297, 0.0002, 1.0, "sheet");
        Physics::place(book, {3.0, 1.4, 5.0}, 0, 0, 0, mode::fall);
        Physics::place(sheet, {7.0, 1.4, 5.0}, 0, sg::physics::kFlat, 0, mode::fall);
        for (int i = 0; i < 20; ++i) {
            p.hear(sg::Event{Physics::step_event(), sg::Params{}.set(sg::keys::dt, 1.0 / 60.0)});
            p.dispatch_pending();
        }
        check(sheet.params.num(sg::keys::y) > book.params.num(sg::keys::y) + 0.02, "a sheet falls slower than a book");
    }
    std::printf("%s\n", failures ? "FAILED" : "all ok");
    return failures ? 1 : 0;
}
