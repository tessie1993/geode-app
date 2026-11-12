#include "viz/ModControls.hpp"

#include <algorithm>

namespace geode::viz::modcontrols {

unsigned ofWave(const LfoConfig& c) {
    unsigned shows = c.target == LfoTarget::Macro ? kKnob : 0u;
    if (c.source != ModSource::Lfo) return shows;
    shows |= kWave;
    // LfoEngine::oscillator follows the music clock only for a beat-timed slot on no moment.
    const bool beatTimed = c.beats >= 1.0f && c.moment < 0;
    if (!beatTimed) shows |= kRate;
    return shows;
}

unsigned ofEnvelope(const AdsrConfig& c) {
    const bool onKnob = std::find(c.targets.begin(), c.targets.end(), LfoTarget::Macro) != c.targets.end();
    return onKnob ? kKnob : 0u;
}

}  // namespace geode::viz::modcontrols
