// Host-side checks for core/viz/frame/CastAudio (#772): Cast's audio pump, as counts.
// Built and run by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler; no GL.
//
// What is pinned: a steady feed queues exactly what it gained, one frame's worth per frame; a paused feed
// pads with silence exactly to the cut; a feed that runs ahead of the cut is held for the next frame (and
// trimmed, oldest first, past half a second); a 3-hour run at 30 fps stays sample-exact at 48 kHz, the
// position never more or less than the slots taken times 1600. The C ABI is checked against the same
// rule, since that is what CastStream calls, and the cuts come from the frame clock's own ABI. The
// encoder feed hands every queued frame to the codec once, in order, at its CastAudio position, whatever
// size the codec's input buffers are and however long it has none free, dropping (and skipping the
// position past) only what a stalled codec leaves over a second behind.
//
// The whole pump (core/output/CastAudioPump, geode_cast_audio_*, #1150): the player's ring through its own
// cursor, resampled, held and padded. It is held sample for sample to the Kotlin AudioPump it replaced,
// rebuilt here over the same C ABIs, through rate changes, pauses, a burst, the mic owning the visuals and a
// mono source. Also pinned: 48 kHz passes untouched; 44.1 kHz is the export resampler's output for the whole
// signal; an underrun queues what came, then silence, to the cut; frame boundaries at 25, 30 and 60 fps;
// the channel layouts; and no allocation per frame once running.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <new>
#include <vector>

#include "api/geode_api.h"
#include "audio/dsp/PcmConvert.hpp"
#include "audio/dsp/PcmResampler.hpp"
#include "viz/frame/CastAudio.hpp"
#include "viz/frame/FixedRateClock.hpp"

using geode::viz::frame::CastAudio;
using geode::viz::frame::CastAudioFeed;
using geode::viz::frame::FixedRateClock;

namespace {
long allocations = 0;   // every operator new in this process, so a pump can be held to none per frame
}  // namespace

