#include "sg/gl/World.hpp"
#include <set>
#include <chrono>
#include <cstring>
#include "sg/domains/Shapes.hpp"
#include "sg/core/Cache.hpp"

#include "sg/domains/Atlas.hpp"
#include "sg/domains/Walk.hpp"

#include <atomic>
#include <thread>

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
        if (const auto* look = dynamic_cast<const LookState*>(g.find(id))) {
            check_look(*look, out);
            gl::answer();  // (a shader compiled: the window answers)
        }
    preparing_ = false;
    // What things wear, made now and on the card: never in the frame it is
    // first seen, through a doorway or round a corner.
    ready_skins();
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
    const bool sized = f.w != w || f.h != h || !f.outs[0][0].valid() || f.made_raw != f.viewer_film;
    f.made_raw = f.viewer_film;
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
            if (d > 1 && f.h / d < 64) break;
            for (gl::RenderTarget& o : f.outs[Feed::slot_of(d)]) {
                o.create(std::max(1, f.w / d), std::max(1, f.h / d), f.format(), 0, false);
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

void GLWorldView::warm_ahead(const std::vector<Spatial3D*>& worlds) {
    for (Spatial3D* w : worlds) {
        if (!w || warmed_.count(w)) continue;
        warmed_.insert(w);
        for (std::size_t i = 0; i < w->elements().size(); ++i) ahead_.emplace_back(w, i);
    }
}

std::vector<Spatial3D*> GLWorldView::ahead(StateGraph& g, const State& from, int steps) const {
    // Out from `from`: through its transitions (to where any of them goes)
    // and its doorways (to the worlds they show), `steps` deep.
    std::vector<Spatial3D*> found;
    std::set<const State*> seen{&from};
    std::vector<const State*> now{&from};
    for (int s = 0; s < steps && !now.empty(); ++s) {
        std::vector<const State*> next;
        const auto reach = [&](State* to) {
            if (!to || !seen.insert(to).second) return;
            next.push_back(to);
            if (auto* w = dynamic_cast<Spatial3D*>(to)) found.push_back(w);
        };
        for (const State* at : now) {
            for (const Transition& t : g.transitions())
                if ((t.from == at->id() || t.from == Key{"*"}) && !t.to.empty()) reach(g.find(t.to));
            for (const Element& e : at->elements())
                if (e.kind == kinds::portal)
                    if (const auto it = worlds_.find(e.id); it != worlds_.end() && it->second.world)
                        reach(g.find(it->second.world->id()));
        }
        now = std::move(next);
    }
    return found;
}

void GLWorldView::warm_some() {
    if (ahead_.empty()) return;
    // (A few things a frame: half a millisecond of the processor at most.)
    const auto from = std::chrono::steady_clock::now();
    making_ahead_ = true;
    while (!ahead_.empty() && std::chrono::steady_clock::now() - from < std::chrono::microseconds(500)) {
        const auto [w, i] = ahead_.front();
        ahead_.pop_front();
        if (i >= w->elements().size()) continue;
        const Element& e = w->elements()[i];
        if (!e.alive || e.kind != kinds::mesh) continue;
        shape_of(*w, e);
        if (e.params.has(Key{"skin"})) bind_skin(*w, e);
        if (is_sprite(e)) bind_picture(*w, e.params.get_or<std::string>(Key{"picture"}, ""));
    }
    making_ahead_ = false;
}

void GLWorldView::warm(const std::vector<Spatial3D*>& worlds, int fb_w, int fb_h) {
    for (Spatial3D* w : worlds)
        if (w) warmed_.insert(w);
    making_ahead_ = true;
    struct Done {
        bool& on;
        ~Done() { on = false; }
    } done{making_ahead_};
    const LookFader fader = fader_;
    const Mix post = post_;
    // (And the eye as it was adjusted: what warming measures is of other worlds.)
    const double ev = exposure_.ev, ev_target = exposure_.target;
    const bool ev_known = exposure_.known, ev_settle = exposure_.settle;
    const float ev_weight = exposure_.weight;
    let_go_.held = true;
    warming_ = true;
    ready_skins();
    ready_worn(worlds);
    for (Spatial3D* w : worlds)
        if (w) {
            render(*w, fb_w, fb_h);
            gl::answer();
        }
    // And each world from just inside each doorway walked through it,
    // looking through - an eye of warming's own; the world is not touched:
    // the shadow maps of its lamps and of the lamps let in by its doorways,
    // the views beyond and their air, laid now, never the first time someone
    // walks up to the doorway (a tenth of a second's stop at a threshold).
    for (Spatial3D* w : worlds) {
        if (!w) continue;
        for (const Element& e : w->elements()) {
            if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.has(Key{"ball"}) ||
                e.params.num(Key{"walk"}, 1.0) <= 0.5 || !worlds_.count(e.id))
                continue;
            const Vec3d at = world_position(*w, e), n = heading(e.params.num(keys::yaw));
            for (double side : {1.0, -1.0}) {
                const Vec3d from{at.x + n.x * 1.6 * side, at.y + 0.55, at.z + n.z * 1.6 * side};
                if (!opens_from(e, from)) continue;
                Element eye = w->camera();
                for (const Key& k : {Key{"stand_w"}, Key{"stand_x"}, Key{"stand_y"}, Key{"stand_z"}}) eye.params.erase(k);
                eye.params.set(keys::x, from.x).set(keys::y, from.y).set(keys::z, from.z);
                eye.params.set(keys::yaw, std::atan2(at.z - from.z, at.x - from.x)).set(keys::pitch, 0.0);
                eye_override_ = &eye;
                render(*w, fb_w, fb_h);
                // And turned round, into the room, a little down: its
                // mirrors (a polished floor) and what they show.
                eye.params.set(keys::yaw, std::atan2(from.z - at.z, from.x - at.x)).set(keys::pitch, -0.3);
                render(*w, fb_w, fb_h);
                eye_override_ = nullptr;
                gl::answer();
                break;
            }
        }
    }
    // And every picture worn or shown sent to the card, seen or not from
    // those eyes - a hundred books' skins sent the frame a bookcase comes
    // into sight stop it.
    for (auto& [id, bound] : surfaces_)
        if (bound.surface) upload_skin(bound);
    // And every thing's shape made, seen or not from those eyes - a mesh
    // made the first time its thing comes into sight stops that frame.
    // And the ways its normal map is laid by, for a thing that wears one
    // (tens of milliseconds a model, worked out on every core at once).
    struct Ways {
        const gl::Mesh* mesh;
        const Spatial3D* world;
        const Element* e;
        std::vector<float> made;
    };
    std::vector<Ways> ways;
    for (Spatial3D* w : worlds)
        if (w)
            for (const Element& e : w->elements())
                if (e.alive && e.kind == kinds::mesh) {
                    const gl::Mesh& mesh = shape_of(*w, e);
                    // (Its picture too, sent to the card now: the first draw
                    // of a thing in a picture of its own stops its frame.)
                    if (e.params.has(Key{"skin"})) bind_skin(*w, e);
                    if (is_sprite(e)) bind_picture(*w, e.params.get_or<std::string>(Key{"picture"}, ""));
                    const std::string* nmap = e.params.num(Key{"uv"}, 0.0) > 0.5 ? e.params.text(Key{"skin_normal"}) : nullptr;
                    if (nmap && w->picture(Key{*nmap}) && !mesh.tangents_made() &&
                        std::none_of(ways.begin(), ways.end(), [&](const Ways& x) { return x.mesh == &mesh; }))
                        ways.push_back(Ways{&mesh, w, &e, {}});
                }
    {
        std::atomic<std::size_t> next{0};
        const auto hand = [&] {
            for (std::size_t k; (k = next.fetch_add(1)) < ways.size();) ways[k].made = tangents_of(*ways[k].world, *ways[k].e);
        };
        std::vector<std::thread> hands;
        const std::size_t n = std::min<std::size_t>(std::max(1u, std::thread::hardware_concurrency()), ways.size());
        for (std::size_t t = 1; t < n; ++t) hands.emplace_back(hand);
        hand();
        for (std::thread& t : hands) t.join();
    }
    for (Ways& x : ways) x.mesh->set_tangents(x.made);
    // And the fewer faces each model is drawn with from further off
    // (Quality::lod), for each model first seen as it is (one made again
    // since is drawn whole: its corners are not these): made by a hand of
    // their own, below the frame's, and kept on disk - not waited for.
    if (q_.lod) {
        // (One hand at a time: one at work already stopped, what it made taken.)
        if (fewer_work_) {
            fewer_work_->stop = true;
            if (fewer_work_->hand.joinable()) fewer_work_->hand.join();
            take_fewer();
            fewer_work_.reset();
        }
        auto work = std::make_unique<FewerWork>();
        static const Key model_key{"model"}, uv_key{"uv"};
        for (Spatial3D* w : worlds)
            if (w)
                for (const Element& e : w->elements()) {
                    if (!e.alive || e.kind != kinds::mesh || skinned(e) || e.params.num(uv_key, 0.0) > 0.5 || !e.params.is(Key{"shape"}, "model"))
                        continue;
                    const Key name{e.params.get_or<std::string>(model_key, "")};
                    const std::vector<float>* corners = w->model(name);
                    if (!corners || corners->size() < kFewerCorners * 8) continue;
                    const gl::Mesh& mesh = shape_of(*w, e);
                    const auto made = model_meshes_.find({w, name.str()});
                    if (made == model_meshes_.end() || made->second.shared != &mesh || fewer_.count(&mesh) ||
                        std::any_of(work->jobs.begin(), work->jobs.end(), [&](const FewerWork::Job& j) { return j.mesh == &mesh; }))
                        continue;
                    work->jobs.push_back(FewerWork::Job{&mesh, *corners});
                }
        if (!work->jobs.empty()) {
            FewerWork* w = work.get();
            w->hand = std::thread([w] {
                cache::background_priority();
                for (FewerWork::Job& j : w->jobs) {
                    if (w->stop) return;
                    std::vector<std::vector<float>> made;
                    std::size_t before = j.corners.size();
                    for (float tol : kFewerErrors) {
                        if (w->stop) return;
                        const Digest key = Hasher{}.text("fewer 2").bytes(j.corners.data(), j.corners.size() * sizeof(float)).number(tol).digest();
                        std::string bytes;
                        std::vector<float> fewer;
                        if (cache::load("fewer", key, bytes) && bytes.size() % (8 * sizeof(float)) == 0) {
                            fewer.resize(bytes.size() / sizeof(float));
                            std::memcpy(fewer.data(), bytes.data(), bytes.size());
                        } else {
                            fewer = shapes::fewer(j.corners, tol);
                            cache::store("fewer", key, std::string_view(reinterpret_cast<const char*>(fewer.data()), fewer.size() * sizeof(float)));
                        }
                        // (Only what is fewer by a fifth or more is worth a level.)
                        if (fewer.empty() || fewer.size() * 5 > before * 4) break;
                        before = fewer.size();
                        made.push_back(std::move(fewer));
                    }
                    std::lock_guard<std::mutex> hold(w->lock);
                    w->done.emplace_back(j.mesh, std::move(made));
                }
                w->finished = true;
            });
            fewer_work_ = std::move(work);
        }
    }
    // And the views of the worlds seen whole through `own_look` doorways
    // (a city out of a window), each drawn once from the world's own eye:
    // made now, not when the window first comes into sight.
    for (Spatial3D* w : worlds)
        if (w)
            for (const Element& e : w->elements()) {
                if (e.kind != kinds::portal || !e.alive || e.params.num(Key{"own_look"}, 0.0) <= 0.5) continue;
                auto it = worlds_.find(e.id);
                if (it == worlds_.end() || !it->second.world) continue;
                Element eye = it->second.world->camera();
                draw_own(*w, e, it->second, eye, fb_w, fb_h);
                gl::answer();
            }
    // And mirrors' pictures ready for the views that will show a mirror,
    // if any world drawn has one: made now, not as one comes into sight.
    if (!mirror_sets_.empty())
        while (mirror_spares_.size() < kMirrorSpares) {
            gl::RenderTarget t;
            t.create(target_w_, target_h_, gl::GL_RGBA16F, 0, true);
            // (Drawn into once: a card makes a picture's memory only when
            // it is first used.)
            t.bind();
            gl::glClear(gl::GL_COLOR_BUFFER_BIT | gl::GL_DEPTH_BUFFER_BIT);
            t.mipmap();
            mirror_spares_.push_back(std::move(t));
        }
    // And every screen's picture, through the screen's own view, whether or
    // not it was in sight from any of those eyes: a painting or a set first
    // turned to is not where its view is made (a world's first picture in a
    // view of its own costs tens of milliseconds - its shaders, its targets).
    for (auto& [id, f] : feeds_)
        if (f.world && f.view && declared_feed(id, *f.world)) {
            f.drawn = true;
            draw_feed(f);
        }
    gl::glFinish();
    fader_ = fader;
    post_ = post;
    exposure_.ev = ev, exposure_.target = ev_target, exposure_.known = ev_known, exposure_.settle = ev_settle;
    exposure_.weight = ev_weight;
    exposure_.asked[0] = exposure_.asked[1] = false;
    let_go_.held = false;
    warming_ = false;
}

