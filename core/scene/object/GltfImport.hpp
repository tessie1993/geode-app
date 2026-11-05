#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Importing the user's own .glb models (#474, limits from #455).
//
// An imported model is one of the Scene's object kinds (#302: shapes, particles, imported models,
// lights), so it lives under geode::scene::object, the namespace #302 gives the base object and its
// kinds. Nothing renders a model yet: this is the gate every model passes before the Scene may hold
// it, and the shape of what it hands over.
//
// The gate is #455's limit table, fitted where it can be and refused where it cannot:
// - the file must be a self-contained .glb (no external buffers: the import has only the bytes);
// - a file over 50 MB is refused before it is parsed, one over 15 MB imports with a warning (the
//   texture fit that brings it down is a follow-up, see below);
// - malformed files are refused, so are unknown required extensions and Draco (re-encoding it is
//   a follow-up); the heavy KHR_materials_* and punctual lights are ignored with a warning;
// - a model up to 4x over the triangle cap is decimated with meshoptimizer (the before and after
//   counts come back), one further over, or still over after decimation, is refused;
// - materials, joints and morph targets over the flagship caps are refused; over the simplified
//   caps they only warn, because the simplified mode will draw less of them, not refuse the model;
// - every model gets a low-detail copy (more index lists over the same vertices) for the
//   simplified mode.
//
// Undo of an automatic fit is the caller's: it still holds the original bytes, so undoing is
// dropping this result. The scene-wide limits (300k triangles and 128 draws per Scene, texture
// memory per Scene) belong to whoever places models in a Scene, not to one import.
//
// Not done here yet (#474 follow-up): texture downscale, mipmaps and ASTC/ETC2 transcode through
// Basis Universal, and the texture-memory limits that depend on them.
//
// Pure: no GL, no files, no threads. Runs wherever the caller runs it (an import worker, a host test).
namespace geode::scene::object {

// The caps one quality tier holds a single model to (#455's table; final after a device check).
struct ModelLimits {
    uint32_t maxTriangles;
    uint32_t maxMaterials;
    uint32_t maxJoints;
    uint32_t maxMorphTargets;
};

struct ImportLimits {
    std::size_t fileBudgetBytes = 15u * 1024u * 1024u;
    std::size_t fileRefuseBytes = 50u * 1024u * 1024u;
    // How far over the flagship triangle cap the automatic fit still tries.
    float maxFitRatio = 4.0f;
    ModelLimits flagship{50000, 8, 128, 8};
    ModelLimits simplified{15000, 4, 64, 0};
};

enum class Refusal {
    NotGlb,                    // not a binary glTF 2.0 container
    TooLarge,                  // over fileRefuseBytes
    Malformed,                 // cgltf could not parse or validate it, or an index is out of range
    ExternalBuffer,            // a buffer points outside the file
    UnknownRequiredExtension,  // extensionsRequired names something the engine cannot do
    DracoUnsupported,          // Draco is required; re-encoding it is a follow-up
    NoGeometry,                // no triangles reachable from the scene
    TooManyTriangles,          // more than maxFitRatio over the flagship cap: not even tried
    StillOverAfterFit,         // decimation could not reach the flagship cap
    LowDetailOverCap,          // the low-detail copy could not reach the simplified cap
    TooManyMaterials,
    TooManyJoints,
    TooManyMorphTargets,
};

enum class Warning {
    OverFileBudget,              // over fileBudgetBytes; imports, the texture fit will shrink it
    IgnoredExtension,            // a listed extension the engine reads past (heavy materials, lights)
    UnknownOptionalExtension,    // used but not required, and unknown: the spec lets it be skipped
    DracoFallback,               // Draco used but not required: the uncompressed fallback is read
    NonTrianglePrimitiveSkipped, // points or lines: the Scene draws triangles only
    ExtraSkinWeightsDropped,     // more than four weights per vertex
    SimplifiedMaterials,         // over the simplified tier's material cap
    SimplifiedJoints,            // over the simplified tier's joint cap
    SimplifiedMorphTargetsOff,   // the simplified tier draws no morph targets
};

// `detail` says what tripped it, for the import sheet and the log ("EXT_foo", "9 materials").
struct RefusalNote {
    Refusal code;
    std::string detail;
};

struct WarningNote {
    Warning code;
    std::string detail;
};

// One mesh primitive in the model's own space, its vertices shared by both detail levels.
struct ModelMesh {
    std::vector<float> positions;   // xyz per vertex
    std::vector<float> normals;     // xyz per vertex, or empty
    std::vector<float> uvs;         // uv0 per vertex, or empty
    std::vector<uint32_t> indices;  // triangles, flagship detail (after any fit)
    std::vector<uint32_t> lowDetailIndices;  // triangles, the simplified mode's copy
    int material = -1;              // the glTF material index, -1 for the default material

    uint32_t vertexCount() const { return static_cast<uint32_t>(positions.size() / 3); }
};

// One placement of a mesh: the node's world matrix (column-major, glTF's). A mesh used by several
// nodes is stored once and placed several times, so it can be drawn instanced.
struct ModelInstance {
    uint32_t mesh = 0;
    float world[16]{};
};

struct ImportCounts {
    std::size_t fileBytes = 0;
    uint32_t trianglesSource = 0;     // before any fit, every instance counted
    uint32_t trianglesFlagship = 0;   // after the fit (== source when nothing was fitted)
    uint32_t trianglesLowDetail = 0;  // the simplified mode's copy
    uint32_t meshes = 0;              // unique primitives kept
    uint32_t instances = 0;
    uint32_t materials = 0;
    uint32_t textures = 0;
    uint32_t maxJoints = 0;           // the largest skin
    uint32_t maxMorphTargets = 0;     // the most targets on one primitive
    uint32_t animations = 0;
};

struct ImportResult {
    bool ok = false;
    bool fitted = false;  // the flagship mesh was decimated; the caller offers Undo
    std::vector<RefusalNote> refusals;
    std::vector<WarningNote> warnings;
    ImportCounts counts;
    std::vector<ModelMesh> meshes;
    std::vector<ModelInstance> instances;

    bool refusedFor(Refusal r) const;
    bool warned(Warning w) const;
};

// Imports a .glb held in memory. Never throws; a refusal leaves meshes and instances empty.
ImportResult importGlb(const uint8_t* bytes, std::size_t size, const ImportLimits& limits = {});

// Triangles drawn for one detail level, every instance counted.
uint32_t triangleCount(const ImportResult& model, bool lowDetail);

}  // namespace geode::scene::object
