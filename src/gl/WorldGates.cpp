// The GL view's questions about doorways the eye cannot see
// (Quality::skip_unseen): what stands in the rooms the eye is in laid as
// depth alone, and each doorway a view is drawn through asked of it - the
// card then leaves out what the view would draw where none of it is seen.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sg::render {

auto GLWorldView::occluders_in(const Spatial3D& room, bool shell) -> RoomOccluders& {
    RoomOccluders& r = occluders_of_[&room];
    bool changed = false;
    if (r.structure != room.structure()) {
        r.structure = room.structure();
        r.list.clear();
        changed = true;
        static const Key unseen{"unseen"}, glass{"glass"}, add{"add"};
        for (const Element& e : room.elements()) {
            if (e.params.num(unseen, 0.0) > 0.5 || e.params.has(keys::parent)) continue;
            if (e.kind == kinds::wall) {
                // (What fills an opening onto nothing is not there where the
                // opening is glued: never taken to hide what is beyond it.)
                const std::string& n = e.id.str();
                if (n.size() >= 6 && n.compare(n.size() - 6, 6, ".blank") == 0) continue;
                r.list.push_back(Occluder{&e});
            } else if (e.kind == kinds::mesh) {
                // Only what nothing is seen through: not glass, light added over
                // the room, a picture on a card or a thing cut out of one.
                // (Nor a skin: posed by its joints, it is no shape to lay as it stands.)
                if (e.params.num(glass, 0.0) > 0.0 || e.params.num(add, 0.0) > 0.5 || is_sprite(e) || cuts_out(e) || skinned(e)) continue;
                r.list.push_back(Occluder{&e});
            }
        }
    }
    // Each where it is now: worked out again only where its stamp moved.
    for (Occluder& o : r.list) {
        const Element& e = *o.element;
        if (e.alive != o.alive) o.alive = e.alive, changed = true;
        if (e.params.stamp() == o.stamp) continue;
        o.stamp = e.params.stamp();
        // (A glow, a colour, changed: nothing of where it stands or its shape.)
        const gl::Mat4& model = box_matrix(room, e).m;
        const gl::Mesh* shape = e.kind == kinds::wall ? &cube_ : &shape_of(room, e);
        if (shape == o.shape && std::equal(model.m, model.m + 16, o.model.m)) continue;
        o.model = model, o.shape = shape;
        o.radius = query_bounds(room_local(o.model)).radius;
        changed = true;
    }
    // (The shell by where its walls are, not by the room's every word.)
    std::vector<gl::Mat4> walls;
    uint64_t shell_now = 0;
    if (shell)
        for (const auto& d : enclosure(room)) {
            walls.push_back(d.model);
            for (float f : d.model.m) shell_now = mix_bits(shell_now, f);
        }
    if (shell_now != r.shell) r.shell = shell_now, changed = true;
    if (!changed && r.stream != 0) return r;
    // Sent again: the shell, then a run for each shape - each a hand across or more.
    constexpr std::size_t k = gl::Mesh::kInstanceFloats;
    std::vector<std::pair<const gl::Mesh*, const gl::Mat4*>> drawn;
    for (const gl::Mat4& m : walls) drawn.push_back({&cube_, &m});
    for (const Occluder& o : r.list)
        if (o.alive && o.radius >= 0.12f) drawn.push_back({o.shape, &o.model});
    std::stable_sort(drawn.begin(), drawn.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<float> stream;
    stream.reserve(drawn.size() * k);
    r.runs.clear();
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        if (r.runs.empty() || r.runs.back().first != drawn[i].first) r.runs.push_back({drawn[i].first, {i, 0}});
        ++r.runs.back().second.second;
        stream.insert(stream.end(), drawn[i].second->m, drawn[i].second->m + 16);
        stream.insert(stream.end(), k - 16, 0.0f);
    }
    if (!r.buffer) r.buffer = std::make_unique<gl::InstanceBuffer>();
    r.stream = stream.empty() ? 0 : r.buffer->upload(stream);
    return r;
}

gl::GLuint GLWorldView::gate_of(const std::string& path) const {
    const auto it = doorway_asked_.find(path);
    return it == doorway_asked_.end() ? 0 : it->second;
}

