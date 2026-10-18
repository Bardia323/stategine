// The GL view's proxies (Quality::proxies): the still things of a room, cell
// by cell, merged into a mesh or two whose every corner carries its thing's
// instance record - drawn by the scene's instanced path, every thing as it
// would be drawn with others of its shape, a cell in a call or two. Merged a
// cell or two a frame, never a room at once; a cell a thing in which changed
// merged again once it has stood a while, without what changed.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace sg::render {

std::vector<float> GLWorldView::corners_for(const State& st, const Element& e) const {
    static const Key shape{"shape"}, bevel{"bevel"}, taper{"taper"}, model{"model"}, bend{"bend"};
    const std::string s = e.params.get_or<std::string>(shape, "");
    if (s == "model") {
        const auto* space = dynamic_cast<const Spatial3D*>(&st);
        const std::vector<float>* corners = space ? space->model(Key{e.params.get_or<std::string>(model, "")}) : nullptr;
        if (corners && !corners->empty()) return *corners;
        return gl::cube_vertices();
    }
    const double tp = e.params.num(taper, 1.0);
    if (s == "sphere") return gl::sphere_vertices();
    if (s == "cylinder") return tp == 1.0 ? gl::cylinder_vertices() : gl::cylinder_vertices(28, static_cast<float>(tp));
    const double r = e.params.num(bevel, 0.0);
    if (r <= 0.0 && tp == 1.0) return e.params.num(bend, 0.0) != 0.0 ? gl::grid_cube_vertices(14) : gl::cube_vertices();
    return gl::rounded_box_vertices(static_cast<float>(e.params.num(keys::sx, 1.0)), static_cast<float>(e.params.num(keys::sy, 1.0)),
                                    static_cast<float>(e.params.num(keys::sz, 1.0)), static_cast<float>(r), static_cast<float>(tp));
}

namespace {
// Whether a thing may be merged at all, by what it is (not where): what the
// scene would draw with others of its shape, hung from nothing.
bool mergeable(const GLWorldView& view, const Element& e, bool instanceable, bool skinned) {
    static const Key glass{"glass"}, add{"add"}, unseen{"unseen"}, cutout{"cutout"}, skin{"skin"}, shape{"shape"}, sprite{"sprite"};
    (void)view;
    if (!e.alive || e.kind != kinds::mesh || skinned || !instanceable) return false;
    if (e.params.num(glass, 0.0) > 0.0 || e.params.num(add, 0.0) > 0.5 || e.params.num(unseen, 0.0) > 0.5 || e.params.has(keys::parent)) return false;
    if (e.params.num(cutout, 0.0) > 0.0 && e.params.has(skin)) return false;
    return !e.params.is(shape, "sprite");
}
}  // namespace

auto GLWorldView::proxies_in(const Spatial3D& room) -> RoomProxies* {
    static const Key cell_key{"proxy_cell"};
    RoomProxies& rp = proxies_[&room];
    const auto& elements = room.elements();
    if (rp.structure != room.structure()) {
        // Which cell each thing that may be merged stands in: found again
        // when things come or go; their meshes merged a cell at a time.
        rp.structure = room.structure();
        rp.last.assign(elements.size(), 0);
        rp.cells.clear();
        const double cell = std::max(room.params().num(cell_key, 16.0), 1.0);
        std::map<std::pair<long long, long long>, std::size_t> at;
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const Element& e = elements[i];
            if (!mergeable(*this, e, instanceable(e), skinned(e)) || is_sprite(e) || cuts_out(e)) continue;
            const gl::Mat4& m = box_matrix(room, e).m;
            const auto [it, made] = at.try_emplace({static_cast<long long>(std::floor(m.m[12] / cell)), static_cast<long long>(std::floor(m.m[14] / cell))},
                                                   rp.cells.size());
            if (made) rp.cells.push_back(std::make_unique<ProxyCell>());
            rp.cells[it->second]->candidates.push_back(i);
        }
    }
    // Cells not merged yet, or changed and since stood a while: merged, a
    // few a frame (proxy_budget_) - until then their things are drawn each
    // on its own.
    for (const auto& c : rp.cells) {
        if (proxy_budget_ <= 0) break;
        if (c->candidates.size() < 4) continue;
        if (!c->merged || (c->changed != 0 && frame_count_ > c->changed + 30)) {
            merge_cell(room, rp, *c);
            --proxy_budget_;
        }
    }
    return &rp;
}

