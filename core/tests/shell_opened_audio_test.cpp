// Host-side checks for core/shell/OpenedAudio: the rules for an audio file another app opens in this one
// ("Open with"): the intent-routing decision, the provider refusals and the display-name sanitising. Every case
// of the Kotlin OpenedAudioTest (app/src/test/java/dev/geode/model/OpenedAudioTest.kt) is here with the same
// expected result, plus the JVM string behaviours the Kotlin leans on (Unicode whitespace, the case folding of
// ".milk", modified UTF-8 from JNI), and the C ABI the app calls them through (core/api/geode_shell_api.h). Text
// is UTF-8 here; non-ASCII is written as byte escapes.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/shell_opened_audio_test.cpp
//     core/shell/*.cpp core/api/geode_shell_api.cpp core/util/Json.cpp -o shell_opened_audio_test && ./shell_opened_audio_test
#include <array>
#include <cstdio>
#include <optional>
#include <string>

#include "api/geode_shell_api.h"
#include "shell/OpenedAudio.hpp"

using namespace geode::shell;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

using S = std::optional<std::string>;
const char* const kView = "android.intent.action.VIEW";
const std::string kOwn = "com.beatrealms3d.app";

OpenedRoute route(S action = std::string(kView), S scheme = std::string("content"), S type = std::string("audio/mpeg"),
                  S segment = std::string("song.mp3"), bool fromHistory = false) {
    return openedAudioRoute(action, scheme, type, segment, fromHistory);
}

void contentViewWithAudioTypeOpens() {
    check(route() == OpenedRoute::Open, "audio/mpeg");
    check(route(kView, std::string("content"), std::string("audio/flac")) == OpenedRoute::Open, "audio/flac");
    check(route(kView, std::string("content"), std::string("AUDIO/Ogg; codecs=opus")) == OpenedRoute::Open, "upper case with parameters");
    check(route(kView, std::string("content"), std::string("application/ogg")) == OpenedRoute::Open, "application/ogg");
    check(route(kView, std::string("content"), std::string("application/x-flac")) == OpenedRoute::Open, "application/x-flac");
}

void untypedOpensUnlessMilk() {
    check(route(kView, std::string("content"), std::nullopt, std::string("1234")) == OpenedRoute::Open, "untyped number");
    check(route(kView, std::string("content"), std::nullopt, std::string("Aderack - Space.MILK")) == OpenedRoute::NotOurs, "untyped milk");
    // ignoreCase in Kotlin folds a char through uppercase then lowercase: the Kelvin sign is a K, a dotless or dotted I an I.
    check(route(kView, std::string("content"), std::nullopt, std::string("x.mil\xE2\x84\xAA")) == OpenedRoute::NotOurs, "Kelvin sign");
    check(route(kView, std::string("content"), std::nullopt, std::string("x.m\xC4\xB1lk")) == OpenedRoute::NotOurs, "dotless i");
    check(route(kView, std::string("content"), std::nullopt, std::string("x.m\xC4\xB0lk")) == OpenedRoute::NotOurs, "dotted I");
    // The segment is trimmed with Kotlin's trim (Unicode whitespace) first.
    check(route(kView, std::string("content"), std::nullopt, std::string("x.milk\xC2\xA0 ")) == OpenedRoute::NotOurs, "trailing no-break space");
    check(route(kView, std::string("content"), std::nullopt, std::nullopt) == OpenedRoute::Open, "no segment is not a milk");
    check(route(kView, std::string("content"), std::nullopt, std::string("milk")) == OpenedRoute::Open, "no dot");
}