void GLWorldView::render(const Spatial3D& world, int fb_w, int fb_h) {
    const PlacedRoom one{&world, Pose{}, {}};
    render(std::vector<PlacedRoom>{one}, fb_w, fb_h);
}

GLWorldView::Camera GLWorldView::placed_camera(const Camera& c, const Pose& at) {
    if (at.position.x == 0.0 && at.position.y == 0.0 && at.position.z == 0.0 && at.yaw == 0.0 && at.pitch == 0.0 && at.roll == 0.0) return c;
    const gl::Mat4 m = gl::Mat4::translate({static_cast<float>(at.position.x), static_cast<float>(at.position.y), static_cast<float>(at.position.z)}) *
                       gl::Mat4::rotate_y(static_cast<float>(at.yaw)) * gl::Mat4::rotate_z(static_cast<float>(at.pitch)) *
                       gl::Mat4::rotate_x(static_cast<float>(at.roll));
    const gl::Vec3 o = m.transform_point({0, 0, 0});
    Camera out = c;
    out.eye = m.transform_point(c.eye);
    out.forward = m.transform_point(c.forward) - o;
    out.up = m.transform_point(c.up) - o;
    return out;
}

std::vector<PlacedRoom> GLWorldView::seen(const Spatial3D& world) const {
    std::vector<PlacedRoom> out{PlacedRoom{&world, Pose{}, {}}};
    // (Glued: the space it is part of, each room where it stands, its glued
    // doorways openings.)
    const auto& glued = glued_round(world);
    if (!glued.empty()) out = glued;
    if (const StateGraph* g = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr))
        for (const PlacedRoom& p : nests(*g, world.id()))
            if (std::none_of(out.begin(), out.end(), [&](const PlacedRoom& r) { return r.room == p.room; })) out.push_back(p);
    return out;
}

