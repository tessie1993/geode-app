#pragma once
#include <algorithm>

#include "sound/SoundFrame.h"
#include "viz/Params.hpp"

namespace geode::viz {

// The per-scene band trims (Bass, Mid and Treble gain) on the sound frame a scene and the resolver hear:
// each band's level, att envelope, shaped low/middle/high, its ~3 s presence and its onset flux. Every
// band read a style makes follows the trim, so the three controls reach every style that hears a band.
// The clocks, counts and the absolute loudness stay the music's own. Identity at 1.
inline GeodeSoundFrame bandTrimmed(const GeodeSoundFrame& f, const SceneParams& p) {
    if (p.bassGain == 1.0f && p.midGain == 1.0f && p.trebGain == 1.0f) return f;
    GeodeSoundFrame out = f;
    const auto unit = [](float v, float g) { return std::clamp(v * g, 0.0f, 1.0f); };
    out.bass = f.bass * p.bassGain;
    out.bassAtt = f.bassAtt * p.bassGain;
    out.low = unit(f.low, p.bassGain);
    out.bassPresence = unit(f.bassPresence, p.bassGain);
    out.fluxLow = f.fluxLow * p.bassGain;
    out.mid = f.mid * p.midGain;
    out.midAtt = f.midAtt * p.midGain;
    out.middle = unit(f.middle, p.midGain);
    out.midPresence = unit(f.midPresence, p.midGain);
    out.fluxMid = f.fluxMid * p.midGain;
    out.treble = f.treble * p.trebGain;
    out.trebleAtt = f.trebleAtt * p.trebGain;
    out.high = unit(f.high, p.trebGain);
    out.treblePresence = unit(f.treblePresence, p.trebGain);
    out.fluxHigh = f.fluxHigh * p.trebGain;
    return out;
}

}  // namespace geode::viz
