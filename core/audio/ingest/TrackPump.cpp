#include "audio/ingest/TrackPump.hpp"

#include <cstring>
#include <stdexcept>
#include <unistd.h>

namespace geode::audio::ingest {

bool isAudioMime(const char* mime) { return mime != nullptr && std::strncmp(mime, "audio/", 6) == 0; }

bool DecodeSource::step(TrackFeed& feed) {
    if (ended_) return false;
    if (!inputDone_) inputDone_ = port_->feedInput();
    DecodedChunk chunk;
    if (!port_->takeOutput(chunk)) return true;
    if (chunk.bytes > 0) {
        feed.pushDecoded(chunk.data, chunk.bytes, chunk.isFloat, chunk.channels, chunk.sampleRate, chunk.ptsUs);
    }
    port_->releaseOutput();
    if (chunk.endOfStream) ended_ = true;
    return !ended_;
}

std::unique_ptr<TrackSource> openTrack(const TrackSpan& span, int64_t startMs, const DecoderOpener& openDecoder) {
    const int own = ::dup(span.fd);
    if (own < 0) throw std::runtime_error("track descriptor not duplicated");
    // Back to the file's start: an earlier open of the same span left the shared position where it stopped, and a
    // fresh descriptor (what the shell opened the URI for each time) starts there. FdSource seeks on to a declared
    // span's start itself; with none declared it reads from here, as ParcelFileDescriptor's stream does.
    ::lseek(own, 0, SEEK_SET);
    const int64_t start = span.length < 0 ? 0 : span.offset;
    if (auto aiff = AiffReader::open(std::make_unique<FdSource>(own, start, span.length))) {
        return std::make_unique<AiffSource>(std::move(aiff));
    }
    std::unique_ptr<DecodePort> port;
    if (openDecoder) port = openDecoder(span, startMs);
    if (!port) return nullptr;
    return std::make_unique<DecodeSource>(std::move(port));
}

void TrackPump::step() {
    if (!source_->step(feed_)) ended_ = true;
}

bool TrackPump::rateKnown() {
    // The rate is only known once the source has produced something.
    while (!feed_.hasRate() && !ended_) step();
    return feed_.hasRate();
}

bool TrackPump::hearUntilTarget(TrackListener& listener) {
    while (!listener.hear(feed_)) {
        if (ended_) return false;
        step();
    }
    return true;
}

bool TrackPump::deliverMs(int64_t endMs, TrackListener& listener) {
    if (!rateKnown()) return false;
    feed_.setTargetMs(endMs);
    return hearUntilTarget(listener);
}

bool TrackPump::deliverFrame(int64_t n, int fps, TrackListener& listener) {
    if (!rateKnown()) return false;
    feed_.setTargetFrame(n, fps);
    return hearUntilTarget(listener);
}

}  // namespace geode::audio::ingest
