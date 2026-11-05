#include "scene/object/GltfImport.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "meshoptimizer.h"

// cgltf is header-only; this is the one translation unit that compiles its implementation.
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace geode::scene::object {
namespace {

constexpr uint32_t kGlbMagic = 0x46546C67u;  // "glTF", little-endian

// EXT_meshopt_compression can expand a small file a long way; past this the file is treated as an
// attack on memory, not a model, whatever its size on disk.
constexpr std::size_t kMaxDecodedBytes = 256u * 1024u * 1024u;

// #455's accept list: extensions the engine reads (meshopt, quantization) or will read once
// textures are imported (basisu, texture transform), plus two material ones that only simplify the
// shading the Scene's overlay pass already does.
constexpr std::string_view kAccepted[] = {
    "KHR_texture_basisu",    "EXT_meshopt_compression",         "KHR_mesh_quantization",
    "KHR_texture_transform", "KHR_materials_unlit",             "KHR_materials_emissive_strength",
};
constexpr std::string_view kDraco = "KHR_draco_mesh_compression";

bool isAccepted(std::string_view name) {
    return std::find(std::begin(kAccepted), std::end(kAccepted), name) != std::end(kAccepted);
}

// #455's ignore-with-warning list: the heavy KHR_materials_* (clearcoat, transmission, volume,
// sheen, iridescence, ...) and punctual lights. Each degrades to the core metal-rough material or to
// no light, which still draws the model recognisably, so even a file that marks one required is
// imported (with the warning) rather than refused: refusing would cost the user the whole model for
// a shading nuance the overlay pass would not draw anyway.
bool isIgnorable(std::string_view name) {
    return name.starts_with("KHR_materials_") || name == "KHR_lights_punctual";
}

struct DataFree {
    void operator()(cgltf_data* d) const { cgltf_free(d); }
};
using DataPtr = std::unique_ptr<cgltf_data, DataFree>;

void refuse(ImportResult& r, Refusal code, std::string detail = {}) {
    r.refusals.push_back({code, std::move(detail)});
}

void warn(ImportResult& r, Warning code, std::string detail = {}) {
    r.warnings.push_back({code, std::move(detail)});
}

// Decodes every EXT_meshopt_compression view into memory the view owns (cgltf_free releases it with
// the default free, hence malloc). After this every accessor reads plain data through
// cgltf_buffer_view_data, compressed or not.
bool decodeMeshopt(cgltf_data* data) {
    std::size_t total = 0;
    for (cgltf_size i = 0; i < data->buffer_views_count; ++i) {
        cgltf_buffer_view& view = data->buffer_views[i];
        if (!view.has_meshopt_compression) continue;
        const cgltf_meshopt_compression& mc = view.meshopt_compression;
        if (mc.buffer == nullptr || mc.buffer->data == nullptr || mc.stride == 0) return false;
        if (mc.count > kMaxDecodedBytes / mc.stride) return false;
        const std::size_t bytes = mc.count * mc.stride;
        total += bytes;
        if (total > kMaxDecodedBytes) return false;
        void* out = std::malloc(bytes > 0 ? bytes : 1);
        if (out == nullptr) return false;
        view.data = out;
        const auto* source = static_cast<const unsigned char*>(mc.buffer->data) + mc.offset;
        int rc = -1;
        switch (mc.mode) {
            case cgltf_meshopt_compression_mode_attributes:
                rc = meshopt_decodeVertexBuffer(out, mc.count, mc.stride, source, mc.size);
                break;
            case cgltf_meshopt_compression_mode_triangles:
                rc = meshopt_decodeIndexBuffer(out, mc.count, mc.stride, source, mc.size);
                break;
            case cgltf_meshopt_compression_mode_indices:
                rc = meshopt_decodeIndexSequence(out, mc.count, mc.stride, source, mc.size);
                break;
            default:
                return false;
        }
        if (rc != 0) return false;
        switch (mc.filter) {
            case cgltf_meshopt_compression_filter_octahedral: meshopt_decodeFilterOct(out, mc.count, mc.stride); break;
            case cgltf_meshopt_compression_filter_quaternion: meshopt_decodeFilterQuat(out, mc.count, mc.stride); break;
            case cgltf_meshopt_compression_filter_exponential: meshopt_decodeFilterExp(out, mc.count, mc.stride); break;
            default: break;
        }
    }
    return true;
}

bool readable(const cgltf_accessor* a) {
    if (a == nullptr) return false;
    if (a->buffer_view == nullptr) return !a->is_sparse;  // all zeros by the spec
    return a->buffer_view->data != nullptr || (a->buffer_view->buffer != nullptr && a->buffer_view->buffer->data != nullptr);
}

const cgltf_accessor* attribute(const cgltf_primitive& p, cgltf_attribute_type type, int index) {
    for (cgltf_size i = 0; i < p.attributes_count; ++i) {
        if (p.attributes[i].type == type && p.attributes[i].index == index) return p.attributes[i].data;
    }
    return nullptr;
}

bool drawsTriangles(const cgltf_primitive& p) {
    return p.type == cgltf_primitive_type_triangles || p.type == cgltf_primitive_type_triangle_strip ||
           p.type == cgltf_primitive_type_triangle_fan;
}

uint64_t primitiveTriangles(const cgltf_primitive& p) {
    const cgltf_accessor* pos = attribute(p, cgltf_attribute_type_position, 0);
    const uint64_t n = p.indices != nullptr ? p.indices->count : (pos != nullptr ? pos->count : 0);
    if (p.type == cgltf_primitive_type_triangles) return n / 3;
    return n >= 3 ? n - 2 : 0;
}

// The primitives the default scene draws (every root when the file names no scene), each stored once,
// and one instance per node that places one.
struct Gathered {
    std::vector<const cgltf_primitive*> prims;
    std::vector<ModelInstance> instances;
    uint64_t skippedNonTriangle = 0;
};

Gathered gather(const cgltf_data* data) {
    Gathered g;
    std::unordered_map<const cgltf_primitive*, uint32_t> meshOf;
    std::vector<const cgltf_node*> stack;
    const cgltf_scene* scene = data->scene != nullptr ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene != nullptr) {
        for (cgltf_size i = 0; i < scene->nodes_count; ++i) stack.push_back(scene->nodes[i]);
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; ++i) {
            if (data->nodes[i].parent == nullptr) stack.push_back(&data->nodes[i]);
        }
    }
    // Iterative, so a hostile chain of nested nodes cannot overflow the stack.
    while (!stack.empty()) {
        const cgltf_node* node = stack.back();
        stack.pop_back();
        if (node == nullptr) continue;
        for (cgltf_size i = 0; i < node->children_count; ++i) stack.push_back(node->children[i]);
        if (node->mesh == nullptr) continue;
        float world[16];
        cgltf_node_transform_world(node, world);
        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            const cgltf_primitive* prim = &node->mesh->primitives[p];
            if (!drawsTriangles(*prim)) {
                ++g.skippedNonTriangle;
                continue;
            }
            if (attribute(*prim, cgltf_attribute_type_position, 0) == nullptr) continue;
            auto [it, added] = meshOf.try_emplace(prim, static_cast<uint32_t>(g.prims.size()));
            if (added) g.prims.push_back(prim);
            ModelInstance inst;
            inst.mesh = it->second;
            std::memcpy(inst.world, world, sizeof(world));
            g.instances.push_back(inst);
        }
    }
    return g;
}

