#include "sg/gl/World.hpp"

#include "sg/domains/Atlas.hpp"
#include "sg/domains/Texture.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>

namespace sg::render {

std::vector<std::string> GLWorldView::prepare(const StateGraph& g) {
    graph_ = &g;
    ensure_resources();
    std::vector<std::string> out = look_defects(g);
    std::set<Key> reach = g.reachable();
    if (reach.empty())
        for (Key id : g.ids()) reach.insert(id);
    preparing_ = true;
    for (Key id : reach)
        if (const auto* look = dynamic_cast<const LookState*>(g.find(id)))
            check_look(*look, out);
    preparing_ = false;
    return out;
}

void GLWorldView::spill(Key light, const Surface2D* from, double most, bool on) {
    Spill& sp = spills_[light];
    sp.from = from;
    sp.most = most;
    sp.on = on;
}

void GLWorldView::bind_world(Key portal_element, const Spatial3D* world, Carry carry, Key back) {
    WorldPortal& wp = worlds_[portal_element];
    wp.world = world;
    wp.carry = std::move(carry);
    wp.back = back;
}

void GLWorldView::bind_feed(Key portal_element, const Spatial3D* world, int w, int h, bool live) {
    Feed& f = feeds_[portal_element];
    if (f.world != world) f.drawn = false;
    f.world = world;
    f.live = live;
    if (f.w != w || f.h != h || !f.out[0].valid()) {
        f.w = std::max(1, w), f.h = std::max(1, h);
        f.rw = f.w, f.rh = f.h, f.div = 1, f.slack = 0;
        // Written as the composite writes the screen, encoded; read back
        // as the panel reads any picture, decoded. Two: one drawn while
        // the other is shown, so a feed that sees its own screen sees the
        // picture it made the frame before.
        for (gl::RenderTarget& o : f.out) o.create(f.w, f.h, gl::GL_SRGB8_ALPHA8, 0, false);
        f.drawn = false;
    }
    if (!f.view) {
        GLQuality q = q_;
        q.shadow_size = std::min(q_.shadow_size, 1024);
        f.view = std::make_unique<GLWorldView>(q);
    }
    f.view->set_fixed_step(fixed_step_);
}

void GLWorldView::warm(const std::vector<Spatial3D*>& worlds, int fb_w, int fb_h) {
    const LookFader fader = fader_;
    const Mix post = post_;
    for (Spatial3D* w : worlds)
        if (w) render(*w, fb_w, fb_h);
    gl::glFinish();
    fader_ = fader;
    post_ = post;
}

void GLWorldView::render(const Spatial3D& world, int fb_w, int fb_h) {
    const PlacedRoom one{&world, Pose{}, {}};
    render(std::vector<PlacedRoom>{one}, fb_w, fb_h);
}

std::vector<PlacedRoom> GLWorldView::seen(const Spatial3D& world) const {
    std::vector<PlacedRoom> out{PlacedRoom{&world, Pose{}, {}}};
    if (const StateGraph* g = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr))
        for (const PlacedRoom& p : nests(*g, world.id())) out.push_back(p);
    return out;
}

void GLWorldView::render(const std::vector<PlacedRoom>& requested, int fb_w, int fb_h) {
    // What is seen of the world the eye is in: it, and the worlds round it
    // or in it (sg::nests) - as through any doorway onto it.
    auto rooms = requested;
    if (!rooms.empty() && rooms.front().room)
        for (const PlacedRoom& p : seen(*rooms.front().room))
            if (std::none_of(rooms.begin(), rooms.end(), [&](const PlacedRoom& r) { return r.room == p.room; })) rooms.push_back(p);
    const StateGraph* declared=graph_?graph_:(root_?root_->graph_:nullptr);
    if(rooms.size()>1){
        if(!declared)rooms.resize(1);
        else if(rooms.front().room){
            const auto plan=view_plan(*declared,*rooms.front().room,rooms);
            rooms.erase(std::remove_if(rooms.begin(),rooms.end(),[&](const PlacedRoom& placed){return std::none_of(plan.rooms.begin(),plan.rooms.end(),[&](const RoomDraw& draw){return draw.room==placed.room;});}),rooms.end());
        }
    }
    if (fb_w <= 0 || fb_h <= 0 || rooms.empty() || !rooms.front().room) return;
    world_time_ = semantic_time(graph_ ? graph_ : (root_ ? root_->graph_ : nullptr), *rooms.front().room);
    ease_spills(rooms);
    aim_rays(*rooms.front().room);
    // Feeds the graph declares: an open embedding of a 3D state in a
    // `feed` portal. Those it no longer declares go. Only the view on
    // the screen keeps them; the views it draws feeds and far rooms with
    // show the same pictures (root_).
    if (graph_ && !root_) {
        for (auto& [id, f] : feeds_) f.seen = false;
        for (const Embedding& em : graph_->embeddings()) {
            if (!em.open) continue;
            auto* guest = dynamic_cast<const Spatial3D*>(graph_->find(em.guest));
            const State* host = graph_->find(em.host);
            const Element* panel = host ? host->find(em.portal) : nullptr;
            if (!guest || !panel || panel->params.num(Key{"feed"}, 0.0) < 0.5) continue;
            const Key resource=panel->params.num(Key{"eye"},0)>.5?em.name:em.portal;
            bind_feed(resource, guest, static_cast<int>(panel->params.num(Key{"feed_w"}, 640.0)),
                      static_cast<int>(panel->params.num(Key{"feed_h"}, 480.0)),
                      panel->params.num(Key{"live"}, 1.0) > 0.5);
            Feed& f = feeds_[resource];
            f.seen = true;
            f.from_graph = true;
            // An `eye` portal - a camera's lens - is where the world is
            // seen from; any other shows the world from its own camera.
            f.eye = panel->params.num(Key{"eye"}, 0.0) > 0.5 ? panel : nullptr;
        }
        for (auto it = feeds_.begin(); it != feeds_.end();)
            it = it->second.from_graph && !it->second.seen ? feeds_.erase(it) : std::next(it);
    }
    // Feeds first, each whole, into its own picture: a screen showing a
    // world shows it as it is this frame. A feed is drawn if its screen
    // is in view here - or stands in a world another feed drawn shows.
    const auto feeds_from = std::chrono::steady_clock::now();
    int feed_views = 0;
    if (!root_) {
        std::vector<std::pair<Key, Feed*>> due;
        std::vector<const Spatial3D*> shown;
        const float seen_aspect = static_cast<float>(fb_w) / static_cast<float>(fb_h);
        const auto seen_in = [&](const Spatial3D& room, Key id, bool look) {
            for (const Element* e : {room.find(id), shown_in(room, id)})
                if (e && e->alive && (!look || in_view(room, *e, camera_of(*rooms.front().room), seen_aspect))) return true;
            return false;
        };
        // How tall a screen showing this feed is on this view, in pixels: as
        // tall as the tallest shows it. Where that is not known (a screen in
        // a world another feed shows, one beyond a doorway) it is the whole
        // picture - which is also what a screen leaned into comes to.
        const Camera seen_from = camera_of(*rooms.front().room);
        const auto shown_px = [&](Key id, int declared) {
            float most = 0.0f;
            for (const PlacedRoom& placed : rooms) {
                if (!placed.room) continue;
                for (const Element* e : {placed.room->find(id), shown_in(*placed.room, id)}) {
                    if (!e || !e->alive) continue;
                    if (placed.room != rooms.front().room) return static_cast<float>(declared);
                    const gl::Vec3 at = to_vec3(pose_of(*placed.room, *e).position) - seen_from.eye;
                    const float away = std::max(std::sqrt(gl::dot(at, at)) - 0.5f * static_cast<float>(e->params.num(keys::w, 1.0)), 0.05f);
                    most = std::max(most, static_cast<float>(e->params.num(keys::h, 1.0)) / (2.0f * away * std::tan(seen_from.fov * 0.5f)) * static_cast<float>(fb_h));
                }
            }
            for (const Spatial3D* w : shown)
                if (w != rooms.front().room && (w->find(id) || shown_in(*w, id))) return static_cast<float>(declared);
            return most;
        };
        for (bool more = true; more;) {
            more = false;
            for (auto& [id, f] : feeds_) {
                if (!f.world || !f.view || !declared_feed(id, *f.world) || (!f.live && f.drawn)) continue;
                if (std::any_of(due.begin(), due.end(), [&](const auto& d) { return d.second == &f; })) continue;
                bool here = false;
                for (const PlacedRoom& placed : rooms)
                    if (placed.room && seen_in(*placed.room, id, true)) here = true;
                for (const Spatial3D* w : shown)
                    if (!here && seen_in(*w, id, false)) here = true;
                if (!here) continue;
                due.emplace_back(id, &f);
                shown.push_back(f.world);
                more = true;
            }
        }
        // The deepest first, so what a feed shows of another is this
        // frame's picture - but its own, or one that shows it back, the
        // last frame's.
        for (auto it = due.rbegin(); it != due.rend(); ++it) {
            Feed& f = *it->second;
            f.drawn = true;
            // The picture drawn is only as big as the screen is seen: a
            // painting across the room costs a few hundred pixels, not the
            // window's. More at once as it comes near; less only after it has
            // been small a while, so it does not flicker at a boundary.
            if (f.live) {
                const int want = feed_detail(shown_px(it->first, f.h), f.h);
                if (want < f.div) f.div = want, f.slack = 0;
                else if (want > f.div && ++f.slack >= 20) f.div = want, f.slack = 0;
                else if (want == f.div) f.slack = 0;
                const int nw = std::max(1, f.w / f.div), nh = std::max(1, f.h / f.div);
                if (nw != f.rw || nh != f.rh) {
                    f.rw = nw, f.rh = nh;
                    for (gl::RenderTarget& o : f.out) o.create(f.rw, f.rh, gl::GL_SRGB8_ALPHA8, 0, false);
                }
            }
            // Nothing it is made from has changed: the picture it has is the one.
            const uint64_t key = feed_key(f);
            if (f.drawn_of == key && f.drawn_of != 0) continue;
            f.drawn_of = key;
            f.view->graph_ = graph_;  // the looks it is shown in are in the same graph
            f.view->root_ = this;
            f.view->output_ = &f.out[1 - f.front];
            f.view->eye_override_ = f.eye;
            f.view->render(*f.world, f.rw, f.rh);
            f.view->output_ = nullptr;
            f.view->eye_override_ = nullptr;
            f.out[1 - f.front].mipmap();
            f.front = 1 - f.front;
            ++feed_views;
        }
    }
    const double feeds_ms =
        timing_ ? (gl::glFinish(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - feeds_from).count()) : 0.0;
    ensure_resources();
    ensure_targets(fb_w, fb_h);
    advance_fades();

    const Spatial3D& world = *rooms.front().room;
    // The post passes belong to the viewer, so they wear the look of the
    // room the viewer is in, and fade when that room changes.
    if (&world != last_world_ && std::find(own_shown_.begin(), own_shown_.end(), &world) != own_shown_.end())
        fader_.settle(view_key(), look_of(world));
    last_world_ = &world;
    own_shown_ = own_shown_now_;
    own_shown_now_.clear();
    post_ = mix(view_key(), look_of(world));
    const Camera eye_cam = eye_override_ ? camera_of(*eye_override_) : camera_of(world);
    const float aspect = static_cast<float>(fb_w) / static_cast<float>(fb_h);

    // --- portal views, one pass per window --------------------------------
    // Rendered first, at framebuffer resolution, because the portal quad
    // samples them in screen space. The guest's camera was already carried
    // through the doorway by the embedding's functor.
    // A view drawing a feed or a far room sees through the same doorways
    // as the view on the screen: its worlds are that one's, each seen
    // from this view's own eye (its targets its own).
    if (root_) {
        for (auto it = worlds_.begin(); it != worlds_.end();)
            it = root_->worlds_.count(it->first) ? std::next(it) : worlds_.erase(it);
        for (const auto& [id, rw] : root_->worlds_) {
            WorldPortal& wp = worlds_[id];
            wp.world = rw.world;
            wp.carry = rw.carry;
            wp.back = rw.back;
        }
    }
    // (The doorways' pictures are this frame's, from pools made with the
    // screen's targets: none is made in the middle of a frame.)
    std::size_t root_views = 0;
    ++frame_count_;
    shadow_budget_ = kNestedShadowMaps;
    times_ = FrameTimes{};
    times_.feeds = feeds_ms;
    times_.feed_views = feed_views;
    const auto mark = [this] {
        if (timing_) gl::glFinish();
        return std::chrono::steady_clock::now();
    };
    const auto since = [](std::chrono::steady_clock::time_point a) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
    };
    auto t0 = mark();
    // A feed whose scene is as it was: developed again, not drawn again.
    const uint64_t scene_now = root_ && output_ ? scene_key(rooms, fb_w, fb_h) : 0;
    const bool still = scene_now != 0 && scene_now == scene_drawn_ && resolve_.valid();
    scene_drawn_ = scene_now;
    if (!still) {
    // Screens already drawn this frame: the same world from the same eye
    // is one view, whichever screen shows it.
    std::vector<std::pair<WorldPortal*, Element>> screens;
    // The views seen through these doorways' views, planned for all of them
    // together and drawn once - the frame's best of them - before any
    // doorway's own view, which shows them.
    jobs_.clear();
    for (const auto& e : world.elements()) {
        if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.has(Key{"ball"})) continue;
        if (e.params.num(Key{"own_look"}, 0.0) > 0.5) continue;
        auto it = worlds_.find(e.id);
        if (it == worlds_.end() || !it->second.world || !declared_world(world, e, *it->second.world)) continue;
        if (!in_view(world, e, eye_cam) || !opens_from(e, position_of(eye_of(world)))) continue;
        Element eye = it->second.world->camera();
        if (it->second.carry) it->second.carry(eye_of(world), eye);
        const Element* back = !it->second.back.empty() ? it->second.world->find(it->second.back) : back_portal(*it->second.world, world);
        view_through(*it->second.world, eye, aspect, 1, "/" + e.id.str(), screen_rect(world, e, eye_cam, aspect), 0, back ? back->id.key() : Key{});
    }
    bool views_drawn = false;
    for (const auto& e : world.elements()) {
        if (e.kind != kinds::portal || !e.alive || e.params.has(Key{"ball"})) continue;
        auto it = worlds_.find(e.id);
        if (it == worlds_.end() || !it->second.world || !declared_world(world, e, *it->second.world)) continue;
        WorldPortal& wp = it->second;
        wp.shared = nullptr;
        if (!in_view(world, e, eye_cam) || !opens_from(e, position_of(eye_of(world)))) continue;
        // The virtual camera stands behind the far side's doorway - that is
        // what a portal is. What lies between it and the doorway is cut
        // away by a plane: this portal's own plane, carried to the far
        // side by the same turn and shift that carried the camera. So a
        // window beside a door shows the far side beside it, and nothing
        // standing behind the far doorway gets in the way.
        Element eye = wp.world->camera();
        if (wp.carry) wp.carry(eye_of(world), eye);
        const bool screen = is_screen(e);
        if (screen) {
            for (const auto& [drawn, at] : screens)
                if (drawn->world == wp.world && same_eye(at, eye)) wp.shared = drawn;
            if (wp.shared) continue;
            screens.emplace_back(&wp, eye);
        }
        wp.own_drawn = false;
        // (Once only: a view that is itself drawn for another draws its
        // own-look doorways as any other, so views never nest for ever.)
        if (e.params.num(Key{"own_look"}, 0.0) > 0.5 && !root_) {
            // Drawn as the far room's own view draws it - every pass, its
            // composite too - from the carried eye, at the size of the
            // screen: what is seen through the doorway is what will be
            // seen once through it, pixel for pixel.
            if (!wp.own) {
                wp.own = std::make_unique<GLWorldView>(q_);
                wp.own->set_fixed_step(fixed_step_);
            }
            if (!wp.own_out.valid() || wp.own_out.width() != fb_w || wp.own_out.height() != fb_h)
                wp.own_out.create(fb_w, fb_h, gl::GL_SRGB8_ALPHA8, 0, false);
            wp.own->graph_ = graph_;
            wp.own->root_ = root_ ? root_ : this;
            wp.own->output_ = &wp.own_out;
            wp.own->eye_override_ = &eye;
            wp.own->render(*wp.world, fb_w, fb_h);
            wp.own->output_ = nullptr;
            wp.own->eye_override_ = nullptr;
            wp.own_drawn = true;
            own_shown_now_.push_back(wp.world);
            ++times_.portal_views;
            continue;
        }
        const Camera guest_cam = camera_of(eye);
        const Element* back = !wp.back.empty() ? wp.world->find(wp.back) : back_portal(*wp.world, world);
        std::vector<HalfSpace> clips;
        if (!screen) clips.push_back(far_side(world, e, eye));
        const std::string path = "/" + e.id.str();
        if (!views_drawn) draw_views(world, aspect), views_drawn = true;
        path_ = path;
        if (root_views >= root_pool_.size()) {
            if (root_pool_.size() >= kRootViewsMost) continue;
            make_root_view(root_pool_.emplace_back(), target_w_, target_h_);
        }
        RootView& view = root_pool_[root_views++];
        host_air_ = air_of(world);
        draw_world(seen(*wp.world), guest_cam, aspect, view.ms,
                   /*depth=*/1, kNear, back ? back->id.key() : Key{}, clips);
        host_air_.on = false;
        wp.drawn = frame_count_;
        path_.clear();
        view.ms.blit_to(view.target);
        wp.shown = &view.target;
        ++times_.portal_views;
    }
    times_.portals = since(t0);
    t0 = mark();

    // --- the room the viewer is actually standing in ------------------------
    path_.clear();
    draw_world(rooms, eye_cam, aspect, scene_target_, /*depth=*/0, kNear);
    times_.scene_cpu = since(t0);
    if (timing_) gl::glFinish();
    times_.scene = since(t0);
    t0 = mark();

    scene_target_.blit_to(resolve_);
    }
    scene_src_ = &resolve_;
    const double ao = setting(post_, passes::composite, "ao", 0.0);
    if (ao > 0.0) {
        view_ = ViewParams{eye_cam.fov, aspect, kNear,
                           static_cast<float>(world.params().num(Key{"far"}, 120.0))};
        run_ao(static_cast<float>(ao),
               static_cast<float>(setting(post_, passes::composite, "ao.radius", 0.45)));
        scene_src_ = &lit_;
    }
    run_bloom();
    composite(fb_w, fb_h);
    if (timing_) gl::glFinish();
    times_.post = since(t0);
    highlight_ = Key{};
}

