// Host-side checks for playback ReplayGain (audio/dsp/ReplayGain, DspChain's gain stage), through the
// geode_dsp_* / geode_replaygain_* C ABI the app calls. Built and run by CI (.github/workflows/android.yml,
// "Native playback DSP tests") with the system compiler; no device.
//
// #351: the gain used to be set from the player's track change on the main thread, a sink buffer after
// the chain had already processed the head of the next track, and dropped to 0 dB until that track's tag
// read returned. So a gapless album went: track B's head at track A's gain, then 0 dB, then B's gain.
// The chain now takes a stream's gain at the flush that starts it (geode_dsp_begin_stream), from tags read
// ahead, and keeps the gain it has while a stream's tags are still unknown.
#include <cmath>
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "audio/dsp/ReplayGain.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

float fromDb(float db) { return std::pow(10.0f, db / 20.0f); }

constexpr int kRate = 48000;
constexpr float kLevel = 0.1f;

// The chain with every stage but the gain left at rest, so its output is exactly kLevel times the gain.
geode_dsp* chain(const geode_replaygain* table) {
    geode_dsp* d = geode_dsp_create(kRate, 2);
    geode_dsp_set_limiter(d, 0);
    geode_dsp_set_replaygain(d, table);
    return d;
}

// One block of constant level through the chain; returns the gain of its first and last frame.
struct Block {
    float first;
    float last;
};

Block run(geode_dsp* d, size_t frames = 9600) {
    std::vector<float> pcm(frames * 2, kLevel);
    geode_dsp_process(d, pcm.data(), frames);
    return {pcm[0] / kLevel, pcm[pcm.size() - 1] / kLevel};
}

void tags(geode_replaygain* t, const char* stream, float trackDb, float albumDb) {
    const float values[4] = {trackDb, 0.5f, albumDb, 0.5f};
    geode_replaygain_set_tags(t, stream, GEODE_TAG_TRACK_GAIN | GEODE_TAG_ALBUM_GAIN, values);
}

// The issue's case: A tagged -9 dB, B (a quiet master) +3 dB, gapless, both read ahead. A's tail plays at
// A's gain; B's first sample is at B's gain, with no stretch at A's gain and no 0 dB burst.
void gaplessTrackChangeLandsOnTheFirstSample() {
    geode_replaygain* t = geode_replaygain_create();
    geode_replaygain_configure(t, 1, 0.0f, 0);
    tags(t, "content://a", -9.0f, -5.0f);
    tags(t, "content://b", 3.0f, -5.0f);
    geode_dsp* d = chain(t);

    geode_dsp_reset(d);
    geode_dsp_begin_stream(d, "content://a");
    const Block a = run(d);
    check(near(a.first, fromDb(-9.0f)) && near(a.last, fromDb(-9.0f)), "gapless: A plays at -9 dB from its first sample");

    geode_dsp_reset(d);  // the processor's flush, then the stream it starts
    geode_dsp_begin_stream(d, "content://b");
    const Block b = run(d);
    check(near(b.first, fromDb(3.0f)), "gapless: B's first sample is at +3 dB, not A's -9 dB");
    check(near(b.last, fromDb(3.0f)), "gapless: B stays at +3 dB (no 0 dB stretch)");

    geode_dsp_destroy(d);
    geode_replaygain_destroy(t);
}

// A skip to a track whose tags have not arrived keeps the gain the chain has rather than 0 dB, then
// glides to the track's own gain within the 20 ms ramp once they do.
void unknownTagsKeepTheGainThenGlide() {
    geode_replaygain* t = geode_replaygain_create();
    geode_replaygain_configure(t, 1, 0.0f, 0);
    tags(t, "content://a", -9.0f, -9.0f);
    geode_dsp* d = chain(t);
    geode_dsp_begin_stream(d, "content://a");
    run(d);

    geode_dsp_reset(d);
    geode_dsp_begin_stream(d, "content://far");
    const Block waiting = run(d);
    check(near(waiting.first, fromDb(-9.0f)) && near(waiting.last, fromDb(-9.0f)),
          "unknown: keeps -9 dB while the read is out, never 0 dB");

    tags(t, "content://far", -2.0f, -2.0f);
    const Block arrived = run(d);  // 200 ms: ten ramp time constants
    check(arrived.first < fromDb(-8.5f), "arrived: glides, does not jump");
    check(near(arrived.last, fromDb(-2.0f), 1e-3f), "arrived: settles at -2 dB");

    // A flush with no stream name (no playlist context) keeps the gain too.
    geode_dsp_reset(d);
    geode_dsp_begin_stream(d, nullptr);
    check(near(run(d).first, fromDb(-2.0f), 1e-3f), "no name: keeps the gain");

    geode_dsp_destroy(d);
    geode_replaygain_destroy(t);
}

