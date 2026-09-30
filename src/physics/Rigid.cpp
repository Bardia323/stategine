#include "sg/physics/Rigid.hpp"

namespace sg::rigid {

auto Hull::prism(V3 centre, V3 half, int sides, const M3& turn, double taper) -> Hull {
    Hull h;
    const int n = std::max(3, sides);
    const double k = n == 4 ? std::sqrt(2.0) : 1.0 / std::cos(3.14159265358979 / n);  // corners outside the circle
    for (int level = 0; level < 2; ++level) {
        const double s = level ? taper : 1.0;
        for (int i = 0; i < n; ++i) {
            const double a = (i + 0.5) * 2 * 3.14159265358979 / n + (n == 4 ? 0.0 : 0.0);
            const double cx = n == 4 ? (i == 0 || i == 3 ? 1.0 : -1.0) : std::cos(a) * k;
            const double cz = n == 4 ? (i < 2 ? 1.0 : -1.0) : std::sin(a) * k;
            // A cylinder's prism is drawn out to touch its circle at the
            // middle of each side only a little: halfway between the
            // circle and the polygon round it.
            const double r = n == 4 ? 1.0 : (1.0 + 1.0 / k) * 0.5;
            const V3 local{cx * half.x * s * r, level ? half.y : -half.y, cz * half.z * s * r};
            h.v.push_back(centre + turn * local);
        }
    }
    Face bottom, top;
    for (int i = 0; i < n; ++i) bottom.vi.push_back(i), top.vi.push_back(n + i);
    h.f.push_back(bottom);
    h.f.push_back(top);
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        h.f.push_back(Face{{}, {i, j, n + j, n + i}});
    }
    h.finish();
    return h;
}

void Hull::finish() {
    V3 mid;
    for (const V3& p : v) mid += p;
    mid = mid * (1.0 / static_cast<double>(v.size()));
    for (Face& face : f) {
        // Newell's normal, then turned outward.
        V3 nn, fc;
        for (std::size_t i = 0; i < face.vi.size(); ++i) {
            const V3 p = v[static_cast<std::size_t>(face.vi[i])], q = v[static_cast<std::size_t>(face.vi[(i + 1) % face.vi.size()])];
            nn += V3{(p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y)};
            fc += p;
        }
        fc = fc * (1.0 / static_cast<double>(face.vi.size()));
        if (dot(nn, fc - mid) < 0) {
            std::reverse(face.vi.begin(), face.vi.end());
            nn = -nn;
        }
        face.n = normalize(nn);
    }
    e.clear();
    std::unordered_map<uint64_t, std::size_t> seen;
    for (std::size_t fi = 0; fi < f.size(); ++fi) {
        const auto& vi = f[fi].vi;
        for (std::size_t i = 0; i < vi.size(); ++i) {
            const int a = vi[i], b = vi[(i + 1) % vi.size()];
            const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint32_t>(std::max(a, b));
            auto it = seen.find(key);
            if (it == seen.end()) {
                seen[key] = e.size();
                e.push_back(Edge{a, b, static_cast<int>(fi), -1});
            } else {
                e[it->second].fb = static_cast<int>(fi);
            }
        }
    }
    // Volume, centre and second moment: a tetrahedron from `mid` to each
    // triangle of each face, summed (Blow and Binstock).
    volume = 0;
    V3 c;
    M3 C = zero3();
    M3 canon;
    canon.a = {2, 1, 1, 1, 2, 1, 1, 1, 2};
    canon = canon * (1.0 / 120.0);
    for (const Face& face : f)
        for (std::size_t i = 1; i + 1 < face.vi.size(); ++i) {
            const V3 a = v[static_cast<std::size_t>(face.vi[0])] - mid, b = v[static_cast<std::size_t>(face.vi[i])] - mid,
                     d = v[static_cast<std::size_t>(face.vi[i + 1])] - mid;
            M3 A;
            A.a = {a.x, b.x, d.x, a.y, b.y, d.y, a.z, b.z, d.z};
            const double det = dot(a, cross(b, d));
            volume += det / 6.0;
            c += (a + b + d) * (det / 24.0);
            C = C + A * canon * transpose(A) * det;
        }
    if (volume > 1e-15) c = c * (1.0 / volume);
    // About its own centre.
    cov = C + outer(c, c) * (-volume);
    centre = mid + c;
}