bool GLWorldView::has_surface(const Element& e) const {
    auto it = surfaces_.find(e.id);
    if (it != surfaces_.end() && it->second.surface != nullptr && declared_surface(e, *it->second.surface)) return true;
    const auto& feeds = shared_feeds();
    auto f = feeds.find(signal_of(e));
    return f != feeds.end() && f->second.world != nullptr && declared_feed(signal_of(e), *f->second.world);
}

Key GLWorldView::signal_of(const Element& e) const {
    if (!graph_) return e.id;
    auto& memo = signal_memo_[&e];
    if (memo.graph != graph_ || memo.revision != graph_->revision() || memo.stamp != e.params.stamp())
        memo = {graph_, graph_->revision(), e.params.stamp(), render::signal_of(*graph_, e)}, ++times_.graph_queries;
    return memo.signal;
}

gl::Vec3 GLWorldView::to_vec3(const Vec3d& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

gl::Vec3 GLWorldView::color_of(const Element& e, gl::Vec3 fallback) {
    return {static_cast<float>(e.params.num(keys::r, fallback.x)),
            static_cast<float>(e.params.num(keys::g, fallback.y)),
            static_cast<float>(e.params.num(keys::b, fallback.z))};
}

void GLWorldView::ensure_resources() {
    if (ready_) return;
    builtin_ = {
        {passes::shadow, {gl::depth_vs(), gl::depth_fs()}},
        {passes::scene, {gl::scene_vs(), gl::scene_fs()}},
        {passes::bright, {gl::post_vs(), gl::bright_fs()}},
        {passes::blur, {gl::post_vs(), gl::blur_fs()}},
        {passes::composite, {gl::post_vs(), gl::composite_fs()}},
    };
    const bool was = preparing_;
    preparing_ = true;  // the built-ins are never late
    for (Key p : passes::all()) program_for(standard_, p);
    preparing_ = was;
    cube_.create(gl::cube_vertices());
    cylinder_.create(gl::cylinder_vertices());
    sphere_.create(gl::sphere_vertices());
    quad_.create(gl::quad_vertices());
    screen_.create();
    ready_ = true;
}

void GLWorldView::ensure_targets(int w, int h) {
    if (w == target_w_ && h == target_h_) return;
    target_w_ = w;
    target_h_ = h;
    // As many samples as asked for, if the driver has them.
    gl::GLint most = 0;
    gl::glGetIntegerv(gl::GL_MAX_SAMPLES, &most);
    msaa_ = most > 0 ? std::min(q_.msaa, static_cast<int>(most)) : q_.msaa;
    scene_target_.create(w, h, gl::GL_RGBA16F, msaa_, true);
    resolve_.create(w, h, gl::GL_RGBA16F, 0, false);
    depth_.create(w, h, gl::GL_RGBA16F, 0, true, /*depth_texture=*/true);
    lit_.create(w, h, gl::GL_RGBA16F, 0, false);
    // The views through doorways seen through doorways, and the last frame's
    // picture: made now, with the rest, so that no step through a doorway
    // waits on one being made.
    root_pool_.reserve(kRootViewsMost);  // (never moved: doorways hold their pictures)
    root_pool_.resize(std::max(root_pool_.size(), kRootViews));
    for (RootView& v : root_pool_) make_root_view(v, w, h);
    pool_.resize(std::max(pool_.size(), kViewPool));
    for (Nested& n : pool_) {
        fit(n, w / 4, h / 4, true);
        n.frame = 0;
    }
    // Full resolution: at half, the occlusion's edges stair-step over the
    // antialiased picture.
    ao_a_.create(w, h, gl::GL_RGBA16F, 0, false);
    ao_b_.create(w, h, gl::GL_RGBA16F, 0, false);
    const int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
    bloom_a_.create(bw, bh, gl::GL_RGBA16F, 0, false);
    bloom_b_.create(bw, bh, gl::GL_RGBA16F, 0, false);
    bloom_levels_ = 0;
    for (int lw = bw / 2, lh = bh / 2; bloom_levels_ < kBloomLevels && lw >= 8 && lh >= 8; lw /= 2, lh /= 2)
        bloom_chain_[bloom_levels_++].create(lw, lh, gl::GL_RGBA16F, 0, false);
}

void GLWorldView::ease_spills(const std::vector<PlacedRoom>& rooms) {
    for (auto& [id, sp] : spills_) {
        if (!sp.from) continue;
        if (!sp.begun)
            for (const PlacedRoom& placed : rooms)
                if (const Element* e = placed.room ? placed.room->find(id) : nullptr) {
                    sp.r = e->params.num(keys::r), sp.g = e->params.num(keys::g), sp.b = e->params.num(keys::b);
                    sp.intensity = e->params.num(keys::intensity);
                    sp.begun = true;
                    break;
                }
        if (!sp.begun) continue;
        // As sg::spill: toward the picture's colour, and its brightness.
        const Rgb c = average_colour(*sp.from);
        const double lum = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
        const double m = std::max({c.r, c.g, c.b, 1e-4});
        const auto toward = [](double& v, double to) { v += (to - v) * 0.25; };
        toward(sp.r, 0.35 + 0.65 * c.r / m);
        toward(sp.g, 0.35 + 0.65 * c.g / m);
        toward(sp.b, 0.35 + 0.65 * c.b / m);
        toward(sp.intensity, sp.most * std::min(1.0, 0.12 + 1.5 * lum));
        if (!sp.on) sp.intensity = 0.0;
    }
}

void GLWorldView::aim_rays(const Spatial3D& first) {
    rays_on_ = rays_room_ == &first;
    if (!rays_on_) return;
    const Element& cam = first.camera();
    const Vec3d to = rays_at_ - position_of(cam);
    const Vec3d f = forward_of(cam);
    const double half = cam.params.num(keys::fov, 70.0) * 3.14159265358979323846 / 360.0;
    rays_dir_ = to;
    rays_fwd_ = f;
    rays_tan_ = std::tan(half);
}

auto GLWorldView::own_lights(const Spatial3D& room, const Pose& pose) const -> std::vector<Light> {
    std::vector<Light> out;
    for (const auto& e : room.elements()) {
        if (e.kind != kinds::light || !e.alive) continue;
        Light l=light_of(room,e,pose);
        if (auto sp = spills_.find(e.id); sp != spills_.end() && sp->second.begun && sp->second.from) {
            l.color = {static_cast<float>(sp->second.r), static_cast<float>(sp->second.g), static_cast<float>(sp->second.b)};
            l.power = static_cast<float>(sp->second.intensity) * 26.0f;
        }
        if (l.power <= 0.0f) continue;  // switched off
        out.push_back(l);
    }
    return out;
}

auto GLWorldView::through_doorways(const PlacedRoom& placed) -> std::vector<Light> {
    std::vector<Light> out;
    if (!placed.room) return out;
    const Spatial3D& room = *placed.room;
    for (const auto& e : room.elements()) {
        if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.num(Key{"light"}, 1.0) < 0.5) continue;
        const auto it = worlds_.find(e.id);
        if (it == worlds_.end() || !it->second.world || !it->second.carry || !declared_world(room,e,*it->second.world)) continue;
        const Spatial3D& far = *it->second.world;
        const Pose door = pose_of(room, e);
        const float half_w = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f;
        const float half_h = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f;
        if (half_w <= 0.0f || half_h <= 0.0f) continue;
        Element probe = room.camera(), there = far.camera();
        probe.params.set(keys::x, door.position.x).set(keys::y, door.position.y).set(keys::z, door.position.z)
            .set(keys::yaw, 0.0).set(keys::pitch, 0.0);
        it->second.carry(probe, there);
        const double turn = there.params.num(keys::yaw);
        const Vec3d from = position_of(there);
        // A point of the far world, where this room has it.
        const auto here = [&](const gl::Vec3& q) {
            const Vec3d local = rotate_xz({q.x - from.x, q.y - from.y, q.z - from.z}, -turn);
            const Pose p{{door.position.x + local.x, door.position.y + local.y, door.position.z + local.z}, 0.0};
            return to_vec3(compose_pose(placed.pose, p).position);
        };
        const auto turned = [&](const gl::Vec3& d) {
            return to_vec3(rotate_xz({d.x, d.y, d.z}, placed.pose.yaw - turn));
        };
        const gl::Vec3 at = to_vec3(compose_pose(placed.pose, door).position);
        const gl::Vec3 across_door = to_vec3(across(door.yaw + placed.pose.yaw));
        const gl::Vec3 into = to_vec3(heading(door.yaw + placed.pose.yaw));  // a portal faces into its own room
        // A door shut in the opening lets nothing through. Ajar, what
        // comes through throws the leaf's shadow (its own shadow map);
        // `open` is only for a light past the maps there are.
        bool shut = false;
        const float open = 1.0f - covered(room, e, door, half_w, half_h, shut);
        const auto gate = [&](Light& l) {
            l.gated = true;
            l.gate = std::hash<Key>{}(e.id);
            l.gate_at = at, l.gate_across = across_door, l.gate_in = into, l.gate_w = half_w, l.gate_h = half_h;
            // In the shadow of what stands in the way, none of it gets through.
            l.open = open, l.floor = 0.0f;
            // Shut, nothing gets through - but onto the leaf itself, which is
            // half in that room.
            if (shut) l.hung_only = true;
        };
        // Its lamps, the strongest few. A bounce standing in for light from
        // all round belongs to its own room, and stays there - but for the
        // door's leaf, half in it.
        std::vector<Light> lamps = own_lights(far, Pose{});
        std::sort(lamps.begin(), lamps.end(), [](const Light& a, const Light& b) {
            if (a.indirect != b.indirect) return b.indirect;
            if (a.sun != b.sun) return a.sun;
            return a.power > b.power;
        });
        std::size_t real = 0;
        for (Light& l : lamps)
            if (l.indirect) l.hung_only = true;
            else ++real;
        if (real > 6) lamps.erase(lamps.begin() + 6, lamps.begin() + static_cast<std::ptrdiff_t>(real));
        for (Light l : lamps) {
            l.pos = here(l.pos);
            l.dir = gl::normalize(turned(l.dir));
            if (l.sun) {
                // Its shadow lies in this room, near the opening.
                constexpr float kReach = 8.0f;
                l.extent = std::min(l.extent, kReach);
                l.pinned = true;
                l.focus = at + into * (kReach * 0.4f);
            }
            gate(l);
            out.push_back(l);
        }
        // Its sky, or whatever light fills it from all round, seen
        // through the opening: a soft lamp just beyond it, as wide as it.
        const LookState& look = look_of(far);
        const double amb = value(look, passes::scene, Key{"uAmbient"}, 0.55);
        const gl::Vec3 sky{static_cast<float>(value(look, passes::scene, Key{"uSky.x"}, 0.10) * amb),
                           static_cast<float>(value(look, passes::scene, Key{"uSky.y"}, 0.13) * amb),
                           static_cast<float>(value(look, passes::scene, Key{"uSky.z"}, 0.20) * amb)};
        const float bright = std::max({sky.x, sky.y, sky.z});
        if (bright > 1e-4f) {
            // A portal faces into its own room; beyond is behind it.
            const gl::Vec3 beyond = to_vec3(heading(door.yaw + placed.pose.yaw)) * -1.0f;
            Light glow;
            glow.pos = at + beyond * 0.35f;
            glow.dir = beyond * -1.0f;
            glow.color = sky * (1.0f / bright);
            glow.power = bright * half_w * half_h * 24.0f;
            glow.inner = 0.9f;
            glow.outer = 1.55f;
            glow.falloff = 1.0f;
            glow.indirect = true;
            gate(glow);
            out.push_back(glow);
        }
    }
    if (out.size() > 8) out.resize(8);
    return out;
}

void GLWorldView::doors_to_program(const PlacedRoom& placed) {
    static const auto name = [](const char* base, int i) {
        static std::array<std::array<std::string, 4>, 5> names = [] {
            std::array<std::array<std::string, 4>, 5> n;
            const char* bases[] = {"uDoorAt", "uDoorAxis", "uDoorIn", "uDoorSky", "uDoorGround"};
            for (int b = 0; b < 5; ++b)
                for (int k = 0; k < 4; ++k) n[static_cast<std::size_t>(b)][static_cast<std::size_t>(k)] = std::string(bases[b]) + "[" + std::to_string(k) + "]";
            return n;
        }();
        const std::string s = base;
        const int b = s == "uDoorAt" ? 0 : s == "uDoorAxis" ? 1 : s == "uDoorIn" ? 2 : s == "uDoorSky" ? 3 : 4;
        return names[static_cast<std::size_t>(b)][static_cast<std::size_t>(i)].c_str();
    };
    int count = 0;
    if (placed.room)
        for (const auto& e : placed.room->elements()) {
            if (count >= 4) break;
            if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.num(Key{"light"}, 1.0) < 0.5) continue;
            const auto it = worlds_.find(e.id);
            if (it == worlds_.end() || !it->second.world || !declared_world(*placed.room,e,*it->second.world)) continue;
            const Pose door = pose_of(*placed.room, e);
            // A door shut in it: nothing of the other side here.
            bool shut = false;
            covered(*placed.room, e, door, static_cast<float>(e.params.num(keys::w, 3.0) * 0.5), static_cast<float>(e.params.num(keys::h, 2.0) * 0.5), shut);
            // (Shut, it is still there for the leaf in it: uDoorIn.z.)
            const gl::Vec3 at = to_vec3(compose_pose(placed.pose, door).position);
            const Vec3d a = across(door.yaw + placed.pose.yaw), in = heading(door.yaw + placed.pose.yaw);
            const LookState& look = look_of(*it->second.world);
            const double amb = value(look, passes::scene, Key{"uAmbient"}, 0.55);
            const auto v3 = [&](const char* k, double fx, double fy, double fz) {
                return gl::Vec3{static_cast<float>(value(look, passes::scene, Key{std::string(k) + ".x"}, fx) * amb),
                                static_cast<float>(value(look, passes::scene, Key{std::string(k) + ".y"}, fy) * amb),
                                static_cast<float>(value(look, passes::scene, Key{std::string(k) + ".z"}, fz) * amb)};
            };
            scene_->set(name("uDoorAt", count), at.x, at.y, at.z, static_cast<float>(e.params.num(keys::w, 3.0) * 0.5));
            scene_->set(name("uDoorAxis", count), static_cast<float>(a.x), static_cast<float>(a.z),
                        static_cast<float>(e.params.num(keys::h, 2.0) * 0.5), 0.0f);
            scene_->set(name("uDoorIn", count), static_cast<float>(in.x), static_cast<float>(in.z), shut ? 1.0f : 0.0f, 0.0f);
            scene_->set(name("uDoorSky", count), v3("uSky", 0.10, 0.13, 0.20));
            scene_->set(name("uDoorGround", count), v3("uGround", 0.14, 0.10, 0.07));
            ++count;
        }
    scene_->set("uDoorCount", count);
}

