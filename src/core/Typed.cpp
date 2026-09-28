#include "sg/core/Typed.hpp"

namespace sg::typed::detail {

void require_state(const State& s, Key want, const char* what) {
    if (s.id() != want)
        throw std::logic_error(std::string("sg::typed::") + what + ": state is " + s.id().str() +
                               ", but the tag says " + want.str());
}

}  // namespace sg::typed::detail
