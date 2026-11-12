#pragma once

#include "audio/ingest/MicInput.hpp"

// The device's mic streams for MicInput: Oboe input streams (AAudio at minSdk 33), shared, normal latency.
// Android only, never in a host test; the header holds no Oboe so the C ABI file needs none.
namespace geode::audio::ingest {

// Lives for the process: it is every mic stream's data and error callback.
MicStreams& oboeMicStreams();

}  // namespace geode::audio::ingest
