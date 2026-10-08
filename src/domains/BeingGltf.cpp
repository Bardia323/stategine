// A being read from glTF 2.0 (.gltf or .glb): its skeleton (a skin's joints),
// its skinned body, and its animations as clips - and that body posed, as the
// skeleton is now (linear blend skinning, in the being's frame).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>

#include "sg/domains/Being.hpp"
#include "sg/domains/Shapes.hpp"

namespace sg {

namespace {

// --- just enough JSON ------------------------------------------------------------
struct J {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    double n = 0;
    bool b = false;
    std::string s;
    std::vector<J> a;
    std::map<std::string, J> o;
    const J& operator[](const std::string& k) const {
        static const J none;
        auto it = o.find(k);
        return it == o.end() ? none : it->second;
    }
    const J& operator[](std::size_t i) const {
        static const J none;
        return i < a.size() ? a[i] : none;
    }
    double num(double d = 0) const { return t == Num ? n : d; }
    int i(int d = -1) const { return t == Num ? int(n) : d; }
    bool has(const std::string& k) const { return o.count(k) != 0; }
};

struct Reader {
    const std::string& s;
    std::size_t p = 0;
    bool ok = true;
    void ws() {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
    }
    std::string str() {
        std::string out;
        ++p;
        while (p < s.size() && s[p] != '"') {
            if (s[p] == '\\' && p + 1 < s.size()) {
                ++p;
                const char c = s[p];
                out += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'u' ? '?' : c;
                if (c == 'u') p += 4;
            } else {
                out += s[p];
            }
            ++p;
        }
        ++p;
        return out;
    }
    J value() {
        ws();
        J v;
        if (p >= s.size()) return ok = false, v;
        const char c = s[p];
        if (c == '{') {
            v.t = J::Obj;
            ++p;
            ws();
            if (s[p] == '}') return ++p, v;
            while (ok) {
                ws();
                const std::string k = str();
                ws();
                ++p;  // :
                v.o[k] = value();
                ws();
                if (s[p] == ',') ++p;
                else if (s[p] == '}') return ++p, v;
                else ok = false;
            }
        } else if (c == '[') {
            v.t = J::Arr;
            ++p;
            ws();
            if (s[p] == ']') return ++p, v;
            while (ok) {
                v.a.push_back(value());
                ws();
                if (s[p] == ',') ++p;
                else if (s[p] == ']') return ++p, v;
                else ok = false;
            }
        } else if (c == '"') {
            v.t = J::Str, v.s = str();
        } else if (s.compare(p, 4, "true") == 0) {
            v.t = J::Bool, v.b = true, p += 4;
        } else if (s.compare(p, 5, "false") == 0) {
            v.t = J::Bool, p += 5;
        } else if (s.compare(p, 4, "null") == 0) {
            p += 4;
        } else {
            char* end = nullptr;
            v.t = J::Num, v.n = std::strtod(s.c_str() + p, &end);
            if (end == s.c_str() + p) ok = false;
            p = std::size_t(end - s.c_str());
        }
        return v;
    }
};

std::string base64(const std::string& in) {
    std::string out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62;
        else if (c == '/') d = 63;
        else continue;
        val = (val << 6) + d, bits += 6;
        if (bits >= 0) out += char((val >> bits) & 0xff), bits -= 8;
    }
    return out;
}

// A glTF document, its buffers, and its accessors read as floats.
struct Doc {
    J j;
    std::vector<std::string> buffers;
    bool ok = false;
    std::string why;

