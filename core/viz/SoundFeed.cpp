#include "viz/SoundFeed.hpp"

namespace geode::viz {

SoundFeed::SoundFeed() : block_(static_cast<size_t>(kBlockFrames) * 2, 0.0f) {}

void SoundFeed::setSource(const audio::ingest::Ring* ring) {
    if (ring == ring_) return;
    cursor_.reset();
    ring_ = ring;
    // From the newest sample: a surface that starts (or switches source) hears from now on, as the
    // speaker does, not the ring's last second over again.
    if (ring_) cursor_ = std::make_unique<audio::ingest::Cursor>(*ring_, audio::ingest::Cursor::Start::Newest);
}

int SoundFeed::pump(Sink& sink) {
    if (!cursor_) return 0;
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
