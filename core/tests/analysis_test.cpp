// Host-side checks for the audio analysis core: synthetic PCM at several sample rates, levels and tempi, and
// the features have to follow the signal, not a constant. Built and run by CI (see
// .github/workflows/android.yml, "Native host tests") with the system compiler; no device.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "analysis/AnalysisSession.hpp"

using geode::analysis::AnalysisSession;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kRates[] = {22050, 44100, 48000, 96000};

// Mirrors AnalysisFft.sizeFor (engine/audio-core): the smallest power of two whose bin width is no wider
// than 2048 points at 48 kHz.
int fftFor(int rate) {
    int size = 256;
    while (size < 32768 && static_cast<long>(size) * 48000 < 2048L * rate) size *= 2;
    return size;
}

struct Noise {
    uint32_t state = 12345u;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
    }
};

// Interleaved stereo, both channels the same: a 15 ms noise burst every beat, decaying over 4 ms, over a bed
// of steady noise at `bed` times the click level. bpm <= 0 is the bed alone.
void appendMusic(std::vector<float>& pcm, int rate, float seconds, float bpm, float level, float bed, Noise& noise) {
    const long frames = static_cast<long>(seconds * rate);
    const double period = bpm > 0.0f ? 60.0 * rate / bpm : 0.0;
    const long burst = rate * 15 / 1000;
    for (long i = 0; i < frames; i++) {
        float x = bed * level * noise.next();
        if (period > 0.0) {
            const long sinceBeat = i - static_cast<long>(std::floor(i / period) * period);
            if (sinceBeat < burst) x += level * noise.next() * std::exp(-static_cast<float>(sinceBeat) / (0.004f * rate));
        }
        pcm.push_back(x);
        pcm.push_back(x);
    }
}

void appendSilence(std::vector<float>& pcm, int rate, float seconds) {
    pcm.insert(pcm.end(), static_cast<size_t>(seconds * rate) * 2, 0.0f);
}

void appendSine(std::vector<float>& pcm, int rate, float seconds, float hz, float level) {
    const long frames = static_cast<long>(seconds * rate);
    for (long i = 0; i < frames; i++) {
        const float x = level * static_cast<float>(std::sin(6.283185307179586 * hz * i / rate));
        pcm.push_back(x);
        pcm.push_back(x);
    }
}

// Pushes the PCM `chunk` frames at a time and pulls every frame it yields, the way both the live engine and
// the offline decoder loop drive a session.
std::vector<GeodeFeatureFrame> run(AnalysisSession& session, const std::vector<float>& pcm, size_t chunk) {
    std::vector<GeodeFeatureFrame> out;
    const size_t frames = pcm.size() / 2;
    for (size_t at = 0; at < frames; at += chunk) {
        session.push(pcm.data() + at * 2, std::min(chunk, frames - at), 2);
        GeodeFeatureFrame f{};
        while (session.pull(f)) out.push_back(f);
    }
    return out;
}

float medianBpm(const std::vector<GeodeFeatureFrame>& frames, size_t from) {
    std::vector<float> bpms;
    for (size_t i = from; i < frames.size(); i++) bpms.push_back(frames[i].bpm);
    if (bpms.empty()) return 0.0f;
    std::nth_element(bpms.begin(), bpms.begin() + bpms.size() / 2, bpms.end());
    return bpms[bpms.size() / 2];
}

int argmaxBand(const GeodeFeatureFrame& f) {
    return static_cast<int>(std::max_element(f.bands, f.bands + GEODE_BAND_COUNT) - f.bands);
}

void hopComesFromTheWindowAndTheStream() {
    for (const int rate : kRates) {
        AnalysisSession session(rate, fftFor(rate));
        check(session.hopSamples() == fftFor(rate) / AnalysisSession::kHopDivisor, "hop is a quarter of the window");
        check(std::fabs(session.hopRateHz() - static_cast<float>(rate) / session.hopSamples()) < 1e-3f,
              "hop rate is the stream rate over the hop");
        std::vector<float> pcm;
        Noise noise;
        appendMusic(pcm, rate, 10.0f, 0.0f, 0.3f, 1.0f, noise);
        const auto frames = run(session, pcm, 1000);
        const float expected = 10.0f * session.hopRateHz();
        if (std::fabs(static_cast<float>(frames.size()) - expected) > 5.0f) {
            std::printf("  %d Hz: %zu frames for 10 s, expected %.0f\n", rate, frames.size(), expected);
            check(false, "one frame per hop of real audio");
        }
    }
}

// The tracker has no preferred tempo, so each of these has to come out as itself, not pulled towards 120 and
// not folded an octave, at every rate.
void tempoFollowsTheClickTrainWithoutBias() {
    for (const int rate : kRates) {
        for (const float bpm : {72.0f, 96.0f, 128.0f, 150.0f, 174.0f}) {
            AnalysisSession session(rate, fftFor(rate));
            std::vector<float> pcm;
            Noise noise;
            appendMusic(pcm, rate, 30.0f, bpm, 0.5f, 0.02f, noise);
            const auto frames = run(session, pcm, 1000);
            const float measured = medianBpm(frames, frames.size() * 2 / 3);
            if (std::fabs(measured - bpm) > bpm * 0.02f) {
                std::printf("  %d Hz, %.0f BPM: measured %.2f\n", rate, bpm, measured);
                check(false, "BPM follows the signal");
            }
        }
    }
}

