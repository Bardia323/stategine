// The GL view's mirrors: a plane things in a view's room say they reflect
// in, and the room seen in it - drawn again from the eye mirrored in that
// plane, only what stands above it, only where the plane is on the screen,
// at a part of the screen's pixels, with the room's own shadow maps and light.
// Every view has its own: the eye's room, and each room seen through a
// doorway, however deep, seen from the eye carried there.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace sg::render {

namespace {
gl::Vec3 at(const gl::Mat4& m, gl::Vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12], m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]};
}
gl::Vec3 mirrored(gl::Vec3 v, gl::Vec3 n) { return v - n * (2.0f * gl::dot(v, n)); }
}  // namespace

GLWorldView::Rect GLWorldView::seen_rect(const std::vector<gl::Vec3>& corners, const Camera& cam, float aspect) const {
    // The polygon cut where it passes behind the eye's near plane, and what
    // is left of it taken onto the screen.
    const gl::Vec3 f = gl::normalize(cam.forward), right = gl::normalize(gl::cross(f, cam.up)), top = gl::cross(right, f);
    const float ty = std::tan(cam.fov * 0.5f), tx = ty * aspect;
    std::vector<gl::Vec3> in;
    for (const gl::Vec3& c : corners) in.push_back(c - cam.eye);
    std::vector<gl::Vec3> kept;
    for (std::size_t i = 0; i < in.size(); ++i) {
        const gl::Vec3 a = in[i], b = in[(i + 1) % in.size()];
        const float za = gl::dot(a, f) - kNear, zb = gl::dot(b, f) - kNear;
        if (za >= 0.0f) kept.push_back(a);
        if ((za >= 0.0f) != (zb >= 0.0f)) kept.push_back(a + (b - a) * (za / (za - zb)));
    }
    Rect r{1e9f, 1e9f, -1e9f, -1e9f};
    for (const gl::Vec3& v : kept) {
        const float z = std::max(gl::dot(v, f), kNear);
        const float x = gl::dot(v, right) / (z * tx), y = gl::dot(v, top) / (z * ty);
        r.x0 = std::min(r.x0, x), r.y0 = std::min(r.y0, y), r.x1 = std::max(r.x1, x), r.y1 = std::max(r.y1, y);
    }
    return Rect{std::max(r.x0, -1.0f), std::max(r.y0, -1.0f), std::min(r.x1, 1.0f), std::min(r.y1, 1.0f)};
}

const GLWorldView::MirrorSet* GLWorldView::mirrors_now() const {
    if (mirroring_) return nullptr;
    const auto it = mirror_sets_.find(path_);
    return it != mirror_sets_.end() && it->second.used == frame_count_ ? &it->second : nullptr;
}