float GLWorldView::covered(const Spatial3D& room,const Element& portal,const Pose& door,float half_w,float half_h,bool& shut) const {
    return portal_occlusion(room,portal,door,half_w,half_h,shut);
}

auto GLWorldView::read_lights(const std::vector<PlacedRoom>& rooms, std::size_t& shadowed) -> std::vector<Light> {
    std::vector<Light> out;
    for (const PlacedRoom& placed : rooms)
        if (placed.room && !placed.image)
            for (const Light& l : own_lights(*placed.room, placed.pose)) out.push_back(l);
    // A sun first - it lights everything, so it has the first shadow -
    // then lamps, brightest first. Not nearest: which lamps cast shadows
    // must not change as the viewer walks about, or shadows pop in and out.
    // Equal lamps are ordered by where they hang, never by the viewer:
    // a tie broken by distance hands the shadow maps from lamp to lamp as
    // the viewer walks, and shadows vanish a step further off.
    // Bounce light stands in for light from all round and casts no
    // shadow worth a map: it comes after every real lamp.
    std::sort(out.begin(), out.end(), [](const Light& a, const Light& b) {
        if (a.indirect != b.indirect) return b.indirect;
        if (a.sun != b.sun) return a.sun;
        if (a.power != b.power) return a.power > b.power;
        if (a.pos.x != b.pos.x) return a.pos.x < b.pos.x;
        if (a.pos.z != b.pos.z) return a.pos.z < b.pos.z;
        return a.pos.y < b.pos.y;
    });
    const std::size_t own = std::min(out.size(), kOwnShadows);
    // What comes through a doorway, shut or not - the same lights in the
    // same places, only what they reach told by the door (hung_only) - gets
    // a shadow map of its own while there are maps: then what stands in the
    // opening - a door ajar, the frame - throws its own shadow. Past that,
    // it is let in by how much of the opening is clear.
    std::vector<Light> in;
    for (const PlacedRoom& placed : rooms)
        if (!placed.image)
            for (const Light& l : through_doorways(placed)) in.push_back(l);
    shadowed = std::min(own + in.size(), kShadowMaps);
    for (std::size_t k = 0; k < shadowed - own && k < in.size(); ++k) in[k].open = 1.0f;
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(own), in.begin(), in.end());
    shadowed = std::min(shadowed, out.size());
    if (out.size() > kMaxLights) out.resize(kMaxLights);
    if (out.empty()) {
        Light none;
        none.power = 0.0f;  // everything off: ambient only, and the shadow map unused
        out.push_back(none);
    }
    return out;
}

bool GLWorldView::same_eye(const Element& a, const Element& b) {
    for (Key k : {keys::x, keys::y, keys::z, keys::yaw, keys::pitch, keys::fov})
        if (a.params.num(k, 0.0) != b.params.num(k, 0.0)) return false;
    return true;
}

const Element* GLWorldView::back_portal(const Spatial3D& guest, const Spatial3D& host) const {
    for (const auto& e : guest.elements()) {
        if (e.kind != kinds::portal || !e.alive) continue;
        auto it = worlds_.find(e.id);
        if (it != worlds_.end() && it->second.world == &host && declared_world(guest,e,host)) return &e;
    }
    return nullptr;
}

