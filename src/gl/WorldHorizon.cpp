// The GL view's terrain horizons: how steeply the land toward the sun rises
// over each point of a terrain, made on the card from its heights on a grid
// round the eye - so a sun low behind a hill leaves the land behind it in
// shadow however far past the sun's own map that land goes, and the air
// over it too. The heights are the terrain's own (its height function),
// sampled in the background as its ground is, again only as the eye goes
// far; the horizon is made again only as the sun's bearing turns. The sun
// rising or setting makes nothing: the scene compares its height with the
// horizon's.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace sg::render {

namespace {
// The grid's cells a side.
constexpr int kHorizonCells = 512;

// The heights of `height` at the middle of each cell of a grid, `cell`
// metres a cell from the corner (x0, z0): a row a z.
std::vector<float> sample_grid(const std::function<double(double, double)>& height, double x0, double z0, double cell) {
    const int n = kHorizonCells;
    std::vector<float> h(static_cast<std::size_t>(n) * n);
    const int workers = static_cast<int>(std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w)
        pool.emplace_back([&, w] {
            for (int j = w; j < n; j += workers)
                for (int i = 0; i < n; ++i)
                    h[static_cast<std::size_t>(j) * n + i] = static_cast<float>(height(x0 + (i + 0.5) * cell, z0 + (j + 0.5) * cell));
        });
    for (std::thread& th : pool) th.join();
    return h;
}
}  // namespace

auto GLWorldView::horizon_for(const std::vector<PlacedRoom>& rooms, const gl::Vec3& eye, const gl::Vec3& sun_dir, bool own) -> const TerrainMesh* {
    if (rooms.empty() || !rooms.front().room || baking_) return nullptr;
    const PlacedRoom& here = rooms.front();
    // (Its room as the view has it: moved, not turned.)
    if (!upright(here.pose) || here.pose.yaw != 0.0) return nullptr;
    for (const Element* e : index_of(*here.room).terrains) {
        if (!e->alive) continue;
        auto it = terrains_.find(e->id);
        if (it == terrains_.end() || !it->second.height) continue;
        TerrainMesh& t = it->second;
        // How far toward the sun it looks for land that hides it (metres; 0
        // none) - past as much as is drawn round the eye (`reach`).
        const double reach = e->params.num(Key{"reach"}, 320.0);
        const double far = e->params.num(Key{"horizon"}, 2.0 * reach);
        if (far <= 0.0) return nullptr;
        // (Another view's eye is not the eye's: as it was made.)
        if (!own) return t.horizon_made ? &t : nullptr;
        const double half = reach + far, cell = 2.0 * half / kHorizonCells, rev = e->params.num(Key{"rev"}, 0.0);
        const double ex = eye.x - here.pose.position.x, ez = eye.z - here.pose.position.z;
        // Its corner kept to whole steps of 32 cells, the eye about its middle.
        const double snap = 32.0 * cell;
        const double cx = std::round((ex - half) / snap) * snap, cz = std::round((ez - half) / snap) * snap;
        bool fresh = false;
        const auto send = [&](const std::vector<float>& h, double x, double z) {
            if (!t.heights.valid()) t.heights.create(kHorizonCells, kHorizonCells, gl::GL_R32F, 0, false);
            gl::glBindBuffer(gl::GL_PIXEL_UNPACK_BUFFER, 0);
            t.heights.bind_color(0);
            gl::glTexSubImage2D(gl::GL_TEXTURE_2D, 0, 0, 0, kHorizonCells, kHorizonCells, gl::GL_RED, gl::GL_FLOAT, h.data());
            t.grid_x = x, t.grid_z = z, t.grid_cell = cell, t.grid_rev = rev;
            fresh = true;
        };
        // A finished resample goes in, if it is still the grid wanted.
        if (t.grid_job.valid() && t.grid_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const std::vector<float> h = t.grid_job.get();
            if (t.grid_job_rev == rev && t.grid_job_cell == cell) send(h, t.grid_job_x, t.grid_job_z);
        }
        if (t.grid_rev != rev || t.grid_cell != cell) {
            // None yet, or the land asks to be read again: wait for it.
            if (t.grid_job.valid()) t.grid_job.wait(), t.grid_job = {};
            send(sample_grid(t.height, cx, cz, cell), cx, cz);
        } else if (!t.grid_job.valid() && (std::fabs(ex - (t.grid_x + half)) > 0.25 * half || std::fabs(ez - (t.grid_z + half)) > 0.25 * half)) {
            // The eye gone out of the middle of it: a grid round it, made in
            // the background - this one is drawn with meanwhile.
            t.grid_job_x = cx, t.grid_job_z = cz, t.grid_job_cell = cell, t.grid_job_rev = rev;
            t.grid_job = std::async(std::launch::async, [height = t.height, cx, cz, cell] { return sample_grid(height, cx, cz, cell); });
        }
        // The sun's bearing: none overhead (nothing is made for it; no land
        // hides a sun that high), else made again once it turns a little.
        const float across = std::sqrt(sun_dir.x * sun_dir.x + sun_dir.z * sun_dir.z);
        if (across < 1e-3f) return t.horizon_made ? &t : nullptr;
        const float bx = sun_dir.x / across, bz = sun_dir.z / across;
        if (!fresh && t.horizon_made && bx * t.bearing_x + bz * t.bearing_z > 0.99999f) return &t;
        if (!horizon_prog_) horizon_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::horizon_fs(), "horizon");
        if (!t.horizon.valid()) t.horizon.create(kHorizonCells, kHorizonCells, gl::GL_RG32F, 0, false);
        gl::glDisable(gl::GL_DEPTH_TEST);
        gl::glDisable(gl::GL_CULL_FACE);
        gl::glDisable(gl::GL_BLEND);
        gl::glDisable(gl::GL_SCISSOR_TEST);
        const gl::Program& p = *horizon_prog_;
        p.use();
        p.set("uHeights", 0);
        p.set("uHorizonGrid", static_cast<float>(kHorizonCells), static_cast<float>(cell));
        p.set("uHorizonSun", bx, bz);
        t.horizon.bind();
        t.heights.bind_color(0);
        screen_.draw();
        gl::glActiveTexture(gl::GL_TEXTURE0);
        t.bearing_x = bx, t.bearing_z = bz, t.horizon_made = true;
        ++times_.horizons_made;
        return &t;
    }
    return nullptr;
}

void GLWorldView::horizon_to(const gl::Program& p, const TerrainMesh* t, const PlacedRoom& here, int unit) const {
    if (!t) {
        p.set("uHorizonAt", 0.0f, 0.0f, 1.0f, 0.0f);
        return;
    }
    const double width = t->grid_cell * kHorizonCells;
    p.set("uHorizonMap", unit);
    p.set("uHorizonAt", static_cast<float>(t->grid_x + here.pose.position.x), static_cast<float>(t->grid_z + here.pose.position.z),
          static_cast<float>(1.0 / width), 1.0f);
    p.set("uHorizonCell", static_cast<float>(t->grid_cell), static_cast<float>(here.pose.position.y));
    t->horizon.bind_color(unit);
    gl::glActiveTexture(gl::GL_TEXTURE0);
}

}  // namespace sg::render