const std::vector<PlacedRoom>& GLWorldView::glued_round(const Spatial3D& world) const {
    static const std::vector<PlacedRoom> none;
    const StateGraph* g = graph_ ? graph_ : (root_ ? root_->graph_ : nullptr);
    if (!g) return none;
    GluedRound& m = glued_round_[&world];
    const uint64_t frame = root_ ? root_->frame_count_ : frame_count_;
    if (m.frame == frame && m.revision == g->revision()) return m.rooms;
    m.frame = frame, m.revision = g->revision();
    m.rooms = sheafify(*g, world.id(), 6, kAllLights, q_.sheafify);
    if (m.rooms.size() <= 1) {
        m.rooms.clear();
        return m.rooms;
    }
    // (Placed in the frame of the first by name: put in `world`'s own.)
    const Pose to_world = inverse(m.rooms.front().pose);
    for (PlacedRoom& p : m.rooms) p.pose = compose_pose(to_world, p.pose);
    m.rooms.front().pose = Pose{};
    return m.rooms;
}

void GLWorldView::set_quality(const GLQuality& to) {
    const GLQuality was = q_;
    q_ = to;
    // Shadow maps of another size, or room for them: the kept ones let go,
    // made again as lamps ask.
    if (was.shadow_size != q_.shadow_size) {
        shadow_sets_.clear();
        spare_shadows_.clear();
    }
    if (was.shadow_budget_mb != q_.shadow_budget_mb || was.shadow_size != q_.shadow_size) shadow_room_ = 0;
    // Other samples: the targets drawn into made again at the size they are.
    if (was.msaa != q_.msaa) {
        parked_.clear();
        if (target_w_ > 0) make_targets(target_w_, target_h_);
    }
    // Frames gathered by another rule are not this one's history.
    if (was.taa != q_.taa || was.msaa != q_.msaa) taa_held_ = false;
    if (was.bloom_passes != q_.bloom_passes || was.bloom_strength != q_.bloom_strength ||
        was.bloom_threshold != q_.bloom_threshold || was.exposure != q_.exposure)
        standard_look(standard_, q_);
    // Packed: what things wear packed (as at start); unpacked: each its own
    // pixels again, sent as it is next drawn.
    if (was.pack != q_.pack) {
        if (q_.pack) pack_skins();
        else
            for (auto& [id, bound] : surfaces_) bound.packed.reset(), bound.packed_surface.reset(), bound.packed_normal.reset();
    }
    // The views drawn for this one draw as it does (a screen's shadow maps
    // no finer than 1024, as when it was made).
    for (auto& [id, f] : feeds_)
        if (f.view) {
            GLQuality q = q_;
            q.shadow_size = std::min(q_.shadow_size, 1024);
            f.view->set_quality(q);
        }
    for (auto& [w, own] : own_views_)
        if (own) own->set_quality(q_);
    for (auto* v : {&baker_, &lamp_seer_, &sun_seer_})
        if (*v) (*v)->set_quality(q_);
}

GLWorldView& GLWorldView::own_view_of(const Spatial3D* world) {
    std::unique_ptr<GLWorldView>& v = own_views_[world];
    if (!v) {
        v = std::make_unique<GLWorldView>(q_);
        v->set_fixed_step(fixed_step_);
        v->scratch_lent_ = true;
    }
    return *v;
}

GLWorldView::Rect GLWorldView::own_cut_of(const Rect& seen) {
    if (seen.empty()) return seen;
    const float mx = 0.08f + 0.15f * (seen.x1 - seen.x0), my = 0.08f + 0.15f * (seen.y1 - seen.y0);
    return Rect{std::max(seen.x0 - mx, -1.0f), std::max(seen.y0 - my, -1.0f), std::min(seen.x1 + mx, 1.0f), std::min(seen.y1 + my, 1.0f)};
}

bool GLWorldView::share_own(WorldPortal& wp, const Element& eye, int fb_w, int fb_h, const Rect& seen) {
    const Vec3d at = position_of(eye);
    const double yaw = eye.params.num(keys::yaw), pitch = eye.params.num(keys::pitch);
    const Rect need = own_cut_of(seen);
    for (auto& [id, other] : worlds_) {
        if (&other == &wp || !other.own_drawn || other.world != wp.world || other.own_pic != &other.own_out || !other.own_out.valid()) continue;
        if (other.own_out.width() != fb_w || other.own_out.height() != fb_h) continue;
        // (Only a picture that holds the part of the screen this one is seen in.)
        if (need.x0 < other.own_cut.x0 || need.y0 < other.own_cut.y0 || need.x1 > other.own_cut.x1 || need.y1 > other.own_cut.y1) continue;
        const Vec3d d = at - other.own_at;
        if (d.x * d.x + d.y * d.y + d.z * d.z > 1e-8 || std::fabs(std::remainder(yaw - other.own_yaw, 6.283185307179586)) > 1e-6 ||
            std::fabs(pitch - other.own_pitch) > 1e-6)
            continue;
        wp.own_pic = &other.own_out;
        return true;
    }
    return false;
}

