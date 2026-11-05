#include "viz/OutputProbe.hpp"

#include <cstring>

namespace geode::viz {

bool OutputProbe::ensure() {
    if (tex_ != 0) return true;
    // A driver that cannot build it once will not build it next frame either: measure nothing rather
    // than retry every frame. The limiter then never holds, which is the old behaviour, not a new risk.
    if (failed_) return false;
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexStorage2D(GL_TEXTURE_2D, kReadLevel + 1, GL_RGBA8, kBlitSide, kBlitSide);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &blitFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, blitFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    const bool blitOk = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glGenFramebuffers(1, &readFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, readFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, kReadLevel);
    const bool readOk = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    for (Slot& s : slots_) {
        glGenBuffers(1, &s.pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, kBytes, nullptr, GL_STREAM_READ);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!blitOk || !readOk) {
        release();
        failed_ = true;
        return false;
    }
    return true;
}

void OutputProbe::capture(GLuint targetFbo, int width, int height, const safety::Grade& grade, double drawnAt) {
    if (width <= 0 || height <= 0 || pending_ >= kSlots || !ensure()) return;
    Slot& slot = slots_[static_cast<size_t>(next_)];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, targetFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blitFbo_);
    glBlitFramebuffer(0, 0, width, height, 0, 0, kBlitSide, kBlitSide, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo_);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, kGrid, kGrid, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    slot.grade = grade;
    slot.drawnAt = drawnAt;
    if (slot.fence != nullptr) {
        next_ = (next_ + 1) % kSlots;
        pending_++;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, width, height);
}

bool OutputProbe::poll(safety::FrameLight& light, safety::Grade& grade, double& drawnAt) {
    if (pending_ == 0) return false;
    Slot& slot = slots_[static_cast<size_t>((next_ - pending_ + kSlots) % kSlots)];
    // Timeout 0: only ask whether it has landed, never wait for it.
    const GLenum state = glClientWaitSync(slot.fence, 0, 0);
    if (state != GL_ALREADY_SIGNALED && state != GL_CONDITION_SATISFIED) {
        if (state != GL_WAIT_FAILED) return false;
    }
    glDeleteSync(slot.fence);
    slot.fence = nullptr;
    pending_--;
    if (state == GL_WAIT_FAILED) return false;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
    const void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, kBytes, GL_MAP_READ_BIT);
    bool ok = false;
    if (mapped != nullptr) {
        std::memcpy(scratch_.data(), mapped, kBytes);
        ok = glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE;
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!ok) return false;
    light = safety::measureGrid(scratch_.data(), kGrid);
    grade = slot.grade;
    drawnAt = slot.drawnAt;
    return true;
}

void OutputProbe::release() {
    for (Slot& s : slots_) {
        if (s.fence != nullptr) glDeleteSync(s.fence);
        if (s.pbo != 0) glDeleteBuffers(1, &s.pbo);
    }
    if (readFbo_ != 0) glDeleteFramebuffers(1, &readFbo_);
    if (blitFbo_ != 0) glDeleteFramebuffers(1, &blitFbo_);
    if (tex_ != 0) glDeleteTextures(1, &tex_);
    forget();
}

void OutputProbe::forget() {
    for (Slot& s : slots_) s = Slot{};
    tex_ = 0;
    blitFbo_ = 0;
    readFbo_ = 0;
    next_ = 0;
    pending_ = 0;
}

}  // namespace geode::viz
