#pragma once

#include <optional>
#include <vector>

#include "util/Json.hpp"

// The frame rates an export offers and accepts, ported from Kotlin data/ExportPrefs.kt (EXPORT_FPS_OPTIONS
// and the rejection in ExportPrefsStore.read) and data/EditorProjectJson.kt (the same check on a project's
// export block). The rule is written once, in terms of the encoder ladder's limits (output/EncoderLadder.hpp:
// kLadderMinFps, kLadderFloorFps, kLadderMaxFps and fallbackFps), so the ladder and the rates the UI offers
// cannot drift apart: the lowest rate, the floor, the highest rate, 50 (the one rate above the floor whose
// retry is not the floor) and that retry, 25. Nothing outside this set is ever persisted or requested.
// Host-tested in core/tests/shell_prefs_test.cpp.
namespace geode::shell {

// An export's frame rate before the user picks one: the best a free user may render, so a first free render
// never meets the paywall. (DEFAULT_EXPORT_FPS)
inline constexpr int kExportDefaultFps = 30;

// The rates offered anywhere in the UI, ascending: 24, 25, 30, 50, 60. (EXPORT_FPS_OPTIONS)
std::vector<int> exportFpsOptions();

bool exportFpsAllowed(int fps);

// A stored rate (nullopt: nothing stored, so the fallback) kept when offered, else the fallback itself, which
// is returned as given. (ExportPrefsStore.read's fps)
int exportFpsRead(std::optional<int> stored, int fallback);

// The same for a project's export block: the block's "fps" member read as org.json's optInt(name, fallback)
// does (a number or numeric string, truncated; anything else, or missing, is the fallback), then kept only
// when offered. exportBlock may be null (no block). (EditorProjectJson.export's fps)
int exportFpsFromJson(const json::Value* exportBlock, int fallback);

}  // namespace geode::shell
