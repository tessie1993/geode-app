#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "audio/ingest/TrackPump.hpp"
#include "output/FrameClock.hpp"

// An export's render driven from inside core (ADR 0003, ADR 0006): the prepare order once, then one call per
// video frame, so live, export and loop renders share one rule and the shell only presents what is drawn.
//
// begin(): the renderer pinned for an export (full tier, cut transitions, fixed fluid quality), its fixed step of
// 1/fps from the first frame on (#377), the surface, the scene (false: it cannot draw it), the MilkDrop preset,
// and the engine settings pinned for the render's length (#350), whose hand mask the reactions merge under.
// The shell then applies its look, which needs that mask; preRoll() is the rest of the prepare: the engine hears
// the track before frame 0 (output/PreRoll, #999 F1), the pre-roll ends, and the range's own decode opens.
// frame(n): the audio up to the exact end of frame n (FrameClock::samplesThrough, #999 F6), then a draw at the
// frame's exact time, floor(n * 1e9 / fps) ns, which steps the engine by 1/fps.
//
// The renderer is behind ExportTarget and the track behind a TrackOpener, so the order is host-tested
// (core/tests/export_frame_test). One thread, the one holding the target's GL context.
namespace geode::output {

// The export's renderer (geode_viz behind core/api/geode_export_job_api.cpp).
class ExportTarget : public audio::ingest::TrackListener {
public:
    virtual void prepareOffscreen() = 0;
    virtual void setFrameRate(int fps) = 0;
    // The new GL surface and its size.
    virtual void surface(int width, int height) = 0;
    virtual bool setScene(const std::string& sceneId) = 0;
    virtual void loadMilkPreset(const std::string& path) = 0;
    // Pins the engine settings for durationMs; the hand mask the pin holds.
    virtual uint32_t pinEngineSettings(bool entitled, int64_t durationMs) = 0;
    // output::endPreRoll on the renderer's engine; false when it heard nothing.
    virtual bool endPreRoll(int64_t rangeStartMs) = 0;
    virtual void render(double timeSeconds, uint32_t targetFbo) = 0;
};

// What begin() sets up.
struct ExportStart {
    int width = 0;
    int height = 0;
    std::string sceneId;
    std::string milkPresetPath;  // empty: none
    bool entitled = false;       // the pinned director's free-taste rule (ProFeature.AUTO_DIRECTOR)
    int64_t pinDurationMs = 0;   // the render's length
};

// The track decoded from startMs; null when there is no audio in it this can read.
using TrackOpener = std::function<std::unique_ptr<audio::ingest::TrackSource>(int64_t startMs)>;

class ExportFrames {
public:
    ExportFrames(int fps, int64_t rangeStartMs, TrackOpener open);

    // The prepare order up to the look. The pinned hand mask, or -1 when the renderer cannot draw the scene.
    int64_t begin(const ExportStart& start, ExportTarget& target);
    // The pre-roll and the range's decode. False when the track cannot be opened or the engine heard nothing
    // (the render fails rather than export a picture the silence rule froze).
    bool preRoll(ExportTarget& target);
    // Frame n of the range into targetFbo (0: the default framebuffer). False once the track has run out before
    // the frame's end; the frame is drawn all the same, on what the engine has heard.
    bool frame(int64_t n, uint32_t targetFbo, ExportTarget& target);

    const FrameClock& clock() const { return clock_; }

private:
    FrameClock clock_;
    int64_t rangeStartMs_;
    TrackOpener open_;
    std::unique_ptr<audio::ingest::TrackPump> range_;
};

}  // namespace geode::output