    std::vector<float> floats(int acc, int& comps) const {
        std::vector<float> out;
        const J& a = j["accessors"][std::size_t(acc)];
        const J& bv = j["bufferViews"][std::size_t(a["bufferView"].i(0))];
        const std::string& buf = buffers[std::size_t(bv["buffer"].i(0))];
        const std::string type = a["type"].s;
        comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 1;
        const int ct = a["componentType"].i(5126), count = a["count"].i(0);
        const int size = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
        const std::size_t stride = bv["byteStride"].i(0) > 0 ? std::size_t(bv["byteStride"].i(0)) : std::size_t(size * comps);
        const std::size_t base = std::size_t(bv["byteOffset"].i(0) + a["byteOffset"].i(0));
        const bool normed = a["normalized"].b;
        out.reserve(std::size_t(count * comps));
        for (int e = 0; e < count; ++e)
            for (int c = 0; c < comps; ++c) {
                const std::size_t at = base + std::size_t(e) * stride + std::size_t(c * size);
                if (at + std::size_t(size) > buf.size()) return out;
                const char* q = buf.data() + at;
                float v = 0;
                if (ct == 5126) std::memcpy(&v, q, 4);
                else if (ct == 5125) { uint32_t u; std::memcpy(&u, q, 4); v = float(u); }
                else if (ct == 5123) { uint16_t u; std::memcpy(&u, q, 2); v = normed ? u / 65535.0f : float(u); }
                else if (ct == 5122) { int16_t u; std::memcpy(&u, q, 2); v = normed ? std::max(u / 32767.0f, -1.0f) : float(u); }
                else if (ct == 5121) { uint8_t u = uint8_t(*q); v = normed ? u / 255.0f : float(u); }
                else if (ct == 5120) { int8_t u = int8_t(*q); v = normed ? std::max(u / 127.0f, -1.0f) : float(u); }
                out.push_back(v);
            }
        return out;
    }
};

Doc read_doc(const std::string& bytes, const std::string& path, const Being::Files* files) {
    Doc d;
    std::string json;
    std::string bin;
    if (bytes.size() >= 12 && bytes.compare(0, 4, "glTF") == 0) {
        // .glb: a header, then chunks - the JSON, then the binary.
        std::size_t at = 12;
        while (at + 8 <= bytes.size()) {
            uint32_t len, type;
            std::memcpy(&len, bytes.data() + at, 4);
            std::memcpy(&type, bytes.data() + at + 4, 4);
            if (type == 0x4E4F534A) json = bytes.substr(at + 8, len);
            else if (type == 0x004E4942) bin = bytes.substr(at + 8, len);
            at += 8 + len;
        }
    } else {
        json = bytes;
    }
    Reader r{json};
    d.j = r.value();
    if (!r.ok || d.j.t != J::Obj) return d.why = "not glTF (its JSON does not read)", d;
    const std::string dir = path.find_last_of("/\\") == std::string::npos ? "" : path.substr(0, path.find_last_of("/\\") + 1);
    for (const J& b : d.j["buffers"].a) {
        const std::string uri = b["uri"].s;
        if (uri.empty()) d.buffers.push_back(bin);
        else if (uri.rfind("data:", 0) == 0) d.buffers.push_back(base64(uri.substr(uri.find(',') + 1)));
        else {
            std::string text;
            if (!files || !files->read || !files->read(dir + uri, text)) return d.why = "cannot read its buffer " + uri, d;
            d.buffers.push_back(text);
        }
    }
    d.ok = true;
    return d;
}

// --- turns and moves -------------------------------------------------------------
struct M4 {
    double m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};  // column-major, as glTF
};
M4 mul(const M4& a, const M4& b) {
    M4 r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
M4 trs(const Vec3d& t, double qw, double qx, double qy, double qz) {
    M4 r;
    const double n = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
    if (n > 1e-12) qw /= n, qx /= n, qy /= n, qz /= n;
    r.m[0] = 1 - 2 * (qy * qy + qz * qz), r.m[1] = 2 * (qx * qy + qw * qz), r.m[2] = 2 * (qx * qz - qw * qy);
    r.m[4] = 2 * (qx * qy - qw * qz), r.m[5] = 1 - 2 * (qx * qx + qz * qz), r.m[6] = 2 * (qy * qz + qw * qx);
    r.m[8] = 2 * (qx * qz + qw * qy), r.m[9] = 2 * (qy * qz - qw * qx), r.m[10] = 1 - 2 * (qx * qx + qy * qy);
    r.m[12] = t.x, r.m[13] = t.y, r.m[14] = t.z;
    return r;
}
M4 scaled(M4 a, double k) {
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) a.m[c * 4 + r] *= k;
    return a;
}
// a * b, as quaternions (w, x, y, z).
void qmul(const double a[4], const double b[4], double out[4]) {
    out[0] = a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3];
    out[1] = a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2];
    out[2] = a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1];
    out[3] = a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0];
}
Vec3d apply(const M4& a, const Vec3d& p, double w) {
    return {a.m[0] * p.x + a.m[4] * p.y + a.m[8] * p.z + a.m[12] * w, a.m[1] * p.x + a.m[5] * p.y + a.m[9] * p.z + a.m[13] * w,
            a.m[2] * p.x + a.m[6] * p.y + a.m[10] * p.z + a.m[14] * w};
}

