// DOM callbacks collect physical input. The application drains it through Bindings.
#pragma once
#include "sg/dsl/Runtime.hpp"
#include <memory>
namespace sg::web {
struct DeviceInput { std::string device,key; double x=0,y=0,value=0; };
class Input {
public:
    explicit Input(const std::string& canvas);
    ~Input();
    bool try_receive(DeviceInput& input);
    void poll(Engine& engine,const dsl::Bindings& bindings);
    void pointer_lock();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
