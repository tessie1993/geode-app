#include "output/ExportFrame.hpp"

#include <utility>

#include "output/PreRoll.hpp"

namespace geode::output {

namespace {
constexpr double kNanosPerSecond = 1'000'000'000.0;
}  // namespace

ExportFrames::ExportFrames(int fps, int64_t rangeStartMs, TrackOpener open)
    : clock_(fps, 0), rangeStartMs_(rangeStartMs), open_(std::move(open)) {}

int64_t ExportFrames::begin(const ExportStart& start, ExportTarget& target) {
    target.prepareOffscreen();
    // Every frame steps the engine by exactly 1/fps, the first one included (#377).
    target.setFrameRate(clock_.fps());
    target.surface(start.width, start.height);
    if (!target.setScene(start.sceneId)) return -1;
    if (!start.milkPresetPath.empty()) target.loadMilkPreset(start.milkPresetPath);
    // Pinned before the look: the hand mask the pin returns is what the routes merge under (the export's, not the
    // live one), so live edits and the player moving on never reach the render.
    return target.pinEngineSettings(start.entitled, start.pinDurationMs);
}

bool ExportFrames::preRoll(ExportTarget& target) {
    const PreRoll plan = preRollFor(rangeStartMs_);
    {
        auto source = open_(plan.fromMs);
        if (!source) return false;
        audio::ingest::TrackPump warm(plan.fromMs, std::move(source));
        warm.deliverMs(plan.heardMs, target);
    }  // the pre-roll's decode closes before the range's opens: they read one descriptor
    if (!target.endPreRoll(rangeStartMs_)) return false;
    auto source = open_(rangeStartMs_);
    if (!source) return false;
    range_ = std::make_unique<audio::ingest::TrackPump>(rangeStartMs_, std::move(source));
    return true;
}

bool ExportFrames::frame(int64_t n, uint32_t targetFbo, ExportTarget& target) {
    // Exactly the audio up to the end of this frame, to the sample (#999 F6), then a render steps the engine by 1/fps.
    const bool heard = range_ && range_->deliverFrame(n, clock_.fps(), target);
    // The frame's exact time, not its whole millisecond (#377).
    target.render(static_cast<double>(clock_.ptsNs(n)) / kNanosPerSecond, targetFbo);
    return heard;
}

}  // namespace geode::output
