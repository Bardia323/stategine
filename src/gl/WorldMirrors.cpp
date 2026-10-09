// The GL view's mirrors: a plane things in the eye's room say they reflect
// in, and the room seen in it - drawn again from the eye mirrored in that
// plane, only what stands above it, only where the plane is on the screen,
// at a part of the screen's pixels, with the room's own shadow maps and light.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <cmath>

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

void GLWorldView::draw_mirrors(const std::vector<PlacedRoom>& rooms, const Camera& eye, float aspect) {
    mirror_count_ = 0;
    if (!q_.reflections || rooms.empty() || !rooms.front().room || eye.ortho > 0.0f) return;
    const Spatial3D& room = *rooms.front().room;
    // The planes said to reflect, each with where it is on the screen: a
    // thing's top face (`reflects`), the room's floor (`floor_reflects`).
    struct Plane {
        gl::Vec3 normal;
        float offset = 0.0f, strength = 0.0f;
        Rect seen{1, 1, -1, -1};
        Key glass;
    };
    std::vector<Plane> planes;
    const auto add = [&](const std::vector<gl::Vec3>& corners, gl::Vec3 normal, float strength, Key glass) {
        const float offset = -gl::dot(normal, corners.front());
        // (Seen from above it only: from below, a plane shows nothing.)
        if (gl::dot(normal, eye.eye) + offset <= 0.01f) return;
        const Rect r = seen_rect(corners, eye, aspect);
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
    const float scale = std::clamp(q_.reflection_scale, 0.1f, 1.0f);
    mirror_eye_vp_ = projection_of(eye, aspect, kNear, static_cast<float>(room.params().num(Key{"far"}, 120.0))) *
                     gl::Mat4::look_at(eye.eye, eye.eye + eye.forward, eye.up);
    const int full_w = target_w_, full_h = target_h_;
    for (const Plane& p : planes) {
        if (mirror_count_ >= kMirrors) break;
        Mirror& mr = mirrors_[mirror_count_];
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
        if (!mr.target.valid() || mr.target.width() < need_w || mr.target.height() < need_h) {
            mr.target.create(std::max(step(need_w, w), mr.target.valid() ? mr.target.width() : 0),
                             std::max(step(need_h, h), mr.target.valid() ? mr.target.height() : 0), gl::GL_RGBA16F, 0, true);
            mr.of = 0;
        }
        if (mr.of != of) {
            mr.of = of;
            const Rect sub_was = sub_, cut_was = cut_;
            const std::string path_was = path_;
            target_w_ = w, target_h_ = h;
            sub_ = drawn;
            cut_ = Rect{-1, -1, 1, 1};
            path_ = "~mirror" + std::to_string(mirror_count_);
            mirroring_ = true;
            draw_world(rooms, cam, aspect, mr.target, /*depth=*/2, kNear, Key{}, above);
            mirroring_ = false;
            mr.fx = vp_w_ / static_cast<float>(mr.target.width());
            mr.fy = vp_h_ / static_cast<float>(mr.target.height());
            target_w_ = full_w, target_h_ = full_h;
            sub_ = sub_was, cut_ = cut_was;
            path_ = path_was;
            mr.target.mipmap();
            ++times_.portal_views;
        }
        mr.normal = p.normal, mr.offset = p.offset, mr.strength = p.strength, mr.seen = seen, mr.glass = p.glass;
        ++mirror_count_;
    }
}

int GLWorldView::mirror_of(const Element& portal) const {
    if (mirroring_) return 0;
    for (int i = 0; i < mirror_count_; ++i)
        if (mirrors_[i].glass == portal.id) return i + 1;
    return 0;
}

void GLWorldView::mirror_uniforms(const gl::Program& p, bool use) {
    static const char* const kPlane[kMirrors] = {"uMirrorPlane[0]", "uMirrorPlane[1]"};
    static const char* const kSeen[kMirrors] = {"uMirrorSeen[0]", "uMirrorSeen[1]"};
    static const char* const kUse[kMirrors] = {"uMirrorUse[0]", "uMirrorUse[1]"};
    static const char* const kPicture[kMirrors] = {"uMirror0", "uMirror1"};
    const int n = use ? mirror_count_ : 0;
    p.set("uMirrorCount", n);
    for (int i = 0; i < kMirrors; ++i) p.set(kPicture[i], kMirrorUnit + i);
    for (int i = 0; i < n; ++i) {
        const Mirror& mr = mirrors_[i];
        p.set(kPlane[i], mr.normal.x, mr.normal.y, mr.normal.z, mr.offset);
        p.set(kSeen[i], mr.seen.x0, mr.seen.y0, mr.seen.x1, mr.seen.y1);
        const float lods = std::floor(std::log2(static_cast<float>(std::max(mr.target.width(), mr.target.height()))));
        // (A window's glass says so by its strength's sign: read by it alone.)
        p.set(kUse[i], mr.glass.empty() ? mr.strength : -mr.strength, mr.fx, mr.fy, lods);
        mr.target.bind_color(kMirrorUnit + i);
    }
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

}  // namespace sg::render
