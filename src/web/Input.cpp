#include "sg/web/Input.hpp"
#include "sg/core/Engine.hpp"
#include <emscripten/val.h>
namespace sg::web {
using emscripten::val;
struct Input::Impl {
    val handle;
    explicit Impl(const std::string &canvas) : handle(val::global("sgDOM").call<val>("create", canvas)) {}
};
Input::Input(const std::string &canvas) : impl_(std::make_unique<Impl>(canvas)) {}
Input::~Input() { impl_->handle.call<void>("destroy"); }
bool Input::try_receive(DeviceInput &input) {
    auto i = impl_->handle.call<val>("receive");
    if (i.isNull())
        return false;
    input = {i["device"].as<std::string>(), i["key"].as<std::string>(), i["x"].as<double>(), i["y"].as<double>(),
             i["value"].as<double>()};
    return true;
}
void Input::poll(Engine &e, const dsl::Bindings &b) {
    impl_->handle.call<void>("poll");
    DeviceInput input;
    while (try_receive(input))
        if (const auto *binding = b.find(input.device, input.key)) {
            auto args = binding->args;
            args.set("x", input.x).set("y", input.y).set("value", input.value);
            e.fire({binding->event, std::move(args)});
        }
}
void Input::pointer_lock() { impl_->handle.call<void>("pointerLock"); }
} // namespace sg::web
