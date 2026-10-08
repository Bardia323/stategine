// The GL view's views through doorways and screens: which are planned,
// which drawn, and each portal drawn with what it shows.
#include "sg/gl/World.hpp"

namespace sg::render {

GLWorldView::Rect GLWorldView::screen_rect(const Spatial3D& world, const Element& e, const Camera& cam, float aspect) const {
    const Pose p = pose_of(world, e);
    const gl::Vec3 c = to_vec3(p.position), side = to_vec3(across_of(p)), up = to_vec3(up_of(p));
    const float w = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f, h = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f;
    const gl::Vec3 f = gl::normalize(cam.forward), right = gl::normalize(gl::cross(f, cam.up)), top = gl::cross(right, f);
    const float ty = std::tan(cam.fov * 0.5f), tx = ty * aspect;
    Rect r{1e9f, 1e9f, -1e9f, -1e9f};
    for (float a : {-1.0f, 1.0f})
        for (float b : {-1.0f, 1.0f}) {
            const gl::Vec3 v = c + side * (a * w) + up * (b * h) - cam.eye;
            const float z = gl::dot(v, f);
            // A corner at or behind the eye: the doorway may fill the view.
            if (z < kNear) return Rect{-1, -1, 1, 1};
            const float x = cam.ortho > 0 ? gl::dot(v, right) / (cam.ortho * aspect) : gl::dot(v, right) / (z * tx);
            const float y = cam.ortho > 0 ? gl::dot(v, top) / cam.ortho : gl::dot(v, top) / (z * ty);
            r.x0 = std::min(r.x0, x), r.y0 = std::min(r.y0, y), r.x1 = std::max(r.x1, x), r.y1 = std::max(r.y1, y);
        }
    return Rect{std::max(r.x0, -1.0f), std::max(r.y0, -1.0f), std::min(r.x1, 1.0f), std::min(r.y1, 1.0f)};
}

void GLWorldView::view_through(const Spatial3D& world, const Element& eye, float aspect, int depth, const std::string& path, Rect seen, int from, Key back) {
    // A doorway not drawn shows its world's air, so whether one is drawn must
    // change nothing on the screen: in air that takes all at last
    // (`uFogFull`) a view is left out only where the air has already taken
    // it - past `reach` from the eye, the whole way through the doorways come
    // by - however deep that is. Where the air never takes all, how deep the
    // views go is the world's to say (`views_deep`, counted from where it was
    // come into): two portals facing, a room glued to itself.
    const LookState& look = look_of(world);
    const double density = value(look, passes::scene, Key{"uFogDensity"}, 0.0);
    const double reach = value(look, passes::scene, Key{"uFogFull"}, 0.0) > 0.5 && density > 0.0
                             ? value(look, passes::scene, Key{"uFogStart"}, 0.0) + 4.6 / density
                             : 1e30;
    const int deep = static_cast<int>(world.params().num(Key{"views_deep"}, 1.0));
    if ((reach >= 1e30 && depth - from > deep) || depth > 16 || jobs_.size() >= 512) return;
    const Camera cam = camera_of(eye);
    for (const auto& e : world.elements()) {
        if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.has(Key{"ball"}) || e.id == back) continue;
        auto it = worlds_.find(e.id);
        if (it == worlds_.end() || !it->second.world || !declared_world(world, e, *it->second.world)) continue;
        if (!in_view(world, e, cam, aspect) || !opens_from(e, position_of(eye))) continue;
        // A door shut in it: no view, and none on from it.
        if (shut_to(e, position_of(eye))) continue;
        if (distance(position_of(eye), pose_of(world, e).position) - 0.5 * std::hypot(e.params.num(keys::w, 3.0), e.params.num(keys::h, 2.0)) > reach) continue;
        const Rect r = screen_rect(world, e, cam, aspect).cut(seen);
        const float area = (r.x1 - r.x0) * (r.y1 - r.y0);
        if (r.empty() || area < kLeastView) continue;
        Element there = it->second.world->camera();
        if (it->second.carry) it->second.carry(eye, there);
        const std::string key = path + "/" + e.id.str();
        jobs_.push_back(ViewJob{key, &world, &e, eye, there, &it->second, depth, area, r});
        // On through its doorways: on in the same world as deep as it says,
        // and into another, counted from there.
        const Element* next_back = !it->second.back.empty() ? it->second.world->find(it->second.back) : back_portal(*it->second.world, world);
        view_through(*it->second.world, there, aspect, depth + 1, key, r, it->second.world == &world ? from : depth, next_back ? next_back->id.key() : Key{});
    }
}

