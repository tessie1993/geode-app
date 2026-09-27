#include "audio/player/Mixer.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// The mixer tests deliberately install no DSP chain. Linking the actual DSP,
// decoder and device backends would obscure the sample scheduling under test.
extern "C" void geode_dsp_destroy(geode_dsp*) { throw std::logic_error("unexpected DSP destroy"); }
extern "C" void geode_dsp_process(geode_dsp*, float*, size_t) { throw std::logic_error("unexpected DSP process"); }

using geode::audio::player::Deck;
using geode::audio::player::DeckCommand;
using geode::audio::player::Mixer;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void near(float actual, float expected, const std::string& message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.00001f,
            message + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
}

void enqueue(Deck& deck, const std::vector<float>& frames) {
    std::vector<float> stereo;
    for (float value : frames) {
        stereo.push_back(value);
        stereo.push_back(-value); // Check both channels and their alignment.
    }
    require(deck.ring.push(stereo.data(), stereo.size()) == stereo.size(), "test ring overflow");
}

std::vector<float> render(Mixer& mixer, size_t frames) {
    std::vector<float> output(frames * 2, 999.0f);
    mixer.render(output.data(), frames);
    for (size_t i = 0; i < frames; ++i) near(output[i * 2 + 1], -output[i * 2], "stereo alignment");
    return output;
}

void setDecks(Mixer& mixer, Deck& current, Deck* next = nullptr) {
    require(mixer.post({DeckCommand::Op::SetCurrent, &current}), "set current");
    if (next) require(mixer.post({DeckCommand::Op::SetNext, next}), "set next");
}

float expectedBlend(float outgoing, float incoming, float t, int curve) {
    if (curve == GEODE_CROSSFADE_EQUAL_POWER) {
        constexpr float halfPi = 1.57079632679f;
        return outgoing * std::cos(t * halfPi) + incoming * std::sin(t * halfPi);
    }
    const float in = curve == GEODE_CROSSFADE_SMOOTH ? t * t * (3.0f - 2.0f * t) : t;
    return outgoing * (1.0f - in) + incoming * in;
}

void boundaryAndContinuity(int curve) {
    Mixer mixer;
    Deck current(32, 1, 0, 10);
    Deck next(32, 2, 0, 20);
    enqueue(current, std::vector<float>(10, 0.2f));
    current.endOfStream = true;
    enqueue(next, {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f});
    setDecks(mixer, current, &next);
    mixer.setCrossfade(4, curve); // Boundary at outgoing frame 6.
    const auto before = render(mixer, 4);
    for (float value : {before[0], before[2], before[4], before[6]}) near(value, 0.2f, "before fade");
    require(next.position() == 0, "incoming remains untouched before fade");
    const auto straddle = render(mixer, 4); // Two prefix frames, then two fade frames.
    near(straddle[0], 0.2f, "straddle prefix 0");
    near(straddle[2], 0.2f, "straddle prefix 1");
    near(straddle[4], expectedBlend(0.2f, 0.1f, 0.0f, curve), "fade start");
    near(straddle[6], expectedBlend(0.2f, 0.2f, 0.25f, curve), "fade next sample");
    require(next.position() == 2, "straddle must consume only two incoming frames");
    const auto tail = render(mixer, 4); // Two mixed frames followed by incoming alone.
    near(tail[0], expectedBlend(0.2f, 0.3f, 0.5f, curve), "fade continuation 0");
    near(tail[2], expectedBlend(0.2f, 0.4f, 0.75f, curve), "fade continuation 1");
    near(tail[4], 0.5f, "after fade 0");
    near(tail[6], 0.6f, "after fade 1");
    require(mixer.currentDeck() == &next, "incoming promoted at EOF");
    require(mixer.takeRetired() == &current, "outgoing retired once");
    require(mixer.takeRetired() == nullptr, "no duplicate retirement");
    const auto continued = render(mixer, 2);
    near(continued[0], 0.7f, "incoming continuity 0");
    near(continued[2], 0.8f, "incoming continuity 1");
}

void outgoingStarvation() {
    Mixer mixer;
    Deck current(32, 1, 4, 10);
    Deck next(32, 2, 0, 20);
    enqueue(current, {0.2f});
    enqueue(next, {0.1f, 0.2f, 0.3f, 0.4f});
    setDecks(mixer, current, &next);
    mixer.setCrossfade(4, GEODE_CROSSFADE_LINEAR);
    const auto before = render(mixer, 4); // Requested range crosses boundary, available data does not.
    near(before[0], 0.2f, "available outgoing");
    near(before[2], 0, "starvation silence");
    require(next.position() == 0, "starvation must not prematurely consume incoming");
    enqueue(current, {0.2f, 0.2f, 0.2f});
    render(mixer, 4); // Frame 5 prefix, then two fade frames, one unavailable frame.
    require(next.position() == 2, "partial fade consumes only available overlap");
    const auto empty = render(mixer, 4);
    for (float value : empty) near(value, 0, "empty outgoing silence");
    require(next.position() == 2, "empty callback must not advance incoming");
    require(mixer.currentDeck() == &current && !mixer.ended(), "starvation is not EOF");
    enqueue(current, {0.2f, 0.2f});
    current.endOfStream = true;
    const auto recovered = render(mixer, 2);
    near(recovered[0], expectedBlend(0.2f, 0.3f, 0.5f, GEODE_CROSSFADE_LINEAR), "recovered fade");
    require(mixer.currentDeck() == &next, "exact callback EOF promotes incoming");
}

void gaplessUnknownDurationAndFinalEof() {
    Mixer mixer;
    Deck current(16, 1, 0, 0);
    Deck next(16, 2, 0, 0);
    enqueue(current, {0.1f, 0.2f});
    enqueue(next, {0.3f, 0.4f, 0.5f});
    current.endOfStream = true;
    next.endOfStream = true;
    setDecks(mixer, current, &next);
    mixer.setCrossfade(4, GEODE_CROSSFADE_LINEAR);
    const auto transition = render(mixer, 4);
    for (size_t i = 0; i < 4; ++i) near(transition[i * 2], 0.1f * (i + 1), "gapless sample");
    require(mixer.currentDeck() == &next && !mixer.ended(), "unknown-duration promotion");
    const auto last = render(mixer, 4);
    near(last[0], 0.5f, "final sample");
    for (size_t i = 1; i < 4; ++i) near(last[i * 2], 0, "final silence");
    require(mixer.ended(), "EOF without next marks ended");
}

void incomingStarvationIsZeroFilled() {
    Mixer mixer;
    Deck current(16, 1, 8, 10);
    Deck next(16, 2, 0, 20);
    enqueue(current, {0.2f, 0.2f});
    current.endOfStream = true;
    enqueue(next, {0.4f});
    setDecks(mixer, current, &next);
    mixer.setCrossfade(4, GEODE_CROSSFADE_LINEAR);
    const auto output = render(mixer, 4);
    near(output[0], 0.3f, "available incoming mix");
    near(output[2], 0.05f, "missing incoming zero-filled");
    near(output[4], 0, "missing incoming after EOF");
    near(output[6], 0, "missing incoming after EOF tail");
    require(next.position() == 1, "only real incoming samples advance position");
}
} // namespace

int main() {
    try {
        for (int curve : {GEODE_CROSSFADE_LINEAR, GEODE_CROSSFADE_EQUAL_POWER, GEODE_CROSSFADE_SMOOTH}) {
            boundaryAndContinuity(curve);
        }
        outgoingStarvation();
        gaplessUnknownDurationAndFinalEof();
        incomingStarvationIsZeroFilled();
        std::cout << "PASS: 6 mixer regression scenarios\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