// Settings reach the stream already playing; off is 0 dB whatever the tags.
void settingsApplyToTheCurrentStream() {
    geode_replaygain* t = geode_replaygain_create();
    tags(t, "content://a", -6.0f, -3.0f);
    geode_dsp* d = chain(t);
    geode_dsp_begin_stream(d, "content://a");
    check(near(run(d).last, 1.0f), "off: 0 dB");

    geode_replaygain_configure(t, 2, 1.0f, 0);
    check(near(run(d).last, fromDb(-2.0f), 1e-3f), "album + 1 dB preamp: -2 dB");
    geode_replaygain_configure(t, 1, 0.0f, 0);
    check(near(run(d).last, fromDb(-6.0f), 1e-3f), "track: -6 dB");
    geode_replaygain_configure(t, 0, 0.0f, 0);
    check(near(run(d).last, 1.0f, 1e-3f), "off again: 0 dB");

    geode_dsp_destroy(d);
    geode_replaygain_destroy(t);
}

// The gain rule itself, as the Kotlin one it replaced had it.
void gainRule() {
    using geode::audio::ReplayGainTags;
    using geode::audio::replayGainDb;
    ReplayGainTags all{15, -6.0f, 0.5f, -3.0f, 0.9f};
    check(near(replayGainDb(all, 0, 2.0f, true), 0.0f), "rule: off is 0 dB");
    check(near(replayGainDb(all, 1, 2.0f, false), -4.0f), "rule: track + preamp");
    check(near(replayGainDb(all, 2, 2.0f, false), -1.0f), "rule: album + preamp");

    ReplayGainTags trackOnly{geode::audio::kTrackGain | geode::audio::kTrackPeak, -6.0f, 0.5f, 0.0f, 0.0f};
    check(near(replayGainDb(trackOnly, 2, 0.0f, false), -6.0f), "rule: album mode falls back to the track gain");
    ReplayGainTags albumOnly{geode::audio::kAlbumGain, 0.0f, 0.0f, -4.0f, 0.0f};
    check(near(replayGainDb(albumOnly, 1, 0.0f, false), -4.0f), "rule: track mode falls back to the album gain");
    check(near(replayGainDb(ReplayGainTags{}, 1, 3.0f, true), 0.0f), "rule: untagged is 0 dB");

    // Peak 0.5 clips above +6.02 dB: a +8 dB track gain is held there with the guard on.
    ReplayGainTags hot{geode::audio::kTrackGain | geode::audio::kTrackPeak, 8.0f, 0.5f, 0.0f, 0.0f};
    check(near(replayGainDb(hot, 1, 0.0f, true), 6.0206f, 1e-3f), "rule: clip guard holds below the peak");
    check(near(replayGainDb(hot, 1, 0.0f, false), 8.0f), "rule: no guard, no hold");
    ReplayGainTags hotAlbumPeak{geode::audio::kAlbumGain | geode::audio::kTrackPeak, 0.0f, 0.5f, 8.0f, 0.0f};
    check(near(replayGainDb(hotAlbumPeak, 2, 0.0f, true), 6.0206f, 1e-3f), "rule: album mode falls back to the track peak");
}

// The table keeps the last kCapacity streams; the same key again replaces its tags.
void tableKeepsTheRecentStreams() {
    using geode::audio::ReplayGainTable;
    using geode::audio::ReplayGainTags;
    using geode::audio::replayGainStreamKey;
    ReplayGainTable table;
    table.configure(1, 0.0f, false);
    char name[32];
    for (size_t i = 0; i < ReplayGainTable::kCapacity + 1; ++i) {
        std::snprintf(name, sizeof name, "content://%zu", i);
        table.setTags(replayGainStreamKey(name), ReplayGainTags{geode::audio::kTrackGain, -static_cast<float>(i), 0, 0, 0});
    }
    float db = 0.0f;
    uint64_t generation = 0;
    check(table.tryGain(replayGainStreamKey("content://0"), db, generation) == ReplayGainTable::Lookup::Unknown,
          "table: the oldest stream goes past capacity");
    check(table.tryGain(replayGainStreamKey("content://32"), db, generation) == ReplayGainTable::Lookup::Found && near(db, -32.0f),
          "table: the newest stream is there");
    table.setTags(replayGainStreamKey("content://32"), ReplayGainTags{geode::audio::kTrackGain, -1.0f, 0, 0, 0});
    check(table.tryGain(replayGainStreamKey("content://32"), db, generation) == ReplayGainTable::Lookup::Found && near(db, -1.0f),
          "table: the same stream again replaces its tags");
    check(replayGainStreamKey(nullptr) == 0 && replayGainStreamKey("") == 0 && replayGainStreamKey("x") != 0,
          "table: only a missing name is key 0");
}

}  // namespace

int main() {
    gaplessTrackChangeLandsOnTheFirstSample();
    unknownTagsKeepTheGainThenGlide();
    settingsApplyToTheCurrentStream();
    gainRule();
    tableKeepsTheRecentStreams();
    if (failures == 0) std::printf("replaygain_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
