#include "sg/gl/World.hpp"

#include "sg/domains/Atlas.hpp"

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

GLWorldView::Rect GLWorldView::ball_rect(const Spatial3D& world, const Element& e, const Camera& cam, float aspect) const {
    // Where a ball is on the screen: round its middle, as wide as it looks
    // from the eye (a little more, for the stretch off the middle of the view).
    const gl::Vec3 c = to_vec3(pose_of(world, e).position), to = c - cam.eye;
    const float r = static_cast<float>(e.params.num(Key{"ball"}, 0.0)), dist = std::sqrt(gl::dot(to, to));
    const gl::Vec3 f = gl::normalize(cam.forward), right = gl::normalize(gl::cross(f, cam.up)), top = gl::cross(right, f);
    const float z = gl::dot(to, f);
    if (dist < r * 1.5f || z < r * 1.5f || cam.ortho > 0.0f) return Rect{-1, -1, 1, 1};
    const float ty = std::tan(cam.fov * 0.5f), tx = ty * aspect, spread = std::tan(std::asin(std::min(r / dist, 0.999f))) * 1.3f;
    const float x = gl::dot(to, right) / (z * tx), y = gl::dot(to, top) / (z * ty), hx = spread / tx * (dist / z), hy = spread / ty * (dist / z);
    return Rect{std::max(x - hx, -1.0f), std::max(y - hy, -1.0f), std::min(x + hx, 1.0f), std::min(y + hy, 1.0f)};
}

void GLWorldView::capture_glass(const Spatial3D& world, const Element& e, WorldPortal& wp) {
    constexpr int kSize = 128;
    const bool fresh = !wp.env.valid();
    // A face a frame while the glass moves (carried, shaken), until all six
    // are taken from where it came to rest; standing, none - a reflection is
    // a hint of the room, not worth a room drawn again every few frames.
    const Vec3d at = pose_of(world, e).position;
    const bool moved = at.x != wp.env_at.x || at.y != wp.env_at.y || at.z != wp.env_at.z;
    wp.env_at = at;
    if (moved) wp.env_wait = 6;
    if (!fresh && !moved && wp.env_wait <= 0) return;
    if (!moved) --wp.env_wait;
    wp.env.create(kSize);
    if (fresh) wp.env_face.create(kSize, kSize, gl::GL_RGBA16F, 0, true);
    const gl::Vec3 c = to_vec3(pose_of(world, e).position);
    // Each way a cube's faces look, and which way is up in each (as a cube
    // map is read).
    static const gl::Vec3 ways[6][2] = {{{1, 0, 0}, {0, -1, 0}}, {{-1, 0, 0}, {0, -1, 0}}, {{0, 1, 0}, {0, 0, 1}},
                                        {{0, -1, 0}, {0, 0, -1}}, {{0, 0, 1}, {0, -1, 0}}, {{0, 0, -1}, {0, -1, 0}}};
    const Rect was = sub_;
    const std::string path = path_;
    sub_ = Rect{-1, -1, 1, 1};
    path_ = "/glass/" + e.id.str();
    for (int n = 0; n < (fresh ? 6 : 1); ++n) {
        const int i = fresh ? n : wp.env_next;
        Camera cam;
        cam.eye = c, cam.forward = ways[i][0], cam.up = ways[i][1], cam.fov = 3.14159265f * 0.5f, cam.ortho = 0.0f;
        // (Seen from its surface out: nothing in the ball - the glass, what
        // stands in it - is what it reflects.)
        const float r = static_cast<float>(e.params.num(Key{"ball"}, 0.0));
        draw_world(seen(world), cam, 1.0f, wp.env_face, /*depth=*/1, std::max(kNear, r * 1.02f), e.id.key(), {});
        wp.env.take(i, wp.env_face.framebuffer(), kSize, kSize);
    }
    wp.env_next = (wp.env_next + 1) % 6;
    sub_ = was;
    path_ = path;
}