void GLWorldView::draw_world(const std::vector<PlacedRoom>& given, const Camera& cam, float aspect, gl::RenderTarget& target, int depth, float znear, Key skip_portal, const std::vector<HalfSpace>& clips) {
    // A space that wraps is seen in every copy of it the eye can reach -
    // from inside it or through any doorway onto it: every view is drawn
    // here, so every view sees the same space.
    const auto part_from = std::chrono::steady_clock::now();
    const auto lap = [&](int k, std::chrono::steady_clock::time_point& at) {
        const auto now = std::chrono::steady_clock::now();
        times_.part[k] += std::chrono::duration<double, std::milli>(now - at).count();
        at = now;
    };
    auto part_at = part_from;
    std::vector<PlacedRoom> rooms = given;
    for (std::size_t r = 0, own = given.size(); r < own; ++r) {
        if (!given[r].room) continue;
        const Spatial3D& room = *given[r].room;
        const Vec3d p = period_of(room);
        if (p.x <= 0 && p.y <= 0 && p.z <= 0) continue;
        const Vec3d eye = local_of(given[r].pose, {cam.eye.x, cam.eye.y, cam.eye.z});
        for (const Vec3d& o : images(room, eye, room.params().num(Key{"far"}, 120.0))) {
            if (o.x == 0 && o.y == 0 && o.z == 0) continue;
            PlacedRoom copy = given[r];
            copy.pose = compose_pose(given[r].pose, Pose{o});
            copy.image = true;
            rooms.push_back(copy);
        }
    }
    std::size_t shadowed = 0;
    const auto lights_from = std::chrono::steady_clock::now();
    const uint64_t rooms_at_now = rooms_key(rooms);
    const uint64_t lit_key = (rooms_at_now ^ worlds_stamp()) * 1099511628211ULL;
    if (lights_memo_.size() > 64) lights_memo_.clear();
    auto lit_memo = lights_memo_.find(lit_key);
    if (lit_memo == lights_memo_.end()) {
        LightsMemo m;
        m.lights = read_lights(rooms, m.shadowed);
        ++times_.lights_read;
        lit_memo = lights_memo_.emplace(lit_key, std::move(m)).first;
    }
    const std::vector<Light>& lights = lit_memo->second.lights;
    shadowed = lit_memo->second.shadowed;
    times_.lights_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - lights_from).count();
    cam_eye_ = cam.eye, cam_forward_ = cam.forward, cam_up_ = cam.up;
    guest_pass_ = depth > 0;
    lod_px_ = 0.5f * static_cast<float>(target.height()) / std::tan(cam.fov * 0.5f);
    lod_least_ = static_cast<float>(rooms.front().room->params().num(Key{"lod_px"}, 0.75));
    const float zfar = static_cast<float>(rooms.front().room->params().num(Key{"far"}, 120.0));
    for (const PlacedRoom& placed : rooms)
        if (placed.room)
            for (const auto& e : placed.room->elements())
                if (e.alive && e.kind == terrain_kind()) ensure_terrain(e, cam);
    // The strongest lamps get a shadow map each. What the scene shader is told
    // of each map is how wide a texel of it is in the world, at a unit's
    // distance from a lamp (or anywhere, for a sun's box): it keeps its
    // lookups that far off the surface (shadow_factor).
    const int shadow_px = q_.shadow_size;
    const auto texel_of = [&](const Light& l) {
        const float size = static_cast<float>(std::max(shadow_px, 1));
        if (l.sun) return 2.0f * l.extent / size;
        return 2.0f * std::tan(std::min(l.outer * 2.05f, 2.7f) * 0.5f) / size;
    };
    gl::Mat4 light_vp[kShadowMaps];
    float bias[kShadowMaps] = {1.0f, 1.0f, 1.0f, 1.0f};
    for (std::size_t i = 0; i < kShadowMaps; ++i) {
        const Light& l = lights[std::min(i, lights.size() - 1)];
        const ViewCamera eye{{cam.eye.x,cam.eye.y,cam.eye.z},{cam.forward.x,cam.forward.y,cam.forward.z},{cam.up.x,cam.up.y,cam.up.z}};
        light_vp[i]=shadow_projection(l,eye,shadow_px,bias[i]);
        bias[i] = texel_of(l);
    }
    // A sun whose map reaches far gets a second, small one round the viewer
    // (in the layers after the lamps'): close up, a texel is a centimetre,
    // not a hand's breadth - a door's edge, a chair's legs, a leaf half in
    // each room would otherwise show the steps of the wide one.
    constexpr float kNearReach = 6.0f;
    std::vector<float> near_of(lights.size(), -1.0f);
    std::array<std::size_t, kShadowMaps> layer_light{};
    std::size_t layers = shadowed;
    for (std::size_t i = 0; i < shadowed; ++i) layer_light[i] = i;
    for (std::size_t i = 0; i < shadowed && layers < kShadowMaps; ++i) {
        const Light& l = lights[i];
        if (!l.sun || l.indirect || l.extent <= 2.0f * kNearReach) continue;
        Light close = l;
        close.extent = kNearReach;
        close.pinned = false;
        const ViewCamera eye{{cam.eye.x,cam.eye.y,cam.eye.z},{cam.forward.x,cam.forward.y,cam.forward.z},{cam.up.x,cam.up.y,cam.up.z}};
        light_vp[layers] = shadow_projection(close, eye, shadow_px, bias[layers]);
        bias[layers] = texel_of(close);
        near_of[i] = static_cast<float>(layers);
        layer_light[layers] = i;
        ++layers;
    }
    // Narrowed to the part of the screen drawn (sub_): that part fills the
    // viewport, at the same pixels it has on the screen.
    const bool part = sub_.x0 > -1 || sub_.y0 > -1 || sub_.x1 < 1 || sub_.y1 < 1;
    const float hx = (sub_.x1 - sub_.x0) * 0.5f, hy = (sub_.y1 - sub_.y0) * 0.5f;
    const gl::Mat4 narrow = part ? gl::Mat4::scale({1.0f / hx, 1.0f / hy, 1.0f}) * gl::Mat4::translate({-(sub_.x0 + sub_.x1) * 0.5f, -(sub_.y0 + sub_.y1) * 0.5f, 0.0f})
                                 : gl::Mat4::identity();
    // (The screen's pixels: a part's picture need only be as big as the part.)
    vp_w_ = part ? std::round(static_cast<float>(target_w_) * hx) : static_cast<float>(target.width());
    vp_h_ = part ? std::round(static_cast<float>(target_h_) * hy) : static_cast<float>(target.height());
    const gl::Mat4 view_proj =
        narrow * projection_of(cam, aspect, znear, zfar) *
        gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, cam.up);

    if (timing_) gl::glFinish();
    const auto shadow_start = std::chrono::steady_clock::now();
    // --- shadow depth, one pass per shadowed lamp ------------------------
    // Each world seen (the room, and whatever a portal shows) keeps its own
    // maps, and a map is drawn again only when its lamp has moved or
    // turned, or anything that casts has: most frames, nothing has, and
    // the shadows cost nothing.
    // Each view keeps its own maps: two windows onto one world see it
    // lit differently (each lets in its own room's light), and would
    // otherwise draw over each other's maps every frame.
    // A lamp's map is the lamp's and the casters', whoever looks: views of a
    // world lit the same way share its maps (a room glued to itself, seen
    // through doorway after doorway, draws them once). Only a sun whose box
    // goes where the eye goes keeps a set for each view.
    // (Which lights, not where they are this moment - a sun goes round with
    // the day; whether each map still holds is its own signature's to say.)
    uint64_t lit = 1469598103934665603ULL ^ layers;
    bool follows_eye = false;
    for (std::size_t i = 0; i < layers; ++i) {
        const Light& l = lights[layer_light[i]];
        lit = mix_bits(lit, l.sun ? 1.0f : l.gated ? 2.0f : 3.0f);
        // (By the doorway it comes in by, not where that is: a doorway that
        // moves - on a planet in its orbit - keeps its maps.)
        if (l.gated) lit = (lit ^ l.gate) * 1099511628211ULL;
        follows_eye = follows_eye || (l.sun && !l.pinned) || near_of[layer_light[i]] >= 0.0f;
    }
    // (A view is the way the eye came to it - its path, the same from frame
    // to frame whatever picture it is drawn into.)
    if (follows_eye) {
        lit = (lit ^ std::hash<std::string>{}(path_)) * 1099511628211ULL;
        lit = (lit ^ reinterpret_cast<std::uintptr_t>(root_)) * 1099511628211ULL;
    }
    ShadowSet& maps = shadows_for(rooms.front().room, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(lit)));
    if (maps.array.ensure(shadow_px, static_cast<int>(std::max<std::size_t>(layers, 1))))
        for (uint64_t& s : maps.sig) s = 0;
    const auto sig_from = std::chrono::steady_clock::now();
    if (casters_memo_.size() > 64) casters_memo_.clear();
    auto cast_memo = casters_memo_.find(rooms_at_now);
    if (cast_memo == casters_memo_.end()) cast_memo = casters_memo_.emplace(rooms_at_now, casters_of(rooms)).first, ++times_.casters_listed;
    const std::vector<Caster>& casters = cast_memo->second;
    times_.signature += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sig_from).count();
    // Where each room stands is part of where its things are.
    uint64_t rooms_at = 1469598103934665603ULL;
    for (const PlacedRoom& placed : rooms) {
        if (!placed.room) continue;
        rooms_at = mix_bits(rooms_at, static_cast<float>(placed.pose.position.x));
        rooms_at = mix_bits(rooms_at, static_cast<float>(placed.pose.position.z));
        rooms_at = mix_bits(rooms_at, static_cast<float>(placed.pose.yaw));
    }
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glEnable(gl::GL_CULL_FACE);
    // Faces towards the lamp: the shadow starts where the thing does (its
    // lookups keep off the surface by the normal, shadow_factor), not at its
    // far side - which leaves light under a thing standing on the floor.
    gl::glCullFace(gl::GL_BACK);
    const gl::Program& caster = *program_for(post_.shown(), passes::shadow);
    bool caster_ready = false;
    const auto draw_casters = [&](const std::vector<const Caster*>& list) {
        std::size_t room_now = rooms.size();
        for (const Caster* c : list) {
            if (c->room != room_now) {
                if (room_now < rooms.size()) flush_batches(caster, false);
                room_now = c->room;
                set_frame(rooms[room_now].pose);
            }
            const Spatial3D& room = *rooms[room_now].room;
            const Element& e = *c->element;
            if (e.kind == kinds::mesh || e.kind == kinds::wall) {
                if (q_.instancing) {
                    batch(shape_of(room, e), box_matrix(room, e).m, {}, 0, 0, 0, 0, 0);
                    continue;
                }
                caster.set("uModel", frame_matrix_ * box_matrix(room, e).m);
                shape_of(room, e).draw();
            } else if (e.kind == terrain_kind()) {
                auto t = terrains_.find(e.id);
                if (t == terrains_.end() || !t->second.mesh.valid()) continue;
                caster.set("uModel", frame_matrix_);
                t->second.mesh.draw();
            } else if (e.kind == kinds::portal) {
                caster.set("uModel", frame_matrix_ * portal_frame_model(room, e).m);
                cube_.draw();
            }
        }
        if (room_now < rooms.size()) flush_batches(caster, false);
    };
    const auto layers_from = std::chrono::steady_clock::now();
    std::vector<const Caster*> sees;
    bool unshadowed = false;
    // The eye's own views draw a few maps a frame, taking turns from frame
    // to frame; past that a map stands as last drawn, with its own box - a
    // shadow that moves follows a frame or two later, and the frame does not
    // wait on every lamp at once. (One never drawn is drawn now.) Only a map
    // whose box is fixed where it is may wait: one whose box goes with the
    // eye (a sun's) stood as it was covers where the eye was, not where it
    // is, and is drawn every frame it changes.
    int own_budget = kOwnShadowMaps;
    for (std::size_t n = 0; n < layers; ++n) {
        const std::size_t i = (n + frame_count_) % layers;
        // What this lamp's map holds: the casters in the volume it sees. A
        // map stands while those stand - a mug moved in another corner of the
        // house is nothing to it - and it is drawn with those alone.
        const Light& li = lights[layer_light[i]];
        const bool with_eye = li.sun && !li.pinned;
        // Which light this layer holds now: a map is that light's, and one
        // that was another's (the lights came in another order) is drawn now,
        // never left standing as some other lamp's shadow.
        uint64_t of = (mix_bits(1469598103934665603ULL, li.sun ? 1.0f : 0.0f) ^ li.gate) * 1099511628211ULL;
        for (float f : {li.color.x, li.color.y, li.color.z, li.power, li.extent, near_of[layer_light[i]] == static_cast<float>(i) ? 1.0f : 0.0f})
            of = mix_bits(of, f);
        if (!li.sun) of = mix_bits(mix_bits(mix_bits(of, li.pos.x), li.pos.y), li.pos.z);
        const bool same = maps.of[i] == of;
        const Frustum volume = frustum_of(light_vp[i]);
        uint64_t sig = rooms_at ^ (0x9E3779B97F4A7C15ULL * (i + 1));
        for (float f : light_vp[i].m) sig = mix_bits(sig, f);
        sees.clear();
        for (const Caster& c : casters) {
            if (!shades(c, volume, li)) continue;
            sig = (sig ^ reinterpret_cast<std::uintptr_t>(c.element)) * 1099511628211ULL;
            sig = (sig ^ c.where) * 1099511628211ULL;
            sees.push_back(&c);
        }
        if (maps.sig[i] == sig) continue;
        // A view seen through another view draws within the frame's budget.
        const bool may_wait = same && !with_eye;
        if (depth > 1 && shadow_budget_ <= 0 && may_wait) {
            if (maps.sig[i] == 0) unshadowed = true;  // never drawn: nothing to cast with yet
            else light_vp[i] = maps.vp[i];            // as last drawn, with its own box
            continue;
        }
        if (depth <= 1 && maps.sig[i] != 0 && may_wait) {
            if (own_budget <= 0) {
                light_vp[i] = maps.vp[i];
                continue;
            }
            --own_budget;
        }
        if (depth > 1) --shadow_budget_;
        maps.sig[i] = sig;
        maps.vp[i] = light_vp[i];
        maps.of[i] = of;
        ++times_.shadow_maps;
        times_.shadow_casters += static_cast<int>(sees.size());
        if (!caster_ready) {
            caster.use();
            apply_uniforms(caster, post_, passes::shadow);
            caster.set("uInstanced", 0);
            caster_ready = true;
        }
        maps.array.bind_layer(static_cast<int>(i));
        gl::glClear(gl::GL_DEPTH_BUFFER_BIT);
        caster.set("uLightViewProj", light_vp[i]);
        // Light from beyond a doorway is kept out only by what stands on
        // this side of it - the room behind the opening's plane is the
        // other world's, and the light comes from there. (A hand's
        // breadth of slack keeps a leaf shut in the plane casting.)
        if (li.gated) {
            gl::glEnable(gl::GL_CLIP_DISTANCE0);
            caster.set("uCasterSide", li.gate_in.x, li.gate_in.y, li.gate_in.z, 0.08f - gl::dot(li.gate_in, li.gate_at));
        } else {
            gl::glDisable(gl::GL_CLIP_DISTANCE0);
            caster.set("uCasterSide", 0.0f, 0.0f, 0.0f, 1.0f);
        }
        draw_casters(sees);
    }
    times_.layers_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - layers_from).count();
    gl::glDisable(gl::GL_CLIP_DISTANCE0);
    gl::glCullFace(gl::GL_BACK);
    if (timing_) {
        gl::glFinish();
        times_.shadows += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shadow_start).count();
    }

    lap(0, part_at);
    // --- the scene -------------------------------------------------------
    // The first room is the one this view is taken from; its look clears.
    const Mix first = mix(rooms.front().room->id(), look_of(*rooms.front().room));
    target.bind();
    if (part) {
        gl::glViewport(0, 0, static_cast<int>(vp_w_), static_cast<int>(vp_h_));
        gl::glScissor(0, 0, static_cast<int>(vp_w_), static_cast<int>(vp_h_));
        gl::glEnable(gl::GL_SCISSOR_TEST);
    }
    gl::glClearColor(static_cast<float>(setting(first, passes::scene, "clear.x", 0.012)),
                     static_cast<float>(setting(first, passes::scene, "clear.y", 0.014)),
                     static_cast<float>(setting(first, passes::scene, "clear.z", 0.022)),
                     1.0f);
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glDepthFunc(gl::GL_LESS);
    gl::glEnable(gl::GL_MULTISAMPLE);
    gl::glDisable(gl::GL_CULL_FACE);
    for (int i = 0; i < kMaxBounds; ++i)
        gl::glEnable(gl::GL_CLIP_DISTANCE0 + static_cast<gl::GLenum>(i));
    maps.array.bind_depth(1);

    // Everything a scene shader is fed that is not the look's. Set again
    // whenever a room's look brings a different program.
    const auto frame_uniforms = [&](const gl::Program& p) {
        p.set("uViewProj", view_proj);
        p.set("uInstanced", 0);
        p.set("uDim", 0.0f);
        for (std::size_t i = 0; i < kShadowMaps; ++i) {
            p.set(shadow_uniform(i, 0), light_vp[i]);
            p.set(shadow_uniform(i, 1), bias[i]);
        }
        p.set("uLightCount", static_cast<int>(lights.size()));
        p.set("uShadowCount", unshadowed ? 0 : static_cast<int>(shadowed));
        gl::Vec3 sun_dir{0, 1, 0}, sun_color{0, 0, 0};
        for (std::size_t i = 0; i < lights.size(); ++i) {
            p.set(light_uniform(i, 0), lights[i].pos);
            p.set(light_uniform(i, 1), lights[i].dir);
            p.set(light_uniform(i, 2), lights[i].color);
            p.set(light_uniform(i, 3), lights[i].power);
            p.set(light_uniform(i, 4), std::cos(lights[i].inner));
            p.set(light_uniform(i, 5), std::cos(lights[i].outer));
            p.set(light_uniform(i, 6), lights[i].sun ? 1.0f : 0.0f);
            p.set(light_uniform(i, 7), lights[i].floor);
            p.set(light_uniform(i, 8), lights[i].indirect ? 1.0f : 0.0f);
            p.set(light_uniform(i, 9), lights[i].falloff);
            const Light& l = lights[i];
            p.set(light_uniform(i, 12), unshadowed ? -1.0f : near_of[i]);
            p.set(light_uniform(i, 10), l.gate_at.x, l.gate_at.y, l.gate_at.z, l.gate_w);
            p.set(light_uniform(i, 11), l.gate_across.x, l.gate_across.z, l.gate_h, l.gated ? (l.hung_only ? 2.0f : 1.0f) : 0.0f);
            p.set(light_uniform(i, 13), l.open);
            // The sky's sun is this room's own, not one seen through a door.
            if (lights[i].sun && !lights[i].gated && sun_color.x == 0.0f && sun_color.y == 0.0f && sun_color.z == 0.0f) {
                sun_dir = gl::normalize(lights[i].dir) * -1.0f;
                sun_color = lights[i].color;
            }
        }
        p.set("uSunDir", sun_dir);
        p.set("uSunColor", sun_color);
        p.set("uViewPos", cam.eye);
        p.set("uTime", static_cast<float>(world_time_));
        p.set("uShadowTexel", 1.0f / static_cast<float>(maps.array.size()),
              1.0f / static_cast<float>(maps.array.size()));
        p.set("uShadowMaps", 1);
        p.set("uStraddle", 0.0f);
        p.set("uScreenRect", 0.0f, 0.0f, 1.0f, 1.0f);
        p.set("uTex", 0);
        p.set("uCRT", 0.0f);
        p.set("uScreenUV", 0.0f);
        p.set("uViewport", vp_w_, vp_h_);
        p.set("uUVRect", 0.0f, 0.0f, 0.0f, 0.0f);
        if (host_air_.on && !clips.empty()) {
            const HalfSpace& door = clips.front();
            p.set("uHostFog", host_air_.density, host_air_.start, host_air_.full, 1.0f);
            p.set("uHostFogColor", host_air_.color);
            p.set("uDoorPlane", static_cast<float>(door.normal.x), static_cast<float>(door.normal.y), static_cast<float>(door.normal.z),
                  static_cast<float>(door.offset));
        } else {
            p.set("uHostFog", 0.0f, 0.0f, 0.0f, 0.0f);
        }
    };

    // A room's own settings on the program drawing it (in use as scene_):
    // its look, time, where its copy is lit, its sides of its doorways.
    const auto prepare = [&](const Mix& look, const gl::Vec3& shift, const PlacedRoom& placed, const Spatial3D& room) {
        apply_uniforms(*scene_, look, passes::scene);
        apply_attended(*scene_, passes::scene);
        scene_->set("uLatticeShift", shift);
        scene_->set("uTime",static_cast<float>(semantic_time(graph_?graph_:(root_?root_->graph_:nullptr),room)));
        // This room's side of each doorway, from the portals as they are now,
        // and whatever the caller cuts away besides.
        int n = 0;
        for (const HalfSpace& h : clips) {
            if (n >= kMaxBounds) break;
            scene_->set(clip_uniform(n++), static_cast<float>(h.normal.x),
                        static_cast<float>(h.normal.y), static_cast<float>(h.normal.z),
                        static_cast<float>(h.offset));
        }
        for (Key d : placed.doorways) {
            const Element* portal = room.find(d);
            if (!portal || n >= kMaxBounds) continue;
            const HalfSpace h = room_side(room, *portal, placed.pose);
            scene_->set(clip_uniform(n++), static_cast<float>(h.normal.x),
                        static_cast<float>(h.normal.y), static_cast<float>(h.normal.z),
                        static_cast<float>(h.offset));
        }
        scene_->set("uClipCount", n);
        clip_count_ = n;
        doors_to_program(placed);
        return n;
    };
    std::vector<const Element*> sprites;

    lap(1, part_at);
    scene_ = nullptr;
    const Frustum view = frustum_of(view_proj);
    const PlacedRoom* last = nullptr;
    for (const PlacedRoom& placed : rooms) {
        if (!placed.room) continue;
        // A copy of a space that wraps, out of sight: its cell, not in view.
        if (placed.image) {
            const Vec3d cell = period_of(*placed.room);
            const double r = 0.5 * std::sqrt(cell.x * cell.x + cell.y * cell.y + cell.z * cell.z);
            const Vec3d c = placed.pose.position;
            if (!view.intersects_sphere({static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)}, static_cast<float>(r))) continue;
        }
        set_frame(placed.pose);
        const Spatial3D& room = *placed.room;
        // A copy of a space that wraps is lit where its original is.
        gl::Vec3 shift{0, 0, 0};
        if (placed.image)
            for (const PlacedRoom& own : rooms)
                if (own.room == placed.room && !own.image) shift = to_vec3(own.pose.position) - to_vec3(placed.pose.position);
        // A copy straight after its own room is that room again: the same
        // look, the same program and everything set on it, only moved.
        const bool again = placed.image && last && last->room == placed.room && placed.doorways.empty();
        last = &placed;
        int bounds = clip_count_;
        if (again) {
            scene_->set("uLatticeShift", shift);
        } else {
        // Each room in its own look: the annex seen through the doorway
        // keeps its own fog, whichever side you stand on.
        const Mix look = mix(room.id(), look_of(room));
        const gl::Program* program = program_for(look.shown(), passes::scene);
        if (program != scene_) {
            scene_ = program;
            scene_->use();
            frame_uniforms(*scene_);
        }
        bounds = prepare(look, shift, placed, room);
        }

        lap(2, part_at);
        if (room.params().num(Key{"sky"}, 0.0) > 0.5) {
            // The sky is at no distance a plane can cut: it is the room's
            // ceiling, whatever bounds its ground.
            scene_->set("uClipCount", 0);
            draw_sky(cam, zfar);
            scene_->set("uClipCount", bounds);
        } else {
            draw_room(room);
        }
        const gl::Vec3 moved = shift * -1.0f;
        for (const auto i : plan_draws(room, view, placed.image ? &moved : nullptr)) {
            const auto& e=room.elements()[i];
            if (e.kind == terrain_kind()) {
                draw_terrain(e);
            } else if (e.kind == kinds::mesh) {
                if (is_sprite(e)) sprites.push_back(&e);
                else if (instanceable(e)) batch_crate(room, e);
                else if (!batch_skinned(room, e)) draw_crate(room, e);
            } else if (e.kind == kinds::wall) {
                if (q_.instancing && e.id != highlight_)
                    batch(cube_, box_matrix(room, e).m, color_of(e, {0.52f, 0.50f, 0.48f}), 0.9f,
                          static_cast<float>(e.params.num(Key{"surface"}, 2.0)), 0, 0, 0);
                else
                    draw_wall_element(room, e);
            } else if (e.kind == kinds::light) {
                draw_lamp(room, e);
            }
        }
        lap(3, part_at);
        flush_batches(*scene_, true);
        // Pictures cut out of their cards, together, with the program that
        // may cut (cutout_of), set up for this room as the other is.
        if (!sprites.empty()) {
            const gl::Program* solid = scene_;
            const gl::Program* cut = cutout_of(*solid);
            if (cut) {
                scene_ = cut;
                cut->use();
                frame_uniforms(*cut);
                prepare(mix(room.id(), look_of(room)), shift, placed, room);
            }
            for (const Element* e : sprites) draw_sprite(room, *e);
            sprites.clear();
            if (cut) {
                scene_ = solid;
                solid->use();
            }
        }
        lap(4, part_at);
        // Doorways' frames drawn together (what each shows, one by one).
        batch_frames_ = q_.instancing;
        for (const auto i : draw_plans_.at(&room).portals) {
            const auto& e=room.elements()[i];
            // The doorway being looked through keeps its frame; only its
            // view is left out - seen from its own far side it would fill
            // the whole picture.
            draw_portal(room, e, depth, target, placed.image || (!skip_portal.empty() && e.id == skip_portal));
        }
        if (batch_frames_) flush_batches(*scene_, true);
        batch_frames_ = false;
        lap(5, part_at);
    }
    for (int i = 0; i < kMaxBounds; ++i)
        gl::glDisable(gl::GL_CLIP_DISTANCE0 + static_cast<gl::GLenum>(i));
    if (part) gl::glDisable(gl::GL_SCISSOR_TEST);
    set_frame(Pose{});
}

GLWorldView::HostAir GLWorldView::air_of(const Spatial3D& world) const {
    const LookState& look = look_of(world);
    HostAir a;
    a.on = true;
    a.density = static_cast<float>(value(look, passes::scene, Key{"uFogDensity"}, 0.0));
    a.start = static_cast<float>(value(look, passes::scene, Key{"uFogStart"}, 0.0));
    a.full = static_cast<float>(value(look, passes::scene, Key{"uFogFull"}, 0.0));
    a.color = {static_cast<float>(value(look, passes::scene, Key{"uFogColor.x"}, 0.0)), static_cast<float>(value(look, passes::scene, Key{"uFogColor.y"}, 0.0)),
               static_cast<float>(value(look, passes::scene, Key{"uFogColor.z"}, 0.0))};
    return a;
}

void GLWorldView::fit(Nested& n, int w, int h, bool anew) {
    // As big as the part of the screen it is given - grown in steps of 128
    // pixels, never past the screen: a few sizes, each made once.
    const auto step = [](int v, int most) { return std::min(most, (std::max(v, 1) + 127) / 128 * 128); };
    if (!anew && n.target.valid() && n.target.width() >= w && n.target.height() >= h) return;
    const int was_w = anew || !n.target.valid() ? 0 : n.target.width(), was_h = anew || !n.target.valid() ? 0 : n.target.height();
    n.target.create(step(std::max(w, was_w), target_w_), step(std::max(h, was_h), target_h_), gl::GL_RGBA16F, 0, true);
    n.target.bind();
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
}

