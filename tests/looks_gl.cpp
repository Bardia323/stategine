// Stategine - looks against a real GL context.
//
// Three broken looks, each named by prepare() with what is wrong; a broken
// shader falling back to the built-in rather than drawing nothing; looks that
// only change numbers sharing the built-in programs; and a look the graph never
// mentioned before the first frame, compiled late and counted as such.
#include <iostream>

#include "sg/gl/Window.hpp"
#include "sg/render/GLWorld.hpp"
#include "sg/sg.hpp"

int main() {
    sg::gl::Window window(320, 180, "looks");
    sg::StateGraph g;
    auto& room = g.add<sg::Spatial3D>("room");
    room.light("lamp", {7, 3, 6});
    g.set_initial("room");

    auto& typo = g.add<sg::LookState>("typo");
    typo.uniform(sg::passes::scene, "uFogColour", 0.1, 0.1, 0.1);  // misspelt
    auto& stripped = g.add<sg::LookState>("stripped");
    stripped.shader(sg::passes::composite, R"(#version 330 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0); })");  // never reads uScene
    auto& broken = g.add<sg::LookState>("broken");
    broken.shader(sg::passes::composite, "#version 330 core\nvoid main() { oops }");
    auto& same = g.add<sg::LookState>("same_as_builtin");
    same.uniform(sg::passes::composite, "uExposure", 2.0);  // numbers only: shares programs
    for (const char* l : {"typo", "stripped", "broken", "same_as_builtin"}) sg::wear(g, "room", l);

    sg::render::GLWorldView view;
    const auto problems = view.prepare(g);
    for (const auto& p : problems) std::cout << "! " << p << "\n";
    std::cout << "programs after prepare: " << view.stats().programs << "\n";

    view.render(room, 320, 180);
    sg::set_look(room, "broken");
    view.render(room, 320, 180);  // falls back to the built-in, no throw

    // A look the graph never mentioned until now: compiled late, and counted.
    auto& surprise = g.add<sg::LookState>("surprise");
    surprise.shader(sg::passes::composite, R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;
void main() { FragColor = texture(uScene, vUV); })");
    sg::wear(g, "room", "surprise");
    sg::set_look(room, "surprise");
    for (int i = 0; i < 3; ++i) view.render(room, 320, 180);
    std::cout << "late compiles: " << view.stats().late << " of " << view.stats().programs << "\n";
    return problems.size() == 3 ? 0 : 1;
}
