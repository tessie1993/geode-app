#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

// The rules for an audio file another app opens in this one ("Open with", ACTION_VIEW), ported from Kotlin
// model/OpenedAudio.kt. Playing is not importing: the file is streamed from the sender's content uri, never
// copied, never added to the library, history or the saved resumption queue. Everything here is a value the
// intent or the provider handed over, so each is treated as hostile. The shell hands in plain values (the
// action, the data uri's scheme, authority and last path segment, the sender's declared type, the provider's
// display name, and the package and grant it looked up) as UTF-8 text; the platform calls stay in the shell.
// Host-tested in core/tests/shell_opened_audio_test.cpp.
namespace geode::shell {

// Marks a queue item as an opened file, so history and resumption leave it out.
inline constexpr const char* kOpenedMediaIdPrefix = "geode:opened:";
// The longest name shown for an opened file, in code points.
inline constexpr int kOpenedNameMaxCodePoints = 120;

// What the activity does with a delivered intent.
enum class OpenedRoute : int {
    NotOurs,  // not an opened audio file: left for the other handlers
    Stale,    // an opened audio file delivered again from Recents, after its grant may have ended: dropped
    Open,     // an opened audio file to play
};

// Routes a delivered intent from its action, its data uri's scheme and last path segment, and the type the
// sender declared; nothing else in the intent is read. Only a "content" scheme is taken, exactly (case
// sensitive). The type only routes: an audio/ type with something after the slash, application/ogg or
// application/x-flac (any case, parameters after ';' ignored); an untyped view is taken unless its name ends
// in ".milk", the one other file the app opens. (OpenedAudio.route)
OpenedRoute openedAudioRoute(const std::optional<std::string>& action, const std::optional<std::string>& scheme,
                             const std::optional<std::string>& type, const std::optional<std::string>& lastSegment,
                             bool fromHistory);

// Whether the app must refuse to play the uri. providerPackage: the package that owns the uri's authority
// (nullopt when no provider answers to it); readGranted: whether this app holds an explicit read grant on it.
// The app's own providers are refused, and so is a uri nobody granted. (OpenedAudio.refuses)
bool openedAudioRefuses(const std::optional<std::string>& scheme, const std::optional<std::string>& authority,
                        const std::string& ownPackage, const std::optional<std::string>& providerPackage, bool readGranted);

// The provider's display name made fit to show: control, format (bidi overrides, zero-width), private-use,
// unassigned and lone surrogate code points dropped, whitespace runs folded to one space, a file extension
// (a dot and one to five ASCII letters or digits) cut, at most kOpenedNameMaxCodePoints code points. nullopt
// when nothing is left. Unassigned means unassigned in Unicode 15.1. (OpenedAudio.displayName)
std::optional<std::string> openedAudioDisplayName(const std::optional<std::string>& raw);

// A queue id for an opened file: the prefix and a version 4 uuid spelled from 16 random bytes the shell
// draws (the app keeps no random source of its own in the shell's rules), so it is distinct per open and
// carries no part of the uri. (OpenedAudio.mediaId)
std::string openedMediaId(const std::array<std::uint8_t, 16>& random);

bool openedAudioIsOpened(const std::optional<std::string>& mediaId);

}  // namespace geode::shell
