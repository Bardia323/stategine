#include "Motion.hpp"
#include "sg/core/Declared.hpp"
#include "sg/net/Integrity.hpp"
#include "../../src/net/Canonical.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sg::examples::pong {
void bounce(Params& l, Params& r, Params& b, Params& score) {
        for (auto* p : {&l, &r}) p->set("y", std::clamp(p->num("y"), -0.65, 0.65));

        double x = b.num("x"), y = b.num("y"), vx = b.num("vx"), vy = b.num("vy");
        if (y > 0.749) { y = 1.498-y; vy = -std::fabs(vy); }
        if (y < -0.749) { y = -1.498-y; vy = std::fabs(vy); }
        const bool left = vx < 0;
        const auto& p = left ? l : r;
        const double face = left ? -0.8565 : 0.8565;
        if ((left ? x <= face && x > -0.93 : x >= face && x < 0.93) && std::fabs(y-p.num("y")) <= 0.121) {
            x = face; vx = (left ? 1 : -1)*std::min(1.25, std::fabs(vx)*1.025);
            vy = std::clamp(4.1*(y-p.num("y")) + 0.18*p.num("vy"), -0.65, 0.65);
            if (std::fabs(vy) < 0.1) vy = vy < 0 ? -0.1 : 0.1;
            score.set("hits", std::int64_t{score.get_or<std::int64_t>("hits", 0)+1});
        }
        if (std::fabs(x) > 1.02) {
            const Key side = x > 0 ? Key{"left_score"} : Key{"right_score"};
            const auto points = score.get_or<std::int64_t>(side, 0)+1;
            score.set(side, points); x = 0; y = 0;
            vx = vx > 0 ? -0.68 : 0.68;
            const auto total = score.get_or<std::int64_t>("left_score",0)+score.get_or<std::int64_t>("right_score",0);
            vy = total % 2 == 0 ? 0.27 : -0.27;
        }
        b.set("x", x).set("y", y).set("vx", vx).set("vy", vy);
}
net::PredictedValues frame(const State& game) {
    const auto& b = game.element("ball").params;
    const auto& l = game.element("left").params;
    const auto& r = game.element("right").params;
    return {b.num("x"),b.num("y"),b.num("vx"),b.num("vy"),l.num("y"),l.num("vy"),r.num("y"),r.num("vy"),game.params().num("left_score"),game.params().num("right_score"),game.params().num("hits")};
}
net::Prediction::Step forecast(const State& game) {
    const auto declared = [&](Key name) {
        const auto* arrow = game.morphism(name);
        if (!arrow || !arrow->declared || arrow->declared->size() != 1) throw std::logic_error("Pong forecast needs its declared affine motion");
        return arrow->declared->front().does;
    };
    const auto ball = declared("move_ball"), left = declared("move_left"), right = declared("move_right");
    return [ball,left,right](const net::PredictedValues& x,const net::DiscreteInputs& input) {
        if (x.size() != 11 || input.size() != 2 || std::abs(input[0]) > 1 || std::abs(input[1]) > 1) throw std::invalid_argument("invalid Pong forecast input");
        Element b{Key{"ball"},Key{}}, l{Key{"left"},Key{}}, r{Key{"right"},Key{}};
        b.params.set("x",x[0]).set("y",x[1]).set("vx",x[2]).set("vy",x[3]);
        l.params.set("y",x[4]).set("vy",0.95*input[0]); r.params.set("y",x[6]).set("vy",0.95*input[1]);
        Params score; score.set("left_score",static_cast<std::int64_t>(x[8])).set("right_score",static_cast<std::int64_t>(x[9])).set("hits",static_cast<std::int64_t>(x[10]));
        const auto args = Params{}.set("duration",1.0/60.0);
        run(left,l,l,&args); run(right,r,r,&args); run(ball,b,b,&args);
        bounce(l.params,r.params,b.params,score);
        return net::PredictedValues{b.params.num("x"),b.params.num("y"),b.params.num("vx"),b.params.num("vy"),l.params.num("y"),l.params.num("vy"),r.params.num("y"),r.params.num("vy"),score.num("left_score"),score.num("right_score"),score.num("hits")};
    };
}
net::Bytes frame_bytes(const net::PredictedValues& x) {
    if (x.size() != 11) throw std::invalid_argument("invalid Pong checkpoint");
    net::detail::Writer w("pong.rules.checkpoint.v1");
    for (int i = 0; i < 8; ++i) w.real(x[i]);
    for (int i = 8; i < 11; ++i) {
        if (x[i] < 0 || x[i] != std::floor(x[i]) || x[i] > 9007199254740991.0) throw std::invalid_argument("Pong score must remain exact discrete data");
        w.integer(static_cast<std::uint64_t>(x[i]));
    }
    return w.data();
}
net::PredictedValues read_frame(const net::Bytes& bytes) {
    net::detail::Reader r(bytes,"pong.rules.checkpoint.v1"); net::PredictedValues x;
    for (int i = 0; i < 8; ++i) x.push_back(r.real());
    for (int i = 8; i < 11; ++i) { const auto n = r.integer(); if (n > 9007199254740991ull) throw std::invalid_argument("oversized discrete Pong score"); x.push_back(static_cast<double>(n)); }
    r.end(); return x;
}
}