// Reads one primitive into a triangle list over its own vertices. False when the data is not what
// its accessors claim (unreadable, short, or an index past the last vertex: meshoptimizer trusts
// indices, so an unchecked one would read out of bounds).
bool readMesh(const cgltf_data* data, const cgltf_primitive& p, ModelMesh& m) {
    const cgltf_accessor* pos = attribute(p, cgltf_attribute_type_position, 0);
    if (!readable(pos) || pos->type != cgltf_type_vec3) return false;
    const std::size_t vertices = pos->count;
    m.positions.resize(vertices * 3);
    if (cgltf_accessor_unpack_floats(pos, m.positions.data(), m.positions.size()) != m.positions.size()) return false;

    const cgltf_accessor* nrm = attribute(p, cgltf_attribute_type_normal, 0);
    if (readable(nrm) && nrm->type == cgltf_type_vec3 && nrm->count == vertices) {
        m.normals.resize(vertices * 3);
        if (cgltf_accessor_unpack_floats(nrm, m.normals.data(), m.normals.size()) != m.normals.size()) m.normals.clear();
    }
    const cgltf_accessor* uv = attribute(p, cgltf_attribute_type_texcoord, 0);
    if (readable(uv) && uv->type == cgltf_type_vec2 && uv->count == vertices) {
        m.uvs.resize(vertices * 2);
        if (cgltf_accessor_unpack_floats(uv, m.uvs.data(), m.uvs.size()) != m.uvs.size()) m.uvs.clear();
    }

    std::vector<uint32_t> raw;
    if (p.indices != nullptr) {
        if (!readable(p.indices) || p.indices->is_sparse || p.indices->type != cgltf_type_scalar) return false;
        raw.resize(p.indices->count);
        if (cgltf_accessor_unpack_indices(p.indices, raw.data(), sizeof(uint32_t), raw.size()) != raw.size()) return false;
    } else {
        raw.resize(vertices);
        for (std::size_t i = 0; i < vertices; ++i) raw[i] = static_cast<uint32_t>(i);
    }
    for (uint32_t index : raw) {
        if (index >= vertices) return false;
    }

    // Strips and fans become plain lists, the one topology the simplifier and the Scene take.
    if (p.type == cgltf_primitive_type_triangles) {
        raw.resize(raw.size() - raw.size() % 3);
        m.indices = std::move(raw);
    } else if (raw.size() >= 3) {
        m.indices.reserve((raw.size() - 2) * 3);
        for (std::size_t i = 0; i + 2 < raw.size(); ++i) {
            if (p.type == cgltf_primitive_type_triangle_fan) {
                m.indices.insert(m.indices.end(), {raw[0], raw[i + 1], raw[i + 2]});
            } else if (i % 2 == 0) {
                m.indices.insert(m.indices.end(), {raw[i], raw[i + 1], raw[i + 2]});
            } else {
                m.indices.insert(m.indices.end(), {raw[i + 1], raw[i], raw[i + 2]});  // keep the winding
            }
        }
    }
    m.material = p.material != nullptr ? static_cast<int>(cgltf_material_index(data, p.material)) : -1;
    return true;
}

