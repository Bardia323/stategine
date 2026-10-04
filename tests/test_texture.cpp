// A texture: a state of its own, painted from its map's params, worn by
// whatever it is embedded in, followed when a file painted over it changes;
// and the six views of a thing, to paint over.
#include "sg/domains/Texture.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "sg/core/Laws.hpp"
#include "sg/core/StateGraph.hpp"

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

int main() {
    using sg::Key;
    {
        sg::StateGraph g;
        sg::Spatial3D& room = g.add<sg::Spatial3D>(Key{"room"});
        room.mesh(Key{"crate"}, 0, 0, 0);
        sg::Texture& t = g.add<sg::Texture>(Key{"rust"}, 16);
        g.set_focus(g.embed(Key{"wears.crate"}, Key{"room"}, Key{"crate"}, t.id(), Key{}, Key{}).name, false);
        g.set_initial(Key{"room"});
        for (const auto& why : g.validate()) std::printf("  %s\n", why.c_str());
        check(g.validate().empty(), "a texture worn by a thing is reached by its embedding");
        check(sg::verify(g).ok(), "and keeps the laws: its one arrow sets what it is given");

        const auto set = [&](sg::Params p) {
            t.hear(sg::Event{sg::Texture::set_event(), std::move(p)});
            t.dispatch_pending();
        };
        const auto first = t.raster();
        const uint64_t r0 = t.revision();
        t.raster();
        check(t.revision() == r0, "painted once, it is not painted again while nothing changes");
        set(sg::Params{}.set("generator", std::string("checks")).set("count", 2.0));
        const auto second = t.raster();
        check(t.revision() != r0 && second != first, "set to another generator, it paints itself anew");
        // checks, two a cell: the corners of a cell differ, as a chequer does.
        check(t.pixel(1, 1)[0] != t.pixel(12, 1)[0], "and what it paints is that generator's");

        // A generator that tiles tiles: the first column is the next of the last.
        set(sg::Params{}.set("generator", std::string("noise")).set("scale", 4.0));
        t.raster();
        int worst = 0;
        for (int y = 0; y < 16; ++y) worst = std::max(worst, std::abs(int(t.pixel(0, y)[0]) - int(t.pixel(15, y)[0])));
        check(worst < 40, "noise tiles across a cell, so a pattern can go on round a thing");

        // A layer painted over it, in a file: read by the program's reader,
        // and followed when the file changes.
        namespace fs = std::filesystem;
        const fs::path f = fs::temp_directory_path() / ("sg_texture_" + std::to_string(std::rand()) + ".raw");
        std::ofstream(f) << "red";
        sg::Texture::set_reader([](const std::string& path, int& w, int& h, std::vector<unsigned char>& rgba) {
            std::ifstream in(path);
            std::string what;
            in >> what;
            w = h = 1;
            rgba = what == "red" ? std::vector<unsigned char>{255, 0, 0, 255} : std::vector<unsigned char>{0, 0, 255, 255};
            return true;
        });
        set(sg::Params{}.set("layer", f.string()));
        t.raster();
        check(t.pixel(5, 5)[0] == 255 && t.pixel(5, 5)[2] == 0, "a file painted over it shows over it");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::ofstream(f, std::ios::trunc) << "blue";
        fs::last_write_time(f, fs::last_write_time(f) + std::chrono::seconds(2));
        t.raster();
        check(t.pixel(5, 5)[2] == 255 && t.pixel(5, 5)[0] == 0, "painted again outside, it follows the file");
        fs::remove(f);
        sg::Texture::set_reader({});
    }
    {
        // The six views of a ball: a disc in each cell, shaded brightest in
        // its middle, outlined; nothing past it.
        const int c = 32;
        const auto guide = sg::projection_guide(sg::unit_shape("sphere"), c);
        const auto at = [&](int cell, int x, int y) { return &guide[(static_cast<std::size_t>((cell / 3) * c + y) * 3 * c + (cell % 3) * c + x) * 4]; };
        bool discs = true;
        for (int cell = 0; cell < 6; ++cell) discs &= at(cell, c / 2, c / 2)[3] > 0 && at(cell, 3, 3)[3] == 0;
        check(discs, "a ball seen from each way of each axis is a disc in each cell, and nothing in its corners");
        const auto box = sg::projection_guide(sg::unit_shape("box"), c);
        check(box[(static_cast<std::size_t>(c / 2) * 3 * c + c / 2) * 4 + 3] > 0, "a box fills its cells");
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
