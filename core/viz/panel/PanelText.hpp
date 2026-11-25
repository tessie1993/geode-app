#pragma once
#include <initializer_list>
#include <string>
#include <string_view>

// The ids of the words the Customize panel draws that are not one control's label: page texts, buttons and
// the labels a screen reader hears. The words themselves are the shell's (its string resources, one per id):
// core names a word and hands over what fills it, and never holds the text. Pure.
// Host-tested in core/tests/customize_rows_test.cpp.
namespace geode::viz::panel {

// Whether id is one of the panel's words.
bool has(std::string_view id);

// How many placeholders ("%1$s", "%2$s", ...) the word has; -1 for an id the panel does not have.
int argCount(std::string_view id);

// A row cell that names a word for the shell to resolve: "id:<id>", then one argument per placeholder after
// a unit separator (0x1F): a missing argument is blank and an extra one dropped. "" for an empty id. An
// argument's own unit separators are spaces, so a cell splits one way.
std::string ref(std::string_view id, std::initializer_list<std::string> args = {});

// Every id in table order with its placeholder count, "<id> <count>" a line: what the tests walk
// (core/tests/panel_words.txt is this text).
std::string ids();

}  // namespace geode::viz::panel