enum class Pass { KeepSeams, Permissive, Sloppy };

std::vector<uint32_t> simplifyOne(const ModelMesh& m, const std::vector<uint32_t>& src, std::size_t target, Pass pass) {
    if (target >= src.size()) return src;
    std::vector<uint32_t> out(src.size());
    // The error bound is relative to the mesh's extent; 1 lets the collapse run until the triangle
    // target, which is what the cap asks for. Collapses still go cheapest first, so the shape holds as
    // long as it can.
    constexpr float kAnyError = 1.0f;
    std::size_t n = 0;
    switch (pass) {
        case Pass::KeepSeams:
            n = meshopt_simplify(out.data(), src.data(), src.size(), m.positions.data(), m.vertexCount(),
                                 sizeof(float) * 3, target, kAnyError, 0, nullptr);
            break;
        case Pass::Permissive:
            n = meshopt_simplify(out.data(), src.data(), src.size(), m.positions.data(), m.vertexCount(),
                                 sizeof(float) * 3, target, kAnyError, meshopt_SimplifyPermissive, nullptr);
            break;
        case Pass::Sloppy:
            n = meshopt_simplifySloppy(out.data(), src.data(), src.size(), m.positions.data(), m.vertexCount(),
                                       sizeof(float) * 3, nullptr, target, kAnyError, nullptr);
            break;
    }
    out.resize(n);
    return out;
}

uint64_t drawn(const std::vector<std::vector<uint32_t>>& lists, const std::vector<uint64_t>& uses) {
    uint64_t total = 0;
    for (std::size_t i = 0; i < lists.size(); ++i) total += lists[i].size() / 3 * uses[i];
    return total;
}

// Index lists, one per mesh, that together draw at most `cap` triangles with every instance counted.
// Every mesh keeps the same share of its triangles, so a small part is thinned as much as a large
// one instead of vanishing. Passes get rougher only while still over: collapses that keep attribute
// seams (UV and normal splits stay clean), then collapses across them, then, for the low-detail copy
// only, sloppy clustering that ignores topology. Each pass aims a little lower, since the simplifier
// can stop a few triangles short of its target.
std::vector<std::vector<uint32_t>> fitToCap(const std::vector<ModelMesh>& meshes,
                                            const std::vector<std::vector<uint32_t>>& sources,
                                            const std::vector<uint64_t>& uses, uint64_t cap, bool allowSloppy) {
    const uint64_t total = drawn(sources, uses);
    std::vector<std::vector<uint32_t>> out = sources;
    if (total <= cap) return out;
    double aim = static_cast<double>(cap) / static_cast<double>(total);
    for (Pass pass : {Pass::KeepSeams, Pass::Permissive, Pass::Sloppy}) {
        if (pass == Pass::Sloppy && !allowSloppy) break;
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            const std::size_t tris = sources[i].size() / 3;
            std::size_t keep = static_cast<std::size_t>(static_cast<double>(tris) * aim);
            // A part never disappears in the careful passes; clustering may drop it.
            if (pass != Pass::Sloppy && tris > 0) keep = std::max<std::size_t>(keep, 1);
            out[i] = simplifyOne(meshes[i], sources[i], keep * 3, pass);
        }
        if (drawn(out, uses) <= cap) return out;
        aim *= 0.95;
    }
    return out;
}