bool GLWorldView::ball_window(const Element& e) const {
    if (!e.params.has(Key{"ball"}) || e.params.num(Key{"window"}, 1.0) < 0.5) return false;
    const auto it = worlds_.find(e.id);
    if (it == worlds_.end() || !it->second.world || it->second.back.empty()) return false;
    const Element* far = it->second.world->find(it->second.back);
    return far && std::fabs(seam_scale(e, *far) - 1.0) > 1e-9;
}

void GLWorldView::bind_seams() {
    if (!graph_ || graph_->revision() == seams_at_) return;
    const StateGraph* g = graph_;
    seams_at_ = g->revision();
    for (Key k : seam_bound_) worlds_.erase(k);
    seam_bound_.clear();
    for (const Seam& s : g->seams())
        for (std::size_t i = 0; i < s.boundary_a.size() && i < s.boundary_b.size(); ++i)
            for (const bool ab : {true, false}) {
                const Key from = ab ? s.a : s.b, to = ab ? s.b : s.a;
                const Key here = ab ? s.boundary_a[i] : s.boundary_b[i], there = ab ? s.boundary_b[i] : s.boundary_a[i];
                const auto* world = dynamic_cast<const Spatial3D*>(g->find(to));
                if (!world || !dynamic_cast<const Spatial3D*>(g->find(from))) continue;
                // The seam's travel: carried by its two doorways where they
                // are when it is used, never where they were.
                bind_world(here, world, [g, from, here, to, there](const Element& src, Element& dst) {
                    const State* A = g->find(from);
                    const State* B = g->find(to);
                    const Element* h = A ? A->find(here) : nullptr;
                    const Element* t = B ? B->find(there) : nullptr;
                    if (h && t) portal_carry(*A, *h, *B, *t)(src, dst);
                }, there);
                seam_bound_.push_back(here);
            }
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
    const bool sized = f.w != w || f.h != h || !f.outs[0][0].valid();
    if (sized) {
        f.w = std::max(1, w), f.h = std::max(1, h);
        f.rw = f.w, f.rh = f.h, f.div = 1, f.slack = 0;
        for (auto& pair : f.outs)
            for (gl::RenderTarget& o : pair) o.destroy();
        f.drawn = false;
    }
    if (!f.view) {
        GLQuality q = q_;
        q.shadow_size = std::min(q_.shadow_size, 1024);
        f.view = std::make_unique<GLWorldView>(q);
    }
    f.view->set_fixed_step(fixed_step_);
    f.view->keeps_sizes_ = true;
    if (sized) {
        // Written as the composite writes the screen, encoded; read back
        // as the panel reads any picture, decoded. Two: one drawn while
        // the other is shown, so a feed that sees its own screen sees the
        // picture it made the frame before. Made at every size the screen
        // may ask for (feed_detail) and kept, with the targets its view
        // draws it with: the picture follows how big the screen is seen by
        // taking the set that is there - not by making one mid-frame.
        std::vector<std::pair<int, int>> sizes;
        for (int d : {1, 2, 4, 8}) {
            if (d > 1 && (!f.live || f.h / d < 64)) break;
            for (gl::RenderTarget& o : f.outs[Feed::slot_of(d)]) {
                o.create(std::max(1, f.w / d), std::max(1, f.h / d), gl::GL_SRGB8_ALPHA8, 0, false);
                o.mipmap();  // (its levels made now: the first time is not when it is first drawn)
            }
            sizes.emplace_back(std::max(1, f.w / d), std::max(1, f.h / d));
        }
        f.view->sizes_ = std::move(sizes);
        f.view->sizes_kept_ = false;
    }
}

void GLWorldView::unbind_feed(Key portal_element) {
    auto it = feeds_.find(portal_element);
    if (it == feeds_.end()) return;
    release_feed(it->second);
    feeds_.erase(it);
}

void GLWorldView::release_feed(Feed& f) {
    for (auto& pair : f.outs)
        for (gl::RenderTarget& o : pair) o.destroy();
    if (f.view) f.view->release_targets();
}