void GLWorldView::draw_own(const Spatial3D& world, const Element& e, WorldPortal& wp, Element& eye, int fb_w, int fb_h, const Rect& seen) {
    wp.own = &own_view_of(wp.world);
    if (!wp.own_out.valid() || wp.own_out.width() != fb_w || wp.own_out.height() != fb_h)
        wp.own_out.create(fb_w, fb_h, gl::GL_SRGB8_ALPHA8, 0, false), wp.own_key = 0;
    // Only the part of the screen the window is seen in is drawn: a window
    // across the room costs what of the screen it covers, not all of it.
    const Rect cut = own_cut_of(seen);
    // (Held: nothing in it changed - its world's data, the eye, the size, the
    // part of the screen - since it was last drawn: the same picture would be
    // drawn again.)
    uint64_t key = 1469598103934665603ULL;
    const auto mix = [&key](uint64_t v) { key = (key ^ v) * 1099511628211ULL; };
    mix(wp.world->data_version());
    for (double v : {position_of(eye).x, position_of(eye).y, position_of(eye).z, eye.params.num(keys::yaw), eye.params.num(keys::pitch), eye.params.num(keys::roll),
                     eye.params.num(keys::fov, 60.0), double(fb_w), double(fb_h), double(cut.x0), double(cut.y0), double(cut.x1), double(cut.y1)})
        mix(static_cast<uint64_t>(std::llround(v * 1e5)));
    if (key == wp.own_key && wp.own_out.valid() && wp.own_key != 0) {
        wp.own_pic = &wp.own_out;
        wp.own_at = position_of(eye), wp.own_yaw = eye.params.num(keys::yaw), wp.own_pitch = eye.params.num(keys::pitch);
        return;
    }
    wp.own_key = key;
    wp.own->graph_ = graph_;
    wp.own->root_ = root_ ? root_ : this;
    wp.own->output_ = &wp.own_out;
    wp.own->eye_override_ = &eye;
    wp.own->film_of_viewer_ = true;
    wp.own->lent_scene_ = &scene_ms();
    // Cut as any view through a doorway is: nothing between the
    // carried eye and the far doorway, and that doorway's own view
    // left out - right at the threshold the eye stands in its frame.
    const Element* own_back = !wp.back.empty() ? wp.world->find(wp.back) : back_portal(*wp.world, world);
    wp.own->own_clips_ = {far_side(world, e, eye)};
    wp.own->own_skip_ = own_back ? own_back->id.key() : Key{};
    // (Its working pictures, this view's, lent while it is drawn:
    // the same size - both are the screen's.)
    const bool lend = target_w_ == fb_w && target_h_ == fb_h;
    if (lend) wp.own->trade_scratch(*this);
    else if (wp.own->scratch_lent_) wp.own->scratch_lent_ = false, wp.own->make_scratch(fb_w, fb_h);
    // (Moved as this view is: it is shown at the screen's pixels.)
    wp.own->jitter_x_ = jitter_x_, wp.own->jitter_y_ = jitter_y_;
    wp.own->own_seen_ = cut;
    const auto own_from = std::chrono::steady_clock::now();
    wp.own->render(*wp.world, fb_w, fb_h);
    times_.own_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - own_from).count();
    wp.own->own_seen_ = Rect{-1, -1, 1, 1};
    wp.own_cut = cut;
    wp.own_pic = &wp.own_out;
    wp.own_at = position_of(eye), wp.own_yaw = eye.params.num(keys::yaw), wp.own_pitch = eye.params.num(keys::pitch);
    if (lend) wp.own->trade_scratch(*this);
    wp.own->lent_scene_ = nullptr;
    wp.own->own_clips_.clear();
    wp.own->own_skip_ = Key{};
    wp.own->output_ = nullptr;
    wp.own->eye_override_ = nullptr;
}

bool GLWorldView::lod_pending() const {
    if (!fewer_work_) return false;
    std::lock_guard<std::mutex> hold(fewer_work_->lock);
    return !fewer_work_->finished || !fewer_work_->done.empty();
}

void GLWorldView::take_fewer() {
    if (!fewer_work_) return;
    std::vector<std::pair<const gl::Mesh*, std::vector<std::vector<float>>>> done;
    {
        std::lock_guard<std::mutex> hold(fewer_work_->lock);
        done.swap(fewer_work_->done);
    }
    for (auto& [mesh, made] : done) {
        auto& levels = fewer_[mesh];
        for (std::size_t k = 0; k < made.size(); ++k) {
            auto f = std::make_unique<Fewer>();
            f->mesh.create(made[k]);
            f->error = kFewerErrors[k];
            levels.push_back(std::move(f));
        }
    }
}