std::string n6(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

// What a skin is made of, read once from its file (memoised on the file's stamp).
struct SkinData {
    long long stamp = -1;
    std::vector<Vec3d> pos, nrm;
    std::vector<float> uv;
    std::vector<int> joints;    // 4 a vertex: indices into `names`
    std::vector<float> weights;  // 4 a vertex
    std::vector<uint32_t> index;
    std::vector<std::string> names;  // the skin's joints, in its order
    std::vector<M4> inverse_bind;
    // Its picture: the base colour its material wears, by its uvs (none: 0 x 0).
    int image_w = 0, image_h = 0;
    std::vector<unsigned char> image;
};
std::map<std::string, std::shared_ptr<SkinData>>& skins() {
    static std::map<std::string, std::shared_ptr<SkinData>> s;
    return s;
}

std::string node_name(const J& doc, int n) {
    const std::string s = doc["nodes"][std::size_t(n)]["name"].s;
    return s.empty() ? "node" + std::to_string(n) : s;
}

}  // namespace

Being::Files& Being::files() {
    static Files f;
    return f;
}

bool Being::import_gltf(const std::string& path, const std::string& prefix, std::string* why) {
    std::string bytes;
    if (!files().read || !files().read(path, bytes)) return why && (*why = "cannot read " + path, true), false;
    const Doc d = read_doc(bytes, path, &files());
    if (!d.ok) return why && (*why = d.why, true), false;
    const J& nodes = d.j["nodes"];
    std::vector<int> parent(nodes.a.size(), -1);
    for (std::size_t i = 0; i < nodes.a.size(); ++i)
        for (const J& c : nodes[i]["children"].a)
            if (c.i() >= 0 && std::size_t(c.i()) < parent.size()) parent[std::size_t(c.i())] = int(i);
    const J& skin = d.j["skins"][0];
    if (skin.t != J::Obj) return why && (*why = "it has no skin (no skeleton)", true), false;
    std::vector<int> joint_nodes;
    for (const J& jn : skin["joints"].a) joint_nodes.push_back(jn.i());
    const auto is_joint = [&](int n) { return std::find(joint_nodes.begin(), joint_nodes.end(), n) != joint_nodes.end(); };
    // Joints in order, parents first: each riding its nearest ancestor that is
    // a joint, at its own translation, turned at rest by its own rotation.
    std::vector<int> order;
    std::function<void(int)> visit = [&](int n) {
        if (std::find(order.begin(), order.end(), n) != order.end()) return;
        int p = parent[std::size_t(n)];
        while (p >= 0 && !is_joint(p)) p = parent[std::size_t(p)];
        if (p >= 0) visit(p);
        order.push_back(n);
    };
    for (int n : joint_nodes) visit(n);
    // What is no joint but holds the skeleton (an `Armature` turned a quarter
    // and scaled to metres, as a Mixamo export is) moves, turns and sizes it
    // all: the root stands where it puts it, turned as it turns it; every
    // bone's length, and every travel, is as long as it makes it. Uniform
    // scale, as glTF's skins are.
    struct Above {
        M4 m;
        double q[4] = {1, 0, 0, 0};
        double scale = 1;
    };
    const auto above_of = [&](int n) {
        Above a;
        int p = parent[std::size_t(n)];
        while (p >= 0 && !is_joint(p)) {
            const J& pn = nodes[std::size_t(p)];
            const double k = pn.has("scale") ? pn["scale"][0].num(1) : 1.0;
            const double pq[4] = {pn["rotation"][3].num(1), pn["rotation"][0].num(), pn["rotation"][1].num(), pn["rotation"][2].num()};
            a.m = mul(scaled(trs({pn["translation"][0].num(), pn["translation"][1].num(), pn["translation"][2].num()}, pq[0], pq[1], pq[2], pq[3]), k), a.m);
            double q[4];
            qmul(pq, a.q, q);
            for (int i = 0; i < 4; ++i) a.q[i] = q[i];
            a.scale *= k;
            p = parent[std::size_t(p)];
        }
        return a;
    };
    std::map<int, Above> roots;  // each root joint's holder
    double rig = 1;              // how much the holder sizes the skeleton
    for (int n : order) {
        const J& nd = nodes[std::size_t(n)];
        int p = parent[std::size_t(n)];
        while (p >= 0 && !is_joint(p)) p = parent[std::size_t(p)];
        const Vec3d t{nd["translation"][0].num(), nd["translation"][1].num(), nd["translation"][2].num()};
        double q[4] = {nd["rotation"][3].num(1), nd["rotation"][0].num(), nd["rotation"][1].num(), nd["rotation"][2].num()};
        Vec3d at = t * rig;
        if (p < 0) {
            const Above a = above_of(n);
            roots[n] = a;
            rig = a.scale;
            at = apply(a.m, t, 1.0);
            double r[4];
            qmul(a.q, q, r);
            for (int i = 0; i < 4; ++i) q[i] = r[i];
        }
        const std::string name = prefix + node_name(d.j, n);
        if (find(Key{name})) continue;
        Element& j = joint(name, p >= 0 ? prefix + node_name(d.j, p) : std::string{}, at);
        j.params.set("rest_qw", q[0]).set("rest_qx", q[1]).set("rest_qy", q[2]).set("rest_qz", q[3]);
        j.params.set("qw", q[0]).set("qx", q[1]).set("qy", q[2]).set("qz", q[3]);
    }
    // The body: the first mesh this skin deforms.
    int mesh_node = -1;
    for (std::size_t i = 0; i < nodes.a.size(); ++i)
        if (nodes[i].has("mesh") && nodes[i].has("skin")) mesh_node = int(i);
    if (mesh_node >= 0) {
        Element& s = add_element(Key{prefix + "skin"}, Key{"skin"});
        s.params.set("file", path).set("node", double(mesh_node)).set("prefix", prefix).set("rig", rig);
        s.params.set(keys::r, 0.75).set(keys::g, 0.72).set(keys::b, 0.68);
        // Which joints the skin binds anything to (`bound`): a joint it binds
        // nothing to only marks a place - the end of a head, a toe.
        const J& node = nodes[std::size_t(mesh_node)];
        const J& sk = d.j["skins"][std::size_t(node["skin"].i(0))];
        std::vector<bool> used(sk["joints"].a.size(), false);
        for (const J& prim : d.j["meshes"][std::size_t(node["mesh"].i(0))]["primitives"].a) {
            const J& at = prim["attributes"];
            if (!at.has("JOINTS_0") || !at.has("WEIGHTS_0")) continue;
            int c = 0;
            const std::vector<float> js = d.floats(at["JOINTS_0"].i(), c), ws = d.floats(at["WEIGHTS_0"].i(), c);
            for (std::size_t k = 0; k < js.size() && k < ws.size(); ++k)
                if (ws[k] > 0.01f && js[k] >= 0 && std::size_t(js[k]) < used.size()) used[std::size_t(js[k])] = true;
        }
        for (std::size_t k = 0; k < sk["joints"].a.size(); ++k)
            if (Element* j = find(Key{prefix + node_name(d.j, sk["joints"][k].i())})) j->params.set("bound", used[k] ? 1.0 : 0.0);
    }
    // Its motions: each animation a clip, its channels the joints' rotations
    // (`q` keys) and translations (`p` keys).
    int a = 0;
    for (const J& an : d.j["animations"].a) {
        std::string keys_text;
        for (const J& ch : an["channels"].a) {
            const int node = ch["target"]["node"].i();
            const std::string what = ch["target"]["path"].s;
            if (!is_joint(node) || (what != "rotation" && what != "translation")) continue;
            const J& sm = an["samplers"][std::size_t(ch["sampler"].i(0))];
            int ci, co;
            const std::vector<float> in = d.floats(sm["input"].i(), ci), out = d.floats(sm["output"].i(), co);
            const bool cubic = sm["interpolation"].s == "CUBICSPLINE";
            const std::size_t stride = std::size_t(co) * (cubic ? 3 : 1), off = cubic ? std::size_t(co) : 0;
            const auto root = roots.find(node);
            for (std::size_t k = 0; k < in.size() && (k * stride + off + std::size_t(co)) <= out.size(); ++k) {
                const float* v = out.data() + k * stride + off;
                // (As the joints were made: a root's keys through what holds
                // it, every travel as long as the holder makes it.)
                if (what == "rotation") {
                    double q[4] = {v[3], v[0], v[1], v[2]};
                    if (root != roots.end()) {
                        double r[4];
                        qmul(root->second.q, q, r);
                        for (int i = 0; i < 4; ++i) q[i] = r[i];
                    }
                    keys_text += n6(in[k]) + " " + prefix + node_name(d.j, node) + " q " + n6(q[0]) + " " + n6(q[1]) + " " + n6(q[2]) + " " + n6(q[3]) + "\n";
                } else {
                    const Vec3d t{v[0], v[1], v[2]};
                    const Vec3d at = root != roots.end() ? apply(root->second.m, t, 1.0) : t * rig;
                    keys_text += n6(in[k]) + " " + prefix + node_name(d.j, node) + " p " + n6(at.x) + " " + n6(at.y) + " " + n6(at.z) + "\n";
                }
            }
        }
        const std::string clip_name = an["name"].s.empty() ? "anim" + std::to_string(a) : an["name"].s;
        clip(clip_name, keys_text, true);
        ++a;
    }
    resolve();
    return true;
}

