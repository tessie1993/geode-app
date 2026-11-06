#include "api/geode_api.h"
#include "audio/LiveInput.hpp"

namespace live = geode::audio::live_input;

extern "C" {

int geode_live_input_step(int state, int event, int has_permission, unsigned* actions_out) {
    if (state < 0 || state > static_cast<int>(live::State::Lost) || event < 0 ||
        event > static_cast<int>(live::Event::CaptureStopped)) {
        if (actions_out) *actions_out = 0;
        return state;
    }
    const live::Step s = live::step(static_cast<live::State>(state), static_cast<live::Event>(event), has_permission != 0);
    if (actions_out) *actions_out = s.actions;
    return static_cast<int>(s.next);
}

int geode_live_input_is_on(int state) {
    return live::isOn(static_cast<live::State>(state)) ? 1 : 0;
}

}  // extern "C"
