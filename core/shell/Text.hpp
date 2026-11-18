#pragma once

#include <string>
#include <string_view>
#include <vector>

// The little text rules the shell modules share, matching what Kotlin and the JVM do on a String: strings come
// in as UTF-8 (JNI's modified UTF-8 is read too: a NUL as C0 80, a surrogate pair as two three-byte surrogates,
// and a lone surrogate as one), and a code point is a Unicode scalar or a lone surrogate (D800..DFFF), as
// String.codePoints() gives them. Private to core/shell.
namespace geode::shell::text {

// The code points of a UTF-8 text. A byte that starts no valid sequence reads as U+FFFD, as the JVM's decoder
// replaces it.
std::vector<char32_t> decode(std::string_view utf8);

// Appends the code point as UTF-8 (a lone surrogate as its three bytes, which only a caller that kept one asks
// for).
void append(std::string& out, char32_t cp);

// Kotlin's Char.isWhitespace for a code point: Java's Character.isWhitespace or Character.isSpaceChar (the
// Unicode space, line and paragraph separators, and the controls 9..13 and 28..31).
bool isWhitespace(char32_t cp);

// Kotlin's CharSequence.isBlank: every char is whitespace (an empty text is blank).
bool isBlank(std::string_view utf8);

}  // namespace geode::shell::text