void Body::set_mass(double m) {
    mass = m;
    double vol = 0;
    V3 c;
    for (const Hull& h : hulls) vol += h.volume, c += h.centre * h.volume;
    if (m <= 0 || vol <= 1e-12) {
        inv_mass = 0;
        inv_inertia_local = zero3();
        com_local = vol > 1e-12 ? c * (1.0 / vol) : V3{};
        return;
    }
    c = c * (1.0 / vol);
    const double density = m / vol;
    M3 C = zero3();
    for (const Hull& h : hulls) C = C + (h.cov + outer(h.centre - c, h.centre - c) * h.volume) * density;
    const double tr = C(0, 0) + C(1, 1) + C(2, 2);
    M3 I = C * -1.0;
    I(0, 0) += tr, I(1, 1) += tr, I(2, 2) += tr;
    // Nothing thin spins without limit: at least a little inertia every way.
    const double least = m * 1e-5;
    for (int i = 0; i < 3; ++i) I(i, i) = std::max(I(i, i), least);
    com_local = c;
    inv_mass = 1.0 / m;
    inv_inertia_local = inverse(I);
}

void Body::place() {
    world.resize(hulls.size());
    lo = {1e18, 1e18, 1e18}, hi = {-1e18, -1e18, -1e18};
    for (std::size_t i = 0; i < hulls.size(); ++i) {
        const Hull& h = hulls[i];
        Placed& p = world[i];
        p.v.resize(h.v.size());
        p.lo = {1e18, 1e18, 1e18}, p.hi = {-1e18, -1e18, -1e18};
        for (std::size_t j = 0; j < h.v.size(); ++j) {
            const V3 q = x + r * h.v[j];
            p.v[j] = q;
            p.lo = {std::min(p.lo.x, q.x), std::min(p.lo.y, q.y), std::min(p.lo.z, q.z)};
            p.hi = {std::max(p.hi.x, q.x), std::max(p.hi.y, q.y), std::max(p.hi.z, q.z)};
        }
        p.n.resize(h.f.size());
        p.d.resize(h.f.size());
        for (std::size_t j = 0; j < h.f.size(); ++j) {
            p.n[j] = r * h.f[j].n;
            p.d[j] = dot(p.n[j], p.v[static_cast<std::size_t>(h.f[j].vi[0])]);
        }
        p.centre = x + r * h.centre;
        lo = {std::min(lo.x, p.lo.x), std::min(lo.y, p.lo.y), std::min(lo.z, p.lo.z)};
        hi = {std::max(hi.x, p.hi.x), std::max(hi.y, p.hi.y), std::max(hi.z, p.hi.z)};
    }
}

void across_of(V3 n, V3& p1, V3& p2) {
    p1 = normalize(std::fabs(n.x) > 0.57 ? V3{n.y, -n.x, 0} : V3{0, n.z, -n.y});
    p2 = cross(n, p1);
}

Body& World::add(Body b) {
    b.place();
    index_[b.id] = bodies.size();
    bodies.push_back(std::move(b));
    return bodies.back();
}

Body* World::find(const std::string& id) {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &bodies[it->second];
}

const Body* World::find(const std::string& id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &bodies[it->second];
}

void World::remove(const std::string& id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;
    const std::size_t i = it->second;
    release(id);
    unjoin(id);
    bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(i));
    manifolds_.clear();
    index_.clear();
    for (std::size_t k = 0; k < bodies.size(); ++k) index_[bodies[k].id] = k;
}

void World::clear() {
    bodies.clear();
    index_.clear();
    manifolds_.clear();
    grabs_.clear();
    joints.clear();
}

void World::teleport(Body& b, V3 x, const M3& r) {
    b.x = x;
    b.r = r;
    b.v = b.w = {};
    b.place();
    wake_near(b);
}

void World::moved(Body& b, V3 x, const M3& r) {
    const V3 lo = b.lo, hi = b.hi;
    b.x = x;
    b.r = r;
    b.place();
    wake_box({std::min(lo.x, b.lo.x), std::min(lo.y, b.lo.y), std::min(lo.z, b.lo.z)},
             {std::max(hi.x, b.hi.x), std::max(hi.y, b.hi.y), std::max(hi.z, b.hi.z)});
}

void World::wake(Body& b) {
    if (!b.dynamic()) return;
    b.awake = true;
    b.idle = 0;
}

