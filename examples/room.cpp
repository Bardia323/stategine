// Stategine example: the same room and the same wall map, in the terminal.
//
// Identical graph to room3d.cpp - same states, same lens, same portal. Only the
// view differs, which is the point of keeping renderers out of the states.
//
//   m        open / close the map          w a s d  move the selected token
//   tab      select the next token         c        cancel (close, discard)
//   , .      slide the lamp                q        quit
#include <cctype>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "sg/render/Ascii.hpp"
#include "sg/sg.hpp"

namespace {

constexpr int kCols = 12;
constexpr int kRows = 10;

double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

int main() {
    sg::StateGraph graph;
    auto& room = graph.add<sg::Spatial3D>("room");
    room.set_integrating(false);

    const std::vector<sg::Key> boxes{"box_a", "box_b", "box_c", "box_d"};
    const double start[4][2] = {{2, 1}, {9, 2}, {4, 7}, {10, 8}};
    for (std::size_t i = 0; i < boxes.size(); ++i)
        room.mesh(boxes[i], start[i][0], 0.0, start[i][1],
                  static_cast<char>('a' + static_cast<int>(i)));

    room.light("lamp", {6, 3, 4}).params.set(sg::keys::glyph, std::string("*"));
    room.portal("wall_map", {0, 2, 0}, 3.0, 2.0, 0.0).params.set(sg::keys::glyph, std::string("M"));

    auto& map = graph.add<sg::Spatial2D>("wallmap", kCols, kRows);
    map.set_integrating(false);
    for (std::size_t i = 0; i < boxes.size(); ++i)
        map.sprite(sg::Key{boxes[i].str() + "_tok"}, 0, 0,
                   static_cast<char>('a' + static_cast<int>(i)));

    // The same lens as the 3D example: (x, z) <-> (x, y).
    std::vector<std::pair<sg::Key, sg::Key>> objects;
    for (sg::Key b : boxes) objects.emplace_back(b, sg::Key{b.str() + "_tok"});
    graph.add_lens("collapse", "stamp", "room", "wallmap", objects,
                   sg::transport::swizzle({{sg::keys::x, sg::keys::x},
                                           {sg::keys::y, sg::keys::z}}),
                   sg::transport::swizzle({{sg::keys::x, sg::keys::x},
                                           {sg::keys::z, sg::keys::y}}));
    graph.embed("map", "room", "wall_map", "wallmap", "collapse", "stamp", sg::EmbedSync::Live);
    graph.set_initial("room");

    // Structure, then every law on the data as it stands - checked and undone.
    const sg::LawReport laws = sg::verify(graph);
    if (!laws.ok()) std::cout << laws.str();

    sg::Engine engine(graph);
    engine.start();
    engine.tick(0.0);
    sg::render::draw_top_down(room, kCols, kRows);
    std::cout << "\ncommands: m (map)  w a s d (move)  tab (next token)  , . (lamp)  c  q\n\n";

    std::size_t sel = 0;
    std::string line;
    while (engine.running() && std::getline(std::cin, line)) {
        for (char cmd : line) {
            if (std::isspace(static_cast<unsigned char>(cmd))) continue;
            const bool open = engine.embed_open("map");
            switch (cmd) {
                case 'q':
                    engine.stop();
                    break;
                case 'm':
                    if (open) {
                        engine.close_embed("map");
                    } else {
                        engine.open_embed("map");
                    }
                    break;
                case 'c':
                    engine.close_embed("map", /*commit=*/false);
                    break;
                case '\t':
                    sel = (sel + 1) % boxes.size();
                    break;
                case 'w':
                case 'a':
                case 's':
                case 'd': {
                    if (!open) {
                        std::cout << "(open the map first: m)\n";
                        break;
                    }
                    sg::Element& tok = map.element(sg::Key{boxes[sel].str() + "_tok"});
                    const double dx = (cmd == 'd') - (cmd == 'a');
                    const double dy = (cmd == 's') - (cmd == 'w');
                    tok.params.set(sg::keys::x,
                                   clampd(tok.params.num(sg::keys::x) + dx, 0, kCols - 1));
                    tok.params.set(sg::keys::y,
                                   clampd(tok.params.num(sg::keys::y) + dy, 0, kRows - 1));
                    break;
                }
                case ',':
                case '.': {
                    sg::Element& lamp = room.element("lamp");
                    lamp.params.set(sg::keys::x,
                                    clampd(lamp.params.num(sg::keys::x) + (cmd == '.' ? 1 : -1), 0,
                                           kCols - 1));
                    break;
                }
                default:
                    break;
            }
            if (!engine.running()) break;
        }
        if (!engine.running()) break;

        engine.tick(0.05);
        sg::render::draw_top_down(room, kCols, kRows);
        if (engine.embed_open("map")) {
            sg::render::draw(map);
            const sg::Vec3d p = sg::position_of(room.element(boxes[sel]));
            std::cout << "  " << boxes[sel].str() << " in the room: x=" << p.x << " y=" << p.y
                      << " z=" << p.z << "   [selected: " << boxes[sel].str() << "_tok]\n";
        }
        std::cout << "\n";
    }

    std::cout << "bye\n";
    return 0;
}