void othersAreLeftToOtherHandlers() {
    check(route(std::string("android.intent.action.SEND")) == OpenedRoute::NotOurs, "SEND");
    check(route(std::nullopt) == OpenedRoute::NotOurs, "no action");
    check(route(kView, std::string("file")) == OpenedRoute::NotOurs, "file");
    check(route(kView, std::string("CONTENT")) == OpenedRoute::NotOurs, "CONTENT");
    check(route(kView, std::string("geode")) == OpenedRoute::NotOurs, "geode");
    check(route(kView, std::nullopt) == OpenedRoute::NotOurs, "no scheme");
    check(route(kView, std::string("content"), std::string("video/mp4")) == OpenedRoute::NotOurs, "video");
    check(route(kView, std::string("content"), std::string("audio/")) == OpenedRoute::NotOurs, "audio/ alone");
    check(route(kView, std::string("content"), std::string("application/octet-stream"), std::string("x.milk")) == OpenedRoute::NotOurs, "octet-stream milk");
    check(route(kView, std::string("content"), std::string("*/*")) == OpenedRoute::NotOurs, "*/*");
}

void replayedFromRecentsIsDropped() {
    check(route(kView, std::string("content"), std::string("audio/mpeg"), std::string("song.mp3"), true) == OpenedRoute::Stale, "stale");
    check(route(kView, std::string("content"), std::string("video/mp4"), std::string("song.mp3"), true) == OpenedRoute::NotOurs, "video from history");
}

bool refuses(S scheme = std::string("content"), S authority = std::string("com.example.files.provider"),
             S provider = std::string("com.example.files"), bool granted = true) {
    return openedAudioRefuses(scheme, authority, kOwn, provider, granted);
}

void grantedUriOfAnotherAppPlays() {
    check(!refuses(), "granted");
    check(!refuses(std::string("content"), std::string("media"), std::string("com.android.providers.media.module")), "media");
}

void ownProvidersAreRefused() {
    check(refuses(std::string("content"), kOwn + ".presets", kOwn), "own presets, own provider");
    check(refuses(std::string("content"), kOwn + ".presets", std::string("com.example.files")), "own authority, other provider");
    check(refuses(std::string("content"), kOwn, std::nullopt), "own authority, no provider");
    check(refuses(std::string("content"), std::string("lib.authority"), kOwn), "other authority, own provider");
}

void ungrantedUnknownOrMalformedIsRefused() {
    check(refuses(std::string("content"), std::string("com.example.files.provider"), std::string("com.example.files"), false), "ungranted");
    check(refuses(std::string("content"), std::string("com.example.files.provider"), std::nullopt), "unknown provider");
    check(refuses(std::string("content"), std::nullopt), "no authority");
    check(refuses(std::string("content"), std::string(" ")), "blank authority");
    check(refuses(std::string("content"), std::string("\xE3\x80\x80\xC2\xA0")), "unicode blank authority");
    check(refuses(std::string("content"), std::string("")), "empty authority");
    check(refuses(std::string("file")), "file");
    check(refuses(std::string("android.resource")), "android.resource");
    check(refuses(std::nullopt), "no scheme");
}

std::string name(const S& raw) {
    const auto n = openedAudioDisplayName(raw);
    return n ? *n : std::string("<none>");
}

void nameLosesExtensionKeepsWords() {
    check(name(std::string("Song.mp3")) == "Song", "Song.mp3");
    check(name(std::string("Mr. Blue Sky")) == "Mr. Blue Sky", "no extension, dot in words");
    check(name(std::string("Mr. Blue Sky.flac")) == "Mr. Blue Sky", "flac");
    check(name(std::string(".mp3")) == ".mp3", "only an extension");
    check(name(std::string("a.toolong")) == "a.toolong", "six letters is no extension");
    check(name(std::string("a.m p3")) == "a.m p3", "a space is no extension");
    check(name(std::string("Song .mp3")) == "Song", "stem trimmed at the end");
    check(name(std::string("a.")) == "a.", "empty extension is none");
}