void World::wake_box(V3 lo, V3 hi) {
    for (Body& o : bodies)
        if (o.dynamic() && !o.awake && o.lo.x <= hi.x + margin && o.hi.x >= lo.x - margin && o.lo.y <= hi.y + margin &&
            o.hi.y >= lo.y - margin && o.lo.z <= hi.z + margin && o.hi.z >= lo.z - margin)
            wake(o);
}

bool World::any_awake() const {
    for (const Body& b : bodies)
        if ((b.dynamic() && b.awake) || b.driven) return true;
    return false;
}

Joint& World::spring(const std::string& a, const std::string& b, V3 pa, V3 pb, double hertz, double damping) {
    Joint& j = join(Joint::Spring, a, b, pa, {0, 1, 0});
    const Body* B = b.empty() ? nullptr : find(b);
    j.lb = B ? transpose(B->r) * (pb - B->x) : pb;
    j.rest = length(pb - pa), j.hertz = hertz, j.damping = damping;
    return j;
}

double World::angle(const Joint& j) const {
    const Body* A = find(j.a);
    const Body* B = j.b.empty() ? nullptr : find(j.b);
    if (!A) return 0;
    const V3 axis = A->r * j.axis_a, ra = A->r * j.ref_a, rb = B ? B->r * j.ref_b : j.ref_b;
    return std::atan2(dot(cross(rb, ra), axis), dot(ra, rb));
}

void World::drive(Body& b, V3 x, const M3& r) {
    b.driven = true, b.drive_x = x, b.drive_r = r;
    const V3 lo = b.lo, hi = b.hi;
    const V3 was_x = b.x;
    const M3 was_r = b.r;
    b.x = x, b.r = r;
    b.place();
    wake_box({std::min(lo.x, b.lo.x), std::min(lo.y, b.lo.y), std::min(lo.z, b.lo.z)},
             {std::max(hi.x, b.hi.x), std::max(hi.y, b.hi.y), std::max(hi.z, b.hi.z)});
    b.x = was_x, b.r = was_r;
    b.place();
}

void World::unjoin(const std::string& id) {
    joints.erase(std::remove_if(joints.begin(), joints.end(), [&](const Joint& j) { return j.a == id || j.b == id; }), joints.end());
}

auto World::grab(const std::string& id, V3 local, V3 target, double force) -> Grab& {
    release(id);
    Grab g;
    g.id = id, g.local = local, g.target = target, g.force = force;
    grabs_.push_back(g);
    if (Body* b = find(id)) wake(*b), b->grabbed = true;
    return grabs_.back();
}

auto World::grabbing(const std::string& id) -> Grab* {
    for (Grab& g : grabs_)
        if (g.id == id) return &g;
    return nullptr;
}

void World::release(const std::string& id) {
    grabs_.erase(std::remove_if(grabs_.begin(), grabs_.end(), [&](const Grab& g) { return g.id == id; }), grabs_.end());
    if (Body* b = find(id)) b->grabbed = false, wake(*b);
}

void World::step(double dt, double time) {
    if (dt <= 0) return;
    dt = std::min(dt, 1.0 / 20.0);
    if (!any_awake()) return;
    for (Body& b : bodies)
        if (b.dynamic() && b.awake) b.place();
    stepped_ = dt;
    // The driven: as fast as takes them there this step.
    for (Body& b : bodies) {
        if (!b.driven) continue;
        b.v = ((b.drive_x + b.drive_r * b.com_local) - b.com()) * (1.0 / dt);
        b.w = log_map(b.drive_r * transpose(b.r)) * (1.0 / dt);
    }
    from_x_.resize(bodies.size());
    from_r_.resize(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) from_x_[i] = bodies[i].x, from_r_[i] = bodies[i].r;
    collide();
    const double h = dt / substeps;
    prepare(h);
    for (int s = 0; s < substeps; ++s) {
        integrate_velocities(h, time + s * h);
        warm_start();
        solve(h, true);
        integrate_positions(h);
        relax(h);
    }
    restitution();
    for (Body& b : bodies) {
        if (b.driven) {
            // Where it was sent, exactly; then itself again.
            b.x = b.drive_x, b.r = b.drive_r;
            b.place();
            b.driven = false;
            b.v = b.w = {};  // stopped where it was taken: a hand hauling it holds it
            continue;
        }
        if (b.dynamic() && b.awake) b.place();
    }
    sweep_fast();
    sleep(dt);
}

