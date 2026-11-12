#include <memory>

#include "api/geode_api.h"
#include "viz/frame/CastTee.hpp"

using geode::viz::frame::CastTee;

// The handle the cast thread holds; the stage's renderer finds the same tee through CastTee::current().
struct geode_cast_tee {
    std::shared_ptr<CastTee> tee;
};

geode_cast_tee* geode_cast_tee_open(int width, int height, int fps) {
    auto tee = std::make_shared<CastTee>(width, height, fps);
    if (!tee->ok()) return nullptr;
    CastTee::setCurrent(tee);
    return new geode_cast_tee{std::move(tee)};
}

int geode_cast_tee_draw(geode_cast_tee* t, int64_t now_ns, int width, int height) {
    return t && t->tee && t->tee->draw(now_ns, width, height) ? 1 : 0;
}

void geode_cast_tee_close(geode_cast_tee* t) {
    if (!t) return;
    if (CastTee::current() == t->tee) CastTee::setCurrent(nullptr);
    if (t->tee) t->tee->releaseReader();
    delete t;
}
