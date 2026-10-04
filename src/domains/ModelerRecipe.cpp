// The recipe language: read a line at a time, built into a solid, made into a
// mesh. See sg/domains/Modeler.hpp for the language.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "ModelerKernel.hpp"
#include "ModelerLibrary.hpp"
#include "sg/domains/Modeler.hpp"

namespace sg::sculpt {

using namespace kernel;

namespace {

constexpr double kPi = 3.14159265358979323846;

// --- numbers as expressions ----------------------------------------------------------

class Expr {
public:
    explicit Expr(const std::string& s) : s_(s) {}
    bool run(double& v) {
        v = sum();
        return ok_ && i_ == s_.size();
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;
    bool ok_ = true;
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    double sum() {
        double v = prod();
        while (peek() == '+' || peek() == '-') {
            const char c = s_[i_++];
            const double r = prod();
            v = c == '+' ? v + r : v - r;
        }
        return v;
    }
    double prod() {
        double v = unary();
        while (peek() == '*' || peek() == '/') {
            const char c = s_[i_++];
            const double r = unary();
            v = c == '*' ? v * r : (r != 0 ? v / r : 0);
        }
        return v;
    }
    double unary() {
        if (peek() == '-') return ++i_, -unary();
        if (peek() == '+') return ++i_, unary();
        const double b = atom();
        if (peek() == '^') return ++i_, std::pow(b, unary());
        return b;
    }
    double atom() {
        if (peek() == '(') {
            ++i_;
            const double v = sum();
            if (peek() == ')') ++i_;
            else ok_ = false;
            return v;
        }
        if (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.') {
            char* end = nullptr;
            const double v = std::strtod(s_.c_str() + i_, &end);
            const std::size_t n = std::size_t(end - (s_.c_str() + i_));
            if (n == 0) return ok_ = false, 0;
            i_ += n;
            return v;
        }
        if (std::isalpha(static_cast<unsigned char>(peek()))) {
            std::string name;
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') name += s_[i_++];
            if (peek() != '(') return name == "pi" ? kPi : (ok_ = false, 0);
            ++i_;
            std::vector<double> a;
            while (ok_) {
                a.push_back(sum());
                if (peek() == ',') ++i_;
                else break;
            }
            if (peek() == ')') ++i_;
            else ok_ = false;
            const double x = a.empty() ? 0 : a[0], y = a.size() > 1 ? a[1] : 0;
            const double r = kPi / 180;
            if (name == "sin") return std::sin(x * r);
            if (name == "cos") return std::cos(x * r);
            if (name == "tan") return std::tan(x * r);
            if (name == "sqrt") return std::sqrt(std::max(0.0, x));
            if (name == "abs") return std::abs(x);
            if (name == "floor") return std::floor(x + 1e-9);
            if (name == "ceil") return std::ceil(x - 1e-9);
            if (name == "round") return std::floor(x + 0.5);
            if (name == "min") return std::min(x, y);
            if (name == "max") return std::max(x, y);
            if (name == "pow") return std::pow(x, y);
            return ok_ = false, 0;
        }
        return ok_ = false, 0;
    }
};

bool number(const std::string& s, double& v) { return !s.empty() && Expr(s).run(v); }

std::string fmt(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}

// The pieces of a token between commas outside any brackets.
std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : s) {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) out.push_back(cur), cur.clear();
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> tokens(const std::string& line) {
    std::string s = line.substr(0, line.find('#'));
    std::vector<std::string> t;
    std::istringstream in(s);
    std::string w;
    while (in >> w) t.push_back(w);
    return t;
}

struct Args {
    std::vector<std::string> pos;
    std::map<std::string, std::string> opt;
};

const std::set<std::string>& openers() {
    static const std::set<std::string> s{"define", "for", "group", "array", "radial", "mirror"};
    return s;
}
const std::set<std::string>& shapes_known() {
    static const std::set<std::string> s{"box", "cyl", "cylinder", "cone", "sphere", "torus", "capsule", "lathe", "extrude", "prism", "loft", "tube", "sweep", "import"};
    return s;
}

struct Defaults {
    double res = 0;
    int sides = 24;
    double crease = 40;
    int mat = 0;
};

struct Macro {
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<std::string> body;
};

struct Frame {
    enum Kind { Root, Group, Array, Radial, Mirror, Macro } kind = Root;
    Solid acc;
    Mode mode = Mode::Add;
    double k = 0;
    Args args;
    std::map<std::string, std::string> vars;
    Defaults def;
    std::string name;
};

struct Made {
    SdfP field;
    std::shared_ptr<Geom> exact;
    Mat pre;
    std::vector<int> face_mats;
};

class Interp {
public:
    Interp(const Options& o, const Files* f) : o_(o), files_(f) {
        Frame root;
        root.def.sides = o.sides;
        root.def.crease = o.crease;
        st_.push_back(std::move(root));
        mats_.push_back("");
    }

