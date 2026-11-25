// Host test: a reader that loses its source forgets what it heard (sound/interp/FrameReader::forget). The frame, the
// feeling and the hit positions the renderer copies out of the analysis are true only while the analysis is
// there; once the source is gone they must not keep the picture lit or its triggers owed.
// Built by CI with frame_blend_test's sources.
#include <cstdint>
#include <cstdio>

#include "sound/Emotion.hpp"
#include "sound/SoundFrame.h"
#include "sound/interp/FrameBlend.hpp"
#include "sound/interp/Hits.hpp"

using geode::sound::Emotion;
using geode::sound::interp::FrameReader;
using geode::sound::interp::Hits;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

void testForgetClearsWhatWasHeard() {
    FrameReader reader;
    GeodeSoundFrame frame{};
    Emotion::State emotion{};
    Hits hits;
    frame.bass = 2.0f;
    frame.presence = 1.0f;
    frame.kick = 1.0f;
    frame.sectionId = 3.0f;
    emotion.valence = 0.7f;
    emotion.energy = 0.9f;
    hits.at[0] = 1234;
    hits.at[GEODE_SOUND_SCALARS - 1] = 99;

    reader.forget(frame, emotion, hits);

    check(frame.bass == 0.0f && frame.presence == 0.0f && frame.kick == 0.0f && frame.sectionId == 0.0f,
          "the frame is the silent one a fresh renderer holds");
    check(emotion.valence == 0.0f && emotion.energy == 0.0f, "the feeling starts over");
    bool none = true;
    for (const int64_t at : hits.at) none = none && at == -1;
    check(none, "no trigger is owed a position");
}

}  // namespace

int main() {
    testForgetClearsWhatWasHeard();
    if (failures) {
        std::printf("heard_forget_test: %d failed\n", failures);
        return 1;
    }
    std::printf("heard_forget_test: all passed\n");
    return 0;
}
