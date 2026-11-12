#pragma once
#include <string>
#include <string_view>

#include "viz/Params.hpp"

namespace geode::viz {

// The scene params object of a preset, a shared link, a template's look and the saved live look (#1150): every
// live row of viz/ParamTable.inc under its name, then the two saved palette slots' ids. The text is what the
// shell's org.json wrote before it moved here, byte for byte: a Bool as true/false, an Int as Kotlin's
// roundToInt, a Float or Circular as org.json writes the float widened to a double (json::numberText), in slot
// order, compact. A non-finite float or int, which org.json refused to write, is written as its row's default.
inline constexpr const char* kCustomPaletteIdKey = "customPaletteId";
inline constexpr const char* kCustomPalette2IdKey = "customPalette2Id";

// `wire` is the geode_viz_set_params frame (kFieldCount floats); a retired slot is not written.
std::string writeParamsJson(const float* wire, std::string_view customPaletteId, std::string_view customPalette2Id);
std::string writeParamsJson(const SceneParams& params, std::string_view customPaletteId, std::string_view customPalette2Id);

// Reads the object the way the shell's org.json did (json::asBoolean, asInt, asDouble, asString): a key that is
// missing or will not read as its row's type takes the row's default, a retired or unknown key is ignored, and
// the frame is then SceneParams::sanitized. An id missing reads as "". False, nothing written, when `text` is not
// a JSON object.
bool readParamsJson(std::string_view text, SceneParams& out, std::string& customPaletteId, std::string& customPalette2Id);

// The same, from the text org.json itself parsed, so nothing is printed again first (a double that org.json
// printed whole would read as a long): the object at `path` (keys of nested objects, newline-separated; "" for
// the top) of `document` when that is standard JSON (json::parseStandard), which org.json reads alike. Anything
// else org.json took (comments, single quotes, octal) reads `fallback`, the object as org.json printed it.
bool readParamsJson(std::string_view document, std::string_view path, std::string_view fallback, SceneParams& out,
                    std::string& customPaletteId, std::string& customPalette2Id);

}  // namespace geode::viz
