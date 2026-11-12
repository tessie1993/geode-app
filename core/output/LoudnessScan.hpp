#pragma once
#include "audio/dsp/Loudness.hpp"
#include "audio/ingest/TrackPump.hpp"

// A finished render's loudness: its audio decoded whole and fed through the BS.1770 meter (audio/dsp/Loudness).
// It measures the file that will be uploaded, after the encoder, not the source. The decoder is behind DecodePort
// (audio/ingest/android/TrackDecoder on a phone, a fake in core/tests/loudness_scan_test). One thread, the caller's.
namespace geode::output::loudness {

enum class ScanEnd {
    kMeasured,
    kTooShort,    // under one 400 ms block: BS.1770 has nothing to gate
    kUnreadable,  // the codec failed or stalled, the audio changed rate or layout part-way, or BS.1770 has no
                  // weights for its channel count
};

struct Scan {
    ScanEnd end = ScanEnd::kUnreadable;
    audio::LoudnessReport report;
};

// Output dequeues in a row that give nothing before a decoder counts as stalled (each waits up to its timeout).
inline constexpr int kStallLimit = 1000;

// Runs port to its end of stream. Never throws: a codec failure is kUnreadable.
Scan scan(audio::ingest::DecodePort& port);

}  // namespace geode::output::loudness
