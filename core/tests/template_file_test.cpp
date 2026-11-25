// Host checks for the template file (core/viz/looks/TemplateFile): what the shell's TemplateFormat read, defaulted, carried
// and wrote before the codec moved here, its JVM checks (SharedLinkNumbersTest) included, and the C ABI the shell
// calls (api/geode_looks_api.cpp). Built and run by CI (.github/workflows/native-host-tests.yml).
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "util/Json.hpp"
#include "viz/Catalogue.hpp"
#include "viz/looks/LookFiles.hpp"
#include "viz/params/Params.hpp"
#include "viz/looks/TemplateFile.hpp"

using namespace geode;
using viz::SceneParams;
namespace files = viz::files;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

files::VideoTemplate parsed(const std::string& text, const std::string& what) {
    const files::TemplateRead read = files::readTemplate(text);
    check(read.status == files::TemplateStatus::Parsed, what + " parses (" + read.why + ")");
    return read.value;
}

const json::Value* section(const json::Value& root, const char* key) { return root.get(key); }

std::string orEmpty(const char* text) { return text ? std::string(text) : std::string(); }

bool hasKey(const json::Value* object, const char* key) { return object && object->get(key) != nullptr; }

bool carries(const files::Carried& carried, const std::string& key) {
    for (const auto& entry : carried) {
        if (entry.first == key) return true;
    }
    return false;
}

void starters() {
    check(files::starterCount() == 6, "six starters");
    std::set<std::string> ids;
    for (int i = 0; i < files::starterCount(); ++i) {
        const std::string text(files::starter(i));
        const files::VideoTemplate t = parsed(text, "starter " + std::to_string(i));
        ids.insert(t.id);
        check(files::isStarter(t.id), "a starter's id is a starter's");
        check(t.author == "BeatRealms", "a starter's author");
        check(t.foreign.empty() && t.look.foreign.empty() && t.look.paramsForeign.empty(), "a starter carries nothing");
        const std::string written = files::writeTemplate(t);
        check(files::writeTemplate(parsed(written, "a starter written")) == written, "a starter reads back to the same text");
    }
    check(ids.size() == 6, "six ids");

    const files::VideoTemplate reel = parsed(std::string(files::starter(0)), "Reel Neon");
    check(reel.name == "Reel Neon" && reel.id == "geode.starter.reel-neon", "Reel Neon's identity");
    check(reel.look.sceneId == "fluid", "Reel Neon is fluid");
    check(reel.look.attack == 0.85f && reel.look.decay == 0.24f, "Reel Neon's envelope");
    check(std::fabs(reel.look.params.speed - 1.3f) < 1e-6f, "Reel Neon's speed");
    check(reel.layout.ratio == "R9_16" && reel.layout.artwork == "BEHIND_BLURRED" && reel.layout.progress == "BAR", "Reel Neon's layout");
    check(reel.slots.size() == 2 && reel.slots[0].role == "TITLE" && reel.slots[1].role == "ARTIST", "no text section: title over artist");
    check(reel.exportSettings.segment.kind == "LOUDEST_WINDOW" && reel.exportSettings.segment.durationMs == 30000, "the hook clip");
    check(reel.exportSettings.quality == "FHD1080" && reel.exportSettings.fps == 30, "Reel Neon's export");

    const files::VideoTemplate vinyl = parsed(std::string(files::starter(2)), "Vinyl Spin");
    check(vinyl.slots.size() == 1 && vinyl.slots[0].pattern == "{artist}" && vinyl.slots[0].allCaps, "Vinyl Spin's one slot");
    check(vinyl.slots[0].anchor == "TOP_CENTER" && vinyl.slots[0].weight == "BOLD" && vinyl.slots[0].shadow, "its anchor, weight and shadow");
    check(vinyl.layout.artwork == "CIRCLE" && vinyl.layout.artworkScale == 0.7f, "round artwork");
    check(!vinyl.exportSettings.loopSafe && vinyl.exportSettings.segment.kind == "WHOLE_TRACK", "the whole track");

    const files::VideoTemplate master = parsed(std::string(files::starter(5)), "Master 4K");
    check(master.look.sceneId == viz::catalogue::allIds().front(), "no scene id: the default style");
    check(master.slots.empty(), "an empty slots array is no text at all");
    check(master.exportSettings.quality == "UHD4K" && master.exportSettings.fps == 60, "4K at sixty");
    check(master.exportSettings.fileNamePattern == "{artist} - {title} (4K)", "its file name pattern");
}

