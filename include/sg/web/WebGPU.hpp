// GPU resources execute a freshly derived plan; they own no semantic objects.
#pragma once
#include "sg/render/ViewPlan.hpp"
#include <memory>
namespace sg::web {
enum class MissingShader { Refuse, BuiltinFallback };
class WebGPUView {
public:
    explicit WebGPUView(const std::string& canvas);
    ~WebGPUView();
    bool ready() const;
    std::vector<std::string> prepare(const StateGraph& graph,MissingShader policy=MissingShader::Refuse);
    std::vector<std::string> diagnostics() const;
    void render(const StateGraph& graph,const State& root,int width,int height,double frame_delta=0);
    void bind_surface(Key portal,Surface2D* surface);
    // The eye's room drawn at this part of the canvas's pixels each way
    // (0.5..1; 1, the canvas's own), laid on the whole of it as it is shown.
    void set_render_scale(float scale);
    // Feeds (a screen showing a world) drawn at most this often, Hz, each keeping its last picture
    // between (0: every frame) - a machine's choice; a panel's own `feed_rate` may ask for less.
    void set_feed_rate(float hz);
    void recreate();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