void GLWorldView::warm(const std::vector<Spatial3D*>& worlds, int fb_w, int fb_h) {
    const LookFader fader = fader_;
    const Mix post = post_;
    // (And the eye as it was adjusted: what warming measures is of other worlds.)
    const double ev = exposure_.ev, ev_target = exposure_.target;
    const bool ev_known = exposure_.known, ev_settle = exposure_.settle;
    const float ev_weight = exposure_.weight;
    let_go_.held = true;
    pack_skins();
    for (Spatial3D* w : worlds)
        if (w) render(*w, fb_w, fb_h);
    // And every screen's picture, through the screen's own view, whether or
    // not it was in sight from any of those eyes: a painting or a set first
    // turned to is not where its view is made (a world's first picture in a
    // view of its own costs tens of milliseconds - its shaders, its targets).
    for (auto& [id, f] : feeds_)
        if (f.world && f.view && declared_feed(id, *f.world)) {
            f.drawn = true;
            draw_feed(f);
        }
    // And every picture a thing wears, made on the card now: one first seen
    // through a doorway, or round a corner, is not made in the frame it is
    // seen (a painted floor's maps take tens of milliseconds to upload).
    for (auto& [id, bound] : surfaces_)
        if (bound.surface) upload_skin(bound);
    gl::glFinish();
    fader_ = fader;
    post_ = post;
    exposure_.ev = ev, exposure_.target = ev_target, exposure_.known = ev_known, exposure_.settle = ev_settle;
    exposure_.weight = ev_weight;
    exposure_.asked[0] = exposure_.asked[1] = false;
    let_go_.held = false;
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
    let_go_.now();
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
    aim_rays(*rooms.front().room);
    // The light from all round of each room seen, relit before anything of
    // this frame is drawn (relighting draws).
    if (!root_ && !baking_) light_rooms(rooms);
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
        for (auto it = feeds_.begin(); it != feeds_.end();) {
            const bool gone = it->second.from_graph && !it->second.seen;
            if (gone) release_feed(it->second);
            it = gone ? feeds_.erase(it) : std::next(it);
        }
    }
    // Feeds first, each whole, into its own picture: a screen showing a
    // world shows it as it is this frame. A feed is drawn if its screen
    // is in view here - or stands in a world another feed drawn shows.
    const auto feeds_from = std::chrono::steady_clock::now();
    int feed_views = 0;
    // The sizes a feed's picture may be drawn at are made now (the picture
    // having been made, or sized, since the last frame): not when a screen is
    // walked up to.
    if (!root_)
        for (auto& [id, f] : feeds_)
            if (f.view && !f.view->sizes_kept_) f.view->keep_sizes(f.view->sizes_);
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
                    // (Made already, with the feed; one asked for since -
                    // a feed that was not `live` then - is made now.)
                    for (int i = 0; i < 2; ++i)
                        if (!f.out()[i].valid()) f.out()[i].create(f.rw, f.rh, gl::GL_SRGB8_ALPHA8, 0, false);
                }
            }
            if (draw_feed(f)) ++feed_views;
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
    adapt_exposure();
    const Camera eye_cam = eye_override_ ? camera_of(*eye_override_) : camera_of(world);
    const float aspect = static_cast<float>(fb_w) / static_cast<float>(fb_h);

    // --- portal views, one pass per window --------------------------------
    // Rendered first, at framebuffer resolution, because the portal quad
    // samples them in screen space. The guest's camera was already carried
    // through the doorway by the embedding's functor.
    // A view drawing a feed or a far room sees through the same doorways
    // as the view on the screen: its worlds are that one's, each seen
    // from this view's own eye (its targets its own).
    if (!root_) bind_seams();
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
        // (A door shut in it: nothing beyond is seen, nor what is seen from there.)
        if (shut_to(e, position_of(eye_of(world)))) continue;
        Element eye = it->second.world->camera();
        if (it->second.carry) it->second.carry(eye_of(world), eye);
        const Element* back = !it->second.back.empty() ? it->second.world->find(it->second.back) : back_portal(*it->second.world, world);
        view_through(*it->second.world, eye, aspect, 1, "/" + e.id.str(), screen_rect(world, e, eye_cam, aspect), 0, back ? back->id.key() : Key{});
    }
    bool views_drawn = false;
    for (const auto& e : world.elements()) {
        if (e.kind != kinds::portal || !e.alive || (e.params.has(Key{"ball"}) && !ball_window(e))) continue;
        auto it = worlds_.find(e.id);
        if (it == worlds_.end() || !it->second.world || !declared_world(world, e, *it->second.world)) continue;
        WorldPortal& wp = it->second;
        wp.shared = nullptr;
        if (!in_view(world, e, eye_cam) || !opens_from(e, position_of(eye_of(world)))) continue;
        // A door shut in the doorway stops the view: none is drawn, and none
        // of the shadows the room beyond would keep for it. (In the door's
        // own thickness - walking through the leaf - it is drawn as ever.)
        if (shut_to(e, position_of(eye_of(world)))) {
            wp.own_drawn = false;
            continue;
        }
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
            wp.own->film_of_viewer_ = true;
            // Cut as any view through a doorway is: nothing between the
            // carried eye and the far doorway, and that doorway's own view
            // left out - right at the threshold the eye stands in its frame.
            const Element* own_back = !wp.back.empty() ? wp.world->find(wp.back) : back_portal(*wp.world, world);
            wp.own->own_clips_ = {far_side(world, e, eye)};
            wp.own->own_skip_ = own_back ? own_back->id.key() : Key{};
            wp.own->render(*wp.world, fb_w, fb_h);
            wp.own->own_clips_.clear();
            wp.own->own_skip_ = Key{};
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
        // (A ball's world keeps within it: nothing stands between.)
        if (!screen && !e.params.has(Key{"ball"})) clips.push_back(far_side(world, e, eye));
        const std::string path = "/" + e.id.str();
        if (!views_drawn) draw_views(world, aspect), views_drawn = true;
        path_ = path;
        if (root_views >= root_pool_.size()) {
            if (root_pool_.size() >= kRootViewsMost) continue;
            make_root_view(root_pool_.emplace_back(), target_w_, target_h_);
            // (The same for the sizes this view is also drawn at, so that a
            // picture changing size does not make one then.)
            for (Targets& t : parked_)
                while (t.roots.size() < root_pool_.size() && t.roots.size() < kRootViewsMost) make_root_view(t.roots.emplace_back(), t.w, t.h);
        }
        RootView& view = root_pool_[root_views++];
        host_air_ = air_of(world);
        // A ball onto a world is drawn only where the ball is on the
        // screen, at the screen's own pixels: a glass on a table costs what
        // of the screen it covers.
        if (e.params.has(Key{"ball"})) capture_glass(world, e, wp);
        wp.seen = e.params.has(Key{"ball"}) ? ball_rect(world, e, eye_cam, aspect) : Rect{-1, -1, 1, 1};
        sub_ = wp.seen;
        // A doorway's view is seen only where the doorway is on the screen:
        // only that is drawn (cut_), with a margin for what stands proud of
        // its plane. (A screen's picture may be shown by another screen too.)
        if (!screen && !e.params.has(Key{"ball"})) {
            const Rect r = screen_rect(world, e, eye_cam, aspect);
            const float mx = 0.04f + 0.1f * (r.x1 - r.x0), my = 0.04f + 0.1f * (r.y1 - r.y0);
            cut_ = Rect{std::max(r.x0 - mx, -1.0f), std::max(r.y0 - my, -1.0f), std::min(r.x1 + mx, 1.0f), std::min(r.y1 + my, 1.0f)};
        }
        // Only what is past the doorway is drawn: the near plane just short
        // of it, and what is outside the pyramid through it, or this side of
        // its plane, culled before it is sent (through).
        const Through way = !screen && !e.params.has(Key{"ball"}) && back && !wp.back.empty() ? through(e, *wp.world, *back, guest_cam) : Through{};
        cull_ = way.sides;
        for (const HalfSpace& h : clips) cull_.push_back(spatial::HalfSpace{{h.normal.x, h.normal.y, h.normal.z}, h.offset});
        draw_world(seen(*wp.world), guest_cam, aspect, view.ms,
                   /*depth=*/1, way.znear, back ? back->id.key() : Key{}, clips);
        cull_.clear();
        wp.fx = vp_w_ / static_cast<float>(view.ms.width()), wp.fy = vp_h_ / static_cast<float>(view.ms.height());
        sub_ = Rect{-1, -1, 1, 1};
        host_air_.on = false;
        wp.drawn = frame_count_;
        path_.clear();
        const bool cut = scissor_to(cut_, view.ms.width(), view.ms.height());
        view.ms.blit_to(view.target);
        if (cut) gl::glDisable(gl::GL_SCISSOR_TEST);
        cut_ = Rect{-1, -1, 1, 1};
        wp.shown = &view.target;
        ++times_.portal_views;
    }
    times_.portals = since(t0);
    t0 = mark();

    // --- the room the viewer is actually standing in ------------------------
    path_.clear();
    draw_world(rooms, eye_cam, aspect, scene_target_, /*depth=*/0, kNear, own_skip_, own_clips_);
    times_.scene_cpu = since(t0);
    if (timing_) gl::glFinish();
    times_.scene = since(t0);
    t0 = mark();

    scene_target_.blit_to(resolve_);
    }
    scene_src_ = &resolve_;
    const double ao = setting(post_, passes::composite, "ao", 0.0);
    view_ = ViewParams{eye_cam.fov, aspect, kNear, static_cast<float>(world.params().num(Key{"far"}, 120.0))};
    if (ao > 0.0) {
        run_ao(static_cast<float>(ao),
               static_cast<float>(setting(post_, passes::composite, "ao.radius", 0.45)));
        scene_src_ = &lit_;
    }
    // The glow thick air spreads reads how far each pixel is (fog_bloom_glsl).
    fog_depth_ = setting(post_, passes::composite, "uFogBloom", 0.0) > 0.0;
    if (fog_depth_ && ao <= 0.0) scene_target_.blit_depth_to(depth_);
    meter_exposure();
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

