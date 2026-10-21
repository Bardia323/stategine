// The eye's lens: what its camera says of it, and the passes that make the
// picture as that lens and shutter would have taken it - out of focus by
// distance as a thin lens is (its depth of field), and blurred along the
// way the picture moved while the shutter was open (README *The lens*).
// The renderer's, as its other passes are: it reads the camera and writes
// nothing of the world.
#include "sg/gl/World.hpp"

namespace sg::render {

void GLWorldView::lens_of(const Spatial3D& world, const Camera& eye, float aspect) {
    lens_ = Lens{};
    // The eye's own view, seen in perspective: not one drawn for another (a
    // doorway's, a screen's), baked or of surfaces only.
    if (root_ || baking_ || surface_only_ != 0 || eye.ortho > 0.0f) return;
    // (The camera it is seen from: a hung one the room says it is seen from,
    // or its own.)
    const Element* cam = eye_override_ ? eye_override_ : (world.find(world.camera_id()) ? &world.camera() : nullptr);
    if (!cam) return;
    const Params& p = cam->params;
    // The depth of field: a thin lens at `fstop` (its f-number). Its focal
    // length is the camera's `focal` (mm) - its sensor then as big as that
    // and the view's fov make it - or what the fov makes of a sensor
    // `sensor` mm wide (36: a full frame), as tall as the picture makes it.
    const double stop = p.num(Key{"fstop"}, 0.0);
    if (stop > 0.0 && eye.fov > 0.0f) {
        const double tan_half = std::tan(static_cast<double>(eye.fov) * 0.5);
        const double focal_mm = p.num(Key{"focal"}, 0.0);
        double f = 0.0, sensor_h = 0.0;
        if (focal_mm > 0.0) {
            f = focal_mm * 1e-3;
            sensor_h = 2.0 * f * tan_half;
        } else {
            sensor_h = std::max(p.num(Key{"sensor"}, 36.0), 1e-3) * 1e-3 / std::max(static_cast<double>(aspect), 1e-3);
            f = 0.5 * sensor_h / tan_half;
        }
        lens_.aperture = static_cast<float>(f / stop / sensor_h);
        lens_.focal = static_cast<float>(f);
        // Focused where it says (`focus_distance`, metres), or on the thing it
        // says it keeps sharp (`focus`, `focus_up` metres over where it
        // stands) - along the view, as far off as it is - or, saying
        // neither, on what is in the middle of the picture.
        double s = p.num(Key{"focus_distance"}, 0.0);
        if (s <= 0.0)
            if (const std::string* on = p.text(Key{"focus"}))
                if (const Element* e = world.find(Key{*on}); e && e->alive) {
                    const Vec3d at = world_position(world, *e) + Vec3d{0.0, p.num(Key{"focus_up"}, 1.0), 0.0};
                    s = (static_cast<float>(at.x) - eye.eye.x) * eye.forward.x + (static_cast<float>(at.y) - eye.eye.y) * eye.forward.y +
                        (static_cast<float>(at.z) - eye.eye.z) * eye.forward.z;
                    s = std::max(s, 1e-3);
                }
        lens_.focus = static_cast<float>(std::max(s, 0.0));
    }
    // The shutter: open for `shutter` of each frame (0.5: a 180 degree
    // shutter), or for `shutter_time` seconds - never longer than the frame.
    // What moves is blurred by how far it went in that time: the frame's own
    // time (set_frame_delta) says how long that is. A frame told no time (a
    // still, drawn alone) is a still: no blur.
    if (eye_view() && dt_ > 0.0) {
        const double open = p.num(Key{"shutter_time"}, 0.0) > 0.0 ? p.num(Key{"shutter_time"}, 0.0) / dt_ : p.num(Key{"shutter"}, 0.0);
        lens_.shutter = static_cast<float>(std::clamp(open, 0.0, 1.0));
    }
}

void GLWorldView::run_lens() {
    if (!lens_setup_prog_) {
        lens_setup_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::lens_setup_fs(), "lens setup");
        lens_fill_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::lens_fill_fs(), "lens fill");
        lens_tiles_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::lens_tiles_fs(), "lens tiles");
        lens_spread_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::lens_spread_fs(), "lens spread");
        lens_bokeh_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::lens_bokeh_fs(), "lens bokeh");
        lens_merge_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::lens_merge_fs(), "lens merge");
        lens_motion_prog_ = std::make_unique<gl::Program>(gl::post_vs(), gl::lens_motion_fs(), "lens motion");
    }
    const int w = scene_src_->width(), h = scene_src_->height();
    const int hw = (w + 1) / 2, hh = (h + 1) / 2;
    const int tw = (w + kLensTile - 1) / kLensTile, th = (h + kLensTile - 1) / kLensTile;
    const auto sized = [](gl::RenderTarget& t, int tw_, int th_) {
        if (!t.valid() || t.width() != tw_ || t.height() != th_) t.create(tw_, th_, gl::GL_RGBA16F, 0, false);
    };
    // How the view moved since the last frame: found only if the last was
    // drawn with the shutter open too, of a world this one goes on from.
    gl::Mat4 reproject;
    const bool moving = lens_.shutter > 0.0f && lens_held_ &&
                        reprojection(lens_last_vp_, lens_last_world_, lens_last_rooms_, lens_last_eye_, reproject);
    lens_held_ = lens_.shutter > 0.0f;
    lens_last_vp_ = taa_vp_;
    lens_last_world_ = taa_world_;
    lens_last_rooms_ = taa_rooms_;
    lens_last_eye_ = taa_eye_;
    const bool dof = lens_.aperture > 0.0f;
    const float shutter = moving ? lens_.shutter : 0.0f;
    if (!dof && shutter <= 0.0f) return;

    // (Each picture made before any is bound: making one binds it.)
    gl::glActiveTexture(gl::GL_TEXTURE0);
    if (dof) sized(lens_half_, hw, hh), sized(lens_fill_, hw, hh), sized(lens_bokeh_, hw, hh), sized(lens_sharp_, w, h);
    if (shutter > 0.0f) sized(lens_moved_, w, h);
    sized(lens_tiles_, tw, th);
    sized(lens_spread_, tw, th);
    gl::glDisable(gl::GL_DEPTH_TEST);
    gl::glDisable(gl::GL_BLEND);
    // Units: 0 the picture, 1 the depth, 2 the half picture, 3 the squares
    // looked across, 4 the out-of-focus picture, 5 what of it is out of focus.
    depth_.bind_depth(1);
    const auto lens = [&](const gl::Program& p) {
        p.use();
        p.set("uDepth", 1);
        p.set("uNearFar", view_.znear, view_.zfar);
        p.set("uReversedZ", gl::reversed_depth() ? 1.0f : 0.0f);
        // (Its circles in the picture's pixels; the widest a part of its height.)
        p.set("uLens", lens_.aperture * static_cast<float>(h), lens_.focal, lens_.focus, 0.08f * static_cast<float>(h));
        p.set("uReproject", reproject);
        p.set("uShutter", shutter);
        p.set("uFull", static_cast<float>(w), static_cast<float>(h));
        p.set("uMostMove", 2.0f * static_cast<float>(kLensTile));
    };
    if (dof) {
        lens_half_.bind();
        lens(*lens_setup_prog_);
        lens_setup_prog_->set("uScene", 0);
        lens_setup_prog_->set("uSrcTexel", 1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
        scene_src_->bind_color(0);
        screen_.draw();
        lens_half_.bind_color(2);
        lens_fill_.bind();
        lens_fill_prog_->use();
        lens_fill_prog_->set("uHalf", 2);
        screen_.draw();
        gl::glActiveTexture(gl::GL_TEXTURE0 + 5u);
        lens_fill_.mipmap();  // (and bound at 5)
    }
    // How far each square of the picture must look: its widest circle, its
    // longest motion, and its neighbours'.
    lens_tiles_.bind();
    lens(*lens_tiles_prog_);
    lens_tiles_prog_->set("uHalf", 2);
    lens_tiles_prog_->set("uDof", dof ? 1.0f : 0.0f);
    screen_.draw();
    lens_spread_.bind();
    lens_spread_prog_->use();
    lens_spread_prog_->set("uTiles", 3);
    lens_tiles_.bind_color(3);
    screen_.draw();
    lens_spread_.bind_color(3);

    const gl::RenderTarget* src = scene_src_;
    if (dof) {
        lens_bokeh_.bind();
        lens_bokeh_prog_->use();
        lens_bokeh_prog_->set("uHalf", 2);
        lens_bokeh_prog_->set("uSpread", 3);
        lens_bokeh_prog_->set("uFill", 5);
        lens_bokeh_prog_->set("uTexel", 1.0f / static_cast<float>(hw), 1.0f / static_cast<float>(hh));
        screen_.draw();
        lens_sharp_.bind();
        lens(*lens_merge_prog_);
        lens_merge_prog_->set("uScene", 0);
        lens_merge_prog_->set("uHalf", 2);
        lens_merge_prog_->set("uBokeh", 4);
        src->bind_color(0);
        lens_bokeh_.bind_color(4);
        screen_.draw();
        src = &lens_sharp_;
    }
    if (shutter > 0.0f) {
        lens_moved_.bind();
        lens(*lens_motion_prog_);
        lens_motion_prog_->set("uScene", 0);
        lens_motion_prog_->set("uSpread", 3);
        src->bind_color(0);
        screen_.draw();
        src = &lens_moved_;
    }
    gl::glActiveTexture(gl::GL_TEXTURE0);
    scene_src_ = src;
}

}  // namespace sg::render
