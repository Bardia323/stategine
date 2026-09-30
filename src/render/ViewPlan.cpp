#include "sg/render/GLWorld.hpp"
namespace sg::render {
bool GLWorldView::in_view(const Spatial3D& world, const Element& e, const Camera& cam) const {
    const Pose p = pose_of(world, e);
    const float w = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f + 0.3f;
    const float h = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f + 0.3f;
    const gl::Vec3 c = to_vec3(p.position), side = to_vec3(across(p.yaw));
    const float far = static_cast<float>(world.params().num(Key{"far"}, 120.0));
    const gl::Vec3 f = gl::normalize(cam.forward);
    for (float a : {-1.0f, 1.0f})
        for (float b : {-1.0f, 1.0f}) {
            const gl::Vec3 corner = c + side * (a * w) + gl::Vec3{0, b * h, 0};
            const float along = gl::dot(corner - cam.eye, f);
            if (along > 0.0f && along < far) return true;
        }
    return false;
}

HalfSpace GLWorldView::far_side(const Spatial3D& host, const Element& portal, const Element& gc) const {
    const Pose p = world_pose(host, portal);
    const Vec3d n = heading(p.yaw);
    const double inset = portal.params.num(Key{"inset"}, 0.06);
    const Vec3d at{p.position.x + n.x * inset, p.position.y, p.position.z + n.z * inset};
    const Element& hc = eye_of(host);
    const double turn = gc.params.num(keys::yaw) - hc.params.num(keys::yaw);
    const Vec3d he = position_of(hc), ge = position_of(gc);
    const Vec3d off = rotate_xz({at.x - he.x, at.y - he.y, at.z - he.z}, turn);
    const Vec3d q{ge.x + off.x, ge.y + off.y, ge.z + off.z};
    const Vec3d m = rotate_xz(n, turn);
    return HalfSpace{{-m.x, -m.y, -m.z}, m.x * q.x + m.y * q.y + m.z * q.z};
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
    Camera c;
    c.eye = to_vec3(position_of(cam));
    c.forward = to_vec3(forward_of(cam));
    c.fov = static_cast<float>(cam.params.num(keys::fov, 70.0)) * 3.14159265f / 180.0f;
    // `roll`: the head tipped about the line of sight (radians).
    if (const float roll = static_cast<float>(cam.params.num(keys::roll, 0.0)); roll != 0.0f) {
        const gl::Vec3 side = gl::normalize(gl::cross(c.forward, gl::Vec3{0, 1, 0}));
        const gl::Vec3 up = gl::cross(side, c.forward);
        c.up = up * std::cos(roll) + side * std::sin(roll);
    }
    return c;
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
    for(std::size_t i=0;i<elements.size();++i) {
        const auto& e=elements[i]; if(!e.alive) continue;
        if(e.kind==kinds::mesh || e.kind==kinds::wall) {
            auto& placement=placed_of(room,e);
            if(!placement.boxed) placement.box=box_model(room,e),placement.boxed=true;
            auto& bound=plan.bounds[i];
            if(!same_frame || bound.element!=&e || bound.stamp!=placement.stamp) {
                bound=query_bounds(placement.box); bound.element=&e; bound.stamp=placement.stamp;
            }
            bounds.push_back({i,bound.bounds});
        }
        else if(e.kind==kinds::portal) plan.portals.push_back(i);
        else if(e.kind==kinds::light || e.kind==terrain_kind()) plan.unbounded.push_back(i);
    }
    plan.visibility.update(std::move(bounds));
    auto visible=plan.visibility.visible(view);
    // Preserve the original float sphere test after conservative BVH pruning.
    visible.erase(std::remove_if(visible.begin(),visible.end(),[&](std::size_t i) {
        const auto& b=plan.bounds[i];
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
    if(!g) return false;
    for(const auto& em:g->embeddings())
        if(em.host==host.id() && em.portal==portal.id && em.guest==guest.id() && em.open) return true;
    for(const auto& seam:g->seams()) {
        if(seam.a==host.id() && seam.b==guest.id() && std::find(seam.boundary_a.begin(),seam.boundary_a.end(),portal.id)!=seam.boundary_a.end()) return true;
        if(seam.b==host.id() && seam.a==guest.id() && std::find(seam.boundary_b.begin(),seam.boundary_b.end(),portal.id)!=seam.boundary_b.end()) return true;
    }
    return false;
}
bool GLWorldView::declared_feed(Key portal,const Spatial3D& guest) const {
    const StateGraph* g=graph_?graph_:(root_?root_->graph_:nullptr);
    if(!g) return false;
    for(const auto& em:g->embeddings()) {
        if(em.portal!=portal || em.guest!=guest.id() || !em.open) continue;
        const State* host=g->find(em.host);
        const Element* panel=host?host->find(portal):nullptr;
        if(panel && panel->alive && panel->params.num("feed",0)>0.5) return true;
    }
    return false;
}
bool GLWorldView::declared_surface(const Element& portal,const Surface2D& surface) const {
    const StateGraph* g=graph_?graph_:(root_?root_->graph_:nullptr);
    if(!g) return false;
    auto& memo=surface_access_[&portal];
    if(memo.graph==g && memo.surface==&surface && memo.revision==g->revision() && memo.stamp==portal.params.stamp()) return memo.allowed;
    memo={g,&surface,g->revision(),portal.params.stamp(),false};
    // A panel can show its surface or an output reached through declared interfaces.
    // Closed 2D panels retain their picture; open is the host's interaction data.
    std::vector<Key> pending, seen;
    const Key signal=signal_of(portal);
    for(const auto& em:g->embeddings()) if(em.portal==signal) pending.push_back(em.guest);
    while(!pending.empty()) {
        const Key id=pending.back(); pending.pop_back();
        if(id==surface.id()) return memo.allowed=g->find(id)==&surface;
        if(std::find(seen.begin(),seen.end(),id)!=seen.end()) continue;
        seen.push_back(id);
        for(const auto& em:g->embeddings()) if(em.host==id) pending.push_back(em.guest);
        for(const auto& entry:g->functors()) if(entry.second.from()==id) pending.push_back(entry.second.to());
    }
    return false;
}
} // namespace sg::render
