#include <string>

#include "api/geode_api.h"
#include "viz/ParamsJson.hpp"

using geode::viz::SceneParams;

extern "C" {

const char* geode_params_write_json(const float* frame, int n, const char* id_a, const char* id_b) {
    if (!frame || n < SceneParams::kFieldCount) return nullptr;
    thread_local std::string text;
    text = geode::viz::writeParamsJson(frame, id_a ? id_a : "", id_b ? id_b : "");
    return text.c_str();
}

int geode_params_read_json(const char* document, const char* path, const char* fallback, float* out_frame, int n,
                           const char** id_a, const char** id_b) {
    if (!out_frame || n < SceneParams::kFieldCount) return 0;
    thread_local std::string first;
    thread_local std::string second;
    SceneParams params;
    if (!geode::viz::readParamsJson(document ? document : "", path ? path : "", fallback ? fallback : "", params, first,
                                    second)) {
        return 0;
    }
    params.toWire(out_frame);
    if (id_a) *id_a = first.c_str();
    if (id_b) *id_b = second.c_str();
    return 1;
}

}