void GLWorldView::merge_cell(const Spatial3D& room, RoomProxies& rp, ProxyCell& c) {
    const auto& elements = room.elements();
    const auto plan = draw_plans_.find(&room);
    constexpr std::size_t k = gl::Mesh::kInstanceFloats;
    static std::vector<float> boxes, others;
    boxes.clear(), others.clear();
    c.members.clear(), c.stamps.clear();
    for (int w = 0; w < 2; ++w) c.spans[w].clear(), c.balls[w].clear();
    std::size_t corners_in = 0;
    for (std::size_t i : c.candidates) {
        if (i >= elements.size()) continue;
        const Element& e = elements[i];
        const uint64_t was = rp.last[i];
        rp.last[i] = e.params.stamp();
        // Standing still: not moved since its room was laid out, the same
        // since it was last merged (one that changed waits out a merging).
        if (!mergeable(*this, e, instanceable(e), skinned(e))) continue;
        if (plan != draw_plans_.end() && i < plan->second.mover.size() && plan->second.mover[i]) continue;
        if (was != 0 && was != e.params.stamp()) continue;
        // Small things only: a big model is drawn as well with others of its
        // shape, and its every corner carrying a record would be its size four
        // times over.
        const gl::Mesh& mesh = shape_of(room, e);
        const std::vector<float> corners = corners_for(room, e);
        const std::size_t n = corners.size() / 8;
        if (n > kProxyCorners || corners_in + n > kProxyCellCorners) continue;
        corners_in += n;
        const auto& record = record_of(room, e);
        const int which = &mesh == &cube_ || &mesh == &bent_ ? 0 : 1;
        std::vector<float>& into = which == 0 ? boxes : others;
        // (Where its corners begin and how many, and the ball round it - for
        // culling on the card.)
        {
            const gl::Mat4& m = box_matrix(room, e).m;
            const float r = 0.5f * std::sqrt(m.m[0] * m.m[0] + m.m[1] * m.m[1] + m.m[2] * m.m[2] + m.m[4] * m.m[4] + m.m[5] * m.m[5] + m.m[6] * m.m[6] +
                                             m.m[8] * m.m[8] + m.m[9] * m.m[9] + m.m[10] * m.m[10]) +
                            static_cast<float>(std::abs(e.params.num(Key{"bend"}, 0.0)));
            c.spans[which].insert(c.spans[which].end(), {static_cast<float>(into.size() / (8 + k)), static_cast<float>(n)});
            c.balls[which].insert(c.balls[which].end(), {m.m[12], m.m[13], m.m[14], r});
        }
        for (std::size_t v = 0; v + 8 <= corners.size(); v += 8) {
            into.insert(into.end(), corners.begin() + static_cast<std::ptrdiff_t>(v), corners.begin() + static_cast<std::ptrdiff_t>(v + 8));
            into.insert(into.end(), record.begin(), record.begin() + k);
        }
        c.members.push_back(i);
        c.stamps.push_back(e.params.stamp());
    }
    c.any_boxes = !boxes.empty(), c.any_others = !others.empty();
    if (c.any_boxes) c.boxes.create_merged(boxes);
    if (c.any_others) c.others.create_merged(others);
    // (And its members' balls and spans sent, for the card to cull them by.)
    if (q_.gpu_cull && gpu_culls())
        for (int w = 0; w < 2; ++w) {
            if (c.balls[w].empty()) continue;
            const std::size_t n = c.balls[w].size() / 4;
            std::vector<gl::GLuint> spans(n * 2);
            for (std::size_t i = 0; i < n * 2; ++i) spans[i] = static_cast<gl::GLuint>(c.spans[w][i]);
            if (!c.ball_buffer[w]) gl::glGenBuffers(1, &c.ball_buffer[w]), gl::glGenBuffers(1, &c.span_buffer[w]), gl::glGenBuffers(1, &c.draws[w]);
            gl::glBindBuffer(gl::GL_SHADER_STORAGE_BUFFER, c.ball_buffer[w]);
            gl::glBufferData(gl::GL_SHADER_STORAGE_BUFFER, static_cast<gl::GLsizeiptr>(c.balls[w].size() * sizeof(float)), c.balls[w].data(), gl::GL_STATIC_DRAW);
            gl::glBindBuffer(gl::GL_SHADER_STORAGE_BUFFER, c.span_buffer[w]);
            gl::glBufferData(gl::GL_SHADER_STORAGE_BUFFER, static_cast<gl::GLsizeiptr>(spans.size() * sizeof(gl::GLuint)), spans.data(), gl::GL_STATIC_DRAW);
            gl::glBindBuffer(gl::GL_SHADER_STORAGE_BUFFER, c.draws[w]);
            gl::glBufferData(gl::GL_SHADER_STORAGE_BUFFER, static_cast<gl::GLsizeiptr>(n * 4 * sizeof(gl::GLuint)), nullptr, gl::GL_DYNAMIC_DRAW);
            gl::glBindBuffer(gl::GL_SHADER_STORAGE_BUFFER, 0);
        }
    c.merged = true, c.changed = 0;
    ++times_.proxies_merged;
}

