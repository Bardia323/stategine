// The GL view's looks: their air, their numbers faded and set on a
// program, their programs built and checked, and the uniforms' names.
#include "sg/gl/World.hpp"

namespace sg::render {

GLWorldView::HostAir GLWorldView::air_of(const Spatial3D& world) const {
    const LookState& look = look_of(world);
    HostAir a;
    a.on = true;
    a.density = static_cast<float>(value(look, passes::scene, Key{"uFogDensity"}, 0.0));
    a.start = static_cast<float>(value(look, passes::scene, Key{"uFogStart"}, 0.0));
    a.full = static_cast<float>(value(look, passes::scene, Key{"uFogFull"}, 0.0));
    a.color = {static_cast<float>(value(look, passes::scene, Key{"uFogColor.x"}, 0.0)), static_cast<float>(value(look, passes::scene, Key{"uFogColor.y"}, 0.0)),
               static_cast<float>(value(look, passes::scene, Key{"uFogColor.z"}, 0.0))};
    return a;
}

auto GLWorldView::air_for(const Spatial3D* world) -> Air& {
    // A view's air is the way the eye came to it (its path), the same from
    // frame to frame whatever picture it is drawn into.
    std::unique_ptr<Air>& slot = airs_[{world, path_}];
    if (!slot) {
        // Never more than a few beyond what this frame's views ask for: past
        // that, the one asked for longest ago goes - never one asked for this
        // frame (each view keeps its own, or it is gathered anew every frame).
        if (airs_.size() > kAirs) {
            auto oldest = airs_.end();
            for (auto it = airs_.begin(); it != airs_.end(); ++it)
                if (it->second && it->second->used < frame_count_ && (oldest == airs_.end() || it->second->used < oldest->second->used))
                    oldest = it;
            if (oldest != airs_.end()) {
                air_spares_.push_back(std::move(oldest->second));
                airs_.erase(oldest);
            }
        }
        // (One gone from sight gives its pictures to the next: a view's air
        // made in the middle of a frame stops it, the card making memory.)
        if (!air_spares_.empty()) {
            slot = std::move(air_spares_.back());
            air_spares_.pop_back();
            slot->of = 0, slot->gathered = 0;
        } else {
            slot = std::make_unique<Air>();
        }
    }
    slot->used = frame_count_;
    return *slot;
}

void GLWorldView::advance_fades() {
    dt_ = std::max(0.0, fixed_step_);
    fader_.advance(dt_);
}

void GLWorldView::apply_attended(const gl::Program& p, Key pass) {
    if (!attend_) return;
    const Mix& am = mix(Key{"attend:" + attend_->id().str()}, look_of(*attend_));
    std::vector<Key> keys;
    for (const Mix::Part& part : am.parts)
        if (const Element* e = part.look->find(pass))
            for (const auto& kv : e->params)
                if (is_uniform_key(kv.first) && kv.first.str().find('.') == std::string::npos &&
                    std::find(keys.begin(), keys.end(), kv.first) == keys.end())
                    keys.push_back(kv.first);
    for (Key k : keys) p.set(k.str().c_str(), static_cast<float>(fader_.value(am, pass, k, 0.0)));
}

void GLWorldView::apply_uniforms(const gl::Program& p, const Mix& m, Key pass) {
    uint64_t key = (1469598103934665603ULL ^ reinterpret_cast<std::uintptr_t>(&p)) * 1099511628211ULL;
    key = (key ^ std::hash<Key>{}(pass)) * 1099511628211ULL;
    key = (key ^ stamp_of(standard_)) * 1099511628211ULL;
    for (const Mix::Part& part : m.parts) {
        uint32_t w = 0;
        std::memcpy(&w, &part.weight, sizeof w);
        key = (key ^ reinterpret_cast<std::uintptr_t>(part.look)) * 1099511628211ULL;
        key = (key ^ w) * 1099511628211ULL;
        key = (key ^ stamp_of(*part.look)) * 1099511628211ULL;
    }
    if (auto hit = uniform_memo_.find(key); hit != uniform_memo_.end()) {
        for (const UniformSet& u : hit->second) p.put(u.at, u.n, u.v);
        return;
    }
    if (uniform_memo_.size() > 256) uniform_memo_.clear();
    ++times_.uniforms_resolved;
    std::vector<UniformSet>& record = uniform_memo_[key];
    uniform_keys_.clear();
    const auto collect = [&](const LookState& l) {
        const Element* e = l.find(pass);
        if (!e) return;
        for (const auto& kv : e->params)
            if (is_uniform_key(kv.first) &&
                std::find(uniform_keys_.begin(), uniform_keys_.end(), kv.first) ==
                    uniform_keys_.end())
                uniform_keys_.push_back(kv.first);
    };
    collect(standard_);
    for (const Mix::Part& part : m.parts) collect(*part.look);
    vectors_.clear();
    for (Key k : uniform_keys_) {
        const std::string& name = k.str();
        const float v = static_cast<float>(fader_.value(m, pass, k, 0.0));
        const std::size_t dot = name.find('.');
        if (dot == std::string::npos) {
            p.set(name.c_str(), v);
            record.push_back(UniformSet{p.uniform(name.c_str()), 1, {v, 0, 0}});
            continue;
        }
        const std::string base = name.substr(0, dot);
        const char c = dot + 1 < name.size() ? name[dot + 1] : 'x';
        const int i = c == 'y' ? 1 : (c == 'z' ? 2 : 0);
        auto it = std::find_if(vectors_.begin(), vectors_.end(),
                               [&](const VectorUniform& u) { return u.name == base; });
        if (it == vectors_.end()) {
            vectors_.push_back(VectorUniform{base, {0, 0, 0}, 0});
            it = vectors_.end() - 1;
        }
        it->v[i] = v;
        it->n = std::max(it->n, i + 1);
    }
    for (const VectorUniform& u : vectors_) {
        if (u.n == 3) {
            p.set(u.name.c_str(), gl::Vec3{u.v[0], u.v[1], u.v[2]});
        } else if (u.n == 2) {
            p.set(u.name.c_str(), u.v[0], u.v[1]);
        } else {
            p.set(u.name.c_str(), u.v[0]);
        }
        record.push_back(UniformSet{p.uniform(u.name.c_str()), u.n, {u.v[0], u.v[1], u.v[2]}});
    }
}

const gl::Program* GLWorldView::program_for(const LookState& look, Key pass) {
    const std::string& vs = source(look, pass, look_keys::vs);
    const std::string& fs = source(look, pass, look_keys::fs);
    PassProgram& slot = resolved_[look.id()][pass];
    if (slot.program && slot.vs == vs && slot.fs == fs) return slot.program;
    slot.vs = vs;
    slot.fs = fs;
    slot.error.clear();
    slot.program = shared_program(vs, fs, look.id().str() + "." + pass.str(), slot.error);
    // A shader that will not build is reported by prepare(); the pass
    // falls back to the built-in rather than drawing nothing.
    if (!slot.program && &look != &standard_) slot.program = program_for(standard_, pass);
    return slot.program;
}

const std::string& GLWorldView::source(const LookState& look, Key pass, Key which) const {
    if (const Element* e = look.find(pass))
        if (e->params.has(which))
            if (const auto* s = std::get_if<std::string>(&e->params.get(which)))
                if (!s->empty()) return *s;
    const auto& b = builtin_.at(pass);
    return which == look_keys::vs ? b.first : b.second;
}

const gl::Program* GLWorldView::shared_program(const std::string& vs, const std::string& fs, const std::string& tag, std::string& error) {
    std::string key;
    key.reserve(vs.size() + fs.size() + 1);
    key += vs;
    key += '\0';
    key += fs;
    auto it = programs_.find(key);
    if (it != programs_.end()) return it->second.get();
    const auto t0 = std::chrono::steady_clock::now();
    std::unique_ptr<gl::Program> p;
    try {
        p = std::make_unique<gl::Program>(vs.c_str(), fs.c_str(), tag.c_str());
    } catch (const std::exception& e) {
        error = e.what();
    }
    stats_.compile_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
            .count();
    if (!p) return nullptr;
    ++stats_.programs;
    if (!preparing_) ++stats_.late;
    const gl::Program* made = programs_.emplace(std::move(key), std::move(p)).first->second.get();
    sources_of_[made] = {vs, fs};
    return made;
}

const gl::Program* GLWorldView::cutout_of(const gl::Program& p) {
    if (auto it = cutout_.find(&p); it != cutout_.end()) return it->second;
    const auto src = sources_of_.find(&p);
    const gl::Program* twin = nullptr;
    if (src != sources_of_.end()) {
        std::string fs = src->second.second;
        const std::size_t line = fs.find('\n');
        fs.insert(line == std::string::npos ? 0 : line + 1, "#define SG_CUTOUT\n");
        std::string error;
        twin = shared_program(src->second.first, fs, "cutout", error);
    }
    return cutout_[&p] = twin;
}

void GLWorldView::check_look(const LookState& look, std::vector<std::string>& out) {
    static const std::unordered_map<Key, std::vector<const char*>> required{
        {passes::shadow, {"uModel", "uLightViewProj"}},
        // Without the clip planes two glued rooms both draw their shared wall.
        {passes::scene, {"uModel", "uViewProj", "uClipCount"}},
        {passes::bright, {"uScene"}},
        {passes::blur, {"uSource", "uDirection"}},
        {passes::composite, {"uScene"}},
    };
    const std::string tag = "look " + look.id().str() + ": ";
    for (Key pass : passes::all()) {
        const gl::Program* p = program_for(look, pass);
        const PassProgram& slot = resolved_[look.id()][pass];
        if (!slot.error.empty()) {
            const std::string& e = slot.error;
            out.push_back(tag + pass.str() + " shader does not build, so the built-in is used - " +
                          e.substr(0, e.find('\n')));
            continue;
        }
        const Element* el = look.find(pass);
        const bool custom = el && (el->params.has(look_keys::vs) || el->params.has(look_keys::fs));
        if (custom)
            for (const char* name : required.at(pass))
                if (!p->has(name))
                    out.push_back(tag + "its " + pass.str() + " shader has no " + name +
                                  ", which the renderer sets on every draw");
        if (!el) continue;
        std::vector<std::string> named;  // a vector's three components are one name
        for (const auto& kv : el->params) {
            if (!is_uniform_key(kv.first)) continue;
            const std::string& name = kv.first.str();
            const std::string base = name.substr(0, name.find('.'));
            if (std::find(named.begin(), named.end(), base) != named.end()) continue;
            named.push_back(base);
            if (!p->has(base.c_str()))
                out.push_back(tag + pass.str() + " sets " + base +
                              ", which its shader does not have (misspelt, or declared "
                              "and never used)");
        }
    }
    // What it lays over its finished picture (deband, smear) is drawn by the
    // renderer's own pass: built now, and named if it does not build.
    if (const Element* c = look.find(passes::composite))
        if (c->params.num(Key{"deband"}, 0.0) > 0.0 || c->params.num(Key{"smear"}, 0.0) > 0.0) {
            std::string error;
            if (!finish_program(&error))
                out.push_back(tag + "its finish (deband, smear) does not build, so the picture is shown as composited - " +
                              error.substr(0, error.find('\n')));
        }
}

const char* GLWorldView::clip_uniform(int i) {
    static const auto names = [] {
        std::array<std::string, kMaxBounds> n;
        for (int b = 0; b < kMaxBounds; ++b)
            n[static_cast<std::size_t>(b)] = "uClip[" + std::to_string(b) + "]";
        return n;
    }();
    return names[static_cast<std::size_t>(i)].c_str();
}

const char* GLWorldView::shadow_uniform(std::size_t i, int field) {
    static const auto names = [] {
        std::array<std::array<std::string, 2>, kShadowMaps> n;
        for (std::size_t l = 0; l < kShadowMaps; ++l) {
            n[l][0] = "uShadowVP[" + std::to_string(l) + "]";
            n[l][1] = "uShadowBias[" + std::to_string(l) + "]";
        }
        return n;
    }();
    return names[i][static_cast<std::size_t>(field)].c_str();
}

const char* GLWorldView::light_uniform(std::size_t i, int field) {
    static const auto names = [] {
        static const char* fields[] = {"uLightPos", "uLightDir", "uLightColor", "uLightPower",
                                       "uCosInner", "uCosOuter", "uLightSun", "uLightFloor",
                                       "uLightIndirect", "uLightFalloff", "uLightGate", "uLightGateAxis",
                                       "uLightNear", "uLightOpen", "uLightScatter", "uLightFrame", "uLightRange",
                                       "uLightLayer", "uLightCube"};
        std::array<std::array<std::string, 19>, kMaxLights> n;
        for (std::size_t l = 0; l < kMaxLights; ++l)
            for (int f = 0; f < 19; ++f)
                n[l][static_cast<std::size_t>(f)] =
                    std::string(fields[f]) + "[" + std::to_string(l) + "]";
        return n;
    }();
    return names[i][static_cast<std::size_t>(field)].c_str();
}

}  // namespace sg::render
