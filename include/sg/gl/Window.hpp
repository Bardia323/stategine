// Stategine - GLFW window plus a frame-by-frame input snapshot.
#pragma once

#include <array>
#include <stdexcept>
#include <string>

// Keep GLFW from dragging in the system GL header: its macros would collide
// with the constants in GL.hpp.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "sg/gl/GL.hpp"

namespace sg::gl {

class Window {
public:
    Window(int w, int h, const std::string& title) {
        if (!glfwInit()) throw std::runtime_error("glfwInit failed");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_SAMPLES, 4);
        win_ = glfwCreateWindow(w, h, title.c_str(), nullptr, nullptr);
        if (!win_) {
            glfwTerminate();
            throw std::runtime_error("window creation failed (needs OpenGL 3.3)");
        }
        glfwMakeContextCurrent(win_);
        glfwSwapInterval(1);
        load(reinterpret_cast<ProcLoader>(glfwGetProcAddress));
        glfwGetCursorPos(win_, &mx_, &my_);
        prev_.fill(false);
        now_.fill(false);
    }

    ~Window() {
        if (win_) glfwDestroyWindow(win_);
        glfwTerminate();
    }

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const { return glfwWindowShouldClose(win_); }
    void close() { glfwSetWindowShouldClose(win_, 1); }
    void swap() { glfwSwapBuffers(win_); }

    int width() const {
        int w = 0, h = 0;
        glfwGetFramebufferSize(win_, &w, &h);
        return w;
    }

    int height() const {
        int w = 0, h = 0;
        glfwGetFramebufferSize(win_, &w, &h);
        return h;
    }

    float aspect() const {
        const int h = height();
        return h > 0 ? static_cast<float>(width()) / static_cast<float>(h) : 1.0f;
    }

    // Call once per frame, before reading input.
    void poll() {
        prev_ = now_;
        glfwPollEvents();
        for (std::size_t k = 0; k < now_.size(); ++k)
            now_[k] = glfwGetKey(win_, static_cast<int>(k)) == GLFW_PRESS;
        double x = 0, y = 0;
        glfwGetCursorPos(win_, &x, &y);
        dx_ = x - mx_;
        dy_ = y - my_;
        mx_ = x;
        my_ = y;
    }

    bool down(int key) const { return valid(key) && now_[static_cast<std::size_t>(key)]; }

    // True only on the frame the key goes down.
    bool pressed(int key) const {
        return valid(key) && now_[static_cast<std::size_t>(key)] &&
               !prev_[static_cast<std::size_t>(key)];
    }

    double mouse_dx() const { return dx_; }
    double mouse_dy() const { return dy_; }

    void capture_mouse(bool on) {
        glfwSetInputMode(win_, GLFW_CURSOR, on ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        captured_ = on;
    }

    bool mouse_captured() const { return captured_; }

    GLFWwindow* handle() const { return win_; }

private:
    static bool valid(int key) { return key >= 0 && key <= GLFW_KEY_LAST; }

    GLFWwindow* win_ = nullptr;
    std::array<bool, GLFW_KEY_LAST + 1> now_{};
    std::array<bool, GLFW_KEY_LAST + 1> prev_{};
    double mx_ = 0, my_ = 0, dx_ = 0, dy_ = 0;
    bool captured_ = false;
};

}  // namespace sg::gl
