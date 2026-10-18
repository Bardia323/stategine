// The GL view's own clock on the card: what each stage of a frame cost the
// card, as the card says, read a few frames late so nothing waits for it
// (GLWorldView::set_card_timing).
#include "sg/gl/World.hpp"

#include <cmath>

namespace sg::render {

GLWorldView::CardFrame::~CardFrame() {
    if (!gl::glDeleteQueries) return;
    if (stamp[0] != 0) gl::glDeleteQueries(kCardPoints, stamp);
    if (!doors.empty()) gl::glDeleteQueries(static_cast<gl::GLsizei>(doors.size()), doors.data());
}

void GLWorldView::card_doors(const gl::Mat4& view_proj, bool reversed) {
    if (!card_now_ || card_doorways_.empty()) {
        card_doorways_.clear();
        return;
    }
    CardFrame& f = *card_now_;
    if (f.doors.size() < card_doorways_.size()) {
        const std::size_t had = f.doors.size();
        f.doors.resize(card_doorways_.size());
        gl::glGenQueries(static_cast<gl::GLsizei>(f.doors.size() - had), f.doors.data() + had);
    }
    // Each doorway's quad, where it was laid, tested against all that is
    // drawn now - nothing of it written.
    const gl::Program& depth = *program_for(post_.shown(), passes::shadow);
    depth.use();
    depth.set("uInstanced", 0);
    depth.set("uCasterSide", 0.0f, 0.0f, 0.0f, 1.0f);
    depth.set("uLightViewProj", view_proj);
    gl::glColorMask(0, 0, 0, 0);
    gl::glDepthMask(0);
    gl::glEnable(gl::GL_DEPTH_TEST);
    gl::glDepthFunc(reversed ? gl::GL_GEQUAL : gl::GL_LEQUAL);
    gl::glDisable(gl::GL_CULL_FACE);
    for (std::size_t i = 0; i < card_doorways_.size(); ++i) {
        // (Three centimetres nearer the eye than it was laid: a panel is laid
        // a little before what it coincides with, and its quad, tested where
        // it lies, would be behind its own picture. What stands nearer than
        // that still hides it.)
        const gl::Mat4& m = card_doorways_[i];
        const gl::Vec3 at{m.m[12], m.m[13], m.m[14]};
        const gl::Vec3 to_eye = cam_eye_ - at;
        const float away = std::sqrt(gl::dot(to_eye, to_eye));
        depth.set("uModel", away > 1e-4f ? gl::Mat4::translate(to_eye * (0.03f / away)) * m : m);
        gl::glBeginQuery(gl::GL_ANY_SAMPLES_PASSED, f.doors[i]);
        quad_.draw();
        gl::glEndQuery(gl::GL_ANY_SAMPLES_PASSED);
    }
    f.doors_asked = card_doorways_.size();
    gl::glColorMask(1, 1, 1, 1);
    gl::glDepthMask(1);
    gl::glDepthFunc(reversed ? gl::GL_GREATER : gl::GL_LESS);
    card_doorways_.clear();
    if (scene_) scene_->use();
}

void GLWorldView::set_card_timing(bool on) {
    card_on_ = on;
    if (on) return;
    // Off: what was asked is forgotten, and nothing is said to be known.
    for (CardFrame& f : card_frames_) f.frame = 0;
    card_now_ = nullptr;
    card_ = CardTimes{};
}

void GLWorldView::card_stamp(CardPoint at) {
    if (!card_now_) return;
    gl::glQueryCounter(card_now_->stamp[at], gl::GL_TIMESTAMP);
    card_now_->laid[at] = true;
}

void GLWorldView::card_read() {
    // The oldest frame the card has finished first: its last stamp answered
    // means every stamp before it is too.
    for (;;) {
        CardFrame* oldest = nullptr;
        for (CardFrame& f : card_frames_)
            if (f.frame != 0 && f.laid[kCardPost] && (!oldest || f.frame < oldest->frame)) oldest = &f;
        if (!oldest) return;
        gl::GLint ready = 0;
        gl::glGetQueryObjectiv(oldest->stamp[kCardPost], gl::GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) return;
        std::uint64_t ns[kCardPoints] = {};
        for (int k = 0; k < kCardPoints; ++k)
            if (oldest->laid[k]) gl::glGetQueryObjectui64v(oldest->stamp[k], gl::GL_QUERY_RESULT, &ns[k]);
        const auto ms = [&](CardPoint from, CardPoint to) {
            if (!oldest->laid[from] || !oldest->laid[to] || ns[to] < ns[from]) return 0.0;
            return static_cast<double>(ns[to] - ns[from]) * 1e-6;
        };
        card_.relit = ms(kCardStart, kCardRelit);
        card_.feeds = ms(kCardRelit, kCardFeeds);
        card_.views = ms(kCardFeeds, kCardViews);
        card_.scene = ms(kCardViews, kCardScene);
        card_.mirrors = ms(kCardViews, kCardMirrors);
        card_.shadows = ms(kCardShadowsFrom, kCardShadowsTo);
        card_.air = ms(kCardShadowsTo, kCardAir);
        card_.lit = ms(kCardAir, kCardScene);
        card_.post = ms(kCardScene, kCardPost);
        card_.total = ms(kCardStart, kCardPost);
        // (Asked before the frame's last stamp: answered by now.)
        card_.doors_laid = static_cast<int>(oldest->doors_asked);
        card_.doors_unseen = 0;
        for (std::size_t i = 0; i < oldest->doors_asked; ++i) {
            gl::GLint passed = 1;
            gl::glGetQueryObjectiv(oldest->doors[i], gl::GL_QUERY_RESULT, &passed);
            if (passed == 0) ++card_.doors_unseen;
        }
        oldest->doors_asked = 0;
        card_.frame = oldest->frame;
        oldest->frame = 0;
    }
}

void GLWorldView::card_begin() {
    card_now_ = nullptr;
    if (!card_on_) return;
    card_read();
    // A free slot; if the card is so far behind that none is, the oldest
    // frame's stamps are let go unread (its times are never known).
    CardFrame* slot = nullptr;
    for (CardFrame& f : card_frames_)
        if (f.frame == 0) {
            slot = &f;
            break;
        }
    if (!slot) {
        slot = &card_frames_[0];
        for (CardFrame& f : card_frames_)
            if (f.frame < slot->frame) slot = &f;
    }
    if (slot->stamp[0] == 0) gl::glGenQueries(kCardPoints, slot->stamp);
    for (bool& laid : slot->laid) laid = false;
    slot->frame = frame_count_ + 1;  // (the frame about to be counted)
    card_now_ = slot;
}

}  // namespace sg::render