bool GLWorldView::gpu_culls() {
    if (gpu_cull_state_ != 0) return gpu_cull_state_ > 0;
    gpu_cull_state_ = -1;
    // A context of 4.3 or more, or one that says it has each of these (a
    // 3.3 context the driver gives them to, as it gives clip control).
    const char* version = reinterpret_cast<const char*>(gl::glGetString(gl::GL_VERSION));
    int major = 0, minor = 0;
    const bool new_enough = version && std::sscanf(version, "%d.%d", &major, &minor) == 2 && major * 10 + minor >= 43;
    if (!new_enough) {
        int has = 0;
        gl::GLint n = 0;
        gl::glGetIntegerv(gl::GL_NUM_EXTENSIONS, &n);
        for (gl::GLint i = 0; i < n; ++i)
            if (const unsigned char* e = gl::glGetStringi(gl::GL_EXTENSIONS, static_cast<gl::GLuint>(i)))
                for (const char* want : {"GL_ARB_compute_shader", "GL_ARB_shader_storage_buffer_object", "GL_ARB_multi_draw_indirect", "GL_ARB_draw_indirect"})
                    has += std::strcmp(reinterpret_cast<const char*>(e), want) == 0 ? 1 : 0;
        if (has < 4) return false;
    }
    if (!gl::glDispatchCompute || !gl::glMemoryBarrier || !gl::glBindBufferBase || !gl::glMultiDrawArraysIndirect) return false;
    // Each member of a mesh drawn (its corners) or not (none) by whether its
    // ball is in the view: a draw the card says to itself, one a member.
    static const char* source = R"(#version 430
#extension GL_ARB_compute_shader : enable
#extension GL_ARB_shader_storage_buffer_object : enable
layout(local_size_x = 64) in;
layout(std430, binding = 0) readonly buffer Balls { vec4 balls[]; };
layout(std430, binding = 1) readonly buffer Spans { uvec2 spans[]; };
layout(std430, binding = 2) writeonly buffer Draws { uvec4 draws[]; };
uniform vec4 uPlanes[5];  // the view's sides and its far end, in the room's frame
uniform uint uCount;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uCount) return;
    vec4 b = balls[i];
    bool seen = true;
    for (int k = 0; k < 5; ++k)
        if (dot(uPlanes[k].xyz, b.xyz) + uPlanes[k].w < -b.w * length(uPlanes[k].xyz)) seen = false;
    draws[i] = uvec4(seen ? spans[i].y : 0u, 1u, spans[i].x, 0u);
})";
    try {
        const gl::GLuint shader = gl::compile(gl::GL_COMPUTE_SHADER, source, "cull on the card");
        cull_program_ = gl::glCreateProgram();
        gl::glAttachShader(cull_program_, shader);
        gl::glLinkProgram(cull_program_);
        gl::glDeleteShader(shader);
        gl::GLint linked = 0;
        gl::glGetProgramiv(cull_program_, gl::GL_LINK_STATUS, &linked);
        if (!linked) return false;
    } catch (const std::exception&) {
        return false;
    }
    gpu_cull_state_ = 1;
    return true;
}

