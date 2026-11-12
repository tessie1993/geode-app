// Host-side checks for the saved text of the user's reaction routes (core/viz/ReactionText):
// text an earlier build saved reads back the same, version line, dropped lines, defaults, clamps, the 12-route cap. Built and run by CI
// (.github/workflows/native-host-tests.yml, "Reaction text host tests"); no GL.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "api/geode_reaction_text_api.h"
#include "viz/Lfo.hpp"
#include "viz/ReactionText.hpp"
#include "viz/Reactions.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

constexpr int kFloats = ReactionEngine::kConfigFloats;
constexpr int kRoom = ReactionEngine::kRoutes * kFloats;

struct Route {
    bool enabled;
    ReactSource source;
    LfoTarget target;
    ReactMode mode;
    float amount;
    ModCurve curve;
    float attack;
    float release;
};

std::vector<float> pack(const std::vector<Route>& routes) {
    std::vector<float> out;
    for (const Route& r : routes) {
        out.insert(out.end(), {r.enabled ? 1.0f : 0.0f, static_cast<float>(r.source), static_cast<float>(r.target),
                               static_cast<float>(r.mode), r.amount, static_cast<float>(r.curve), r.attack, r.release});
    }
    return out;
}

// The decoded routes; `count` is -1 for text that is no list.
struct Decoded {
    int count;
    float routes[kRoom];
    const float* at(int i) const { return routes + i * kFloats; }
};

Decoded decode(const std::string& text) {
    Decoded d{};
    d.count = reactiontext::decode(text, d.routes);
    return d;
}

const std::vector<Route> kDna = {
    {true, ReactSource::Kick, LfoTarget::ColorShift, ReactMode::Jump, 0.35f, ModCurve::Linear, 0.08f, 0.35f},
    {true, ReactSource::Snare2, LfoTarget::Warp, ReactMode::Follow, 0.6f, ModCurve::Smooth, 0.03f, 0.45f},
    {true, ReactSource::HardCut, LfoTarget::CamFly, ReactMode::Follow, 1.1f, ModCurve::Linear, 0.25f, 2.2f},
    {false, ReactSource::Kick4, LfoTarget::Rotation, ReactMode::Step, -0.2f, ModCurve::Linear, 0.12f, 0.12f},
};

// kDna as installs have it saved.
const char* const kSavedText =
    "reactions/2\n"
    "true|KICK|COLOR_SHIFT|JUMP|0.35|LINEAR|0.08|0.35\n"
    "true|SNARE_2|WARP|FOLLOW|0.6|SMOOTH|0.03|0.45\n"
    "true|HARD_CUT|CAM_FLY|FOLLOW|1.1|LINEAR|0.25|2.2\n"
    "false|KICK_4|ROTATION|STEP|-0.2|LINEAR|0.12|0.12";

void roundTrip() {
    const std::vector<float> packed = pack(kDna);
    const std::string text = reactiontext::encode(packed.data(), static_cast<int>(kDna.size()));
    check(text == kSavedText, "encode writes the saved text: " + text);
    const Decoded d = decode(text);
    check(d.count == static_cast<int>(kDna.size()), "every route reads back");
    check(d.count > 0 && std::memcmp(d.routes, packed.data(), packed.size() * sizeof(float)) == 0, "the routes round-trip exactly");
    const Decoded old = decode(kSavedText);
    check(old.count == d.count && std::memcmp(old.routes, d.routes, sizeof d.routes) == 0, "saved text reads the same");
}

void emptyAndMissing() {
    const std::string none = reactiontext::encode(nullptr, 0);
    check(none == "reactions/2", "an empty list is the version line alone");
    check(decode(none).count == 0, "an empty list is kept, not read as nothing saved");
    check(decode("").count == -1, "empty text is no list");
    check(decode("[{\"source\":\"KICK\"}]").count == -1, "the old JSON is no list");
    check(decode("reactions/99\ntrue|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3").count == -1, "an unknown version is no list");
    check(decode("reactions/1\ntrue|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3").count == -1, "the old analyser's routes reset to the DNA");
    check(decode("  reactions/2 \r\n\r\n  true|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3  \r\n").count == 1, "lines are trimmed, blanks skipped");
}

void corruptedLines() {
    const Decoded d = decode("reactions/2\n"
                             "true|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3\n"
                             "this line is garbage\n"
                             "true|SNARE|TWIST\n"
                             "||||||||\n"
                             "true|HARD_CUT|CAM_FLY|STEP|1.0|SMOOTH|0.2|2.0");
    check(d.count == 2, "a corrupted line drops only itself");
    check(d.count == 2 && d.at(0)[2] == static_cast<float>(LfoTarget::Zoom) && d.at(1)[2] == static_cast<float>(LfoTarget::CamFly),
          "the good lines keep their order");
}

void unknownNames() {
    const Decoded d = decode("reactions/2\n"
                             "true|THEREMIN|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3\n"
                             "true|KICK|WARP_DRIVE|FOLLOW|0.3|LINEAR|0.05|0.3\n"
                             "true|KICK|NONE|FOLLOW|0.3|LINEAR|0.05|0.3\n"
                             "true|KICK|PULSE|FOLLOW|0.3|LINEAR|0.05|0.3\n"
                             "true|KICK|GLITCH|FOLLOW|0.3|LINEAR|0.05|0.3\n"
                             "maybe|HAT|TWIST|WOBBLE|0.4|SQUIGGLE|0.05|0.3");
    check(d.count == 1, "an unknown source or target, or the target NONE, drops the route");
    if (d.count != 1) return;
    const float* r = d.at(0);
    check(r[1] == static_cast<float>(ReactSource::Hat) && r[2] == static_cast<float>(LfoTarget::Twist), "the known route is read");
    check(r[0] == 1.0f, "a flag that is not true/false is on, a route's default");
    check(r[3] == static_cast<float>(ReactMode::Follow), "an unknown mode is Follow");
    check(r[5] == static_cast<float>(ModCurve::Linear), "an unknown curve is Linear");
    check(r[4] == 0.4f, "its numbers are still read");
}

