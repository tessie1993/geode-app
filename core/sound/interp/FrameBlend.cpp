#include "sound/interp/FrameBlend.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "sound/Rhythm.hpp"

namespace geode::sound::interp {

namespace {

using Heard = AnalysisThread::Heard;

constexpr std::array<int, SoundEngine::kTriggers> triggerIndices() {
    std::array<int, SoundEngine::kTriggers> out{};
    size_t n = 0;
    for (const FieldSpan& f : kFieldKinds) {
        if (f.kind == FieldKind::Trigger) out[n++] = static_cast<int>(f.offset / sizeof(float));
    }
    return out;
}
constexpr std::array<int, SoundEngine::kTriggers> kTriggerIndex = triggerIndices();

float* floats(GeodeSoundFrame& f) { return reinterpret_cast<float*>(&f); }
const float* floats(const GeodeSoundFrame& f) { return reinterpret_cast<const float*>(&f); }

float wrap01(float x) {
    const float w = x - std::floor(x);
    return w < 1.0f ? w : 0.0f;
}

void clearTriggers(GeodeSoundFrame& out) {
    float* o = floats(out);
    for (const int i : kTriggerIndex) o[i] = 0.0f;
}

}  // namespace

void blendSteps(const Heard& a, const Heard* b, int64_t position, GeodeSoundFrame& out) {
    out = a.frame;
    float t = 0.0f;
    if (b != nullptr && b->position > a.position && position > a.position) {
        t = static_cast<float>(std::min(1.0, static_cast<double>(position - a.position) /
                                                 static_cast<double>(b->position - a.position)));
    }
    const float* from = floats(a.frame);
    const float* to = b != nullptr ? floats(b->frame) : from;
    float* o = floats(out);
    for (const FieldSpan& span : kFieldKinds) {
        const size_t first = span.offset / sizeof(float);
        for (size_t i = first; i < first + static_cast<size_t>(span.count); ++i) {
            switch (span.kind) {
                case FieldKind::Linear:
                    if (t > 0.0f) o[i] = from[i] + (to[i] - from[i]) * t;
                    break;
                case FieldKind::Angle:
                    if (t > 0.0f) {
                        const float d = to[i] - from[i];
                        o[i] = wrap01(from[i] + (d - std::round(d)) * t);
                    }
                    break;
                case FieldKind::Clock:
                    if (t > 0.0f && to[i] >= from[i]) o[i] = from[i] + (to[i] - from[i]) * t;
                    break;
                case FieldKind::Trigger:
                case FieldKind::Drawn:
                    o[i] = 0.0f;
                    break;
                case FieldKind::Phase:
                case FieldKind::Hold:
                case FieldKind::Told:
                    break;
            }
        }
    }
    // As SoundEngine::frameAt: the analysis samples since the earlier hop, the input's halvings divided out.
    const int64_t since = position > a.position ? (position - a.position) >> a.halvings : 0;
    Rhythm::phases(a.clock, static_cast<int>(std::min<int64_t>(since, std::numeric_limits<int>::max())), out.hitTime, out);
}

bool FrameReader::read(const AnalysisThread& analysis, int64_t position, GeodeSoundFrame& out, Emotion::State& emotion,
                       Hits& hits) {
    hits.clear();
    Heard a;
    if (position < 0 || !analysis.hopAt(position, a)) {
        clearTriggers(out);
        emotion.degreeAccent = 0.0f;
        return false;
    }
    Heard b;
    const bool after = analysis.hopAfter(position, b);
    blendSteps(a, after ? &b : nullptr, position, out);
    emotion = a.emotion;
    emotion.degreeAccent = 0.0f;
    if (!primed_) {
        // A first read hears from here on: nothing fired before it. Older than every kept hop, `a` is still to come.
        const bool ahead = a.position > position;
        lastIndex_ = static_cast<int64_t>(a.index) - (ahead ? 1 : 0);
        lastHopPosition_ = ahead ? position : a.position;
        primed_ = true;
        return true;
    }
    // Every hop up to the position no read delivered yet, by index: hops sharing a position (a starved writer's
    // silence) and a hop published after an earlier read passed its sample are each delivered once.
    analysis.hopsBetween(lastHopPosition_ - 1, position, [&](const Heard& h) {
        const int64_t index = static_cast<int64_t>(h.index);
        if (index <= lastIndex_) return;
        lastIndex_ = index;
        lastHopPosition_ = h.position;
        // Heard while the speaker stood (readStill): not again.
        if (h.position == stillAt_ && index > stillFirst_ && index <= stillTo_) return;
        fire(h, out, emotion, hits);
    });
    return true;
}

void FrameReader::fire(const Heard& hop, GeodeSoundFrame& out, Emotion::State& emotion, Hits& hits) const {
    float* o = floats(out);
    const float* fired = floats(hop.frame);
    for (const int i : kTriggerIndex) {
        // The strongest since the last read; of equals, the newest.
        if (fired[i] > 0.0f && fired[i] >= o[i]) {
            o[i] = fired[i];
            hits.at[static_cast<size_t>(i)] = hop.position;
        }
    }
    emotion.degreeAccent = std::max(emotion.degreeAccent, hop.emotion.degreeAccent);
}

bool FrameReader::readStill(const AnalysisThread& analysis, GeodeSoundFrame& out, Emotion::State& emotion, Hits& hits) {
    hits.clear();
    const int64_t newest = analysis.newestPosition();
    Heard a;
    if (newest < 0 || !analysis.hopAt(newest, a)) {
        clearTriggers(out);
        emotion.degreeAccent = 0.0f;
        return false;
    }
    blendSteps(a, nullptr, newest, out);
    emotion = a.emotion;
    emotion.degreeAccent = 0.0f;
    if (stillAt_ != newest) {
        // The newest hop under this position, or the oldest kept when every kept hop stands here: the one after it
        // is the first here, and every later one repeats its position.
        Heard under;
        const bool found = analysis.hopAt(newest - 1, under);
        stillAt_ = newest;
        stillFirst_ = !found ? static_cast<int64_t>(a.index)
                             : static_cast<int64_t>(under.index) + (under.position < newest ? 1 : 0);
        // A first read hears from here on; a hop read() already delivered does not fire twice.
        stillTo_ = primed_ ? std::max(stillFirst_, lastIndex_) : static_cast<int64_t>(a.index);
    }
    analysis.hopsBetween(newest - 1, newest, [&](const Heard& h) {
        const int64_t index = static_cast<int64_t>(h.index);
        if (index <= stillTo_) return;
        stillTo_ = index;
        fire(h, out, emotion, hits);
    });
    return true;
}

void FrameReader::heardThrough(const AnalysisThread& analysis, int64_t position) {
    Heard h;
    if (analysis.hopAt(position, h)) {
        lastIndex_ = static_cast<int64_t>(h.index);
        lastHopPosition_ = h.position;
    } else {
        lastIndex_ = -1;
        lastHopPosition_ = position;
    }
    primed_ = true;
}

bool FrameReader::starved(const AnalysisThread& analysis) {
    const int64_t newest = analysis.newestPosition();
    if (newest < 0) return false;
    Heard last;
    Heard before;
    if (!analysis.hopAt(newest, last) || !analysis.hopAt(newest - 1, before)) return false;
    // `before` is the newest hop under the newest position, or the oldest kept when every kept hop is at it.
    return before.position == newest ? last.index > before.index : last.index > before.index + 1;
}

}  // namespace geode::sound::interp
