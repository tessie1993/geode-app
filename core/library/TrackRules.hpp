#pragma once
// The music library's small rules, ported from the Kotlin shell (MusicLibraryRepository, DeviceTrackQuery,
// LibraryPrefsStore.MinTrackLength, PlaylistFormats.resolve). Text in, records out: no file, no clock, no
// platform. Strings are UTF-8. Where the Kotlin leaned on the JVM (String.lowercase() in the ROOT locale, trim()
// and isBlank() on Unicode white space, Character.isDigit, compareTo on UTF-16) the .cpp says how far it matches.
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace geode::library {

// One track of the app's own library (library.json), field for field.
struct LibraryTrack {
    std::string uri;
    std::string title;
    std::string artist;
    long long durationMs = 0;
    std::string album;
    std::string genre;
    int year = 0;
    int trackNo = 0;
    std::string comment;
    std::string fileName;
    long long sizeBytes = 0;
};

// What a metadata reader answered for one file; nullopt is a tag the reader returned null for (or a reader that
// failed). year and trackNumber are the reader's raw text ("2019-05-01", "3/12").
struct TagRead {
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> album;
    std::optional<std::string> genre;
    std::optional<std::string> year;
    std::optional<std::string> trackNumber;
};

// What the platform says about the file around its tags.
struct FileFacts {
    std::string uri;
    std::string displayName;                     // the provider's display name, "" when it gave none
    long long sizeBytes = 0;                     // the provider's size, 0 when it gave none
    std::optional<std::string> lastPathSegment;  // the URI's last path segment, if it has one
    std::string untitled;                        // the localized "Untitled track", handed in
};

// The tag fallback chain: each tag trimmed, a blank one is none; the year is the first four digits found (0 if
// none); the track number is the text before a '/', read as an integer (0 if it is not one); a missing title is
// the display name without its last extension, else the path segment's file name without its extension, else
// `untitled`; the other missing tags are "".
LibraryTrack tagFallbacks(const TagRead& tags, const FileFacts& file);

// What a folder walk does with one entry of a picked tree. `parentDepth` is the depth of the folder listing it
// (0 for the picked root). A nameless entry and a dot-name are skipped, a folder is entered down to depth 8,
// a file is kept when its MIME type starts with "audio/" or its extension is a known audio one.
enum class WalkAction { Skip, Descend, KeepAudio };
WalkAction walkAction(std::optional<std::string_view> name, bool isDirectory,
                      std::optional<std::string_view> mimeType, int parentDepth);

// True for a title the library should read again from the file: four or more of 0-9 : % A-F, or all digits when
// the artist is empty.
bool needsTitleRefresh(std::string_view title, std::string_view artist);

// Settings > Library folders' "Hide tracks shorter than". Unknown length (0 or less) is always kept; 0 or less
// seconds keeps everything.
bool minTrackKeeps(long long durationMs, int minSeconds);
// The stored threshold as it is read and written back: never below 0.
int minTrackSecondsStored(int seconds);
inline constexpr int kDefaultMinTrackSeconds = 30;

// A MediaStore tag as the library shows it: missing, blank or "<unknown>" read as `fallback`.
std::string tagOr(std::optional<std::string_view> value, std::string_view fallback);
// A file path's folder: everything before the last '/', "" when it has none.
std::string folderOf(std::string_view path);

// A playlist file's entries matched to the library by file name (case-insensitively). `baseNames` are the
// entries' names in order; `libraryFileNames[i]` is track i's file name ("" for none; those never match) and
// `libraryUris[i]` its uri. An entry with no track is missing; with several, the first track in library order
// is used and the entry is also ambiguous. Entries are reported by their index in `baseNames`.
struct PlaylistResolution {
    std::vector<std::string> uris;
    std::vector<int> missing;
    std::vector<int> ambiguous;
};
PlaylistResolution resolvePlaylistEntries(const std::vector<std::string>& baseNames,
                                          const std::vector<std::string>& libraryFileNames,
                                          const std::vector<std::string>& libraryUris);

// Shared by TrackLibraryDoc.cpp: Kotlin's String.lowercase() (ROOT), isBlank(), and its UTF-16 code units.
std::string lowerRoot(std::string_view utf8);
bool isBlank(std::string_view utf8);
std::string trimKotlin(std::string_view utf8);
std::u16string toUtf16(std::string_view utf8);

}  // namespace geode::library