void GLWorldView::make_root_view(RootView& v, int w, int h) {
    v.ms.create(w, h, gl::GL_RGBA16F, msaa_, true);
    v.target.create(w, h, gl::GL_RGBA16F, 0, false);
    for (gl::RenderTarget* t : {&v.ms, &v.target}) {
        t->bind();
        gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    }
}

void GLWorldView::sample_screen(const Rect& r, float fx, float fy) const {
    // Where `r` is in the part of the screen being drawn, 0..1 across it.
    const float w = sub_.x1 - sub_.x0, h = sub_.y1 - sub_.y0;
    scene_->set("uScreenRect", (r.x0 - sub_.x0) / w, (r.y0 - sub_.y0) / h, (r.x1 - sub_.x0) / w, (r.y1 - sub_.y0) / h);
    scene_->set("uViewport", vp_w_, vp_h_);
    if (fx < 1.0f || fy < 1.0f) scene_->set("uUVRect", 0.0f, 0.0f, fx, fy);
}

uint64_t GLWorldView::chain_stamp(const State& st, const Element& e) {
    uint64_t h = e.params.stamp();
    const Element* cur = &e;
    for (int i = 0; i < 8; ++i) {
        if (!cur->params.has(keys::parent)) break;
        const std::string* parent_id = std::get_if<std::string>(&cur->params.get(keys::parent));
        if (!parent_id || parent_id->empty()) break;
        const Element* parent = st.find(Key{*parent_id});
        if (!parent) break;
        h = (h * 1099511628211ULL) ^ parent->params.stamp();
        cur = parent;
    }
    return h;
}

auto GLWorldView::placed_of(const State& st, const Element& e) const -> Placed& {
    Placed& p = placed_[&e];
    const uint64_t stamp = chain_stamp(st, e);
    if (p.stamp != stamp) p = Placed{stamp};
    return p;
}

Pose GLWorldView::pose_of(const State& st, const Element& e) const {
    Placed& p = placed_of(st, e);
    if (!p.posed) p.pose = world_pose(st, e), p.posed = true;
    return p.pose;
}

const RoomMatrix& GLWorldView::box_matrix(const State& st, const Element& e) const {
    Placed& p = placed_of(st, e);
    if (!p.boxed) p.box = box_model(st, e), p.boxed = true;
    return p.box;
}

void GLWorldView::draw_sky(const Camera& cam, float zfar) {
    const gl::Mat4 m = gl::Mat4::translate(cam.eye) * gl::Mat4::scale(gl::Vec3{1, 1, 1} * (zfar * 1.8f));
    scene_->set("uModel", m);
    scene_->set("uTexModel", m);
    scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
    scene_->set("uRoughness", 1.0f);
    scene_->set("uSurface", 9.0f);
    scene_->set("uEmissive", 0.0f);
    scene_->set("uHighlight", 0.0f);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
    sphere_.draw();
}

std::vector<float> GLWorldView::sample_ground(const std::function<double(double, double)>& height, int n, double reach, double step, double cx, double cz) {
    const int half = n / 2;
    const double grow = std::max(0.0, (reach - step * half) / (static_cast<double>(half) * half));
    std::vector<double> off(static_cast<std::size_t>(n + 1));
    for (int i = 0; i <= n; ++i) {
        const double k = i - half, a = std::fabs(k);
        off[static_cast<std::size_t>(i)] = (k < 0 ? -1.0 : 1.0) * (step * a + grow * a * a);
    }
    const auto at = [n](int i, int j) { return static_cast<std::size_t>(j) * static_cast<std::size_t>(n + 1) + static_cast<std::size_t>(i); };
    std::vector<double> hgt(static_cast<std::size_t>((n + 1) * (n + 1)));
    const int workers = static_cast<int>(std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w)
        pool.emplace_back([&, w] {
            for (int j = w; j <= n; j += workers)
                for (int i = 0; i <= n; ++i)
                    hgt[at(i, j)] = height(cx + off[static_cast<std::size_t>(i)], cz + off[static_cast<std::size_t>(j)]);
        });
    for (std::thread& th : pool) th.join();
    std::vector<gl::Vec3> nrm(hgt.size());
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const int i0 = std::max(0, i - 1), i1 = std::min(n, i + 1);
            const int j0 = std::max(0, j - 1), j1 = std::min(n, j + 1);
            const double dx = (hgt[at(i1, j)] - hgt[at(i0, j)]) / (off[static_cast<std::size_t>(i1)] - off[static_cast<std::size_t>(i0)]);
            const double dz = (hgt[at(i, j1)] - hgt[at(i, j0)]) / (off[static_cast<std::size_t>(j1)] - off[static_cast<std::size_t>(j0)]);
            nrm[at(i, j)] = gl::normalize({static_cast<float>(-dx), 1.0f, static_cast<float>(-dz)});
        }
    std::vector<float> v;
    v.reserve(static_cast<std::size_t>(n) * n * 6 * 8);
    const auto put = [&](int i, int j) {
        const std::size_t k = at(i, j);
        const float x = static_cast<float>(cx + off[static_cast<std::size_t>(i)]);
        const float z = static_cast<float>(cz + off[static_cast<std::size_t>(j)]);
        v.insert(v.end(), {x, static_cast<float>(hgt[k]), z, nrm[k].x, nrm[k].y, nrm[k].z, x * 0.1f, z * 0.1f});
    };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            put(i, j);
            put(i, j + 1);
            put(i + 1, j + 1);
            put(i, j);
            put(i + 1, j + 1);
            put(i + 1, j);
        }
    return v;
}

void GLWorldView::ensure_terrain(const Element& e, const Camera& cam) {
    auto it = terrains_.find(e.id);
    if (it == terrains_.end() || !it->second.height) return;
    TerrainMesh& t = it->second;
    const int n = std::max(16, std::min(400, static_cast<int>(e.params.num(Key{"cells"}, 160.0)))) / 2 * 2;
    const double reach = e.params.num(Key{"reach"}, 320.0);
    const double step = e.params.num(Key{"step"}, 0.5);
    const double snap = e.params.num(Key{"snap"}, 2.0);
    const double rev = e.params.num(Key{"rev"}, 0.0);
    const double cx = std::floor(cam.eye.x / snap) * snap, cz = std::floor(cam.eye.z / snap) * snap;

    // A finished resample goes in, if it is still the ground wanted.
    if (t.job.valid() && t.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        std::vector<float> v = t.job.get();
        if (t.job_rev == rev) {
            t.mesh.update(v);
            t.cx = t.job_cx;
            t.cz = t.job_cz;
            t.rev = t.job_rev;
        }
    }
    if (t.mesh.valid() && cx == t.cx && cz == t.cz && rev == t.rev) return;
    if (!t.mesh.valid() || rev != t.rev) {
        // Nothing sensible to draw meanwhile: wait for it.
        if (t.job.valid()) t.job.wait();
        t.mesh.update(sample_ground(t.height, n, reach, step, cx, cz));
        t.cx = cx;
        t.cz = cz;
        t.rev = rev;
        return;
    }
    if (t.job.valid()) return;  // one at a time; the next frame asks again
    t.job_cx = cx;
    t.job_cz = cz;
    t.job_rev = rev;
    t.job = std::async(std::launch::async, [height = t.height, n, reach, step, cx, cz] {
        return sample_ground(height, n, reach, step, cx, cz);
    });
}

void GLWorldView::draw_terrain(const Element& e) {
    auto it = terrains_.find(e.id);
    if (it == terrains_.end() || !it->second.mesh.valid()) return;
    set_model(room_local(gl::Mat4::identity()));
    scene_->set("uAlbedo", color_of(e, {0.80f, 0.62f, 0.42f}));
    scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.85)));
    scene_->set("uSurface", static_cast<float>(e.params.num(Key{"surface"}, 8.0)));
    scene_->set("uEmissive", 0.0f);
    scene_->set("uHighlight", 0.0f);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
    it->second.mesh.draw();
}

bool GLWorldView::is_doorway(const State& host,const Element& e) const {
    auto it = worlds_.find(e.id);
    if(it==worlds_.end() || !it->second.world) return false;
    return declared_world(host,e,*it->second.world);
}

const gl::Mesh& GLWorldView::shape_of(const State& st, const Element& e) const {
    if (e.kind != kinds::mesh) return cube_;
    // Asked every frame, in every pass, of every thing: remembered until
    // the thing's parameters change.
    auto& memo = shape_memo_[&e];
    if (memo.first == e.params.stamp() && memo.second) return *memo.second;
    const gl::Mesh& m = find_shape(st, e);
    memo = {e.params.stamp(), &m};
    return m;
}

const gl::Mesh& GLWorldView::find_shape(const State& st, const Element& e) const {
    static const Key shape{"shape"}, bevel{"bevel"}, taper{"taper"}, model{"model"};
    const std::string s = e.params.get_or<std::string>(shape, "");
    // One of its state's own models (sg/domains/Shapes.hpp): made into a
    // mesh the first time it is drawn, kept after.
    if (s == "model") {
        const auto* space = dynamic_cast<const Spatial3D*>(&st);
        const std::vector<float>* corners = space ? space->model(Key{e.params.get_or<std::string>(model, "")}) : nullptr;
        if (!corners || corners->empty()) return cube_;
        gl::Mesh& m = model_meshes_[corners];
        if (!m.valid()) m.create(*corners);
        return m;
    }
    const double tp = e.params.num(taper, 1.0);
    if (s == "sphere") return sphere_;
    if (s == "cylinder") {
        if (tp == 1.0) return cylinder_;
        const std::string key = "c" + std::to_string(std::lround(tp * 1000));
        gl::Mesh& m = shaped_[key];
        if (!m.valid()) m.create(gl::cylinder_vertices(28, static_cast<float>(tp)));
        return m;
    }
    const double r = e.params.num(bevel, 0.0);
    if (r <= 0.0 && tp == 1.0) return cube_;
    // Sizes to the millimetre: near enough the same box is the same mesh.
    const auto mm = [](double v) { return std::to_string(std::lround(v * 1000)); };
    const double sx = e.params.num(keys::sx, 1.0), sy = e.params.num(keys::sy, 1.0), sz = e.params.num(keys::sz, 1.0);
    const std::string key = "b" + mm(sx) + "," + mm(sy) + "," + mm(sz) + "," + mm(r) + "," + mm(tp);
    gl::Mesh& m = shaped_[key];
    if (!m.valid())
        m.create(gl::rounded_box_vertices(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz),
                                          static_cast<float>(r), static_cast<float>(tp)));
    return m;
}

RoomMatrix GLWorldView::box_model(const State& st, const Element& e) const {
    return room_local(box_transform(st,e));
}

// (Its whole turn is in its pose, its anchors' with it.)
gl::Mat4 GLWorldView::panel_turn(const Pose& pose, const Element&) {
    return gl::Mat4::rotate_y(static_cast<float>(pose.yaw)) *
           gl::Mat4::rotate_z(static_cast<float>(pose.pitch)) *
           gl::Mat4::rotate_x(static_cast<float>(pose.roll));
}

gl::Vec3 GLWorldView::panel_normal(const Pose& pose, const Element&) {
    return to_vec3(facing(pose));
}

float GLWorldView::sheet_thickness(const Element& e) {
    return static_cast<float>(e.params.num(Key{"thick"}, 0.004));
}

RoomMatrix GLWorldView::portal_frame_model(const State& st, const Element& e) const {
    const Pose pose = pose_of(st, e);
    const float w = static_cast<float>(e.params.num(keys::w, 3.0));
    const float h = static_cast<float>(e.params.num(keys::h, 2.0));
    const float border = static_cast<float>(e.params.num(Key{"border"}, 0.15));
    const gl::Vec3 size = framed(e) ? gl::Vec3{0.08f, h + 2 * border - 0.08f, w + 2 * border - 0.08f}
                                    : gl::Vec3{sheet_thickness(e), h, w};
    return room_local(gl::Mat4::translate(to_vec3(pose.position)) * panel_turn(pose, e) *
                      gl::Mat4::scale(size));
}

void GLWorldView::draw_solid(const RoomMatrix& local, const gl::Vec3& albedo, float roughness, float surface, float emissive, float highlight) {
    ++times_.draws;
    set_model(local);
    scene_->set("uAlbedo", albedo);
    scene_->set("uRoughness", roughness);
    scene_->set("uSurface", surface);
    scene_->set("uEmissive", emissive);
    scene_->set("uHighlight", highlight);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
    cube_.draw();
}

bool GLWorldView::has_walls(const State& st) {
    for (const auto& e : st.elements())
        if (e.kind == kinds::wall && e.alive) return true;
    return false;
}

void GLWorldView::set_model(const RoomMatrix& local) {
    scene_->set("uModel", frame_matrix_ * local.m);
    scene_->set("uTexModel", local.m);
}

void GLWorldView::draw_room(const Spatial3D& world) {
    for(const auto& draw:enclosure(world))
        draw_solid(room_local(draw.model),{static_cast<float>(draw.colour.r),static_cast<float>(draw.colour.g),static_cast<float>(draw.colour.b)},static_cast<float>(draw.roughness),static_cast<float>(draw.surface));
}

void GLWorldView::draw_wall_element(const State& st, const Element& e) {
    // `surface` picks a wall's material; plaster if it does not say.
    draw_solid(box_matrix(st, e), color_of(e, {0.52f, 0.50f, 0.48f}), 0.9f, static_cast<float>(e.params.num(Key{"surface"}, 2.0)));
}

bool GLWorldView::instanceable(const Element& e) const {
    static const Key worn{"skin"}, hung{"straddle"};
    if (!q_.instancing || e.id == highlight_ || e.params.has(worn) || e.params.num(hung, 0.0) > 0.5) return false;
    const auto skin = surfaces_.find(e.id);
    if (skin != surfaces_.end() && skin->second.surface) return false;
    // Hung from a thing that wears a texture, it wears it too: drawn on its
    // own (skin_holder says in whose frame).
    const std::string parent = e.params.get_or<std::string>(keys::parent, "");
    const auto worn_by = parent.empty() ? surfaces_.end() : surfaces_.find(Key{parent});
    return worn_by == surfaces_.end() || !worn_by->second.surface;
}

const Element* GLWorldView::skin_holder(const State& st, const Element& e) const {
    const Element* at = &e;
    for (int i = 0; i < 8 && at; ++i) {
        const auto s = surfaces_.find(at->id);
        // A thing's parts wear only a texture, never a sheet bound to it.
        if (s != surfaces_.end() && s->second.surface && (at == &e || dynamic_cast<const Texture*>(s->second.surface)) &&
            declared_surface(*at, *s->second.surface))
            return at;
        const std::string parent = at->params.get_or<std::string>(keys::parent, "");
        at = parent.empty() ? nullptr : st.find(Key{parent});
    }
    return nullptr;
}

namespace {
// The inverse of a matrix that moves, turns and scales (no projection).
gl::Mat4 affine_inverse(const gl::Mat4& a) {
    const float* m = a.m;
    const float c00 = m[5] * m[10] - m[9] * m[6], c01 = m[9] * m[2] - m[1] * m[10], c02 = m[1] * m[6] - m[5] * m[2];
    const float det = m[0] * c00 + m[4] * c01 + m[8] * c02;
    const float k = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
    gl::Mat4 r;
    r.m[0] = c00 * k, r.m[1] = c01 * k, r.m[2] = c02 * k;
    r.m[4] = (m[8] * m[6] - m[4] * m[10]) * k, r.m[5] = (m[0] * m[10] - m[8] * m[2]) * k, r.m[6] = (m[4] * m[2] - m[0] * m[6]) * k;
    r.m[8] = (m[4] * m[9] - m[8] * m[5]) * k, r.m[9] = (m[8] * m[1] - m[0] * m[9]) * k, r.m[10] = (m[0] * m[5] - m[4] * m[1]) * k;
    r.m[3] = r.m[7] = r.m[11] = 0.0f, r.m[15] = 1.0f;
    for (int i = 0; i < 3; ++i) r.m[12 + i] = -(r.m[i] * m[12] + r.m[4 + i] * m[13] + r.m[8 + i] * m[14]);
    return r;
}
}  // namespace

