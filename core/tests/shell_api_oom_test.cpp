// Host-side checks that the C ABI entries an audio file another app opens reaches (core/api/geode_shell_api.cpp)
// fail closed when memory runs out, instead of letting std::bad_alloc through to JNI. Built and run by CI
// ("Native host tests") with the system compiler; no device.
#include <cstdio>
#include <string>

#include "api/geode_shell_api.h"
#include "throwing_new.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

const std::string kAuthority = "com.example.music.documents.provider";
const std::string kName = "A song with a name long enough to need the heap.mp3";

void openedAudioFailsClosed() {
    char name[128] = {};
    int route = -2;
    int refuses = -2;
    int named = -2;
    int opened = -2;
    int thrown = 0;
    {
        const oom::Armed armed;
        route = geode_opened_audio_route("android.intent.action.VIEW", "content", "audio/mpeg-long-enough-type", 0);
        refuses = geode_opened_audio_refuses("content", kAuthority.c_str(), "dev.geode", kAuthority.c_str(), 1);
        named = geode_opened_audio_display_name(kName.c_str(), name, static_cast<int>(sizeof name));
        opened = geode_opened_audio_is_opened("geode:opened:0123456789abcdef0123456789abcdef");
        thrown = oom::thrown;
    }
    check(thrown > 0, "the opened-audio entries asked for memory and were refused it");
    check(route == GEODE_OPENED_NOT_OURS, "a route read without memory leaves the intent to other handlers");
    check(refuses == 1, "a grant checked without memory is refused");
    check(named == -1, "a name read without memory is no name");
    check(opened == 1, "an id read without memory counts as an opened file, so its uri is not kept");
}

}  // namespace

int main() {
    openedAudioFailsClosed();
    if (failures == 0) std::printf("shell_api_oom_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