void checkExtensions(const cgltf_data* data, ImportResult& r) {
    std::unordered_set<std::string_view> required;
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
        const std::string_view name = data->extensions_required[i];
        required.insert(name);
        if (isAccepted(name)) continue;
        if (name == kDraco) {
            refuse(r, Refusal::DracoUnsupported, std::string(name));
        } else if (isIgnorable(name)) {
            warn(r, Warning::IgnoredExtension, std::string(name) + " (required)");
        } else {
            refuse(r, Refusal::UnknownRequiredExtension, std::string(name));
        }
    }
    for (cgltf_size i = 0; i < data->extensions_used_count; ++i) {
        const std::string_view name = data->extensions_used[i];
        if (required.contains(name) || isAccepted(name)) continue;
        if (name == kDraco) {
            warn(r, Warning::DracoFallback, std::string(name));
        } else if (isIgnorable(name)) {
            warn(r, Warning::IgnoredExtension, std::string(name));
        } else {
            warn(r, Warning::UnknownOptionalExtension, std::string(name));
        }
    }
}

std::string countOf(uint64_t n, const char* what) { return std::to_string(n) + " " + what; }

}  // namespace

bool ImportResult::refusedFor(Refusal code) const {
    return std::any_of(refusals.begin(), refusals.end(), [code](const RefusalNote& n) { return n.code == code; });
}

bool ImportResult::warned(Warning code) const {
    return std::any_of(warnings.begin(), warnings.end(), [code](const WarningNote& n) { return n.code == code; });
}

uint32_t triangleCount(const ImportResult& model, bool lowDetail) {
    uint64_t total = 0;
    for (const ModelInstance& inst : model.instances) {
        if (inst.mesh >= model.meshes.size()) continue;
        const ModelMesh& m = model.meshes[inst.mesh];
        total += (lowDetail ? m.lowDetailIndices : m.indices).size() / 3;
    }
    return static_cast<uint32_t>(std::min<uint64_t>(total, UINT32_MAX));
}

