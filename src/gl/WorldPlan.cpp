#include "sg/gl/World.hpp"
namespace sg::render {
bool GLWorldView::in_view(const Spatial3D& world, const Element& e, const Camera& cam, float aspect) const {
    const Pose p = pose_of(world, e);
    // A ball: whether any of it is before the eye and in what it sees.
    if (const double r = e.params.num(Key{"ball"}, 0.0); r > 0.0) {
        const gl::Vec3 c = to_vec3(p.position);
        const float far = static_cast<float>(world.params().num(Key{"far"}, 120.0));
        const Frustum sees = frustum_of(projection_of(cam, aspect > 0.0f ? aspect : 1.0f, kNear, far) * gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, cam.up));
        return sees.intersects_sphere({c.x, c.y, c.z}, static_cast<float>(r));
    }
    const float w = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f + 0.3f;
    const float h = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f + 0.3f;
    const gl::Vec3 c = to_vec3(p.position), side = to_vec3(across_of(p)), up = to_vec3(up_of(p));
    const float far = static_cast<float>(world.params().num(Key{"far"}, 120.0));
    const gl::Vec3 f = gl::normalize(cam.forward);
    bool ahead = false;
    for (float a : {-1.0f, 1.0f})
        for (float b : {-1.0f, 1.0f}) {
            const gl::Vec3 corner = c + side * (a * w) + up * (b * h);
            const float along = gl::dot(corner - cam.eye, f);
            ahead = ahead || (along > 0.0f && along < far);
        }
    if (!ahead) return false;
    // And inside what the eye sees, not only in front of it: a screen well off
    // to one side is not looked at.
    if (aspect <= 0.0f) return true;
    const Frustum sees = frustum_of(projection_of(cam, aspect, kNear, far) * gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, cam.up));
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

double GLWorldView::slab_of(const Element& portal) {
    // The wall's depth (`depth`), and a little more: the near plane cuts a
    // leaf the eye comes within a few centimetres of.
    return std::max(portal.params.num(Key{"depth"}, 0.34), 0.2) + 2.0 * static_cast<double>(kNear);
}

bool GLWorldView::shut_to(const Element& portal, const Vec3d& eye) {
    if (portal.params.num(Key{"closed"}, 0.0) < 0.5) return false;
    // (Measured as opens_from measures which side the eye is on.)
    const Pose p = local_pose(portal);
    const Vec3d face = upright(p) ? heading(p.yaw) : facing(p);
    return std::fabs(dot(eye - p.position, face)) > slab_of(portal);
}

auto GLWorldView::through(const Element& portal, const Spatial3D& guest, const Element& back, const Camera& cam) const -> Through {
    Through t;
    if (cam.ortho > 0.0f || portal.params.has(Key{"ball"}) || back.params.has(Key{"ball"}) || is_screen(portal)) return t;
    // The far doorway is the near one identified (a seam): its plane, its
    // outline, as the carried eye sees them.
    const Pose p = pose_of(guest, back);
    const Vec3d c = p.position, n = facing(p), side = across_of(p), up = up_of(p);
    const Vec3d eye{cam.eye.x, cam.eye.y, cam.eye.z};
    const double d = dot(eye - c, n), away = std::fabs(d);
    const double inset = portal_inset(portal);
    if (away < std::max(slab_of(portal), slab_of(back)) || away <= 2.0 * inset) return t;
    // The doorway's picture is drawn `inset` nearer the eye than its plane
    // (portal_inset): seen from the eye, that is its outline on the plane
    // grown about the foot of the eye by away / (away - inset).
    const Vec3d toward = n * (d > 0.0 ? 1.0 : -1.0), foot = eye - n * d;
    const double grow = away / (away - inset);
    const double w = back.params.num(keys::w, 3.0) * 0.5, h = back.params.num(keys::h, 2.0) * 0.5;
    const Vec3d f = unit(Vec3d{cam.forward.x, cam.forward.y, cam.forward.z});
    const Vec3d middle = foot + (c - foot) * grow;
    // Round the outline, corner after corner.
    const double a[4] = {-1.0, 1.0, 1.0, -1.0}, b[4] = {-1.0, -1.0, 1.0, 1.0};
    Vec3d corner[4];
    double nearest = 1e30;
    for (int i = 0; i < 4; ++i) {
        const Vec3d q = c + side * (a[i] * w) + up * (b[i] * h);
        nearest = std::min(nearest, dot(q + toward * inset - eye, f));
        const Vec3d s = foot + (q - foot) * grow;
        // A little wider than it is (a centimetre and a hundredth of how far
        // off), so nothing a pixel inside its edge is lost to rounding.
        corner[i] = s + unit(s - middle) * (0.01 + 0.01 * length(s - eye));
    }
    // Everything the view shows is past the doorway, so no nearer than its
    // nearest corner: the near plane just short of that.
    if (nearest > static_cast<double>(kNear)) t.znear = std::max(kNear, static_cast<float>(0.9 * nearest));
    // The pyramid's sides: through the eye and each edge, facing in.
    for (int i = 0; i < 4; ++i) {
        const Vec3d m = cross(corner[i] - eye, corner[(i + 1) % 4] - eye);
        const double len = length(m);
        if (len < 1e-12) continue;
        Vec3d k = m * (1.0 / len);
        if (dot(k, middle - eye) < 0.0) k = k * -1.0;
        t.sides.push_back(spatial::HalfSpace{{k.x, k.y, k.z}, -dot(k, eye)});
    }
    return t;
}