// The same music at -6 dBFS and at -34 dBFS: an arrival after a silent break is a property of the music, so
// it has to fire at both levels (it gated on absolute RMS 0.4 and fired at neither).
void arrivalIsRelativeToTheTracksOwnLoudness() {
    for (const float level : {0.5f, 0.02f}) {
        AnalysisSession session(48000, fftFor(48000));
        std::vector<float> pcm;
        Noise noise;
        appendMusic(pcm, 48000, 12.0f, 120.0f, level, 0.4f, noise);
        appendSilence(pcm, 48000, 3.0f);
        appendMusic(pcm, 48000, 6.0f, 120.0f, level, 0.4f, noise);
        const auto frames = run(session, pcm, 1000);
        const size_t breakEnds = static_cast<size_t>(15.0f * session.hopRateHz());
        bool arrived = false;
        for (size_t i = breakEnds; i < frames.size(); i++) arrived = arrived || frames[i].arrival > 0.0f;
        if (!arrived) std::printf("  level %.2f: no arrival after the break\n", level);
        check(arrived, "arrival fires after a break at any level");
    }
}

// A 1 kHz tone lands in the same band at every rate, and loud and quiet copies normalise to the same shape.
void bandsFollowHertzAndNotLevel() {
    int reference = -1;
    for (const int rate : kRates) {
        AnalysisSession session(rate, fftFor(rate));
        std::vector<float> pcm;
        appendSine(pcm, rate, 3.0f, 1000.0f, 0.5f);
        const auto frames = run(session, pcm, 1000);
        const int band = argmaxBand(frames.back());
        if (reference < 0) reference = band;
        if (std::abs(band - reference) > 1) {
            std::printf("  %d Hz: 1 kHz in band %d, %d at %d Hz\n", rate, band, reference, kRates[0]);
            check(false, "band edges are in Hz");
        }
    }
    std::vector<GeodeFeatureFrame> last;
    for (const float level : {0.5f, 0.005f}) {
        AnalysisSession session(48000, fftFor(48000));
        std::vector<float> pcm;
        Noise noise;
        appendMusic(pcm, 48000, 6.0f, 120.0f, level, 0.4f, noise);
        last.push_back(run(session, pcm, 1000).back());
    }
    float worst = 0.0f;
    for (int b = 0; b < GEODE_BAND_COUNT; b++) worst = std::max(worst, std::fabs(last[0].bands[b] - last[1].bands[b]));
    if (worst > 0.05f) std::printf("  bands differ by %.3f between -6 and -46 dBFS\n", worst);
    check(worst <= 0.05f, "bands normalise against the music, not full scale");
}

// Live pushes hop-sized slices of the ring; offline pushes whatever the decoder hands it. Same PCM, same frames.
void framesDoNotDependOnHowThePcmArrives() {
    std::vector<float> pcm;
    Noise noise;
    appendMusic(pcm, 44100, 8.0f, 128.0f, 0.4f, 0.2f, noise);
    AnalysisSession a(44100, fftFor(44100));
    AnalysisSession b(44100, fftFor(44100));
    AnalysisSession c(44100, fftFor(44100));
    const auto byDecoder = run(a, pcm, 4608);
    const auto byHop = run(b, pcm, static_cast<size_t>(b.hopSamples()));
    const auto byOdd = run(c, pcm, 333);
    bool same = byDecoder.size() == byHop.size() && byHop.size() == byOdd.size();
    for (size_t i = 0; same && i < byHop.size(); i++) {
        same = std::memcmp(&byDecoder[i], &byHop[i], sizeof(GeodeFeatureFrame)) == 0 &&
               std::memcmp(&byOdd[i], &byHop[i], sizeof(GeodeFeatureFrame)) == 0;
    }
    check(same, "live and offline chunking give identical frames");
}

void silenceReadsAsSilence() {
    AnalysisSession session(48000, fftFor(48000));
    std::vector<float> pcm;
    Noise noise;
    appendMusic(pcm, 48000, 4.0f, 120.0f, 0.4f, 0.4f, noise);
    appendSilence(pcm, 48000, 1.0f);
    const auto& f = run(session, pcm, 1000).back();
    float bands = 0.0f;
    for (float v : f.bands) bands += v;
    check(f.rms == 0.0f && bands == 0.0f && f.onset == 0.0f && f.kick == 0.0f, "no audio, no features");
}

}  // namespace

int main() {
    hopComesFromTheWindowAndTheStream();
    tempoFollowsTheClickTrainWithoutBias();
    arrivalIsRelativeToTheTracksOwnLoudness();
    bandsFollowHertzAndNotLevel();
    framesDoNotDependOnHowThePcmArrives();
    silenceReadsAsSilence();
    if (failures == 0) std::printf("analysis_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
