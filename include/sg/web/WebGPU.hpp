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
    void recreate();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
