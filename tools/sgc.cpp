// sgc: the Stategine DSL compiler.
//
//   sgc [-o out.cpp] [--name build_name] [--ns namespace] [--deps out.d] [--facts out.facts] file.sg...
//
// Reads the sources, resolves them, holds them to the ontology, and writes the
// C++17 that builds the same construction through the engine's own API. A
// program that breaks a rule is not compiled: the rule is said, where.
//
// The compiler builds nothing itself. The engine's laws and `sg::verify` check
// the graph the generated code makes.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "sg/dsl/Compile.hpp"
#include "sg/dsl/Emit.hpp"
#include "sg/dsl/Facts.hpp"
#include "sg/dsl/Parse.hpp"

namespace fs = std::filesystem;

namespace {

bool slurp(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out_path, name, ns = "sgen", deps_path, facts_path;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "sgc: %s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-o") out_path = next();
        else if (a == "--name") name = next();
        else if (a == "--ns") ns = next();
        else if (a == "--deps") deps_path = next();
        else if (a == "--facts") facts_path = next();
        else files.push_back(a);
    }
    if (files.empty()) {
        std::fprintf(stderr, "usage: sgc [-o out.cpp] [--name n] [--ns ns] [--deps out.d] [--facts out.facts] file.sg...\n");
        return 2;
    }
    if (name.empty()) name = fs::path(out_path.empty() ? files.front() : out_path).stem().string();

    std::vector<sg::dsl::Program> programs;
    std::vector<sg::dsl::Diagnostic> errors;
    for (const std::string& f : files) {
        std::string text;
        if (!slurp(f, text)) {
            std::fprintf(stderr, "sgc: cannot read %s\n", f.c_str());
            return 2;
        }
        sg::dsl::Parsed p = sg::dsl::parse(text, f);
        errors.insert(errors.end(), p.errors.begin(), p.errors.end());
        programs.push_back(std::move(p.program));
    }
    std::vector<std::string> read;  // files a source names with file("..."): the output depends on them too
    if (errors.empty()) {
        sg::dsl::Options opt;
        opt.read_file = [&](const std::string& path, const std::string& from, std::string& body) {
            const fs::path p = fs::path(from).parent_path() / path;
            if (!slurp(p.string(), body)) return false;
            read.push_back(p.generic_string());
            return true;
        };
        sg::dsl::Compiled c = sg::dsl::compile(programs, opt);
        errors = c.errors;
        if (errors.empty()) {
            const std::string cpp = sg::dsl::emit_cpp(c.plan, name, ns);
            if (out_path.empty()) {
                std::cout << cpp;
            } else {
                fs::create_directories(fs::path(out_path).parent_path());
                std::ofstream(out_path, std::ios::binary) << cpp;
            }
            if (!facts_path.empty()) std::ofstream(facts_path, std::ios::binary) << sg::dsl::to_text(sg::dsl::facts(c.plan));
        }
    }
    if (!errors.empty()) {
        for (const sg::dsl::Diagnostic& d : errors) std::fprintf(stderr, "%s\n", d.str().c_str());
        std::fprintf(stderr, "sgc: %zu error%s, nothing written\n", errors.size(), errors.size() == 1 ? "" : "s");
        return 1;
    }
    if (!deps_path.empty() && !out_path.empty()) {
        std::ofstream d(deps_path, std::ios::binary);
        d << fs::path(out_path).generic_string() << ":";
        for (const std::string& f : files) d << " " << fs::path(f).generic_string();
        for (const std::string& f : read) d << " " << f;
        d << "\n";
    }
    return 0;
}