auto GLWorldView::skin_frame(const State& st, const Element& holder) const -> const SkinFrame& {
    SkinFrame& f = skin_frames_[&holder];
    if (f.frame == frame_count_) return f;
    f.frame = frame_count_;
    const bool mesh = holder.kind == kinds::mesh || holder.kind == kinds::wall;
    if (mesh) {
        f.to_unit = affine_inverse(box_matrix(st, holder).m);
        f.size = {static_cast<float>(holder.params.num(keys::sx, 1.0)), static_cast<float>(holder.params.num(keys::sy, 1.0)),
                  static_cast<float>(holder.params.num(keys::sz, 1.0))};
        return f;
    }
    // A thing of parts: the box round all of them, in its own turn.
    const Pose at = pose_of(st, holder);
    Vec3d lo{1e30, 1e30, 1e30}, hi{-1e30, -1e30, -1e30};
    for (const Element& d : st.elements()) {
        if (!d.alive || (d.kind != kinds::mesh && d.kind != kinds::wall) || skin_holder(st, d) != &holder) continue;
        const gl::Mat4& m = box_matrix(st, d).m;
        for (float x : {-0.5f, 0.5f})
            for (float y : {-0.5f, 0.5f})
                for (float z : {-0.5f, 0.5f}) {
                    const gl::Vec3 c = m.transform_point({x, y, z});
                    const Vec3d l = local_of(at, {c.x, c.y, c.z});
                    lo = {std::min(lo.x, l.x), std::min(lo.y, l.y), std::min(lo.z, l.z)};
                    hi = {std::max(hi.x, l.x), std::max(hi.y, l.y), std::max(hi.z, l.z)};
                }
    }
    if (lo.x > hi.x) lo = hi = Vec3d{};
    const gl::Vec3 size{static_cast<float>(std::max(hi.x - lo.x, 1e-3)), static_cast<float>(std::max(hi.y - lo.y, 1e-3)),
                        static_cast<float>(std::max(hi.z - lo.z, 1e-3))};
    const gl::Vec3 mid{static_cast<float>((lo.x + hi.x) * 0.5), static_cast<float>((lo.y + hi.y) * 0.5), static_cast<float>((lo.z + hi.z) * 0.5)};
    const gl::Mat4 frame = gl::Mat4::translate(to_vec3(at.position)) * panel_turn(at, holder) * gl::Mat4::translate(mid) * gl::Mat4::scale(size);
    f.to_unit = affine_inverse(frame);
    f.size = size;
    return f;
}

void GLWorldView::upload_skin(BoundSurface& bound) {
    Surface2D& surf = *bound.surface;
    const auto& pixels = surf.raster();
    if (!bound.texture.valid() || bound.texture.width() != surf.px_w() || bound.texture.height() != surf.px_h()) {
        bound.texture.create(surf.px_w(), surf.px_h(), /*mipmaps=*/true, surf.srgb());
        bound.revision = ~uint64_t{0};
    }
    if (bound.revision != surf.revision()) {
        bound.texture.upload(pixels);
        bound.revision = surf.revision();
    }
}

bool GLWorldView::batch_skinned(const State& st, const Element& e) {
    if (!q_.instancing || e.id == highlight_ || e.params.num(Key{"straddle"}, 0.0) > 0.5 || e.params.has(Key{"skin"})) return false;
    if (skin_holder(st, e) != &e) return false;
    const auto it = surfaces_.find(e.id);
    if (it == surfaces_.end() || !dynamic_cast<const Texture*>(it->second.surface)) return false;
    upload_skin(it->second);
    append_record(st, e, batch_for(shape_of(st, e), &it->second));
    return true;
}

void GLWorldView::batch_crate(const State& st, const Element& e) { append_record(st, e, batch_for(shape_of(st, e))); }

void GLWorldView::append_record(const State& st, const Element& e, Batch& b) {
    Placed& p = placed_of(st, e);
    if (!p.recorded) {
        const gl::Mat4& m = box_matrix(st, e).m;
        const gl::Vec3 c = color_of(e, {0.8f, 0.5f, 0.25f});
        std::copy(m.m, m.m + 16, p.record.begin());
        const float mat[8] = {c.x, c.y, c.z, static_cast<float>(e.params.num(Key{"roughness"}, 0.6)),
                              static_cast<float>(e.params.num(Key{"surface"}, 3.0)),
                              static_cast<float>(e.params.num(Key{"emissive"}, 0.0)), 0.0f,
                              static_cast<float>(e.params.num(Key{"mirror"}, 0.0))};
        std::copy(mat, mat + 8, p.record.begin() + 16);
        p.recorded = true;
    }
    b.data.insert(b.data.end(), p.record.begin(), p.record.end());
}

auto GLWorldView::batch_for(const gl::Mesh& mesh, BoundSurface* skin) -> Batch& {
    for (Batch& x : batches_)
        if (x.mesh == &mesh && x.skin == skin) return x;
    return batches_.emplace_back(Batch{&mesh, {}, skin});
}

void GLWorldView::batch(const gl::Mesh& mesh, const gl::Mat4& local, const gl::Vec3& albedo, float roughness, float surface, float emissive, float highlight, float mirror) {
    Batch& b = batch_for(mesh);
    b.data.insert(b.data.end(), local.m, local.m + 16);
    b.data.insert(b.data.end(), {albedo.x, albedo.y, albedo.z, roughness, surface, emissive, highlight, mirror});
}

void GLWorldView::flush_batches(const gl::Program& p, bool scene) {
    bool any = false;
    // A box's far side is never seen from outside it: in the scene it is not
    // drawn, or far off - where depth is coarser than a wall is thick - the
    // two sides fight for every pixel. (Only the box: its faces are wound
    // outward; other shapes are drawn whole. The shadow pass culls as it
    // likes: its own, set before it.)
    for (Batch& b : batches_) {
        if (b.data.empty()) continue;
        if (scene) {
            if (b.mesh == &cube_) gl::glEnable(gl::GL_CULL_FACE), gl::glCullFace(gl::GL_BACK);
            else gl::glDisable(gl::GL_CULL_FACE);
        }
        if (!any) {
            p.set("uInstanced", 1);
            p.set("uFrame", frame_matrix_);
            if (scene) {
                p.set("uTexMix", 0.0f);
                p.set("uGlow", 0.0f);
                p.set("uSkin", 0.0f);
                p.set("uScreenUV", 0.0f);
                p.set("uCRT", 0.0f);
            }
            any = true;
        }
        const auto n = static_cast<gl::GLsizei>(b.data.size() / gl::Mesh::kInstanceFloats);
        const auto* tex = scene && b.skin ? dynamic_cast<const Texture*>(b.skin->surface) : nullptr;
        if (tex) {
            const Element& m = tex->map();
            b.skin->texture.bind(0);
            p.set("uTexMix", 1.0f), p.set("uSkin", 1.0f), p.set("uSkinFramed", 1.0f), p.set("uSkinOwn", 1.0f);
            p.set("uSkinTile", static_cast<float>(m.params.num("tile", 0.0)));
            p.set("uSkinBlend", static_cast<float>(m.params.num("blend", 0.0)));
            p.set("uSkinRelief", static_cast<float>(m.params.num("relief", 0.0)));
        }
        b.mesh->draw_instanced(instances_.upload(b.data), n);
        if (tex) p.set("uTexMix", 0.0f), p.set("uSkin", 0.0f), p.set("uSkinFramed", 0.0f), p.set("uSkinOwn", 0.0f), p.set("uSkinTile", 0.0f), p.set("uSkinBlend", 0.0f), p.set("uSkinRelief", 0.0f);
        if (scene) ++times_.draws, times_.instanced += n;
        b.data.clear();
    }
    if (any) p.set("uInstanced", 0);
    if (scene) gl::glDisable(gl::GL_CULL_FACE);
}

bool GLWorldView::is_sprite(const Element& e) {
    static const Key shape{"shape"};
    return e.params.has(shape) && e.params.get_or<std::string>(shape, "") == "sprite";
}

bool GLWorldView::bind_picture(const State& st, const std::string& name) {
    const auto* space = dynamic_cast<const Spatial3D*>(&st);
    const Spatial3D::Picture* pic = space ? space->picture(Key{name}) : nullptr;
    if (!pic || pic->w <= 0 || pic->h <= 0) return false;
    PictureTexture& t = picture_textures_[pic];
    if (!t.texture.valid() || t.texture.width() != pic->w || t.texture.height() != pic->h) {
        t.texture.create(pic->w, pic->h, /*mipmaps=*/false, /*srgb=*/true, /*pixel=*/true);
        t.revision = ~uint64_t{0};
    }
    if (t.revision != pic->revision) {
        t.texture.upload(pic->rgba);
        t.revision = pic->revision;
    }
    t.texture.bind(0);
    return true;
}

void GLWorldView::draw_sprite(const State& st, const Element& e) {
    if (!bind_picture(st, e.params.get_or<std::string>(Key{"picture"}, ""))) return;
    ++times_.draws;
    const gl::Vec3 c = (frame_matrix_ * box_matrix(st, e).m).transform_point({0, 0, 0});
    gl::Vec3 n, up;
    if (e.params.num(Key{"face"}, 0.0) > 0.5) {
        n = gl::normalize(cam_forward_ * -1.0f);
        up = gl::normalize(cam_up_ - n * gl::dot(cam_up_, n));
    } else {
        n = cam_eye_ - c;
        n.y = 0;
        n = gl::dot(n, n) > 1e-8f ? gl::normalize(n) : gl::Vec3{1, 0, 0};
        up = {0, 1, 0};
    }
    const gl::Vec3 side = gl::cross(n, up);  // the card's +z: to the eye's left
    const float w = static_cast<float>(e.params.num(keys::sx, 1.0)), h = static_cast<float>(e.params.num(keys::sy, 1.0));
    gl::Mat4 m;
    m.m[0] = n.x, m.m[1] = n.y, m.m[2] = n.z;
    m.m[4] = up.x * h, m.m[5] = up.y * h, m.m[6] = up.z * h;
    m.m[8] = side.x * w, m.m[9] = side.y * w, m.m[10] = side.z * w;
    m.m[12] = c.x, m.m[13] = c.y, m.m[14] = c.z;
    scene_->set("uModel", m);
    scene_->set("uTexModel", m);
    const float frames = static_cast<float>(std::max(1.0, e.params.num(Key{"frames"}, 1.0)));
    const float frame = std::fmod(std::max(0.0f, std::floor(static_cast<float>(e.params.num(Key{"frame"}, 0.0)))), frames);
    scene_->set("uUVRect", frame / frames, 0.0f, 1.0f / frames, 1.0f);
    scene_->set("uCutout", 1.0f);
    scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
    scene_->set("uRoughness", 1.0f);
    scene_->set("uSurface", 0.0f);
    scene_->set("uEmissive", 0.0f);
    scene_->set("uHighlight", 0.0f);
    scene_->set("uMirror", 0.0f);
    scene_->set("uTexMix", 1.0f);
    scene_->set("uSkin", 0.0f);
    scene_->set("uScreenUV", 0.0f);
    scene_->set("uCRT", 0.0f);
    // `glow`: how much it lights itself - a lit thing, or one held up to
    // the eye, whatever the light where it is.
    scene_->set("uGlow", static_cast<float>(e.params.num(Key{"glow"}, 0.0)));
    quad_.draw();
    scene_->set("uUVRect", 0.0f, 0.0f, 0.0f, 0.0f);
    scene_->set("uCutout", 0.0f);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
}

void GLWorldView::draw_crate(const State& st, const Element& e) {
    ++times_.draws;
    // `straddle`: it hangs in a doorway, half in each room, and is lit as
    // that by one rule wherever it is drawn - in the room the eye is in, or
    // seen through the doorway - each point by the rooms it is in, as far as
    // it is in them (uStraddle): the two pictures of it are one, and shut or
    // ajar it is lit the same. Seen through the doorway it is drawn whole: no
    // plane cuts it.
    struct Hung {
        const gl::Program* p;
        float on;
        int clips = -1;
        ~Hung() {
            if (on < 0.5f) return;
            p->set("uStraddle", 0.0f);
            if (clips >= 0) p->set("uClipCount", clips);
        }
    } hung{scene_, static_cast<float>(e.params.num(Key{"straddle"}, 0.0))};
    if (hung.on > 0.5f) {
        scene_->set("uStraddle", 1.0f);
        if (guest_pass_) hung.clips = clip_count_, scene_->set("uClipCount", 0);
    }
    set_model(box_matrix(st, e));
    scene_->set("uAlbedo", color_of(e, {0.8f, 0.5f, 0.25f}));
    scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.6)));
    scene_->set("uSurface", static_cast<float>(e.params.num(Key{"surface"}, 3.0)));
    scene_->set("uEmissive", static_cast<float>(e.params.num(Key{"emissive"}, 0.0)));
    scene_->set("uHighlight", e.id == highlight_ ? 1.0f : 0.0f);
    scene_->set("uGlow", 0.0f);
    // `mirror`: how much of the real sky it reflects, rather than the
    // light from all round - glossy stone, still water, under a sky.
    const float mirror = static_cast<float>(e.params.num(Key{"mirror"}, 0.0));
    scene_->set("uMirror", mirror);
    // A picture of its state's, tiled over the world.
    if (e.params.has(Key{"skin"}) && bind_picture(st, e.params.get_or<std::string>(Key{"skin"}, ""))) {
        scene_->set("uTexMix", 1.0f);
        scene_->set("uSkin", 2.0f);
        scene_->set("uTile", static_cast<float>(e.params.num(Key{"tile"}, 1.0)));
        scene_->set("uScreenUV", 0.0f);
        scene_->set("uCRT", 0.0f);
        shape_of(st, e).draw();
        scene_->set("uSkin", 0.0f);
        scene_->set("uTexMix", 0.0f);
        if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
        return;
    }
    // A surface bound to a mesh is its skin: an atlas, a cell a face - or a
    // texture worn by a thing it is part of.
    const Element* holder = skin_holder(st, e);
    auto skin = holder ? surfaces_.find(holder->id) : surfaces_.end();
    if (skin != surfaces_.end()) {
        BoundSurface& bound = skin->second;
        Surface2D& surf = *bound.surface;
        const auto& pixels = surf.raster();
        if (!bound.texture.valid() || bound.texture.width() != surf.px_w() || bound.texture.height() != surf.px_h()) {
            bound.texture.create(surf.px_w(), surf.px_h(), /*mipmaps=*/true, surf.srgb());
            bound.revision = ~uint64_t{0};
        }
        if (bound.revision != surf.revision()) {
            bound.texture.upload(pixels);
            bound.revision = surf.revision();
        }
        bound.texture.bind(0);
        scene_->set("uTexMix", 1.0f);
        scene_->set("uSkin", 1.0f);
        scene_->set("uScreenUV", 0.0f);
        scene_->set("uCRT", 0.0f);
        // A texture says how it is worn: tiled round the thing by the metre,
        // and how softly a curve goes from one way's cell to the next.
        const auto* tex = dynamic_cast<const Texture*>(&surf);
        if (tex) {
            const Element& m = tex->map();
            const SkinFrame& f = skin_frame(st, *holder);
            scene_->set("uSkinFramed", 1.0f);
            scene_->set("uSkinFrame", f.to_unit);
            scene_->set("uSkinSize", f.size);
            scene_->set("uSkinTile", static_cast<float>(m.params.num("tile", 0.0)));
            scene_->set("uSkinBlend", static_cast<float>(m.params.num("blend", 0.0)));
            scene_->set("uSkinRelief", static_cast<float>(m.params.num("relief", 0.0)));
        }
        shape_of(st, e).draw();
        if (tex) scene_->set("uSkinFramed", 0.0f), scene_->set("uSkinTile", 0.0f), scene_->set("uSkinBlend", 0.0f), scene_->set("uSkinRelief", 0.0f);
        scene_->set("uSkin", 0.0f);
        scene_->set("uTexMix", 0.0f);
        if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
        return;
    }
    scene_->set("uTexMix", 0.0f);
    shape_of(st, e).draw();
    if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
}