void GLWorldView::draw_views(const Spatial3D& world, float aspect) {
    // The biggest first (a view is never bigger than the one it is seen
    // through, so whatever shows it is kept before it)...
    std::vector<std::size_t> order(jobs_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return jobs_[x].area > jobs_[y].area; });
    const std::size_t most = std::min(kViewPoolMost, static_cast<std::size_t>(world.params().num(Key{"views_most"}, static_cast<double>(kViewPoolMost))));
    std::unordered_map<std::string, bool> kept;
    std::vector<std::size_t> draw;
    for (std::size_t i : order) {
        if (draw.size() >= most) break;
        const ViewJob& j = jobs_[i];
        const std::string parent = j.key.substr(0, j.key.rfind('/'));
        if (j.depth > 1 && !kept.count(parent)) continue;
        kept[j.key] = true;
        draw.push_back(i);
    }
    // ...each drawn before the view that shows it, the deepest first.
    std::stable_sort(draw.begin(), draw.end(), [&](std::size_t x, std::size_t y) { return jobs_[x].depth > jobs_[y].depth; });
    // Drawn only where its doorway is on the screen, at the screen's own
    // pixels, into a picture as big as that: a view costs what of the
    // screen it covers. (Its rect to the pixel, a pixel over, so that it
    // is sampled one to one.)
    const float W = static_cast<float>(target_w_), H = static_cast<float>(target_h_);
    struct Place {
        float x0, y0, x1, y1;
        int w() const { return static_cast<int>(x1 - x0); }
        int h() const { return static_cast<int>(y1 - y0); }
    };
    std::vector<Place> places(draw.size());
    for (std::size_t k = 0; k < draw.size(); ++k) {
        const Rect& seen = jobs_[draw[k]].seen;
        places[k] = Place{std::max(0.0f, std::floor((seen.x0 + 1) * 0.5f * W) - 1), std::max(0.0f, std::floor((seen.y0 + 1) * 0.5f * H) - 1),
                          std::min(W, std::ceil((seen.x1 + 1) * 0.5f * W) + 1), std::min(H, std::ceil((seen.y1 + 1) * 0.5f * H) + 1)};
    }
    // A view keeps the slot it had (by the way the eye came to it): the
    // pictures are handed out by what a view is, not by where it ranks in
    // the frame's sizes - which two views of a size swap as one grows past
    // the other, and each would be made again at the other's size. A view
    // with none is given the free slot that holds it most snugly; if none
    // does, the biggest free one, grown (the fewest pixels made again).
    std::vector<std::size_t> slot(draw.size(), pool_.size());
    std::vector<char> taken(pool_.size(), 0);
    for (std::size_t k = 0; k < draw.size(); ++k) {
        const auto b = bound_.find(jobs_[draw[k]].key);
        if (b == bound_.end() || b->second >= pool_.size() || taken[b->second] || pool_[b->second].owner != b->first) continue;
        slot[k] = b->second;
        taken[b->second] = 1;
    }
    std::vector<std::size_t> loose;
    for (std::size_t k = 0; k < draw.size(); ++k)
        if (slot[k] == pool_.size()) loose.push_back(k);
    std::stable_sort(loose.begin(), loose.end(), [&](std::size_t x, std::size_t y) { return places[x].w() * places[x].h() > places[y].w() * places[y].h(); });
    for (std::size_t k : loose) {
        const int need_w = places[k].w(), need_h = places[k].h();
        std::size_t snug = pool_.size(), biggest = pool_.size();
        long snug_area = 0, biggest_area = -1;
        for (std::size_t c = 0; c < pool_.size(); ++c) {
            if (taken[c]) continue;
            const gl::RenderTarget& t = pool_[c].target;
            const long area = static_cast<long>(t.width()) * t.height();
            if (t.valid() && t.width() >= need_w && t.height() >= need_h && (snug == pool_.size() || area < snug_area)) snug = c, snug_area = area;
            if (area > biggest_area) biggest = c, biggest_area = area;
        }
        std::size_t pick = snug != pool_.size() ? snug : biggest;
        if (pick == pool_.size()) {  // none free: another slot
            pool_.emplace_back();
            taken.push_back(0);
            pick = pool_.size() - 1;
        }
        slot[k] = pick;
        taken[pick] = 1;
    }
    // (Views gone from sight leave their names behind: let those go.)
    if (bound_.size() > 4 * pool_.size() + 64)
        for (auto it = bound_.begin(); it != bound_.end();)
            it = it->second >= pool_.size() || pool_[it->second].owner != it->first ? bound_.erase(it) : std::next(it);
    slot_.clear();
    for (std::size_t k = 0; k < draw.size(); ++k) {
        const std::size_t i = draw[k];
        const ViewJob& j = jobs_[i];
        Nested& n = pool_[slot[k]];
        slot_[j.key] = slot[k];
        n.owner = j.key;
        bound_[j.key] = slot[k];
        const Element* back = !j.wp->back.empty() ? j.wp->world->find(j.wp->back) : back_portal(*j.wp->world, *j.host);
        path_ = j.key;
        const float px0 = places[k].x0, py0 = places[k].y0, px1 = places[k].x1, py1 = places[k].y1;
        fit(n, static_cast<int>(px1 - px0), static_cast<int>(py1 - py0));
        n.rect = Rect{px0 / W * 2 - 1, py0 / H * 2 - 1, px1 / W * 2 - 1, py1 / H * 2 - 1};
        n.fx = (px1 - px0) / static_cast<float>(n.target.width()), n.fy = (py1 - py0) / static_cast<float>(n.target.height());
        sub_ = n.rect;
        host_air_ = air_of(*j.host);
        // As a view on the screen is: the near plane just short of the
        // doorway, and culled by the pyramid through it and its plane.
        const Camera there = camera_of(j.there);
        const HalfSpace cut = portal_clip(*j.host, *j.portal, j.from, j.there);
        const Through way = back && !j.wp->back.empty() ? through(*j.portal, *j.wp->world, *back, there) : Through{};
        cull_ = way.sides;
        cull_.push_back(spatial::HalfSpace{{cut.normal.x, cut.normal.y, cut.normal.z}, cut.offset});
        draw_world(seen(*j.wp->world), there, aspect, n.target, j.depth + 1, way.znear,
                   back ? back->id.key() : Key{}, {cut});
        cull_.clear();
        host_air_.on = false;
        sub_ = Rect{-1, -1, 1, 1};
        n.frame = frame_count_;
        ++times_.portal_views;
    }
    path_.clear();
    jobs_.clear();
}

