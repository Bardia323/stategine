// The disk cache of derived data (sg/core/Cache.hpp): what is kept is read
// back whole, by its key alone; a damaged file, one of another key or none is
// a miss; and the modeller's meshes kept by one program are read back by
// another, the same as made - a changed recipe misses, a damaged file is made
// again.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "sg/core/Cache.hpp"
#include "sg/core/StateGraph.hpp"
#include "sg/domains/Modeler.hpp"

namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static const char* kRecipe = "box 1 2 3\nsub cyl 0.3 3 at=0,-0.5,0\n";
static const char* kOther = "box 1 2 3\nsub cyl 0.35 3 at=0,-0.5,0\n";

// A program of its own, the second one: makes `recipe` with the folder set,
// and says how it went - 0 if what it got is what building it makes, and
// the cache's counts on its output.
static int child(const std::string& dir, const std::string& recipe) {
    sg::cache::set_folder(dir);
    sg::StateGraph g;
    auto& m = g.add<sg::Modeler>(sg::Key{"m"});
    m.hear(sg::Event{sg::Modeler::set_event(), sg::Params{}.set("ops", recipe)});
    m.dispatch_pending();
    const sg::sculpt::Model& got = m.model();
    const sg::sculpt::Model made = sg::sculpt::build(recipe);
    bool same = got.parts.size() == made.parts.size() && got.triangles == made.triangles && got.errors == made.errors;
    for (std::size_t i = 0; same && i < got.parts.size(); ++i)
        same = got.parts[i].material == made.parts[i].material && got.parts[i].corners == made.parts[i].corners;
    const sg::cache::Stats s = sg::cache::stats();
    std::printf("hits=%zu misses=%zu stored=%zu rejected=%zu\n", s.hits, s.misses, s.stored, s.rejected);
    return same ? 0 : 3;
}

static std::string run_child(const std::string& self, const std::string& dir, const std::string& which) {
    const std::string out = dir + "/child.txt";
    const std::string cmd = "\"\"" + self + "\" child \"" + dir + "\" " + which + " > \"" + out + "\"\"";
    // (the outer quotes are cmd.exe's; a POSIX shell takes them as an empty word joined on)
#ifdef _WIN32
    const int rc = std::system(cmd.c_str());
#else
    const int rc = std::system(("\"" + self + "\" child \"" + dir + "\" " + which + " > \"" + out + "\"").c_str());
#endif
    std::ifstream in(out);
    std::string line;
    std::getline(in, line);
    return (rc == 0 ? "" : "bad ") + line;
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "child") return child(argv[2], std::string(argv[3]) == "other" ? kOther : kRecipe);

    const fs::path dir = fs::temp_directory_path() / ("sg_cache_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const std::string d = dir.string();

    // --- digests -------------------------------------------------------------------
    const sg::Digest a = sg::Hasher{}.text("ab").text("c").digest(), b = sg::Hasher{}.text("a").text("bc").digest();
    check(a != b, "a digest knows where one piece ends: ab|c is not a|bc");
    check(sg::Hasher{}.text("ab").text("c").digest() == a, "and is the same for the same pieces");
    check(sg::Hasher{}.number(0.1).digest() != sg::Hasher{}.number(0.1000000001).digest(), "a number is its exact bits");

    // --- the cache itself -------------------------------------------------------------
    std::string got;
    check(!sg::cache::load("t", a, got), "with no folder nothing is kept, nothing found");
    check(!sg::cache::store("t", a, "x"), "and nothing stored");
    sg::cache::set_folder(d);
    check(!sg::cache::load("t", a, got), "a key never stored is a miss");
    const std::string bytes = std::string("some\0bytes", 10) + std::string(100000, 'q');
    check(sg::cache::store("t", a, bytes), "stored");
    check(sg::cache::load("t", a, got) && got == bytes, "and read back whole, the same bytes");
    check(!sg::cache::load("t", b, got), "another key does not find them");
    const fs::path file = dir / "t" / (a.hex() + ".bin");
    {
        std::fstream f(file, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(200);
        f.put('Z');
    }
    const std::size_t rejected = sg::cache::stats().rejected;
    check(!sg::cache::load("t", a, got) && sg::cache::stats().rejected == rejected + 1, "a damaged file is refused");
    fs::resize_file(file, 100);
    check(!sg::cache::load("t", a, got), "and so is a cut-off one");
    fs::copy_file(dir / "t" / (a.hex() + ".bin"), dir / "t" / (b.hex() + ".bin"));
    check(!sg::cache::load("t", b, got), "and one of another key under this one's name");
    check(sg::cache::store("t", a, bytes) && sg::cache::load("t", a, got) && got == bytes, "stored again over it, it is whole again");
    bool loose = false;
    for (const auto& e : fs::directory_iterator(dir / "t")) loose = loose || e.path().extension() == ".tmp";
    check(!loose, "and nothing is left half-written beside it");

    // --- the modeller's meshes, between two programs ----------------------------------------
    const std::string self = fs::absolute(argv[0]).string();
    std::string said = run_child(self, d, "this");
    check(said.find("bad") == std::string::npos && said.find("stored=1") != std::string::npos,
          "one program makes a recipe's mesh and keeps it: " + said);
    said = run_child(self, d, "this");
    check(said.find("bad") == std::string::npos && said.find("hits=1") != std::string::npos && said.find("stored=0") != std::string::npos,
          "another reads it back, the same as building it: " + said);
    said = run_child(self, d, "other");
    check(said.find("bad") == std::string::npos && said.find("hits=0") != std::string::npos && said.find("stored=1") != std::string::npos,
          "a changed recipe misses, and is made: " + said);
    for (const auto& e : fs::directory_iterator(dir / "modeler")) {
        std::fstream f(e.path(), std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(60);
        f.put('\x7f');
    }
    said = run_child(self, d, "this");
    check(said.find("bad") == std::string::npos && said.find("rejected=1") != std::string::npos && said.find("stored=1") != std::string::npos,
          "a damaged mesh is refused, made again as it should be, and kept again: " + said);
    said = run_child(self, d, "this");
    check(said.find("bad") == std::string::npos && said.find("hits=1") != std::string::npos, "and is read back after: " + said);

    sg::cache::set_folder("");
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf(failures ? "%d FAILED\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
