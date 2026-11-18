#pragma once
#include <initializer_list>
#include <string>
#include <string_view>

// The words the Customize panel draws that are not one control's label: page texts, buttons and the
// labels a screen reader hears, by id (the name the string resource had before it moved here). Pure.
//
// WHY native (owner, 2026-11-12: "strings.xml must be in C++"): these words describe the engine's own
// tables (the palette stack, your colours, the panel's pieces), so they sit beside the tables they name
// and the shell keeps no copy. The app ships English only. Host-tested in core/tests/customize_rows_test.cpp.
namespace geode::viz::panel {

// The text for id, "" for an id the table does not have.
const char* text(std::string_view id);

// The text for id with its placeholders filled: "%1$s" or "%1$d" takes the first argument, "%2$s" the
// second, and "%%" is one '%'. A placeholder past the arguments is dropped.
std::string format(std::string_view id, std::initializer_list<std::string> args);

// Every id in table order, one a line: what the test walks.
std::string ids();

}  // namespace geode::viz::panel
