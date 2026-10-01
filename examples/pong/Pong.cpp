#include "Pong.hpp"
#include "Motion.hpp"
#include "sg/net/Epoch.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sg::examples::pong {
namespace {
int command(double x) { return x > 0.5 ? 1 : x < -0.5 ? -1 : 0; }
void publish_world(State& s) {
    const auto player = static_cast<int>(s.params().num("player"));
    auto& p = s.element(Key{"p"+std::to_string(player)}).params;
    p.set("position_0",s.element("left").params.num("y")).set("position_1",s.element("right").params.num("y"));
    p.set("snapshot",net::hex(frame_bytes(frame(s))));
    s.emit({"game.published",Params{}.set("epoch",s.params().get("epoch"))});
}
void begin(State& s, std::int64_t epoch) {
    s.params().set("epoch", epoch).set("round", std::int64_t{0});
    for (auto& element : s.elements()) {
        if (element.kind != Key{"participant"} || !element.params.get_or<bool>("computes", false)) continue;
        for (int c = 0; c < 2; ++c) {
            const auto suffix = "_" + std::to_string(c);
            element.params.set(Key{"result"+suffix}, element.params.num(Key{"observation"+suffix}));
        }
    }
}
}
Params configuration(int player) {
    if (player != 0 && player != 1) throw std::invalid_argument("Pong player must be 0 or 1");
    return Params{}.set("player", std::int64_t{player}).set("peer", "p"+std::to_string(player));
}
Params scripted_input(const State& game, int player) {
    // A repeated rally, then deliberate misses: tests exercise bounces and scoring.
    int move = 0;
    if (game.params().num("epoch") >= 240) move = player == 0 ? -1 : 1;
    else {
        const auto& ball = game.element("ball").params;
        const double t = std::max(0.0, ((player == 0 ? -0.8565 : 0.8565)-ball.num("x"))/ball.num("vx",0.68));
        double target = std::fmod(ball.num("y")+ball.num("vy")*t+0.749,2.996);
        if (target < 0) target += 2.996;
        target = target > 1.498 ? 2.247-target : target-0.749;
        target = std::clamp(target,-0.65,0.65);
        const double dy = target-game.element(player == 0 ? "left" : "right").params.num("y");
        move = dy > 0.012 ? 1 : dy < -0.012 ? -1 : 0;
    }
    return Params{}.set("move", static_cast<double>(move));
}
dsl::Natives natives(const net::Backend* backend, std::shared_ptr<net::Distributed> solver) {
    dsl::Natives n;
    n.arrow("dispatch", [](State& s, Element&, Element*, const Event& e) {
        if (e.args.has("packet")) s.emit({"network.outgoing",e.args});
    });
    n.arrow("configure_game", [](State& s, Element&, Element*, const Event& e) {
        if (!e.args.has("player")) return;
        const auto player = static_cast<int>(e.args.num("player"));
        configuration(player); s.params().set("player", std::int64_t{player});
        s.element("p0").params.set("publishes", player == 0);
        s.element("p1").params.set("publishes", player == 1);
        s.element("p0").params.set("coordinate",std::int64_t{0}); s.element("p1").params.set("coordinate",std::int64_t{1});
    });
    n.arrow("configure_network", [](State& s, Element&, Element*, const Event& e) {
        if (!e.args.has("player")) return;
        const auto player = static_cast<int>(e.args.num("player"));
        s.params().set("peer", configuration(player).get("peer"));
        s.params().set("secured",e.args.get_or<bool>("secured",false));
        if (e.args.has("committee")) s.params().set("committee",e.args.get("committee"));
        if (e.args.has("world")) s.params().set("world",e.args.get("world"));
        for (int i = 0; i < 2; ++i) {
            auto& p = s.element(Key{"p"+std::to_string(i)}).params;
            p.set("computes", i == player);
            if (s.params().get_or<bool>("secured",false)) p.set("weight",1.0).set("pinned_0",false).set("pinned_1",false);
            if (i != player) { p.erase("observation_0"); p.erase("observation_1"); }
        }
        begin(s, 0);
    });
    n.arrow("input", [](State& s, Element&, Element*, const Event& e) {
        if (!e.args.has("move")) return;
        const auto player = static_cast<int>(s.params().num("player"));
        auto& p = s.element(Key{"p"+std::to_string(player)}).params;
        const Key coordinate{"value_"+std::to_string(player)}; const double move = command(e.args.num("move"));
        if (p.num(coordinate) != move) { p.set(coordinate,move); s.emit("game.input.changed"); }
    });
    n.arrow("publish_request", [](State& s, Element&, Element*, const Event&) {
        publish_world(s);
    });
    n.transport("publish", [](const Element& from, Element& to) {
        if (!from.params.get_or<bool>("publishes", false)) return;
        for (int c = 0; c < 2; ++c) {
            const auto suffix = "_"+std::to_string(c);
            if (from.params.has(Key{"value"+suffix})) to.params.set(Key{"observation"+suffix}, from.params.get(Key{"value"+suffix}));
            if (from.params.has(Key{"position"+suffix})) to.params.set(Key{"position"+suffix},from.params.get(Key{"position"+suffix}));
        }
        const auto coordinate = static_cast<int>(from.params.num("coordinate"));
        if (from.params.has(Key{"value_"+std::to_string(coordinate)})) to.params.set("input",from.params.get(Key{"value_"+std::to_string(coordinate)}));
        if (from.params.has("snapshot")) to.params.set("snapshot",from.params.get("snapshot"));
    });
    n.transport("correct", [](const Element& from, Element& to) {
        if (!from.params.get_or<bool>("computes", false)) return;
        for (int c = 0; c < 2; ++c) {
            const auto suffix = "_"+std::to_string(c);
            if (from.params.has(Key{"result"+suffix})) to.params.set(Key{"command"+suffix}, from.params.get(Key{"result"+suffix}));
            if (from.params.has(Key{"accepted_command"+suffix})) to.params.set(Key{"command"+suffix},from.params.get(Key{"accepted_command"+suffix}));
        }
    });
    n.arrow("begin", [](State& s, Element&, Element*, const Event& e) {
        if (!e.args.has("epoch")) return;
        const auto epoch = net::Exchange::step(e.args).epoch;
        if (epoch < net::Exchange::step(s).epoch) return;
        begin(s, epoch);
    });
    n.arrow("receive", [solver](State& s, Element&, Element*, const Event& e) {
        if (s.params().get_or<bool>("secured",false)) return;
        if (!e.args.has("packet")) return;
        for (const auto& change : solver->receive(s, net::Exchange::decode(net::Exchange::bytes(e.args)))) s.element(change.element).params = change.params;
    });
    n.arrow("reconcile", [solver, backend](State& s, Element&, Element*, const Event& e) {
        const double dt = e.args.num("dt");
        if (dt <= 0) return; // Temporal's zero interval is the identity.
        if (s.params().get_or<bool>("secured",false)) {
            const auto epoch = s.params().get_or<std::int64_t>("epoch",0); const auto peer = s.params().get_or<std::string>("peer","p0");
            const auto& p = s.element(Key{peer}).params;
            s.emit({"network.forecast",Params{}.set("input",p.num("input")).set("time",e.args.num("time")).set("dt",dt)});
            if (s.params().num("requested_epoch",-1) != epoch) { s.params().set("requested_epoch",epoch); s.emit({"network.observation",Params{}.set("epoch",epoch)}); }
            return;
        }
        const auto step = net::Exchange::step(s);
        if (step.tick < s.params().num("settle_rounds", 8)) {
            const auto result = solver->evaluate(s, backend);
            s.params() = result.params;
            for (const auto& change : result.boundaries) s.element(change.element).params = change.params;
            for (const auto& r : result.section.readings) s.element(r.element).params.set(net::Cellular::coordinate_key("result", r.dimension, r.coordinate), r.value);
            if (result.advanced) s.params().set("residual", result.section.residual);
            for (const auto& packet : result.outgoing) s.emit({"network.boundary", net::Exchange::arguments(net::Exchange::encode(packet))});
        }
        const auto now = net::Exchange::step(s);
        const auto& boundary = s.element("shared").params;
        // Keep the final boundary available until the neighbor reached it too.
        // Last-two-packet retransmission carries this acknowledgement into the
        // next epoch without introducing a privileged step coordinator.
        if (now.tick >= s.params().num("settle_rounds", 8) &&
            boundary.num("remote_epoch", -1) == now.epoch && boundary.num("remote_tick", -1) == now.tick &&
            boundary.get_or<bool>("remote_ready", false) && s.params().num("committed_epoch", -1) != now.epoch) {
            s.params().set("committed_epoch", now.epoch);
            s.emit({"network.committed", Params{}.set("epoch", now.epoch)});
        }
    });
    const auto final_topology = std::make_shared<net::Cellular>();
    n.arrow("finalized", [final_topology](State& s, Element&, Element*, const Event& e) {
        if (!e.args.has("receipt")) return;
        const auto epoch = net::Exchange::step(e.args).epoch;
        if (!s.params().get_or<bool>("secured",false) || epoch != s.params().get_or<std::int64_t>("epoch",0) || s.params().num("committed_epoch",-1) == epoch) return;
        if (!e.args.is("committee",s.params().get_or<std::string>("committee",{}).c_str()) || e.args.get_or<std::string>("previous",{}) != s.params().get_or<std::string>("previous",net::hex(net::Digest{}))) throw std::invalid_argument("Pong finalization does not extend its declared committee checkpoint");
        const auto receipt = net::digest_from_hex(e.args.get_or<std::string>("receipt",{}));
        final_topology->update(s); const auto& topology = *final_topology;
        if (e.args.get_or<std::string>("topology",{}) != net::hex(net::topology_hash(topology.layout(),net::topology_description(topology)))) throw std::invalid_argument("Pong finalization has a different topology");
        const int left = static_cast<int>(e.args.num("left_input",2)), right = static_cast<int>(e.args.num("right_input",2));
        if (std::abs(left) > 1 || std::abs(right) > 1 || e.args.num("left_input") != left || e.args.num("right_input") != right) throw std::invalid_argument("Pong controls must be exact discrete events");
        for (int v = 0; v < 2; ++v) for (int c = 0; c < 2; ++c) {
            const Key key{"value_"+std::to_string(2*v+c)}; const double value = e.args.num(key,2);
            if (!std::isfinite(value) || std::fabs(value) > 0.650001) throw std::invalid_argument("Pong numerical boundary is outside its court");
        }
        for (int v = 0; v < 2; ++v) {
            auto& p = s.element(Key{"p"+std::to_string(v)}).params;
            for (int c = 0; c < 2; ++c) p.set(Key{"result_"+std::to_string(c)},e.args.num(Key{"value_"+std::to_string(2*v+c)}));
            p.set("accepted_command_0",static_cast<double>(left)).set("accepted_command_1",static_cast<double>(right));
        }
        s.params().set("previous",net::hex(receipt)).set("committed_epoch",epoch);
        s.emit({"network.committed",Params{}.set("epoch",epoch)});
    });
    n.arrow("corrected", [](State& s, Element& control, Element*, const Event& e) {
        if (!e.args.has("epoch")) return;
        const auto epoch = net::Exchange::step(e.args).epoch;
        if (epoch == s.params().get_or<std::int64_t>("epoch",0)) control.params.set("ready_epoch",epoch);
    });
    n.arrow("step", [](State& s, Element& control, Element*, const Event& e) {
        const auto epoch = s.params().get_or<std::int64_t>("epoch",0);
        if (e.args.num("dt") <= 0 || control.params.num("ready_epoch",-1) != epoch || control.params.num("issued_epoch",-1) == epoch) return;
        control.params.set("issued_epoch",epoch);
        s.emit({"game.ready",Params{}.set("duration",e.args.num("dt"))});
    });
    n.arrow("commands", [](State& s, Element&, Element*, const Event& e) {
        if (e.args.num("duration") <= 0) return;
        const auto player = static_cast<int>(s.params().num("player"));
        const auto& p = s.element(Key{"p"+std::to_string(player)}).params;
        s.element("left").params.set("vy", 0.95*command(p.num("command_0")));
        s.element("right").params.set("vy", 0.95*command(p.num("command_1")));
    });
    n.arrow("bounce", [](State& s, Element&, Element*, const Event& e) {
        if (e.args.num("duration") <= 0) return;
        auto& l = s.element("left").params; auto& r = s.element("right").params;
        bounce(l,r,s.element("ball").params,s.params());
        const auto epoch = s.params().get_or<std::int64_t>("epoch", 0)+1;
        s.params().set("epoch", epoch);
        publish_world(s);
    });
    return n;
}
}
