// The agent's lens (docs/workflows.md section 14): any program that draws a
// world through a GLWorldView, run with SG_LENS set, looks at a point or a
// thing of the room it stands in from nine eyes round it (every side, every
// scale), holds one eye still for a while and measures what changes with
// nothing moving (flicker, a black frame, the exposure hunting), says where
// in each picture the world's faults (prepare) are, writes it all to a
// folder and ends the program. tools/sg_lens.py runs it and lays the sheet.
//
//   SG_LENS="at=x,y,z;yaw=deg;r=0.9;hold=2;warm=40;out=dir"   or  "thing=<id>;..."
//
// The eyes are the lens's own: a copy of the room's camera, stood elsewhere;
// nothing of the world, or its time, is written. (The program steps on as it
// does: the still eye's frames are the world's own frames.)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "sg/domains/Modeler.hpp"
#include "sg/gl/GL.hpp"
#include "sg/gl/World.hpp"
#include "sg/render/ViewPlan.hpp"
#include "sg/domains/Walk.hpp"

namespace sg::render {
namespace {

struct LensEye {
    std::string name;
    Vec3d at;
    double yaw = 0, pitch = 0, fov = 60;
};
struct LensRun {
    bool read = false, on = false;
    std::string thing, out = "lens";
    Vec3d at;
    bool have_at = false;
    double face = 0, r = 0.9, hold = 2.0;
    int warm = 40, frame = 0, settle = 8;
    std::vector<LensEye> eyes;
    std::vector<double> lum, diff, changed;
    std::vector<std::vector<float>> blocks;  // each still frame's light in 8 by 8 blocks
    double worst = 0.0;                      // the most of its blocks a still frame changed from the one before
    int worst_at = -1;
    std::vector<unsigned char> worst_before, worst_px;
    float gain = 1.0f;
    bool clean = false;
    std::vector<unsigned char> last;
    std::vector<std::string> faults;
    std::string json;
};
std::map<const void*, LensRun>& runs() {
    static std::map<const void*, LensRun> r;
    return r;
}

LensEye eye_round(const std::string& name, const Vec3d& p, double face_deg, double r, double around, double up) {
    const double a = (face_deg + around) * 3.14159265358979 / 180.0;
    LensEye e;
    e.name = name;
    e.at = {p.x + r * std::cos(a), p.y + up, p.z + r * std::sin(a)};
    e.yaw = std::atan2(p.z - e.at.z, p.x - e.at.x);
    e.pitch = std::atan2(p.y - e.at.y, std::hypot(p.x - e.at.x, p.z - e.at.z));
    e.fov = r < 1.5 ? 50.0 : 62.0;
    return e;
}

// (Read bottom up, written top down.)
void write_png(const std::string& path, const std::vector<unsigned char>& px, int w, int h) {
    std::vector<unsigned char> down(px.size());
    for (int y = 0; y < h; ++y)
        std::copy_n(px.data() + static_cast<std::size_t>(h - 1 - y) * w * 3, static_cast<std::size_t>(w) * 3, down.data() + static_cast<std::size_t>(y) * w * 3);
    std::ofstream o(path, std::ios::binary);
    o << sculpt::png(down, w, h);
}

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if (static_cast<unsigned char>(c) >= 32) o += c;
    }
    return o;
}

}  // namespace

// What prepare said of the world, kept for the lens to show where it is.
void lens_keep_faults(const void* view, const std::vector<std::string>& faults) { runs()[view].faults = faults; }

