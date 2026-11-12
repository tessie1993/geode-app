#pragma once
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdint>
#include <memory>
#include <mutex>

#include "viz/frame/CastTeeRule.hpp"

struct AHardwareBuffer;

namespace geode::viz::frame {

// Cast's tee: the stage's finished picture, handed to the cast stream's encoder (rules in CastTeeRule.hpp).
//
// The hand-off is through tee::kSlots square AHardwareBuffers as wide as the stream's frame, each an EGLImage that
// both GL contexts (the stage's and the cast thread's) bind as a texture: the stage blits its composite into
// one after the composite and before the UI blur, as Record's tap does, cropped to the frame's shape (tee::place;
// a portrait stage to the frame's transpose, unturned); the cast thread blits the newest into the encoder's
// surface, or draws it a quarter turn counter-clockwise when it was a portrait stage (no blit turns a picture).
// An EGL fence after each blit orders the
// two GPUs' work: the reader waits on the writer's fence before it samples a slot, the writer on the reader's
// before it overwrites one; neither waits long (the stage gives up after 2 ms and skips the copy).
//
// Why not Record's own tap (an EGL surface on the encoder's window, made on the stage's context)? One window
// takes one producer at a time, so the background fallback renderer could not draw into the same encoder
// without a cross-thread surface hand-over, and the encoder's back-pressure (a full input queue blocks
// eglSwapBuffers) would stall the stage's GL thread. Here the encoder surface stays the cast thread's for the
// whole stream: its clock, PTS and audio cut never change when the picture's source does, so nothing restarts.
//
// Threads: publish() on the stage's GL thread with its context current; draw() and releaseReader() on the cast
// thread with its context current; releaseWriter() on the stage's thread. The mailbox is the only shared state
// and sits behind one mutex held for a few instructions. EGL images, fences and the buffers are display-wide and
// freed by the destructor from any thread; each context's textures and framebuffers by its own release call.
class CastTee {
public:
    CastTee(int width, int height, int fps);
    ~CastTee();
    CastTee(const CastTee&) = delete;
    CastTee& operator=(const CastTee&) = delete;

    // False when the buffers could not be made (the cast then renders as before).
    bool ok() const { return ok_; }
    int width() const { return width_; }
    int height() const { return height_; }

    // The stage: copies sourceFbo (its composite, sourceWidth x sourceHeight) when a cast frame is due.
    void publish(GLuint sourceFbo, int sourceWidth, int sourceHeight, int64_t nowNs);
    // The cast: blits the stage's newest fresh copy into framebuffer 0 (the encoder's surface), width x height.
    // False when there is none: the stage is not drawing, and the cast's own renderer draws this frame.
    bool draw(int64_t nowNs, int width, int height);
    void releaseReader();
    void releaseWriter();

    // The tee the stage publishes into: the running cast's, or null. Any thread.
    static std::shared_ptr<CastTee> current();
    static void setCurrent(std::shared_ptr<CastTee> tee);

private:
    struct Side {
        EGLContext context = EGL_NO_CONTEXT;
        GLuint texture[tee::kSlots] = {0, 0, 0};
        GLuint framebuffer[tee::kSlots] = {0, 0, 0};
    };

    bool images();
    bool bindSide(Side& side, int slot);
    void forget(Side& side);
    void freeSide(Side& side);
    // Waits for and destroys a fence; false when it timed out (it is then kept for the next try).
    bool settle(void*& fence, int64_t timeoutNs);
    // The cast thread's quarter-turn draw of a portrait stage's slot into framebuffer 0; false without a program.
    bool buildTurn();
    bool drawTurned(int slot);

    // The turn's program, in the cast thread's context.
    struct Turn {
        EGLContext context = EGL_NO_CONTEXT;
        GLuint program = 0;
        GLuint vao = 0;
        GLint tex = -1;
        GLint scale = -1;
        GLint max = -1;
        bool failed = false;
    };

    const int width_;
    const int height_;
    const int side_;  // the slots' edge: the frame's longer side
    const int fps_;
    bool ok_ = false;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    AHardwareBuffer* buffers_[tee::kSlots] = {nullptr, nullptr, nullptr};
    void* images_[tee::kSlots] = {nullptr, nullptr, nullptr};   // EGLImageKHR
    std::mutex lock_;
    tee::Mailbox box_;                                          // lock_
    void* written_[tee::kSlots] = {nullptr, nullptr, nullptr};  // EGLSyncKHR, lock_: the stage's copy is done
    void* read_[tee::kSlots] = {nullptr, nullptr, nullptr};     // EGLSyncKHR, lock_: the cast's blit is done
    bool turned_[tee::kSlots] = {false, false, false};          // lock_: the slot holds a portrait stage
    Side writer_;   // stage thread only
    Side reader_;   // cast thread only
    Turn turn_;     // cast thread only
};

}  // namespace geode::viz::frame
