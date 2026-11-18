// The C ABI of the backup's rules (core/backup/Backup): one call per plan, lists as arrays of UTF-8 strings. Marshals
// only; the layout of each list is in api/geode_api.h.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "backup/Backup.hpp"

namespace {

using namespace geode::backup;

// The list an answer is built in, handed out as C strings. Valid until the next call on the thread.
struct Answer {
    std::vector<std::string> items;
    std::vector<const char*> pointers;

    void add(std::string s) { items.push_back(std::move(s)); }
    void add(const Failure& f) {
        add(std::to_string(static_cast<int>(f.problem)));
        add(f.message);
    }
    void add(int64_t n) { add(std::to_string(n)); }
    void add(size_t n) { add(std::to_string(n)); }
    const char* const* finish(int* count) {
        pointers.clear();
        for (const auto& s : items) pointers.push_back(s.c_str());
        if (count) *count = static_cast<int>(pointers.size());
        return pointers.data();
    }
};

Answer& answer() {
    thread_local Answer a;
    a.items.clear();
    return a;
}

const char* const* failed(Problem p, const char* message, int* count) {
    Answer& a = answer();
    a.add(Failure{p, message});
    return a.finish(count);
}

// A reader over an argument list; any read past the end or of a non-number marks the whole list bad.
class In {
public:
    In(const char* const* items, int count) : items_(items), count_(count < 0 ? 0 : count) {}
    bool ok() const { return ok_; }
    bool done() const { return at_ >= count_; }
    std::string str() {
        if (at_ >= count_ || !items_[at_]) {
            ok_ = false;
            ++at_;
            return "";
        }
        return items_[at_++];
    }
    int64_t num() {
        const std::string s = str();
        char* end = nullptr;
        const long long n = std::strtoll(s.c_str(), &end, 10);
        if (s.empty() || *end != '\0') ok_ = false;
        return n;
    }
    // A count, bounded by what is left so a lying number cannot reserve memory.
    size_t count() {
        const int64_t n = num();
        if (n < 0 || n > count_ - at_ + 1) {
            ok_ = false;
            return 0;
        }
        return static_cast<size_t>(n);
    }
    bool flag() { return str() == "1"; }
    std::vector<std::string> strs() {
        std::vector<std::string> out;
        for (size_t n = count(); n > 0 && ok_; --n) out.push_back(str());
        return out;
    }

private:
    const char* const* items_;
    int count_;
    int at_ = 0;
    bool ok_ = true;
};

PrefEntry readEntry(In& in) {
    PrefEntry e;
    e.key = in.str();
    const std::string type = in.str();
    const std::string text = in.str();
    std::vector<std::string> items = in.strs();
    PrefValue& v = e.value;
    if (type == "s") {
        v.type = PrefType::String;
        v.text = text;
    } else if (type == "i" || type == "l") {
        v.type = type == "i" ? PrefType::Int : PrefType::Long;
        v.integer = std::strtoll(text.c_str(), nullptr, 10);
    } else if (type == "f") {
        v.type = PrefType::Float;
        v.real = std::strtod(text.c_str(), nullptr);
    } else if (type == "b") {
        v.type = PrefType::Boolean;
        v.flag = text == "1";
    } else if (type == "t") {
        v.type = PrefType::StringSet;
        v.items = std::move(items);
    }
    return e;
}

void writeEntry(Answer& a, const PrefEntry& e) {
    const PrefValue& v = e.value;
    a.add(e.key);
    switch (v.type) {
        case PrefType::String: a.add("s"); a.add(v.text); break;
        case PrefType::Int: a.add("i"); a.add(v.integer); break;
        case PrefType::Long: a.add("l"); a.add(v.integer); break;
        case PrefType::Float: {
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.17g", v.real);
            a.add("f");
            a.add(std::string(buf));
            break;
        }
        case PrefType::Boolean: a.add("b"); a.add(std::string(v.flag ? "1" : "0")); break;
        case PrefType::StringSet: a.add("t"); a.add(""); break;
        case PrefType::Unsupported: a.add("u"); a.add(""); break;
    }
    const bool set = v.type == PrefType::StringSet;
    a.add(set ? v.items.size() : size_t{0});
    if (set) {
        for (const auto& item : v.items) a.add(item);
    }
}

// SHA-256 as the shell hands it, lowercase hex, to the 32 raw bytes the rules compare. Empty when it is not.
std::string rawDigest(const std::string& hex) {
    if (hex.size() != 64) return "";
    std::string out;
    for (size_t i = 0; i < 64; i += 2) {
        int byte = 0;
        for (size_t k = 0; k < 2; ++k) {
            const char c = hex[i + k];
            const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0) return "";
            byte = byte * 16 + d;
        }
        out += static_cast<char>(byte);
    }
    return out;
}

}  // namespace

