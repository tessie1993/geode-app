#include "viz/SoundFeed.hpp"

#include <algorithm>

#include "sound/interp/SpeakerSync.hpp"

namespace geode::viz {

SoundFeed::SoundFeed() : block_(static_cast<size_t>(kBlockFrames) * 2, 0.0f) {}

void SoundFeed::setSource(const audio::ingest::Ring* ring) {
    if (ring == ring_) return;
    cursor_.reset();
    ring_ = ring;
    // From the newest sample: a surface that starts (or switches source) hears from now on, as the
    // speaker does, not the ring's last second over again.
    if (ring_) cursor_ = std::make_unique<audio::ingest::Cursor>(*ring_, audio::ingest::Cursor::Start::Newest);
    fresh_ = ring_ != nullptr;
}

int SoundFeed::pumpUntil(Sink& sink, int64_t speakerFrame) {
    namespace interp = sound::interp;
    if (!cursor_) return 0;
    const int64_t written = ring_->written();
    const int64_t limit = interp::listenLimit(written, speakerFrame);
    if (limit < 0) return 0;   // the clock knows nothing of this stream yet: nothing of it is heard
    if (fresh_) {
        // From the speaker's sample: the cursor made at setSource stands at the newest, a sink buffer ahead.
        cursor_ = std::make_unique<audio::ingest::Cursor>(*ring_, audio::ingest::Cursor::Start::Oldest);
        cursor_->skipToNewest(written - limit);
        fresh_ = false;
    } else if (limit - cursor_->position() > kStallFrames) {
        cursor_->skipToNewest(written - (limit - kMaxCatchUpFrames));
    }
    const interp::ReadWindow w =
        interp::window(cursor_->position(), written, ring_->oldestSafe(written), limit, kMaxCatchUpFrames);
    return drain(sink, w.count);
}

int SoundFeed::drain(Sink& sink, int64_t count) {
    int total = 0;
    while (total < count) {
        const auto want = static_cast<int>(std::min<int64_t>(kBlockFrames, count - total));
        const audio::ingest::Cursor::Block b = cursor_->read(block_.data(), want);
        if (b.frames <= 0) break;
        if (b.restarted) sink.restart();
        if (b.sampleRate > 0) sink.write(block_.data(), b.frames, b.sampleRate);
        total += b.frames;
    }
    return total;
}

int SoundFeed::pump(Sink& sink) {
    if (!cursor_) return 0;
    fresh_ = false;
    cursor_->skipToNewest(kMaxCatchUpFrames);
    int total = 0;
    // Bounded by what was written before the first read plus what the writer adds meanwhile; the guard only
    // stops a writer far faster than real time from holding the GL thread here.
    for (int guard = 0; guard < 2 * kMaxCatchUpFrames / kBlockFrames + 2; ++guard) {
        const audio::ingest::Cursor::Block b = cursor_->read(block_.data(), kBlockFrames);
        if (b.frames <= 0) break;
        if (b.restarted) sink.restart();
        if (b.sampleRate > 0) sink.write(block_.data(), b.frames, b.sampleRate);
        total += b.frames;
    }
    return total;
}

}  // namespace geode::viz