std::vector<float> Being::skinned(Key skin_id) const {
    std::vector<float> out;
    const Element* s = find(skin_id);
    if (!s) return out;
    const std::string path = s->params.get_or<std::string>("file", "");
    const long long stamp = files().stamp ? files().stamp(path) : 0;
    auto& slot = skins()[path + "#" + std::to_string(int(s->params.num("node")))];
    if (!slot || slot->stamp != stamp) {
        slot = std::make_shared<SkinData>();
        slot->stamp = stamp;
        std::string bytes;
        if (files().read && files().read(path, bytes)) {
            const Doc d = read_doc(bytes, path, &files());
            if (d.ok) {
                const J& node = d.j["nodes"][std::size_t(s->params.num("node"))];
                const J& sk = d.j["skins"][std::size_t(node["skin"].i(0))];
                for (const J& jn : sk["joints"].a) slot->names.push_back(s->params.get_or<std::string>("prefix", "") + node_name(d.j, jn.i()));
                int c;
                if (sk.has("inverseBindMatrices")) {
                    const std::vector<float> ib = d.floats(sk["inverseBindMatrices"].i(), c);
                    for (std::size_t i = 0; i + 16 <= ib.size(); i += 16) {
                        M4 m;
                        for (int k = 0; k < 16; ++k) m.m[k] = ib[i + std::size_t(k)];
                        slot->inverse_bind.push_back(m);
                    }
                }
                // The picture its first primitive's material wears, if any:
                // embedded (a buffer view) or beside it (a file), decoded by
                // the program (files().decode).
                {
                    const J& prim0 = d.j["meshes"][std::size_t(node["mesh"].i(0))]["primitives"][0];
                    const J& mat = d.j["materials"][std::size_t(prim0["material"].i(-1) < 0 ? 0 : prim0["material"].i(0))];
                    const int tex = prim0.has("material") ? mat["pbrMetallicRoughness"]["baseColorTexture"]["index"].i(-1) : -1;
                    const int img = tex >= 0 ? d.j["textures"][std::size_t(tex)]["source"].i(-1) : -1;
                    if (img >= 0 && files().decode) {
                        const J& im = d.j["images"][std::size_t(img)];
                        std::string data;
                        if (im.has("bufferView")) {
                            const J& bv = d.j["bufferViews"][std::size_t(im["bufferView"].i(0))];
                            const std::string& buf = d.buffers[std::size_t(bv["buffer"].i(0))];
                            const std::size_t at = std::size_t(bv["byteOffset"].i(0)), n = std::size_t(bv["byteLength"].i(0));
                            if (at + n <= buf.size()) data = buf.substr(at, n);
                        } else if (!im["uri"].s.empty() && im["uri"].s.rfind("data:", 0) != 0) {
                            const std::string dir = path.find_last_of("/\\") == std::string::npos ? "" : path.substr(0, path.find_last_of("/\\") + 1);
                            files().read(dir + im["uri"].s, data);
                        }
                        if (!data.empty() && !files().decode(data, slot->image_w, slot->image_h, slot->image)) slot->image_w = slot->image_h = 0;
                    }
                }
                for (const J& prim : d.j["meshes"][std::size_t(node["mesh"].i(0))]["primitives"].a) {
                    const J& at = prim["attributes"];
                    if (!at.has("POSITION") || !at.has("JOINTS_0") || !at.has("WEIGHTS_0")) continue;
                    const uint32_t first = uint32_t(slot->pos.size());
                    const std::vector<float> p = d.floats(at["POSITION"].i(), c);
                    for (std::size_t i = 0; i + 2 < p.size(); i += 3) slot->pos.push_back({p[i], p[i + 1], p[i + 2]});
                    std::vector<float> nn = at.has("NORMAL") ? d.floats(at["NORMAL"].i(), c) : std::vector<float>(p.size(), 0.f);
                    for (std::size_t i = 0; i + 2 < nn.size(); i += 3) slot->nrm.push_back({nn[i], nn[i + 1], nn[i + 2]});
                    std::vector<float> t = at.has("TEXCOORD_0") ? d.floats(at["TEXCOORD_0"].i(), c) : std::vector<float>(p.size() / 3 * 2, 0.f);
                    slot->uv.insert(slot->uv.end(), t.begin(), t.end());
                    const std::vector<float> js = d.floats(at["JOINTS_0"].i(), c), ws = d.floats(at["WEIGHTS_0"].i(), c);
                    for (float v : js) slot->joints.push_back(int(v));
                    slot->weights.insert(slot->weights.end(), ws.begin(), ws.end());
                    if (prim.has("indices")) {
                        for (float v : d.floats(prim["indices"].i(), c)) slot->index.push_back(first + uint32_t(v));
                    } else {
                        for (uint32_t i = first; i < uint32_t(slot->pos.size()); ++i) slot->index.push_back(i);
                    }
                }
            }
        }
    }
    const SkinData& sd = *slot;
    // Each joint's move from where it was bound to where it is now: its pose
    // (resolved) after its inverse bind.
    std::vector<M4> m(sd.names.size());
    for (std::size_t i = 0; i < sd.names.size(); ++i) {
        const Element* j = find(Key{sd.names[i]});
        if (!j) continue;
        const M4 now = trs({j->params.num("px"), j->params.num("py"), j->params.num("pz")}, j->params.num("pqw", 1), j->params.num("pqx"),
                           j->params.num("pqy"), j->params.num("pqz"));
        // (The joint as glTF has it is sized by what holds the skeleton; ours
        // is not - its lengths already are - so the size goes back between.)
        const M4 sized = scaled(now, s->params.num("rig", 1.0));
        m[i] = i < sd.inverse_bind.size() ? mul(sized, sd.inverse_bind[i]) : sized;
    }
    std::vector<Vec3d> P(sd.pos.size()), N(sd.pos.size());
    for (std::size_t v = 0; v < sd.pos.size(); ++v) {
        Vec3d p{}, nn{};
        double wsum = 0;
        for (int k = 0; k < 4; ++k) {
            const std::size_t at = v * 4 + std::size_t(k);
            if (at >= sd.weights.size()) break;
            const double w = sd.weights[at];
            const int ji = sd.joints[at];
            if (w <= 0 || ji < 0 || std::size_t(ji) >= m.size()) continue;
            p = p + apply(m[std::size_t(ji)], sd.pos[v], 1.0) * w;
            nn = nn + apply(m[std::size_t(ji)], v < sd.nrm.size() ? sd.nrm[v] : Vec3d{0, 1, 0}, 0.0) * w;
            wsum += w;
        }
        if (wsum <= 0) p = sd.pos[v], nn = v < sd.nrm.size() ? sd.nrm[v] : Vec3d{0, 1, 0};
        P[v] = p, N[v] = nn;
    }
    out.reserve(sd.index.size() * 8);
    for (uint32_t i : sd.index) {
        if (i >= P.size()) continue;
        const Vec3d& n = N[i];
        const double l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z) + 1e-12;
        out.insert(out.end(), {float(P[i].x), float(P[i].y), float(P[i].z), float(n.x / l), float(n.y / l), float(n.z / l),
                               i * 2 + 1 < sd.uv.size() ? sd.uv[i * 2] : 0.f, i * 2 + 1 < sd.uv.size() ? sd.uv[i * 2 + 1] : 0.f});
    }
    return out;
}