void controlAndBidiDroppedWhitespaceFolded() {
    check(name(std::string("\xE2\x80\xAE" "evil")) == "evil", "bidi override");
    check(name(std::string("  a\n\t  b\0  ", 11)) == "a b", "whitespace folded, NUL dropped");
    check(name(std::string("a\xE2\x80\x8B" "b")) == "ab", "zero width space");
    check(name(std::string("a\xED\xA0\x80" "b")) == "ab", "lone surrogate");
    check(name(std::string("a\xC0\x80" "b")) == "ab", "modified UTF-8 NUL");
    check(name(std::string("a\xC2\xA0\xE3\x80\x80" "b")) == "a b", "no-break and ideographic space fold");
    check(name(std::string("a\xC2\x85" "b")) == "ab", "NEL is a control, not whitespace");
    check(name(std::string("a\xEE\x80\x80" "b")) == "ab", "private use");
    check(name(std::string("a\xF3\xA0\x80\x80" "b")) == "ab", "unassigned/format plane 14 start");
    check(name(std::string("a\xF4\x8F\xBF\xBF" "b")) == "ab", "noncharacter at the end of the plane 16");
}

void nothingVisibleIsNoName() {
    check(name(std::nullopt) == "<none>", "null");
    check(name(std::string("")) == "<none>", "empty");
    check(name(std::string(" \xE2\x80\xAE\x07 ")) == "<none>", "only hidden");
}

void longNameIsCappedWithoutSplittingAPair() {
    std::string emoji;
    for (int i = 0; i < 500; ++i) emoji += "\xF0\x9F\x8E\xB5";
    const auto n = openedAudioDisplayName(emoji);
    check(n && n->size() == 120u * 4u, "120 code points of 4 bytes");
    check(n && n->substr(n->size() - 4) == "\xF0\x9F\x8E\xB5", "ends on a whole emoji");
    const auto x = openedAudioDisplayName(std::string(10000, 'x'));
    check(x && static_cast<int>(x->size()) == kOpenedNameMaxCodePoints, "x capped");
    // The same emoji as JNI's modified UTF-8 (a surrogate pair of two three-byte forms) is one code point.
    const auto m = openedAudioDisplayName(std::string("\xED\xA0\xBC\xED\xBE\xB5"));
    check(m && *m == "\xF0\x9F\x8E\xB5", "CESU pair joined");
    // Spaces count toward the cap; whatever follows the cap is ignored, trailing spaces included.
    std::string spaced;
    for (int i = 0; i < 119; ++i) spaced += 'a';
    spaced += "  b";
    const auto sp = openedAudioDisplayName(spaced);
    check(sp && sp->size() == 120u && sp->back() == ' ', "the space took the last place");
}

void openedIdsAreFreshMarkedAndCarryNoUri() {
    std::array<std::uint8_t, 16> r1{};
    std::array<std::uint8_t, 16> r2{};
    for (int i = 0; i < 16; ++i) {
        r1[static_cast<size_t>(i)] = static_cast<std::uint8_t>(i * 7 + 1);
        r2[static_cast<size_t>(i)] = static_cast<std::uint8_t>(i * 11 + 3);
    }
    const std::string a = openedAudioMediaId(r1);
    const std::string b = openedAudioMediaId(r2);
    check(openedAudioIsOpened(a), "marked");
    check(a != b, "distinct");
    check(a.size() == std::string(kOpenedMediaIdPrefix).size() + 36, "a uuid after the prefix");
    check(a[std::string(kOpenedMediaIdPrefix).size() + 14] == '4', "version 4");
    check(!openedAudioIsOpened(std::string("content://media/external/audio/media/1")), "a uri is not one");
    check(!openedAudioIsOpened(std::nullopt), "null");
}

std::string nameOut(const char* raw) {
    char buf[512];
    const int need = geode_opened_audio_display_name(raw, buf, static_cast<int>(sizeof buf));
    if (need < 0) return "<none>";
    check(need < static_cast<int>(sizeof buf), "the name fits the test's buffer");
    return std::string(buf);
}

