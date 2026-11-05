#include "api/geode_api.h"
#include "output/TakeSession.hpp"

using geode::output::TakeSession;

extern "C" {

int geode_take_begin(int fd, float display_fps) { return TakeSession::shared().begin(fd, display_fps) ? 1 : 0; }

int64_t geode_take_end(void) { return TakeSession::shared().end(); }

int geode_take_active(void) { return TakeSession::shared().active() ? 1 : 0; }

void geode_take_write_audio(const int16_t* interleaved, int frames, int channels, int sample_rate) {
    TakeSession::shared().writeAudio(interleaved, frames, channels, sample_rate);
}

}