bool GLWorldView::lens_frame(const std::vector<PlacedRoom>& requested, int fb_w, int fb_h) {
    LensRun& L = runs()[this];
    if (!L.read) {
        L.read = true;
        const char* spec = std::getenv("SG_LENS");
        if (!spec || !*spec) return false;
        L.on = true;
        std::string s = spec;
        for (std::size_t i = 0; i <= s.size();) {
            const std::size_t j = std::min(s.find(';', i), s.size());
            const std::string kv = s.substr(i, j - i);
            const std::size_t eq = kv.find('=');
            if (eq != std::string::npos) {
                const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
                if (k == "at") L.have_at = std::sscanf(v.c_str(), "%lf,%lf,%lf", &L.at.x, &L.at.y, &L.at.z) == 3;
                else if (k == "thing") L.thing = v;
                else if (k == "yaw") L.face = std::atof(v.c_str());
                else if (k == "r") L.r = std::atof(v.c_str());
                else if (k == "hold") L.hold = std::atof(v.c_str());
                else if (k == "warm") L.warm = std::atoi(v.c_str());
                else if (k == "settle") L.settle = std::max(1, std::atoi(v.c_str()));
                else if (k == "out") L.out = v;
                else if (k == "bright") L.gain = static_cast<float>(std::max(0.01, std::atof(v.c_str())));
                else if (k == "clean") L.clean = std::atoi(v.c_str()) != 0;
            }
            i = j + 1;
        }
    }
    if (!L.on || requested.empty() || !requested.front().room) return false;
    const Spatial3D& world = *requested.front().room;
    ++L.frame;
    if (L.frame <= L.warm) return false;  // (the program's own first frames: its start)
    if (L.eyes.empty()) {
        // Where to look: the point, or the thing's box's middle (by its id, or the first whose id ends so).
        if (!L.thing.empty()) {
            const Element* t = world.find(Key{L.thing});
            for (const Element& e : world.elements())
                if (!t && e.id.str().size() >= L.thing.size() && e.id.str().compare(e.id.str().size() - L.thing.size(), L.thing.size(), L.thing) == 0) t = &e;
            if (t) {
                const Pose p = world_pose(world, *t);
                L.at = place_in(p, {0.0, t->params.num(keys::sy, 0.0) * 0.5, 0.0}), L.have_at = true;
                L.face = p.yaw * 180.0 / 3.14159265358979;
                const double size = std::max({t->params.num(keys::sx, 0.3), t->params.num(keys::sy, 0.3), t->params.num(keys::sz, 0.3)});
                if (L.r <= 0.0 || L.r == 0.9) L.r = std::clamp(size * 1.6, 0.5, 4.0);
            } else
                std::fprintf(stderr, "lens: no thing %s in %s\n", L.thing.c_str(), world.id().str().c_str());
        }
        if (!L.have_at) {
            const Vec3d e = world_position(world, world.camera());
            const double yaw = world.camera().params.num(keys::yaw);
            L.at = {e.x + 1.5 * std::cos(yaw), e.y - 0.4, e.z + 1.5 * std::sin(yaw)};
            L.face = yaw * 180.0 / 3.14159265358979 + 180.0;
        }
        const double r = L.r;
        // The still eye first (from across: what a player sees), then the nine.
        L.eyes = {eye_round("hold", L.at, L.face, std::max(2.2, r * 2.5), 15, 0.5),     eye_round("front", L.at, L.face, r, 0, 0.15),
                  eye_round("left", L.at, L.face, r, -65, 0.1),                         eye_round("right", L.at, L.face, r, 65, 0.1),
                  eye_round("above", L.at, L.face, r * 0.6, 20, r * 0.9),               eye_round("low", L.at, L.face, r, 25, -0.2),
                  eye_round("across", L.at, L.face, std::max(2.6, r * 2.8), 15, 0.6),   eye_round("close", L.at, L.face, std::max(0.25, r * 0.4), 10, 0.08),
                  eye_round("closer", L.at, L.face, std::max(0.18, r * 0.22), -30, 0.12), eye_round("far", L.at, L.face, std::max(3.6, r * 4.0), -10, 0.9)};
        // (An eye behind a wall sees the wall: each is drawn in to stand before what is between it and the point.)
        for (LensEye& le : L.eyes) {
            const Vec3d d{le.at.x - L.at.x, le.at.y - L.at.y, le.at.z - L.at.z};
            const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (len < 1e-6) continue;
            const Vec3d u{d.x / len, d.y / len, d.z / len};
            Vec3d hit, n;
            double dist = 0.0;
            // (From a hand off the point, so its own box does not stop it.)
            const double from = std::min(0.3, len * 0.5);
            const Vec3d o{L.at.x + u.x * from, L.at.y + u.y * from, L.at.z + u.z * from};
            if (ray(world, o, u, len - from, hit, n, &dist) && dist + from < len) {
                const double keep = std::max(from, dist + from - 0.12);
                le.at = {L.at.x + u.x * keep, L.at.y + u.y * keep, L.at.z + u.z * keep};
            }
        }
        std::error_code ec;
        std::filesystem::create_directories(L.out, ec);
    }
    // (The still eye's first frames are its settling - the picture's history, the exposure: not counted.)
    constexpr int kSettleFrames = 24;
    const int hold_frames = std::clamp(static_cast<int>(L.hold * 60.0), 0, 600);
    const int k = L.frame - L.warm - 1;
    // Which eye this frame: the still one for `hold_frames`, then each of the nine for `settle`.
    int which = 0, into = k;
    if (k >= hold_frames) which = 1 + (k - hold_frames) / L.settle, into = (k - hold_frames) % L.settle;
    if (which >= static_cast<int>(L.eyes.size())) {
        // Done: say it all, and end the program.
        const auto median = [](std::vector<double> v) {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        const double mid = median(L.lum);
        int black = 0, flick = 0, turns = 0;
        for (std::size_t i = 0; i < L.lum.size(); ++i) black += L.lum[i] < mid * 0.25 ? 1 : 0;
        // Flicker, not grain, not a change that stays. Each 8 by 8 block's light (grain, a pixel wide,
        // averages away in a block). A frame flickers where more than 0.3% of its blocks stand more than
        // 4/255 off the mean of the frames either side of it - a pop and back, an alternation: two faces
        // fighting, a shadow redrawn. A step - many blocks changing at once and staying changed (a light
        // switched, a shadow or probe made again, a picture streamed in) - is a pop, said apart with its
        // frame; a slow drift (settling) is neither.
        std::vector<double> flicker;
        std::vector<int> steps;
        if (L.blocks.size() >= 3) {
            const std::size_t nb = L.blocks.front().size(), nf = L.blocks.size();
            flicker.push_back(0.0);
            for (std::size_t f = 1; f + 1 < nf; ++f) {
                std::size_t off = 0, jump = 0;
                for (std::size_t b = 0; b < nb; ++b) {
                    const float p = L.blocks[f - 1][b], c = L.blocks[f][b], n = L.blocks[f + 1][b];
                    off += std::fabs(c - 0.5f * (p + n)) > 4.0f && (c - p) * (c - n) > 0.0f ? 1 : 0;
                    // (Not the way back from a one-frame flash: it differs from the frame before that too.)
                    const float pp = f >= 2 ? L.blocks[f - 2][b] : p;
                    jump += std::fabs(c - p) > 4.0f && std::fabs(n - c) < 2.0f && std::fabs(c - pp) > 4.0f ? 1 : 0;
                }
                flicker.push_back(static_cast<double>(off) / std::max<std::size_t>(1, nb));
                flick += flicker.back() > 0.003 ? 1 : 0;
                if (static_cast<double>(jump) / std::max<std::size_t>(1, nb) > 0.005) steps.push_back(static_cast<int>(f));
            }
            flicker.push_back(0.0);
        }
        double grain = 0.0;
        for (double c : L.changed) grain += c / std::max<std::size_t>(1, L.changed.size());
        // (The exposure hunting: the mean light turning up then down again, by more than 1.5%, more than twice.)
        double prev_d = 0.0;
        for (std::size_t i = 1; i < L.lum.size(); ++i) {
            const double d = L.lum[i] - L.lum[i - 1];
            if (std::fabs(d) < mid * 0.015) continue;
            if (prev_d != 0.0 && (d > 0) != (prev_d > 0)) ++turns;
            prev_d = d;
        }
        std::string j = "{\n  \"room\": \"" + esc(world.id().str()) + "\",\n  \"size\": [" + std::to_string(fb_w) + ", " + std::to_string(fb_h) + "],\n";
        char b[256];
        std::snprintf(b, sizeof b, "  \"at\": [%.3f, %.3f, %.3f], \"face\": %.2f, \"r\": %.3f,\n", L.at.x, L.at.y, L.at.z, L.face, L.r);
        j += b;
        std::snprintf(b, sizeof b, "  \"hold\": {\"frames\": %zu, \"median_light\": %.4f, \"black_frames\": %d, \"flicker_frames\": %d, \"exposure_turns\": %d, \"pops\": %zu, \"grain\": %.4f, \"bright\": %.2f, \"clean\": %s,\n", L.lum.size(), mid, black,
                      flick, turns, steps.size(), grain, L.gain, L.clean ? "true" : "false");
        j += b;
        j += "    \"light\": [";
        for (std::size_t i = 0; i < L.lum.size(); ++i) j += (i ? "," : "") + std::to_string(L.lum[i]);
        j += "],\n    \"changed\": [";
        for (std::size_t i = 0; i < flicker.size(); ++i) j += (i ? "," : "") + std::to_string(flicker[i]);
        j += "],\n    \"worst_frame\": " + std::to_string(L.worst_at) + ", \"worst_blocks\": " + std::to_string(L.worst) + ",\n    \"pop_frames\": [";
        for (std::size_t i = 0; i < steps.size(); ++i) j += (i ? "," : "") + std::to_string(steps[i]);
        j += "],\n    \"pixels_changed\": [";
        for (std::size_t i = 0; i < L.changed.size(); ++i) j += (i ? "," : "") + std::to_string(L.changed[i]);
        j += "]},\n  \"views\": [\n" + L.json + "  ],\n";
        // Every screen's picture as it was last made: one flat colour is a screen that came out blank.
        j += "  \"feeds\": [";
        int blank = 0;
        bool first_feed = true;
        for (const auto& kv : shared_feeds()) {
            const Feed& fd = kv.second;
            double best_spread = -1.0, best_mean = 0.0;
            int fw = 0, fh = 0;
            for (const gl::RenderTarget& rt : fd.outs[Feed::slot_of(fd.div)]) {
                if (!rt.valid() || rt.width() <= 0 || rt.height() <= 0) continue;
                std::vector<unsigned char> q(static_cast<std::size_t>(rt.width()) * rt.height() * 4);
                gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, rt.framebuffer());
                gl::glReadPixels(0, 0, rt.width(), rt.height(), gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, q.data());
                double sum = 0.0, sq = 0.0;
                const double n = static_cast<double>(q.size() / 4);
                for (std::size_t i = 0; i < q.size(); i += 4) {
                    const double y = (0.2126 * q[i] + 0.7152 * q[i + 1] + 0.0722 * q[i + 2]) / 255.0;
                    sum += y, sq += y * y;
                }
                const double mean = sum / n, spread = std::sqrt(std::max(0.0, sq / n - mean * mean));
                if (spread > best_spread) best_spread = spread, best_mean = mean, fw = rt.width(), fh = rt.height();
            }
            gl::glBindFramebuffer(gl::GL_READ_FRAMEBUFFER, 0);
            if (best_spread < 0.0) continue;
            const bool flat = best_spread < 0.01;
            blank += flat && fd.drawn ? 1 : 0;
            char m[320];
            std::snprintf(m, sizeof m, "%s\n    {\"portal\": \"%s\", \"world\": \"%s\", \"size\": [%d, %d], \"drawn\": %s, \"mean\": %.4f, \"spread\": %.4f, \"blank\": %s}",
                          first_feed ? "" : ",", esc(kv.first.str()).c_str(), fd.world ? esc(fd.world->id().str()).c_str() : "", fw, fh, fd.drawn ? "true" : "false", best_mean,
                          best_spread, flat && fd.drawn ? "true" : "false");
            j += m;
            first_feed = false;
        }
        j += "\n  ],\n  \"blank_feeds\": " + std::to_string(blank) + ",\n  \"faults\": [\n";
        for (std::size_t i = 0; i < L.faults.size(); ++i) j += "    \"" + esc(L.faults[i]) + "\"" + (i + 1 < L.faults.size() ? ",\n" : "\n");
        j += "  ]\n}\n";
        if (L.worst > 0.003 && !L.worst_px.empty()) {
            write_png(L.out + "/hold_worst_before.png", L.worst_before, fb_w, fb_h);
            write_png(L.out + "/hold_worst.png", L.worst_px, fb_w, fb_h);
        }
        if (FILE* f = std::fopen((L.out + "/lens.json").c_str(), "w")) std::fputs(j.c_str(), f), std::fclose(f);
        std::printf("lens: %s/lens.json (%d black, %d flickering, %d exposure turns in %zu still frames; %d screens blank)\n", L.out.c_str(), black, flick, turns, L.lum.size(), blank);
        std::fflush(stdout), std::fflush(stderr);
        std::_Exit(0);
    }
    const LensEye& le = L.eyes[which];
    Element eye = world.camera();
    for (const Key& kk : {Key{"stand_w"}, Key{"stand_x"}, Key{"stand_y"}, Key{"stand_z"}, Key{"seen_from"}, keys::parent}) eye.params.erase(kk);
    eye.params.set(keys::x, le.at.x).set(keys::y, le.at.y).set(keys::z, le.at.z).set(keys::yaw, le.yaw).set(keys::pitch, le.pitch).set(keys::roll, 0.0);
    eye.params.set(keys::fov, le.fov).set(Key{"fstop"}, 0.0).set(Key{"shot"}, static_cast<double>(1000 + which));
    if (into == 0) settle_exposure();
    eye_override_ = &eye;
    lens_gain_ = L.gain, lens_clean_ = L.clean;
    render(requested, fb_w, fb_h);
    lens_gain_ = 1.0f, lens_clean_ = false;
    eye_override_ = nullptr;
    // What it made, read back.
    std::vector<unsigned char> rgba(static_cast<std::size_t>(fb_w) * fb_h * 4), px(static_cast<std::size_t>(fb_w) * fb_h * 3);
    gl::glReadPixels(0, 0, fb_w, fb_h, gl::GL_RGBA, gl::GL_UNSIGNED_BYTE, rgba.data());
    for (std::size_t i = 0, n = static_cast<std::size_t>(fb_w) * fb_h; i < n; ++i)
        px[i * 3] = rgba[i * 4], px[i * 3 + 1] = rgba[i * 4 + 1], px[i * 3 + 2] = rgba[i * 4 + 2];
    if (which == 0) {
        double sum = 0.0;
        std::size_t moved = 0;
        double d = 0.0;
        for (std::size_t i = 0; i < px.size(); i += 3) {
            const double y = 0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2];
            sum += y;
            if (L.last.size() == px.size()) {
                const double yl = 0.2126 * L.last[i] + 0.7152 * L.last[i + 1] + 0.0722 * L.last[i + 2];
                d += std::fabs(y - yl);
                moved += std::fabs(y - yl) > 12.0 ? 1 : 0;
            }
        }
        const double n = static_cast<double>(px.size() / 3);
        // (The first few frames of the still eye are its settling: not counted.)
        if (into >= kSettleFrames) {
            const int bw = std::max(1, fb_w / 8), bh = std::max(1, fb_h / 8);
            std::vector<float> bl(static_cast<std::size_t>(bw) * bh, 0.0f);
            for (int y = 0; y < bh * 8 && y < fb_h; ++y)
                for (int x = 0; x < bw * 8 && x < fb_w; ++x) {
                    const std::size_t i = (static_cast<std::size_t>(y) * fb_w + x) * 3;
                    bl[static_cast<std::size_t>(y / 8) * bw + x / 8] += static_cast<float>(0.2126 * px[i] + 0.7152 * px[i + 1] + 0.0722 * px[i + 2]) / 64.0f;
                }
            if (!L.blocks.empty()) {
                // (The frame that changed most, kept with the one before: where to look.)
                std::size_t moved_blocks = 0;
                for (std::size_t b = 0; b < bl.size(); ++b) moved_blocks += std::fabs(bl[b] - L.blocks.back()[b]) > 4.0f ? 1 : 0;
                const double frac = static_cast<double>(moved_blocks) / std::max<std::size_t>(1, bl.size());
                if (frac > L.worst && L.last.size() == px.size()) L.worst = frac, L.worst_at = static_cast<int>(L.blocks.size()), L.worst_before = L.last, L.worst_px = px;
            }
            L.blocks.push_back(std::move(bl));
            L.lum.push_back(sum / n / 255.0);
            if (L.last.size() == px.size()) L.diff.push_back(d / n / 255.0), L.changed.push_back(moved / n);
        }
        L.last = px;
        if (into == kSettleFrames || into == hold_frames - 1) write_png(L.out + "/hold_" + (into == kSettleFrames ? "first" : "last") + ".png", px, fb_w, fb_h);
        return true;
    }
    if (into == L.settle - 1) {
        const std::string file = L.out + "/" + le.name + ".png";
        write_png(file, px, fb_w, fb_h);
        // Where each named fault's thing is in this picture.
        const ViewCamera vc = view_camera(eye);
        const Vec3d f = vc.forward, up0 = vc.up;
        const Vec3d right{f.y * up0.z - f.z * up0.y, f.z * up0.x - f.x * up0.z, f.x * up0.y - f.y * up0.x};
        const double rl = std::max(1e-9, std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z));
        const Vec3d rr{right.x / rl, right.y / rl, right.z / rl};
        const Vec3d uu{rr.y * f.z - rr.z * f.y, rr.z * f.x - rr.x * f.z, rr.x * f.y - rr.y * f.x};
        const double th = std::tan(vc.fov * 3.14159265358979 / 360.0), aspect = static_cast<double>(fb_w) / std::max(1, fb_h);
        std::string marks;
        for (std::size_t fi = 0; fi < L.faults.size(); ++fi) {
            // (The thing it names: the first word of it that is a thing here.)
            const std::string& text = L.faults[fi];
            const Element* hit = nullptr;
            for (std::size_t i = 0; i < text.size() && !hit;) {
                std::size_t j = text.find_first_of(" ,:()`", i);
                if (j == std::string::npos) j = text.size();
                if (j > i) {
                    const Element* e = world.find(Key{text.substr(i, j - i)});
                    if (e && e->alive && e->id != SpatialState::camera_id()) hit = e;
                }
                i = j + 1;
            }
            if (!hit) continue;
            const Pose p = world_pose(world, *hit);
            const Vec3d c = place_in(p, {0.0, hit->params.num(keys::sy, 0.0) * 0.5, 0.0});
            const Vec3d dv{c.x - vc.eye.x, c.y - vc.eye.y, c.z - vc.eye.z};
            const double z = dv.x * f.x + dv.y * f.y + dv.z * f.z;
            if (z < 0.05) continue;
            const double sx = (dv.x * rr.x + dv.y * rr.y + dv.z * rr.z) / (z * th * aspect), sy = (dv.x * uu.x + dv.y * uu.y + dv.z * uu.z) / (z * th);
            if (std::fabs(sx) > 1.0 || std::fabs(sy) > 1.0) continue;
            char m[160];
            std::snprintf(m, sizeof m, "%s{\"fault\": %zu, \"thing\": \"%s\", \"px\": [%.1f, %.1f], \"dist\": %.2f}", marks.empty() ? "" : ", ", fi, esc(hit->id.str()).c_str(),
                          (sx * 0.5 + 0.5) * fb_w, (0.5 - sy * 0.5) * fb_h, z);
            marks += m;
        }
        char b[400];
        std::snprintf(b, sizeof b, "    {\"name\": \"%s\", \"file\": \"%s.png\", \"eye\": [%.3f, %.3f, %.3f], \"yaw\": %.2f, \"pitch\": %.2f, \"fov\": %.1f, \"marks\": [", le.name.c_str(),
                      le.name.c_str(), le.at.x, le.at.y, le.at.z, le.yaw * 180.0 / 3.14159265358979, le.pitch * 180.0 / 3.14159265358979, le.fov);
        L.json += std::string(L.json.empty() ? "" : ",\n") + b + marks + "]}";
        if (which + 1 == static_cast<int>(L.eyes.size())) L.json += "\n";
    }
    return true;
}

}  // namespace sg::render