    void exec(const std::vector<std::string>& lines, std::size_t floor, int first_line);
    Solid result() { return st_.front().acc; }
    std::string errors;
    std::vector<std::string> imports;
    std::vector<std::string> mats_;
    std::set<std::string> defined_by_library;

private:
    Options o_;
    const Files* files_;
    std::vector<Frame> st_;
    std::map<std::string, Macro> macros_;
    std::map<std::string, Solid> named_;
    int line_ = 0, depth_ = 0;

    void err(const std::string& m) { errors += "line " + std::to_string(line_) + ": " + m + "\n"; }
    int mat_id(const std::string& n) {
        if (n.empty()) return 0;
        for (std::size_t i = 0; i < mats_.size(); ++i)
            if (mats_[i] == n) return int(i);
        mats_.push_back(n);
        return int(mats_.size()) - 1;
    }
    bool lookup(const std::string& name, std::string& v) const {
        for (auto f = st_.rbegin(); f != st_.rend(); ++f) {
            auto it = f->vars.find(name);
            if (it != f->vars.end()) return v = it->second, true;
        }
        return false;
    }
    std::string subst(const std::string& tok) {
        std::string out;
        for (std::size_t i = 0; i < tok.size(); ++i) {
            if (tok[i] != '$') {
                out += tok[i];
                continue;
            }
            std::size_t j = i + 1;
            while (j < tok.size() && (std::isalnum(static_cast<unsigned char>(tok[j])) || tok[j] == '_')) ++j;
            const std::string name = tok.substr(i + 1, j - i - 1);
            std::string v;
            if (!lookup(name, v)) err("no variable $" + name), v = "0";
            double d;
            out += number(v, d) ? "(" + v + ")" : v;
            i = j - 1;
        }
        return out;
    }
    double num(const std::string& s, double fallback) {
        double v;
        if (number(s, v)) return v;
        if (!s.empty()) err("not a number: " + s);
        return fallback;
    }
    std::vector<double> nums(const std::string& s) {
        std::vector<double> v;
        for (const std::string& p : split_commas(s)) {
            double d;
            if (!number(p, d)) return {};
            v.push_back(d);
        }
        return v;
    }
    double opt_num(const Args& a, const char* k, double fallback) {
        auto it = a.opt.find(k);
        return it == a.opt.end() ? fallback : num(it->second, fallback);
    }
    // at, rot, scale: where it goes.
    Mat place(const Args& a) {
        V3 at, sc{1, 1, 1};
        double rx = 0, ry = 0, rz = 0;
        if (auto it = a.opt.find("at"); it != a.opt.end()) {
            const auto v = nums(it->second);
            if (v.size() == 3) at = {v[0], v[1], v[2]};
            else err("at= takes x,y,z");
        }
        if (auto it = a.opt.find("rot"); it != a.opt.end()) {
            const auto v = nums(it->second);
            if (v.size() == 1) ry = v[0];
            else if (v.size() == 3) rx = v[0], ry = v[1], rz = v[2];
            else err("rot= takes a, or x,y,z degrees");
        }
        if (auto it = a.opt.find("scale"); it != a.opt.end()) {
            const auto v = nums(it->second);
            if (v.size() == 1) sc = {v[0], v[0], v[0]};
            else if (v.size() == 3) sc = {v[0], v[1], v[2]};
            else err("scale= takes s, or x,y,z");
        }
        return translate(at) * rotation(rx, ry, rz) * scaling(sc);
    }
    std::vector<P2> pairs(const std::vector<std::string>& t, std::size_t from, std::size_t to) {
        std::vector<P2> out;
        for (std::size_t i = from; i < to; ++i) {
            const auto v = nums(t[i]);
            if (v.size() != 2) {
                err("expected x,y but got " + t[i]);
                continue;
            }
            out.push_back({v[0], v[1]});
        }
        return out;
    }
    std::vector<V3> triples(const std::vector<std::string>& t, std::size_t from) {
        std::vector<V3> out;
        for (std::size_t i = from; i < t.size(); ++i) {
            const auto v = nums(t[i]);
            if (v.size() != 3) {
                err("expected x,y,z but got " + t[i]);
                continue;
            }
            out.push_back({v[0], v[1], v[2]});
        }
        return out;
    }