void GLWorldView::render(const std::vector<PlacedRoom>& requested, int fb_w, int fb_h) {
    // A mesh that is no skin has no joints: what its vertex stage reads for
    // them, where its corners do not say (none of a skin's: posed_by_joints).
    gl::glVertexAttrib4f(11, 0.0f, 0.0f, 0.0f, 0.0f);
    gl::glVertexAttrib4f(12, 0.0f, 0.0f, 0.0f, 0.0f);
    // (Models' fewer faces made since the last frame, given to the card.)
    if (!root_) take_fewer();
    // (And a few rooms' cells merged a frame: Quality::proxies.)
    if (!root_) proxy_budget_ = 2;
    // (And what pictures' finer levels the frame may send: Quality::stream_textures.)
    if (!root_) gl::Texture::stream_budget = std::size_t{4} << 20;

    // A room that says it is seen from one of its own (its camera's
    // `seen_from`: an element of it - a camera hung there, chosen by whatever
    // stages it) is drawn from there; what walks it is still its camera.
    if (!eye_override_ && !root_ && !requested.empty() && requested.front().room) {
        const Spatial3D& w = *requested.front().room;
        const std::string from = w.camera().params.get_or<std::string>("seen_from", "");
        if (const Element* e = from.empty() ? nullptr : w.find(Key{from})) {
            eye_override_ = e;
            render(requested, fb_w, fb_h);
            eye_override_ = nullptr;
            return;
        }
    }
    // Drawn at a part of the screen's size, and laid on it in squares: the
    // eye's own view only (a view drawn for another is that one's size).
    if (q_.pixel > 1 && !root_ && !output_ && !baking_ && surface_only_ == 0 && fb_w > 0 && fb_h > 0) {
        const int w = std::max(1, (fb_w + q_.pixel / 2) / q_.pixel), h = std::max(1, (fb_h + q_.pixel / 2) / q_.pixel);
        if (!pixel_out_.valid() || pixel_out_.width() != w || pixel_out_.height() != h) pixel_out_.create(w, h, gl::GL_RGBA8, 0, false);
        output_ = &pixel_out_;
        render(requested, w, h);
        output_ = nullptr;
        // Laid on the screen in squares (drawn, not copied: the window's
        // own picture may be of several samples, which no copy scales into).
        if (!pixel_prog_)
            pixel_prog_ = std::make_unique<gl::Program>(gl::post_vs(), R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uPicture;
void main() {
    ivec2 size = textureSize(uPicture, 0);
    FragColor = texelFetch(uPicture, clamp(ivec2(vUV * vec2(size)), ivec2(0), size - 1), 0);
}
)", "pixel");
        gl::glBindFramebuffer(gl::GL_FRAMEBUFFER, 0);
        gl::glViewport(0, 0, fb_w, fb_h);
        gl::glDisable(gl::GL_DEPTH_TEST);
        gl::glDisable(gl::GL_BLEND);
        pixel_prog_->use();
        pixel_out_.bind_color(0);
        pixel_prog_->set("uPicture", 0);
        screen_.draw();
        return;
    }
    let_go_.now();
    // How things are drawn, as the state that says so says now.
    if (graphics_ && graphics_->stamp() != graphics_stamp_) {
        graphics_stamp_ = graphics_->stamp();
        set_quality(graphics_->quality());
    }
    // What is seen of the world the eye is in: it, and the worlds round it
    // or in it (sg::nests) - as through any doorway onto it.
    auto rooms = requested;
    if (!rooms.empty() && rooms.front().room)
        for (const PlacedRoom& p : seen(*rooms.front().room))
            if (std::none_of(rooms.begin(), rooms.end(), [&](const PlacedRoom& r) { return r.room == p.room; })) rooms.push_back(p);
    const StateGraph* declared=graph_?graph_:(root_?root_->graph_:nullptr);
    // Glued: the rooms that agree round the eye's, in its frame - drawn where
    // they stand, their doorways openings (sg::sheafify).
    if ((!root_ || viewer_feed_) && declared && !rooms.empty() && rooms.front().room && rooms.front().pose.position.x == 0.0 && rooms.front().pose.position.y == 0.0 && rooms.front().pose.position.z == 0.0 &&
        rooms.front().pose.yaw == 0.0) {
        const auto glued = sheafify(*declared, rooms.front().room->id(), 6, kAllLights, q_.sheafify);
        rooms.front().pose = glued.front().pose;
        rooms.front().sheafed = glued.front().sheafed;
        rooms.front().doorways = glued.front().doorways;
        // (Each where the anchor's frame has it - seen() put them in the
        // eye's room's.)
        for (std::size_t i = 1; i < glued.size(); ++i) {
            const auto at = std::find_if(rooms.begin(), rooms.end(), [&](const PlacedRoom& r) { return r.room == glued[i].room && !r.image; });
            if (at == rooms.end()) rooms.push_back(glued[i]);
            else at->pose = glued[i].pose, at->doorways = glued[i].doorways, at->sheafed = glued[i].sheafed;
        }
    }
    // (What the graph's view plan says of the worlds round the eye's - not
    // of the rooms made one with it: those are what sheafify says, and
    // asked of the plan cost a frame its time for nothing.)
    const bool planned_rooms = std::any_of(rooms.begin() + (rooms.empty() ? 0 : 1), rooms.end(), [](const PlacedRoom& r) { return !r.sheafed; });
    if(rooms.size()>1 && planned_rooms){
        if(!declared)rooms.resize(1);
        else if(rooms.front().room){
            const auto plan=view_plan(*declared,*rooms.front().room,rooms);
            rooms.erase(std::remove_if(rooms.begin(),rooms.end(),[&](const PlacedRoom& placed){return !placed.sheafed && std::none_of(plan.rooms.begin(),plan.rooms.end(),[&](const RoomDraw& draw){return draw.room==placed.room;});}),rooms.end());
        }
    }
    if (fb_w <= 0 || fb_h <= 0 || rooms.empty() || !rooms.front().room) return;
    world_time_ = semantic_time(graph_ ? graph_ : (root_ ? root_->graph_ : nullptr), *rooms.front().room);
    aim_rays(*rooms.front().room);
    // (The card's clock stamps the eye's own frames only: a view drawn for
    // another is part of that one's.)
    if (!root_ && !baking_) card_begin(), card_stamp(kCardStart);
    // The light from all round of each room seen, relit before anything of
    // this frame is drawn (relighting draws).
    if (!root_ && !baking_) light_rooms(rooms);
    card_stamp(kCardRelit);
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
            feeds_[resource].viewer_film = panel->params.num(Key{"viewer_film"}, 0.0) > 0.5;
            feeds_[resource].painted = panel->params.num(Key{"painted"}, 0.0) > 0.5;
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
        // The picture drawn is only as big as the screen is seen: a
        // painting across the room costs a few hundred pixels, not the
        // window's. More at once as it comes near; less only after it has
        // been small a while, so it does not flicker at a boundary.
        std::vector<const Feed*> adapted;
        const auto detail = [&](Key id, Feed& f) {
            adapted.push_back(&f);
            // (A held picture is painted once, at its size: drawn again as it
            // is walked up to, it would be a new picture at every step.)
            const int want = f.live ? feed_detail(shown_px(id, f.h), f.h) : 1;
            if (want < f.div) f.div = want, f.slack = 0;
            else if (want > f.div && ++f.slack >= 20) f.div = want, f.slack = 0;
            else if (want == f.div) f.slack = 0;
            const int nw = std::max(1, f.w / f.div), nh = std::max(1, f.h / f.div);
            if (nw != f.rw || nh != f.rh) {
                f.rw = nw, f.rh = nh;
                // (Made already, with the feed.)
                for (int i = 0; i < 2; ++i)
                    if (!f.out()[i].valid()) f.out()[i].create(f.rw, f.rh, f.format(), 0, false);
            }
        };
        int held = 0;
        for (bool more = true; more;) {
            more = false;
            for (auto& [id, f] : feeds_) {
                if (!f.world || !f.view || !declared_feed(id, *f.world)) continue;
                if (std::any_of(due.begin(), due.end(), [&](const auto& d) { return d.second == &f; })) continue;
                // (Warming, every feed is drawn - and the maps its view lays
                // laid - never the first time its screen is seen.)
                bool here = warming_ && !f.drawn;
                for (const PlacedRoom& placed : rooms)
                    if (!here && placed.room && seen_in(*placed.room, id, true)) here = true;
                for (const Spatial3D* w : shown)
                    if (!here && seen_in(*w, id, false)) here = true;
                if (!here) continue;
                // A held picture is drawn again only when what it sees has
                // changed (feed_key): it shows the world as it is, and costs
                // nothing while that stands still. Not again within kHeldEvery
                // frames of the last, and one in a frame: a door swung, a lamp
                // flickering, is a picture a few frames behind - never a
                // frame that stops to draw one.
                if (!f.live && f.drawn) {
                    detail(id, f);
                    if (frame_count_ - f.drawn_at < kHeldEvery || held >= 1 || f.drawn_of == feed_key(f)) continue;
                    ++held;
                }
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
            if (std::find(adapted.begin(), adapted.end(), &f) == adapted.end()) detail(it->first, f);
            if (draw_feed(f)) ++feed_views;
        }
    }
    const double feeds_ms =
        timing_ ? (gl::glFinish(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - feeds_from).count()) : 0.0;
    card_stamp(kCardFeeds);
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
    keep_shadows_within_room();
    std::size_t root_views = 0;
    ++frame_count_;
    // This frame's part of a pixel (Halton's 2 and 3, eight in turn): spread
    // evenly over the pixel within every eight frames.
    if (taa_on()) {
        const auto halton = [](uint64_t i, uint64_t b) {
            float f = 1.0f, r = 0.0f;
            for (; i > 0; i /= b) f /= static_cast<float>(b), r += f * static_cast<float>(i % b);
            return r;
        };
        jitter_x_ = halton(frame_count_ % 8 + 1, 2) - 0.5f;
        jitter_y_ = halton(frame_count_ % 8 + 1, 3) - 0.5f;
    } else if (!root_) {
        jitter_x_ = jitter_y_ = 0.0f;
    }
    shadow_budget_ = kNestedShadowMaps;
    eye_shadow_budget_ = kEyeShadowMaps;
    times_ = FrameTimes{};
    // (And the worlds the game may go to next, a few things of them.)
    if (!root_) warm_some();
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
    mirror_plans_.clear();
    // (Drawn in its own look this frame, or not: said again as it is planned.)
    for (auto& [id, wp] : worlds_) wp.own_drawn = false, wp.own_pic = nullptr;
    // The eye's own mirrors, and what is seen through their doorways.
    plan_mirrors(world, eye_of(world), aspect, 1, std::string(), Rect{-1, -1, 1, 1}, 0);
    // The doorways of each room seen from the eye: its own, and those of the
    // rooms glued into one space with it (a window of the room past a glued
    // doorway shows its view too) - each from the eye as that room has it.
    const auto glued_of = [](const std::vector<Key>& d, const Element& e) { return std::find(d.begin(), d.end(), e.id.key()) != d.end(); };
    struct Host {
        const Spatial3D* room;
        Element eye;
        Camera cam;
        const std::vector<Key>* openings;
        // Where on the screen the eye sees into it: its doorways made one,
        // in sight and open (the eye's own room: all of the screen).
        Rect through{-1, -1, 1, 1};
    };
    std::vector<Host> hosts{Host{&world, eye_of(world), eye_cam, &rooms.front().doorways}};
    if (rooms.front().sheafed) {
        const Camera drawn = placed_camera(eye_cam, rooms.front().pose);
        for (std::size_t i = 1; i < rooms.size(); ++i) {
            const PlacedRoom& r = rooms[i];
            if (!r.room || r.image || !r.sheafed || r.room == &world) continue;
            // (From the eye's room into this one's frame: rooms made one stand upright.)
            const Pose to = compose_pose(inverse(r.pose), rooms.front().pose);
            Host h{r.room, eye_of(world), placed_camera(drawn, inverse(r.pose)), &r.doorways};
            set_position(h.eye, place_in(to, position_of(h.eye)));
            h.eye.params.set(keys::yaw, h.eye.params.num(keys::yaw) + to.yaw);
            // Only a room seen into: a doorway of it in view, and not shut.
            Rect seen{1, 1, -1, -1};
            for (Key d : r.doorways)
                if (const Element* pe = r.room->find(d); pe && in_view(*r.room, *pe, h.cam, aspect) && !shut_to(*pe, position_of(h.eye))) {
                    const Rect sr = screen_rect(*r.room, *pe, h.cam, aspect);
                    seen = Rect{std::min(seen.x0, sr.x0), std::min(seen.y0, sr.y0), std::max(seen.x1, sr.x1), std::max(seen.y1, sr.y1)};
                }
            h.through = seen.cut(Rect{-1, -1, 1, 1});
            if (!h.through.empty()) hosts.push_back(std::move(h));
        }
    }
    const Element* const own_override = eye_override_;
    for (const Host& host : hosts) {
    const Spatial3D& world = *host.room;
    const Camera& eye_cam = host.cam;
    if (host.room != hosts.front().room) eye_override_ = &host.eye;
    const auto glued = [&](const Element& e) { return glued_of(*host.openings, e); };
    for (const auto& e : world.elements()) {
        if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.has(Key{"ball"})) continue;
        if (glued(e)) continue;  // (an opening: what is beyond it is drawn where it stands)
        if (e.params.num(Key{"own_look"}, 0.0) > 0.5) continue;
        auto it = worlds_.find(e.id);
        if (it == worlds_.end() || !it->second.world || !declared_world(world, e, *it->second.world)) continue;
        if (!in_view(world, e, eye_cam) || !opens_from(e, position_of(eye_of(world)))) continue;
        // (A door shut in it: nothing beyond is seen, nor what is seen from there.)
        if (shut_to(e, position_of(eye_of(world)))) continue;
        Element eye = it->second.world->camera();
        if (it->second.carry) it->second.carry(eye_of(world), eye);
        const Element* back = !it->second.back.empty() ? it->second.world->find(it->second.back) : back_portal(*it->second.world, world);
        // (Only where it is seen: in a room seen into, through the doorway it is seen into by.)
        const Rect shown = screen_rect(world, e, eye_cam, aspect).cut(host.through);
        if (shown.empty()) continue;
        view_through(*it->second.world, eye, aspect, 1, "/" + e.id.str(), shown, 0, back ? back->id.key() : Key{});
    }
    eye_override_ = own_override;
    }
    // The doorways the eye cannot see, asked of what stands before them
    // (Quality::skip_unseen): what their views would draw is left out by the
    // card. Never one the eye's mirrors may show - each mirror's eye, the
    // eye as the mirror shows it, seeing it (they may see one the eye
    // cannot). (A mirror in a view is seen only if its doorway is.)
    doorway_asked_.clear();
    if (q_.skip_unseen && !root_ && !baking_ && surface_only_ == 0) {
        std::vector<Asked> asked;
        for (const Host& h : hosts) asked.push_back(Asked{h.room, h.cam, h.openings, h.room != hosts.front().room || rooms.front().sheafed, {}});
        if (const auto eye_mirrors = mirror_plans_.find(std::string()); eye_mirrors != mirror_plans_.end())
            for (const MirrorPlane& p : eye_mirrors->second)
                asked.front().mirrored.push_back(camera_of(mirrored_eye(eye_of(*hosts.front().room), hosts.front().cam, p)));
        ask_doorways(asked, aspect);
    }
    bool views_drawn = false;
    for (const Host& host : hosts) {
    const Spatial3D& world = *host.room;
    const Camera& eye_cam = host.cam;
    if (host.room != hosts.front().room) eye_override_ = &host.eye;
    const auto glued = [&](const Element& e) { return glued_of(*host.openings, e); };
    for (const auto& e : world.elements()) {
        if (e.kind != kinds::portal || !e.alive || (e.params.has(Key{"ball"}) && !ball_window(e))) continue;
        if (glued(e)) continue;
        auto it = worlds_.find(e.id);
        if (it == worlds_.end() || !it->second.world || !declared_world(world, e, *it->second.world)) continue;
        WorldPortal& wp = it->second;
        wp.shared = nullptr;
        if (!in_view(world, e, eye_cam) || !opens_from(e, position_of(eye_of(world)))) continue;
        // (In a room seen into: only where it is seen, through the doorway it is seen into by.)
        const Rect shown = screen_rect(world, e, eye_cam, aspect).cut(host.through);
        if (!is_screen(e) && !e.params.has(Key{"ball"}) && shown.empty()) continue;
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
            // (The next doorway onto the same world from the same eye shows
            // the same picture: drawn once.)
            if (!share_own(wp, eye, fb_w, fb_h, shown)) draw_own(world, e, wp, eye, fb_w, fb_h, shown);
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
        // Its mirrors, from the eye carried there, where the doorway shows them.
        if (!screen && !e.params.has(Key{"ball"})) draw_mirrors(seen(*wp.world), guest_cam, aspect, shown);
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
            const Rect r = shown;
            const float mx = 0.04f + 0.1f * (r.x1 - r.x0), my = 0.04f + 0.1f * (r.y1 - r.y0);
            cut_ = Rect{std::max(r.x0 - mx, -1.0f), std::max(r.y0 - my, -1.0f), std::min(r.x1 + mx, 1.0f), std::min(r.y1 + my, 1.0f)};
        }
        // Only what is past the doorway is drawn: the near plane just short
        // of it, and what is outside the pyramid through it, or this side of
        // its plane, culled before it is sent (through).
        const Through way = !screen && !e.params.has(Key{"ball"}) && back && !wp.back.empty() ? through(e, *wp.world, *back, guest_cam) : Through{};
        cull_ = way.sides;
        for (const HalfSpace& h : clips) cull_.push_back(spatial::HalfSpace{{h.normal.x, h.normal.y, h.normal.z}, h.offset});
        // (Left out by the card if its doorway is not seen: ask_doorways.)
        const gl::GLuint gate = gate_of(path);
        gate_now_ = gate;
        draw_world(seen(*wp.world), guest_cam, aspect, scene_ms(),
                   /*depth=*/1, way.znear, back ? back->id.key() : Key{}, clips);
        gate_now_ = 0;
        cull_.clear();
        wp.fx = vp_w_ / static_cast<float>(scene_ms().width()), wp.fy = vp_h_ / static_cast<float>(scene_ms().height());
        sub_ = Rect{-1, -1, 1, 1};
        host_air_.on = false;
        wp.drawn = frame_count_;
        path_.clear();
        const float view_near = drawn_near_, view_far = drawn_far_;
        const bool cut = scissor_to(cut_, scene_ms().width(), scene_ms().height());
        if (gate) gl::glBeginConditionalRender(gate, gl::GL_QUERY_WAIT);
        scene_ms().blit_to(view.target);
        scene_ms().blit_depth_to(view.target);
        if (gate) gl::glEndConditionalRender();
        if (cut) gl::glDisable(gl::GL_SCISSOR_TEST);
        cut_ = Rect{-1, -1, 1, 1};
        wp.shown = &view.target;
        wp.shown_near = view_near, wp.shown_far = view_far;
        ++times_.portal_views;
    }
    eye_override_ = own_override;
    }
    card_stamp(kCardViews);
    times_.portals = since(t0);
    t0 = mark();

    // --- the room the viewer is actually standing in ------------------------
    path_.clear();
    // (Seen in the frame the rooms are placed in: the eye's room's own, or
    // the anchor's of a space glued into one.)
    const Camera drawn_cam = placed_camera(eye_cam, rooms.front().pose);
    draw_mirrors(rooms, drawn_cam, aspect);
    card_stamp(kCardMirrors);
    // A view seen only in part (a window in its own look: own_seen_) draws
    // only that part of its scene; the rest is cleared, so what spreads in
    // from beyond the part (its glow) spreads from nothing.
    const bool in_part = own_seen_.x0 > -1.0f || own_seen_.y0 > -1.0f || own_seen_.x1 < 1.0f || own_seen_.y1 < 1.0f;
    if (in_part) {
        scene_ms().bind();
        gl::glDisable(gl::GL_SCISSOR_TEST);
        gl::glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl::glClear(gl::GL_COLOR_BUFFER_BIT);
        cut_ = own_seen_;
    }
    draw_world(rooms, drawn_cam, aspect, scene_ms(), /*depth=*/0, kNear, own_skip_, own_clips_);
    if (in_part) cut_ = Rect{-1, -1, 1, 1};
    card_stamp(kCardScene);
    times_.scene_cpu = since(t0);
    if (timing_) gl::glFinish();
    times_.scene = since(t0);
    t0 = mark();

    // (Seen only in part: only that part made out of its samples.)
    const bool resolve_part = scissor_to(own_seen_, scene_ms().width(), scene_ms().height());
    scene_ms().blit_to(resolve_);
    if (resolve_part) gl::glDisable(gl::GL_SCISSOR_TEST);
    }
    scene_src_ = &resolve_;
    const double ao = setting(post_, passes::composite, "ao", 0.0);
    view_ = ViewParams{eye_cam.fov, aspect, kNear, static_cast<float>(world.params().num(Key{"far"}, 120.0))};
    if (ao > 0.0) {
        run_ao(static_cast<float>(ao),
               static_cast<float>(setting(post_, passes::composite, "ao.radius", 0.45)));
        scene_src_ = &lit_;
    }
    // The glow thick air spreads reads how far each pixel is (fog_bloom_glsl);
    // so does a look's depth of field (`uDof`).
    fog_depth_ = setting(post_, passes::composite, "uFogBloom", 0.0) > 0.0 || setting(post_, passes::composite, "uDof", 0.0) > 0.0 ||
                 setting(post_, passes::composite, "uHalation", 0.0) > 0.0;
    const double mosh = eye_view() ? setting(post_, passes::composite, "mosh", 0.0) : 0.0;
    if ((fog_depth_ || taa_on() || mosh > 0.0) && ao <= 0.0) {
        const bool depth_part = scissor_to(own_seen_, scene_ms().width(), scene_ms().height());
        scene_ms().blit_depth_to(depth_);
        if (depth_part) gl::glDisable(gl::GL_SCISSOR_TEST);
    }
    if (taa_on()) run_taa();
    if (mosh > 0.0) run_mosh(static_cast<float>(std::min(mosh, 1.0)));
    else mosh_held_ = false;
    meter_exposure();
    run_bloom();
    composite(fb_w, fb_h);
    card_stamp(kCardPost);
    card_now_ = nullptr;
    if (timing_) gl::glFinish();
    times_.post = since(t0);
    highlight_ = Key{};
}