void* operator new(std::size_t bytes) {
    ++allocations;
    if (void* p = std::malloc(bytes > 0 ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kRate = 48'000;
constexpr int kFps = 30;
constexpr int64_t kPerFrame = kRate / kFps;   // 1600
constexpr int64_t kStartNs = 77LL * 1'000'000'000;

// The frame clock through its C ABI, taken on time: the cut after each take.
struct Clock {
    int64_t s[GEODE_FRAME_CLOCK_STATE] = {};
    Clock() { geode_frame_clock_start(s, kFps, kStartNs); }
    int64_t nextCutUs() {
        const int64_t now = kStartNs + s[1] * FixedRateClock::kNanosPerSec / kFps;
        geode_frame_clock_take(s, now);
        return geode_frame_clock_audio_until_us(s);
    }
};

// The pump through its C ABI.
struct Pump {
    int64_t s[GEODE_CAST_AUDIO_STATE] = {};
    int64_t o[GEODE_CAST_AUDIO_STEP] = {};
    int capacity = 0;
    Pump() { capacity = geode_cast_audio_start(s, kRate); }
    void step(int64_t gained, int64_t untilUs) { geode_cast_audio_step(s, gained, untilUs, o); }
    int64_t produced() const { return s[0]; }
    int64_t held() const { return s[1]; }
    int64_t evict() const { return o[0]; }
    int64_t skip() const { return o[1]; }
    int64_t keep() const { return o[2]; }
    int64_t take() const { return o[3]; }
    int64_t pad() const { return o[4]; }
    int64_t ptsUs() const { return o[5]; }
};

void startsClean() {
    Pump p;
    check(p.capacity == kRate / 2, "start: the hold is half a second");
    check(p.s[2] == kRate && p.produced() == 0 && p.held() == 0, "start: rate set, nothing queued or held");
    int64_t s[GEODE_CAST_AUDIO_STATE] = {};
    check(geode_cast_audio_start(s, 0) == CastAudio::kDefaultRateHz / 2 && s[2] == CastAudio::kDefaultRateHz,
          "start: a bad rate falls back to 48 kHz");
    geode_cast_audio_step(nullptr, 1, 1, p.o);
    geode_cast_audio_step(p.s, 1, 1, nullptr);
    check(geode_cast_audio_start(nullptr, kRate) == 0, "null state is a no-op");
}

void steadyFeed() {
    Clock clock;
    Pump p;
    bool ok = true;
    for (int i = 0; i < 300; ++i) {
        const int64_t before = p.produced();
        p.step(kPerFrame, clock.nextCutUs());
        ok = ok && p.take() == kPerFrame && p.pad() == 0 && p.held() == 0;
        ok = ok && p.skip() == 0 && p.evict() == 0 && p.keep() == kPerFrame;
        ok = ok && p.ptsUs() == before * 1'000'000 / kRate;
    }
    check(ok, "steady: each frame queues exactly what it gained, no pad, nothing held");
    check(p.produced() == 300 * kPerFrame, "steady: the position is the frames taken times 1600");
}

void pausedFeedPadsToCut() {
    Clock clock;
    Pump p;
    bool ok = true;
    for (int i = 0; i < 90; ++i) {
        const int64_t cut = clock.nextCutUs();
        p.step(0, cut);
        ok = ok && p.take() == 0 && p.pad() == kPerFrame;
        ok = ok && p.produced() == (cut * kRate + 500'000) / 1'000'000;
    }
    check(ok, "paused: silence pads each frame exactly to the cut");

    // Pausing mid-feed: what is held goes first, then silence makes up the rest of the frame.
    Pump q;
    Clock c2;
    q.step(500, 0);   // gained before the first cut: held
    check(q.take() + q.pad() == 0 && q.held() == 500, "paused: frames gained before any cut are held");
    const int64_t cut = c2.nextCutUs();
    q.step(0, cut);
    check(q.take() == 500 && q.pad() == kPerFrame - 500 && q.held() == 0, "paused: the hold, then silence to the cut");
    check(q.produced() == kPerFrame, "paused: the position lands on the cut");
}

void feedAheadIsHeld() {
    Clock clock;
    Pump p;
    const int64_t cut1 = clock.nextCutUs();
    p.step(kPerFrame, cut1);
    check(p.take() == kPerFrame, "ahead: the first frame queues its own frames");
    // The ring gains again before the cut moves (the pump ran twice inside one frame's slot).
    p.step(kPerFrame, cut1);
    check(p.take() + p.pad() == 0 && p.held() == kPerFrame, "ahead: gained past the cut is held, nothing queued");
    p.step(0, clock.nextCutUs());
    check(p.take() == kPerFrame && p.pad() == 0 && p.held() == 0, "ahead: the next frame queues what was held");

    // A feed steadily faster than the cut fills the hold, then the oldest frames are trimmed at half a second.
    Pump q;
    Clock c2;
    bool ok = true;
    for (int i = 0; i < 200; ++i) {
        q.step(kPerFrame + 400, c2.nextCutUs());
        ok = ok && q.take() == kPerFrame && q.pad() == 0 && q.held() <= q.capacity;
    }
    check(ok, "ahead: a fast feed never pads and never holds past the cap");
    // Full at half a second when the gained frames join it, one frame's worth lighter once that frame is queued.
    check(q.held() == q.capacity - kPerFrame, "ahead: the hold sits full at half a second");
    check(q.evict() == 400, "ahead: once full, the oldest frames make room for the newest");

    // One burst bigger than the hold keeps only its newest half second.
    Pump r;
    r.step(30'000, 0);
    check(r.skip() == 30'000 - r.capacity && r.keep() == r.capacity && r.held() == r.capacity,
          "ahead: a burst past the cap keeps its newest half second");
}

void threeHoursSampleExact() {
    Clock clock;
    Pump p;
    constexpr int64_t kFrames = 3LL * 3600 * kFps;   // 324 000 frames
    bool exact = true;
    bool even = true;
    bool contiguous = true;
    int64_t lastEnd = 0;
    for (int64_t i = 0; i < kFrames; ++i) {
        // A wobbling feed: a little short, a little over, sometimes nothing (a pause), never steady.
        const int64_t gained = (i % 97 == 0) ? 0 : kPerFrame + ((i % 3) - 1) * 7;
        const int64_t before = p.produced();
        p.step(gained, clock.nextCutUs());
        exact = exact && p.produced() == (i + 1) * kPerFrame;
        even = even && p.take() + p.pad() == kPerFrame;
        contiguous = contiguous && before == lastEnd && p.ptsUs() == before * 1'000'000 / kRate;
        lastEnd = p.produced();
    }
    check(exact, "3 h: the position is exactly slots times 1600 after every frame");
    check(even, "3 h: every block is exactly one frame's 1600 samples");
    check(contiguous, "3 h: each block starts where the last ended, stamped on the sample clock");
    check(p.produced() == kFrames * kPerFrame, "3 h: 518 400 000 frames queued, none lost or doubled");
    check(p.ptsUs() == (kFrames - 1) * kPerFrame * 1'000'000 / kRate, "3 h: the last block's PTS is exact");
}

void structMatchesAbi() {
    CastAudio a = CastAudio::started(kRate);
    Pump p;
    Clock clock;
    bool same = true;
    for (int i = 0; i < 120; ++i) {
        const int64_t gained = (i * 733) % 3000;
        const int64_t cut = clock.nextCutUs();
        const CastAudio::Step s = a.step(gained, cut);
        p.step(gained, cut);
        same = same && s.evict == p.evict() && s.skip == p.skip() && s.keep == p.keep() && s.take == p.take() &&
               s.pad == p.pad() && s.ptsUs == p.ptsUs() && a.produced == p.produced() && a.held == p.held();
    }
    check(same, "the C ABI is the struct's rule");
    check(a.cutFrames(33'333) == kPerFrame, "the clock's truncated cut rounds to the slot's 1600 frames");
    check(a.cutFrames(0) == 0 && a.cutFrames(-5) == 0, "no cut before the stream starts");
}

// The pump's blocks through the encoder feed, against codec input buffers of a few sizes, some frames
// with none free: every frame queued once, PTS on the pump's own sample clock, the backlog drained.
void feedLosesNothing() {
    for (const int64_t buffer : {int64_t{1024}, int64_t{1600}, int64_t{4096}, int64_t{700}}) {
        CastAudio a = CastAudio::started(kRate);
        CastAudioFeed f = CastAudioFeed::started(kRate);
        Clock clock;
        bool onClock = true;
        int64_t handed = 0;
        for (int i = 0; i < 3000; ++i) {
            const CastAudio::Step s = a.step(kPerFrame, clock.nextCutUs());
            check(f.add(s.take + s.pad) == 0, "a codec that keeps up never drops");
            const int buffers = (i % 11 == 0) ? 0 : 3;   // now and then none free
            for (int b = 0; b < buffers; ++b) {
                int64_t pts = -1;
                const int64_t n = f.take(buffer, pts);
                if (n == 0) break;
                onClock = onClock && pts == handed * 1'000'000 / kRate && n <= buffer;
                handed += n;
            }
        }
        check(onClock, "each buffer is stamped where the last ended, never larger than the codec's");
        check(handed + f.pending == a.produced && f.queued == handed, "every frame queued reaches the codec or still waits");
        check(f.pending < 2 * kPerFrame, "the backlog drains once buffers are free again");
    }
}

void feedDropsOnlyPastASecond() {
    CastAudioFeed f = CastAudioFeed::started(kRate);
    check(f.capacity() == kRate, "the backlog holds a second");
    check(f.add(30'000) == 0 && f.add(18'000) == 0, "up to the cap nothing goes");
    check(f.add(4'000) == 4'000 && f.pending == kRate && f.queued == 4'000, "past it the oldest go, the position moves past them");
    int64_t pts = 0;
    check(f.take(1024, pts) == 1024 && pts == int64_t{4'000} * 1'000'000 / kRate, "and the next buffer's PTS is where its frames sit");
    check(f.add(0) == 0 && f.add(-3) == 0, "nothing in, nothing dropped");
    CastAudioFeed g = CastAudioFeed::started(kRate);
    check(g.add(2 * kRate) == kRate && g.pending == kRate, "a block bigger than the cap keeps its newest second");
    int64_t none = -1;
    CastAudioFeed e = CastAudioFeed::started(kRate);
    check(e.take(1024, none) == 0 && none == 0, "an empty backlog takes nothing");
}

void feedMatchesAbi() {
    int64_t st[GEODE_CAST_AUDIO_FEED_STATE] = {};
    check(geode_cast_audio_feed_start(st, kRate) == kRate, "the ABI's cap is the struct's");
    CastAudioFeed f = CastAudioFeed::started(kRate);
    bool same = true;
    for (int i = 0; i < 200; ++i) {
        const int64_t block = (i * 977) % 5000;
        same = same && geode_cast_audio_feed_add(st, block) == f.add(block);
        int64_t p1 = 0;
        int64_t p2 = 0;
        same = same && geode_cast_audio_feed_take(st, 1024, &p1) == f.take(1024, p2) && p1 == p2;
        same = same && st[0] == f.queued && st[1] == f.pending;
    }
    check(same, "the feed's C ABI is the struct's rule");
}

// ---- The whole pump: ring, resampler, hold and pad (core/output/CastAudioPump, #1150) ----

constexpr int kCh = 2;
constexpr int kRingFrames = 16'384;   // a take hands out at most a quarter of it
constexpr double kTwoPi = 6.283185307179586;

size_t at(int64_t frames, int channels = kCh) { return static_cast<size_t>(frames) * static_cast<size_t>(channels); }

struct Ring {
    geode_pcm_ring* r;
    explicit Ring(int channels = kCh) : r(geode_pcm_ring_create(kRingFrames, channels, 0)) {}
    ~Ring() { geode_pcm_ring_destroy(r); }
    Ring(const Ring&) = delete;
    Ring& operator=(const Ring&) = delete;
    void write(const std::vector<float>& s, int channels, int rate) {
        geode_pcm_ring_write_f32(r, s.data(), static_cast<int>(s.size() / static_cast<size_t>(channels)), channels, rate,
                                 nullptr);
    }
};

// A known signal: a tone per channel, frames from `from` on.
void tone(std::vector<float>& out, int64_t from, int frames, int channels, int rate) {
    out.resize(at(frames, channels));
    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            const double t = static_cast<double>(from + f) / rate;
            out[at(f, channels) + static_cast<size_t>(c)] = static_cast<float>(0.5 * std::sin(kTwoPi * (440.0 + 113.0 * c) * t));
        }
    }
}

// The pump through its C ABI, as CastStream calls it.
struct Native {
    geode_cast_audio* a;
    std::vector<int16_t> out = std::vector<int16_t>(at(kRate));
    explicit Native(const geode_pcm_ring* music) : a(geode_cast_audio_create(music, kRate, kCh)) {}
    ~Native() { geode_cast_audio_destroy(a); }
    Native(const Native&) = delete;
    Native& operator=(const Native&) = delete;
    // The block's frames, or -1 when the read disagrees with the pump.
    int pump(const geode_pcm_ring* current, int64_t untilUs) {
        const int frames = geode_cast_audio_pump(a, current, untilUs);
        if (frames <= 0) return frames;
        if (out.size() < at(frames)) out.resize(at(frames));
        return geode_cast_audio_read(a, out.data(), static_cast<int>(out.size())) == frames ? frames : -1;
    }
};

// The Kotlin AudioPump this replaced, line for line over the C ABIs it called: SoundCursor's 16-bit take,
// PcmResampler (a copy at equal rates, else the native one, rebuilt on a rate change), castAudioStep's
// counts and the hold's shuffles.
struct KotlinPump {
    const geode_pcm_ring* music;
    const geode_pcm_ring* input = nullptr;
    geode_pcm_cursor* cursor = nullptr;
    std::vector<int16_t> pcm;
    geode_resampler* resampler = nullptr;
    int resamplerInRate = 0;
    int64_t state[GEODE_CAST_AUDIO_STATE] = {};
    int64_t step[GEODE_CAST_AUDIO_STEP] = {};
    std::vector<int16_t> hold;
    std::vector<int16_t> gained;
    std::vector<int16_t> block;

    explicit KotlinPump(const geode_pcm_ring* m) : music(m) { hold.assign(at(geode_cast_audio_start(state, kRate)), 0); }
    ~KotlinPump() {
        geode_pcm_cursor_destroy(cursor);
        geode_resampler_destroy(resampler);
    }
    KotlinPump(const KotlinPump&) = delete;
    KotlinPump& operator=(const KotlinPump&) = delete;

    bool takeFromRing(const geode_pcm_ring* current) {
        if (!current || current != music) return false;
        if (current != input) {
            input = current;
            geode_pcm_cursor_destroy(cursor);
            cursor = geode_pcm_cursor_create(current, 0);
            pcm.assign(at(geode_pcm_cursor_max_frames(cursor), geode_pcm_cursor_channels(cursor)), 0);
        }
        int restarted = 0;
        int rate = 0;
        const int frames = geode_pcm_cursor_take_s16(cursor, pcm.data(), geode_pcm_cursor_max_frames(cursor), &restarted, &rate);
        if (frames <= 0 || geode_pcm_cursor_channels(cursor) != kCh || rate <= 0) return false;
        if (rate != resamplerInRate) {
            geode_resampler_destroy(resampler);
            resampler = rate == kRate ? nullptr : geode_resampler_create(rate, kRate, kCh);
            resamplerInRate = rate;
        }
        if (!resampler) {
            gained.assign(pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(at(frames)));
            return true;
        }
        const size_t room = geode_resampler_max_out_frames(resampler, static_cast<size_t>(frames));
        gained.resize(at(static_cast<int64_t>(room)));
        const int64_t n = geode_resampler_process(resampler, pcm.data(), static_cast<size_t>(frames), gained.data(), room);
        gained.resize(at(n > 0 ? n : 0));
        return true;
    }

    int pump(const geode_pcm_ring* current, int64_t untilUs) {
        const bool has = takeFromRing(current);
        const int64_t held = state[1];
        geode_cast_audio_step(state, has ? static_cast<int64_t>(gained.size() / kCh) : 0, untilUs, step);
        const int64_t evict = step[0];
        if (evict > 0) {
            std::copy(hold.begin() + static_cast<std::ptrdiff_t>(at(evict)), hold.begin() + static_cast<std::ptrdiff_t>(at(held)),
                      hold.begin());
        }
        if (has) {
            std::copy_n(gained.begin() + static_cast<std::ptrdiff_t>(at(step[1])), at(step[2]),
                        hold.begin() + static_cast<std::ptrdiff_t>(at(held - evict)));
        }
        const int64_t take = step[3];
        const int64_t need = take + step[4];
        if (need <= 0) return 0;
        block.assign(at(need), 0);
        std::copy_n(hold.begin(), at(take), block.begin());
        if (take > 0) {
            std::copy(hold.begin() + static_cast<std::ptrdiff_t>(at(take)),
                      hold.begin() + static_cast<std::ptrdiff_t>(at(take + state[1])), hold.begin());
        }
        return static_cast<int>(need);
    }
};

void sameAsTheKotlinPump() {
    Ring music;
    Ring mic;
    Native native(music.r);
    KotlinPump kotlin(music.r);
    Clock clock;
    std::vector<float> chunk;
    int64_t written = 0;
    int64_t queued = 0;
    int64_t heard = 0;
    bool sameFrames = true;
    bool sameSamples = true;
    for (int i = 0; i < 900; ++i) {
        // 44.1 kHz, a new track at 48 kHz, then 44.1 kHz again inside that track (no new epoch).
        const int rate = (i >= 300 && i < 600) ? 48'000 : 44'100;
        if (i == 300) geode_pcm_ring_begin_epoch(music.r);
        const int channels = (i >= 700 && i < 750) ? 1 : kCh;
        int frames = (i % 50 >= 45) ? 0 : rate / kFps + ((i % 7) - 3) * 11;
        if (i == 400) frames = 12'000;               // a stall's catch-up: more than one take holds
        if (i >= 750 && i < 800) frames = 4'000;     // a feed far ahead of the cut: the hold fills and evicts
        if (frames > 0) {
            tone(chunk, written, frames, channels, rate);
            music.write(chunk, channels, rate);
            written += frames;
        }
        if (i >= 440 && i < 480) {
            tone(chunk, 0, static_cast<int>(kPerFrame), kCh, kRate);
            mic.write(chunk, kCh, kRate);
        }
        // The mic feeds the visuals for a second, then nothing for a third of one.
        const geode_pcm_ring* current = (i >= 450 && i < 480) ? mic.r : (i >= 480 && i < 490) ? nullptr : music.r;
        const int64_t cut = clock.nextCutUs();
        for (int p = 0; p < (i % 13 == 0 ? 2 : 1); ++p) {
            const int n = native.pump(current, cut);
            const int k = kotlin.pump(current, cut);
            sameFrames = sameFrames && n == k;
            if (n > 0 && n == k) {
                sameSamples = sameSamples && std::equal(kotlin.block.begin(), kotlin.block.end(), native.out.begin());
                heard += std::count_if(native.out.begin(), native.out.begin() + static_cast<std::ptrdiff_t>(at(n)),
                                       [](int16_t s) { return s != 0; });
            }
            queued += n > 0 ? n : 0;
        }
    }
    check(sameFrames, "Kotlin parity: every pump queues as many frames as the Kotlin pump did");
    check(sameSamples, "Kotlin parity: every block is the Kotlin pump's, sample for sample");
    check(heard > 600 * kPerFrame, "Kotlin parity: the blocks carry the music, not only silence");
    check(queued == 900 * kPerFrame, "the stream's position lands on the last cut");
}

void passesThroughAt48k() {
    Ring music;
    Native native(music.r);
    Clock clock;
    std::vector<float> chunk(at(kPerFrame));
    std::vector<int16_t> sent;
    std::vector<int16_t> got;
    int k = 0;
    bool blocks = true;
    for (int i = 0; i < 150; ++i) {
        for (float& s : chunk) {
            s = static_cast<float>((k * 37) % 20'001 - 10'000) / 32767.0f;
            sent.push_back(geode::audio::roundToS16(s));
            ++k;
        }
        music.write(chunk, kCh, kRate);
        const int n = native.pump(music.r, clock.nextCutUs());
        blocks = blocks && n == kPerFrame;
        if (n > 0) got.insert(got.end(), native.out.begin(), native.out.begin() + static_cast<std::ptrdiff_t>(at(n)));
    }
    check(blocks, "48 kHz: each frame queues the 1600 frames it gained");
    check(got == sent, "48 kHz: the ring's samples reach the encoder unchanged and in order");
    check(sent[1] == 37 - 10'000 && sent[2] == 74 - 10'000, "48 kHz: a written value comes back as itself");
}

void resampledFrom44k() {
    Ring music;
    Native native(music.r);
    Clock clock;
    constexpr int kIn = 44'100;
    constexpr int kInPerFrame = kIn / kFps;   // 1470
    std::vector<float> chunk;
    std::vector<int16_t> in;
    std::vector<int16_t> got;
    bool blocks = true;
    for (int i = 0; i < 300; ++i) {
        tone(chunk, int64_t{i} * kInPerFrame, kInPerFrame, kCh, kIn);
        for (const float s : chunk) in.push_back(geode::audio::roundToS16(s));
        music.write(chunk, kCh, kIn);
        const int n = native.pump(music.r, clock.nextCutUs());
        blocks = blocks && n == kPerFrame;
        if (n > 0) got.insert(got.end(), native.out.begin(), native.out.begin() + static_cast<std::ptrdiff_t>(at(n)));
    }
    geode::audio::PcmResampler whole(kIn, kRate, kCh);
    const size_t inFrames = in.size() / kCh;
    std::vector<int16_t> ref(at(static_cast<int64_t>(whole.maxOutFrames(inFrames))));
    ref.resize(at(static_cast<int64_t>(whole.process(in.data(), inFrames, ref.data()))));
    check(blocks, "44.1 kHz: 1470 frames in, 1600 out, every frame, nothing held or padded");
    check(got == ref, "44.1 kHz: the stream is the export resampler's output for the whole signal");
}

void underrunHoldsThenPads() {
    Ring music;
    Native native(music.r);
    Clock clock;
    const int16_t loud = geode::audio::roundToS16(0.25f);
    auto steady = [](int frames) { return std::vector<float>(at(frames), 0.25f); };
    auto all = [&native](int from, int to, int16_t v) {
        return std::all_of(native.out.begin() + from * kCh, native.out.begin() + to * kCh, [v](int16_t s) { return s == v; });
    };
    int64_t queued = 0;
    int64_t cut = 0;
    for (int i = 0; i < 10; ++i) {
        music.write(steady(kPerFrame), kCh, kRate);
        cut = clock.nextCutUs();
        queued += native.pump(music.r, cut);
    }
    check(all(0, kPerFrame, loud), "underrun: a steady frame is all music");

    music.write(steady(1000), kCh, kRate);
    cut = clock.nextCutUs();
    int n = native.pump(music.r, cut);
    queued += n;
    check(n == kPerFrame && all(0, 1000, loud) && all(1000, kPerFrame, 0), "underrun: what came, then silence to the cut");

    bool silent = true;
    for (int i = 0; i < 10; ++i) {
        cut = clock.nextCutUs();
        n = native.pump(music.r, cut);
        queued += n;
        silent = silent && n == kPerFrame && all(0, kPerFrame, 0);
    }
    check(silent, "paused: each frame is a frame of silence");

    // The ring runs ahead of the cut: the rest is held for the next frame, then silence after it.
    music.write(steady(2400), kCh, kRate);
    cut = clock.nextCutUs();
    n = native.pump(music.r, cut);
    queued += n;
    check(n == kPerFrame && all(0, kPerFrame, loud), "ahead: the frame queues its own 1600");
    check(native.pump(music.r, cut) == 0, "ahead: a second pump inside the slot queues nothing");
    cut = clock.nextCutUs();
    n = native.pump(music.r, cut);
    queued += n;
    check(n == kPerFrame && all(0, 800, loud) && all(800, kPerFrame, 0), "ahead: the held 800, then silence");
    check(queued == (cut * kRate + 500'000) / 1'000'000, "underrun: the position is always the cut");
}

void frameBoundaries() {
    for (const int fps : {25, 30, 60}) {
        Ring music;
        Native native(music.r);
        const int per = kRate / fps;
        std::vector<float> chunk(at(per), 0.1f);
        bool ok = true;
        int64_t total = 0;
        for (int i = 0; i < fps * 4; ++i) {
            if (i % 9 != 4) music.write(chunk, kCh, kRate);   // now and then a frame with nothing new
            const int64_t untilUs = int64_t{i + 1} * 1'000'000 / fps;
            const int n = native.pump(music.r, untilUs);
            total += n;
            ok = ok && n == per && total == int64_t{i + 1} * per;
        }
        check(ok, "frame boundaries: each block is one frame's slot at 25, 30 and 60 fps, the position on the cut");
    }
}

void channelLayouts() {
    Clock clock;
    {
        Ring music;
        Native native(music.r);
        std::vector<float> mono(kPerFrame);
        for (int f = 0; f < kPerFrame; ++f) mono[static_cast<size_t>(f)] = static_cast<float>(f - 800) / 1000.0f;
        music.write(mono, 1, kRate);
        bool same = native.pump(music.r, clock.nextCutUs()) == kPerFrame;
        for (int f = 0; f < kPerFrame; ++f) {
            const int16_t want = geode::audio::roundToS16(mono[static_cast<size_t>(f)]);
            same = same && native.out[at(f)] == want && native.out[at(f) + 1] == want;
        }
        check(same, "layout: a mono source in the stereo ring plays on both channels");
    }
    {
        Ring music;
        Native native(music.r);
        Clock c2;
        std::vector<float> quad(at(kPerFrame, 4));
        for (size_t s = 0; s < quad.size(); ++s) quad[s] = static_cast<float>(s % 4) * 0.2f;
        music.write(quad, 4, kRate);
        bool front = native.pump(music.r, c2.nextCutUs()) == kPerFrame;
        for (int f = 0; f < kPerFrame; ++f) {
            front = front && native.out[at(f)] == 0 && native.out[at(f) + 1] == geode::audio::roundToS16(0.2f);
        }
        check(front, "layout: a four-channel source keeps its first two, the stream's left and right");
    }
    {
        Ring music(1);
        Native native(music.r);
        Clock c3;
        bool silent = true;
        for (int i = 0; i < 30; ++i) {
            music.write(std::vector<float>(kPerFrame, 0.5f), 1, kRate);
            silent = silent && native.pump(music.r, c3.nextCutUs()) == kPerFrame &&
                     std::all_of(native.out.begin(), native.out.begin() + kPerFrame * kCh, [](int16_t s) { return s == 0; });
        }
        check(silent, "layout: a mono ring is not the stream's stereo: silence, still to every cut");
    }
}

void onlyTheMusicLeaves() {
    Ring music;
    Ring mic;
    Native native(music.r);
    Clock clock;
    bool silent = true;
    for (int i = 0; i < 30; ++i) {
        music.write(std::vector<float>(at(kPerFrame), 0.3f), kCh, kRate);
        mic.write(std::vector<float>(at(kPerFrame), 0.6f), kCh, kRate);
        const int n = native.pump(i < 15 ? mic.r : nullptr, clock.nextCutUs());
        silent = silent && n == kPerFrame &&
                 std::all_of(native.out.begin(), native.out.begin() + kPerFrame * kCh, [](int16_t s) { return s == 0; });
    }
    check(silent, "music only: the mic, or no ring, feeding the visuals sends silence");
    music.write(std::vector<float>(at(kPerFrame), 0.3f), kCh, kRate);
    check(native.pump(music.r, clock.nextCutUs()) == kPerFrame && native.out[0] == geode::audio::roundToS16(0.3f),
          "music only: the music again once it feeds the visuals");
}

void noAllocationPerFrame() {
    for (const int rate : {44'100, kRate}) {
        Ring music;
        Native native(music.r);
        Clock clock;
        std::vector<float> chunk;
        tone(chunk, 0, rate / kFps, kCh, rate);
        // The first take at a rate builds that rate's resampler.
        for (int i = 0; i < 5; ++i) {
            music.write(chunk, kCh, rate);
            native.pump(music.r, clock.nextCutUs());
        }
        long during = 0;
        for (int i = 0; i < 300; ++i) {
            if (i % 40 < 35) music.write(chunk, kCh, rate);   // with pauses: the pad path too
            const long before = allocations;
            native.pump(i % 50 == 7 ? nullptr : music.r, clock.nextCutUs());
            during += allocations - before;
        }
        check(during == 0, "no allocation per frame once the pump runs");
    }
}

void cAbiEdges() {
    check(geode_cast_audio_create(nullptr, 0, kCh) == nullptr && geode_cast_audio_create(nullptr, kRate, 0) == nullptr,
          "C ABI: a bad rate or channel count makes nothing");
    geode_cast_audio* silent = geode_cast_audio_create(nullptr, kRate, kCh);
    check(silent != nullptr, "C ABI: no music ring is a pump of silence");
    Clock clock;
    check(geode_cast_audio_pump(silent, nullptr, clock.nextCutUs()) == kPerFrame, "C ABI: silence still runs to the cut");
    int16_t small[8] = {};
    check(geode_cast_audio_read(silent, small, 8) == 0, "C ABI: a buffer too small for the block is refused");
    std::vector<int16_t> out(at(kPerFrame), 7);
    check(geode_cast_audio_read(silent, out.data(), static_cast<int>(out.size())) == kPerFrame &&
              std::all_of(out.begin(), out.end(), [](int16_t s) { return s == 0; }),
          "C ABI: the read is the block");
    check(geode_cast_audio_pump(nullptr, nullptr, 1) == 0 && geode_cast_audio_read(nullptr, out.data(), 1) == 0 &&
              geode_cast_audio_read(silent, nullptr, 1) == 0,
          "C ABI: null handles and buffers are no-ops");
    geode_cast_audio_destroy(silent);
    geode_cast_audio_destroy(nullptr);
}

}  // namespace

int main() {
    startsClean();
    steadyFeed();
    pausedFeedPadsToCut();
    feedAheadIsHeld();
    threeHoursSampleExact();
    structMatchesAbi();
    feedLosesNothing();
    feedDropsOnlyPastASecond();
    feedMatchesAbi();
    sameAsTheKotlinPump();
    passesThroughAt48k();
    resampledFrom44k();
    underrunHoldsThenPads();
    frameBoundaries();
    channelLayouts();
    onlyTheMusicLeaves();
    noAllocationPerFrame();
    cAbiEdges();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("cast_audio_test: ok\n");
    return 0;
}