std::vector<uint64_t> World::touching() const {
    std::vector<uint64_t> out;
    for (const auto& [k, m] : manifolds_) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}

uint64_t World::pair_key(std::size_t a, std::size_t b, int ha, int hb) {
    return (static_cast<uint64_t>(a) << 40) ^ (static_cast<uint64_t>(b) << 16) ^ (static_cast<uint64_t>(ha) << 8) ^
           static_cast<uint64_t>(hb);
}

void World::collide() {
    for (auto& [k, m] : manifolds_) m.live = false;
    live_.clear();
    for (Body& b : bodies) b.contacts = 0;
    // What is inside a sensor is found again for whatever moves; what
    // lies still in one stays in it.
    const std::set<Inside> was = inside_;
    for (auto it = inside_.begin(); it != inside_.end();) {
        const Body* s = find(it->first);
        const Body* o = find(it->second);
        it = !s || !o || moves(*s) || moves(*o) ? inside_.erase(it) : std::next(it);
    }
    if (sweep && bodies.size() >= 64) indexed_pairs();
    else if (sweep) sweep_pairs();
    else every_pair();
    entered_.clear();
    left_.clear();
    for (const Inside& i : inside_)
        if (!was.count(i)) entered_.push_back(i);
    for (const Inside& i : was)
        if (!inside_.count(i)) left_.push_back(i);
    for (auto it = manifolds_.begin(); it != manifolds_.end();)
        it = it->second.live ? std::next(it) : manifolds_.erase(it);
    for (auto& [k, m] : manifolds_) {
        live_.push_back(&m);
        ++bodies[m.a].contacts, ++bodies[m.b].contacts;
    }
    // The solver meets the contacts in one order, whatever found them
    // and however the table of them grew: by the pair, then the hulls.
    std::sort(live_.begin(), live_.end(), [](const Manifold* x, const Manifold* y) {
        if (x->a != y->a) return x->a < y->a;
        if (x->b != y->b) return x->b < y->b;
        if (x->ha != y->ha) return x->ha < y->ha;
        return x->hb < y->hb;
    });
}

void World::sweep_fast() {
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        Body& b = bodies[i];
        if (!moves(b) || b.sensor || i >= from_x_.size()) continue;
        if (b.radius < 0) {
            b.radius = 0;
            for (const Hull& h : b.hulls)
                for (const V3& v : h.v) b.radius = std::max(b.radius, length(v));
        }
        const double radius = b.radius;
        const double narrow = std::min({b.hi.x - b.lo.x, b.hi.y - b.lo.y, b.hi.z - b.lo.z});
        const V3 x0 = from_x_[i], x1 = b.x;
        // Most things move a little: known at once, from where it went
        // and how fast it spins (a bound: at most as far as that).
        if (length(x1 - x0) + (length(b.w) + 1.0) * stepped_ * radius < 0.5 * narrow) continue;
        const M3 r0 = from_r_[i], r1 = b.r;
        const V3 dx = x1 - x0, turn = log_map(r1 * transpose(r0));
        const double angle = length(turn);
        const double reach = length(dx) + angle * radius;
        if (reach < 0.5 * narrow) continue;
        const V3 axis = angle > 1e-12 ? turn * (1.0 / angle) : V3{0, 1, 0};
        const auto pose_at = [&](double t) {
            b.x = x0 + dx * t;
            b.r = angle > 1e-12 ? orthonormal(axis_angle(axis, angle * t) * r0) : r0;
            b.place();
        };
        // Its way, as a box: where it was, where it is, and what its
        // turning sweeps out.
        V3 lo = b.lo, hi = b.hi;
        pose_at(0.0);
        lo = {std::min(lo.x, b.lo.x) - angle * radius, std::min(lo.y, b.lo.y) - angle * radius, std::min(lo.z, b.lo.z) - angle * radius};
        hi = {std::max(hi.x, b.hi.x) + angle * radius, std::max(hi.y, b.hi.y) + angle * radius, std::max(hi.z, b.hi.z) + angle * radius};
        const auto gap_to = [&](const Body& o) {
            double least = 1e18;
            for (std::size_t ha = 0; ha < b.hulls.size(); ++ha)
                for (std::size_t hb = 0; hb < o.hulls.size(); ++hb)
                    least = std::min(least, apart(b.hulls[ha], b.world[ha], o.hulls[hb], o.world[hb]));
            return least;
        };
        const double near = margin * 0.5;
        double first = 1.0;
        for (const Body& o : bodies) {
            if (&o == &b || moves(o) || o.sensor) continue;
            if (o.hi.x < lo.x || o.lo.x > hi.x || o.hi.y < lo.y || o.lo.y > hi.y || o.hi.z < lo.z || o.lo.z > hi.z) continue;
            double t = 0;
            pose_at(0.0);
            if (gap_to(o) < near) continue;  // touching it already: not a thing it flies into
            for (int k = 0; k < 32 && t < first; ++k) {
                pose_at(t);
                const double gap = gap_to(o);
                if (gap < near) {
                    first = t;
                    break;
                }
                t += std::max((gap - near * 0.5) / reach, 1e-4);
            }
        }
        pose_at(first);
        if (first >= 1.0) b.x = x1, b.r = r1, b.place();
    }
}

