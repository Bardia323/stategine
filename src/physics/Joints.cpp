#include "sg/physics/Rigid.hpp"
namespace sg::rigid {
Joint& World::join(Joint::Kind kind, const std::string& a, const std::string& b, V3 at, V3 axis) {
    Joint j;
    j.kind = kind, j.a = a, j.b = b;
    const Body* A = find(a);
    const Body* B = b.empty() ? nullptr : find(b);
    const M3 ta = A ? transpose(A->r) : M3{}, tb = B ? transpose(B->r) : M3{};
    j.la = A ? ta * (at - A->x) : at;
    j.lb = B ? tb * (at - B->x) : at;
    V3 p1, p2;
    across_of(axis, p1, p2);
    j.axis_a = ta * axis, j.axis_b = tb * axis;
    j.ref_a = ta * p1, j.ref_b = tb * p1;
    joints.push_back(j);
    if (Body* x = find(a)) wake(*x);
    if (Body* x = b.empty() ? nullptr : find(b)) wake(*x);
    return joints.back();
}

bool World::held(Joint& j, Held& h) {
    h.A = find(j.a);
    h.B = j.b.empty() ? nullptr : find(j.b);
    if (!h.A) return false;
    const bool ma = moves(*h.A), mb = h.B && moves(*h.B);
    if (!ma && !mb) return false;
    h.ma = ma ? h.A->inv_mass : 0.0, h.mb = mb ? h.B->inv_mass : 0.0;
    h.ia = ma ? h.A->inv_inertia() : zero3(), h.ib = mb ? h.B->inv_inertia() : zero3();
    h.pa = h.A->x + h.A->r * j.la;
    h.pb = h.B ? h.B->x + h.B->r * j.lb : j.lb;
    h.ra = h.pa - h.A->com();
    h.rb = h.B ? h.pb - h.B->com() : V3{};
    const int moving_now = (ma ? 1 : 0) | (mb ? 2 : 0);
    if (j.moving != moving_now) {
        // Found against something that could not move then: not handed on.
        j.point = {}, j.tilt1 = j.tilt2 = j.drive = j.pull = j.low = j.high = 0;
        j.moving = moving_now;
    }
    return true;
}

void World::push(Held& h, V3 j) {
    if (h.ma > 0) h.A->v -= j * h.ma, h.A->w -= h.ia * cross(h.ra, j);
    if (h.mb > 0) h.B->v += j * h.mb, h.B->w += h.ib * cross(h.rb, j);
}

void World::twist(Held& h, V3 t) {
    if (h.ma > 0) h.A->w -= h.ia * t;
    if (h.mb > 0) h.B->w += h.ib * t;
}

V3 World::speed_between(const Held& h) const {
    const V3 vb = h.mb > 0 ? h.B->v + cross(h.B->w, h.rb) : V3{};
    const V3 va = h.ma > 0 ? h.A->v + cross(h.A->w, h.ra) : V3{};
    return vb - va;
}

double World::soft(double hz, double zeta, double h, double& ms, double& is) {
    const double omega = 2.0 * 3.14159265358979 * hz, a1 = 2.0 * zeta + h * omega, a2 = h * omega * a1, a3 = 1.0 / (1.0 + a2);
    ms = a2 * a3, is = a3;
    return omega / a1;
}

void World::warm_joints() {
    for (Joint& j : joints) {
        Held h;
        if (!held(j, h)) continue;
        push(h, j.point);
        if (j.kind == Joint::Hinge) {
            const V3 axis = h.A->r * j.axis_a;
            V3 p1, p2;
            across_of(axis, p1, p2);
            twist(h, p1 * j.tilt1 + p2 * j.tilt2 - axis * (j.drive + j.pull + j.low - j.high));
        } else if (j.kind == Joint::Spring) {
            const V3 d = h.pb - h.pa;
            const double len = length(d);
            if (len > 1e-9) push(h, d * (j.pull / len));
        }
    }
}

void World::solve_joints(double hstep, bool springs) {
    double ms = 1, is = 0;
    const double bias_rate = springs ? soft(joint_hertz, 5.0, hstep, ms, is) : 0.0;
    if (!springs) ms = 1, is = 0;
    for (Joint& j : joints) {
        Held h;
        if (!held(j, h)) continue;
        const auto effective = [&](V3 d) {  // along d, at the points
            const V3 xa = cross(h.ra, d), xb = cross(h.rb, d);
            const double k = h.ma + h.mb + dot(xa, h.ia * xa) + dot(xb, h.ib * xb);
            return k > 1e-12 ? 1.0 / k : 0.0;
        };
        const auto turning = [&](V3 d) {  // about d
            const double k = dot(d, h.ia * d) + dot(d, h.ib * d);
            return k > 1e-12 ? 1.0 / k : 0.0;
        };
        if (j.kind == Joint::Spring) {
            const V3 d = h.pb - h.pa;
            const double len = length(d);
            if (len < 1e-9) continue;
            const V3 n = d * (1.0 / len);
            double sms = 1, sis = 0;
            const double rate = soft(j.hertz, j.damping, hstep, sms, sis);
            if (!springs) continue;  // a spring is a force, not a correction
            const double lambda = -effective(n) * sms * (dot(speed_between(h), n) + rate * (len - j.rest)) - sis * j.pull;
            j.pull += lambda;
            push(h, n * lambda);
            continue;
        }
        if (j.kind == Joint::Hinge) {
            const V3 axis = h.A->r * j.axis_a, other = h.B ? h.B->r * j.axis_b : j.axis_b;
            V3 p1, p2;
            across_of(axis, p1, p2);
            const V3 w = spin_between(h);
            // Only about the axis: the other two ways, held.
            const V3 tilt = cross(axis, other);
            for (int k = 0; k < 2; ++k) {
                const V3 d = k ? p2 : p1;
                double& acc = k ? j.tilt2 : j.tilt1;
                const double lambda = -turning(d) * ms * (dot(w, d) + bias_rate * dot(tilt, d)) - is * acc;
                acc += lambda;
                twist(h, d * lambda);
            }
            const double m_axis = turning(axis);
            const double theta = angle(j);
            // Along the axis, what turns is `a` against `b`: a twist of
            // `ax` turns `a` forward.
            const V3 ax = axis * -1.0;
            // The motor: towards its speed, with no more than its torque.
            if (j.motor) {
                const double most = j.torque * hstep;
                const double want = -m_axis * (dot(spin_between(h), ax) - j.speed);
                const double total = std::clamp(j.drive + want, -most, most);
                twist(h, ax * (total - j.drive));
                j.drive = total;
            }
            // The spring: drawn back towards its angle.
            if (j.spring && springs) {
                double sms = 1, sis = 0;
                const double rate = soft(j.hertz, j.damping, hstep, sms, sis);
                const double lambda = -m_axis * sms * (dot(spin_between(h), ax) + rate * std::remainder(theta - j.target, 2 * 3.14159265358979)) -
                                      sis * j.pull;
                j.pull += lambda;
                twist(h, ax * lambda);
            }
            // The limits: never past them (closed at once from a gap,
            // pushed out softly from within, as a contact is).
            if (j.limit) {
                for (int side = 0; side < 2; ++side) {
                    const double gap = side ? j.upper - theta : theta - j.lower;
                    const double s = side ? -1.0 : 1.0;
                    double& acc = side ? j.high : j.low;
                    double bias = 0, lms = 1, lis = 0;
                    if (gap > 0) bias = gap / hstep;
                    else if (springs) bias = std::max(bias_rate * gap, -4.0), lms = ms, lis = is;
                    const double lambda = -m_axis * lms * (s * dot(spin_between(h), ax) + bias) - lis * acc;
                    const double total = std::max(acc + lambda, 0.0);
                    twist(h, ax * (s * (total - acc)));
                    acc = total;
                }
            }
        }
        // The points held together: every way at once.
        {
            const V3 cdot = speed_between(h);
            const V3 c = h.pb - h.pa;
            const M3 xa = skew(h.ra), xb = skew(h.rb);
            const M3 k = M3{} * (h.ma + h.mb) + xa * h.ia * transpose(xa) + xb * h.ib * transpose(xb);
            const V3 imp = inverse(k) * (cdot + c * bias_rate) * -ms - j.point * is;
            j.point = j.point + imp;
            push(h, imp);
        }
    }
}

} // namespace sg::rigid
