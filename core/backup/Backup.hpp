// The rules of a BeatRealms backup archive, moved out of Kotlin (data/backup/Backup*.kt). Native decides; the shell
// performs: it reads and writes the zip container, files and SharedPreferences, and computes SHA-256 with the
// platform's MessageDigest. Everything here is a pure function of the documents and lists it is handed: no IO,
// no threads, no clock.
//
// A restore, in the order the shell runs it:
//   planUnpack      entry names + manifest text      -> which entries to extract where, each with a byte budget
//   checkExtracted  after each extracted entry        -> its real size and digest against the manifest's promise
//   (shell raises UnpackPlan::afterExtract when every item passed)
//   finishUnpack    prefs texts of the staged files   -> migration, allowlist, prefs validity, the preview
//   planRestore     what the user chose               -> per section: files to swap, prefs keys to remove and write
// An export:
//   planExport      prefs values + files found        -> what goes into the zip, section by section
//   finishExport    each entry's size and digest      -> the manifest text (written last) or TOO_LARGE
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace geode::backup {

// Why a backup could not be written or read. The values are stable: the shell maps them to its own enum.
enum class Problem : int {
    None = 0,
    NotABackup = 1,
    NewerVersion = 2,
    UnsafePath = 3,
    Damaged = 4,
    TooLarge = 5,
    IoFailed = 6,
};

struct Failure {
    Problem problem = Problem::None;
    std::string message;  // for the log only
    bool ok() const { return problem == Problem::None; }
};

constexpr const char* kFormatId = "muzviz-backup";
constexpr int kCurrentVersion = 2;
constexpr const char* kManifestPath = "manifest.json";
constexpr int64_t kMaxManifestBytes = 8LL * 1024 * 1024;
constexpr int64_t kMaxTotalBytes = 512LL * 1024 * 1024;
constexpr int kMaxEntries = 20000;
constexpr int64_t kArchiveSlackBytes = 64LL * 1024 * 1024;  // the zip itself may exceed the cap by its overhead
constexpr int kMaxPathLength = 512;

// Caps a restore checks while it reads, so a hostile or broken archive cannot fill the disk.
struct Limits {
    int64_t maxTotalBytes = kMaxTotalBytes;
    int maxEntries = kMaxEntries;
};

// ---- the layout the shell walks -------------------------------------------------------------------------------

struct PrefsEntryInfo {
    std::string file;  // SharedPreferences file name, e.g. geode-prefs
    std::string path;  // where it lives in the archive, e.g. prefs/creative/geode-prefs.json
};

struct SectionInfo {
    std::string id;
    std::vector<PrefsEntryInfo> prefsEntries;  // its own files, then its slices of other sections' files
    std::vector<std::string> roots;            // filesDir-relative dirs then files the shell walks for an export
    std::vector<std::string> dirs;
    std::vector<std::string> files;
};

// The sections in the order a backup writes and a restore applies them.
std::vector<SectionInfo> layout();

// beatrealms-backup-YYYYMMDD.zip
std::string defaultFileName(int year, int month, int day);

// ---- single rules (public so a test can aim at each; the shell reaches them only through the plans) ------------

bool isSafePath(const std::string& path);
// The section id a path belongs to, "" when a backup may not hold it.
std::string sectionOfPath(const std::string& path);
// The section id that backs up `key` of `prefsFile`, "" for a device-local key or a file no section holds.
std::string sectionOfKey(const std::string& prefsFile, const std::string& key);
bool isPortableKey(const std::string& prefsFile, const std::string& key);
bool isPrefsPath(const std::string& path);
// Whether `target` (canonical) lies under `root` (canonical): target starts with root + separator.
bool isInsideStaging(const std::string& root, const std::string& target, char separator);
// What is left of the byte cap after `used` bytes.
int64_t remainingBudget(int64_t used, const Limits& limits = {});

// ---- preferences as JSON ---------------------------------------------------------------------------------------

enum class PrefType { String, Int, Long, Float, Boolean, StringSet, Unsupported };

struct PrefValue {
    PrefType type = PrefType::Unsupported;
    std::string text;                // String
    int64_t integer = 0;             // Int, Long
    double real = 0;                 // Float (already a float's value widened to double)
    bool flag = false;               // Boolean
    std::vector<std::string> items;  // StringSet, sorted, no duplicates
    bool operator==(const PrefValue& o) const {
        return type == o.type && text == o.text && integer == o.integer && real == o.real && flag == o.flag &&
               items == o.items;
    }
};

struct PrefEntry {
    std::string key;
    PrefValue value;
};

// A file's values as `{"key": {"type": "float", "value": 0.5}}`, keys sorted, org.json's toString(2) layout.
// An Unsupported value is left out. A non-finite float is IoFailed (org.json throws on it).
Failure encodePrefs(const std::vector<PrefEntry>& values, std::string& out);
// Damaged on anything that is not that shape.
Failure decodePrefs(const std::string& text, std::vector<PrefEntry>& out);

// ---- the manifest ----------------------------------------------------------------------------------------------

struct EntryRecord {
    std::string path;
    int64_t size = 0;
    std::string sha256;  // lowercase hex
    bool operator==(const EntryRecord& o) const { return path == o.path && size == o.size && sha256 == o.sha256; }
};

struct Manifest {
    std::string format;
    int version = 0;
    int appVersionCode = 0;
    int64_t createdAtMs = 0;
    std::vector<std::string> sections;  // raw ids, checked against the layout only after migration
    std::vector<EntryRecord> entries;
    bool operator==(const Manifest& o) const {
        return format == o.format && version == o.version && appVersionCode == o.appVersionCode &&
               createdAtMs == o.createdAtMs && sections == o.sections && entries == o.entries;
    }
};

std::string encodeManifest(const Manifest& m);
// The format id and the version are checked first and on their own, so a backup from a newer app is reported as
// exactly that however different the rest of it looks.
Failure decodeManifest(const std::string& text, Manifest& out);

// ---- migrations -------------------------------------------------------------------------------------------------

// The `from` of each step to run, in order, to take `version` up to `target`, given the steps that exist (by their
// `from`). Damaged when the chain has a gap.
Failure migrationPath(int version, int target, const std::vector<int>& stepFroms, std::vector<int>& path);

// ---- export -----------------------------------------------------------------------------------------------------

struct PrefsSnapshot {
    std::string file;
    std::vector<PrefEntry> entries;  // every key the phone's file holds
};

struct FoundFile {
    std::string section;  // the section whose root the shell found it under
    std::string path;     // filesDir-relative, forward slashes
};

struct ExportRequest {
    std::vector<PrefsSnapshot> prefs;
    std::vector<FoundFile> files;  // regular files, symbolic links already left out by the shell
};

struct ExportSource {
    std::string path;
    bool inlineBytes = false;  // true: `bytes` is the content; false: the shell opens filesDir/path
    std::string bytes;
};

struct ExportPlan {
    Failure failure;
    std::vector<ExportSource> sources;  // in the order they go into the zip
};

ExportPlan planExport(const ExportRequest& request, const Limits& limits = {});

struct ExportedEntry {
    std::string path;
    int64_t size = 0;
    std::string digest;  // the raw 32 SHA-256 bytes
};

struct ExportResult {
    Failure failure;
    std::string manifestText;  // goes into the zip last, as manifest.json
};

// `entries` are the sources the shell actually wrote (a file that vanished is left out), in order.
ExportResult finishExport(const std::vector<ExportedEntry>& entries, int appVersionCode, int64_t createdAtMs,
                          const Limits& limits = {});

// ---- unpack -----------------------------------------------------------------------------------------------------

struct ZipEntryInfo {
    std::string name;
    bool isDirectory = false;
};

struct UnpackRequest {
    int64_t entryCount = 0;        // the zip's own count of entries
    bool hasManifest = false;      // a manifest.json entry exists
    bool manifestOverCap = false;  // reading it passed kMaxManifestBytes (the shell stops reading there)
    std::string manifestText;      // its bytes, UTF-8
    // Every entry in zip order. When entryCount > maxEntries + 1 the shell may leave this empty: the count alone
    // is rejected first.
    std::vector<ZipEntryInfo> entries;
};

struct ExtractItem {
    size_t entryIndex = 0;  // into UnpackRequest::entries
    std::string path;       // relative to staging, already checked safe
    int64_t promisedSize = 0;
    std::string promisedSha;
    int64_t budget = 0;  // stop reading and report overBudget once more than this many bytes have passed
};

struct UnpackPlan {
    Failure failure;       // reject before extracting anything
    std::vector<ExtractItem> extract;
    Failure afterExtract;  // raise once every item passed checkExtracted (a later entry was unsafe, or files are missing)
    std::vector<std::string> readForFinish;  // staged prefs files the shell reads into finishUnpack's `texts`
};

UnpackPlan planUnpack(const UnpackRequest& request, const Limits& limits = {});

// After extracting `item`: its real byte count, the raw SHA-256 of what was read, and whether the read was cut at
// the budget.
Failure checkExtracted(const ExtractItem& item, int64_t size, const std::string& digest, bool overBudget);

struct FileWrite {
    std::string path;  // staged, relative
    std::string bytes;
};

struct FinishResult {
    Failure failure;
    Manifest manifest;                     // migrated to the current version
    std::vector<FileWrite> writes;         // files a migration rewrote, for the shell to write into staging
    std::vector<std::string> sections;     // what the preview offers (the manifest's ids this app knows), in layout order
    int64_t createdAtMs = 0;
    int appVersionCode = 0;
};

// `texts`: the staged text of each path planUnpack listed in readForFinish.
FinishResult finishUnpack(const std::string& manifestText, const std::map<std::string, std::string>& texts);

// ---- restore ----------------------------------------------------------------------------------------------------

struct RestoreRequest {
    bool hasStaged = false;                          // inspect staged something
    std::vector<std::string> stagedSections;         // the preview's section ids
    std::vector<std::string> requested;              // what the user ticked
    std::map<std::string, std::string> stagedPrefs;  // staged path -> text, for each prefs entry that is a file
    std::map<std::string, std::vector<std::string>> currentKeys;  // prefs file -> keys the phone holds now
};

struct PrefsRestore {
    std::string file;
    std::string stagedPath;
    std::vector<std::string> removeKeys;  // first
    std::vector<PrefEntry> put;           // then, in this order
};

struct SectionRestore {
    std::string id;
    std::vector<std::string> targets;  // dirs then files: each present one set aside, then each staged one placed
    std::vector<std::string> carryOver;  // cache subtrees moved from the old copy into the new one when present
    std::vector<std::string> ensureDirs;  // created after the swap
    std::vector<PrefsRestore> prefs;      // after the files, in this order
};

struct RestorePlan {
    Failure failure;
    std::vector<SectionRestore> sections;
};

RestorePlan planRestore(const RestoreRequest& request);

}  // namespace geode::backup
