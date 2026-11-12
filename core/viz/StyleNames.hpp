#pragma once
#include <string>
#include <string_view>

// The names people read for styles and the Styles page's families (were the shell's StyleLabels and their
// string resources). Pure; host-tested in core/tests/style_names_test.cpp.
//
// WHY native: a style's name is catalogue data beside its id, kind and family (viz/Catalogue), so a style
// added or renamed there is named in the same place, and the widget, the player and the Styles page read one
// answer. The app ships English only.
namespace geode::viz::catalogue {

// A style's display name: its own when the table has one, else the id in words ("neon_city" is "Neon City").
std::string styleLabel(std::string_view id);

// A family's display name: its own when the table has one, else the name with a capital first letter.
std::string familyLabel(std::string_view name);

}  // namespace geode::viz::catalogue
