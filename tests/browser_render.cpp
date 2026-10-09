// The same declared fixture is drawn without changing its semantic snapshot.
#include "sg/core/Engine.hpp"
#include "sg/core/Laws.hpp"
#include "sg/domains/Surface.hpp"
#include "sg/domains/Texture.hpp"
#include "sg/render/Pack.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/dsl/Natives.hpp"
#include "sg/dsl/Runtime.hpp"
#include "sg/net/Integrity.hpp"
#ifdef __EMSCRIPTEN__
#include "sg/web/WebGPU.hpp"
#include <emscripten.h>
#else
#include "sg/gl/Window.hpp"
#include "sg/gl/World.hpp"
#endif
#include <iostream>
#include <memory>
namespace sgen {
void build_browser_render(sg::StateGraph &, const sg::dsl::Natives &, sg::dsl::Bindings &);
}
namespace {
// A texture that names its pixels (so a picture kept packed by that name is
// what is sent of it, as BC7 with every mip, where the device takes it).
struct NamedSkin : sg::Texture {
    using sg::Texture::Texture;
    bool pixels_digest(sg::Digest &out) const override {
        out = sg::Hasher{}.text("browser fixture skin").digest();
        return true;
    }
};
struct Fixture {
    sg::StateGraph graph;
    sg::dsl::Bindings bindings;
    sg::Engine engine{graph};
    Fixture() {
        graph.add<sg::Surface2D>("board", 8, 5);
        graph.add<NamedSkin>("skin", 64);
        graph.find("skin")->element(sg::Texture::map_id()).params.set("generator", std::string("checks")).set("tint_g", 0.6);
        sg::dsl::Natives natives;
        natives.arrow("shader_source", [](sg::State &, sg::Element &pass, sg::Element *, const sg::Event &e) {
            if (e.args.has("source"))
                pass.params.set("wgsl", e.args.get_or<std::string>("source", {}));
        });
        sgen::build_browser_render(graph, natives, bindings);
        engine.set_strict(true);
        engine.start();
        engine.tick(.25);
        const auto &board = dynamic_cast<const sg::Surface2D &>(graph.state("board"));
        const auto &screen = graph.state("room").element("screen");
        if (sg::render::signal_of(graph, screen) != screen.id || !sg::render::declared_surface(graph, screen, board) ||
            !sg::render::declared_surface(graph, graph.state("room").element("box"), board))
            throw std::runtime_error("declared surface path lost its empty alias or incoming functor");
        const auto stale = graph.state("room").element("box");
        if (sg::render::declared_surface(graph, stale, board))
            throw std::runtime_error("unowned element copy authorized a surface binding");
        const auto plan = sg::render::view_plan(graph, graph.state("room"));
        const auto feed = std::find_if(plan.rooms[0].portals.begin(), plan.rooms[0].portals.end(),
                                       [](const auto &portal) { return portal.portal->id == sg::Key{"monitor"}; });
        if (feed == plan.rooms[0].portals.end() || feed->feed_eye != &graph.state("recorder").element("lens"))
            throw std::runtime_error("camera feed lost its declared lens");
    }
    std::string facts() const {
        sg::net::Bytes bytes;
        for (const auto &f : sg::dsl::facts(graph)) {
            bytes.insert(bytes.end(), f.begin(), f.end());
            bytes.push_back('\n');
        }
        return sg::net::hex(sg::net::hash(bytes));
    }
};
#ifdef __EMSCRIPTEN__
std::unique_ptr<Fixture> fixture;
std::unique_ptr<sg::web::WebGPUView> view;
std::string result;
bool shader_fallback = false;
#endif
} // namespace
#ifdef __EMSCRIPTEN__
extern "C" {
EMSCRIPTEN_KEEPALIVE int sg_fixture_start() {
    try {
        fixture = std::make_unique<Fixture>();
        view = std::make_unique<sg::web::WebGPUView>("#canvas");
        view->bind_surface("screen", dynamic_cast<sg::Surface2D *>(fixture->graph.find("board")));
        view->bind_surface("box", dynamic_cast<sg::Surface2D *>(fixture->graph.find("board")));
        // The crate's picture kept packed by its name, as a game ships it.
        auto *skin = dynamic_cast<sg::Surface2D *>(fixture->graph.find("skin"));
        sg::cache::set_folder("/cache");
        sg::Digest named;
        skin->pixels_digest(named);
        sg::render::pack_kept(skin->raster().data(), skin->px_w(), skin->px_h(), skin->srgb(), named);
        view->bind_surface("crate", skin);
        const auto errors = view->prepare(fixture->graph);
        if (!errors.empty())
            throw std::runtime_error(errors[0]);
        return 1;
    } catch (const std::exception &e) {
        result = e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE int sg_fixture_draw() {
    try {
        view->render(fixture->graph, fixture->graph.state("room"), 640, 360);
        const auto errors = view->diagnostics();
        if (!errors.empty() && !shader_fallback)
            throw std::runtime_error(errors[0]);
        return view->ready() ? 1 : 2;
    } catch (const std::exception &e) {
        result = e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE const char *sg_fixture_error() { return result.c_str(); }
EMSCRIPTEN_KEEPALIVE const char *sg_fixture_facts() {
    result = fixture->facts();
    return result.c_str();
}
EMSCRIPTEN_KEEPALIVE void sg_fixture_recreate() { view->recreate(); }
EMSCRIPTEN_KEEPALIVE int sg_fixture_verify() {
    return fixture->graph.validate().empty() && sg::verify(fixture->graph).holds();
}
EMSCRIPTEN_KEEPALIVE int sg_fixture_shader(const char *source, int fallback) {
    try {
        fixture->engine.send("room.look", {"room.look.shader", sg::Params{}.set("source", std::string(source))});
        fixture->engine.tick(0);
        shader_fallback = fallback != 0;
        view->prepare(fixture->graph,
                      fallback ? sg::web::MissingShader::BuiltinFallback : sg::web::MissingShader::Refuse);
        return 1;
    } catch (const std::exception &e) {
        result = e.what();
        return 0;
    }
}
EMSCRIPTEN_KEEPALIVE const char *sg_fixture_diagnostics() {
    result.clear();
    for (const auto &diagnostic : view->diagnostics())
        result += diagnostic + '\n';
    return result.c_str();
}
}
int main() { return 0; }
#else
int main() {
    try {
        Fixture f;
        sg::gl::Window window(640, 360, "renderer fixture");
        sg::render::GLWorldView view;
        const auto before = f.facts();
        const auto errors = view.prepare(f.graph);
        if (!errors.empty())
            throw std::runtime_error(errors[0]);
        view.bind_surface("screen", dynamic_cast<sg::Surface2D *>(f.graph.find("board")));
        view.bind_surface("box", dynamic_cast<sg::Surface2D *>(f.graph.find("board")));
        view.bind_surface("crate", dynamic_cast<sg::Surface2D *>(f.graph.find("skin")));
        const auto &seam = f.graph.seams().front();
        const auto *forward = f.graph.functor(seam.a_to_b)->transport_of("camera");
        view.bind_world("door", dynamic_cast<const sg::Spatial3D *>(f.graph.find("guest")), *forward, "door");
        for (int i = 0; i < 3; ++i)
            view.render(dynamic_cast<const sg::Spatial3D &>(f.graph.state("room")), 640, 360);
        const auto laws = sg::verify(f.graph);
        if (!laws.holds())
            std::cerr << laws.str();
        if (before != f.facts())
            throw std::runtime_error("renderer changed world");
        if (!f.graph.validate().empty() || !laws.holds())
            throw std::runtime_error("fixture laws failed");
        std::cout << before << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
