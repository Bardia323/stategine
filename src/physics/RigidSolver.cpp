#include "sg/physics/Rigid.hpp"
namespace sg::rigid {
void World::prepare(double h) {
    (void)h;
    for (Manifold* m : live_) {
        Body& a = bodies[m->a];
        Body& b = bodies[m->b];
        const V3 ca = a.com(), cb = b.com();
        const M3 ia = moves(a) ? inertia_[m->a] : zero3(), ib = moves(b) ? inertia_[m->b] : zero3();
        const double ma = moves(a) ? a.inv_mass : 0.0, mb = moves(b) ? b.inv_mass : 0.0;
        // Impulses found while one of them slept were found against a
        // thing that could not move - a wall, as far as the other knew:
        // woken, it must not be handed them (a marker on a tray the
        // board was pulled from under would be flung off). Nor the
        // other way. Whatever moves now starts from nothing.
        const int moving = (moves(a) ? 1 : 0) | (moves(b) ? 2 : 0);
        if (m->moving != moving) {
            for (Point& p : m->pts) p.pn = p.pt1 = p.pt2 = 0;
            m->moving = moving;
        }
        const M3 rbt = transpose(b.r);
        for (Point& p : m->pts) {
            p.ra = p.p - ca;
            p.rb = p.p - cb;
            p.lb = rbt * (p.p - b.x);
            const auto eff = [&](V3 d) {
                const V3 xa = cross(p.ra, d), xb = cross(p.rb, d);
                const double k = ma + mb + dot(xa, ia * xa) + dot(xb, ib * xb);
                return k > 1e-12 ? 1.0 / k : 0.0;
            };
            p.mn = eff(m->n), p.mt1 = eff(m->t1), p.mt2 = eff(m->t2);
            const V3 dv = (b.v + cross(b.w, p.rb)) - (a.v + cross(a.w, p.ra));
            p.vn0 = dot(dv, m->n);
            p.most = 0;
        }
    }
    for (Grab& g : grabs_) g.impulse = {}, g.spin = {};
}

void World::sample_fields(double time) {
    std::vector<field::Source> sources=fields;
    for(std::size_t i=0;i<bodies.size();++i) {
        const Body& b=bodies[i];
        for(auto s:b.fields) {
            s.pose=spatial::Transform{b.r,b.x}*s.pose; s.emitter=i;
            sources.push_back(std::move(s));
        }
    }
    field_solver_.rebuild(std::move(sources));
    responses_.resize(bodies.size());
    const std::vector<field::Receiver>* previous=nullptr;
    std::size_t last=0;
    for(std::size_t i=0;i<bodies.size();++i) {
        const auto& b=bodies[i]; if(!moves(b)) continue;
        // Position-independent fields share a result for equal response data.
        if(field_solver_.uniform() && previous && *previous==b.receives) responses_[i]=responses_[last];
        else responses_[i]=field_solver_.evaluate(b.com(),time,b.receives,i);
        previous=&b.receives; last=i;
    }
}

void World::integrate_velocities(double h, double time) {
    sample_fields(time);
    for (std::size_t i=0;i<bodies.size();++i) {
        Body& b=bodies[i];
        if (!moves(b)) continue;
        const auto& response=responses_[i];
        b.v += (response.acceleration + response.force * b.inv_mass) * h;
        if(dot(response.torque,response.torque)>0) b.w += (b.inv_inertia() * response.torque) * h;
        // Air, and the losses that bring everything to rest in the end.
        b.v = b.v * (1.0 / (1.0 + 0.02 * h));
        // Rolling and twisting lose more where it rests on something: a
        // mug on its side rolls to a stop.
        b.w = b.w * (1.0 / (1.0 + (b.contacts ? 0.1 : 0.05) * h));
        // No spin so fast a step turns it past what can be followed.
        const double wl = length(b.w);
        if (wl > 60) b.w = b.w * (60 / wl);
    }
}

void World::refresh_inertia() {
    inertia_.resize(bodies.size());
    for (std::size_t i = 0; i < bodies.size(); ++i)
        if (moves(bodies[i])) inertia_[i] = bodies[i].inv_inertia();
}

void World::apply(std::size_t ia, std::size_t ib, V3 ra, V3 rb, V3 j) {
    Body& a = bodies[ia];
    Body& b = bodies[ib];
    if (moves(a)) {
        a.v -= j * a.inv_mass;
        a.w -= inertia_[ia] * cross(ra, j);
    }
    if (moves(b)) {
        b.v += j * b.inv_mass;
        b.w += inertia_[ib] * cross(rb, j);
    }
}

void World::warm_start() {
    for (Grab& g : grabs_) g.impulse = {}, g.spin = {};
    warm_joints();
    for (Manifold* m : live_) {
        Body& a = bodies[m->a];
        Body& b = bodies[m->b];
        for (Point& p : m->pts) apply(m->a, m->b, p.ra, p.rb, m->n * p.pn + m->t1 * p.pt1 + m->t2 * p.pt2);
    }
}

double World::separation(const Manifold& m, const Point& p) const {
    const Body& a = bodies[m.a];
    const Body& b = bodies[m.b];
    const V3 pa = a.x + a.r * p.la, pb = b.x + b.r * p.lb;
    return p.sep + dot(pb - pa, m.n);
}

void World::solve(double h, bool springs) {
    // Soft contact: a spring of `contact_hertz`, damped ten times over.
    const double zeta = 10.0, omega = 2.0 * 3.14159265358979 * contact_hertz;
    const double a1 = 2.0 * zeta + h * omega, a2 = h * omega * a1, a3 = 1.0 / (1.0 + a2);
    const double bias_rate = omega / a1, mass_scale = a2 * a3, impulse_scale = a3;
    for (int it = 0; it < iterations; ++it) {
        // The hand is a spring itself: pulled with it, not relaxed.
        if (springs) solve_grabs(h);
        solve_joints(h, springs);
        for (Manifold* m : live_) {
            Body& a = bodies[m->a];
            Body& b = bodies[m->b];
            double total = 0;
            for (Point& p : m->pts) {
                // (Nothing has moved since the first pass: the gap is the same.)
                if (it == 0) p.gap = separation(*m, p);
                const double s = p.gap;
                double bias = 0, ms = 1, is = 0;
                if (s > 0) {
                    bias = s / h;  // a gap: it may be closed, no faster
                } else if (springs) {
                    bias = std::max(bias_rate * std::min(0.0, s + slop), -max_push);
                    ms = mass_scale, is = impulse_scale;
                }
                const V3 dv = (b.v + cross(b.w, p.rb)) - (a.v + cross(a.w, p.ra));
                const double vn = dot(dv, m->n);
                double j = -p.mn * ms * (vn + bias) - is * p.pn;
                const double pn = std::max(p.pn + j, 0.0);
                j = pn - p.pn;
                p.pn = pn;
                p.most = std::max(p.most, pn);
                total += pn;
                apply(m->a, m->b, p.ra, p.rb, m->n * j);
            }
            for (Point& p : m->pts) {
                const double limit = m->friction * p.pn;
                const V3 dv = (b.v + cross(b.w, p.rb)) - (a.v + cross(a.w, p.ra));
                const double v1 = dot(dv, m->t1), v2 = dot(dv, m->t2);
                double n1 = p.pt1 - p.mt1 * v1, n2 = p.pt2 - p.mt2 * v2;
                // Friction is a disc, not a square.
                const double l = std::sqrt(n1 * n1 + n2 * n2);
                if (l > limit && l > 0) n1 *= limit / l, n2 *= limit / l;
                const double j1 = n1 - p.pt1, j2 = n2 - p.pt2;
                p.pt1 = n1, p.pt2 = n2;
                apply(m->a, m->b, p.ra, p.rb, m->t1 * j1 + m->t2 * j2);
            }
            (void)total;
        }
    }
}

void World::solve_grabs(double h) {
    const bool springs = true;
    for (Grab& g : grabs_) {
        Body* b = find(g.id);
        if (!b || !b->dynamic()) continue;
        const double omega = 2.0 * 3.14159265358979 * g.hertz;
        const double a1 = 2.0 * g.damping + h * omega, a2 = h * omega * a1, a3 = 1.0 / (1.0 + a2);
        const double bias_rate = springs ? omega / a1 : 0.0, ms = springs ? a2 * a3 : 1.0, is = springs ? a3 : 0.0;
        const M3 ii = b->inv_inertia();
        // The point: pulled to the target.
        const V3 r = b->r * (g.local - b->com_local);
        const V3 p = b->com() + r;
        const V3 c = p - g.target;
        const V3 cdot = b->v + cross(b->w, r);
        const M3 rx = skew(r);
        M3 k = M3{} * b->inv_mass + rx * ii * transpose(rx);
        const M3 kinv = inverse(k);
        V3 j = kinv * (cdot + c * bias_rate) * -ms - g.impulse * is;
        V3 total = g.impulse + j;
        const double most = g.force * h;
        if (length(total) > most) total = total * (most / length(total));
        j = total - g.impulse;
        g.impulse = total;
        b->v += j * b->inv_mass;
        b->w += ii * cross(r, j);
        if (g.turns) {
            // And turned: the turn still to make, undone by a spring.
            const V3 err = log_map(g.turn * transpose(b->r)) * -1.0;
            const M3 kinv2 = inverse(ii);
            V3 t = kinv2 * (b->w + err * bias_rate) * -ms - g.spin * is;
            V3 tt = g.spin + t;
            const double most_t = g.torque * h;
            if (length(tt) > most_t) tt = tt * (most_t / length(tt));
            t = tt - g.spin;
            g.spin = tt;
            b->w += ii * t;
        }
    }
}

void World::integrate_positions(double h) {
    for (Body& b : bodies) {
        if (!moves(b) && !b.driven) continue;
        const V3 c = b.com() + b.v * h;
        const double wl = length(b.w);
        if (wl > 1e-12) b.r = orthonormal(axis_angle(b.w * (1.0 / wl), wl * h) * b.r);
        b.x = c - b.r * b.com_local;
    }
}

void World::restitution() {
    for (Manifold* m : live_) {
        Body& a = bodies[m->a];
        Body& b = bodies[m->b];
        for (Point& p : m->pts) {
            // How hard it struck: for whoever makes the sound.
            if (p.vn0 < -0.4 && p.most > 0) {
                if (moves(a)) a.hit = std::max(a.hit, -p.vn0);
                if (moves(b)) b.hit = std::max(b.hit, -p.vn0);
            }
            if (m->restitution <= 0 || p.vn0 > -1.0 || p.most <= 0) continue;
            const V3 dv = (b.v + cross(b.w, p.rb)) - (a.v + cross(a.w, p.ra));
            const double vn = dot(dv, m->n);
            double j = -p.mn * (vn + m->restitution * p.vn0);
            const double pn = std::max(p.pn + j, 0.0);
            j = pn - p.pn;
            p.pn = pn;
            apply(m->a, m->b, p.ra, p.rb, m->n * j);
        }
    }
}

void World::sleep(double dt) {
    std::vector<std::size_t> parent(bodies.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    const auto root = [&](std::size_t i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    for (Manifold* m : live_) {
        const Body& a = bodies[m->a];
        const Body& b = bodies[m->b];
        if (!a.dynamic() || !b.dynamic()) continue;
        bool touching = false;
        for (const Point& p : m->pts) touching |= p.pn > 0 || p.sep < slop;
        if (!touching) continue;
        // Something sleeping that something awake is on or against: awake.
        if (a.awake != b.awake) {
            const Body& moving = a.awake ? a : b;
            if (length(moving.v) > 0.05 || length(moving.w) > 0.2 || moving.grabbed) wake(bodies[a.awake ? m->b : m->a]);
        }
        parent[root(m->a)] = root(m->b);
    }
    // Joined things are one island: a door and its frame, a chain.
    for (std::size_t k = 0; k < joints.size(); ++k) {
        const Joint& j = joints[k];
        const int ia = joint_at_[k][0], ib = joint_at_[k][1];
        if (ia < 0) continue;
        Body* A = &bodies[static_cast<std::size_t>(ia)];
        Body* B = ib < 0 ? nullptr : &bodies[static_cast<std::size_t>(ib)];
        // A motor driving it, or a spring not yet where it draws it: it
        // is still on its way, however slowly (a door closing the last
        // degree), and does not sleep.
        if (j.kind == Joint::Hinge && ((j.motor && j.speed != 0.0) ||
                                       (j.spring && std::fabs(std::remainder(angle_between(j, A, B) - j.target, 2 * 3.14159265358979)) > 0.002))) {
            if (A->dynamic()) wake(*A);
            if (B && B->dynamic()) wake(*B);
        }
        if (!B || !A->dynamic() || !B->dynamic()) continue;
        if (A->awake != B->awake) wake(A->awake ? *B : *A);
        parent[root(static_cast<std::size_t>(ia))] = root(static_cast<std::size_t>(ib));
    }
    std::unordered_map<std::size_t, double> least;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        Body& b = bodies[i];
        if (!moves(b)) continue;
        const bool still = length(b.v) < 0.04 && length(b.w) < 0.12 && !b.grabbed;
        b.idle = still ? b.idle + dt : 0.0;
        const std::size_t r = root(i);
        auto it = least.find(r);
        least[r] = it == least.end() ? b.idle : std::min(it->second, b.idle);
    }
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        Body& b = bodies[i];
        if (!moves(b)) continue;
        if (least[root(i)] >= sleep_after) {
            b.awake = false;
            b.v = b.w = {};
        }
    }
}

} // namespace sg::rigid
