// C ABI over the Customize slider maths (viz/SliderMap). See geode_api.h.
#include "api/geode_api.h"
#include "viz/SliderMap.hpp"

namespace slider = geode::viz::slider;

int geode_slider_off_default(float value, float def) {
    return slider::offDefault(value, def) ? 1 : 0;
}

int geode_slider_int_steps(int first, int last) {
    return slider::intSteps(first, last);
}

int geode_slider_int_value(float position, int first, int last) {
    return slider::intValue(position, first, last);
}

float geode_slider_log_position(float value, float lo, float hi) {
    return slider::logPosition(value, lo, hi);
}

float geode_slider_log_value(float position, float lo, float hi) {
    return slider::logValue(position, lo, hi);
}
