// The GL view's things drawn one by one: pictures standing up (sprites),
// meshes and their skins, lamps.
#include "sg/gl/World.hpp"

#include <algorithm>

#include "sg/domains/Texture.hpp"

namespace sg::render {

bool GLWorldView::is_sprite(const Element& e) {
    static const Key shape{"shape"};
    return e.params.is(shape, "sprite");
}

bool GLWorldView::bind_picture(const State& st, const std::string& name, bool data) {
    const auto* space = dynamic_cast<const Spatial3D*>(&st);
    const Spatial3D::Picture* pic = space ? space->picture(Key{name}) : nullptr;
    if (!pic || pic->w <= 0 || pic->h <= 0) return false;
    PictureTexture& t = (data ? data_textures_ : picture_textures_)[pic];
    if (!t.texture.valid() || t.texture.width() != pic->w || t.texture.height() != pic->h) {
        t.texture.create(pic->w, pic->h, /*mipmaps=*/false, /*srgb=*/!data, /*pixel=*/!data);
        t.revision = ~uint64_t{0};
    }
    if (t.revision != pic->revision) {
        t.texture.upload(pic->rgba);
        t.revision = pic->revision;
    }
    t.texture.bind(0);
    return true;
}

void GLWorldView::draw_sprite(const State& st, const Element& e) {
    if (!bind_picture(st, e.params.get_or<std::string>(Key{"picture"}, ""))) return;
    ++times_.draws;
    const gl::Vec3 c = (frame_matrix_ * box_matrix(st, e).m).transform_point({0, 0, 0});
    gl::Vec3 n, up;
    if (e.params.num(Key{"face"}, 0.0) > 0.5) {
        n = gl::normalize(cam_forward_ * -1.0f);
        up = gl::normalize(cam_up_ - n * gl::dot(cam_up_, n));
    } else {
        n = cam_eye_ - c;
        n.y = 0;
        n = gl::dot(n, n) > 1e-8f ? gl::normalize(n) : gl::Vec3{1, 0, 0};
        up = {0, 1, 0};
    }
    const gl::Vec3 side = gl::cross(n, up);  // the card's +z: to the eye's left
    const float w = static_cast<float>(e.params.num(keys::sx, 1.0)), h = static_cast<float>(e.params.num(keys::sy, 1.0));
    gl::Mat4 m;
    m.m[0] = n.x, m.m[1] = n.y, m.m[2] = n.z;
    m.m[4] = up.x * h, m.m[5] = up.y * h, m.m[6] = up.z * h;
    m.m[8] = side.x * w, m.m[9] = side.y * w, m.m[10] = side.z * w;
    m.m[12] = c.x, m.m[13] = c.y, m.m[14] = c.z;
    scene_->set("uModel", m);
    scene_->set("uTexModel", m);
    const float frames = static_cast<float>(std::max(1.0, e.params.num(Key{"frames"}, 1.0)));
    const float frame = std::fmod(std::max(0.0f, std::floor(static_cast<float>(e.params.num(Key{"frame"}, 0.0)))), frames);
    scene_->set("uUVRect", frame / frames, 0.0f, 1.0f / frames, 1.0f);
    scene_->set("uCutout", 1.0f);
    scene_->set("uAlbedo", gl::Vec3{1, 1, 1});
    scene_->set("uRoughness", 1.0f);
    scene_->set("uSurface", 0.0f);
    scene_->set("uEmissive", 0.0f);
    scene_->set("uHighlight", 0.0f);
    scene_->set("uMirror", 0.0f);
    scene_->set("uTexMix", 1.0f);
    scene_->set("uSkin", 0.0f);
    scene_->set("uScreenUV", 0.0f);
    scene_->set("uCRT", 0.0f);
    // `glow`: how much it lights itself - a lit thing, or one held up to
    // the eye, whatever the light where it is.
    scene_->set("uGlow", static_cast<float>(e.params.num(Key{"glow"}, 0.0)));
    quad_.draw();
    scene_->set("uUVRect", 0.0f, 0.0f, 0.0f, 0.0f);
    scene_->set("uCutout", 0.0f);
    scene_->set("uTexMix", 0.0f);
    scene_->set("uGlow", 0.0f);
}

void GLWorldView::draw_crate(const State& st, const Element& e) {
    ++times_.draws;
    // `straddle`: it hangs in a doorway, half in each room, and is lit as
    // that by one rule wherever it is drawn - in the room the eye is in, or
    // seen through the doorway - each point by the rooms it is in, as far as
    // it is in them (uStraddle): the two pictures of it are one, and shut or
    // ajar it is lit the same. Seen through the doorway it is drawn whole: no
    // plane cuts it - but a mirror's (the first the view is given, in a
    // mirror's view): what is behind the glass is not in it.
    struct Hung {
        const gl::Program* p;
        float on;
        int clips = -1;
        ~Hung() {
            if (on < 0.5f) return;
            p->set("uStraddle", 0.0f);
            if (clips >= 0) p->set("uClipCount", clips);
        }
    } hung{scene_, static_cast<float>(e.params.num(Key{"straddle"}, 0.0))};
    if (hung.on > 0.5f) {
        scene_->set("uStraddle", 1.0f);
        if (guest_pass_) hung.clips = clip_count_, scene_->set("uClipCount", mirroring_ ? std::min(clip_count_, 1) : 0);
    }
    // Its depth layer, for this draw alone.
    struct Layer {
        const gl::Program* p;
        ~Layer() { p->set("uDepthLayer", 0.0f); }
    } layer{scene_};
    scene_->set("uDepthLayer", static_cast<float>(e.params.num(Key{"depth_layer"}, 0.0)));
    set_model(box_matrix(st, e));
    scene_->set("uAlbedo", color_of(e, {0.8f, 0.5f, 0.25f}));
    scene_->set("uRoughness", static_cast<float>(e.params.num(Key{"roughness"}, 0.6)));
    scene_->set("uSurface", static_cast<float>(e.params.num(Key{"surface"}, 3.0)));
    scene_->set("uEmissive", static_cast<float>(e.params.num(Key{"emissive"}, 0.0)));
    scene_->set("uHighlight", e.id == highlight_ ? 1.0f : 0.0f);
    scene_->set("uGlow", 0.0f);
    // `mirror`: how much of the real sky it reflects, rather than the
    // light from all round - glossy stone, still water, under a sky.
    const float mirror = static_cast<float>(e.params.num(Key{"mirror"}, 0.0));
    scene_->set("uMirror", mirror);
    // `metal`: how metal it is, 0..1 - for this draw alone (a texture's
    // surface map says instead, where it has one).
    struct Metal {
        const gl::Program* p;
        ~Metal() { p->set("uMetal", 0.0f); }
    } metal{scene_};
    scene_->set("uMetal", static_cast<float>(std::clamp(e.params.num(Key{"metal"}, 0.0), 0.0, 1.0)));
    // A land's water, how deep; a road's lines.
    struct Land {
        GLWorldView* view;
        bool on;
        float lines;
        ~Land() {
            if (on) view->unbind_land();
            if (lines > 0.5f) view->scene_->set("uLines", 0.0f);
        }
    } land{this, false, static_cast<float>(e.params.num(Key{"lines"}, 0.0))};
    if (land.lines > 0.5f) scene_->set("uLines", 1.0f);
    if (e.params.has(Key{"splat"}) && bind_land(st, e)) {
        land.on = true;
        scene_->set("uTexMix", 0.0f);
        scene_->set("uSkin", 0.0f);
        shape_of(st, e).draw();
        if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
        return;
    }
    // A picture of its state's, tiled over the world - or, if it says `uv`,
    // worn by its faces' own places on it (a model made with them).
    if (e.params.has(Key{"skin"}) && bind_picture(st, e.params.get_or<std::string>(Key{"skin"}, ""))) {
        scene_->set("uTexMix", 1.0f);
        scene_->set("uSkin", e.params.num(Key{"uv"}, 0.0) > 0.5 ? 0.0f : 2.0f);
        scene_->set("uTile", static_cast<float>(e.params.num(Key{"tile"}, 1.0)));
        scene_->set("uScreenUV", 0.0f);
        scene_->set("uCRT", 0.0f);
        shape_of(st, e).draw();
        scene_->set("uSkin", 0.0f);
        scene_->set("uTexMix", 0.0f);
        if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
        return;
    }
    // A surface bound to a mesh is its skin: an atlas, a cell a face - or a
    // texture worn by a thing it is part of.
    const Element* holder = skin_holder(st, e);
    auto skin = holder ? surfaces_.find(holder->id) : surfaces_.end();
    if (skin != surfaces_.end()) {
        BoundSurface& bound = skin->second;
        Surface2D& surf = *bound.surface;
        upload_skin(bound);
        bound.texture.bind(0);
        scene_->set("uTexMix", 1.0f);
        scene_->set("uSkin", 1.0f);
        scene_->set("uScreenUV", 0.0f);
        scene_->set("uCRT", 0.0f);
        // A texture says how it is worn: tiled round the thing by the metre,
        // and how softly a curve goes from one way's cell to the next.
        const auto* tex = dynamic_cast<const Texture*>(&surf);
        if (tex) {
            const Element& m = tex->map();
            const SkinFrame& f = skin_frame(st, *holder);
            scene_->set("uSkinFramed", 1.0f);
            scene_->set("uSkinFrame", f.to_unit);
            scene_->set("uSkinSize", f.size);
            scene_->set("uSkinTile", static_cast<float>(m.params.num("tile", 0.0)));
            scene_->set("uSkinBlend", static_cast<float>(m.params.num("blend", 0.0)));
            scene_->set("uSkinRelief", static_cast<float>(m.params.num("relief", 0.0)));
            set_skin_maps(*scene_, &bound);
        }
        shape_of(st, e).draw();
        if (tex) scene_->set("uSkinFramed", 0.0f), scene_->set("uSkinTile", 0.0f), scene_->set("uSkinBlend", 0.0f), scene_->set("uSkinRelief", 0.0f), set_skin_maps(*scene_, nullptr);
        scene_->set("uSkin", 0.0f);
        scene_->set("uTexMix", 0.0f);
        if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
        return;
    }
    scene_->set("uTexMix", 0.0f);
    shape_of(st, e).draw();
    if (mirror != 0.0f) scene_->set("uMirror", 0.0f);
}

void GLWorldView::draw_lamp(const Spatial3D& world, const Element& e) {
    // A sun hangs from nothing; nor does a lamp that says it is no fixture.
    if (e.params.num(Key{"fixture"}, 1.0) < 0.5 || e.params.num(Key{"sun"}, 0.0) > 0.5) return;
    // (Seen from the lamp itself - its cube of distances - its own fitting
    // is not in the way of its light.)
    if (e.id == own_lamp_) return;
    const gl::Vec3 pos = to_vec3(pose_of(world, e).position);
    const gl::Vec3 color = color_of(e, {1.0f, 0.93f, 0.82f});
    const float room_h = static_cast<float>(world.params().num(Key{"room_h"}, 4.0));

    draw_solid(room_local(gl::Mat4::translate({pos.x, pos.y + 0.12f, pos.z}) *
                   gl::Mat4::scale({0.62f, 0.22f, 0.62f})),
               {0.12f, 0.11f, 0.10f}, 0.4f, 0.0f);
    // The shade glows while the lamp is on, and is only glass when it is off.
    const float lit = static_cast<float>(std::min(1.0, e.params.num(keys::intensity, 1.0) * 2.0));
    draw_solid(room_local(gl::Mat4::translate(pos) * gl::Mat4::scale({0.30f, 0.16f, 0.30f})), color, 0.2f,
               0.0f, 6.0f * lit + 0.02f);
    const float stem = std::max(0.05f, room_h - pos.y - 0.2f);
    draw_solid(room_local(gl::Mat4::translate({pos.x, pos.y + 0.2f + stem * 0.5f, pos.z}) *
                   gl::Mat4::scale({0.035f, stem, 0.035f})),
               {0.09f, 0.09f, 0.10f}, 0.8f, 0.0f);
}

}  // namespace sg::render