void defaults() {
    const files::VideoTemplate t = parsed(R"({"format":"geode.template"})", "a bare marker");
    check(t.id.size() == 36 && t.id[8] == '-' && t.id[14] == '4', "a missing id is a fresh UUID");
    check(parsed(R"({"format":"geode.template"})", "again").id != t.id, "every fresh id differs");
    check(t.name == "Untitled template", "a missing name");
    check(t.look.sceneId == viz::catalogue::allIds().front(), "the default style");
    check(t.look.attack == 0.6f && t.look.decay == 0.12f, "the default envelope");
    check(t.look.params.speed == SceneParams{}.speed && t.look.customPaletteId.empty(), "default params");
    check(!t.look.customShader, "no shader");
    check(t.layout.artworkScale == 0.62f && t.layout.safeArea == 0.06f, "layout numbers");
    check(t.layout.accent == -1 && t.layout.backdrop == static_cast<int>(0xFF000000u), "white accent on black");
    check(t.slots.size() == 2 && t.slots[0].offsetY == -0.06f && t.slots[0].sizeSp == 34.0f && t.slots[1].sizeSp == 22.0f,
          "the default slots");
    check(t.exportSettings.fps == 30 && t.exportSettings.loopSafe && t.exportSettings.audio == "FULL_MIX", "export defaults");
    check(t.exportSettings.fileNamePattern == "{artist} - {title}", "the default file name");
    check(t.author.empty() && t.notes.empty() && t.createdAtMs == 0 && t.formatVersion == 1, "the root defaults");

    const files::VideoTemplate blanks = parsed(
        R"({"format":"geode.template","id":"  ","name":" \t ","look":{"sceneId":"  "},"export":{"fileNamePattern":"  "}})", "blanks");
    check(blanks.id.size() == 36 && blanks.name == "Untitled template", "a blank id and name");
    check(blanks.look.sceneId == viz::catalogue::allIds().front(), "a blank scene id");
    check(blanks.exportSettings.fileNamePattern == "{artist} - {title}", "a blank file name pattern");

    const files::VideoTemplate trimmed = parsed(R"({"format":"geode.template","id":" abc ","name":" Night ","look":{"sceneId":"water"}})", "trims");
    check(trimmed.id == "abc" && trimmed.name == "Night", "id and name trimmed");
    check(trimmed.look.sceneId == viz::catalogue::resolveId("water") && trimmed.look.sceneId != "water", "a retired style resolves");
    const files::VideoTemplate gone = parsed(R"({"format":"geode.template","id":"abc","name":"Old","look":{"sceneId":"no_such_style"}})", "unknown style");
    check(gone.look.sceneId == viz::catalogue::allIds().front(), "a style this build does not have opens on the default style");
}

// The exact text org.json's toString(2) wrote for a template at its defaults, after the look.
void writtenText() {
    files::VideoTemplate t = files::defaultTemplate();
    t.id = "id-1";
    t.name = "A \"quoted\" / name";
    t.createdAtMs = 1700000000123LL;
    const std::string text = files::writeTemplate(t);
    const std::string head = "{\n  \"format\": \"geode.template\",\n  \"formatVersion\": 1,\n  \"id\": \"id-1\",\n"
                             "  \"name\": \"A \\\"quoted\\\" \\/ name\",\n  \"author\": \"\",\n  \"notes\": \"\",\n"
                             "  \"createdAtMs\": 1700000000123,\n  \"look\": {\n    \"sceneId\": \"" +
                             viz::catalogue::allIds().front() +
                             "\",\n    \"attack\": 0.6000000238418579,\n    \"decay\": 0.11999999731779099,\n    \"params\": {\n";
    check(text.compare(0, head.size(), head) == 0, "the head as org.json wrote it");
    const std::string tail = R"(  "layout": {
    "ratio": "R9_16",
    "artwork": "BEHIND_BLURRED",
    "artworkScale": 0.6200000047683716,
    "progress": "BAR",
    "safeArea": 0.05999999865889549,
    "accent": "#FFFFFFFF",
    "backdrop": "#FF000000"
  },
  "text": {
    "slots": [
      {
        "role": "TITLE",
        "pattern": "",
        "anchor": "BOTTOM_LEFT",
        "offsetX": 0,
        "offsetY": -0.05999999865889549,
        "sizeSp": 34,
        "color": "#FFFFFFFF",
        "weight": "BOLD",
        "allCaps": false,
        "shadow": true
      },
      {
        "role": "ARTIST",
        "pattern": "",
        "anchor": "BOTTOM_LEFT",
        "offsetX": 0,
        "offsetY": 0,
        "sizeSp": 22,
        "color": "#FFFFFFFF",
        "weight": "REGULAR",
        "allCaps": false,
        "shadow": true
      }
    ]
  },
  "export": {
    "quality": "FHD1080",
    "fps": 30,
    "loopSafe": true,
    "audio": "FULL_MIX",
    "segment": {
      "kind": "WHOLE_TRACK"
    },
    "fileNamePattern": "{artist} - {title}"
  }
})";
    check(text.size() > tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0, "layout, text and export as written");
    check(text.find("\n    }\n  },\n  \"layout\"") != std::string::npos, "the look closes before the layout");

    const auto root = json::parse(text);
    check(root.has_value(), "the text is JSON");
    if (root) {
        const json::Value* params = section(*section(*root, "look"), "params");
        check(hasKey(params, "speed") && hasKey(params, "customPaletteId") && hasKey(params, "customPalette2Id"),
              "the params object is the native schema's");
    }
}

