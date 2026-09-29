#include "sg/dsl/Natives.hpp"

#include <stdexcept>

namespace sg::dsl {

Natives& Natives::arrow(const std::string& name, Morphism::Handler h) {
    arrows_[name] = std::move(h);
    return *this;
}

Natives& Natives::transport(const std::string& name, Transport t) {
    transports_[name] = std::move(t);
    return *this;
}

Natives& Natives::edit(const std::string& name, Edit::Apply a) {
    edits_[name] = std::move(a);
    return *this;
}

Morphism::Handler Natives::arrow(const std::string& name) const {
    auto it = arrows_.find(name);
    if (it == arrows_.end()) throw std::runtime_error("no native arrow " + name + " is registered");
    return it->second;
}

Transport Natives::transport(const std::string& name) const {
    auto it = transports_.find(name);
    if (it == transports_.end()) throw std::runtime_error("no native transport " + name + " is registered");
    return it->second;
}

Edit::Apply Natives::edit(const std::string& name) const {
    auto it = edits_.find(name);
    if (it == edits_.end()) throw std::runtime_error("no native edit " + name + " is registered");
    return it->second;
}

}  // namespace sg::dsl
