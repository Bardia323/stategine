#include "sg/gl/World.hpp"
namespace sg::render {
bool GLWorldView::in_view(const Spatial3D& world, const Element& e, const Camera& cam, float aspect) const {
    const Pose p = pose_of(world, e);
    const float w = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f + 0.3f;
    const float h = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f + 0.3f;
    const gl::Vec3 c = to_vec3(p.position), side = to_vec3(across(p.yaw));
    const float far = static_cast<float>(world.params().num(Key{"far"}, 120.0));
    const gl::Vec3 f = gl::normalize(cam.forward);
    bool ahead = false;
    for (float a : {-1.0f, 1.0f})
        for (float b : {-1.0f, 1.0f}) {
            const gl::Vec3 corner = c + side * (a * w) + gl::Vec3{0, b * h, 0};
            const float along = gl::dot(corner - cam.eye, f);
            ahead = ahead || (along > 0.0f && along < far);
        }
    if (!ahead) return false;
    // And inside what the eye sees, not only in front of it: a screen well off
    // to one side is not looked at.
    if (aspect <= 0.0f) return true;
    const Frustum sees = frustum_of(gl::Mat4::perspective(cam.fov, aspect, kNear, far) * gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, cam.up));
    return sees.intersects_sphere({c.x, c.y, c.z}, std::sqrt(w * w + h * h));
}

int GLWorldView::feed_detail(float shown, int declared) {
    for (int d : {8, 4, 2})
        if (static_cast<float>(declared) / static_cast<float>(d) >= shown * 1.6f && declared / d >= 64) return d;
    return 1;
}

HalfSpace GLWorldView::far_side(const Spatial3D& host,const Element& portal,const Element& guest) const {
    return portal_clip(host,portal,eye_of(host),guest);
}

void GLWorldView::set_frame(const Pose& p) {
    frame_ = p;
    frame_matrix_ = gl::Mat4::translate({static_cast<float>(p.position.x),
                                         static_cast<float>(p.position.y),
                                         static_cast<float>(p.position.z)}) *
                    gl::Mat4::rotate_y(static_cast<float>(p.yaw));
}

const Element* GLWorldView::shown_in(const State& room, Key id) const {
    static const Key shows{"shows"};
    if (!graph_) return nullptr;
    for (const Element& e : room.elements())
        if (e.kind == kinds::portal && e.params.has(shows) && signal_of(e) == id && e.id != id) return &e;
    return nullptr;
}

auto GLWorldView::camera_of(const Element& cam) -> Camera {
    const auto view=view_camera(cam);Camera c;c.eye=to_vec3(view.eye);c.forward=to_vec3(view.forward);c.up=to_vec3(view.up);c.fov=static_cast<float>(view.fov)*3.14159265f/180.0f;return c;
}

auto GLWorldView::frustum_of(const gl::Mat4& vp) -> Frustum {
    // Retain the original float normalization at the presentation boundary.
    Frustum f;
    const auto row=[&](int i,int k) { return vp.m[k*4+i]; };
    for(int p=0;p<6;++p) {
        const int axis=p/2; const float sign=p%2?-1.0f:1.0f;
        float plane[4], len=0;
        for(int k=0;k<4;++k) { plane[k]=row(3,k)+sign*row(axis,k); if(k<3) len+=plane[k]*plane[k]; }
        len=std::sqrt(len);
        if(len>0) for(float& k:plane) k/=len;
        f.planes.push_back({{plane[0],plane[1],plane[2]},plane[3]});
    }
    return f;
}

auto GLWorldView::query_bounds(const RoomMatrix& local) const -> DrawBound {
    const gl::Mat4 w=frame_matrix_*local.m;
    float r2=0;
    for(int c=0;c<3;++c) r2+=w.m[c*4]*w.m[c*4]+w.m[c*4+1]*w.m[c*4+1]+w.m[c*4+2]*w.m[c*4+2];
    const float r=0.5f*std::sqrt(r2)*1.5f+0.05f;
    DrawBound bound;
    bound.centre={w.m[12],w.m[13],w.m[14]}; bound.radius=r;
    const spatial::V3 centre{bound.centre.x,bound.centre.y,bound.centre.z};
    bound.bounds=spatial::Aabb{centre,centre}.expanded(r+0.00001);
    return bound;
}

std::vector<std::size_t> GLWorldView::plan_draws(const Spatial3D& room,const Frustum& view) {
    auto& plan=draw_plans_[&room]; plan.unbounded.clear(); plan.portals.clear();
    std::vector<spatial::Index::Entry> bounds;
    const auto& elements=room.elements();
    bool same_frame=plan.framed;
    for(int i=0;i<16 && same_frame;++i) same_frame=plan.frame.m[i]==frame_matrix_.m[i];
    plan.bounds.resize(elements.size());
    const auto lists=draw_lists(room);
    plan.unbounded=lists.unbounded;plan.portals=lists.portals;
    for(const auto i:lists.solids) {
        const auto& e=elements[i];
        auto& placement=placed_of(room,e);
        if(!placement.boxed) placement.box=box_model(room,e),placement.boxed=true;
        auto& bound=plan.bounds[i];
        if(!same_frame || bound.element!=&e || bound.stamp!=placement.stamp) {
            bound=query_bounds(placement.box);bound.element=&e;bound.stamp=placement.stamp;
        }
        bounds.push_back({i,bound.bounds});
    }
    plan.visibility.update(std::move(bounds));
    auto visible=plan.visibility.visible(view);
    // Preserve the original float sphere test after conservative BVH pruning.
    visible.erase(std::remove_if(visible.begin(),visible.end(),[&](std::size_t i) {
        const auto& b=plan.bounds[i];
        // A speck: less than a pixel or so across at this distance on this
        // picture. (What is highlighted is never one.)
        if(lod_least_>0.0f && elements[i].id!=highlight_) {
            const gl::Vec3 away=b.centre-cam_eye_;
            const float d2=gl::dot(away,away),r=(b.radius-0.05f)/1.5f;
            if(r*lod_px_*r*lod_px_<lod_least_*lod_least_*d2) return true;
        }
        for(const auto& p:view.planes)
            if(static_cast<float>(p.normal.x)*b.centre.x+static_cast<float>(p.normal.y)*b.centre.y+static_cast<float>(p.normal.z)*b.centre.z+static_cast<float>(p.offset)<-b.radius) return true;
        return false;
    }),visible.end());
    visible.insert(visible.end(),plan.unbounded.begin(),plan.unbounded.end());
    std::sort(visible.begin(),visible.end());
    plan.frame=frame_matrix_; plan.framed=true;
    return visible;
}

bool GLWorldView::declared_world(const State& host,const Element& portal,const Spatial3D& guest) const {
    const StateGraph* g=graph_?graph_:(root_?root_->graph_:nullptr);
    return g && render::declared_world(*g,host,portal,guest);
}
bool GLWorldView::declared_feed(Key portal,const Spatial3D& guest) const {
    const StateGraph* g=graph_?graph_:(root_?root_->graph_:nullptr);
    return g && render::declared_feed(*g,portal,guest);
}
bool GLWorldView::declared_surface(const Element& portal,const Surface2D& surface) const {
    const StateGraph* g=graph_?graph_:(root_?root_->graph_:nullptr);
    if(!g) return false;
    auto& memo=surface_access_[&portal];
    if(memo.graph==g && memo.surface==&surface && memo.revision==g->revision() && memo.stamp==portal.params.stamp()) return memo.allowed;
    memo={g,&surface,g->revision(),portal.params.stamp(),render::declared_surface(*g,portal,surface)};
    return memo.allowed;
}
} // namespace sg::render
