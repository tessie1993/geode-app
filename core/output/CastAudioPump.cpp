#include "output/CastAudioPump.hpp"

#include <algorithm>
#include <utility>

#include "audio/dsp/PcmConvert.hpp"

namespace geode::output {

using audio::ingest::Cursor;
using audio::ingest::PcmRing;
using viz::frame::CastAudio;

CastAudioPump::CastAudioPump(std::shared_ptr<const PcmRing> music, int rateHz, int channels)
    : music_(std::move(music)),
      cursor_(music_ ? std::make_unique<Cursor>(*music_) : std::unique_ptr<Cursor>()),
      channels_(channels > 0 ? channels : 1),
      counts_(CastAudio::started(rateHz)) {
    hold_.assign(samples(counts_.capacity()), 0);
    block_.reserve(samples(counts_.rateHz));
    if (cursor_) taken_.assign(samples(cursor_->maxFrames()), 0);
}

int64_t CastAudioPump::gain() {
    if (!cursor_) return 0;
    const Cursor::Take take = cursor_->take();
    if (take.frames <= 0 || cursor_->channels() != channels_ || take.sampleRateHz <= 0) return 0;
    const size_t n = samples(take.frames);
    const float* in = cursor_->interleaved();
    for (size_t i = 0; i < n; ++i) taken_[i] = audio::roundToS16(in[i]);
    if (!resampler_ || resampler_->inRateHz() != take.sampleRateHz) {
        resampler_ = std::make_unique<audio::PcmResampler>(take.sampleRateHz, counts_.rateHz, channels_);
        const size_t most = static_cast<size_t>(cursor_->maxFrames());
        resampler_->reserve(most);
        const size_t room = samples(static_cast<int64_t>(resampler_->maxOutFrames(most)));
        if (resampled_.size() < room) resampled_.resize(room);
    }
    const size_t out = resampler_->process(taken_.data(), static_cast<size_t>(take.frames), resampled_.data());
    return static_cast<int64_t>(out);
}

int CastAudioPump::pump(bool fromMusic, int64_t untilUs) {
    blockFrames_ = 0;
    const int64_t gained = fromMusic ? gain() : 0;
    const int64_t heldBefore = counts_.held;
    const CastAudio::Step s = counts_.step(gained, untilUs);
    int16_t* hold = hold_.data();
    if (s.evict > 0) std::copy(hold + samples(s.evict), hold + samples(heldBefore), hold);
    if (s.keep > 0) {
        const int16_t* from = resampled_.data() + samples(s.skip);
        std::copy(from, from + samples(s.keep), hold + samples(heldBefore - s.evict));
    }
    const int64_t need = s.take + s.pad;
    if (need <= 0) return 0;
    if (block_.size() < samples(need)) block_.resize(samples(need));
    std::copy(hold, hold + samples(s.take), block_.data());
    std::fill(block_.data() + samples(s.take), block_.data() + samples(need), int16_t{0});
    if (s.take > 0) std::copy(hold + samples(s.take), hold + samples(s.take + counts_.held), hold);
    blockFrames_ = static_cast<int>(need);
    return blockFrames_;
}

}  // namespace geode::output