void openedAudioThroughTheAbi() {
    const char* view = "android.intent.action.VIEW";
    check(geode_opened_audio_route(view, "content", "audio/mpeg", "x", 0) == GEODE_OPENED_OPEN, "abi: open");
    check(geode_opened_audio_route(view, "content", "audio/mpeg", "x", 1) == GEODE_OPENED_STALE, "abi: stale");
    check(geode_opened_audio_route(view, "content", nullptr, "a.milk", 0) == GEODE_OPENED_NOT_OURS, "abi: milk untyped");
    check(geode_opened_audio_route(view, "content", nullptr, nullptr, 0) == GEODE_OPENED_OPEN, "abi: untyped, no name");
    check(geode_opened_audio_route(nullptr, "content", "audio/mpeg", "x", 0) == GEODE_OPENED_NOT_OURS, "abi: no action");
    check(geode_opened_audio_route(view, "file", "audio/mpeg", "x", 0) == GEODE_OPENED_NOT_OURS, "abi: a file uri");

    check(geode_opened_audio_refuses("content", "com.other.files", "dev.geode", "com.other", 1) == 0, "abi: granted plays");
    check(geode_opened_audio_refuses("content", "com.other.files", "dev.geode", "com.other", 0) == 1, "abi: ungranted");
    check(geode_opened_audio_refuses("content", "dev.geode.provider", "dev.geode", "dev.geode", 1) == 1, "abi: own provider");
    check(geode_opened_audio_refuses("content", "com.other.files", "dev.geode", nullptr, 1) == 1, "abi: no provider");
    check(geode_opened_audio_refuses("content", nullptr, "dev.geode", "com.other", 1) == 1, "abi: no authority");

    check(nameOut("Mr. Blue Sky.flac") == "Mr. Blue Sky", "abi: the extension cut");
    check(nameOut("  a\n\t  b  ") == "a b", "abi: whitespace folded");
    check(nameOut(nullptr) == "<none>", "abi: no name");
    check(nameOut("\xE2\x80\xAE") == "<none>", "abi: nothing visible");
    // A lone surrogate from the JVM, as its own three bytes, is dropped like the other hidden code points.
    check(nameOut("ab\xED\xA0\x80" "c") == "abc", "abi: a lone surrogate dropped");
    // A NUL from the JVM comes as C0 80 (a plain NUL would end the C string): dropped, the rest kept.
    check(nameOut("ab\xC0\x80" "cd") == "abcd", "abi: a NUL dropped");
    check(geode_opened_audio_route(view, "content", "audio/\xC0\x80" "x", "x", 0) == GEODE_OPENED_OPEN, "abi: a NUL in the type");
    char small[3] = {'z', 'z', 'z'};
    check(geode_opened_audio_display_name("Song.mp3", small, 3) == 4 && std::string(small) == "So", "abi: a short buffer");

    unsigned char random[16];
    for (int i = 0; i < 16; ++i) random[i] = static_cast<unsigned char>(i * 7 + 1);
    char id[64];
    const int n = geode_opened_audio_media_id(random, id, static_cast<int>(sizeof id));
    std::array<std::uint8_t, 16> same{};
    for (int i = 0; i < 16; ++i) same[static_cast<size_t>(i)] = random[i];
    check(n == static_cast<int>(openedAudioMediaId(same).size()) && std::string(id) == openedAudioMediaId(same), "abi: the id");
    check(geode_opened_audio_is_opened(id) == 1, "abi: marked");
    check(geode_opened_audio_is_opened("content://media/1") == 0 && geode_opened_audio_is_opened(nullptr) == 0, "abi: not one");
}

}  // namespace

int main() {
    contentViewWithAudioTypeOpens();
    untypedOpensUnlessMilk();
    othersAreLeftToOtherHandlers();
    replayedFromRecentsIsDropped();
    grantedUriOfAnotherAppPlays();
    ownProvidersAreRefused();
    ungrantedUnknownOrMalformedIsRefused();
    nameLosesExtensionKeepsWords();
    controlAndBidiDroppedWhitespaceFolded();
    nothingVisibleIsNoName();
    longNameIsCappedWithoutSplittingAPair();
    openedIdsAreFreshMarkedAndCarryNoUri();
    openedAudioThroughTheAbi();
    if (failures == 0) std::printf("shell_opened_audio_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