void GLWorldView::draw_portal(const State& st, const Element& e, int depth, const gl::RenderTarget& target, bool frame_only) {
    // A ball is the boundary of a world drawn where it is (sg::nests): there
    // is no picture of it to draw - unless the world is of another scale (a
    // snow globe's): then the ball is a window onto it, its view on the ball.
    if (e.params.has(Key{"ball"})) {
        if (!ball_window(e) || frame_only) return;
        WorldPortal& wp = worlds_.find(e.id)->second.shared ? *worlds_.find(e.id)->second.shared : worlds_.find(e.id)->second;
        const float r = static_cast<float>(e.params.num(Key{"ball"}));
        set_model(room_local(gl::Mat4::translate(to_vec3(pose_of(st, e).position)) * gl::Mat4::scale({2.0f * r, 2.0f * r, 2.0f * r})));
        // Glass: polished (`roughness`, 0.04 unless it says), so the room's
        // lamps glint on it and it shines at its edges, over what is in it.
        scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.04)));
        scene_->set("uSurface", 0.0f);
        scene_->set("uEmissive", 1.0f);  // what is seen in it arrives already lit
        scene_->set("uHighlight", 0.0f);
        scene_->set("uGlow", 0.0f);
        if (depth > 0 || !(wp.shown && wp.drawn == frame_count_)) {
            // Past how deep the views go: its world's air.
            const Mix far = mix(wp.world->id(), look_of(*wp.world));
            scene_->set("uAlbedo", gl::Vec3{static_cast<float>(setting(far, passes::scene, "clear.x", 0.012)),
                                            static_cast<float>(setting(far, passes::scene, "clear.y", 0.014)),
                                            static_cast<float>(setting(far, passes::scene, "clear.z", 0.022))});
            scene_->set("uTexMix", 0.0f);
            sphere_.draw();
        } else {
            wp.shown->bind_color(0);
            scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
            scene_->set("uTexMix", 1.0f);
            scene_->set("uScreenUV", 1.0f);
            sample_screen(wp.seen, wp.fx, wp.fy);
            // Glass: the room round it reflected in it (`reflect`, 1 unless it says).
            const float reflect = static_cast<float>(e.params.num(Key{"reflect"}, 1.0));
            if (reflect > 0.0f && wp.env.valid()) {
                wp.env.bind(7);
                scene_->set("uEnvMix", reflect);
            }
            // Never cut by the near plane, however close the eye comes: the
            // glass covers what it covers, and nothing behind shows through.
            gl::glEnable(gl::GL_DEPTH_CLAMP);
            sphere_.draw();
            gl::glDisable(gl::GL_DEPTH_CLAMP);
            scene_->set("uEnvMix", 0.0f);
            scene_->set("uScreenUV", 0.0f);
            scene_->set("uScreenRect", 0.0f, 0.0f, 1.0f, 1.0f);
            scene_->set("uUVRect", 0.0f, 0.0f, 0.0f, 0.0f);
            scene_->set("uTexMix", 0.0f);
        }
        scene_->set("uEmissive", 0.0f);
        return;
    }
    // A portal with nothing bound to it is a marker for a plain opening -
    // the gap between wall segments is the doorway, and it needs no
    // geometry of its own.
    if (!has_surface(e) && !is_doorway(st,e)) return;

    const Pose pose = pose_of(st, e);
    const gl::Vec3 pos = to_vec3(pose.position);
    const float w = static_cast<float>(e.params.num(keys::w, 3.0));
    const float h = static_cast<float>(e.params.num(keys::h, 2.0));
    const float yaw = static_cast<float>(pose.yaw);
    const bool open = e.params.get_or<bool>(keys::open, false);
    const gl::Vec3 n = to_vec3(facing(pose));  // the domain decides what a turn means
    const gl::Vec3 along = to_vec3(across_of(pose)), upward = to_vec3(up_of(pose));
    const gl::Mat4 turned = panel_turn(pose, e);

    auto world_it = worlds_.find(e.id);
    const bool is_window = world_it != worlds_.end() && world_it->second.world != nullptr && declared_world(st, e, *world_it->second.world);

    // An open panel's frame lights up - unless the panel states its own
    // glow, as a blackboard does: then only when pointed at.
    const bool says_glow = e.params.has(Key{"glow"});
    const float hi = ((open && !says_glow) || e.id == highlight_) ? 1.0f : 0.0f;
    Body& body = bodies_[&e];
    if (const uint64_t stamp = placed_of(st, e).stamp; body.stamp != stamp || body.window != is_window)
        body = Body{stamp, is_window, portal_body(st, e, is_window)};
    for(const auto& draw:body.parts) {
        const gl::Vec3 c{static_cast<float>(draw.colour.r),static_cast<float>(draw.colour.g),static_cast<float>(draw.colour.b)};
        if (batch_frames_) batch(cube_, draw.model, c, static_cast<float>(draw.roughness), 0, 0, hi, 0);
        else draw_solid(room_local(draw.model),c,static_cast<float>(draw.roughness),0,0,hi);
    }

    if (is_window) {
        if (frame_only) return;
        // Seen through another portal, a screen is not there at all: what
        // it projects is already what lies beyond it.
        if (depth > 0 && is_screen(e)) return;
        WorldPortal& wp = world_it->second.shared ? *world_it->second.shared : world_it->second;
        // `inset` is how far in front of the portal's plane the view is
        // drawn; a doorway walked through has it on the plane (portal_inset).
        const float inset = static_cast<float>(portal_inset(e));
        // Where the viewer is, in front of the doorway (> 0) or behind it.
        const Vec3d eye_here = local_of(frame_, {cam_eye_.x, cam_eye_.y, cam_eye_.z});
        const float side = gl::dot(to_vec3(eye_here) - (pos + n * inset), n);
        // Seen from a side it is not crossed from, a doorway is only its
        // frame: what is behind it is what one walks into (opens_from).
        if (!opens_from(e, eye_here)) return;
        // Seen through another doorway, its own view is the one drawn for
        // the way the eye came (view_through) - or, past how deep views go,
        // glass.
        const Nested* deeper = nullptr;
        if (depth > 0) {
            auto nt = slot_.find(path_ + "/" + e.id.str());
            if (nt != slot_.end() && pool_[nt->second].frame == frame_count_) deeper = &pool_[nt->second];
        }
        // Past how deep the views go, or past what a frame draws, a doorway
        // shows the air of the world beyond - as anything that far off is. A
        // world that should look endless says its air takes all by then
        // (`uFogFull`, from `uFogStart`), and its views that far off are not
        // drawn at all (view_through).
        const bool fresh = wp.shown && wp.drawn == frame_count_;
        if ((depth > 0 && !deeper) || (depth == 0 && !fresh && !(wp.own_drawn && wp.own_out.valid()))) {
            const Mix far = mix(wp.world->id(), look_of(*wp.world));
            set_model(room_local(gl::Mat4::translate(pos + n * inset) * turned * gl::Mat4::scale({1.0f, h, w})));
            scene_->set("uAlbedo", gl::Vec3{static_cast<float>(setting(far, passes::scene, "clear.x", 0.012)),
                                            static_cast<float>(setting(far, passes::scene, "clear.y", 0.014)),
                                            static_cast<float>(setting(far, passes::scene, "clear.z", 0.022))});
            scene_->set("uRoughness", 1.0f);
            scene_->set("uSurface", 0.0f);
            scene_->set("uEmissive", 1.0f);
            scene_->set("uHighlight", 0.0f);
            scene_->set("uTexMix", 0.0f);
            scene_->set("uGlow", 0.0f);
            quad_.draw();
            scene_->set("uEmissive", 0.0f);
            return;
        }
        // The far room, sampled in screen space: a hole in the wall - all
        // of it, or (`crop_a0..a1` along it, `crop_b0..b1` up it, each 0
        // to 1) only that part of it open: the rest of the doorway is
        // there, glued and carrying, but not yet a hole.
        const bool own = !deeper && wp.own_drawn && wp.own_out.valid();
        if (deeper) deeper->target.bind_color(0);
        else if (own) wp.own_out.bind_color(0);
        else wp.shown->bind_color(0);
        // A picture already developed in its own look is taken back
        // through the tone curve, to come out as it went in.
        if (own) scene_->set("uUntone", 1.0f);
        const float a0 = static_cast<float>(e.params.num(Key{"crop_a0"}, 0.0)), a1 = static_cast<float>(e.params.num(Key{"crop_a1"}, 1.0));
        const float b0 = static_cast<float>(e.params.num(Key{"crop_b0"}, 0.0)), b1 = static_cast<float>(e.params.num(Key{"crop_b1"}, 1.0));
        if (a1 <= a0 || b1 <= b0) return;
        const gl::Vec3 open_at = pos + n * inset + along * (((a0 + a1) * 0.5f - 0.5f) * w) +
                                 upward * (((b0 + b1) * 0.5f - 0.5f) * h);
        set_model(room_local(gl::Mat4::translate(open_at) * turned * gl::Mat4::scale({1.0f, h * (b1 - b0), w * (a1 - a0)})));
        scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
        scene_->set("uRoughness", 1.0f);
        scene_->set("uSurface", 0.0f);
        scene_->set("uEmissive", 1.0f);  // the far room arrives already lit
        scene_->set("uHighlight", 0.0f);
        scene_->set("uTexMix", 1.0f);
        scene_->set("uGlow", 0.0f);
        scene_->set("uScreenUV", 1.0f);
        if (deeper) sample_screen(deeper->rect, deeper->fx, deeper->fy);
        else sample_screen(Rect{-1, -1, 1, 1}, 1.0f, 1.0f);
        // `undim`: a doorway that is part of a screen the room dims round
        // (leaned in to it) is not dimmed with the room.
        const bool undim = e.params.num(Key{"undim"}, 0.0) > 0.5;
        if (undim) scene_->set("uUndim", 1.0f);
        quad_.draw();
        if (undim) scene_->set("uUndim", 0.0f);
        if (own) scene_->set("uUntone", 0.0f);
        // Stepping through, the eye comes nearer the doorway than the near
        // plane and the quad is cut away. For those last few centimetres
        // (`tunnel` > 0) the same view is drawn again just past the near
        // plane, behind the doorway, on the opening's own outline as seen
        // from the eye - so it covers exactly what the opening covers, no
        // more, and nothing about the doorway changes size as you close in.
        // It is drawn in front of everything (depth 0): behind the doorway
        // may be solid wall - a portal on a wall - and nothing stands
        // between the eye and a doorway it is centimetres from.
        // (Scaled from the eye so its plane lies twice the near plane off,
        // however close the eye is: right at the threshold too - a floor on
        // `side` once left it inside the near plane, and the opening empty
        // for a frame.)
        // (Every doorway walked through has it, unless it says `tunnel` 0.)
        const double tunnel = e.params.num(Key{"tunnel"}, e.params.num(Key{"walk"}, 0.0) > 0.5 ? 1.0 : 0.0);
        if (tunnel > 0.0 && side >= 0.0f && side < kNear * 2.0f) {
            const float k = kNear * 2.0f / std::max(side, 1e-6f);
            const gl::Vec3 plane = pos + n * inset, eye = to_vec3(eye_here);
            const gl::Vec3 centre = eye + (plane - eye) * k;
            set_model(room_local(gl::Mat4::translate(centre) * turned *
                      gl::Mat4::scale({1.0f, h * k, w * k})));
            // (Not tested against what is there: a wall right behind the
            // doorway may stand on the near plane, at the same depth.)
            gl::glDepthRange(0.0, 0.0);
            gl::glDepthFunc(gl::GL_ALWAYS);
            quad_.draw();
            gl::glDepthFunc(gl::GL_LESS);
            gl::glDepthRange(0.0, 1.0);
        }
        scene_->set("uScreenUV", 0.0f);
        scene_->set("uScreenRect", 0.0f, 0.0f, 1.0f, 1.0f);
        scene_->set("uUVRect", 0.0f, 0.0f, 0.0f, 0.0f);
        scene_->set("uTexMix", 0.0f);
        scene_->set("uEmissive", 0.0f);
        return;
    }

    // The picture: a world's feed, or a 2D state's pixels.
    int tex_w = 0, tex_h = 0;
    const auto& feeds = shared_feeds();
    if (auto f = feeds.find(signal_of(e)); f != feeds.end() && f->second.world && declared_feed(signal_of(e), *f->second.world) && f->second.shown().valid()) {
        f->second.shown().bind_color(0);
        tex_w = f->second.w, tex_h = f->second.h;
        scene_->set("uTexFlip", 1.0f);
        // Developed light, taken back to light - unless the panel is paint
        // (`untone` = 0): then the picture is only colour, as a canvas is,
        // and reflects the room's light and no more.
        scene_->set("uUntone", e.params.num(Key{"untone"}, 1.0) > 0.5 ? 1.0f : 0.0f);
    } else {
        auto it = surfaces_.find(e.id);
        if (it == surfaces_.end() || !it->second.surface) return;  // a plain opening
        BoundSurface& bound = it->second;
        Surface2D& surf = *bound.surface;
        // A surface that has changed size gets a texture its new size.
        refresh(bound);
        bound.texture.bind(0);
        tex_w = surf.px_w(), tex_h = surf.px_h();
    }

    // Clear of the frame slab (half-thickness 0.06), or the panel sinks into
    // it; a bare sheet's face sits just off its own body.
    const gl::Vec3 face = panel_normal(pose, e);
    const float lift = framed(e) ? 0.08f : sheet_thickness(e) * 0.5f + 0.0015f;
    set_model(room_local(gl::Mat4::translate(pos + face * lift) * panel_turn(pose, e) *
              gl::Mat4::scale({1.0f, h, w})));
    // `glow` is how much the panel lights itself - a screen more than a
    // map, paper not at all; an open panel that does not say glows at
    // least as a map does. A stated glow is kept: a blackboard or a
    // photo, open or not, is lit only by the room.
    const float glow = static_cast<float>(e.params.num(Key{"glow"}, 0.12));
    scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
    scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.75)));
    scene_->set("uSurface", 0.0f);
    scene_->set("uEmissive", 0.0f);
    // A bare sheet has no frame to light up, so its face takes the
    // highlight - only when pointed at; an open screen is lit by its glow.
    scene_->set("uHighlight", !framed(e) && e.id == highlight_ ? 1.0f : 0.0f);
    scene_->set("uTexMix", 1.0f);
    scene_->set("uGlow", open && !says_glow ? std::max(glow, 0.55f) : glow);
    // `crt` makes the panel a screen: the glass is drawn per pixel.
    scene_->set("uCRT", static_cast<float>(e.params.num(Key{"crt"}, 0.0)));
    // `halo`: how much the tube's phosphor glows into the glass round it.
    scene_->set("uHalo", static_cast<float>(e.params.num(Key{"halo"}, 0.0)));
    // `flat`: how flat the tube is seen (crt_shape) - 1 face up to it.
    scene_->set("uFlat", static_cast<float>(e.params.num(Key{"flat"}, 0.0)));
    scene_->set("uTexSize", static_cast<float>(tex_w), static_cast<float>(tex_h));
    quad_.draw();
    scene_->set("uTexFlip", 0.0f);
    scene_->set("uUntone", 0.0f);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
    scene_->set("uCRT", 0.0f);
    scene_->set("uHalo", 0.0f);
    scene_->set("uFlat", 0.0f);
}

}  // namespace sg::render