void World::pair(std::size_t ia, std::size_t ib, double reach) {
    Body& a = bodies[ia];
    Body& b = bodies[ib];
    if (a.sensor || b.sensor) {
        // Inside it, if no axis parts them at all; nothing is pushed.
        if (a.sensor && b.sensor) return;
        for (std::size_t ha = 0; ha < a.hulls.size(); ++ha)
            for (std::size_t hb = 0; hb < b.hulls.size(); ++hb)
                if (apart(a.hulls[ha], a.world[ha], b.hulls[hb], b.world[hb]) < 0) {
                    inside_.insert(a.sensor ? Inside{a.id, b.id} : Inside{b.id, a.id});
                    return;
                }
        return;
    }
    for (std::size_t ha = 0; ha < a.hulls.size(); ++ha)
        for (std::size_t hb = 0; hb < b.hulls.size(); ++hb) {
            const Body::Placed& pa = a.world[ha];
            const Body::Placed& pb = b.world[hb];
            if (pa.hi.x + reach < pb.lo.x || pb.hi.x + reach < pa.lo.x || pa.hi.y + reach < pb.lo.y || pb.hi.y + reach < pa.lo.y ||
                pa.hi.z + reach < pb.lo.z || pb.hi.z + reach < pa.lo.z)
                continue;
            V3 n;
            std::array<Touch, 4> t;
            const int k = touch(a.hulls[ha], pa, b.hulls[hb], pb, reach, n, t);
            if (k == 0) continue;
            const uint64_t key = pair_key(ia, ib, static_cast<int>(ha), static_cast<int>(hb));
            Manifold& m = manifolds_[key];
            const bool fresh = m.pts.empty() && !m.live;
            std::vector<Point> old;
            old.swap(m.pts);
            m.a = ia, m.b = ib, m.ha = static_cast<int>(ha), m.hb = static_cast<int>(hb);
            m.n = n;
            const double fa = a.hulls[ha].friction >= 0 ? a.hulls[ha].friction : a.friction;
            const double fb = b.hulls[hb].friction >= 0 ? b.hulls[hb].friction : b.friction;
            m.friction = std::sqrt(fa * fb);
            m.restitution = std::max(a.restitution, b.restitution);
            m.live = true;
            const M3 rat = transpose(a.r);
            for (int i = 0; i < k; ++i) {
                Point p;
                p.p = t[static_cast<std::size_t>(i)].p;
                p.sep = t[static_cast<std::size_t>(i)].sep;
                p.la = rat * (p.p - a.x);
                p.id = t[static_cast<std::size_t>(i)].id;
                // The same point from last step, if there was one: what
                // it was pushed with then, it is pushed with now.
                if (!fresh)
                    for (const Point& o : old)
                        if (o.id == p.id) {
                            p.pn = o.pn, p.pt1 = o.pt1, p.pt2 = o.pt2;
                            break;
                        }
                m.pts.push_back(p);
            }
            // Friction's two directions, kept steady from step to step.
            V3 t1 = std::fabs(n.y) < 0.9 ? cross(n, V3{0, 1, 0}) : cross(n, V3{1, 0, 0});
            t1 = normalize(t1);
            if (!old.empty() && length(m.t1) > 0.5) {
                // Carry last step's, turned onto this normal.
                const V3 k1 = m.t1 - n * dot(m.t1, n);
                if (length(k1) > 0.3) t1 = normalize(k1);
            }
            m.t1 = t1;
            m.t2 = cross(n, t1);
        }
}

}  // namespace sg::rigid