    bool make(const std::string& head, Args& a, const Defaults& d, Made& m);
    Solid solid(const Made& m, const Args& a, const Defaults& d);
    void parse_args(const std::vector<std::string>& t, std::size_t from, Args& a);
    void open(const std::string& head, Args& a, Mode mode, double k);
    void close();
    Solid finish(Frame& f);
    void join_top(const Solid& s, Mode mode, double k) { kernel::join(st_.back().acc, s, mode, k); }
    void call(const std::string& name, Args& a, Mode mode, double k);
    void collect(const std::vector<std::string>& lines, std::size_t& i, std::vector<std::string>& body);
};

void Interp::parse_args(const std::vector<std::string>& t, std::size_t from, Args& a) {
    for (std::size_t i = from; i < t.size(); ++i) {
        const std::size_t eq = t[i].find('=');
        const bool word = eq != std::string::npos && eq > 0 && std::isalpha(static_cast<unsigned char>(t[i][0])) && t[i].find_first_of("(,") > eq;
        if (word) a.opt[t[i].substr(0, eq)] = t[i].substr(eq + 1);
        else a.pos.push_back(t[i]);
    }
}

void Interp::collect(const std::vector<std::string>& lines, std::size_t& i, std::vector<std::string>& body) {
    int depth = 1;
    for (++i; i < lines.size(); ++i) {
        const auto t = tokens(lines[i]);
        if (!t.empty()) {
            std::size_t h = 0;
            if (t[0] == "add" || t[0] == "sub" || t[0] == "and" || t[0].rfind("blend=", 0) == 0 || t[0].rfind("carve=", 0) == 0) h = 1;
            if (h < t.size()) {
                if (openers().count(t[h])) ++depth;
                else if (t[h] == "end" && --depth == 0) return;
            }
        }
        body.push_back(lines[i]);
    }
    err("a block was never closed with `end`");
}

bool Interp::make(const std::string& head, Args& a, const Defaults& d, Made& m) {
    const int sides = int(opt_num(a, "sides", d.sides));
    const double round = opt_num(a, "round", 0), chamfer = opt_num(a, "chamfer", 0);
    const bool centre = opt_num(a, "centre", 0) != 0;
    std::vector<double> p;
    for (const std::string& s : a.pos) {
        double v;
        if (number(s, v)) p.push_back(v);
    }
    const auto need = [&](std::size_t n) {
        if (p.size() < n) return err(head + " needs " + std::to_string(n) + " numbers"), false;
        return true;
    };
    double lift = 0;
    if (head == "box") {
        if (!need(1)) return false;
        const V3 s = p.size() >= 3 ? V3{p[0], p[1], p[2]} : p.size() == 2 ? V3{p[0], p[1], p[1]} : V3{p[0], p[0], p[0]};
        m.field = sdf_box(s * 0.5, round);
        if (round <= 0) m.exact = std::make_shared<Geom>(g_box(s));
        lift = s.y / 2;
    } else if (head == "cyl" || head == "cylinder") {
        if (!need(2)) return false;
        m.field = sdf_cyl(p[0], p[1] / 2, round);
        if (round <= 0) m.exact = std::make_shared<Geom>(g_lathe({{p[0], -p[1] / 2}, {p[0], p[1] / 2}}, sides));
        lift = p[1] / 2;
    } else if (head == "cone") {
        if (!need(2)) return false;
        const double r1 = p[0], r2 = p.size() >= 3 ? p[1] : 0, h = p.size() >= 3 ? p[2] : p[1];
        m.field = sdf_cone(r1, r2, h / 2);
        m.exact = std::make_shared<Geom>(g_lathe({{r1, -h / 2}, {r2, h / 2}}, sides));
        lift = h / 2;
    } else if (head == "sphere" || head == "capsule") {
        if (!need(1)) return false;
        const double r = p[0];
        const double a2 = head == "capsule" && p.size() > 1 ? std::max(0.0, p[1] / 2 - r) : 0;
        const int n = std::max(4, sides / 4) * 2;
        std::vector<P2> prof;
        for (int i = 0; i <= n; ++i) {
            const double phi = kPi * i / n;
            prof.push_back({r * std::sin(phi), (i <= n / 2 ? -a2 : a2) - r * std::cos(phi)});
        }
        if (a2 > 0) m.field = sdf_capsule(r, a2);
        else m.field = sdf_sphere(r);
        m.exact = std::make_shared<Geom>(g_lathe(prof, sides));
        lift = a2 + r;
    } else if (head == "torus") {
        if (!need(2)) return false;
        m.field = sdf_torus(p[0], p[1]);
        m.exact = std::make_shared<Geom>(g_torus(p[0], p[1], sides, std::max(8, sides / 2)));
        lift = p[1];
    } else if (head == "lathe") {
        const auto prof = pairs(a.pos, 0, a.pos.size());
        if (prof.size() < 2) return err("lathe needs a profile of r,y points"), false;
        m.field = sdf_lathe(prof);
        m.exact = std::make_shared<Geom>(g_lathe(prof, sides));
    } else if (head == "extrude" || head == "prism") {
        if (a.pos.size() < 4) return err(head + " needs a depth and three or more points"), false;
        const double depth = num(a.pos[0], 1);
        auto pts = pairs(a.pos, 1, a.pos.size());
        if (pts.size() < 3) return false;
        if (head == "prism") {
            for (P2& q : pts) q.y = -q.y;
            m.pre = basis({0, depth / 2, 0}, {1, 0, 0}, {0, 0, -1}, {0, 1, 0});
        }
        m.field = sdf_extrude(pts, depth / 2, chamfer);
        m.exact = std::make_shared<Geom>(g_extrude(pts, depth, chamfer));
    } else if (head == "loft") {
        if (a.pos.size() < 2) return err("loft needs a height and rings"), false;
        const double h = num(a.pos[0], 1);
        std::vector<std::vector<P2>> rings(1);
        for (std::size_t i = 1; i < a.pos.size(); ++i) {
            if (a.pos[i] == "/") {
                rings.emplace_back();
                continue;
            }
            const auto v = nums(a.pos[i]);
            if (v.size() == 2) rings.back().push_back({v[0], v[1]});
        }
        bool ok = rings.size() >= 2 && rings[0].size() >= 3;
        for (const auto& r : rings) ok = ok && r.size() == rings[0].size();
        if (!ok) return err("loft wants two or more rings of the same number of x,z points, between /"), false;
        m.field = sdf_loft(rings, h);
        m.exact = std::make_shared<Geom>(g_loft(rings, h));
    } else if (head == "tube") {
        if (a.pos.size() < 3) return err("tube needs a radius and a path"), false;
        const double r = num(a.pos[0], 0.1);
        const auto path = triples(a.pos, 1);
        if (path.size() < 2) return false;
        std::vector<P2> prof;
        for (int i = 0; i < sides; ++i) prof.push_back({r * std::cos(2 * kPi * i / sides), r * std::sin(2 * kPi * i / sides)});
        m.field = sdf_tube(path, r);
        m.exact = std::make_shared<Geom>(g_sweep(prof, path, true));
    } else if (head == "sweep") {
        const auto slash = std::find(a.pos.begin(), a.pos.end(), std::string("/"));
        if (slash == a.pos.end()) return err("sweep needs outline / path"), false;
        const std::size_t cut = std::size_t(slash - a.pos.begin());
        const auto prof = pairs(a.pos, 0, cut);
        const auto path = triples(std::vector<std::string>(a.pos.begin() + long(cut) + 1, a.pos.end()), 0);
        if (prof.size() < 3 || path.size() < 2) return err("sweep needs an outline of 3+ points and a path of 2+"), false;
        m.field = sdf_sweep(prof, path);
        m.exact = std::make_shared<Geom>(g_sweep(prof, path, true));
    } else if (head == "import") {
        if (a.pos.empty() || !files_ || !files_->read) return err("import needs a file and the program's files"), false;
        std::string text;
        if (!files_->read(a.pos[0], text)) return err("cannot read " + a.pos[0]), false;
        imports.push_back(a.pos[0]);
        Obj ob = read_obj(text);
        if (!ob.ok) return err("nothing to read in " + a.pos[0]), false;
        auto g = std::make_shared<Geom>(std::move(ob.geom));
        const bool keep = opt_num(a, "mats", 0) != 0;
        for (int& f : g->mat) f = keep ? mat_id(ob.materials[std::size_t(f)]) : d.mat;
        Box bb;
        for (const V3& q : g->p) bb.grow(q);
        // fit=h scales it to that height; centre=1 puts its middle on the origin; base=1 its foot there
        double s = 1;
        const double fit = opt_num(a, "fit", 0);
        if (fit > 0 && bb.hi.y > bb.lo.y) s = fit / (bb.hi.y - bb.lo.y);
        const V3 mid = (bb.lo + bb.hi) * 0.5;
        V3 shift{0, 0, 0};
        if (opt_num(a, "base", 0) != 0 || centre) shift = {-mid.x, centre ? -mid.y : -bb.lo.y, -mid.z};
        m.pre = scaling({s, s, s}) * translate(shift);
        const Box nb = bb;
        m.field = sdf_xform(sdf_box((nb.hi - nb.lo) * 0.5, 0), translate(mid));
        m.exact = g;
        return true;
    } else {
        return false;
    }
    if (m.pre.t.x == 0 && m.pre.t.y == 0 && m.pre.t.z == 0 && m.pre.m[4] == 1 && !centre && lift != 0) m.pre = translate({0, lift, 0});
    return true;
}

Solid Interp::solid(const Made& m, const Args& a, const Defaults& d) {
    Solid s;
    Piece p;
    int mat = d.mat;
    if (auto it = a.opt.find("mat"); it != a.opt.end()) mat = mat_id(it->second);
    const Mat full = place(a) * m.pre;
    SdfP f = m.field;
    if (mat) f = with_material(f, mat);
    p.field = sdf_xform(f, full);
    p.bb = p.field->bb;
    p.material = mat;
    p.res = opt_num(a, "res", d.res);
    p.crease = opt_num(a, "crease", d.crease);
    if (m.exact) {
        auto g = std::make_shared<Geom>(*m.exact);
        g->crease = p.crease;
        transform(*g, full);
        p.exact = g;
    }
    s.pieces.push_back(std::move(p));
    return s;
}

void Interp::open(const std::string& head, Args& a, Mode mode, double k) {
    Frame f;
    f.kind = head == "array" ? Frame::Array : head == "radial" ? Frame::Radial : head == "mirror" ? Frame::Mirror : Frame::Group;
    f.mode = mode, f.k = k, f.args = a;
    f.def = st_.back().def;
    if (auto it = a.opt.find("mat"); it != a.opt.end()) f.def.mat = mat_id(it->second);
    if (auto it = a.opt.find("res"); it != a.opt.end()) f.def.res = num(it->second, 0);
    if (auto it = a.opt.find("sides"); it != a.opt.end()) f.def.sides = int(num(it->second, 24));
    if (auto it = a.opt.find("crease"); it != a.opt.end()) f.def.crease = num(it->second, 40);
    st_.push_back(std::move(f));
}

Solid Interp::finish(Frame& f) {
    Solid s = std::move(f.acc);
    const Args& a = f.args;
    if (f.kind == Frame::Array) {
        const int n = std::max(1, int(opt_num(a, "n", 2)));
        V3 step;
        if (auto it = a.opt.find("step"); it != a.opt.end()) {
            const auto v = nums(it->second);
            if (v.size() == 3) step = {v[0], v[1], v[2]};
            else err("step= takes x,y,z");
        }
        const double turn = opt_num(a, "turn", 0);
        Solid all;
        for (int i = 0; i < n; ++i) all = unite(all, moved(s, translate(step * double(i)) * rotation(0, turn * i, 0)));
        s = std::move(all);
    } else if (f.kind == Frame::Radial) {
        const int n = std::max(1, int(opt_num(a, "n", 4)));
        const double arc = opt_num(a, "arc", 360);
        const std::string axis = a.opt.count("axis") ? a.opt.at("axis") : "y";
        Solid all;
        for (int i = 0; i < n; ++i) {
            const double ang = arc * i / n;
            all = unite(all, moved(s, axis == "x" ? rotation(ang, 0, 0) : axis == "z" ? rotation(0, 0, ang) : rotation(0, ang, 0)));
        }
        s = std::move(all);
    } else if (f.kind == Frame::Mirror) {
        const std::string axis = !a.pos.empty() ? a.pos[0] : a.opt.count("axis") ? a.opt.at("axis") : "x";
        const V3 sc = axis == "y" ? V3{1, -1, 1} : axis == "z" ? V3{1, 1, -1} : V3{-1, 1, 1};
        s = unite(s, moved(s, scaling(sc)));
    }
    if (!f.name.empty()) named_[f.name] = s;
    return moved(s, place(a));
}

void Interp::close() {
    Frame f = std::move(st_.back());
    st_.pop_back();
    const Solid s = finish(f);
    join_top(s, f.mode, f.k);
}

void Interp::call(const std::string& name, Args& a, Mode mode, double k) {
    const Macro mac = macros_[name];
    if (depth_ > 24) return err("macros nest too deep at " + name);
    Frame f;
    f.kind = Frame::Macro;
    f.mode = mode, f.k = k, f.args = a;
    f.def = st_.back().def;
    if (auto it = a.opt.find("mat"); it != a.opt.end()) f.def.mat = mat_id(it->second);
    if (auto it = a.opt.find("res"); it != a.opt.end()) f.def.res = num(it->second, 0);
    st_.push_back(std::move(f));
    for (std::size_t i = 0; i < mac.params.size(); ++i) {
        const auto& [pn, pd] = mac.params[i];
        std::string v;
        if (i < a.pos.size()) v = a.pos[i];
        else if (auto it = a.opt.find(pn); it != a.opt.end()) v = it->second;
        else v = subst(pd);
        double d;
        st_.back().vars[pn] = number(v, d) ? fmt(d) : v;
    }
    const std::size_t floor = st_.size();
    ++depth_;
    exec(mac.body, floor, line_);
    --depth_;
    while (st_.size() > floor) err("a block in " + name + " was left open"), close();
    close();
}

void Interp::exec(const std::vector<std::string>& lines, std::size_t floor, int first_line) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (first_line > 0 && depth_ == 0) line_ = first_line + int(i);
        auto t = tokens(lines[i]);
        if (t.empty()) continue;
        std::size_t h = 0;
        Mode mode = Mode::Add;
        double k = 0;
        if (t[0] == "sub") mode = Mode::Sub, h = 1;
        else if (t[0] == "and") mode = Mode::And, h = 1;
        else if (t[0] == "add") h = 1;
        else if (t[0].rfind("blend=", 0) == 0 || t[0].rfind("carve=", 0) == 0) {
            mode = t[0][0] == 'b' ? Mode::Blend : Mode::Carve;
            k = num(subst(t[0].substr(6)), 0.1);
            h = 1;
        }
        if (h >= t.size()) {
            err("a prefix with nothing after it");
            continue;
        }
        const std::string head = t[h];
        if (head == "define") {
            if (t.size() < h + 2) {
                err("define needs a name");
                continue;
            }
            Macro mac;
            for (std::size_t j = h + 2; j < t.size(); ++j) {
                const std::size_t eq = t[j].find('=');
                mac.params.push_back({t[j].substr(0, eq), eq == std::string::npos ? "0" : t[j].substr(eq + 1)});
            }
            collect(lines, i, mac.body);
            macros_[t[h + 1]] = std::move(mac);
            continue;
        }
        for (std::size_t j = h + 1; j < t.size(); ++j) t[j] = subst(t[j]);
        Args a;
        parse_args(t, h + 1, a);
        if (head == "end") {
            if (st_.size() > floor) close();
            else err("`end` with nothing to end");
        } else if (head == "let") {
            if (a.pos.size() < 2) {
                err("let name value");
                continue;
            }
            double d;
            st_.back().vars[a.pos[0]] = number(a.pos[1], d) ? fmt(d) : a.pos[1];
        } else if (head == "set") {
            Defaults& d = st_.back().def;
            for (auto& [key, v] : a.opt) {
                if (key == "res") d.res = num(v, 0);
                else if (key == "sides") d.sides = int(num(v, 24));
                else if (key == "crease") d.crease = num(v, 40);
                else if (key == "mat") d.mat = mat_id(v);
                else err("set knows res, sides, crease, mat - not " + key);
            }
        } else if (head == "for") {
            if (a.pos.size() < 2) {
                err("for var count");
                continue;
            }
            std::vector<std::string> body;
            collect(lines, i, body);
            const int n = std::max(0, int(num(a.pos[1], 0)));
            open("group", a, mode, k);
            st_.back().args = Args{};
            for (const auto& [kk, vv] : a.opt) st_.back().args.opt[kk] = vv;
            const std::size_t fl = st_.size();
            for (int q = 0; q < n; ++q) {
                st_.back().vars[a.pos[0]] = std::to_string(q);
                ++depth_;
                exec(body, fl, 0);
                --depth_;
                while (st_.size() > fl) close();
            }
            close();
        } else if (head == "group" || head == "array" || head == "radial" || head == "mirror") {
            open(head, a, mode, k);
            if (head == "group" && !a.pos.empty()) st_.back().name = a.pos[0];
        } else if (head == "copy") {
            auto it = a.pos.empty() ? named_.end() : named_.find(a.pos[0]);
            if (it == named_.end()) err("no group named " + (a.pos.empty() ? std::string("?") : a.pos[0]));
            else join_top(moved(it->second, place(a)), mode, k);
        } else if (shapes_known().count(head)) {
            Made m;
            if (make(head, a, st_.back().def, m)) join_top(solid(m, a, st_.back().def), mode, k);
        } else if (macros_.count(head)) {
            call(head, a, mode, k);
        } else {
            err("unknown statement " + head);
        }
    }
}

