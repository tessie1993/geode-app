// A tiny assert harness for the host tests in core/tests: no framework, one include.
//
// A "table test" checks a text table the C++ core serves to Kotlin (catalogue, params schema,
// Customize map, modulation targets) and keeps its committed fixture byte-equal:
//
//   <test> core/tests/<name>.txt           checks, fixture compared to the live text
//   <test> --write core/tests/<name>.txt   rewrites the fixture after a deliberate change
//
// The fixtures are the C++ output and are never hand-edited: the JVM tests read them in place of
// libgeode (the geode.* properties in build-logic/src/main/kotlin/geode.kotlin-common.gradle.kts).
#pragma once
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace hostcheck {

inline int& failures() {
    static int n = 0;
    return n;
}

inline void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures();
    }
}

inline std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

// The table's lines, each split on '|'. Fails on an empty text or a blank line.
inline std::vector<std::vector<std::string>> rows(const std::string& text) {
    check(!text.empty(), "text is empty");
    check(text.empty() || text.back() == '\n', "text does not end in a newline");
    std::vector<std::vector<std::string>> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        check(!line.empty(), "blank line in text");
        out.push_back(split(line, '|'));
    }
    return out;
}

inline bool parseNum(const std::string& s, double& v) {
    try {
        std::size_t used = 0;
        v = std::stod(s, &used);
        return used == s.size();
    } catch (...) {
        return false;
    }
}

// Every value of `column` among rows whose first field is `tag` is distinct.
inline void uniqueColumn(const std::vector<std::vector<std::string>>& table, const std::string& tag, std::size_t column) {
    std::set<std::string> seen;
    for (const auto& r : table) {
        if (r.empty() || r[0] != tag) continue;
        if (r.size() <= column) {
            check(false, tag + " row too short");
            continue;
        }
        check(seen.insert(r[column]).second, "duplicate " + tag + " " + r[column]);
    }
}

inline int count(const std::vector<std::vector<std::string>>& table, const std::string& tag) {
    int n = 0;
    for (const auto& r : table) n += (!r.empty() && r[0] == tag) ? 1 : 0;
    return n;
}

// Compares `text` with the fixture (argv[1], else defaultPath), or rewrites it with --write.
inline int finish(int argc, char** argv, const std::string& text, const char* defaultPath, const char* name) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        std::ofstream(argv[2], std::ios::binary) << text;
        std::printf("%s: wrote %s\n", name, argv[2]);
        return 0;
    }
    const char* path = argc > 1 ? argv[1] : defaultPath;
    std::ifstream f(path, std::ios::binary);
    check(f.good(), std::string("cannot open fixture ") + path);
    if (f.good()) {
        std::string disk((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(disk == text, std::string("text differs from ") + path + "; after a deliberate change run " + name + " --write " + path);
    }
    if (failures() == 0) std::printf("%s: ok\n", name);
    return failures() == 0 ? 0 : 1;
}

}  // namespace hostcheck