void roundTrip() {
    files::VideoTemplate t = files::defaultTemplate();
    t.id = "rt";
    t.name = "Round trip";
    t.author = "me";
    t.notes = "line\nbreak";
    t.createdAtMs = 42;
    t.look.sceneId = "fluid";
    t.look.attack = 0.3f;
    t.look.decay = 0.9f;
    t.look.params.speed = 1.7f;
    t.look.params.trails = false;
    t.look.customPaletteId = "dusk";
    t.look.customShader = "void main(){}";
    t.layout.ratio = "R1_1";
    t.layout.accent = static_cast<int>(0x80FF0000u);
    t.slots = {files::TemplateSlot{}};
    t.slots[0].pattern = "{title}";
    t.slots[0].color = 0x0000FF00;
    t.exportSettings.segment.kind = "FIXED";
    t.exportSettings.segment.startMs = 5000;
    t.exportSettings.segment.durationMs = 9000000000LL;
    t.exportSettings.audio = "MUTED";

    const std::string text = files::writeTemplate(t);
    const files::VideoTemplate back = parsed(text, "a written template");
    check(back.id == "rt" && back.name == "Round trip" && back.author == "me" && back.notes == "line\nbreak", "identity");
    check(back.createdAtMs == 42, "created at");
    check(back.look.sceneId == "fluid" && back.look.attack == 0.3f && back.look.decay == 0.9f, "the look's envelope");
    check(back.look.params.speed == 1.7f && !back.look.params.trails, "the look's params");
    check(back.look.customPaletteId == "dusk", "the saved palette id");
    check(back.look.customShader == t.look.customShader, "the shader");
    check(back.layout.ratio == "R1_1" && back.layout.accent == static_cast<int>(0x80FF0000u), "layout");
    check(text.find("\"accent\": \"#80FF0000\"") != std::string::npos, "colours as #AARRGGBB");
    check(back.slots.size() == 1 && back.slots[0].pattern == "{title}" && back.slots[0].color == 0x0000FF00, "the slot");
    check(back.exportSettings.segment.kind == "FIXED" && back.exportSettings.segment.startMs == 5000 &&
              back.exportSettings.segment.durationMs == 9000000000LL,
          "a fixed segment, long durations intact");
    check(back.exportSettings.audio == "MUTED", "audio");
    check(files::writeTemplate(back) == text, "write, read, write is stable");
}