bool GLWorldView::draw_feed(Feed& f) {
    // Nothing it is made from has changed: the picture it has is the one.
    const uint64_t key = feed_key(f);
    if (f.drawn_of == key && f.drawn_of != 0) return false;
    f.drawn_of = key;
    f.view->graph_ = graph_;  // the looks it is shown in are in the same graph
    f.view->root_ = this;
    f.view->output_ = &f.out()[1 - f.front];
    f.view->eye_override_ = f.eye;
    f.view->render(*f.world, f.rw, f.rh);
    f.view->output_ = nullptr;
    f.view->eye_override_ = nullptr;
    f.out()[1 - f.front].mipmap();
    f.front = 1 - f.front;
    return true;
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

auto GLWorldView::index_of(const State& s) const -> const KindIndex& {
    KindIndex& ix = kind_index_[&s];
    if (ix.structure == s.structure()) return ix;
    ix.structure = s.structure();
    ix.lights.clear(), ix.portals.clear(), ix.terrains.clear(), ix.probes.clear();
    for (const Element& e : s.elements()) {
        if (e.kind == kinds::light) ix.lights.push_back(&e);
        else if (e.kind == kinds::portal) ix.portals.push_back(&e);
        else if (e.kind == terrain_kind()) ix.terrains.push_back(&e);
        else if (e.kind == kinds::probe) ix.probes.push_back(&e);
    }
    return ix;
}

}  // namespace sg::render
