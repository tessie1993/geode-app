// Host-side checks for the muxer budget (output/MuxerBudget, #774): the video bit-rate cap that keeps a long
// render under MediaMuxer's 4 GB MP4 limit. Built and run by CI (see .github/workflows/android.yml, "Native
// muxer budget tests") with the system compiler. The pinned rates are what Kotlin's export/MuxerBudget gave
// before the port (its JVM test's cases, plus exact values worked through its Long maths), so a render made
// after the port asks the encoder for the same rate as one made before it.
#include <climits>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "api/geode_api.h"
#include "output/MuxerBudget.hpp"

using namespace geode::output;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// ExportQuality's preset rates.
constexpr int32_t kRate4K = 40'000'000;
constexpr int32_t kRate1080 = 12'000'000;
constexpr int32_t kRate720 = 6'000'000;

constexpr int64_t minutesUs(int64_t minutes) { return minutes * 60 * 1'000'000; }

// What bps for durationUs takes, as the old JVM test measured it.
constexpr int64_t videoBytes(int64_t bps, int64_t durationUs) { return bps * durationUs / 8'000'000; }

// The header and the ABI must agree on every case (the ABI always uses the 3.8e9 limit).
void checkCap(int32_t requested, int64_t durationUs, int64_t audio, int32_t want, const char* what) {
    const int32_t got = cappedVideoBps(requested, durationUs, audio);
    const int abi = geode_muxer_capped_video_bps(requested, durationUs, audio);
    if (got != want || abi != want) {
        std::printf("FAIL: %s: %d bps over %lld us with %lld audio bytes gave %d (abi %d), want %d\n", what,
                    requested, static_cast<long long>(durationUs), static_cast<long long>(audio), got, abi, want);
        ++failures;
    }
}