const std::vector<unsigned char>* Being::skin_picture(Key skin_id, int& w, int& h) const {
    // (Read with the skin, and kept with it: skinned() makes sure it is read.)
    const Element* s = find(skin_id);
    if (!s) return nullptr;
    auto it = skins().find(s->params.get_or<std::string>("file", "") + "#" + std::to_string(int(s->params.num("node"))));
    if (it == skins().end() || !it->second || it->second->image_w <= 0) return nullptr;
    w = it->second->image_w, h = it->second->image_h;
    return &it->second->image;
}

void show_skins(Spatial3D& host, const Being& b, Key anchor) {
    for (const Element& s : b.elements()) {
        if (s.kind != Key{"skin"}) continue;
        std::vector<float> tris = b.skinned(s.id);
        if (tris.empty()) continue;
        // Fitted in the box round it as it is posed, in the being's frame -
        // the frame it is drawn in: the thing's box is what the renderer
        // culls it by and a hand finds it in, so it holds all that is drawn,
        // however it moves. (Never the box its bind pose was made in: that
        // is in the model's own units - a Mixamo export's a box a centimetre
        // high at the feet, and the body vanished when they were out of view.)
        Vec3d lo, hi, size;
        shapes::bounds(tris, lo, hi);
        const Vec3d mid = (lo + hi) * 0.5;
        const Key model{b.id().str() + "." + s.id.str()};
        host.model(model, shapes::fit(std::move(tris), size, lo, hi));
        const Key there{anchor.str() + "." + s.id.str()};
        Element* e = host.find(there);
        if (!e) e = &host.add_element(there, kinds::mesh);
        e->params.set(keys::parent, anchor.str()).set("shape", std::string("model")).set("model", model.str());
        e->params.set(keys::sx, size.x).set(keys::sy, size.y).set(keys::sz, size.z);
        e->params.set(keys::x, mid.x).set(keys::y, lo.y).set(keys::z, mid.z);
        for (Key k : {keys::r, keys::g, keys::b}) e->params.set(k, s.params.num(k, 0.7));
        // Its picture, worn by its own uvs (given to the host once).
        int w = 0, h = 0;
        if (const std::vector<unsigned char>* rgba = b.skin_picture(s.id, w, h)) {
            const Key pic{model.str() + ".picture"};
            if (!host.picture(pic)) host.picture(pic, w, h, *rgba);
            e->params.set("skin", pic.str()).set("uv", 1.0);
            for (Key k : {keys::r, keys::g, keys::b}) e->params.set(k, 1.0);
        }
    }
}

}  // namespace sg