// What a newer build wrote survives this one: keys, tokens, segment kinds and the version.
void carriesTheFuture() {
    const std::string future = R"({
      "format": "GEODE.TEMPLATE", "formatVersion": 7, "id": "f", "name": "Future",
      "rootExtra": {"nested": [1, 2.5, "x", null, true]},
      "look": {"sceneId": "fluid", "attack": 0.5, "decay": 0.5, "lookExtra": 3,
               "params": {"speed": 2, "paramFromTheFuture": 0.25}},
      "layout": {"ratio": "R2_1", "artwork": "card", "layoutExtra": "y"},
      "text": {"slots": [{"role": "NARRATOR", "anchor": "top_left", "slotExtra": false}, 7, "x"], "textExtra": 1},
      "export": {"quality": "UHD8K", "audio": "SPATIAL", "exportExtra": {},
                 "segment": {"kind": "BEAT_ALIGNED", "bars": 4, "phase": 0.5}}
    })";
    const files::VideoTemplate t = parsed(future, "a future template");
    check(t.formatVersion == 7, "the version read");
    check(carries(t.foreign, "rootExtra") && t.foreign.size() == 1, "the root carries its unknown key");
    check(carries(t.look.foreign, "lookExtra") && t.look.foreign.size() == 1, "the look carries its unknown key");
    check(carries(t.look.paramsForeign, "paramFromTheFuture") && t.look.paramsForeign.size() == 1, "the params carry their unknown key");
    check(t.look.params.speed == 2.0f, "a known param still reads");
    check(t.layout.ratio == "R2_1" && t.layout.artwork == "CARD", "an unknown token kept, a known one in its own spelling");
    check(carries(t.layout.foreign, "layoutExtra"), "the layout carries");
    check(t.slots.size() == 1, "an entry that is no object is dropped");
    check(t.slots[0].role == "NARRATOR" && t.slots[0].anchor == "TOP_LEFT" && carries(t.slots[0].foreign, "slotExtra"), "the slot carries");
    check(carries(t.textForeign, "textExtra"), "the text section carries");
    check(t.exportSettings.quality == "UHD8K" && t.exportSettings.audio == "SPATIAL", "unknown export tokens kept");
    check(carries(t.exportSettings.foreign, "exportExtra"), "the export carries");
    check(t.exportSettings.segment.kind == "BEAT_ALIGNED" && t.exportSettings.segment.fields.size() == 2, "a later segment kind keeps its fields");

    const std::string text = files::writeTemplate(t);
    check(text.find("\"formatVersion\": 7") != std::string::npos, "the version never goes backwards");
    check(text.find("\"format\": \"geode.template\"") != std::string::npos, "the marker in its own spelling");
    const auto root = json::parse(text);
    check(root.has_value(), "the written text is JSON");
    if (!root) return;
    check(root->keys().back() == "rootExtra", "carried keys after the known ones");
    check(json::orgJsonText(*root->get("rootExtra")).value_or("") == R"({"nested":[1,2.5,"x",null,true]})", "a carried value intact");
    const json::Value* look = section(*root, "look");
    check(hasKey(look, "lookExtra") && hasKey(section(*look, "params"), "paramFromTheFuture"), "look and params keys written back");
    const json::Value* segment = section(*section(*root, "export"), "segment");
    check(segment && segment->optString("kind") == "BEAT_ALIGNED" && hasKey(segment, "bars") && hasKey(segment, "phase"),
          "the later segment written back whole");
    check(section(*section(*root, "layout"), "ratio")->string() == "R2_1", "a foreign token written verbatim");
    check(files::writeTemplate(parsed(text, "the future again")) == text, "and it is stable");

    const files::VideoTemplate old = parsed(R"({"format":"geode.template","formatVersion":0,"name":"Old"})", "version 0");
    check(files::writeTemplate(old).find("\"formatVersion\": 1") != std::string::npos, "an older version is written as this one");
    const files::VideoTemplate none = parsed(R"({"format":"geode.template","name":"No version"})", "no version");
    check(none.formatVersion == 1, "a missing version is this build's");
}

// SharedLinkNumbersTest: a link is a document anyone can write; numbers no float holds take the defaults.
void numbers() {
    const files::VideoTemplate t = parsed(R"({"format":"geode.template","name":"t",
         "look":{"attack":1e39,"decay":1e999},
         "layout":{"artworkScale":1e39,"safeArea":1e999},
         "text":{"slots":[{"sizeSp":1e39,"offsetX":-1e999}]}})",
                                          "overflowing numbers");
    check(t.look.attack == 0.6f, "an overflowing attack is the default");
    check(t.look.decay == 0.12f, "an infinite decay is the default");
    check(t.layout.artworkScale == 0.62f && t.layout.safeArea == 0.06f, "layout numbers");
    check(t.slots.size() == 1 && t.slots[0].sizeSp == 28.0f && t.slots[0].offsetX == 0.0f, "slot numbers");
    check(files::readTemplate(files::writeTemplate(t)).status == files::TemplateStatus::Parsed, "and it writes again");

    const files::VideoTemplate held = parsed(R"({"format":"geode.template","look":{"attack":7,"decay":-1}})", "out of range");
    check(held.look.attack == 1.0f && held.look.decay == 0.01f, "the envelope held to the sliders' ranges");
    const files::VideoTemplate nan = parsed(R"({"format":"geode.template","look":{"attack":NaN,"x":NaN},"export":{"fps":"24"}})", "NaN");
    check(nan.look.attack == 0.6f && nan.look.foreign.empty(), "NaN is the default, and is not carried");
    check(nan.exportSettings.fps == 24, "a number as a string reads as org.json read it");
    const files::VideoTemplate strings = parsed(
        R"({"format":"geode.template","createdAtMs":"12.9","export":{"loopSafe":"FALSE","segment":{"kind":"loudest_window"}}})", "strings");
    check(strings.createdAtMs == 12 && !strings.exportSettings.loopSafe, "longs and booleans from strings");
    check(strings.exportSettings.segment.kind == "LOUDEST_WINDOW" && strings.exportSettings.segment.durationMs == 30000,
          "a loudest window without a length is thirty seconds");
}

