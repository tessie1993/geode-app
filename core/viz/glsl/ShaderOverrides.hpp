#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace geode::viz::shader_overrides {

// Customize › Advanced › Shader code: the user's edited fragment source per style id, process-wide
// (#364). It used to live on one Renderer (the live screen's), so an export, the live wallpaper and the
// dream each built the stock style. Like the macro bank and the director, the state is one table every
// renderer reads: a renderer applies the override when it builds a scene and, when generation() moves,
// re-applies it to the scenes it keeps resident (Renderer::syncShaderOverrides). The main thread
// writes; GL threads read at scene build and on a generation change, so a mutex is enough.

// Sets the source a style draws with; an empty source clears it (the style draws its built-in shader).
// Every call that changes the table bumps the generation; setting the same source again does not.
void set(const std::string& styleId, const std::string& source);
void clear(const std::string& styleId);
// The style's source, or "" when it draws its built-in shader (an unknown id included).
std::string get(const std::string& styleId);
// Moves on every change; a renderer compares it with the one it last applied.
uint64_t generation();
// A copy of the whole table: later edits never change it.
std::map<std::string, std::string> snapshot();
// Tests only: forget every override (bumps the generation when anything was set).
void clearAll();

}  // namespace geode::viz::shader_overrides
