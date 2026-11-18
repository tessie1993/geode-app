#include "shell/ExportFps.hpp"

#include <algorithm>
#include <array>

#include "output/EncoderLadder.hpp"

namespace geode::shell {

namespace {

namespace ladder = geode::output;

// Ascending, as the UI lists them.
constexpr std::array<int, 5> kOptions{ladder::kLadderMinFps, ladder::fallbackFps(50), ladder::kLadderFloorFps, 50,
                                      ladder::kLadderMaxFps};

static_assert(kOptions[0] == 24 && kOptions[1] == 25 && kOptions[2] == 30 && kOptions[3] == 50 && kOptions[4] == 60,
              "the offered rates are 24, 25, 30, 50, 60");
static_assert(std::is_sorted(kOptions.begin(), kOptions.end()), "ascending");
static_assert(kOptions.front() >= ladder::kLadderMinFps && kOptions.back() <= ladder::kLadderMaxFps,
              "inside the ladder's clamp");
static_assert(kExportDefaultFps == ladder::kLadderFloorFps, "the default is the floor, the best a free user may render");

}  // namespace

std::vector<int> exportFpsOptions() { return std::vector<int>(kOptions.begin(), kOptions.end()); }

bool exportFpsAllowed(int fps) { return std::find(kOptions.begin(), kOptions.end(), fps) != kOptions.end(); }

int exportFpsRead(std::optional<int> stored, int fallback) {
    const int value = stored.value_or(fallback);
    return exportFpsAllowed(value) ? value : fallback;
}

int exportFpsFromJson(const json::Value* exportBlock, int fallback) {
    std::optional<int> read;
    if (exportBlock) {
        if (const json::Value* v = exportBlock->get("fps")) read = json::asInt(*v);
    }
    return exportFpsRead(read, fallback);
}

}  // namespace geode::shell
