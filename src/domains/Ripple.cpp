#include "sg/domains/Ripple.hpp"

#include <algorithm>
#include <string>

namespace sg {

void ripple(Element& water, double x, double z, double strength, double now) {
    static const Key next{"ripple_next"};
    const int k = static_cast<int>(water.params.num(next, 0.0)) % kRipples;
    const std::string at = "ripple" + std::to_string(k);
    water.params.set(Key{at + "_x"}, x).set(Key{at + "_z"}, z).set(Key{at + "_t"}, now).set(Key{at + "_a"}, std::clamp(strength, 0.0, 1.0));
    water.params.set(next, static_cast<double>((k + 1) % kRipples));
}

}  // namespace sg