void GLWorldView::set_frame(const Pose& p) {
    frame_ = p;
    frame_matrix_ = gl::Mat4::translate({static_cast<float>(p.position.x),
                                         static_cast<float>(p.position.y),
                                         static_cast<float>(p.position.z)}) *
                    gl::Mat4::rotate_y(static_cast<float>(p.yaw)) * gl::Mat4::rotate_z(static_cast<float>(p.pitch)) *
                    gl::Mat4::rotate_x(static_cast<float>(p.roll));
}

const Element* GLWorldView::shown_in(const State& room, Key id) const {
    static const Key shows{"shows"};
    if (!graph_) return nullptr;
    for (const Element& e : room.elements())
        if (e.kind == kinds::portal && e.params.has(shows) && signal_of(e) == id && e.id != id) return &e;
    return nullptr;
}

auto GLWorldView::camera_of(const Element& cam) -> Camera {
    const auto view=view_camera(cam);Camera c;c.eye=to_vec3(view.eye);c.forward=to_vec3(view.forward);c.up=to_vec3(view.up);c.fov=static_cast<float>(view.fov)*3.14159265f/180.0f;
    c.ortho=static_cast<float>(cam.params.num(Key{"ortho"},0.0));
    return c;
}

gl::Mat4 GLWorldView::projection_of(const Camera& cam, float aspect, float znear, float zfar) {
    if (cam.ortho > 0.0f) return gl::Mat4::ortho(-cam.ortho * aspect, cam.ortho * aspect, -cam.ortho, cam.ortho, znear, zfar);
    return gl::Mat4::perspective(cam.fov, aspect, znear, zfar);
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

std::vector<std::size_t> GLWorldView::plan_draws(const Spatial3D& room,const Frustum& given,const gl::Vec3* shift) {
    // Through a doorway, only what is in the pyramid from the eye through
    // it and past its plane can be seen (cull_): the planes the GPU clips
    // by, used here too, so what they would cut away is never sent.
    Frustum narrowed;
    if(!cull_.empty()) {
        narrowed=given;
        narrowed.planes.insert(narrowed.planes.end(),cull_.begin(),cull_.end());
    }
    const Frustum& view=cull_.empty()?given:narrowed;
    auto& plan=draw_plans_[&room];
    const auto& elements=room.elements();
    // Whether a thing, its bounds moved by `o`, is to be drawn: in what is
    // seen, and not a speck - less than a pixel or so across at this
    // distance on this picture (what is highlighted is never one).
    const auto shown=[&](std::size_t i,const gl::Vec3& o) {
        const auto& b=plan.bounds[i];
        const gl::Vec3 c=b.centre+o;
        if(lod_least_>0.0f && elements[i].id!=highlight_) {
            const gl::Vec3 away=c-cam_eye_;
            const float d2=gl::dot(away,away),r=(b.radius-0.05f)/1.5f;
            if(r*lod_px_*r*lod_px_<lod_least_*lod_least_*d2) return false;
        }
        for(const auto& p:view.planes)
            if(static_cast<float>(p.normal.x)*c.x+static_cast<float>(p.normal.y)*c.y+static_cast<float>(p.normal.z)*c.z+static_cast<float>(p.offset)<-b.radius) return false;
        return true;
    };
    if(shift && plan.framed) {
        const gl::Vec3 o=*shift;
        Frustum back=view;
        for(auto& p:back.planes) p.offset+=p.normal.x*o.x+p.normal.y*o.y+p.normal.z*o.z;
        auto visible=plan.visibility.visible(back);
        visible.erase(std::remove_if(visible.begin(),visible.end(),[&](std::size_t i) { return plan.mover[i] || !shown(i,o); }),visible.end());
        for(const auto i:plan.movers) if(shown(i,o)) visible.push_back(i);
        visible.insert(visible.end(),plan.unbounded.begin(),plan.unbounded.end());
        std::sort(visible.begin(),visible.end());
        return visible;
    }
    plan.unbounded.clear(); plan.portals.clear();
    bool same_frame=plan.framed;
    for(int i=0;i<16 && same_frame;++i) same_frame=plan.frame.m[i]==frame_matrix_.m[i];
    plan.bounds.resize(elements.size());
    plan.mover.resize(elements.size(),0);
    plan.slots.resize(elements.size(),nullptr);
    const auto lists=draw_lists(room);
    plan.unbounded=lists.unbounded;plan.portals=lists.portals;
    bool remake=!same_frame;
    plan.movers.clear();
    std::vector<spatial::Index::Entry> bounds;
    for(const auto i:lists.solids) {
        const auto& e=elements[i];
        auto& bound=plan.bounds[i];
        PlacedSlot*& slot=plan.slots[i];
        if(!slot || bound.element!=&e) slot=&slot_of(e);
        auto& placement=placed_in(*slot,room,e);
        if(!placement.boxed) placement.box=box_model(room,e),placement.boxed=true;
        if(!same_frame || bound.element!=&e || bound.stamp!=placement.stamp) {
            // Moved (not made, nor seen from a moved frame): culled on its
            // own from now on. Its entry in the index is left where it was
            // (and passed over), so a hundred books starting to fall one
            // after another never build the index again.
            if(same_frame && bound.element==&e && !plan.mover[i]) plan.mover[i]=1;
            if(bound.element!=&e) plan.mover[i]=0, remake=true;
            bound=query_bounds(placement.box);bound.element=&e;bound.stamp=placement.stamp;
        }
        if(plan.mover[i]) plan.movers.push_back(i);
        if(remake || !plan.mover[i]) bounds.push_back({i,bound.bounds});
    }
    // (The index is the bounds of what stands: while nothing came or went,
    // it stands - a thing that moved keeps its old entry, passed over.)
    if(const std::size_t n=lists.solids.size(); remake || plan.indexed!=n) {
        if(!remake) { bounds.clear(); for(const auto i:lists.solids) bounds.push_back({i,plan.bounds[i].bounds}); }
        plan.visibility.update(std::move(bounds)), plan.indexed=n, ++times_.indices_built;
    }
    auto visible=plan.visibility.visible(view);
    visible.erase(std::remove_if(visible.begin(),visible.end(),[&](std::size_t i) { return plan.mover[i] || !shown(i,{0,0,0}); }),visible.end());
    for(const auto i:plan.movers) if(shown(i,{0,0,0})) visible.push_back(i);
    visible.insert(visible.end(),plan.unbounded.begin(),plan.unbounded.end());
    std::sort(visible.begin(),visible.end());
    plan.frame=frame_matrix_; plan.framed=true;
    return visible;
}
// What of a portal its being declared reads - whether it is there, what it
// shows, whether it is a feed - and not the rest of its parameters: a thing
// that moves (a book falling, a glass carried) is asked again every frame
// otherwise, and each asking walks the whole graph.
static uint64_t declares(const Element& portal) {
    uint64_t h = std::hash<std::string>{}(portal.params.get_or<std::string>("shows", {}));
    h = (h ^ (portal.alive ? 1u : 2u)) * 1099511628211ULL;
    return (h ^ static_cast<uint64_t>(portal.params.num("feed", 0.0) > 0.5)) * 1099511628211ULL;
}
bool GLWorldView::declared_world(const State& host,const Element& portal,const Spatial3D& guest) const {
    const StateGraph* g=graph_?graph_:(root_?root_->graph_:nullptr);
    if(!g) return false;
    auto& memo=world_access_[&portal];
    if(memo.graph==g && memo.host==&host && memo.guest==&guest && memo.revision==g->revision() && memo.stamp==declares(portal)) return memo.allowed;
    memo={g,&host,&guest,g->revision(),declares(portal),render::declared_world(*g,host,portal,guest)}; ++times_.graph_queries;
    return memo.allowed;
}
bool GLWorldView::declared_feed(Key portal,const Spatial3D& guest) const {
    const StateGraph* g=graph_?graph_:(root_?root_->graph_:nullptr);
    return g && render::declared_feed(*g,portal,guest);
}
bool GLWorldView::declared_surface(const Element& portal,const Surface2D& surface) const {
    const StateGraph* g=graph_?graph_:(root_?root_->graph_:nullptr);
    if(!g) return false;
    auto& memo=surface_access_[&portal];
    if(memo.graph==g && memo.surface==&surface && memo.revision==g->revision() && memo.stamp==declares(portal)) return memo.allowed;
    memo={g,&surface,g->revision(),declares(portal),render::declared_surface(*g,portal,surface)};
    return memo.allowed;
}
} // namespace sg::render
