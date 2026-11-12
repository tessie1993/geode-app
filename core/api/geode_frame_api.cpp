#include "api/geode_api.h"
#include "viz/frame/CastAudio.hpp"
#include <cstring>

#include "viz/frame/CastTeeRule.hpp"
#include "viz/frame/FixedRateClock.hpp"
#include "viz/frame/HlsSegments.hpp"

using geode::viz::frame::CastAudio;
using geode::viz::frame::CastAudioFeed;
using geode::viz::frame::CastFormat;
using geode::viz::frame::FixedRateClock;
namespace hls = geode::viz::frame::hls;

static_assert(hls::kWindow == GEODE_HLS_WINDOW && hls::kStartSegments == GEODE_HLS_START_SEGMENTS);
static_assert(hls::kMaxTracks == GEODE_HLS_TRACKS && hls::kMaxGrowth == GEODE_HLS_FRAGMENT_GROWTH);

namespace {

FixedRateClock load(const int64_t* state) {
    FixedRateClock c;
    c.startNs = state[0];
    c.next = state[1];
    c.fps = state[2] > 0 ? static_cast<int>(state[2]) : CastFormat::kFps;
    return c;
}

void store(const FixedRateClock& c, int64_t* state) {
    state[0] = c.startNs;
    state[1] = c.next;
    state[2] = c.fps;
}

CastAudio loadAudio(const int64_t* state) {
    CastAudio a = CastAudio::started(static_cast<int>(state[2]));
    a.produced = state[0];
    a.held = state[1];
    return a;
}

CastAudioFeed loadFeed(const int64_t* state) {
    CastAudioFeed f = CastAudioFeed::started(static_cast<int>(state[2]));
    f.queued = state[0];
    f.pending = state[1];
    return f;
}

void storeFeed(const CastAudioFeed& f, int64_t* state) {
    state[0] = f.queued;
    state[1] = f.pending;
    state[2] = f.rateHz;
}

void storeAudio(const CastAudio& a, int64_t* state) {
    state[0] = a.produced;
    state[1] = a.held;
    state[2] = a.rateHz;
}

}  // namespace

extern "C" {

void geode_frame_clock_start(int64_t* state, int fps, int64_t now_ns) {
    if (!state) return;
    store(FixedRateClock::started(fps, now_ns), state);
}

int64_t geode_frame_clock_wait_ns(const int64_t* state, int64_t now_ns) {
    return state ? load(state).waitNs(now_ns) : 0;
}

int64_t geode_frame_clock_take(int64_t* state, int64_t now_ns) {
    if (!state) return 0;
    FixedRateClock c = load(state);
    const int64_t pts = c.take(now_ns);
    store(c, state);
    return pts;
}

int64_t geode_frame_clock_audio_until_us(const int64_t* state) {
    return state ? load(state).audioUntilUs() : 0;
}

void geode_cast_format(int* out) {
    if (!out) return;
    out[0] = CastFormat::kWidth;
    out[1] = CastFormat::kHeight;
    out[2] = CastFormat::kFps;
    out[3] = CastFormat::kKeyframeIntervalSec;
    out[4] = CastFormat::kVideoBitrate;
    out[5] = CastFormat::kAudioBitrate;
}

int geode_cast_tee_await_stage(int stage_draws, int fallback_made, int64_t since_open_ns) {
    return geode::viz::frame::tee::awaitStage(stage_draws != 0, fallback_made != 0, since_open_ns) ? 1 : 0;
}

int geode_hls_window(void) { return hls::kWindow; }

double geode_hls_fragment_seconds(const uint8_t* init, size_t init_size, const uint8_t* fragment, size_t fragment_size) {
    return hls::fragmentSeconds(init, init_size, fragment, fragment_size);
}

int geode_hls_playlist(int64_t first_sequence, const double* durations, int count, const char* init_uri,
                       const char* segment_prefix, const char* segment_suffix, char* out, int cap) {
    const std::string text = hls::playlist(first_sequence, durations, count, init_uri, segment_prefix, segment_suffix);
    if (out && cap > 0 && text.size() + 1 <= static_cast<size_t>(cap)) {
        std::memcpy(out, text.c_str(), text.size() + 1);
    } else if (out && cap > 0) {
        out[0] = '\0';
    }
    return static_cast<int>(text.size());
}

size_t geode_hls_rewrite_fragment(const uint8_t* init, size_t init_size, int64_t moof_at, const uint8_t* fragment,
                                  size_t fragment_size, int64_t* decode_times, uint8_t* out, size_t cap) {
    return hls::rewriteFragment(init, init_size, moof_at, fragment, fragment_size, decode_times, out, cap);
}

int geode_cast_audio_start(int64_t* state, int rate_hz) {
    if (!state) return 0;
    const CastAudio a = CastAudio::started(rate_hz);
    storeAudio(a, state);
    return static_cast<int>(a.capacity());
}

void geode_cast_audio_step(int64_t* state, int64_t gained_frames, int64_t until_us, int64_t* out) {
    if (!state || !out) return;
    CastAudio a = loadAudio(state);
    const CastAudio::Step s = a.step(gained_frames, until_us);
    storeAudio(a, state);
    out[0] = s.evict;
    out[1] = s.skip;
    out[2] = s.keep;
    out[3] = s.take;
    out[4] = s.pad;
    out[5] = s.ptsUs;
}

int geode_cast_audio_feed_start(int64_t* state, int rate_hz) {
    if (!state) return 0;
    const CastAudioFeed f = CastAudioFeed::started(rate_hz);
    state[0] = f.queued;
    state[1] = f.pending;
    state[2] = f.rateHz;
    return static_cast<int>(f.capacity());
}

int64_t geode_cast_audio_feed_add(int64_t* state, int64_t frames) {
    if (!state) return 0;
    CastAudioFeed f = loadFeed(state);
    const int64_t drop = f.add(frames);
    storeFeed(f, state);
    return drop;
}

int64_t geode_cast_audio_feed_take(int64_t* state, int64_t buffer_frames, int64_t* pts_us) {
    if (!state || !pts_us) return 0;
    CastAudioFeed f = loadFeed(state);
    const int64_t n = f.take(buffer_frames, *pts_us);
    storeFeed(f, state);
    return n;
}

}  // extern "C"
