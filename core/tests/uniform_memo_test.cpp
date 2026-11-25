// Host-side checks for the uniform value cache (core/viz/gl/UniformMemo.hpp and UniformCache::set1f ... setFloats in
// core/viz/gl/Program.hpp): an upload of a value the program already holds is skipped, a changed one is sent, and a
// rebuilt program (a new UniformCache), a forgetValues() and a location of -1 behave. GL is faked as call counters;
// UniformCache's name lookup is a link seam here (Program.cpp builds shaders and needs the whole stager).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <string_view>

#include "viz/gl/Program.hpp"
#include "viz/gl/UniformMemo.hpp"

using namespace geode::viz;

namespace {

int g_calls = 0;
float g_last[4] = {};
int g_lastCount = 0;

}  // namespace

void glUniform1f(GLint, GLfloat a) { ++g_calls; g_last[0] = a; }
void glUniform1i(GLint, GLint a) { ++g_calls; g_last[0] = static_cast<float>(a); }
void glUniform2f(GLint, GLfloat a, GLfloat b) { ++g_calls; g_last[0] = a; g_last[1] = b; }
void glUniform3f(GLint, GLfloat a, GLfloat b, GLfloat c) { ++g_calls; g_last[0] = a; g_last[1] = b; g_last[2] = c; }
void glUniform4f(GLint, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { ++g_calls; g_last[0] = a; g_last[1] = b; g_last[2] = c; g_last[3] = d; }
void glUniform1fv(GLint, GLsizei n, const GLfloat* v) { ++g_calls; g_lastCount = n; g_last[0] = v[0]; }
void glUniform2fv(GLint, GLsizei n, const GLfloat* v) { ++g_calls; g_lastCount = n; g_last[0] = v[0]; }
void glUniform3fv(GLint, GLsizei n, const GLfloat* v) { ++g_calls; g_lastCount = n; g_last[0] = v[0]; }
void glUniform4fv(GLint, GLsizei n, const GLfloat* v) { ++g_calls; g_lastCount = n; g_last[0] = v[0]; }

namespace geode::viz {
// Every name the fake program knows is at location 3; "uZoom" is the one the program never reads (-1).
GLint UniformCache::loc(std::string_view name) { return name == "uZoom" ? -1 : 3; }
GLsizei UniformCache::arraySize(std::string_view, GLsizei declared) { return declared; }
GLsizei UniformCache::arrayCount(std::string_view name, GLsizei declared) { return arraySize(name, declared); }
}  // namespace geode::viz

namespace {

int failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

void memoRules() {
    UniformMemo memo(2);
    const float a = 1.5f;
    CHECK(memo.changed(0, &a, sizeof a));    // never sent
    CHECK(!memo.changed(0, &a, sizeof a));   // the same
    const float b = 2.0f;
    CHECK(memo.changed(0, &b, sizeof b));    // a new value
    CHECK(memo.changed(1, &b, sizeof b));    // slots are independent
    CHECK(!memo.changed(1, &b, sizeof b));
    const float pair[2] = {2.0f, 3.0f};
    CHECK(memo.changed(0, pair, sizeof pair));  // a longer upload to the same slot is a change
    CHECK(!memo.changed(0, pair, sizeof pair));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(memo.changed(0, &nan, sizeof nan));
    CHECK(!memo.changed(0, &nan, sizeof nan));  // the same bits: NaN is not "always changed"
    const float zero = 0.0f;
    const float negZero = -0.0f;
    CHECK(memo.changed(0, &zero, sizeof zero));
    CHECK(memo.changed(0, &negZero, sizeof negZero));  // bytes differ, so it is sent
    memo.clear();
    CHECK(memo.changed(0, &negZero, sizeof negZero));  // forgotten: sent again
    CHECK(memo.changed(1, &b, sizeof b));
}

void cacheSkipsUnchanged() {
    UniformCache cache(7);
    g_calls = 0;
    cache.set1f(Uniform::uTime, 1.0f);
    CHECK(g_calls == 1);
    cache.set1f(Uniform::uTime, 1.0f);
    CHECK(g_calls == 1);
    cache.set1f(Uniform::uTime, 2.0f);
    CHECK(g_calls == 2 && g_last[0] == 2.0f);
    cache.set1i(Uniform::uStyle, 4);
    cache.set1i(Uniform::uStyle, 4);
    CHECK(g_calls == 3);
    cache.set4f(Uniform::uPoolLook, 1, 2, 3, 4);
    cache.set4f(Uniform::uPoolLook, 1, 2, 3, 4);
    cache.set4f(Uniform::uPoolLook, 1, 2, 3, 5);
    CHECK(g_calls == 5 && g_last[3] == 5.0f);
    const float arr[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    cache.setFloats(Uniform::uWorld, 4, 2, arr);
    cache.setFloats(Uniform::uWorld, 4, 2, arr);
    CHECK(g_calls == 6 && g_lastCount == 2);
    float changed[8] = {1, 2, 3, 4, 5, 6, 7, 9};
    cache.setFloats(Uniform::uWorld, 4, 2, changed);  // only the last element differs
    CHECK(g_calls == 7);
    // Two uniforms with the same value do not share a memo.
    cache.set1f(Uniform::uSat, 2.0f);
    CHECK(g_calls == 8);
}

void unreadNamesSendNothing() {
    UniformCache cache(7);
    g_calls = 0;
    cache.set1f(Uniform::uZoom, 1.0f);  // location -1
    cache.set1f(Uniform::uZoom, 2.0f);
    CHECK(g_calls == 0);
}

void rebuiltProgramStartsEmpty() {
    UniformCache cache(7);
    g_calls = 0;
    cache.set1f(Uniform::uTime, 1.0f);
    CHECK(g_calls == 1);
    cache = UniformCache(8);  // relinked, or the context was lost and the program rebuilt
    cache.set1f(Uniform::uTime, 1.0f);
    CHECK(g_calls == 2);      // a new program holds 0, so the same value is sent again
    cache.forgetValues();     // written behind the cache's back
    cache.set1f(Uniform::uTime, 1.0f);
    CHECK(g_calls == 3);
}

}  // namespace

int main() {
    memoRules();
    cacheSkipsUnchanged();
    unreadNamesSendNothing();
    rebuiltProgramStartsEmpty();
    if (failures == 0) std::puts("uniform_memo_test: ok");
    return failures == 0 ? 0 : 1;
}
