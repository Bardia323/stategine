// Stategine - terminal views.
//
// A view reads a state and draws it. It is not a state and not a subclass of
// one, so any state can have several views at once (or none), and adding a
// backend never touches the domain code.
#pragma once

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sg/domains/Console.hpp"
#include "sg/domains/Spatial.hpp"

namespace sg::render {

// A character grid with a couple of drawing helpers, shared by the views below.
class CharCanvas {
public:
    CharCanvas(int w, int h, char fill = ' ') : w_(w), h_(h), rows_(h, std::string(w, fill)) {}

    void put(int x, int y, char c);

    void print(std::ostream& os, const std::string& title = {}, bool framed = true) const;

    int width() const { return w_; }
    int height() const { return h_; }

private:
    int w_, h_;
    std::vector<std::string> rows_;
};

char glyph_of(const Element& e, char fallback);

// A 2D state, straight down.
void draw(const Spatial2D& s, std::ostream& os = std::cout, bool framed = true);

// A 3D state seen from above, with the light baked into the shading - the cheap
// way to see the same world the GL renderer draws.
void draw_top_down(const Spatial3D& s, int cols, int rows, std::ostream& os = std::cout);

// A console state's scrollback.
void draw(const ConsoleState& c, std::ostream& os = std::cout, std::size_t tail = 12);

}  // namespace sg::render