void constants() {
    check(kMuxerSafeBytes == 3'800'000'000LL, "safe bytes");
    check(kMuxerFloorBps == 2'000'000, "floor");
    check(kExportAacBps == 192'000, "aac rate");
    // The margin: the safe size sits well below 4 GiB and below LoopExtend's 4 GiB - 64 MiB.
    check(kMuxerSafeBytes < 4LL * 1024 * 1024 * 1024 - 64LL * 1024 * 1024, "under LoopExtend's limit");
}

void audioBytesFollowTheAacRate() {
    // 192 kbit/s for one second is 24,000 bytes.
    check(audioBytes(1'000'000) == 24'000, "one second of aac");
    check(geode_muxer_aac_bytes(1'000'000) == 24'000, "abi: one second of aac");
    check(audioBytes(minutesUs(5)) == 7'200'000, "five minutes of aac");
    check(audioBytes(minutesUs(60)) == 86'400'000, "an hour of aac");
    check(audioBytes(1) == 0, "truncates");
    check(audioBytes(0) == 0 && audioBytes(-1) == 0 && geode_muxer_aac_bytes(-1'000'000) == 0, "no negative audio");
    check(audioBytes(1'000'000, 128'000) == 16'000, "another rate");
}

void shortRenderKeepsItsRate() {
    const int64_t five = minutesUs(5);
    checkCap(kRate4K, five, audioBytes(five), kRate4K, "5 min 4K");
    checkCap(kRate1080, five, audioBytes(five), kRate1080, "5 min 1080p");
    checkCap(kRate720, minutesUs(60), audioBytes(minutesUs(60)), kRate720, "an hour of 720p fits as is");
    // At exactly the fitting rate the request stands: 3.8e9 bytes at 40 Mbit/s is 760 s.
    checkCap(kRate4K, 760'000'000, 0, kRate4K, "exactly the safe size");
}

void longRenderIsCappedUnderTheLimit() {
    const int64_t hour = minutesUs(60);
    const int64_t audio = audioBytes(hour);
    // (3.8e9 - 86.4e6) * 8e6 / 3.6e9, truncated.
    checkCap(kRate4K, hour, audio, 8'252'444, "an hour of 4K");
    checkCap(kRate1080, hour, audio, 8'252'444, "an hour of 1080p");
    const int32_t capped = cappedVideoBps(kRate4K, hour, audio);
    check(capped < kRate4K, "capped below the preset");
    check(videoBytes(capped, hour) + audio <= kMuxerSafeBytes, "fits the safe size, margin included");
    check(videoBytes(capped, hour) + audio < 4LL * 1024 * 1024 * 1024, "well under 4 GiB");
    // Not needlessly low: within a kilobit of the most that fits.
    check(videoBytes(capped + 1'000, hour) + audio > kMuxerSafeBytes, "within a kilobit of the most that fits");
    // Just past the line: 761 s of 4K.
    checkCap(kRate4K, 761'000'000, 0, 39'947'437, "one second past the line");
    // Three hours: still above the floor.
    checkCap(kRate4K, 3 * hour, audioBytes(3 * hour), 2'622'814, "three hours of 4K");
    // The audio the caller measured counts in full; negative audio counts as none.
    checkCap(kRate4K, hour, -5, 8'444'444, "negative audio is none");
}

void veryLongRender() {
    const int64_t tenHours = minutesUs(600);
    checkCap(kRate4K, tenHours, audioBytes(tenHours), kMuxerFloorBps, "ten hours: the floor");
    checkCap(1'000'000, tenHours, audioBytes(tenHours), 1'000'000, "a request below the floor is left alone");
    checkCap(1'500'000, tenHours, audioBytes(tenHours), 1'500'000, "the floor never lifts a request");
    // Audio alone past the limit: no bytes left for video, so the floor.
    const int64_t thousandHours = minutesUs(60'000);
    checkCap(kRate4K, thousandHours, audioBytes(thousandHours), kMuxerFloorBps, "audio past the limit");
    // The longest duration int64 holds: Kotlin's Long wrapped in audioBytes; this wraps the same way.
    check(audioBytes(INT64_MAX) == 0, "int64 max audio wraps as Kotlin's Long did");
    checkCap(kRate4K, INT64_MAX, audioBytes(INT64_MAX), kMuxerFloorBps, "int64 max duration");
    // A limit far past any real one overflows (limit * 8e6) exactly as Kotlin did, rather than being undefined.
    check(cappedVideoBps(kRate4K, minutesUs(60), 0, 10'000'000'000'000LL) == kRate4K, "wrapping limit");
}

void unknownDurationLeavesTheRequest() {
    checkCap(kRate4K, 0, 0, kRate4K, "zero duration");
    checkCap(kRate4K, -1, 0, kRate4K, "negative duration");
    checkCap(kRate4K, INT64_MIN, 0, kRate4K, "int64 min duration");
    checkCap(0, minutesUs(600), 0, 0, "zero request");
    checkCap(-5, minutesUs(600), 0, -5, "negative request");
}

// The old Kotlin, written out in plain int64 for inputs where nothing wraps; the header must match it on a
// sweep of real renders (every preset, one second to five hours, with estimated and measured audio).
int32_t kotlinReference(int32_t requested, int64_t durationUs, int64_t audio) {
    if (durationUs <= 0 || requested <= 0) return requested;
    int64_t video = kMuxerSafeBytes - (audio < 0 ? 0 : audio);
    if (video < 0) video = 0;
    const int64_t fitting = video * 8 * 1'000'000 / durationUs;
    if (fitting >= requested) return requested;
    const int32_t lo = requested < kMuxerFloorBps ? requested : kMuxerFloorBps;
    return static_cast<int32_t>(fitting) < lo ? lo : static_cast<int32_t>(fitting);
}

void sweepMatchesTheKotlin() {
    for (int32_t rate : {kRate720, kRate1080, kRate4K, 1'000'000}) {
        for (int64_t s = 1; s <= 5 * 3600; s += 7) {
            const int64_t d = s * 1'000'000 + 123'457;
            for (int64_t audio : {audioBytes(d), audioBytes(d) + 4'096, int64_t{0}}) {
                if (cappedVideoBps(rate, d, audio) != kotlinReference(rate, d, audio)) {
                    std::printf("FAIL: sweep %d bps %lld us %lld audio\n", rate, static_cast<long long>(d),
                                static_cast<long long>(audio));
                    ++failures;
                    return;
                }
            }
        }
    }
}

// Pure and constexpr: the cap is decided at compile time where its inputs are.
static_assert(cappedVideoBps(kRate4K, 0, 0) == kRate4K);
static_assert(cappedVideoBps(kRate4K, 3'600'000'000LL, 86'400'000) == 8'252'444);
static_assert(audioBytes(1'000'000) == 24'000);

}  // namespace

int main() {
    constants();
    audioBytesFollowTheAacRate();
    shortRenderKeepsItsRate();
    longRenderIsCappedUnderTheLimit();
    veryLongRender();
    unknownDurationLeavesTheRequest();
    sweepMatchesTheKotlin();
    if (failures != 0) {
        std::printf("%d muxer budget check(s) failed\n", failures);
        return 1;
    }
    std::printf("muxer budget: all checks passed\n");
    return 0;
}
