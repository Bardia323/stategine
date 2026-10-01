// A const view of Pong, and physical keyboard input outside the world.
#pragma once
#include "sg/core/State.hpp"
#include "sg/net/Prediction.hpp"
#include "sg/gl/Window.hpp"
#include "sg/gl/Renderer.hpp"
#include <filesystem>

namespace sg::examples::pong {
class View {
public:
    View(int player, const std::string& backend);
    ~View();
    bool poll();
    double input() const;
    void draw(const State& game, const State& network, const net::PredictedValues* prediction = nullptr, std::uint64_t lead = 0);
    void shot(const std::filesystem::path& path) const;
private:
    void box(float x, float y, float w, float h, float r, float g, float b);
    void text(const std::string& text, float x, float y, float size, float r, float g, float b);
    int player_;
    std::string backend_;
    gl::Window window_;
    gl::Program program_;
    gl::GLuint vao_ = 0, buffer_ = 0;
    std::vector<float> vertices_;
};
}
