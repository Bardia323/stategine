#include "sg/gl/Shots.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "sg/domains/Modeler.hpp"
#include "sg/gl/GL.hpp"

namespace sg::render {

namespace {

std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

void write_png(const std::string& path, const std::vector<unsigned char>& rgb, int w, int h) {
    std::ofstream o(path, std::ios::binary);
    o << sculpt::png(rgb, w, h);
    std::printf("wrote %s (%dx%d)\n", path.c_str(), w, h);
}

}  // namespace

std::vector<Shots::Shot> Shots::parse(const std::string& text, int frames, std::vector<std::string>* errors) {
    std::vector<Shot> out;
    std::istringstream in(text);
    std::string line;
    for (int n = 1; std::getline(in, line); ++n) {
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        const auto colon = line.find(':');
        Shot s{trim(line.substr(0, colon == std::string::npos ? 0 : colon)), {}, frames};
        if (colon == std::string::npos || s.name.empty() || s.name.find_first_of(" \t/\\") != std::string::npos) {
            if (errors) errors->push_back("line " + std::to_string(n) + ": a shot is <name>: <command>; <command> [frames=N]");
            continue;
        }
        std::string rest = line.substr(colon + 1);
        // A last word `frames=N` says how long it is held.
        const auto f = rest.rfind("frames=");
        if (f != std::string::npos && (f == 0 || rest[f - 1] == ' ' || rest[f - 1] == '\t') && trim(rest.substr(f)).find(' ') == std::string::npos) {
            s.frames = std::max(1, std::atoi(rest.c_str() + f + 7));
            rest = rest.substr(0, f);
        }
        std::istringstream cmds(rest);
        for (std::string c; std::getline(cmds, c, ';');)
            if (!trim(c).empty()) s.setup.push_back(trim(c));
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<Shots::Shot> Shots::load(const std::string& source, int frames, std::vector<std::string>* errors) {
    std::error_code ec;
    if (source.find('\n') == std::string::npos && std::filesystem::is_regular_file(source, ec)) {
        std::ifstream f(source);
        std::stringstream ss;
        ss << f.rdbuf();
        return parse(ss.str(), frames, errors);
    }
    return parse(source, frames, errors);
}

Shots::Shots(std::vector<Shot> shots, std::string dir) : shots_(std::move(shots)), dir_(std::move(dir)) {
    std::filesystem::create_directories(dir_);
}

void Shots::before(const std::function<std::string(const std::string&)>& run) {
    if (done() || frame_ != 0) return;
    for (const std::string& c : shots_[at_].setup) {
        const std::string said = run(c);
        std::printf("[%s] > %s%s%s\n", shots_[at_].name.c_str(), c.c_str(), said.empty() ? "" : "\n  ", said.c_str());
    }
}

void Shots::after(int w, int h) {
    if (done()) return;
    if (++frame_ < shots_[at_].frames) return;
    Picture p{shots_[at_].name, w, h, std::vector<unsigned char>(static_cast<std::size_t>(w) * h * 3)};
    std::vector<unsigned char> up(p.rgb.size());
    gl::glReadPixels(0, 0, w, h, gl::GL_RGB, gl::GL_UNSIGNED_BYTE, up.data());
    // (GL's rows run up from the bottom; a picture's run down.)
    for (int y = 0; y < h; ++y)
        std::copy_n(up.data() + static_cast<std::size_t>(h - 1 - y) * w * 3, static_cast<std::size_t>(w) * 3,
                    p.rgb.data() + static_cast<std::size_t>(y) * w * 3);
    write_png(dir_ + "/" + p.name + ".png", p.rgb, w, h);
    taken_.push_back(std::move(p));
    ++at_, frame_ = 0;
    if (done() && taken_.size() > 1) sheet();
}

// All of them on one picture, each half its size, in rows as near square as
// they go: to compare at a glance.
void Shots::sheet() const {
    const int n = static_cast<int>(taken_.size());
    const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    const int rows = (n + cols - 1) / cols;
    int cw = 0, ch = 0;
    for (const Picture& p : taken_) cw = std::max(cw, p.w / 2), ch = std::max(ch, p.h / 2);
    std::vector<unsigned char> rgb(static_cast<std::size_t>(cw) * cols * ch * rows * 3, 0);
    const int W = cw * cols;
    for (int i = 0; i < n; ++i) {
        const Picture& p = taken_[static_cast<std::size_t>(i)];
        const int ox = (i % cols) * cw, oy = (i / cols) * ch;
        for (int y = 0; y < p.h / 2; ++y)
            for (int x = 0; x < p.w / 2; ++x)
                for (int c = 0; c < 3; ++c) {
                    // Each pixel the mean of the four it stands for.
                    const auto at = [&](int dx, int dy) {
                        return static_cast<int>(p.rgb[(static_cast<std::size_t>(y * 2 + dy) * p.w + (x * 2 + dx)) * 3 + c]);
                    };
                    rgb[(static_cast<std::size_t>(oy + y) * W + ox + x) * 3 + c] =
                        static_cast<unsigned char>((at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
                }
    }
    write_png(dir_ + "/sheet.png", rgb, W, ch * rows);
}

}  // namespace sg::render