auto GLWorldView::mirror_planes(const Spatial3D& room, const Camera& eye, float aspect, Rect within) const -> std::vector<MirrorPlane> {
    // The planes said to reflect, each with where it is on the screen: a
    // thing's top face (`reflects`), the room's floor (`floor_reflects`).
    using Plane = MirrorPlane;
    std::vector<Plane> planes;
    const auto add = [&](const std::vector<gl::Vec3>& corners, gl::Vec3 normal, float strength, Key glass) {
        const float offset = -gl::dot(normal, corners.front());
        // (Seen from above it only: from below, a plane shows nothing.)
        if (gl::dot(normal, eye.eye) + offset <= 0.01f) return;
        const Rect r = seen_rect(corners, eye, aspect).cut(within);
        if (r.empty()) return;
        // (A window's glass is a mirror of its own; things on one plane share one.)
        if (!glass.empty()) {
            planes.push_back(Plane{normal, offset, strength, r, glass});
            return;
        }
        for (Plane& p : planes)
            if (p.glass.empty() && gl::dot(p.normal, normal) > 0.999f && std::fabs(p.offset - offset) < 0.01f) {
                p.seen = Rect{std::min(p.seen.x0, r.x0), std::min(p.seen.y0, r.y0), std::max(p.seen.x1, r.x1), std::max(p.seen.y1, r.y1)};
                p.strength = std::max(p.strength, strength);
                return;
            }
        planes.push_back(Plane{normal, offset, strength, r, Key{}});
    };
    if (const float s = static_cast<float>(room.params().num(Key{"floor_reflects"}, 0.0)); s > 0.0f && room.params().has(Key{"room_w"})) {
        const float w = static_cast<float>(room.params().num(Key{"room_w"})), d = static_cast<float>(room.params().num(Key{"room_d"}));
        add({{0, 0, 0}, {w, 0, 0}, {w, 0, d}, {0, 0, d}}, {0, 1, 0}, std::min(s, 1.0f), Key{});
    }
    for (const Element& e : room.elements()) {
        if (!e.alive) continue;
        const float s = static_cast<float>(e.params.num(Key{"reflects"}, 0.0));
        if (s <= 0.0f) continue;
        if (e.kind == kinds::portal) {
            // A window's glass: its opening, facing the eye.
            const Pose pose = pose_of(room, e);
            const gl::Vec3 c = to_vec3(pose.position), side = to_vec3(across_of(pose)), up = to_vec3(up_of(pose));
            const float hw = static_cast<float>(e.params.num(keys::w, 3.0)) * 0.5f, hh = static_cast<float>(e.params.num(keys::h, 2.0)) * 0.5f;
            gl::Vec3 n = gl::normalize(gl::cross(side, up));
            if (gl::dot(n, eye.eye - c) < 0.0f) n = n * -1.0f;
            add({c - side * hw - up * hh, c + side * hw - up * hh, c + side * hw + up * hh, c - side * hw + up * hh}, n, std::min(s, 1.0f), e.id);
            continue;
        }
        // A thing reflects on the face across its thinnest side that faces
        // the eye: a slab lying flat on its top, one standing on its front.
        const gl::Mat4& m = box_matrix(room, e).m;
        const gl::Vec3 c = at(m, {0, 0, 0});
        const gl::Vec3 axes[3] = {at(m, {1, 0, 0}) - c, at(m, {0, 1, 0}) - c, at(m, {0, 0, 1}) - c};
        int thin = 0;
        for (int a = 1; a < 3; ++a)
            if (gl::dot(axes[a], axes[a]) < gl::dot(axes[thin], axes[thin])) thin = a;
        gl::Vec3 n = gl::normalize(axes[thin]);
        float half = 0.5f;
        if (gl::dot(n, eye.eye - c) < 0.0f) n = n * -1.0f, half = -0.5f;
        const gl::Vec3 face = c + axes[thin] * half, u = axes[(thin + 1) % 3] * 0.5f, v = axes[(thin + 2) % 3] * 0.5f;
        add({face - u - v, face + u - v, face + u + v, face - u + v}, n, std::min(s, 1.0f), Key{});
    }
    // The planes that cover the most of the screen, as many as are drawn.
    std::sort(planes.begin(), planes.end(), [](const Plane& a, const Plane& b) {
        return (a.seen.x1 - a.seen.x0) * (a.seen.y1 - a.seen.y0) > (b.seen.x1 - b.seen.x0) * (b.seen.y1 - b.seen.y0);
    });
    if (planes.size() > static_cast<std::size_t>(kMirrors)) planes.resize(kMirrors);
    return planes;
}

Element GLWorldView::mirrored_eye(const Element& eye, const Camera& by, const MirrorPlane& p) {
    // Where it is, which way it looks and which way is up, each mirrored -
    // a turn still (the picture is turned left for right after: draw_mirrors)
    // - said as any camera says them (yaw, pitch, roll about its look).
    const gl::Vec3 at = by.eye - p.normal * (2.0f * (gl::dot(p.normal, by.eye) + p.offset));
    const gl::Vec3 f = gl::normalize(mirrored(by.forward, p.normal)), u = mirrored(by.up, p.normal);
    Element m = eye;
    m.params.clear();
    set_position(m, {at.x, at.y, at.z});
    const gl::Vec3 side = gl::normalize(gl::cross(f, gl::Vec3{0, 1, 0})), up0 = gl::cross(side, f);
    m.params.set(keys::yaw, static_cast<double>(std::atan2(f.z, f.x)));
    m.params.set(keys::pitch, static_cast<double>(std::asin(std::clamp(f.y, -1.0f, 1.0f))));
    m.params.set(keys::roll, static_cast<double>(std::atan2(gl::dot(u, side), gl::dot(u, up0))));
    m.params.set(keys::fov, eye.params.num(keys::fov, 70.0));
    return m;
}

