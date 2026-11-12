#pragma once
// A Studio project's documents: its kind and its edit (timeline lanes and clips, markers, keyframe tracks), read
// from and written to the JSON a project folder keeps (`project.json`'s "kind" and "edit"). Nothing plays or draws
// an edit since video editing was dropped (ADR 0007); it is kept so a project saved with one opens and saves back
// unchanged, and so a new project starts with the song (and the look) it was made over.
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace geode::studio {

// Shortest clip anything makes: about two frames at 60 fps. Below it a clip cannot be grabbed with a fingertip,
// so a song that short gets no clip and a render range that short has nothing to draw over.
inline constexpr long long kMinClipMs = 33;
inline constexpr long long kTransitionMs = 800;
inline constexpr int kBeatsPerCut = 4;
inline constexpr long long kLoopTargetMs = 60LL * 60LL * 1000LL;
inline constexpr long long kLoopMs = 20'000;
inline constexpr int kEditVersion = 1;

enum class LaneKind { Visual, Media, Text, Overlay, Audio };
enum class Blend { Normal, Add, Screen, Multiply };
enum class MarkerColour { Red, Amber, Lime, Cyan, Blue, Violet, White };
enum class EaseShape { In, Out, InOut, Smooth };

struct BeatCut {
    std::vector<std::string> sceneIds;
    int everyBeats = kBeatsPerCut;
};

struct SceneContent {
    std::string sceneId;
    std::optional<std::string> presetId;
    std::optional<std::string> milkPath;
    std::optional<BeatCut> beatCut;
};
struct VideoContent {
    std::string uri;
};
struct StillContent {
    std::string uri;
    float kenBurns = 0.0f;
};
struct TextContent {
    std::string text;
    std::optional<std::string> styleId;
};
struct OverlayContent {
    std::string uri;
    Blend blend = Blend::Screen;
    float opacity = 1.0f;
};
struct AudioContent {
    std::string uri;
    float gainDb = 0.0f;
};
using Content = std::variant<SceneContent, VideoContent, StillContent, TextContent, OverlayContent, AudioContent>;

struct Transition {
    std::string id;
    long long durationMs = kTransitionMs;
    bool onBeat = false;
};

// sourceDurationMs is how much material the media has; 0 means synthesised content with no length of its own.
struct Clip {
    std::string id;
    Content content;
    long long startMs = 0;
    long long durationMs = 0;
    long long sourceInMs = 0;
    long long sourceDurationMs = 0;
    std::string label;
    bool enabled = true;
    std::optional<Transition> transition;

    long long endMs() const { return startMs + durationMs; }
};

// Clips are kept in start order.
struct Lane {
    std::string id;
    LaneKind kind = LaneKind::Visual;
    std::string name;
    std::vector<Clip> clips;
    bool muted = false;
    bool locked = false;
};

struct ManualOrigin {};
// rawAtMs is the playhead at the tap before latency compensation, so a new latency can re-place the whole take.
struct TappedOrigin {
    long long rawAtMs = 0;
    long long latencyMs = 0;
};
struct DetectedOrigin {
    float confidence = 0.0f;
    float strength = 0.0f;
};
using MarkerOrigin = std::variant<ManualOrigin, TappedOrigin, DetectedOrigin>;

struct Marker {
    std::string id;
    long long atMs = 0;
    std::string name;
    MarkerColour colour = MarkerColour::Cyan;
    std::string note;
    MarkerOrigin origin;
};

struct Scalar {
    float value = 0.0f;
};
struct Vector2 {
    float x = 0.0f;
    float y = 0.0f;
};
struct Colour {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};
struct Toggle {
    bool on = false;
};
struct Choice {
    int index = 0;
};
using KeyValue = std::variant<Scalar, Vector2, Colour, Toggle, Choice>;

struct Linear {};
struct Hold {};
struct Eased {
    EaseShape shape = EaseShape::InOut;
};
// A cubic bezier pinned at (0,0) and (1,1): the curve editor's two handles.
struct Bezier {
    float c1x = 0.0f;
    float c1y = 0.0f;
    float c2x = 0.0f;
    float c2y = 0.0f;
};
using Interpolation = std::variant<Linear, Hold, Eased, Bezier>;

struct Keyframe {
    std::string id;
    long long atMs = 0;
    KeyValue value;
    Interpolation interpolation;
};

// One animated parameter, programme-wide (no clipId) or scoped to a clip. Keys are kept in time order; a track
// is identified by its parameter and its scope.
struct Track {
    std::string paramId;
    std::optional<std::string> clipId;
    bool enabled = true;
    std::vector<Keyframe> keys;
};

struct Edit {
    long long durationMs = 0;
    std::vector<Lane> lanes;
    std::vector<Marker> markers;
    std::vector<Track> tracks;
};

// A loop project draws a short seamless loop once and repeats it to targetLengthMs.
struct Kind {
    bool loop = false;
    long long targetLengthMs = kLoopTargetMs;
    long long loopLengthMs = kLoopMs;
};

// Reads an edit, nullopt when the text is not one: bad JSON, a newer version, a missing required field or an
// unknown tag. What is read is put in order (clips by start, markers and keys by time, one track per parameter
// and scope, the last one read winning), every beat-cut steps at least one beat, and retired style ids open on
// the style that replaced them.
std::optional<Edit> readEdit(const std::string& json);
std::optional<Kind> readKind(const std::string& json);
std::string writeEdit(const Edit& edit);
std::string writeKind(const Kind& kind);

struct Song {
    std::string uri;
    std::string title;
    long long durationMs = 0;  // 0: not known yet
};
// The look on stage when a render was made, drawn as one clip over the song or the render, whichever is longer.
struct Look {
    std::string sceneId;
    std::optional<std::string> milkPath;
    long long renderMs = 0;
};
using IdSource = std::function<std::string()>;

// A new project's edit: an audio lane holding the song (no clip when the song is shorter than kMinClipMs), and
// with a look a visual lane in front of it holding the look.
Edit newEdit(const std::optional<Song>& song, const std::optional<Look>& look, const IdSource& ids);

// A random UUID (version 4) in its usual text form.
std::string randomId();

}  // namespace geode::studio
