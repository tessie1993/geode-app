#include "viz/resolver/EngineSettings.hpp"

#include "viz/modulation/Reactions.hpp"

namespace geode::signal {

EngineSettings globalSettings() {
    EngineSettings s;
    s.bank = viz::macrobank::global();
    s.director = viz::director::global();
    s.directorTuning = viz::director::tuning();
    s.reach = viz::director::reach();
    s.kept = viz::director::kept();
    s.palettes = viz::palette::global();
    s.handMask = viz::hand::global();
    return s;
}

EngineSettings exportSettings(bool entitled, long long durationMs) {
    EngineSettings s = globalSettings();
    s.director = viz::director::forExport(viz::director::switchOn(), s.director.seed, entitled, durationMs);
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

std::uint64_t SettingsLatch::directorSeed() const {
    std::lock_guard<std::mutex> guard(lock_);
    return pin_ ? pin_->director.seed : viz::director::global().seed;
}

std::uint32_t SettingsLatch::handMask() const {
    std::lock_guard<std::mutex> guard(lock_);
    return pin_ ? pin_->handMask : viz::hand::global();
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
    }
    s.reducedMotion = reducedMotion;
    return s;
}

}  // namespace geode::signal