void numbers() {
    const Decoded d = decode("reactions/2\n"
                             "true|KICK|ZOOM|FOLLOW|NaN|LINEAR|-4|1e9\n"
                             "true|KICK|TWIST|FOLLOW|99|LINEAR|abc|Infinity\n"
                             "true|KICK|WARP|FOLLOW|-99|LINEAR|1.0E-4|7.5f\n"
                             "true|KICK|SWAY|FOLLOW||LINEAR|99|");
    check(d.count == 4, "bad numbers never drop a route");
    if (d.count != 4) return;
    const ReactRoute defaults;
    check(d.at(0)[4] == defaults.amount, "NaN takes the default amount");
    check(d.at(0)[6] == 0.0f, "a negative attack clamps to 0");
    check(d.at(0)[7] == reactiontext::kMaxReleaseSeconds, "a huge release clamps to the maximum");
    check(d.at(1)[4] == reactiontext::kMaxAmount, "a huge amount clamps to the maximum");
    check(d.at(1)[6] == defaults.attackSeconds, "text takes the default attack");
    check(d.at(1)[7] == defaults.releaseSeconds, "Infinity takes the default release");
    check(d.at(2)[4] == reactiontext::kMinAmount, "a hugely negative amount clamps to the minimum");
    check(d.at(2)[6] == 1.0e-4f, "Float.toString's exponent form reads");
    check(d.at(2)[7] == 7.5f, "a Java float suffix reads");
    check(d.at(3)[4] == defaults.amount && d.at(3)[7] == defaults.releaseSeconds, "an empty number takes its default");
    check(d.at(3)[6] == reactiontext::kMaxAttackSeconds, "a huge attack clamps to the maximum");
}

void cap() {
    const std::vector<Route> many(40, Route{true, ReactSource::Kick, LfoTarget::Zoom, ReactMode::Follow, 0.3f, ModCurve::Linear, 0.05f, 0.3f});
    const std::vector<float> packed = pack(many);
    const std::string text = reactiontext::encode(packed.data(), static_cast<int>(many.size()));
    int lines = 1;
    for (char c : text) lines += c == '\n' ? 1 : 0;
    check(lines == ReactionEngine::kRoutes + 1, "encode writes at most the matrix's routes");
    std::string longText = "reactions/2";
    for (int i = 0; i < 40; ++i) longText += "\ntrue|KICK|ZOOM|FOLLOW|0.3|LINEAR|0.05|0.3";
    check(decode(longText).count == ReactionEngine::kRoutes, "decode reads at most the matrix's routes");
}

void encodeSkipsWhatItCannotName() {
    std::vector<float> packed = pack({kDna[0], kDna[1]});
    packed[1] = 9999.0f;
    packed[kFloats + 5] = 77.0f;
    const std::string text = reactiontext::encode(packed.data(), 2);
    check(text == "reactions/2\ntrue|SNARE_2|WARP|FOLLOW|0.6|LINEAR|0.03|0.45", "an unknown source is left out, an unknown curve is Linear");
}

void abi() {
    const std::vector<float> packed = pack(kDna);
    const size_t need = geode_reactions_text_encode(packed.data(), static_cast<int>(packed.size()), nullptr, 0);
    check(need == std::strlen(kSavedText), "the ABI reports the text's length");
    std::vector<char> small(need, '#');
    geode_reactions_text_encode(packed.data(), static_cast<int>(packed.size()), small.data(), small.size());
    check(small[0] == '#', "a buffer too small for the NUL is left alone");
    std::vector<char> buffer(need + 1, '#');
    geode_reactions_text_encode(packed.data(), static_cast<int>(packed.size()), buffer.data(), buffer.size());
    check(std::strcmp(buffer.data(), kSavedText) == 0, "the ABI writes the text");
    float out[kRoom] = {};
    check(geode_reactions_text_decode(nullptr, out, kRoom) == -1, "null text is no list");
    check(geode_reactions_text_decode("reactions/1", out, kRoom) == -1, "another version is no list");
    check(geode_reactions_text_decode(kSavedText, out, kRoom) == 4, "the ABI reads the routes");
    check(std::memcmp(out, packed.data(), packed.size() * sizeof(float)) == 0, "in the routes layout");
    check(geode_reactions_text_decode(kSavedText, out, 2 * kFloats) == 2, "never past the capacity");
    check(geode_reactions_text_encode(nullptr, 0, nullptr, 0) == std::strlen("reactions/2"), "no routes is the version line");
}

}  // namespace

int main() {
    roundTrip();
    emptyAndMissing();
    corruptedLines();
    unknownNames();
    numbers();
    cap();
    encodeSkipsWhatItCannotName();
    abi();
    if (failures) {
        std::printf("%d reaction text check(s) failed\n", failures);
        return 1;
    }
    std::printf("reaction text checks passed\n");
    return 0;
}