void GLWorldView::plan_mirrors(const Spatial3D& world, const Element& eye, float aspect, int depth, const std::string& path, Rect seen, int from) {
    if (!q_.reflections) return;
    const Camera by = camera_of(eye);
    if (by.ortho > 0.0f) return;
    std::vector<MirrorPlane>& planes = mirror_plans_[path];
    planes = mirror_planes(world, by, aspect, seen);
    // Each mirror's view, seen from the eye mirrored in it, where the mirror
    // is drawn (turned left for right, as its picture is): its doorways
    // views as any eye's, the one the eye came in by among them.
    for (std::size_t i = 0; i < planes.size(); ++i) {
        const Rect& r = planes[i].seen;
        view_through(world, mirrored_eye(eye, by, planes[i]), aspect, depth, path + "~m" + std::to_string(i), Rect{-r.x1, r.y0, -r.x0, r.y1}, from,
                     Key{}, &path, true);
    }
}

void GLWorldView::draw_mirrors(const std::vector<PlacedRoom>& rooms, const Camera& eye, float aspect, Rect within) {
    const auto from = std::chrono::steady_clock::now();
    struct Spent {
        FrameTimes& t;
        std::chrono::steady_clock::time_point at;
        ~Spent() { t.mirrors_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - at).count(); }
    } spent{times_, from};
    // (Once a frame, from the eye's own view: the views gone from sight let
    // their mirrors go a second or so after.)
    if (path_.empty())
        for (auto it = mirror_sets_.begin(); it != mirror_sets_.end();) {
            if (it->second.used + 90 >= frame_count_) {
                ++it;
                continue;
            }
            for (Mirror& m : it->second.at)
                if (m.target.valid()) mirror_spares_.push_back(std::move(m.target));
            it = mirror_sets_.erase(it);
        }
    MirrorSet& set = mirror_sets_[path_];
    set.used = frame_count_;
    set.count = 0;
    if (!q_.reflections || rooms.empty() || !rooms.front().room || eye.ortho > 0.0f) return;
    const Spatial3D& room = *rooms.front().room;
    // As planned with the views (whose doorways the mirrors' views go on
    // through), or - a view drawn with none planned - found now.
    const auto planned = mirror_plans_.find(path_);
    const Pose& at0 = rooms.front().pose;
    const bool placed0 = at0.position.x != 0.0 || at0.position.y != 0.0 || at0.position.z != 0.0 || at0.yaw != 0.0;
    std::vector<MirrorPlane> planes = placed0 ? std::vector<MirrorPlane>{} : planned != mirror_plans_.end() ? planned->second : mirror_planes(room, eye, aspect, within);
    // And those of the rooms glued to it into one space (sg::glue_space),
    // found in each room's own frame and carried into this one's.
    for (std::size_t r = placed0 ? 0 : 1; r < rooms.size() && planes.size() < static_cast<std::size_t>(kMirrors); ++r) {
        const PlacedRoom& placed = rooms[r];
        if (!placed.room || placed.image || (r > 0 && placed.doorways.empty())) continue;
        const Pose& at = placed.pose;
        const auto in_room = [&](const gl::Vec3& v, bool point) {
            const Vec3d d{v.x, v.y, v.z};
            return to_vec3(point ? local_of(at, d) : rotate_xz(d, -at.yaw));
        };
        Camera local = eye;
        local.eye = in_room(eye.eye, true), local.forward = in_room(eye.forward, false), local.up = in_room(eye.up, false);
        for (MirrorPlane p : mirror_planes(*placed.room, local, aspect, within)) {
            const gl::Vec3 on = p.normal * -p.offset;  // a point of it, in its room's frame
            const Vec3d n = rotate_xz({p.normal.x, p.normal.y, p.normal.z}, at.yaw);
            const Vec3d w = at.position + rotate_xz({on.x, on.y, on.z}, at.yaw);
            p.normal = to_vec3(n);
            p.offset = static_cast<float>(-(n.x * w.x + n.y * w.y + n.z * w.z));
            planes.push_back(p);
            if (planes.size() >= static_cast<std::size_t>(kMirrors)) break;
        }
    }
    const float scale = std::clamp(q_.reflection_scale, 0.1f, 1.0f);
    const bool eyes_own = path_.empty();
    if (eyes_own)
        mirror_eye_vp_ = projection_of(eye, aspect, kNear, static_cast<float>(room.params().num(Key{"far"}, 120.0))) *
                         gl::Mat4::look_at(eye.eye, eye.eye + eye.forward, eye.up);
    const int full_w = target_w_, full_h = target_h_;
    for (const MirrorPlane& p : planes) {
        if (set.count >= kMirrors) break;
        Mirror& mr = set.at[set.count];
        // The eye mirrored in the plane; seen through it the picture is
        // turned left for right, so its part of the screen is too.
        Camera cam = eye;
        cam.eye = eye.eye - p.normal * (2.0f * (gl::dot(p.normal, eye.eye) + p.offset));
        cam.forward = mirrored(eye.forward, p.normal);
        cam.up = mirrored(eye.up, p.normal);
        const Rect seen = p.seen;
        const Rect drawn{-seen.x1, seen.y0, -seen.x0, seen.y1};
        // Only what stands above it (a hair above: the plane itself, seen
        // from under it, would be all the picture).
        const std::vector<HalfSpace> above{HalfSpace{Vec3d{p.normal.x, p.normal.y, p.normal.z}, static_cast<double>(p.offset) - 0.002}};
        // Kept while nothing it shows has moved, and the eye has not.
        uint64_t of = 1469598103934665603ULL;
        for (float f : {cam.eye.x, cam.eye.y, cam.eye.z, cam.forward.x, cam.forward.y, cam.forward.z, cam.up.x, cam.up.y, cam.up.z, cam.fov, aspect,
                        seen.x0, seen.y0, seen.x1, seen.y1, p.normal.x, p.normal.y, p.normal.z, p.offset, scale, p.strength})
            of = mix_bits(of, f);
        for (const PlacedRoom& placed : rooms)
            if (placed.room) of = (of ^ placed.room->data_version()) * 1099511628211ULL, of = (of ^ reinterpret_cast<std::uintptr_t>(placed.room)) * 1099511628211ULL;
        of = (of ^ static_cast<uint64_t>(full_w) << 20 ^ static_cast<uint64_t>(full_h)) * 1099511628211ULL;
        // And as the views seen through its doorways are: drawn again this
        // frame, it shows them as they are now.
        {
            const std::string mine = path_ + "~m" + std::to_string(set.count) + "/";
            for (const auto& [key, at] : slot_)
                if (key.compare(0, mine.size(), mine) == 0 && pool_[at].frame == frame_count_) {
                    of = (of ^ frame_count_) * 1099511628211ULL;
                    break;
                }
        }
        // At a part of the screen's pixels, only where it is on the screen (a
        // few sizes, grown, never made again for a pixel) - as many pixels as
        // `reflection_scale` gives the whole screen, so a mirror that covers
        // little of it is drawn sharper for the same cost, up to the screen's own.
        const float area = std::max((seen.x1 - seen.x0) * (seen.y1 - seen.y0) * 0.25f, 1e-3f);
        // (A window's glass shows little of the room - a few parts in a
        // hundred face on - and that little is drawn at half as sharp.)
        const float sharp = std::min(1.0f, scale / std::sqrt(area)) * (p.glass.empty() ? 1.0f : 0.5f);
        const int w = std::max(1, static_cast<int>(std::lround(full_w * sharp))), h = std::max(1, static_cast<int>(std::lround(full_h * sharp)));
        const int need_w = std::max(1, static_cast<int>(std::ceil(w * (seen.x1 - seen.x0) * 0.5f))),
                  need_h = std::max(1, static_cast<int>(std::ceil(h * (seen.y1 - seen.y0) * 0.5f)));
        const auto step = [](int v, int most) { return std::min(most, (v + 127) / 128 * 128); };
        // (Made once at the most it could take - the screen's pixels - never
        // grown as a mirror comes further into sight: a target made in the
        // middle of a frame stops it for tens of milliseconds.)
        if (!mr.target.valid() || mr.target.width() < need_w || mr.target.height() < need_h) {
            for (std::size_t k = 0; k < mirror_spares_.size(); ++k)
                if (mirror_spares_[k].width() >= std::max(need_w, full_w) && mirror_spares_[k].height() >= std::max(need_h, full_h)) {
                    gl::RenderTarget took = std::move(mirror_spares_[k]);
                    mirror_spares_.erase(mirror_spares_.begin() + static_cast<std::ptrdiff_t>(k));
                    if (mr.target.valid()) mirror_spares_.push_back(std::move(mr.target));
                    mr.target = std::move(took);
                    mr.of = 0;
                    break;
                }
        }
        if (!mr.target.valid() || mr.target.width() < need_w || mr.target.height() < need_h) {
            mr.target.create(std::max({step(need_w, w), full_w, mr.target.valid() ? mr.target.width() : 0}),
                             std::max({step(need_h, h), full_h, mr.target.valid() ? mr.target.height() : 0}), gl::GL_RGBA16F, 0, true);
            mr.of = 0;
        }
        if (mr.of != of) {
            mr.of = of;
            // (Drawn as a view of its own: what the view it is in is cut to -
            // the pyramid through a doorway, the air on this side of one -
            // is not what the mirror sees.)
            const Rect sub_was = sub_, cut_was = cut_;
            const std::string path_was = path_;
            std::vector<spatial::HalfSpace> cull_was;
            cull_was.swap(cull_);
            const HostAir air_was = host_air_;
            host_air_.on = false;
            target_w_ = w, target_h_ = h;
            sub_ = drawn;
            cut_ = Rect{-1, -1, 1, 1};
            path_ = path_was + "~m" + std::to_string(set.count);
            mirroring_ = true;
            mirroring_eye_ = eyes_own;
            draw_world(rooms, cam, aspect, mr.target, /*depth=*/2, kNear, Key{}, above);
            mirroring_ = false;
            mirroring_eye_ = false;
            mr.fx = vp_w_ / static_cast<float>(mr.target.width());
            mr.fy = vp_h_ / static_cast<float>(mr.target.height());
            target_w_ = full_w, target_h_ = full_h;
            sub_ = sub_was, cut_ = cut_was;
            path_ = path_was;
            cull_.swap(cull_was);
            host_air_ = air_was;
            mr.target.mipmap();
            ++times_.portal_views;
        }
        mr.normal = p.normal, mr.offset = p.offset, mr.strength = p.strength, mr.seen = seen, mr.glass = p.glass;
        ++set.count;
    }
}