void colours() {
    const auto accent = [](const std::string& raw) {
        return parsed(R"({"format":"geode.template","layout":{"accent":")" + raw + R"("}})", "accent " + raw).layout.accent;
    };
    check(accent("#ff00ff00") == static_cast<int>(0xFF00FF00u), "lower case hex");
    check(accent("  #80123456 ") == static_cast<int>(0x80123456u), "trimmed");
    check(accent("FF0000") == 0xFF0000, "no '#', no alpha: as written");
    check(accent("-1") == -1, "a signed value");
    check(accent("zz") == -1 && accent("") == -1 && accent("#") == -1 && accent("# FF") == -1, "unreadable: the default");
    check(accent("1FFFFFFFFFFFFFFFF") == -1, "past a long: the default");
    check(accent("100000000") == 0, "the low 32 bits");
}

void refusals() {
    const auto status = [](const std::string& text) { return files::readTemplate(text); };
    check(status("").status == files::TemplateStatus::Malformed && status("").why == "the template was empty", "empty");
    check(status("no brace here [1]").why == "the template was empty", "no object at all");
    check(status("{").why == "this is not JSON", "a cut-off object");
    check(status("{\"format\":\"geode.template\",").status == files::TemplateStatus::Malformed, "a cut-off template");
    check(status("{'format':'geode.template'}").status == files::TemplateStatus::Malformed, "single quotes are not read");
    const files::TemplateRead other = status(R"({"name":"a preset","sceneId":"fluid"})");
    check(other.status == files::TemplateStatus::NotATemplate && other.why == "no 'format': 'geode.template' marker", "JSON but no marker");
    check(status(R"({"format":"geode.preset"})").status == files::TemplateStatus::NotATemplate, "another format");
    check(status(R"({"format":7})").status == files::TemplateStatus::NotATemplate, "a marker that is no string");
    check(status(std::string(100000, '[')).status == files::TemplateStatus::Malformed, "no '{' in a hostile array");
    check(status("{\"a\":" + std::string(100000, '[')).status == files::TemplateStatus::Malformed, "deep nesting refused without a crash");
    std::string bytes;
    for (int i = 0; i < 512; ++i) bytes += static_cast<char>((i * 37) & 0xFF);
    check(status(bytes).status != files::TemplateStatus::Parsed, "noise refused");

    parsed("\xEF\xBB\xBF  \n{\"format\":\"geode.template\",\"name\":\"bom\"}\n\t ", "a byte order mark and whitespace");
    const files::VideoTemplate pasted = parsed("Here it is: {\"format\":\"geode.template\",\"name\":\"pasted\"} enjoy!", "text around it");
    check(pasted.name == "pasted", "a template inside a message");
}

void links() {
    const auto in = files::templateLinkIn("try this GEODE://Template/H4sIAAAA_-x?y=1\nthanks");
    check(in && *in == "geode://preset/H4sIAAAA_-x?y=1", "a template link as its preset link");
    check(!files::templateLinkIn("geode://preset/abc"), "a preset link is no template link");
    const auto end = files::templateLinkIn("geode://template/abc");
    check(end && *end == "geode://preset/abc", "a link at the end");
    const auto of = files::templateLinkOf("geode://preset/abc", 100);
    check(of && *of == "geode://template/abc", "a preset link as a template link");
    check(!files::templateLinkOf("geode://preset/abc", 19), "too long");
    check(!files::templateLinkOf("http://x", 100), "no preset link");
}

void shareNames() {
    check(files::templateShareName("Night: drive?") == "Night_ drive_.geode.json", "unsafe characters");
    check(files::templateShareName("   ") == "Untitled template.geode.json", "blank");
    check(files::templateShareName("a/b\\c|d") == "a_b_c_d.geode.json", "separators");
    check(files::templateShareName(std::string(70, 'x')) == std::string(60, 'x') + ".geode.json", "sixty units");
    check(files::templateShareName(std::string(59, 'x') + " y") == std::string(59, 'x') + ".geode.json", "trimmed after the cut");
    check(files::templateShareName(std::string(59, 'x') + "\xF0\x9F\x8E\xB5") == std::string(59, 'x') + ".geode.json",
          "a pair never split");
}

