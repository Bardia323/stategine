#include "View.hpp"
#include <array>
#include <fstream>
#include <map>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace sg::examples::pong {
namespace {
const char* vertex = R"(#version 330 core
layout(location=0) in vec2 position;
layout(location=1) in vec3 color;
out vec3 ink;
void main(){ gl_Position=vec4(position,0,1); ink=color; }
)";
const char* fragment = R"(#version 330 core
in vec3 ink;
out vec4 outColor;
void main(){ outColor=vec4(ink,1); }
)";
const std::map<char, std::array<unsigned,7>> font = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,14}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
    {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,21,19,17,17,17}},
    {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
    {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
    {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
    {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
    {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
    {'/',{1,1,2,4,8,16,16}}, {'-',{0,0,0,31,0,0,0}},
    {'.',{0,0,0,0,0,12,12}}, {':',{0,12,12,0,12,12,0}},
    {'>',{16,8,4,2,4,8,16}}, {'+',{0,4,4,31,4,4,0}}
};
}
View::View(int player, const std::string& backend)
    : player_(player), backend_(backend), window_(720, 600, "Stategine Pong | P"+std::to_string(player)+" | "+backend+" | W/S + Up/Down"), program_(vertex, fragment, "Pong") {
    using namespace gl;
    // Neither rendering nor socket polling blocks the other peer's numerical work.
    glfwSwapInterval(0);
    const auto* monitor = glfwGetVideoMode(glfwGetPrimaryMonitor());
    const int width = monitor ? monitor->width : 1600;
    const int height = monitor ? monitor->height : 900;
    const int w = std::min(720, (width-60)/2);
    const int h = std::min(600, height-120);
    glfwSetWindowSize(window_.handle(), w, h);
    glfwSetWindowPos(window_.handle(), (width-2*w-20)/2 + player*(w+20), (height-h)/2);
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &buffer_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, false, 5*sizeof(float), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, false, 5*sizeof(float), reinterpret_cast<void*>(2*sizeof(float)));
    glDisable(GL_DEPTH_TEST);
}
View::~View() { gl::glDeleteBuffers(1, &buffer_); gl::glDeleteVertexArrays(1, &vao_); gl::glDeleteProgram(program_.id()); }
bool View::poll() { window_.poll(); if (window_.pressed(GLFW_KEY_ESCAPE)) window_.close(); return !window_.should_close(); }
double View::input() const {
#ifdef _WIN32
    // Both programs read their own physical controls while either Pong view
    // has focus. No instance routes input for or writes to the other one.
    char title[256]{}; GetWindowTextA(GetForegroundWindow(), title, sizeof(title));
    if (std::string(title).rfind("Stategine Pong", 0) != 0) return 0;
    const auto held = [](int key) { return (GetAsyncKeyState(key)&0x8000) != 0; };
    return static_cast<double>(held(player_ == 0 ? 'W' : VK_UP)) - static_cast<double>(held(player_ == 0 ? 'S' : VK_DOWN));
#else
    return static_cast<double>(window_.down(player_ == 0 ? GLFW_KEY_W : GLFW_KEY_UP)) - static_cast<double>(window_.down(player_ == 0 ? GLFW_KEY_S : GLFW_KEY_DOWN));
#endif
}
void View::box(float x, float y, float w, float h, float r, float g, float b) {
    const float left = x-w/2, right = x+w/2, bottom = y-h/2, top = y+h/2;
    const float points[] = {left,bottom, right,bottom, right,top, left,bottom, right,top, left,top};
    for (int i = 0; i < 12; i += 2) vertices_.insert(vertices_.end(), {points[i],points[i+1],r,g,b});
}
void View::text(const std::string& str, float x, float y, float size, float r, float g, float b) {
    for (auto c : str) {
        const auto glyph = font.find(c);
        if (glyph != font.end()) for (int row = 0; row < 7; ++row) for (int col = 0; col < 5; ++col)
            if (glyph->second[row] & (1u<<(4-col))) box(x+col*size, y-row*size, size*.87f, size*.87f, r,g,b);
        x += size*6;
    }
}
void View::draw(const State& game, const State& network, const net::PredictedValues* prediction, std::uint64_t lead) {
    using namespace gl;
    vertices_.clear();
    glViewport(0,0,window_.width(),window_.height()); glClearColor(.025f,.035f,.07f,1); glClear(GL_COLOR_BUFFER_BIT);
    box(0,0,1.96f,1.52f,.042f,.063f,.10f);
    box(0,.765f,1.96f,.006f,.17f,.24f,.30f); box(0,-.765f,1.96f,.006f,.17f,.24f,.30f);
    for (int i = -8; i <= 8; ++i) box(0,i*.086f,.004f,.034f,.13f,.19f,.25f);
    text("PONG / PEER "+std::to_string(player_), -.95f,.94f,.013f,.65f,.77f,.86f);
    text(backend_ == "cuda" ? "GPU" : "CPU", .72f,.94f,.013f,.32f,.78f,.70f);
    for (int i = 0; i < 2; ++i) {
        const auto& p = game.element(i == 0 ? "left" : "right").params;
        const float red = i == 0 ? 1.f : .3f, green = i == 0 ? .62f : .84f, blue = i == 0 ? .34f : 1.f;
        box(static_cast<float>(p.num("x")),static_cast<float>(prediction ? (*prediction)[i == 0 ? 4 : 6] : p.num("y")),.025f,.22f,red,green,blue);
        if (i == player_) box(static_cast<float>(p.num("x")), -.81f,.10f,.006f,red,green,blue);
        text(std::to_string(static_cast<int>(game.params().num(i == 0 ? "left_score" : "right_score"))), i == 0 ? -.28f : .21f,.66f,.028f,red,green,blue);
    }
    const auto& ball = game.element("ball").params;
    box(static_cast<float>(prediction ? (*prediction)[0] : ball.num("x")),static_cast<float>(prediction ? (*prediction)[1] : ball.num("y")),.022f,.0264f,.91f,.95f,1.f);
    text("W/S : LEFT     UP/DOWN : RIGHT", -.94f,-.86f,.0105f,.57f,.69f,.79f);
    text("VERIFIED "+std::to_string(static_cast<int>(game.params().num("epoch")))+"  PREDICT +"+std::to_string(lead)+"  ESC CLOSE", -.94f,-.94f,.009f,.34f,.49f,.61f);
    (void)network;
    if (game.params().num("epoch") == 0) text("WAITING FOR BOTH PEERS", -.53f,.12f,.017f,.67f,.81f,.87f);
    program_.use(); glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(vertices_.size()*sizeof(float)),vertices_.data(),GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(vertices_.size()/5));
    window_.swap();
}
void View::shot(const std::filesystem::path& path) const {
    using namespace gl;
    const auto w = window_.width(), h = window_.height();
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w)*h*4);
    glReadBuffer(GL_FRONT); glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()); glReadBuffer(GL_BACK);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary); out << "P6\n" << w << ' ' << h << "\n255\n";
    for (int y = h-1; y >= 0; --y) for (int x = 0; x < w; ++x) out.write(reinterpret_cast<const char*>(pixels.data()+4*(static_cast<std::size_t>(y)*w+x)),3);
    if (!out) throw std::runtime_error("Pong screenshot failed");
}
}
