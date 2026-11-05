#pragma once

#include <cstdint>

namespace geode::viz::frame {

// The frame grid of an offline (export) render: which instant frame N shows, and how much of the
// track the renderer's own sound engine must have heard before drawing it (#573, ADR 0003).
//
// Export must equal live. The screen's engine hears the ring as it fills and is stepped by each frame's
// dt; an export has no wall clock, so frame N is drawn at a time taken from N alone and its engine is fed
// exactly the track up to where frame N+1 begins, then the render steps it by that frame's dt (about
// 1/fps). Both are computed from the absolute frame index, never accumulated, so a three-hour render ends
// on the same instant and the same sample as its first frame predicts: there is no drift to correct.
//
// The grid is whole milliseconds (floor), as it always was in Kotlin (`frame * 1000 / fps`), so a render
// made after the port draws the same frames at the same times. The rate is a fraction num/den so a
// broadcast rate renders on its true grid: 29.97 is 30000/1001, 59.94 is 60000/1001, 30 is 30/1.
// All maths is int64; the products overflow only after more than a thousand years of video at 192 kHz.
struct OfflineRate {
    int64_t num = 0;
    int64_t den = 1;

    constexpr bool valid() const { return num > 0 && den > 0; }
};

// The render time of `frame`, in whole milliseconds from the range start (floor). 0 for a bad rate or a
// negative frame.
constexpr int64_t frameMs(int64_t frame, OfflineRate rate) {
    if (!rate.valid() || frame <= 0) return 0;
    return frame * 1000 * rate.den / rate.num;
}

// The same instant in seconds, as Renderer::render takes it.
constexpr double frameSeconds(int64_t frame, OfflineRate rate) {
    return static_cast<double>(frameMs(frame, rate)) / 1000.0;
}

// The audio cut: how many track frames (at `sampleRateHz`, counted from the range start) the engine must
// have heard before `frame` is drawn. Everything up to the start of the next video frame, so frame N's
// engine has heard the audio up to frame N's end and no further. 0 for an unknown rate (a track that has
// not decoded yet, or none): the engine stays silent rather than guessing.
constexpr int64_t audioUntil(int64_t frame, OfflineRate rate, int64_t sampleRateHz) {
    if (sampleRateHz <= 0 || frame < 0) return 0;
    return frameMs(frame + 1, rate) * sampleRateHz / 1000;
}

// The params an offline render pins whatever the keyframes say: quality never adapts downward in an
// export, because its wall-clock frame time is the encoder's pace, not the device's (the thermal half
// of the same rule is the offscreen tier, `geode_viz_prepare_offscreen`). A template so the host test
// can drive it with a stand-in for SceneParams.
template <class Params>
constexpr void pinOfflineParams(Params& p) {
    p.fluidAutoQuality = false;
}

}  // namespace geode::viz::frame
