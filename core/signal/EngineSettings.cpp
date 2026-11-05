#include "signal/EngineSettings.hpp"

#include <random>

#include "viz/NoteResponse.hpp"
#include "viz/Reactions.hpp"

namespace geode::signal {

EngineSettings globalSettings() {
    EngineSettings s;
    s.knobs = viz::macros::global();
    s.director = viz::director::global();
    s.notesOn = viz::note_response::globalEnabled();
    s.handMask = viz::hand::global();
    return s;
}

EngineSettings exportSettings(bool entitled, long long durationMs) {
    EngineSettings s = globalSettings();
    s.director = viz::director::forExport(viz::director::switchOn(), s.director.seed, entitled, durationMs);
    s.noteEntropy = 0;
    return s;
}

std::uint32_t SettingsLatch::pinForExport(bool entitled, long long durationMs) {
    const EngineSettings s = exportSettings(entitled, durationMs);
    std::lock_guard<std::mutex> guard(lock_);
    pin_ = s;
    return s.handMask;
}

bool SettingsLatch::pinned() const {
    std::lock_guard<std::mutex> guard(lock_);
    return pin_.has_value();
}

EngineSettings SettingsLatch::latch(bool reducedMotion) {
    std::optional<EngineSettings> pinned;
    {
        std::lock_guard<std::mutex> guard(lock_);
        pinned = pin_;
    }
    EngineSettings s;
    if (pinned) {
        s = *pinned;
    } else {
        s = globalSettings();
        if (!noteSeeded_ || s.director.seed != notePlaySeed_) {
            std::random_device device;
            noteEntropy_ = (static_cast<std::uint64_t>(device()) << 32) ^ device();
            notePlaySeed_ = s.director.seed;
            noteSeeded_ = true;
        }
        s.noteEntropy = noteEntropy_;
    }
    s.reducedMotion = reducedMotion;
    return s;
}

}  // namespace geode::signal
