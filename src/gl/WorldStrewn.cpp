// The GL view's strewn things (Spatial3D::placings): an element of kind
// `strewn` stands for many of one thing, each at one of its placings. Its
// placings are laid once into cells of the ground - each its instance record,
// as a fixture of its own placed, turned and sized so would have - again only
// when they are made again or the element moves; a frame draws the cells in
// view and within its `far` as one batch of its model, nearest cells first,
// and lays the cells near the eye into the shadow maps.
#include "sg/gl/World.hpp"

#include <algorithm>
#include <cmath>

#include "WorldHash.hpp"

namespace sg::render {

namespace {
// A cell's side, metres.
constexpr float kStrewnCell = 32.0f;
float length(const gl::Vec3& v) { return std::sqrt(gl::dot(v, v)); }
}  // namespace

auto GLWorldView::strewn_cells(const Spatial3D& room, const Element& e) -> const StrewnCells& {
    const uint64_t key = fnv(fnv(1469598103934665603ULL, reinterpret_cast<std::uintptr_t>(&room)), std::hash<Key>{}(e.id));
    StrewnCells& s = strewn_[key];
    const Key name{e.params.get_or<std::string>(Key{"placings"}, "")};
    const std::vector<float>* at = room.placings(name);
    const uint64_t made = fnv(fnv(room.placings_revision(name), placed_of(room, e).stamp), at ? at->size() : 0);
    if (s.made == made) return s;
    s.made = made;
    s.cells.clear();
    if (!at) return s;
    // Its own frame (its pose, its anchors'), as a fixture's box is turned.
    const Pose w = pose_of(room, e);
    gl::Mat4 frame = gl::Mat4::translate(to_vec3(w.position)) * gl::Mat4::rotate_y(static_cast<float>(w.yaw));
    if (!upright(w)) frame = frame * gl::Mat4::rotate_z(static_cast<float>(w.pitch)) * gl::Mat4::rotate_x(static_cast<float>(w.roll));
    const gl::Vec3 size{static_cast<float>(e.params.num(keys::sx, 1.0)), static_cast<float>(e.params.num(keys::sy, 1.0)),
                        static_cast<float>(e.params.num(keys::sz, 1.0))};
    const gl::Vec3 c = color_of(e, {0.8f, 0.5f, 0.25f});
    const bool shaded = e.params.num(Key{"shaded"}, 0.0) > 0.5;
    const float rough = static_cast<float>(e.params.num(Key{"roughness"}, 0.6)), surface = static_cast<float>(e.params.num(Key{"surface"}, 3.0));
    const float bend = static_cast<float>(e.params.num(Key{"bend"}, 0.0));
    const float reach = 0.5f * length(size);
    std::unordered_map<uint64_t, std::size_t> cell_of;
    constexpr std::size_t k = gl::Mesh::kInstanceFloats, n = Spatial3D::kStrewFloats;
    for (std::size_t i = 0; i + n <= at->size(); i += n) {
        const float* p = &(*at)[i];
        const float scale = p[4];
        const gl::Vec3 foot = frame.transform_point({p[0], p[1], p[2]});
        const gl::Mat4 m = frame * gl::Mat4::translate({p[0], p[1] + 0.5f * size.y * scale, p[2]}) * gl::Mat4::rotate_y(p[3]) *
                           gl::Mat4::scale(size * scale);
        const int cx = static_cast<int>(std::floor(foot.x / kStrewnCell)), cz = static_cast<int>(std::floor(foot.z / kStrewnCell));
        const uint64_t ck = (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz);
        auto [it, fresh] = cell_of.try_emplace(ck, s.cells.size());
        if (fresh) s.cells.push_back(StrewnCells::Cell{{1e30f, 1e30f, 1e30f}, {-1e30f, -1e30f, -1e30f}, {}});
        StrewnCells::Cell& cell = s.cells[it->second];
        const float r = reach * scale;
        const gl::Vec3 mid = m.transform_point({0, 0, 0});
        cell.lo = {std::min(cell.lo.x, mid.x - r), std::min(cell.lo.y, mid.y - r), std::min(cell.lo.z, mid.z - r)};
        cell.hi = {std::max(cell.hi.x, mid.x + r), std::max(cell.hi.y, mid.y + r), std::max(cell.hi.z, mid.z + r)};
        const float shade = shaded ? p[5] : 1.0f;
        float record[k] = {};
        std::copy(m.m, m.m + 16, record);
        const float mat[12] = {c.x * shade, c.y * shade, c.z * shade, rough, surface, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, bend};
        std::copy(mat, mat + 12, record + 16);
        cell.records.insert(cell.records.end(), record, record + k);
    }
    return s;
}

void GLWorldView::strewn_into(const Spatial3D& room, const Element& e, const Frustum* view, float far) {
    const StrewnCells& s = strewn_cells(room, e);
    if (s.cells.empty()) return;
    // The cells near enough (by their nearest point) and in view, nearest
    // first: what is near hides what is behind it before that is shaded.
    order_strewn_.clear();
    for (std::size_t i = 0; i < s.cells.size(); ++i) {
        const StrewnCells::Cell& cell = s.cells[i];
        const gl::Vec3 mid = frame_matrix_.transform_point((cell.lo + cell.hi) * 0.5f);
        const float r = 0.5f * length(cell.hi - cell.lo);
        const gl::Vec3 away = mid - cam_eye_;
        const float d = std::max(length(away) - r, 0.0f);
        if (d > far) continue;
        if (view) {
            bool in = true;
            for (const auto& p : view->planes)
                if (static_cast<float>(p.normal.x) * mid.x + static_cast<float>(p.normal.y) * mid.y + static_cast<float>(p.normal.z) * mid.z +
                        static_cast<float>(p.offset) < -r) {
                    in = false;
                    break;
                }
            if (!in) continue;
        }
        order_strewn_.emplace_back(d, i);
    }
    std::sort(order_strewn_.begin(), order_strewn_.end());
    Batch& b = batch_for(shape_of(room, e));
    b.ordered = true;
    for (const auto& [d, i] : order_strewn_) {
        const std::vector<float>& r = s.cells[i].records;
        b.data.insert(b.data.end(), r.begin(), r.end());
        if (view) times_.strewn += static_cast<int>(r.size() / gl::Mesh::kInstanceFloats);
    }
}

}  // namespace sg::render
