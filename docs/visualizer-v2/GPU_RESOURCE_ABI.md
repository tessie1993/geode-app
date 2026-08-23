# GPU resource ABI — probe facts, capability derivation and format policy

Required by [`MASTER_PLAN.md`](MASTER_PLAN.md) §2.2; the design is §6.3. This documents what
`:engine:gl` ships as of V2-4-01a: the fact/judgment split, the derivation rules and the
per-role format policy. Sizes and ownership of individual resources (audio textures, pools,
render-graph targets) are written by V2-4-03 and V2-4-05, when those resources exist.

**Status:** the decidable half. Every rule below runs and is tested on the JVM; the probe
outcomes that feed it are measured on a device by the V2-4-01b prober, which does not exist
yet. Until it runs, no production code consumes these types.

---

## 1. Facts and judgments are different types

`GlProbeReport` holds only **facts** read off one live GL context: the three identity strings,
the extension set, integer limits, and per-format behavioural outcomes. `GlCapabilities.derive`
and `FormatPolicy.resolve` turn facts into **judgments**, fresh on every load. The cache stores
facts only, so a better derivation rule next month applies to outcomes cached today instead of
being masked by them.

A `FormatProbe` is proven behaviour, never an advertisement:

| Field | Proven by |
|---|---|
| `attachable` | FBO completeness with the format as the colour attachment |
| `rendersExactly` | draw + readback returning the expected texels |
| `blendsAdditively` | GL_ONE/GL_ONE accumulation reading back correctly |
| `filtersLinearly` | a magnified sample between two texels interpolating |

A format absent from the report was not probed and counts as failed. An empty report therefore
claims nothing — that is the honest state of a device before its first probe pass.

## 2. Capability derivation

§6.3: "never infer support from GLES version alone." Every capability needs the version, the
relevant limit **and** any behavioural proof, together:

| Capability | Rule |
|---|---|
| `computeShaders` | ES ≥ 3.1 **and** `maxComputeWorkGroupInvocations ≥ 128` (the ES 3.1 floor) |
| `storageBuffersInCompute` | ES ≥ 3.1 **and** `maxComputeStorageBlocks ≥ 4` (the floor) |
| `storageBuffersInFragment` | ES ≥ 3.1 **and** `maxFragmentStorageBlocks > 0` (spec floor is zero — genuinely optional) |
| `imageLoadStore` | ES ≥ 3.1 **and** `maxComputeImageUniforms ≥ 4` |
| `vertexTextureFetch` | `maxVertexTextureImageUnits > 0` **and** the behavioural fetch probe passed |
| `timerQueries` | ABSENT → UNTRUSTED → TRUSTED ladder: extension present, then timings proven monotonic/nonzero |
| `programBinaries` | `programBinaryFormats > 0` |

A driver below a spec floor is not a slower 3.1 — it is a context whose version string cannot
be trusted for that capability. An unparseable `GL_VERSION` enables nothing enhanced.

## 3. Format policy, by resource role

Downstream slices ask "what does this role get here", never "is R16F supported". Each role
walks its own ladder; a rung is taken only on proven behaviour; the bottom rung is `RGBA8`,
whose renderability is core-mandated, so even a driver that fails every probe gets a named
plan rather than a black frame (§9.3).

| Role | Preferred | Requires (proven) | Fallback |
|---|---|---|---|
| Simulation state (ping-pong, exact) | `RGBA32UI`, float bits packed in uint | renderable | `RGBA16F` linear if renderable, else `RGBA8` pre-scaled |
| Filterable field (velocity, height) | `RG16F` linear | renderable + filterable | `RGBA32UI` packed with manual interpolation, else `RGBA8` pre-scaled |
| Linear accumulation (additive deposits) | `R16F` linear | renderable **+ additively blendable** | `RGBA8` pre-scaled |
| Audio textures (uploaded, never rendered to) | `R16F` linear | filterable only — half-float *filtering* is core ES 3.0 | `RGBA8` pre-scaled |
| Linear colour target (blend/bloom headroom) | `RGBA16F` linear | renderable | `RGBA8` linear, range clamped at 1.0 |

Two commitments from §6.3 are encoded in the types rather than in prose:

- `TexelEncoding` has **no logarithmic member**. A deposit field's resolution can only be
  `LINEAR` or `PRE_SCALED`; log-packing an additive target is unrepresentable.
- The accumulation rung requires the *blend* proof, not just attachability — a format that
  attaches but cannot additively blend is exactly the driver lie the probe exists to catch.

Every `ResolvedFormat` carries `because`, the sentence a debug capability screen shows so a
tester on an unfamiliar device can read which probe made the choice.

## 4. The capability cache

`CapabilityCache` persists one probe report as versioned text. The layout is fixed and owned
by `SCHEMA_VERSION`; fields are written and read in one order and anything that deviates —
schema bump, truncation, a tampered value, an unknown key — decodes to null. Null has one
meaning: **re-probe**, which is always safe and merely slower.

Identity is the vendor, renderer and full `GL_VERSION` strings as `glGetString` returns them
*now*; a driver update shows only in the version string, so all three are compared. New fields
mean a schema bump, never a lenient parse.

## 5. What V2-4-01b owes

The on-device prober: an EGL probe context, the `glGetString`/`glGetIntegerv` reads, the
attach-render-readback loop per format, the vertex-fetch and timer-query behavioural probes,
and wiring the cache to app storage. Plus the first real reports from a Mali and an Adreno
device — the evidence V2-0-04 has carried since Phase 0.
