#include "sg/audio/Ramp.hpp"

#include <algorithm>

namespace sg::audio {

Shape shape_of(const std::string& word) {
    if (word == "linear") return Shape::Linear;
    if (word == "log") return Shape::Log;
    if (word == "exp") return Shape::Exp;
    return Shape::S;
}

double ease(Shape shape, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (shape) {
        case Shape::Linear: return t;
        case Shape::S: return t * t * (3.0 - 2.0 * t);
        case Shape::Log: return std::log10(1.0 + 9.0 * t);
        case Shape::Exp: return (std::pow(10.0, t) - 1.0) / 9.0;
    }
    return t;
}

double Ramp::now() const { return t >= 1.0 ? to : from + (to - from) * ease(shape, t); }

void Ramp::toward(double db, double secs, Shape s) {
    db = std::max(kSilent, db);
    if (std::fabs(db - to) < 1e-3) return;
    from = now();
    to = db;
    seconds = std::max(0.0, secs);
    shape = s;
    t = seconds > 0.0 ? 0.0 : 1.0;
}

void Ramp::set(double db) {
    from = to = std::max(kSilent, db);
    t = 1.0;
}

void Ramp::advance(double dt) {
    if (t >= 1.0) return;
    t = seconds > 0.0 ? std::min(1.0, t + std::max(0.0, dt) / seconds) : 1.0;
}

}  // namespace sg::audio