bool GLWorldView::has_surface(const Element& e) const {
    auto it = bound_surfaces().find(e.id);
    if (it != bound_surfaces().end() && it->second.surface != nullptr && declared_surface(e, *it->second.surface)) return true;
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
            if (const auto s = bound_surfaces().find(e.id); s != bound_surfaces().end() && s->second.surface) mix(s->second.surface->revision());
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
    f.drawn_at = frame_count_;
    f.view->graph_ = graph_;  // the looks it is shown in are in the same graph
    f.view->root_ = this;
    f.view->output_ = &f.out()[1 - f.front];
    f.view->eye_override_ = f.eye;
    f.view->film_of_viewer_ = f.viewer_film;
    f.view->viewer_feed_ = f.viewer_film;
    f.view->render(*f.world, f.rw, f.rh);
    f.view->output_ = nullptr;
    f.view->eye_override_ = nullptr;
    f.view->film_of_viewer_ = false;
    f.view->viewer_feed_ = false;
    f.out()[1 - f.front].mipmap();
    f.front = 1 - f.front;
    return true;
}

uint64_t GLWorldView::feed_key(const Feed& f) const {
    uint64_t h = 1469598103934665603ULL;
    const auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
    // Painted (held, `painted`): from where it was painted, as big as it is -
    // nothing the world does since paints it again.
    if (!f.live && f.painted) {
        if (f.eye) mix(f.eye->params.stamp());
        mix(static_cast<uint64_t>(f.rw) << 32 | static_cast<uint64_t>(f.rh));
        return h | 1;
    }
    mix(stamp_of(look_of(*f.world)));
    if (f.eye) mix(f.eye->params.stamp());
    mix(static_cast<uint64_t>(f.rw) << 32 | static_cast<uint64_t>(f.rh));
    if (!f.live) {
        // Held: what its picture is made of, not every stamp of the world -
        // a walker's body (`unseen`) or the eye of the one looking is not in
        // it, and time passing alone does not make it a new picture.
        std::vector<const Spatial3D*> walked;
        const Camera seen_by = camera_of(f.eye ? *f.eye : f.world->camera());
        mix(seen_stamp(*f.world, f.eye == nullptr, walked, &seen_by, static_cast<float>(f.w) / static_cast<float>(std::max(1, f.h))));
        return h | 1;
    }
    mix(stamp_of(*f.world));
    const double t = semantic_time(graph_, *f.world);
    uint64_t b = 0;
    std::memcpy(&b, &t, sizeof b);
    mix(b);
    for (const Element& e : f.world->elements())
        if (e.kind == kinds::portal && e.alive)
            if (auto it = worlds_.find(e.id); it != worlds_.end() && it->second.world && it->second.world != f.world)
                mix(stamp_of(*it->second.world));
    return h | 1;  // (never 0: 0 is "never drawn")
}