extern "C" {

const char* const* geode_backup_layout(int* out_count) try {
    Answer& a = answer();
    const auto sections = layout();
    a.add(std::string(kManifestPath));
    a.add(kMaxManifestBytes);
    a.add(Limits{}.maxTotalBytes + kArchiveSlackBytes);
    a.add(static_cast<int64_t>(Limits{}.maxEntries));
    a.add(sections.size());
    for (const auto& s : sections) {
        a.add(s.id);
        a.add(s.prefsEntries.size());
        for (const auto& p : s.prefsEntries) {
            a.add(p.file);
            a.add(p.path);
        }
        a.add(s.roots.size());
        for (const auto& r : s.roots) a.add(r);
    }
    return a.finish(out_count);
} catch (...) {
    return failed(Problem::IoFailed, "layout", out_count);
}

const char* const* geode_backup_plan_export(const char* const* prefs, int prefs_len, const char* const* files,
                                            int files_len, int* out_count) try {
    ExportRequest request;
    In p(prefs, prefs_len);
    for (size_t n = p.count(); n > 0 && p.ok(); --n) {
        PrefsSnapshot snap;
        snap.file = p.str();
        for (size_t k = p.count(); k > 0 && p.ok(); --k) snap.entries.push_back(readEntry(p));
        request.prefs.push_back(std::move(snap));
    }
    In f(files, files_len);
    while (!f.done() && f.ok()) {
        FoundFile found;
        found.section = f.str();
        found.path = f.str();
        request.files.push_back(std::move(found));
    }
    if (!p.ok() || !f.ok()) return failed(Problem::IoFailed, "bad export request", out_count);
    const ExportPlan plan = planExport(request);
    Answer& a = answer();
    a.add(plan.failure);
    a.add(Limits{}.maxTotalBytes);
    a.add(plan.sources.size());
    for (const auto& s : plan.sources) {
        a.add(s.path);
        a.add(std::string(s.inlineBytes ? "1" : "0"));
        a.add(s.bytes);
    }
    return a.finish(out_count);
} catch (...) {
    return failed(Problem::IoFailed, "plan_export", out_count);
}

const char* const* geode_backup_finish_export(const char* const* entries, int entries_len, int app_version_code,
                                              long long created_at_ms, int* out_count) try {
    In in(entries, entries_len);
    std::vector<ExportedEntry> written;
    while (!in.done() && in.ok()) {
        ExportedEntry e;
        e.path = in.str();
        e.size = in.num();
        e.digest = rawDigest(in.str());
        written.push_back(std::move(e));
    }
    if (!in.ok()) return failed(Problem::IoFailed, "bad entry list", out_count);
    const ExportResult r = finishExport(written, app_version_code, created_at_ms);
    Answer& a = answer();
    a.add(r.failure);
    a.add(r.manifestText);
    return a.finish(out_count);
} catch (...) {
    return failed(Problem::IoFailed, "finish_export", out_count);
}

const char* const* geode_backup_plan_unpack(const char* const* request, int request_len, int* out_count) try {
    In in(request, request_len);
    UnpackRequest r;
    r.entryCount = in.num();
    r.hasManifest = in.flag();
    r.manifestOverCap = in.flag();
    r.manifestText = in.str();
    while (!in.done() && in.ok()) {
        ZipEntryInfo e;
        e.name = in.str();
        e.isDirectory = in.flag();
        r.entries.push_back(std::move(e));
    }
    if (!in.ok()) return failed(Problem::IoFailed, "bad unpack request", out_count);
    const UnpackPlan plan = planUnpack(r);
    Answer& a = answer();
    a.add(plan.failure);
    a.add(plan.afterExtract);
    a.add(plan.readForFinish.size());
    for (const auto& path : plan.readForFinish) a.add(path);
    a.add(plan.extract.size());
    for (const auto& item : plan.extract) {
        a.add(item.entryIndex);
        a.add(item.path);
        a.add(item.promisedSize);
        a.add(item.promisedSha);
        a.add(item.budget);
    }
    return a.finish(out_count);
} catch (...) {
    return failed(Problem::IoFailed, "plan_unpack", out_count);
}

const char* const* geode_backup_check_staging(const char* const* request, int request_len, int* out_count) try {
    In in(request, request_len);
    const std::string root = in.str();
    const std::string separator = in.str();
    Answer& a = answer();
    Failure verdict;
    while (!in.done() && in.ok()) {
        const std::string name = in.str();
        const std::string target = in.str();
        if (verdict.ok() && !separator.empty() && !isInsideStaging(root, target, separator[0])) {
            verdict = Failure{Problem::UnsafePath, "'" + name + "' leaves staging"};
        }
    }
    if (!in.ok() || separator.size() != 1) return failed(Problem::IoFailed, "bad staging check", out_count);
    a.add(verdict);
    return a.finish(out_count);
} catch (...) {
    return failed(Problem::IoFailed, "check_staging", out_count);
}

const char* const* geode_backup_check_extracted(const char* const* item, int item_len, int* out_count) try {
    In in(item, item_len);
    ExtractItem x;
    x.path = in.str();
    x.promisedSize = in.num();
    x.promisedSha = in.str();
    x.budget = in.num();
    const int64_t size = in.num();
    const std::string digest = rawDigest(in.str());
    const bool over = in.flag();
    if (!in.ok()) return failed(Problem::IoFailed, "bad extracted item", out_count);
    Answer& a = answer();
    a.add(checkExtracted(x, size, digest, over));
    return a.finish(out_count);
} catch (...) {
    return failed(Problem::IoFailed, "check_extracted", out_count);
}

const char* const* geode_backup_finish_unpack(const char* const* request, int request_len, int* out_count) try {
    In in(request, request_len);
    const std::string manifestText = in.str();
    std::map<std::string, std::string> texts;
    while (!in.done() && in.ok()) {
        std::string path = in.str();
        texts[std::move(path)] = in.str();
    }
    if (!in.ok()) return failed(Problem::IoFailed, "bad staged texts", out_count);
    const FinishResult r = finishUnpack(manifestText, texts);
    Answer& a = answer();
    a.add(r.failure);
    a.add(r.createdAtMs);
    a.add(static_cast<int64_t>(r.appVersionCode));
    a.add(r.sections.size());
    for (const auto& id : r.sections) a.add(id);
    a.add(r.writes.size());
    for (const auto& w : r.writes) {
        a.add(w.path);
        a.add(w.bytes);
    }
    return a.finish(out_count);
} catch (...) {
    return failed(Problem::IoFailed, "finish_unpack", out_count);
}

const char* const* geode_backup_plan_restore(const char* const* request, int request_len, int* out_count) try {
    In in(request, request_len);
    RestoreRequest r;
    r.hasStaged = in.flag();
    r.stagedSections = in.strs();
    r.requested = in.strs();
    for (size_t n = in.count(); n > 0 && in.ok(); --n) {
        std::string path = in.str();
        r.stagedPrefs[std::move(path)] = in.str();
    }
    while (!in.done() && in.ok()) {
        std::string file = in.str();
        r.currentKeys[std::move(file)] = in.strs();
    }
    if (!in.ok()) return failed(Problem::IoFailed, "bad restore request", out_count);
    const RestorePlan plan = planRestore(r);
    Answer& a = answer();
    a.add(plan.failure);
    a.add(plan.sections.size());
    for (const auto& s : plan.sections) {
        a.add(s.id);
        for (const auto* list : {&s.targets, &s.carryOver, &s.ensureDirs}) {
            a.add(list->size());
            for (const auto& item : *list) a.add(item);
        }
        a.add(s.prefs.size());
        for (const auto& p : s.prefs) {
            a.add(p.file);
            a.add(p.stagedPath);
            a.add(p.removeKeys.size());
            for (const auto& key : p.removeKeys) a.add(key);
            a.add(p.put.size());
            for (const auto& e : p.put) writeEntry(a, e);
        }
    }
    return a.finish(out_count);
} catch (...) {
    return failed(Problem::IoFailed, "plan_restore", out_count);
}

const char* geode_backup_file_name(int year, int month, int day) try {
    thread_local std::string name;
    name = defaultFileName(year, month, day);
    return name.c_str();
} catch (...) {
    return "";
}

}  // extern "C"
