// Host-side checks for the backup's export and unpack decisions (core/backup): what an export holds and in what
// order, what a restore extracts and where, the caps on bytes and entries, the hash comparison, the allowlist after
// migration, with the cases of BackupArchiveTest.kt and a hostile input for each security check. A small fake shell
// plays the platform: it lists zip entries, "extracts" them (a stand-in 32-byte digest in place of SHA-256, which the
// platform computes) and reports sizes the way the real one would.
//
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/backup_archive_test.cpp core/backup/*.cpp
//     core/util/Json.cpp -o backup_archive_test && ./backup_archive_test
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "backup/Backup.hpp"

using namespace geode::backup;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

// A stand-in digest: 32 bytes that differ when the content differs. The real one is SHA-256 from the platform.
std::string digest(const std::string& bytes) {
    std::string out(32, '\0');
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < out.size(); ++i) {
        for (const char c : bytes) h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ULL;
        h = (h ^ (i + 1)) * 1099511628211ULL;
        out[i] = static_cast<char>(h >> 24);
    }
    return out;
}

std::string hex(const std::string& raw) {
    std::string out;
    for (const char c : raw) {
        out += "0123456789abcdef"[static_cast<unsigned char>(c) >> 4];
        out += "0123456789abcdef"[static_cast<unsigned char>(c) & 15];
    }
    return out;
}

using Files = std::vector<std::pair<std::string, std::string>>;

EntryRecord record(const std::string& path, const std::string& bytes) {
    return {path, static_cast<int64_t>(bytes.size()), hex(digest(bytes))};
}

Manifest manifestOf(const Files& listed, int version = kCurrentVersion) {
    Manifest m;
    m.format = kFormatId;
    m.version = version;
    m.appVersionCode = 33;
    m.createdAtMs = 1000;
    m.sections = {"settings", "creative", "music"};
    for (const auto& f : listed) m.entries.push_back(record(f.first, f.second));
    return m;
}

struct Outcome {
    Failure failure;
    std::vector<std::string> extracted;
    FinishResult finish;
};

// The shell: list the entries, run planUnpack, extract each item under its budget, check it, raise afterExtract,
// then finishUnpack with the staged prefs texts.
Outcome unpack(const std::string& manifestText, const Files& zip, const Limits& limits = {}, bool hasManifest = true,
               const std::vector<ZipEntryInfo>& extra = {}) {
    Outcome out;
    UnpackRequest req;
    req.hasManifest = hasManifest;
    req.manifestText = manifestText;
    req.entries = extra;
    for (const auto& f : zip) req.entries.push_back({f.first, false});
    req.entries.push_back({kManifestPath, false});
    req.entryCount = static_cast<int64_t>(req.entries.size());
    UnpackPlan plan = planUnpack(req, limits);
    if (!plan.failure.ok()) {
        out.failure = plan.failure;
        return out;
    }
    std::map<std::string, std::string> staged;
    for (const auto& item : plan.extract) {
        const std::string& bytes = zip[item.entryIndex - extra.size()].second;
        const bool over = static_cast<int64_t>(bytes.size()) > item.budget;
        out.extracted.push_back(item.path);
        const Failure f = checkExtracted(item, over ? item.budget + 1 : static_cast<int64_t>(bytes.size()),
                                         digest(bytes), over);
        if (!f.ok()) {
            out.failure = f;
            return out;
        }
        staged[item.path] = bytes;
    }
    if (!plan.afterExtract.ok()) {
        out.failure = plan.afterExtract;
        return out;
    }
    std::map<std::string, std::string> texts;
    for (const auto& p : plan.readForFinish) {
        if (staged.count(p)) texts[p] = staged[p];
    }
    out.finish = finishUnpack(manifestText, texts);
    out.failure = out.finish.failure;
    return out;
}

Outcome unpackFiles(const Files& listed, const Files& zip, const Limits& limits = {}) {
    return unpack(encodeManifest(manifestOf(listed)), zip, limits);
}

const std::string kPreset = "{\"name\":\"Kyanite\"}";

Files good() { return {{"presets/a.json", kPreset}}; }

PrefEntry boolean(const std::string& key, bool v) {
    PrefEntry e{key, {}};
    e.value.type = PrefType::Boolean;
    e.value.flag = v;
    return e;
}
PrefEntry text(const std::string& key, const std::string& v) {
    PrefEntry e{key, {}};
    e.value.type = PrefType::String;
    e.value.text = v;
    return e;
}

std::string prefsText(const std::vector<PrefEntry>& v) {
    std::string out;
    encodePrefs(v, out);
    return out;
}

void roundTrip() {
    ExportRequest req;
    req.prefs.push_back({"geode-prefs", {[] {
                             PrefEntry e{"gui_opacity", {}};
                             e.value.type = PrefType::Float;
                             e.value.real = static_cast<double>(0.4f);
                             return e;
                         }()}});
    req.files = {{"creative", "presets/a.json"}, {"creative", "presets/gone.json"}};
    const ExportPlan plan = planExport(req);
    check(plan.failure.ok(), "plan export");
    std::vector<ExportedEntry> written;
    Files zip;
    for (const auto& s : plan.sources) {
        const std::string bytes = s.inlineBytes ? s.bytes : (s.path == "presets/gone.json" ? "" : kPreset);
        if (!s.inlineBytes && s.path == "presets/gone.json") continue;  // vanished after it was listed
        written.push_back({s.path, static_cast<int64_t>(bytes.size()), digest(bytes)});
        zip.push_back({s.path, bytes});
    }
    const ExportResult done = finishExport(written, 33, 1000);
    check(done.failure.ok(), "finish export");
    Manifest m;
    check(decodeManifest(done.manifestText, m).ok() && m.sections.size() == 3 && m.version == kCurrentVersion &&
              m.appVersionCode == 33 && m.createdAtMs == 1000,
          "manifest of the export");
    const Outcome o = unpack(done.manifestText, zip);
    check(o.failure.ok(), "pack then unpack gives back the same files");
    check(o.finish.sections.size() == 3, "every section known");
    std::vector<std::string> paths;
    for (const auto& e : m.entries) paths.push_back(e.path);
    // Prefs of every section first (settings' six, creative's two), then its files; the vanished one is not listed.
    check(paths.size() == 10 && paths[8] == "presets/a.json" && paths[9] == "prefs/geode-favourites.json", "entries in order, section by section: its prefs then its files");
    check(paths[0] == "prefs/geode-prefs.json" && paths[6] == "prefs/geode-mod.json" && paths[7] == "prefs/creative/geode-prefs.json",
          "settings prefs, then creative's own file and slice");
    check(plan.sources[0].inlineBytes && plan.sources[0].bytes.find("gui_opacity") != std::string::npos, "settings holds the opacity");
    check(!plan.sources[9].inlineBytes && plan.sources[9].path == "presets/gone.json", "files are opened by the shell");
}

void exportRules() {
    ExportRequest req;
    req.prefs.push_back({"geode-prefs",
                         {boolean("gui_tutorial_seen", true), boolean("gui_reduced_motion", true), text("macro_bank", "m"),
                          text("macro_knobs", "old"), text("palette_stack", "p")}});
    req.prefs.push_back({"geode-wallpaper", {boolean("mix_refused", true), text("home_look", "x")}});
    req.prefs.push_back({"geode-library", {text("roots", "content://r"), text("sort", "name")}});
    const ExportPlan plan = planExport(req);
    auto bytesOf = [&](const std::string& path) {
        for (const auto& s : plan.sources) {
            if (s.path == path) return s.bytes;
        }
        return std::string("<missing>");
    };
    const std::string settings = bytesOf("prefs/geode-prefs.json");
    check(settings.find("gui_reduced_motion") != std::string::npos, "portable key exported");
    check(settings.find("gui_tutorial_seen") == std::string::npos, "device-local key left out");
    check(settings.find("macro_bank") == std::string::npos && settings.find("palette_stack") == std::string::npos,
          "creative's keys are not in settings' file");
    check(settings.find("macro_knobs") == std::string::npos, "retired macro knobs left out");
    const std::string slice = bytesOf("prefs/creative/geode-prefs.json");
    check(slice.find("macro_bank") != std::string::npos && slice.find("palette_stack") != std::string::npos &&
              slice.find("gui_reduced_motion") == std::string::npos,
          "creative's slice holds only its keys");
    check(bytesOf("prefs/geode-wallpaper.json") == "<missing>", "the retired wallpaper file is never exported");
    check(bytesOf("prefs/geode-library.json").find("roots") == std::string::npos, "roots stay");
    check(bytesOf("prefs/geode-mod.json") == "{}", "a file with nothing to say is {}");
    check(bytesOf("prefs/geode-viz.json") == "{}", "a file never supplied is {}");

    // Files: only what the allowlist keeps, section by section, in the shell's order within a section.
    ExportRequest files;
    files.files = {{"creative", "presets/b.json"},         {"creative", "presets/a.json.tmp"},
                   {"music", "history.json"},              {"creative", "presets/a.json"},
                   {"music", "library.json"},              {"creative", "../escape.json"},
                   {"music", "presets/wrong-root.json"}};
    const ExportPlan fp = planExport(files);
    std::vector<std::string> got;
    for (const auto& s : fp.sources) {
        if (!s.inlineBytes) got.push_back(s.path);
    }
    check(got == std::vector<std::string>({"presets/b.json", "presets/a.json", "history.json", "library.json"}),
          "export keeps allowlisted files, grouped by section in order");

    // The entry cap counts every source, vanished or not.
    ExportRequest many;
    for (int i = 0; i < 5; ++i) many.files.push_back({"creative", "presets/" + std::to_string(i) + ".json"});
    Limits small;
    small.maxEntries = 12;  // 10 prefs sources + 5 files = 15 > 12
    check(planExport(many, small).failure.problem == Problem::TooLarge, "too many files");
    small.maxEntries = 15;
    check(planExport(many, small).failure.ok(), "exactly the cap is fine");

    // The total byte cap on export, and bad inputs.
    std::vector<ExportedEntry> big = {{"presets/a.json", 600, digest("a")}, {"presets/b.json", 600, digest("b")}};
    Limits bytes;
    bytes.maxTotalBytes = 1000;
    check(finishExport(big, 1, 1, bytes).failure.problem == Problem::TooLarge, "export over the byte cap");
    big[1].size = 400;
    check(finishExport(big, 1, 1, bytes).failure.ok(), "export exactly at the cap");
    big[1].digest = "short";
    check(finishExport(big, 1, 1, bytes).failure.problem == Problem::IoFailed, "a digest that is not 32 bytes");
    check(remainingBudget(600, bytes) == 400, "remaining budget");
}

void version_and_format() {
    const Files ok = good();
    Manifest newer = manifestOf(ok, kCurrentVersion + 1);
    newer.entries = {record("visuals-v2/x.json", kPreset)};
    check(unpack(encodeManifest(newer), {{"visuals-v2/x.json", kPreset}}).failure.problem == Problem::NewerVersion,
          "a newer version is reported as newer before its paths are looked at");
    Manifest other = manifestOf({});
    other.format = "some-other-app";
    check(unpack(encodeManifest(other), {}).failure.problem == Problem::NotABackup, "a wrong format id is not a backup");
    check(unpack("", {}, {}, false).failure.problem == Problem::NotABackup, "a zip without a manifest is not a backup");
    check(unpack("hello", {}).failure.problem == Problem::NotABackup, "a manifest that is not JSON");
    check(unpackFiles({}, {}).failure.ok(), "an empty backup is fine");
    check(unpackFiles(ok, ok).failure.ok(), "a good backup is fine");
}

void hostilePaths() {
    for (const char* path : {"../escaped.json", "presets/../../escaped.json", "/tmp/escaped.json", "presets\\..\\x.json",
                             "C:/Windows/x.json", "a//b.json", "./a.json", "presets/./a.json", "presets/", "..", ""}) {
        const Files f = {{path, kPreset}};
        const Outcome o = unpack(encodeManifest(manifestOf(f)), f);
        check(o.failure.problem == Problem::UnsafePath, std::string("zip-slip rejected: ") + path);
        check(o.extracted.empty(), std::string("nothing extracted for: ") + path);
    }
    // The unsafe entry comes after a good one: the good one is extracted, then the whole thing is refused.
    const Files f = {{"presets/a.json", kPreset}, {"../escaped.json", kPreset}};
    const Outcome o = unpack(encodeManifest(manifestOf(f)), f);
    check(o.failure.problem == Problem::UnsafePath && o.extracted == std::vector<std::string>({"presets/a.json"}),
          "an unsafe later entry refuses the backup and never becomes an extract item");
    // A hash mismatch earlier in the zip wins over an unsafe name later, as the sequential read finds it first.
    const Files g = {{"presets/a.json", "tampered"}, {"../escaped.json", kPreset}};
    Manifest m = manifestOf(g);
    m.entries[0] = record("presets/a.json", kPreset);
    check(unpack(encodeManifest(m), g).failure.problem == Problem::Damaged, "the first problem in zip order is the one reported");
    // Directory entries and the manifest's own entry are skipped before the path check.
    const Outcome d = unpack(encodeManifest(manifestOf(good())), good(), {}, true, {{"../dir/", true}, {"presets/", true}});
    check(d.failure.ok(), "directory entries are skipped, even hostile names");
    // The manifest is listed by the manifest itself: a hostile path in it is refused too.
    Manifest evil = manifestOf({});
    evil.entries = {record("../x", "a")};
    check(unpack(encodeManifest(evil), {}).failure.problem == Problem::Damaged, "a manifest entry with no file is a missing file");
}

void allowlist() {
    for (const char* path : {"takes/t.json", "analysis/a.bin", "crash-latest.txt",
                             "prefs/geode-widget.json", "presets/a.json.tmp", "session.json", "prefs/settings/geode-prefs.json"}) {
        const Files f = {{path, kPreset}};
        check(unpackFiles(f, f).failure.problem == Problem::UnsafePath, std::string("paths outside the allowlist are rejected: ") + path);
    }
    // A section the manifest does not list.
    Manifest m = manifestOf({{"presets/a.json", kPreset}});
    m.sections = {"settings"};
    check(unpack(encodeManifest(m), {{"presets/a.json", kPreset}}).failure.problem == Problem::Damaged,
          "an entry of a section that is not listed");
    m.sections = {"settings", "nonsense"};
    check(unpack(encodeManifest(m), {{"presets/a.json", kPreset}}).failure.problem == Problem::Damaged, "an unknown section id");
    // An unparseable prefs file.
    const Files bad = {{"prefs/geode-prefs.json", "not json"}};
    check(unpackFiles(bad, bad).failure.problem == Problem::Damaged, "an unparseable prefs file is damage");
    const Files typeless = {{"prefs/geode-prefs.json", "{\"a\":{\"type\":\"weird\",\"value\":1}}"}};
    check(unpackFiles(typeless, typeless).failure.problem == Problem::Damaged, "an unknown prefs type is damage");
    const Files fine = {{"prefs/geode-prefs.json", prefsText({boolean("gui_reduced_motion", true)})}};
    check(unpackFiles(fine, fine).failure.ok(), "a good prefs file passes");
    // Each prefs file in its section.
    const Files slice = {{"prefs/creative/geode-prefs.json", "{}"}, {"prefs/geode-favourites.json", "{}"}};
    check(unpackFiles(slice, slice).failure.ok(), "slice and favourites pass");
}


void integrity() {
    const Files tampered = {{"presets/a.json", "tampered"}};
    check(unpackFiles(good(), tampered).failure.problem == Problem::Damaged, "a checksum mismatch is damage");
    // Same size, different bytes: only the digest tells them apart.
    const Files sameSize = {{"presets/a.json", std::string(kPreset.size(), 'x')}};
    check(unpackFiles(good(), sameSize).failure.problem == Problem::Damaged, "a same-size swap is damage");
    check(unpackFiles(good(), {}).failure.problem == Problem::Damaged, "a file missing from the zip is damage");
    check(unpackFiles({}, good()).failure.problem == Problem::Damaged, "a file extra to the manifest is damage");
    // The sha must match exactly, case included.
    Manifest upper = manifestOf(good());
    for (char& c : upper.entries[0].sha256) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    check(unpack(encodeManifest(upper), good()).failure.problem == Problem::Damaged, "an upper-case digest does not match");
    Manifest shorter = manifestOf(good());
    shorter.entries[0].size += 1;
    check(unpack(encodeManifest(shorter), good()).failure.problem == Problem::Damaged, "a size that disagrees is damage");
    // The same path twice: in the manifest, and in the zip.
    Manifest twice = manifestOf(good());
    twice.entries.push_back(twice.entries[0]);
    check(unpack(encodeManifest(twice), good()).failure.problem == Problem::Damaged, "the manifest lists a path twice");
    const Files dup = {{"presets/a.json", kPreset}, {"presets/a.json", kPreset}};
    check(unpack(encodeManifest(manifestOf(good())), dup).failure.problem == Problem::Damaged, "the zip holds a name twice");
    // A raw digest of the wrong length is a mismatch, not a crash.
    UnpackRequest req;
    req.hasManifest = true;
    req.manifestText = encodeManifest(manifestOf(good()));
    req.entries = {{"presets/a.json", false}};
    req.entryCount = 2;
    const UnpackPlan plan = planUnpack(req);
    check(plan.extract.size() == 1 && checkExtracted(plan.extract[0], static_cast<int64_t>(kPreset.size()), "short", false).problem == Problem::Damaged,
          "a digest of the wrong length is damage");
    check(plan.extract[0].path == "presets/a.json" && plan.extract[0].promisedSha == hex(digest(kPreset)), "the plan names the promise");
}

void caps() {
    const std::string big(4096, 'b');
    const Files f = {{"presets/big.bin", big}};
    Limits tight;
    tight.maxTotalBytes = 1024;
    check(unpackFiles(f, f, tight).failure.problem == Problem::TooLarge, "oversize input is rejected by bytes");
    // A manifest that promises a small file while the zip holds a large one: the read is cut at the budget.
    Manifest liar = manifestOf(f);
    liar.entries[0].size = 3;
    const Outcome l = unpack(encodeManifest(liar), f, tight);
    check(l.failure.problem == Problem::TooLarge, "a lying header cannot get past the byte cap");
    // Budgets shrink by what earlier entries promised, so many small files cannot add up past the cap.
    const Files three = {{"presets/1.bin", std::string(500, '1')}, {"presets/2.bin", std::string(500, '2')}, {"presets/3.bin", std::string(500, '3')}};
    check(unpackFiles(three, three, tight).failure.problem == Problem::TooLarge, "the total across files is capped");
    Limits fits;
    fits.maxTotalBytes = 1500;
    check(unpackFiles(three, three, fits).failure.ok(), "exactly the cap is accepted");
    Limits one;
    one.maxTotalBytes = 1499;
    check(unpackFiles(three, three, one).failure.problem == Problem::TooLarge, "one byte over the cap is refused");
    UnpackRequest req;
    req.hasManifest = true;
    req.manifestText = encodeManifest(manifestOf(three));
    for (const auto& t : three) req.entries.push_back({t.first, false});
    req.entryCount = 4;
    const UnpackPlan plan = planUnpack(req, fits);
    check(plan.extract.size() == 3 && plan.extract[0].budget == 1500 && plan.extract[1].budget == 1000 && plan.extract[2].budget == 500,
          "budgets count down");
    check(checkExtracted(plan.extract[0], 501, digest(three[0].second), true).problem == Problem::TooLarge, "over budget reported");
    check(checkExtracted(plan.extract[0], 1501, digest(three[0].second), false).problem == Problem::TooLarge, "a size past the budget is refused even if unflagged");

    // Entry count: more than maxEntries + 1 (the manifest) is refused, before names are read.
    const Files files = {{"presets/1.json", kPreset}, {"presets/2.json", kPreset}, {"presets/3.json", kPreset}};
    Limits two;
    two.maxEntries = 2;
    check(unpackFiles(files, files, two).failure.problem == Problem::TooLarge, "oversize input is rejected by entry count");
    two.maxEntries = 3;
    check(unpackFiles(files, files, two).failure.ok(), "exactly the entry cap is accepted");
    UnpackRequest huge;
    huge.entryCount = static_cast<int64_t>(kMaxEntries) + 2;
    huge.hasManifest = true;
    check(planUnpack(huge).failure.problem == Problem::TooLarge, "a hostile count is refused with no names given");
    huge.entryCount = static_cast<int64_t>(kMaxEntries) + 1;
    check(planUnpack(huge).failure.problem != Problem::TooLarge, "the cap itself is allowed");
    huge.entryCount = 5000000000LL;
    check(planUnpack(huge).failure.problem == Problem::TooLarge, "a count past 32 bits");
    // The count is checked before the manifest's presence.
    UnpackRequest noManifest;
    noManifest.entryCount = static_cast<int64_t>(kMaxEntries) + 2;
    check(planUnpack(noManifest).failure.problem == Problem::TooLarge, "count before manifest");
    // The manifest read itself is capped.
    UnpackRequest cut;
    cut.hasManifest = true;
    cut.manifestOverCap = true;
    cut.entryCount = 1;
    check(planUnpack(cut).failure.problem == Problem::TooLarge, "a manifest past 8 MiB is refused");
    check(kMaxManifestBytes == 8 * 1024 * 1024, "the manifest cap");
    // A hostile budget from an absurd promise cannot overflow.
    Manifest absurd = manifestOf({});
    absurd.entries = {{"presets/a.json", INT64_MAX, "x"}, {"presets/b.json", INT64_MAX, "x"}, {"presets/c.json", -5, "x"}};
    UnpackRequest ar;
    ar.hasManifest = true;
    ar.manifestText = encodeManifest(absurd);
    ar.entries = {{"presets/a.json", false}, {"presets/b.json", false}, {"presets/c.json", false}};
    ar.entryCount = 4;
    const UnpackPlan ap = planUnpack(ar);
    check(ap.extract.size() == 3 && ap.extract[1].budget <= kMaxTotalBytes && ap.extract[2].budget <= kMaxTotalBytes, "budgets never exceed the cap");
    check(checkExtracted(ap.extract[0], 5, digest("12345"), false).problem != Problem::None, "an absurd promise never matches");
}

}  // namespace

int main() {
    roundTrip();
    exportRules();
    version_and_format();
    hostilePaths();
    allowlist();
    integrity();
    caps();
    if (failures == 0) std::printf("backup_archive_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