int GLWorldView::mirror_of(const Element& portal) const {
    const MirrorSet* set = mirrors_now();
    if (!set) return 0;
    for (int i = 0; i < set->count; ++i)
        if (set->at[i].glass == portal.id) return i + 1;
    return 0;
}

void GLWorldView::mirror_uniforms(const gl::Program& p, bool use) {
    static const char* const kPlane[kMirrors] = {"uMirrorPlane[0]", "uMirrorPlane[1]"};
    static const char* const kSeen[kMirrors] = {"uMirrorSeen[0]", "uMirrorSeen[1]"};
    static const char* const kUse[kMirrors] = {"uMirrorUse[0]", "uMirrorUse[1]"};
    static const char* const kPicture[kMirrors] = {"uMirror0", "uMirror1"};
    const MirrorSet* set = use ? mirrors_now() : nullptr;
    const int n = set ? set->count : 0;
    p.set("uMirrorCount", n);
    for (int i = 0; i < kMirrors; ++i) p.set(kPicture[i], kMirrorUnit + i);
    // Where each is on the screen, as the part of it this view is drawn
    // into (sub_) has it: a view through a doorway fills its viewport with
    // only the doorway's part of the screen.
    const float cx = (sub_.x0 + sub_.x1) * 0.5f, cy = (sub_.y0 + sub_.y1) * 0.5f;
    const float hx = std::max((sub_.x1 - sub_.x0) * 0.5f, 1e-6f), hy = std::max((sub_.y1 - sub_.y0) * 0.5f, 1e-6f);
    for (int i = 0; i < n; ++i) {
        const Mirror& mr = set->at[i];
        p.set(kPlane[i], mr.normal.x, mr.normal.y, mr.normal.z, mr.offset);
        p.set(kSeen[i], (mr.seen.x0 - cx) / hx, (mr.seen.y0 - cy) / hy, (mr.seen.x1 - cx) / hx, (mr.seen.y1 - cy) / hy);
        const float lods = std::floor(std::log2(static_cast<float>(std::max(mr.target.width(), mr.target.height()))));
        // (A window's glass says so by its strength's sign: read by it alone.)
        p.set(kUse[i], mr.glass.empty() ? mr.strength : -mr.strength, mr.fx, mr.fy, lods);
        mr.target.bind_color(kMirrorUnit + i);
    }
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

}  // namespace sg::render