void api() {
    std::vector<float> frame(static_cast<size_t>(SceneParams::kFieldCount));
    const char* out[6] = {};
    float envelope[2] = {};
    const std::string starter(files::starter(1));
    check(geode_template_read(starter.c_str(), frame.data(), SceneParams::kFieldCount, out, envelope) == 1, "read a starter");
    const std::string document = orEmpty(out[0]);
    check(orEmpty(out[1]) == "geode.starter.lo-fi-loop" && orEmpty(out[2]) == "Lo-Fi Loop", "id and name");
    check(orEmpty(out[3]) == "aurora" && out[4] == nullptr && orEmpty(out[5]).empty(), "the look's strings");
    check(envelope[0] == 0.4f && envelope[1] == 0.06f, "the envelope");
    check(std::fabs(SceneParams::fromWire(frame.data()).speed - 0.5f) < 1e-6f, "the params frame");
    check(document == files::writeTemplate(files::readTemplate(starter).value), "the document is the written text");

    check(geode_template_read("{}", frame.data(), SceneParams::kFieldCount, out, envelope) == 0, "not a template");
    check(orEmpty(out[0]) == "no 'format': 'geode.template' marker", "and why");
    check(geode_template_read("x", frame.data(), SceneParams::kFieldCount, out, envelope) == -1, "unreadable");
    check(geode_template_read(nullptr, frame.data(), SceneParams::kFieldCount, out, envelope) == -1, "null text");
    check(geode_template_read(starter.c_str(), frame.data(), 1, out, envelope) == -1, "a short frame");

    const std::string renamed = orEmpty(geode_template_renamed(document.c_str(), "Lo-Fi Loop 2"));
    const files::VideoTemplate r = files::readTemplate(renamed).value;
    check(r.name == "Lo-Fi Loop 2" && r.id == "geode.starter.lo-fi-loop" && r.author == "BeatRealms", "renamed keeps the rest");
    check(geode_template_renamed("nope", "x") == nullptr, "renaming no template");

    const std::string copy = orEmpty(geode_template_new_copy(document.c_str(), "Mine", 99));
    const files::VideoTemplate c = files::readTemplate(copy).value;
    check(c.id != r.id && c.id.size() == 36 && !files::isStarter(c.id), "a copy takes a fresh id");
    check(c.name == "Mine" && c.author.empty() && c.createdAtMs == 99 && c.notes == r.notes, "a copy's identity");
    check(c.layout.ratio == "R1_1" && c.slots.size() == 2, "a copy keeps every setting");

    SceneParams params{};
    params.speed = 0.5f;
    std::vector<float> wire(static_cast<size_t>(SceneParams::kFieldCount));
    params.toWire(wire.data());
    const char* made = geode_template_create("Made", 7, "fluid", 0.2f, 0.3f, nullptr, wire.data(), SceneParams::kFieldCount, "p1");
    check(made != nullptr, "create");
    const files::VideoTemplate m = files::readTemplate(made ? made : "").value;
    check(m.name == "Made" && m.createdAtMs == 7 && m.id.size() == 36, "a new template's identity");
    check(m.look.sceneId == "fluid" && m.look.attack == 0.2f && m.look.decay == 0.3f && m.look.params.speed == 0.5f, "its look");
    check(!m.look.customShader && m.look.customPaletteId == "p1", "its strings");
    check(m.slots.size() == 2 && m.exportSettings.segment.kind == "WHOLE_TRACK", "every other section at its default");
    check(geode_template_create("x", 0, "fluid", 0, 0, nullptr, wire.data(), 1, "") == nullptr, "a short frame");

    check(orEmpty(geode_template_share_name("a:b")) == "a_b.geode.json", "share name");
    check(geode_template_link_in("nothing") == nullptr, "no link");
    check(orEmpty(geode_template_link_of("geode://preset/z", 8000)) == "geode://template/z", "link of");
}

}  // namespace

int main() {
    starters();
    defaults();
    writtenText();
    roundTrip();
    carriesTheFuture();
    numbers();
    colours();
    refusals();
    links();
    shareNames();
    api();
    if (failures) {
        std::printf("%d template file check(s) failed\n", failures);
        return 1;
    }
    std::printf("template file checks passed\n");
    return 0;
}
