// Host-side checks that the C ABI entries a shared link or an imported preset or template reaches
// (core/api/geode_looks_api.cpp) answer "unreadable" when memory runs out, instead of letting std::bad_alloc
// through to JNI. Built and run by CI ("Native host tests") with the system compiler; no device.
#include <cstdio>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "throwing_new.hpp"
#include "viz/Params.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

const std::string kLink = "geode://preset/" + std::string(40, 'A');
const std::string kText = "Shared with you: " + kLink + " and geode://template/" + std::string(40, 'B');
const std::string kJson = R"({"name": "A preset named at some length", "scene": "unknown", "params": {}})";

void linksAnswerNothing() {
    check(geode_preset_link_payload(kLink.c_str()) != nullptr, "the link reads with memory to spare");
    const char* payload = nullptr;
    const char* found = nullptr;
    const char* templateLink = nullptr;
    int accepted = -1;
    int thrown = 0;
    {
        const oom::Armed armed;
        payload = geode_preset_link_payload(kLink.c_str());
        found = geode_preset_link_find(kText.c_str());
        templateLink = geode_template_link_in(kText.c_str());
        accepted = geode_preset_link_body_accepted(kJson.c_str());
        thrown = oom::thrown;
    }
    check(thrown > 0, "the link entries asked for memory and were refused it");
    check(payload == nullptr && found == nullptr && templateLink == nullptr, "a link read without memory is no link");
    check(accepted == 0, "a body read without memory is not accepted");
}

void readsAnswerUnreadable() {
    std::vector<float> frame(static_cast<size_t>(geode::viz::SceneParams::kFieldCount));
    const char* strings[6] = {};
    float envelope[2] = {};
    int preset = -2;
    int templateStatus = -2;
    int thrown = 0;
    {
        const oom::Armed armed;
        preset = geode_preset_read(kJson.c_str(), frame.data(), static_cast<int>(frame.size()), strings, envelope);
        templateStatus = geode_template_read(kJson.c_str(), frame.data(), static_cast<int>(frame.size()), strings, envelope);
        thrown = oom::thrown;
    }
    check(thrown > 0, "the reads asked for memory and were refused it");
    check(preset == 0, "a preset read without memory is unreadable");
    check(templateStatus == -1, "a template read without memory is malformed");
}

}  // namespace

int main() {
    linksAnswerNothing();
    readsAnswerUnreadable();
    if (failures == 0) std::printf("looks_api_oom_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