ImportResult importGlb(const uint8_t* bytes, std::size_t size, const ImportLimits& limits) {
    ImportResult r;
    r.counts.fileBytes = size;

    // Size first: a 2 GB file is refused without reading a byte of it.
    if (size > limits.fileRefuseBytes) {
        refuse(r, Refusal::TooLarge, countOf(size, "bytes"));
        return r;
    }
    uint32_t magic = 0;
    if (bytes != nullptr && size >= 12) std::memcpy(&magic, bytes, sizeof(magic));
    if (magic != kGlbMagic) {
        refuse(r, Refusal::NotGlb);
        return r;
    }
    if (size > limits.fileBudgetBytes) warn(r, Warning::OverFileBudget, countOf(size, "bytes"));

    cgltf_options options{};
    options.type = cgltf_file_type_glb;
    cgltf_data* raw = nullptr;
    const cgltf_result parsed = cgltf_parse(&options, bytes, size, &raw);
    DataPtr data(raw);
    if (parsed == cgltf_result_unknown_format || parsed == cgltf_result_legacy_gltf) {
        refuse(r, Refusal::NotGlb);
        return r;
    }
    if (parsed != cgltf_result_success) {
        refuse(r, Refusal::Malformed, "parse");
        return r;
    }
    // A .glb arrives as bytes with no folder around it, so a buffer may only be the BIN chunk or a
    // data: URI. Checked before cgltf_load_buffers, which would otherwise go looking for the file.
    for (cgltf_size i = 0; i < data->buffers_count; ++i) {
        const char* uri = data->buffers[i].uri;
        if (uri != nullptr && std::strncmp(uri, "data:", 5) != 0) {
            refuse(r, Refusal::ExternalBuffer, uri);
            return r;
        }
    }
    if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success ||
        cgltf_validate(data.get()) != cgltf_result_success) {
        refuse(r, Refusal::Malformed, "buffers");
        return r;
    }

    checkExtensions(data.get(), r);
    if (!r.refusals.empty()) return r;
    if (!decodeMeshopt(data.get())) {
        refuse(r, Refusal::Malformed, "EXT_meshopt_compression");
        return r;
    }

    Gathered g = gather(data.get());
    if (g.skippedNonTriangle > 0) {
        warn(r, Warning::NonTrianglePrimitiveSkipped, countOf(g.skippedNonTriangle, "primitives"));
    }
    std::vector<uint64_t> uses(g.prims.size(), 0);
    for (const ModelInstance& inst : g.instances) ++uses[inst.mesh];

    // Every count the limits judge comes from the accessors alone, before any vertex is read, so a
    // model far over the cap is refused cheaply.
    uint64_t triangles = 0;
    std::unordered_set<const cgltf_material*> materials;
    uint32_t maxTargets = 0;
    bool extraWeights = false;
    for (std::size_t i = 0; i < g.prims.size(); ++i) {
        const cgltf_primitive& p = *g.prims[i];
        triangles += primitiveTriangles(p) * uses[i];
        materials.insert(p.material);  // nullptr is the default material, which counts as one
        maxTargets = std::max<uint32_t>(maxTargets, static_cast<uint32_t>(p.targets_count));
        extraWeights = extraWeights || attribute(p, cgltf_attribute_type_joints, 1) != nullptr;
    }
    uint32_t maxJoints = 0;
    for (cgltf_size i = 0; i < data->skins_count; ++i) {
        maxJoints = std::max<uint32_t>(maxJoints, static_cast<uint32_t>(data->skins[i].joints_count));
    }
    ImportCounts& c = r.counts;
    c.trianglesSource = static_cast<uint32_t>(std::min<uint64_t>(triangles, UINT32_MAX));
    c.meshes = static_cast<uint32_t>(g.prims.size());
    c.instances = static_cast<uint32_t>(g.instances.size());
    c.materials = static_cast<uint32_t>(materials.size());
    c.textures = static_cast<uint32_t>(data->textures_count);
    c.maxJoints = maxJoints;
    c.maxMorphTargets = maxTargets;
    c.animations = static_cast<uint32_t>(data->animations_count);

    const ModelLimits& top = limits.flagship;
    const ModelLimits& low = limits.simplified;
    if (triangles == 0) refuse(r, Refusal::NoGeometry);
    if (static_cast<double>(triangles) > static_cast<double>(top.maxTriangles) * limits.maxFitRatio) {
        refuse(r, Refusal::TooManyTriangles, countOf(triangles, "triangles"));
    }
    if (c.materials > top.maxMaterials) refuse(r, Refusal::TooManyMaterials, countOf(c.materials, "materials"));
    if (maxJoints > top.maxJoints) refuse(r, Refusal::TooManyJoints, countOf(maxJoints, "joints"));
    if (maxTargets > top.maxMorphTargets) refuse(r, Refusal::TooManyMorphTargets, countOf(maxTargets, "morph targets"));
    if (!r.refusals.empty()) return r;

    if (extraWeights) warn(r, Warning::ExtraSkinWeightsDropped);
    if (c.materials > low.maxMaterials) warn(r, Warning::SimplifiedMaterials, countOf(c.materials, "materials"));
    if (maxJoints > low.maxJoints) warn(r, Warning::SimplifiedJoints, countOf(maxJoints, "joints"));
    if (maxTargets > low.maxMorphTargets) warn(r, Warning::SimplifiedMorphTargetsOff, countOf(maxTargets, "morph targets"));

    std::vector<ModelMesh> meshes(g.prims.size());
    for (std::size_t i = 0; i < g.prims.size(); ++i) {
        if (!readMesh(data.get(), *g.prims[i], meshes[i])) {
            refuse(r, Refusal::Malformed, "mesh " + std::to_string(i));
            return r;
        }
    }

    std::vector<std::vector<uint32_t>> source(meshes.size());
    for (std::size_t i = 0; i < meshes.size(); ++i) source[i] = meshes[i].indices;
    std::vector<std::vector<uint32_t>> flagship = fitToCap(meshes, source, uses, top.maxTriangles, false);
    const uint64_t flagshipTris = drawn(flagship, uses);
    if (flagshipTris > top.maxTriangles) {
        refuse(r, Refusal::StillOverAfterFit, countOf(flagshipTris, "triangles"));
        return r;
    }
    std::vector<std::vector<uint32_t>> lowDetail = fitToCap(meshes, flagship, uses, low.maxTriangles, true);
    const uint64_t lowTris = drawn(lowDetail, uses);
    if (lowTris > low.maxTriangles) {
        refuse(r, Refusal::LowDetailOverCap, countOf(lowTris, "triangles"));
        return r;
    }
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        meshes[i].indices = std::move(flagship[i]);
        meshes[i].lowDetailIndices = std::move(lowDetail[i]);
    }

    r.fitted = flagshipTris < triangles;
    c.trianglesFlagship = static_cast<uint32_t>(flagshipTris);
    c.trianglesLowDetail = static_cast<uint32_t>(lowTris);
    r.meshes = std::move(meshes);
    r.instances = std::move(g.instances);
    r.ok = true;
    return r;
}

}  // namespace geode::scene::object