void GLWorldView::cull_on_card(const ProxyCell& c, int which) {
    // The view's planes in the room's frame: its matrix after the frame's
    // (Gribb and Hartmann's rows), its near end left out (whichever way its
    // depth runs, nothing in front of the eye is lost by that).
    const gl::Mat4 m = last_view_proj_ * frame_matrix_;
    const auto row = [&](int r) { return std::array<float, 4>{m.m[r], m.m[4 + r], m.m[8 + r], m.m[12 + r]}; };
    const auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    float planes[5][4];
    for (int k = 0; k < 4; ++k) {
        planes[0][k] = r3[k] + r0[k], planes[1][k] = r3[k] - r0[k];
        planes[2][k] = r3[k] + r1[k], planes[3][k] = r3[k] - r1[k];
        planes[4][k] = r3[k] - r2[k];
    }
    const auto n = static_cast<gl::GLuint>(c.balls[which].size() / 4);
    gl::glUseProgram(cull_program_);
    gl::glUniform4fv(gl::glGetUniformLocation(cull_program_, "uPlanes"), 5, &planes[0][0]);
    gl::glUniform1ui(gl::glGetUniformLocation(cull_program_, "uCount"), n);
    gl::glBindBufferBase(gl::GL_SHADER_STORAGE_BUFFER, 0, c.ball_buffer[which]);
    gl::glBindBufferBase(gl::GL_SHADER_STORAGE_BUFFER, 1, c.span_buffer[which]);
    gl::glBindBufferBase(gl::GL_SHADER_STORAGE_BUFFER, 2, c.draws[which]);
    gl::glDispatchCompute((n + 63) / 64, 1, 1);
    gl::glMemoryBarrier(gl::GL_COMMAND_BARRIER_BIT | gl::GL_SHADER_STORAGE_BARRIER_BIT);
    // (How many it culled, only while the frame is timed: read back.)
    if (timing_) {
        std::vector<gl::GLuint> said(n * 4);
        gl::glBindBuffer(gl::GL_SHADER_STORAGE_BUFFER, c.draws[which]);
        gl::glGetBufferSubData(gl::GL_SHADER_STORAGE_BUFFER, 0, static_cast<gl::GLsizeiptr>(said.size() * sizeof(gl::GLuint)), said.data());
        gl::glBindBuffer(gl::GL_SHADER_STORAGE_BUFFER, 0);
        for (std::size_t i = 0; i < n; ++i) times_.culled_on_card += said[i * 4] == 0 ? 1 : 0;
    }
    scene_->use();
}

void GLWorldView::draw_proxies(const std::vector<const ProxyCell*>& cells) {
    const gl::Program& p = *scene_;
    p.set("uInstanced", 1);
    p.set("uFrame", frame_matrix_);
    p.set("uTexMix", 0.0f);
    p.set("uGlow", 0.0f);
    p.set("uSkin", 0.0f);
    p.set("uScreenUV", 0.0f);
    p.set("uCRT", 0.0f);
    // (A box's far side not drawn, as its batch draws it; the rest whole.)
    gl::glEnable(gl::GL_CULL_FACE);
    gl::glCullFace(gl::GL_BACK);
    // (Or, where the card culls them itself, each member drawn or not by the
    // draws it says to itself.)
    const bool on_card = q_.gpu_cull && gpu_culls();
    const auto draw = [&](const ProxyCell& c, int which) {
        const gl::Mesh& mesh = which == 0 ? c.boxes : c.others;
        if (on_card && c.draws[which] && !c.balls[which].empty()) {
            cull_on_card(c, which);
            mesh.draw_commands(c.draws[which], static_cast<gl::GLsizei>(c.balls[which].size() / 4));
        } else {
            mesh.draw();
        }
        ++times_.proxy_calls;
    };
    for (const ProxyCell* c : cells)
        if (c->any_boxes) draw(*c, 0);
    gl::glDisable(gl::GL_CULL_FACE);
    for (const ProxyCell* c : cells) {
        if (c->any_others) draw(*c, 1);
        times_.proxied += static_cast<int>(c->members.size());
    }
    p.set("uInstanced", 0);
}

}  // namespace sg::render