uint64_t GLWorldView::seen_stamp(const Spatial3D& room, bool camera, std::vector<const Spatial3D*>& walked, const Camera* seen_by, float aspect) const {
    // What a picture of this room is made of: its drawn things where they
    // stand (an `unseen` one is in no picture), and what the doorways and
    // screens in the picture (seen by `seen_by`) show beyond - one doorway
    // on, each room once.
    static const Key unseen_key{"unseen"};
    uint64_t k = 1469598103934665603ULL;
    const auto mix = [&k](uint64_t v) { k = (k ^ v) * 1099511628211ULL; };
    if (std::find(walked.begin(), walked.end(), &room) != walked.end()) return k;
    walked.push_back(&room);
    // (Not the room's own params: a walker's steps are kept there, and it is not
    // what a picture is made of - its things, its looks and its lamps are.)
    static const Key base_key{"base"};
    for (const Element& e : room.elements()) {
        const bool drawn = e.kind == kinds::mesh || e.kind == kinds::wall || e.kind == kinds::light || e.kind == kinds::portal ||
                           e.kind == terrain_kind() || (camera && e.kind == kinds::camera);
        if (!drawn || e.params.num(unseen_key, 0.0) > 0.5) continue;
        // (A doorway or screen with nothing in it and no frame is drawn as nothing: where a body stands.)
        if (e.kind == kinds::portal && !framed(e) && !worlds_.count(e.id) && !bound_surfaces().count(e.id) && !shared_feeds().count(signal_of(e))) continue;
        // Where it stands in the room (following what it hangs from: a door's
        // leaf as its hinge turns), how big - and a lamp, whether it is lit
        // (a flickering one, by its steady light): what a still picture is
        // of. Not a lamp's flicker, nor a glow's, nor a clock's hand going
        // round: it was painted in one moment of those.
        mix(e.alive ? 1u : 0u);
        const Pose at = world_pose(room, e);
        for (double v : {at.position.x, at.position.y, at.position.z, at.yaw, at.pitch, at.roll, e.params.num(keys::sx, 0.0), e.params.num(keys::sy, 0.0),
                         e.params.num(keys::sz, 0.0)})
            mix(static_cast<uint64_t>(std::llround(v * 1000.0)));
        if (e.kind == kinds::light) mix(e.params.num(base_key, e.params.num(keys::intensity, 1.0)) > 0.05 ? 1u : 0u);
        if (e.kind != kinds::portal || !e.alive || !seen_by || !in_view(room, e, *seen_by, aspect)) continue;
        if (auto it = worlds_.find(e.id); it != worlds_.end() && it->second.world && it->second.world != &room)
            mix(seen_stamp(*it->second.world, false, walked, nullptr, aspect));
    }
    return k;
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
