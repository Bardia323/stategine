#include "sg/render/Graphics.hpp"

namespace sg::render {

Graphics::Graphics(Key id, const Quality& start) : State(id) {
    Element& q = add_element(quality_id(), Key{"settings"});
    quality_to_params(kept_to_bounds(start), q.params);
    // quality --set--> quality: whichever settings the event names (what
    // it names that is no setting, it leaves), kept to what they may be.
    loop(Key{"set"}, quality_id(), set_event(), [](State&, Element& e, Element*, const Event& ev) {
        quality_to_params(kept_to_bounds(quality_from_params(ev.args, quality_from_params(e.params))), e.params);
    });
}

Quality Graphics::quality() const { return quality_from_params(element(quality_id()).params); }

}  // namespace sg::render