void GLWorldView::draw_lamp(const Spatial3D& world, const Element& e) {
    // A sun hangs from nothing; nor does a lamp that says it is no fixture.
    if (e.params.num(Key{"fixture"}, 1.0) < 0.5 || e.params.num(Key{"sun"}, 0.0) > 0.5) return;
    const gl::Vec3 pos = to_vec3(pose_of(world, e).position);
    const gl::Vec3 color = color_of(e, {1.0f, 0.93f, 0.82f});
    const float room_h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));

    draw_solid(room_local(gl::Mat4::translate({pos.x, pos.y + 0.12f, pos.z}) *
                   gl::Mat4::scale({0.62f, 0.22f, 0.62f})),
               {0.12f, 0.11f, 0.10f}, 0.4f, 0.0f);
    // The shade glows while the lamp is on, and is only glass when it is off.
    const float lit = static_cast<float>(std::min(1.0, e.params.num(keys::intensity, 1.0) * 2.0));
    draw_solid(room_local(gl::Mat4::translate(pos) * gl::Mat4::scale({0.30f, 0.16f, 0.30f})), color, 0.2f,
               0.0f, 6.0f * lit + 0.02f);
    const float stem = std::max(0.05f, room_h - pos.y - 0.2f);
    draw_solid(room_local(gl::Mat4::translate({pos.x, pos.y + 0.2f + stem * 0.5f, pos.z}) *
                   gl::Mat4::scale({0.035f, stem, 0.035f})),
               {0.09f, 0.09f, 0.10f}, 0.8f, 0.0f);
}

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
    slot_.clear();
    for (std::size_t k = 0; k < draw.size(); ++k) {
        const std::size_t i = draw[k];
        const ViewJob& j = jobs_[i];
        if (k >= pool_.size()) pool_.emplace_back();
        Nested& n = pool_[k];
        slot_[j.key] = k;
        const Element* back = !j.wp->back.empty() ? j.wp->world->find(j.wp->back) : back_portal(*j.wp->world, *j.host);
        path_ = j.key;
        // Drawn only where its doorway is on the screen, at the screen's own
        // pixels, into a picture as big as that: a view costs what of the
        // screen it covers. (Its rect to the pixel, a pixel over, so that it
        // is sampled one to one.)
        const float W = static_cast<float>(target_w_), H = static_cast<float>(target_h_);
        const float px0 = std::max(0.0f, std::floor((j.seen.x0 + 1) * 0.5f * W) - 1), px1 = std::min(W, std::ceil((j.seen.x1 + 1) * 0.5f * W) + 1);
        const float py0 = std::max(0.0f, std::floor((j.seen.y0 + 1) * 0.5f * H) - 1), py1 = std::min(H, std::ceil((j.seen.y1 + 1) * 0.5f * H) + 1);
        fit(n, static_cast<int>(px1 - px0), static_cast<int>(py1 - py0));
        n.rect = Rect{px0 / W * 2 - 1, py0 / H * 2 - 1, px1 / W * 2 - 1, py1 / H * 2 - 1};
        n.fx = (px1 - px0) / static_cast<float>(n.target.width()), n.fy = (py1 - py0) / static_cast<float>(n.target.height());
        sub_ = n.rect;
        host_air_ = air_of(*j.host);
        draw_world(seen(*j.wp->world), camera_of(j.there), aspect, n.target, j.depth + 1, kNear,
                   back ? back->id.key() : Key{}, {portal_clip(*j.host, *j.portal, j.from, j.there)});
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
    // is no picture of it to draw.
    if (e.params.has(Key{"ball"})) return;
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
        // drawn; a doorway walked through wants it on the plane (0).
        const float inset = static_cast<float>(e.params.num(Key{"inset"}, 0.06));
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
        if (e.params.num(Key{"tunnel"}, 0.0) > 0.0 && side >= 0.0f && side < kNear * 2.0f) {
            const float back = kNear * 2.0f - side;
            const float k = (std::max(side, 0.002f) + back) / std::max(side, 0.002f);
            const gl::Vec3 plane = pos + n * inset, eye = to_vec3(eye_here);
            const gl::Vec3 centre = eye + (plane - eye) * k;
            set_model(room_local(gl::Mat4::translate(centre) * turned *
                      gl::Mat4::scale({1.0f, h * k, w * k})));
            gl::glDepthRange(0.0, 0.0);
            quad_.draw();
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
        const auto& pixels = surf.raster();
        // A surface that has changed size gets a texture its new size.
        if (!bound.texture.valid() || bound.texture.width() != surf.px_w() || bound.texture.height() != surf.px_h()) {
            bound.texture.create(surf.px_w(), surf.px_h(), /*mipmaps=*/true, surf.srgb());
            bound.revision = ~uint64_t{0};
        }
        if (bound.revision != surf.revision()) {
            bound.texture.upload(pixels);
            bound.revision = surf.revision();
        }
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

void GLWorldView::run_ao(float strength, float radius) {
    if (!ao_prog_) {
        ao_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_fs(), "ao");
        ao_blur_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_blur_fs(), "ao blur");
        ao_apply_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::ao_apply_fs(), "ao apply");
    }
    gl::glDisable(gl::GL_DEPTH_TEST);
    scene_target_.blit_depth_to(depth_);
    const float tan_half = std::tan(view_.fov * 0.5f);

    ao_a_.bind();
    ao_prog_->use();
    ao_prog_->set("uDepth", 0);
    depth_.bind_depth(0);
    ao_prog_->set("uTexel", 1.0f / static_cast<float>(ao_a_.width()), 1.0f / static_cast<float>(ao_a_.height()));
    ao_prog_->set("uNear", view_.znear);
    ao_prog_->set("uFar", view_.zfar);
    ao_prog_->set("uTanHalf", tan_half);
    ao_prog_->set("uAspect", view_.aspect);
    ao_prog_->set("uRadius", radius);
    screen_.draw();

    ao_b_.bind();
    ao_blur_prog_->use();
    ao_blur_prog_->set("uAO", 0);
    ao_blur_prog_->set("uDepth", 1);
    ao_a_.bind_color(0);
    depth_.bind_depth(1);
    ao_blur_prog_->set("uTexel", 1.0f / static_cast<float>(ao_b_.width()), 1.0f / static_cast<float>(ao_b_.height()));
    ao_blur_prog_->set("uNear", view_.znear);
    ao_blur_prog_->set("uFar", view_.zfar);
    screen_.draw();

    lit_.bind();
    ao_apply_prog_->use();
    ao_apply_prog_->set("uScene", 0);
    ao_apply_prog_->set("uAO", 1);
    ao_apply_prog_->set("uStrength", strength);
    ao_apply_prog_->set("uDepth", 2);
    ao_apply_prog_->set("uNear", view_.znear);
    ao_apply_prog_->set("uFar", view_.zfar);
    ao_apply_prog_->set("uTexel", 1.0f / static_cast<float>(lit_.width()), 1.0f / static_cast<float>(lit_.height()));
    resolve_.bind_color(0);
    ao_b_.bind_color(1);
    depth_.bind_depth(2);
    screen_.draw();
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

void GLWorldView::run_bloom() {
    gl::glDisable(gl::GL_DEPTH_TEST);
    bloom_a_.bind();
    gl::glClear(gl::GL_COLOR_BUFFER_BIT);
    const gl::Program& bright = *program_for(post_.shown(), passes::bright);
    bright.use();
    apply_uniforms(bright, post_, passes::bright);
    bright.set("uScene", 0);
    scene_src_->bind_color(0);
    screen_.draw();

    const gl::Program& blur = *program_for(post_.shown(), passes::blur);
    blur.use();
    apply_uniforms(blur, post_, passes::blur);
    blur.set("uSource", 0);
    const float tx = 1.0f / static_cast<float>(bloom_a_.width());
    const float ty = 1.0f / static_cast<float>(bloom_a_.height());
    const long rounds = std::lround(setting(post_, passes::blur, "passes", q_.bloom_passes));
    for (long i = 0; i < rounds; ++i) {
        bloom_b_.bind();
        blur.set("uDirection", tx, 0.0f);
        bloom_a_.bind_color(0);
        screen_.draw();

        bloom_a_.bind();
        blur.set("uDirection", 0.0f, ty);
        bloom_b_.bind_color(0);
        screen_.draw();
    }
    run_wide_bloom(static_cast<float>(setting(post_, passes::blur, "wide", 0.5)));
}

void GLWorldView::run_wide_bloom(float wide) {
    if (wide <= 0.0f || bloom_levels_ == 0) return;
    if (!bloom_down_) {
        bloom_down_ = std::make_unique<gl::Program>(gl::post_vs(), gl::bloom_down_fs(), "bloom down");
        bloom_up_ = std::make_unique<gl::Program>(gl::post_vs(), gl::bloom_up_fs(), "bloom up");
    }
    const auto texel = [](const gl::RenderTarget& t) {
        return std::pair{1.0f / static_cast<float>(t.width()), 1.0f / static_cast<float>(t.height())};
    };
    bloom_down_->use();
    bloom_down_->set("uSource", 0);
    const gl::RenderTarget* from = &bloom_a_;
    for (int i = 0; i < bloom_levels_; ++i) {
        bloom_chain_[i].bind();
        from->bind_color(0);
        const auto [tx, ty] = texel(*from);
        bloom_down_->set("uTexel", tx, ty);
        bloom_down_->set("uFirst", i == 0 ? 1.0f : 0.0f);
        screen_.draw();
        from = &bloom_chain_[i];
    }
    bloom_up_->use();
    bloom_up_->set("uSource", 0);
    bloom_up_->set("uWeight", 1.0f);
    gl::glEnable(gl::GL_BLEND);
    gl::glBlendFunc(gl::GL_ONE, gl::GL_ONE);
    for (int i = bloom_levels_ - 1; i > 0; --i) {
        bloom_chain_[i - 1].bind();
        bloom_chain_[i].bind_color(0);
        const auto [tx, ty] = texel(bloom_chain_[i]);
        bloom_up_->set("uTexel", tx, ty);
        screen_.draw();
    }
    // Every level has added the whole of the light once: their sum,
    // divided among them, is as bright as the tight bloom it came from.
    bloom_a_.bind();
    bloom_chain_[0].bind_color(0);
    const auto [tx, ty] = texel(bloom_chain_[0]);
    bloom_up_->set("uTexel", tx, ty);
    bloom_up_->set("uWeight", 1.0f / static_cast<float>(bloom_levels_));
    gl::glBlendColor(0.0f, 0.0f, 0.0f, std::min(wide, 1.0f));
    gl::glBlendFunc(gl::GL_CONSTANT_ALPHA, gl::GL_ONE_MINUS_CONSTANT_ALPHA);
    screen_.draw();
    gl::glDisable(gl::GL_BLEND);
}

void GLWorldView::composite(int fb_w, int fb_h) {
    if (output_) {
        output_->bind();
    } else {
        gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
        gl::glViewport(0, 0, fb_w, fb_h);
    }
    gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
    scene_src_->bind_color(0);
    bloom_a_.bind_color(1);

    const auto draw = [&](const gl::Program& p) {
        p.use();
        apply_uniforms(p, post_, passes::composite);
        apply_attended(p, passes::composite);
        if (rays_on_) {
            p.set("uRayDir", to_vec3(rays_dir_));
            p.set("uCamFwd", to_vec3(rays_fwd_));
            p.set("uTanHalf", static_cast<float>(rays_tan_));
            p.set("uRays", static_cast<float>(std::max(0.0, rays_strength_)));
            p.set("uRayColor", gl::Vec3{static_cast<float>(rays_colour_.r), static_cast<float>(rays_colour_.g), static_cast<float>(rays_colour_.b)});
        }
        p.set("uScene", 0);
        p.set("uBloom", 1);
        p.set("uTime", static_cast<float>(world_time_));
        p.set("uTexel", 1.0f / static_cast<float>(fb_w), 1.0f / static_cast<float>(fb_h));
        screen_.draw();
    };
    // Looks that share a program share its draw.
    programs_in_mix_.clear();
    for (const Mix::Part& part : post_.parts) {
        const gl::Program* p = program_for(*part.look, passes::composite);
        auto it = std::find_if(programs_in_mix_.begin(), programs_in_mix_.end(),
                               [p](const auto& e) { return e.first == p; });
        if (it == programs_in_mix_.end()) {
            programs_in_mix_.emplace_back(p, part.weight);
        } else {
            it->second += part.weight;
        }
    }
    // A running weighted mean: the k-th program is blended over the ones
    // before it with alpha = its weight / the weight drawn so far.
    float drawn = 0.0f;
    for (const auto& e : programs_in_mix_) {
        drawn += e.second;
        if (drawn == e.second) {
            draw(*e.first);
            continue;
        }
        gl::glEnable(gl::GL_BLEND);
        gl::glBlendColor(0.0f, 0.0f, 0.0f, e.second / drawn);
        gl::glBlendFunc(gl::GL_CONSTANT_ALPHA, gl::GL_ONE_MINUS_CONSTANT_ALPHA);
        draw(*e.first);
        gl::glDisable(gl::GL_BLEND);
    }
    gl::glEnable(gl::GL_DEPTH_TEST);
}

void GLWorldView::advance_fades() {
    dt_ = std::max(0.0, fixed_step_);
    fader_.advance(dt_);
}

void GLWorldView::apply_attended(const gl::Program& p, Key pass) {
    if (!attend_) return;
    const Mix& am = mix(Key{"attend:" + attend_->id().str()}, look_of(*attend_));
    std::vector<Key> keys;
    for (const Mix::Part& part : am.parts)
        if (const Element* e = part.look->find(pass))
            for (const auto& kv : e->params)
                if (is_uniform_key(kv.first) && kv.first.str().find('.') == std::string::npos &&
                    std::find(keys.begin(), keys.end(), kv.first) == keys.end())
                    keys.push_back(kv.first);
    for (Key k : keys) p.set(k.str().c_str(), static_cast<float>(fader_.value(am, pass, k, 0.0)));
}

void GLWorldView::apply_uniforms(const gl::Program& p, const Mix& m, Key pass) {
    uint64_t key = (1469598103934665603ULL ^ reinterpret_cast<std::uintptr_t>(&p)) * 1099511628211ULL;
    key = (key ^ std::hash<Key>{}(pass)) * 1099511628211ULL;
    key = (key ^ stamp_of(standard_)) * 1099511628211ULL;
    for (const Mix::Part& part : m.parts) {
        uint32_t w = 0;
        std::memcpy(&w, &part.weight, sizeof w);
        key = (key ^ reinterpret_cast<std::uintptr_t>(part.look)) * 1099511628211ULL;
        key = (key ^ w) * 1099511628211ULL;
        key = (key ^ stamp_of(*part.look)) * 1099511628211ULL;
    }
    if (auto hit = uniform_memo_.find(key); hit != uniform_memo_.end()) {
        for (const UniformSet& u : hit->second) p.put(u.at, u.n, u.v);
        return;
    }
    if (uniform_memo_.size() > 256) uniform_memo_.clear();
    ++times_.uniforms_resolved;
    std::vector<UniformSet>& record = uniform_memo_[key];
    uniform_keys_.clear();
    const auto collect = [&](const LookState& l) {
        const Element* e = l.find(pass);
        if (!e) return;
        for (const auto& kv : e->params)
            if (is_uniform_key(kv.first) &&
                std::find(uniform_keys_.begin(), uniform_keys_.end(), kv.first) ==
                    uniform_keys_.end())
                uniform_keys_.push_back(kv.first);
    };
    collect(standard_);
    for (const Mix::Part& part : m.parts) collect(*part.look);
    vectors_.clear();
    for (Key k : uniform_keys_) {
        const std::string& name = k.str();
        const float v = static_cast<float>(fader_.value(m, pass, k, 0.0));
        const std::size_t dot = name.find('.');
        if (dot == std::string::npos) {
            p.set(name.c_str(), v);
            record.push_back(UniformSet{p.uniform(name.c_str()), 1, {v, 0, 0}});
            continue;
        }
        const std::string base = name.substr(0, dot);
        const char c = dot + 1 < name.size() ? name[dot + 1] : 'x';
        const int i = c == 'y' ? 1 : (c == 'z' ? 2 : 0);
        auto it = std::find_if(vectors_.begin(), vectors_.end(),
                               [&](const VectorUniform& u) { return u.name == base; });
        if (it == vectors_.end()) {
            vectors_.push_back(VectorUniform{base, {0, 0, 0}, 0});
            it = vectors_.end() - 1;
        }
        it->v[i] = v;
        it->n = std::max(it->n, i + 1);
    }
    for (const VectorUniform& u : vectors_) {
        if (u.n == 3) {
            p.set(u.name.c_str(), gl::Vec3{u.v[0], u.v[1], u.v[2]});
        } else if (u.n == 2) {
            p.set(u.name.c_str(), u.v[0], u.v[1]);
        } else {
            p.set(u.name.c_str(), u.v[0]);
        }
        record.push_back(UniformSet{p.uniform(u.name.c_str()), u.n, {u.v[0], u.v[1], u.v[2]}});
    }
}

