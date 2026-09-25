#include "audio/player/Mixer.hpp"

#include <algorithm>
#include <cmath>

namespace geode::audio::player {

namespace {

constexpr size_t kTapSamples = 1u << 16;
constexpr float kHalfPi = 1.57079632679f;

void gainsFor(float t, int curve, float& out, float& in) {
    switch (curve) {
        case GEODE_CROSSFADE_EQUAL_POWER:
            out = std::cos(t * kHalfPi);
            in = std::sin(t * kHalfPi);
            return;
        case GEODE_CROSSFADE_SMOOTH: {
            const float s = t * t * (3.0f - 2.0f * t);
            out = 1.0f - s;
            in = s;
            return;
        }
        case GEODE_CROSSFADE_LINEAR:
        default:
            out = 1.0f - t;
            in = t;
            return;
    }
}

}  // namespace

Mixer::Mixer() : tap_(kTapSamples), scratch_(kMaxRenderFrames * kChannels, 0.0f) {}

// Only frees chains setDsp() already retired (queued for us to destroy); the chain still live in dsp_,
// if any, was never handed to us to own and is left for its caller to destroy, same as before this
// change.
Mixer::~Mixer() {
    std::lock_guard<std::mutex> guard(dspRetireLock_);
    for (const RetiredDsp& retired : retiredDsp_) geode_dsp_destroy(retired.dsp);
    retiredDsp_.clear();
}

Deck* Mixer::takeRetired() {
    Deck* deck = nullptr;
    return retired_.pop(&deck, 1) == 1 ? deck : nullptr;
}

void Mixer::setCrossfade(int64_t frames, int curve) {
    crossfadeFrames_.store(frames, std::memory_order_relaxed);
    curve_.store(curve, std::memory_order_relaxed);
}

// Never blocks: geode_player_set_dsp may be called from any thread (including a UI thread, where a wait
// risks an ANR), and a fixed timeout can't guarantee the previous chain is no longer in use, which is
// exactly the promise this call needs to keep. So the swap is only ever published here; the previous
// chain is queued and destroyed later by reclaimDsp(), once render() can no longer be inside
// geode_dsp_process() with it. See the PR notes for the geode_player_set_dsp doc text this implies for
// who now owns destroying the old chain.
void Mixer::setDsp(geode_dsp* dsp, bool streamRunning) {
    geode_dsp* old = dsp_.exchange(dsp, std::memory_order_acq_rel);
    if (!old) return;
    // callbacks_ read right after the swap becomes visible: once it has advanced past this value, every
    // render() call that could still have read the old pointer (started before the swap, still holding
    // it locally) has finished and incremented callbacks_, so the old chain is safe to destroy. If the
    // stream wasn't running at swap time, no callback could have touched it at all.
    const RetiredDsp retired{old, streamRunning, callbacks_.load(std::memory_order_acquire)};
    std::lock_guard<std::mutex> guard(dspRetireLock_);
    retiredDsp_.push_back(retired);
}

void Mixer::reclaimDsp(bool streamRunning) {
    std::lock_guard<std::mutex> guard(dspRetireLock_);
    if (retiredDsp_.empty()) return;
    const uint64_t callbacks = callbacks_.load(std::memory_order_acquire);
    while (!retiredDsp_.empty()) {
        const RetiredDsp& front = retiredDsp_.front();
        // Ready once a callback has run since the swap, or (streamRunning here reflects "now", not swap
        // time) the stream isn't running any more: Oboe guarantees no callback keeps running past a
        // confirmed pause/stop, so anything in flight at swap time must have finished by then too.
        const bool ready = !front.waitForCallback || !streamRunning || callbacks != front.readyAt;
        if (!ready) break;
        geode_dsp_destroy(front.dsp);
        retiredDsp_.pop_front();
    }
}

void Mixer::render(float* out, size_t frames) {
    applyCommands();
    const size_t samples = frames * kChannels;
    std::fill_n(out, samples, 0.0f);
    if (Deck* current = current_.load(std::memory_order_relaxed)) {
        const int64_t firstFrame = current->position();
        const size_t got = pull(*current, out, frames);
        Deck* next = next_.load(std::memory_order_relaxed);
        const int64_t fadeFrames = crossfadeFrames_.load(std::memory_order_relaxed);
        const int64_t fadeStart = current->durationFrames - fadeFrames;
        const bool fadeWindow = next && fadeFrames > 0 && current->durationFrames > fadeFrames &&
                                firstFrame + static_cast<int64_t>(got) > fadeStart;
        const bool ranDry = got < frames && current->endOfStream.load(std::memory_order_acquire);
        if (fadeWindow) {
            // A callback can straddle the fade boundary. Do not consume the next deck for the
            // prefix whose incoming gain is zero. On decoder starvation, advance neither deck
            // beyond the available outgoing frames (except at real EOF).
            const size_t prefix = static_cast<size_t>(std::max<int64_t>(0, fadeStart - firstFrame));
            const size_t overlap = (ranDry ? frames : got) - prefix;
            const size_t gotNext = pull(*next, scratch_.data(), overlap);
            std::fill_n(scratch_.data() + gotNext * kChannels, (overlap - gotNext) * kChannels, 0.0f);
            fade(out + prefix * kChannels, scratch_.data(), overlap,
                 firstFrame + static_cast<int64_t>(prefix), fadeStart, fadeFrames);
            if (ranDry || current->position() >= current->durationFrames) beginTransition(current, next);
        } else if (ranDry) {
            if (next) {
                pull(*next, out + got * kChannels, frames - got);
                beginTransition(current, next);
            } else {
                ended_.store(true, std::memory_order_release);
            }
        }
    }
    const float volume = volume_.load(std::memory_order_relaxed);
    if (volume != 1.0f) {
        for (size_t i = 0; i < samples; ++i) out[i] *= volume;
    }
    if (geode_dsp* dsp = dsp_.load(std::memory_order_acquire)) geode_dsp_process(dsp, out, frames);
    tap_.push(out, samples);
    callbacks_.fetch_add(1, std::memory_order_release);
}

void Mixer::applyCommands() {
    DeckCommand command{};
    while (commands_.pop(&command, 1) == 1) {
        switch (command.op) {
            case DeckCommand::Op::SetCurrent:
                retire(current_.exchange(command.deck, std::memory_order_acq_rel));
                ended_.store(false, std::memory_order_release);
                break;
            case DeckCommand::Op::SetNext:
                retire(next_.exchange(command.deck, std::memory_order_acq_rel));
                break;
            case DeckCommand::Op::ClearNext:
                retire(next_.exchange(nullptr, std::memory_order_acq_rel));
                break;
        }
    }
}

// A full retired ring only delays the free: the engine releases every deck it still owns at shutdown.
void Mixer::retire(Deck* deck) {
    if (deck) retired_.push(&deck, 1);
}

void Mixer::beginTransition(Deck* from, Deck* to) {
    current_.store(to, std::memory_order_release);
    next_.store(nullptr, std::memory_order_release);
    retire(from);
}

size_t Mixer::pull(Deck& deck, float* stereo, size_t frames) {
    const size_t got = deck.ring.pop(stereo, frames * kChannels) / kChannels;
    deck.framesConsumed.fetch_add(static_cast<int64_t>(got), std::memory_order_release);
    return got;
}

void Mixer::fade(float* out, const float* incoming, size_t frames, int64_t firstFrame, int64_t fadeStart,
                 int64_t fadeFrames) const {
    const int curve = curve_.load(std::memory_order_relaxed);
    const float step = 1.0f / static_cast<float>(fadeFrames);
    for (size_t i = 0; i < frames; ++i) {
        const float t = std::clamp(static_cast<float>(firstFrame + static_cast<int64_t>(i) - fadeStart) * step, 0.0f, 1.0f);
        float gainOut = 1.0f;
        float gainIn = 0.0f;
        gainsFor(t, curve, gainOut, gainIn);
        for (int ch = 0; ch < kChannels; ++ch) {
            const size_t k = i * kChannels + static_cast<size_t>(ch);
            out[k] = out[k] * gainOut + incoming[k] * gainIn;
        }
    }
}

}  // namespace geode::audio::player