// --- the lists ---------------------------------------------------------------------

std::string lib_text() { return library(); }

}  // namespace

std::vector<float> Model::all() const {
    std::vector<float> v;
    for (const Part& p : parts) v.insert(v.end(), p.corners.begin(), p.corners.end());
    return v;
}

Model build(const std::string& recipe, const Options& options, const Files* files) {
    Interp in(options, files);
    const auto lines_of = [](const std::string& s) {
        std::vector<std::string> out;
        std::istringstream is(s);
        std::string l;
        while (std::getline(is, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            out.push_back(l);
        }
        return out;
    };
    in.exec(lines_of(lib_text()), 1, 0);
    in.errors.clear();
    in.exec(lines_of(recipe), 1, 1);
    Solid s = in.result();
    Model out;
    std::vector<std::vector<float>> by;
    for (const Piece& p : s.pieces) {
        Geom g;
        if (p.exact) g = *p.exact;
        else {
            surface(*p.field, p.res > 0 ? p.res : options.cell, options.max_grid, g, &in.errors);
            g.crease = p.crease;
        }
        shade(g, by, p.material);
    }
    Box bb;
    for (std::size_t m = 0; m < by.size(); ++m) {
        if (by[m].empty()) continue;
        Part part;
        part.material = m < in.mats_.size() ? in.mats_[m] : "";
        part.corners = std::move(by[m]);
        for (std::size_t i = 0; i + 7 < part.corners.size(); i += 8) bb.grow(V3{part.corners[i], part.corners[i + 1], part.corners[i + 2]});
        out.triangles += part.corners.size() / 24;
        out.parts.push_back(std::move(part));
    }
    if (!bb.empty()) out.lo = {bb.lo.x, bb.lo.y, bb.lo.z}, out.hi = {bb.hi.x, bb.hi.y, bb.hi.z};
    out.errors = in.errors;
    out.imports = in.imports;
    return out;
}

std::string recipes() {
    std::string out;
    std::istringstream is(lib_text());
    std::string l;
    while (std::getline(is, l)) {
        if (l.rfind("define ", 0) != 0) continue;
        const std::size_t hash = l.find('#');
        std::string head = l.substr(7, hash == std::string::npos ? std::string::npos : hash - 7);
        while (!head.empty() && head.back() == ' ') head.pop_back();
        out += head + (hash == std::string::npos ? "" : "  -" + l.substr(hash + 1)) + "\n";
    }
    return out;
}

std::string to_obj(const Model& m, const std::string& name) {
    std::string out = "# " + name + "\n";
    std::size_t base = 1;
    for (const Part& p : m.parts) {
        out += "o " + name + (p.material.empty() ? "" : "." + p.material) + "\n";
        if (!p.material.empty()) out += "usemtl " + p.material + "\n";
        std::unordered_map<std::string, std::size_t> seen;
        std::string v, f;
        const std::size_t n = p.corners.size() / 8;
        std::vector<std::size_t> idx(n);
        std::size_t count = 0;
        for (std::size_t i = 0; i < n; ++i) {
            char b[160];
            const float* c = &p.corners[i * 8];
            std::snprintf(b, sizeof b, "%.5f %.5f %.5f|%.4f %.4f %.4f", c[0], c[1], c[2], c[3], c[4], c[5]);
            auto it = seen.find(b);
            if (it == seen.end()) {
                it = seen.emplace(b, count++).first;
                char l[160];
                std::snprintf(l, sizeof l, "v %.5f %.5f %.5f\nvn %.4f %.4f %.4f\n", c[0], c[1], c[2], c[3], c[4], c[5]);
                v += l;
            }
            idx[i] = it->second + base;
        }
        for (std::size_t i = 0; i + 2 < n; i += 3)
            f += "f " + std::to_string(idx[i]) + "//" + std::to_string(idx[i]) + " " + std::to_string(idx[i + 1]) + "//" + std::to_string(idx[i + 1]) + " " +
                 std::to_string(idx[i + 2]) + "//" + std::to_string(idx[i + 2]) + "\n";
        out += v + f;
        base += count;
    }
    return out;
}

}  // namespace sg::sculpt