const gl::Program* GLWorldView::program_for(const LookState& look, Key pass) {
    const std::string& vs = source(look, pass, look_keys::vs);
    const std::string& fs = source(look, pass, look_keys::fs);
    PassProgram& slot = resolved_[look.id()][pass];
    if (slot.program && slot.vs == vs && slot.fs == fs) return slot.program;
    slot.vs = vs;
    slot.fs = fs;
    slot.error.clear();
    slot.program = shared_program(vs, fs, look.id().str() + "." + pass.str(), slot.error);
    // A shader that will not build is reported by prepare(); the pass
    // falls back to the built-in rather than drawing nothing.
    if (!slot.program && &look != &standard_) slot.program = program_for(standard_, pass);
    return slot.program;
}

const std::string& GLWorldView::source(const LookState& look, Key pass, Key which) const {
    if (const Element* e = look.find(pass))
        if (e->params.has(which))
            if (const auto* s = std::get_if<std::string>(&e->params.get(which)))
                if (!s->empty()) return *s;
    const auto& b = builtin_.at(pass);
    return which == look_keys::vs ? b.first : b.second;
}

const gl::Program* GLWorldView::shared_program(const std::string& vs, const std::string& fs, const std::string& tag, std::string& error) {
    std::string key;
    key.reserve(vs.size() + fs.size() + 1);
    key += vs;
    key += '\0';
    key += fs;
    auto it = programs_.find(key);
    if (it != programs_.end()) return it->second.get();
    const auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<gl::Program> p;
    try {
        p = std::make_unique<gl::Program>(vs.c_str(), fs.c_str(), tag.c_str());
    } catch (const std::exception& e) {
        error = e.what();
    }
    stats_.compile_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
            .count();
    if (!p) return nullptr;
    ++stats_.programs;
    if (!preparing_) ++stats_.late;
    const gl::Program* made = programs_.emplace(std::move(key), std::move(p)).first->second.get();
    sources_of_[made] = {vs, fs};
    return made;
}

const gl::Program* GLWorldView::cutout_of(const gl::Program& p) {
    if (auto it = cutout_.find(&p); it != cutout_.end()) return it->second;
    const auto src = sources_of_.find(&p);
    const gl::Program* twin = nullptr;
    if (src != sources_of_.end()) {
        std::string fs = src->second.second;
        const std::size_t line = fs.find('\n');
        fs.insert(line == std::string::npos ? 0 : line + 1, "#define SG_CUTOUT\n");
        std::string error;
        twin = shared_program(src->second.first, fs, "cutout", error);
    }
    return cutout_[&p] = twin;
}

void GLWorldView::check_look(const LookState& look, std::vector<std::string>& out) {
    static const std::unordered_map<Key, std::vector<const char*>> required{
        {passes::shadow, {"uModel", "uLightViewProj"}},
        // Without the clip planes two glued rooms both draw their shared wall.
        {passes::scene, {"uModel", "uViewProj", "uClipCount"}},
        {passes::bright, {"uScene"}},
        {passes::blur, {"uSource", "uDirection"}},
        {passes::composite, {"uScene"}},
    };
    const std::string tag = "look " + look.id().str() + ": ";
    for (Key pass : passes::all()) {
        const gl::Program* p = program_for(look, pass);
        const PassProgram& slot = resolved_[look.id()][pass];
        if (!slot.error.empty()) {
            const std::string& e = slot.error;
            out.push_back(tag + pass.str() + " shader does not build, so the built-in is used - " +
                          e.substr(0, e.find('\n')));
            continue;
        }
        const Element* el = look.find(pass);
        const bool custom = el && (el->params.has(look_keys::vs) || el->params.has(look_keys::fs));
        if (custom)
            for (const char* name : required.at(pass))
                if (!p->has(name))
                    out.push_back(tag + "its " + pass.str() + " shader has no " + name +
                                  ", which the renderer sets on every draw");
        if (!el) continue;
        std::vector<std::string> named;  // a vector's three components are one name
        for (const auto& kv : el->params) {
            if (!is_uniform_key(kv.first)) continue;
            const std::string& name = kv.first.str();
            const std::string base = name.substr(0, name.find('.'));
            if (std::find(named.begin(), named.end(), base) != named.end()) continue;
            named.push_back(base);
            if (!p->has(base.c_str()))
                out.push_back(tag + pass.str() + " sets " + base +
                              ", which its shader does not have (misspelt, or declared "
                              "and never used)");
        }
    }
}

const char* GLWorldView::clip_uniform(int i) {
    static const auto names = [] {
        std::array<std::string, kMaxBounds> n;
        for (int b = 0; b < kMaxBounds; ++b)
            n[static_cast<std::size_t>(b)] = "uClip[" + std::to_string(b) + "]";
        return n;
    }();
    return names[static_cast<std::size_t>(i)].c_str();
}

const char* GLWorldView::shadow_uniform(std::size_t i, int field) {
    static const auto names = [] {
        std::array<std::array<std::string, 2>, kShadowMaps> n;
        for (std::size_t l = 0; l < kShadowMaps; ++l) {
            n[l][0] = "uShadowVP[" + std::to_string(l) + "]";
            n[l][1] = "uShadowBias[" + std::to_string(l) + "]";
        }
        return n;
    }();
    return names[i][static_cast<std::size_t>(field)].c_str();
}

const char* GLWorldView::light_uniform(std::size_t i, int field) {
    static const auto names = [] {
        static const char* fields[] = {"uLightPos", "uLightDir", "uLightColor", "uLightPower",
                                       "uCosInner", "uCosOuter", "uLightSun", "uLightFloor",
                                       "uLightIndirect", "uLightFalloff", "uLightGate", "uLightGateAxis",
                                       "uLightNear", "uLightOpen"};
        std::array<std::array<std::string, 14>, kMaxLights> n;
        for (std::size_t l = 0; l < kMaxLights; ++l)
            for (int f = 0; f < 14; ++f)
                n[l][static_cast<std::size_t>(f)] =
                    std::string(fields[f]) + "[" + std::to_string(l) + "]";
        return n;
    }();
    return names[i][static_cast<std::size_t>(field)].c_str();
}

auto GLWorldView::shadows_for(const void* world, const void* view) -> ShadowSet& {
    // Never more than the views there can be: past that, the set asked for
    // longest ago goes - one, never all of them at once (every view would
    // draw its maps again in the same frame).
    if (shadow_sets_.size() >= 64 && !shadow_sets_.count({world, view})) {
        auto oldest = shadow_sets_.begin();
        for (auto it = shadow_sets_.begin(); it != shadow_sets_.end(); ++it)
            if (it->second->used < oldest->second->used) oldest = it;
        shadow_sets_.erase(oldest);
    }
    auto& set = shadow_sets_[{world, view}];
    if (!set) set = std::make_unique<ShadowSet>();
    set->used = frame_count_;
    return *set;
}

uint64_t GLWorldView::mix_bits(uint64_t h, float f) {
    uint32_t b = 0;
    std::memcpy(&b, &f, sizeof b);
    return (h ^ b) * 1099511628211ULL;
}

uint64_t GLWorldView::scene_key(const std::vector<PlacedRoom>& rooms, int w, int h) const {
    uint64_t k = 1469598103934665603ULL;
    const auto mix = [&k](uint64_t v) { k = (k ^ v) * 1099511628211ULL; };
    const auto bits = [](double d) {
        uint64_t b = 0;
        std::memcpy(&b, &d, sizeof b);
        return b;
    };
    mix(static_cast<uint64_t>(w) << 32 | static_cast<uint64_t>(h));
    mix(bits(world_time_));
    mix(reinterpret_cast<std::uintptr_t>(attend_));
    if (eye_override_) mix(eye_override_->params.stamp());
    const auto& feeds = shared_feeds();
    for (const PlacedRoom& p : rooms) {
        if (!p.room) continue;
        const Spatial3D& room = *p.room;
        mix(reinterpret_cast<std::uintptr_t>(&room));
        for (double v : {p.pose.position.x, p.pose.position.y, p.pose.position.z, p.pose.yaw, p.pose.pitch, p.pose.roll}) mix(bits(v));
        mix(room.params().stamp());
        const LookState& look = look_of(room);
        for (Key pass : {passes::scene, passes::shadow})
            if (const Element* e = look.find(pass)) mix(e->params.stamp());
        for (const Element& e : room.elements()) {
            const bool drawn = e.kind == kinds::mesh || e.kind == kinds::wall || e.kind == kinds::light || e.kind == kinds::portal ||
                               e.kind == kinds::anchor || e.kind == kinds::camera || e.kind == terrain_kind();
            if (!drawn) continue;
            mix(e.params.stamp() << 1 | (e.alive ? 1u : 0u));
            if (const auto s = surfaces_.find(e.id); s != surfaces_.end() && s->second.surface) mix(s->second.surface->revision());
            if (e.kind != kinds::portal) continue;
            if (const auto f = feeds.find(signal_of(e)); f != feeds.end()) mix(f->second.drawn_of), mix(static_cast<uint64_t>(f->second.front));
            if (const auto it = worlds_.find(e.id); it != worlds_.end() && it->second.world && it->second.world != &room)
                mix(stamp_of(*it->second.world));
        }
    }
    return k | 1;
}

uint64_t GLWorldView::feed_key(const Feed& f) const {
    uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
    mix(stamp_of(*f.world));
    mix(stamp_of(look_of(*f.world)));
    const double t = semantic_time(graph_, *f.world);
    uint64_t b = 0;
    std::memcpy(&b, &t, sizeof b);
    mix(b);
    if (f.eye) mix(f.eye->params.stamp());
    mix(static_cast<uint64_t>(f.rw) << 32 | static_cast<uint64_t>(f.rh));
    for (const Element& e : f.world->elements())
        if (e.kind == kinds::portal && e.alive)
            if (auto it = worlds_.find(e.id); it != worlds_.end() && it->second.world && it->second.world != f.world)
                mix(stamp_of(*it->second.world));
    return h | 1;  // (never 0: 0 is "never drawn")
}

uint64_t GLWorldView::stamp_of(const State& s) const {
    auto& at = stamps_[&s];
    if (at.first != frame_count_ + 1) at = {frame_count_ + 1, s.data_version()};
    return at.second;
}

uint64_t GLWorldView::rooms_key(const std::vector<PlacedRoom>& rooms) const {
    uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
    for (const PlacedRoom& p : rooms) {
        if (!p.room || p.image) continue;
        mix(reinterpret_cast<std::uintptr_t>(p.room));
        mix(stamp_of(*p.room));
        for (double v : {p.pose.position.x, p.pose.position.y, p.pose.position.z, p.pose.yaw, p.pose.pitch, p.pose.roll}) {
            uint64_t b = 0;
            std::memcpy(&b, &v, sizeof b);
            mix(b);
        }
        for (Key d : p.doorways) mix(std::hash<Key>{}(d));
    }
    return h;
}

uint64_t GLWorldView::worlds_stamp() const {
    // Every world a doorway opens onto, and how it is seen: what comes in
    // through a doorway is theirs.
    uint64_t h = 1469598103934665603ULL;
    for (const auto& [id, wp] : worlds_) {
        if (!wp.world) continue;
        h = (h ^ std::hash<Key>{}(id)) * 1099511628211ULL;
        h = (h ^ stamp_of(*wp.world)) * 1099511628211ULL;
        h = (h ^ stamp_of(look_of(*wp.world))) * 1099511628211ULL;
    }
    return h;
}

std::vector<GLWorldView::Caster> GLWorldView::casters_of(const std::vector<PlacedRoom>& rooms) {
    std::vector<Caster> out;
    for (std::size_t r = 0; r < rooms.size(); ++r) {
        const PlacedRoom& placed = rooms[r];
        if (!placed.room || placed.image) continue;
        set_frame(placed.pose);
        uint64_t frame_hash = 1469598103934665603ULL;
        for (float f : frame_matrix_.m) frame_hash = mix_bits(frame_hash, f);
        for (const auto& e : placed.room->elements()) {
            if (!e.alive) continue;
            Caster c;
            c.room = r, c.element = &e;
            if ((e.kind == kinds::mesh || e.kind == kinds::wall) && !is_sprite(e)) {
                // A lamp's own shade does not shadow its lamp; a sprite, a
                // picture turned to the eye, has no shape to cast.
                if (e.params.num(Key{"cast"}, 1.0) < 0.5) continue;
                // Where it is and what shape: worked out once each time
                // its parameters (or its anchor's) change - a change of
                // colour or glow is no change to a shadow.
                Placed& p = placed_of(*placed.room, e);
                if (!p.hashed) {
                    // To the tenth of a millimetre: a cord settling by less
                    // than that draws no shadow again.
                    uint64_t w = 1469598103934665603ULL;
                    for (float f : box_matrix(*placed.room, e).m.m) w = mix_bits(w, std::round(f * 1e4f));
                    w = (w ^ reinterpret_cast<std::uintptr_t>(&shape_of(*placed.room, e))) * 1099511628211ULL;
                    p.where = w, p.hashed = true;
                }
                if (p.held_in != frame_hash) {
                    const DrawBound b = query_bounds(box_matrix(*placed.room, e));
                    p.held_at = b.centre, p.held_r = b.radius, p.held_in = frame_hash;
                }
                c.centre = p.held_at, c.radius = p.held_r, c.bounded = true, c.where = p.where;
            } else if (e.kind == terrain_kind()) {
                auto t = terrains_.find(e.id);
                if (t == terrains_.end()) continue;
                uint64_t w = 1469598103934665603ULL;
                w = mix_bits(w, static_cast<float>(t->second.cx));
                w = mix_bits(w, static_cast<float>(t->second.cz));
                w = mix_bits(w, static_cast<float>(t->second.rev));
                c.where = w;
            } else if (e.kind == kinds::portal && !is_doorway(*placed.room, e) && has_surface(e)) {
                const RoomMatrix frame = portal_frame_model(*placed.room, e);
                uint64_t w = 1469598103934665603ULL;
                for (float f : frame.m.m) w = mix_bits(w, f);
                const DrawBound b = query_bounds(frame);
                c.centre = b.centre, c.radius = b.radius, c.bounded = true, c.where = w;
            } else {
                continue;
            }
            out.push_back(c);
        }
    }
    set_frame(Pose{});
    return out;
}

bool GLWorldView::shades(const Caster& c, const Frustum& sees, const Light& light) {
    if (!c.bounded) return true;
    if (!sees.intersects_sphere({c.centre.x, c.centre.y, c.centre.z}, c.radius)) return false;
    if (light.gated) {
        // (The same plane the caster pass cuts at: a hand's breadth of slack.)
        const float side = gl::dot(light.gate_in, {c.centre.x, c.centre.y, c.centre.z}) + 0.08f - gl::dot(light.gate_in, light.gate_at);
        if (side < -c.radius) return false;
    }
    return true;
}

}  // namespace sg::render