void GLWorldView::ask_doorways(const std::vector<Asked>& rooms, float aspect) {
    doorway_asked_.clear();
    const auto from = std::chrono::steady_clock::now();
    struct Spent {
        FrameTimes& t;
        std::chrono::steady_clock::time_point from;
        ~Spent() { t.asked_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count(); }
    } spent{times_, from};
    // The doorways a view may be drawn through, each where its picture is
    // laid (draw_portal) - a little nearer the eye, and a little wider, than
    // it is: what is in doubt is seen.
    struct Door {
        std::string path;
        gl::Mat4 model;
        std::size_t room;
    };
    std::vector<Door> doors;
    for (std::size_t r = 0; r < rooms.size(); ++r) {
        const Spatial3D& world = *rooms[r].room;
        const Camera& cam = rooms[r].cam;
        const std::vector<Key>* openings = rooms[r].openings;
        for (const auto& e : world.elements()) {
            if (e.kind != kinds::portal || !e.alive || is_screen(e) || e.params.has(Key{"ball"}) || e.params.num(Key{"own_look"}, 0.0) > 0.5) continue;
            if (openings && std::find(openings->begin(), openings->end(), e.id.key()) != openings->end()) continue;
            const auto it = worlds_.find(e.id);
            if (it == worlds_.end() || !it->second.world || !declared_world(world, e, *it->second.world)) continue;
            if (!in_view(world, e, cam, aspect)) continue;
            const Pose pose = pose_of(world, e);
            const gl::Vec3 n = to_vec3(facing(pose));
            const float w = static_cast<float>(e.params.num(keys::w, 3.0)), h = static_cast<float>(e.params.num(keys::h, 2.0));
            gl::Vec3 at = to_vec3(pose.position) + n * static_cast<float>(portal_inset(e));
            const gl::Vec3 to_eye = cam.eye - at;
            const float away = std::sqrt(gl::dot(to_eye, to_eye));
            if (away > 1e-4f) at = at + to_eye * (0.03f / away);
            doors.push_back({"/" + e.id.str(),
                             gl::Mat4::translate(at) * panel_turn(pose, e) * gl::Mat4::scale({1.0f, h * 1.06f + 0.04f, w * 1.06f + 0.04f}), r});
        }
    }
    if (doors.empty()) return;
    // What stands in each room, as depth alone, at half the screen's size.
    const int qw = std::max(1, target_w_ / 2), qh = std::max(1, target_h_ / 2);
    if (!occluders_.valid() || occluders_.width() != qw || occluders_.height() != qh) occluders_.create(qw, qh, gl::GL_RGBA8, 0, true);
    occluders_.bind();
    gl::glDisable(gl::GL_SCISSOR_TEST);
    gl::glDisable(gl::GL_BLEND);
    gl::glDisable(gl::GL_CULL_FACE);
    gl::glColorMask(0, 0, 0, 0);
    gl::glDepthMask(1);
    gl::glClearDepth(1.0);
    gl::glClear(gl::GL_DEPTH_BUFFER_BIT);
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glDepthFunc(gl::GL_LESS);
    const gl::Program& depth = *program_for(post_.shown(), passes::shadow);
    depth.use();
    depth.set("uInstanced", 0);
    depth.set("uCasterSide", 0.0f, 0.0f, 0.0f, 1.0f);
    std::vector<gl::Mat4> seen_by(rooms.size());
    for (std::size_t r = 0; r < rooms.size(); ++r) {
        const Spatial3D& room = *rooms[r].room;
        const Camera& cam = rooms[r].cam;
        const float zfar = static_cast<float>(room.params().num(Key{"far"}, 120.0));
        seen_by[r] = projection_of(cam, aspect, kNear, zfar) * gl::Mat4::look_at(cam.eye, cam.eye + cam.forward, cam.up);
        depth.set("uLightViewProj", seen_by[r]);
        // Its floor, walls and ceiling - as the room draws them (draw_world:
        // none under a sky, nor round a room of a space made one that does
        // not say how big it is) - and what stands in it, a hand across or
        // more: those of a shape drawn together.
        const bool shell = room.params().num(Key{"sky"}, 0.0) <= 0.5 && !(rooms[r].sheafed && !room.params().has(Key{"room_w"}));
        const RoomOccluders& occluders = occluders_in(room, shell);
        if (occluders.stream == 0) continue;
        depth.set("uInstanced", 1);
        depth.set("uFrame", gl::Mat4::identity());
        for (const auto& [shape, run] : occluders.runs)
            shape->draw_instanced(occluders.stream, static_cast<gl::GLsizei>(run.second), run.first);
        depth.set("uInstanced", 0);
    }
    // Each doorway asked whether any of it is in front of all that.
    gl::glDepthMask(0);
    gl::glDepthFunc(gl::GL_LEQUAL);
    if (questions_.size() < doors.size()) {
        const std::size_t had = questions_.size();
        questions_.resize(doors.size());
        gl::glGenQueries(static_cast<gl::GLsizei>(doors.size() - had), questions_.data() + had);
    }
    for (std::size_t i = 0; i < doors.size(); ++i) {
        depth.set("uLightViewProj", seen_by[doors[i].room]);
        depth.set("uModel", doors[i].model);
        gl::glBeginQuery(gl::GL_ANY_SAMPLES_PASSED, questions_[i]);
        quad_.draw();
        gl::glEndQuery(gl::GL_ANY_SAMPLES_PASSED);
        doorway_asked_[doors[i].path] = questions_[i];
    }
    gl::glColorMask(1, 1, 1, 1);
    gl::glDepthMask(1);
    gl::glDepthFunc(gl::GL_LESS);
}

}  // namespace sg::render
