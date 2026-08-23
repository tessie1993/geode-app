package dev.geode.engine.gl

import android.content.Context
import android.util.Log
import dev.geode.util.bestEffort
import java.io.File

/** Where the facts behind a [GlProfile] came from. Worth logging: a device that re-probes on
 * every launch has a cache that is not sticking, and that is a bug worth seeing. */
enum class ProbeSource {
    /** Decoded from disk; still valid for this exact vendor/renderer/version. */
    CACHED_FACTS,

    /** Measured just now against a live context, and stored. */
    FRESH_PROBE,

    /** No context could be obtained. The report claims nothing and every role gets RGBA8. */
    NO_CONTEXT,
}

/**
 * Everything the renderer needs to know about this device's GL, in one value.
 *
 * The split is deliberate and comes straight from the ABI: [report] is **facts**, and
 * [capabilities], [formats] and [tier] are **judgments** derived fresh from those facts on
 * every load. Only the facts are cached, so a better derivation rule next month applies to
 * outcomes measured today instead of being masked by them.
 */
data class GlProfile(
    val report: GlProbeReport,
    val capabilities: GlCapabilities,
    val formats: FormatPlan,
    val tier: GlTier,
    val source: ProbeSource,
) {
    /** One line for logcat or a debug capability screen: what was chosen, and why. */
    val summary: String
        get() =
            "gl=${tier.label} source=$source state=${formats.simulationState.format} " +
                "accum=${formats.linearAccumulation.format} colour=${formats.linearColorTarget.format} " +
                "vtf=${capabilities.vertexTextureFetch} timer=${capabilities.timerQueries} — ${tier.because}"
}

/**
 * The composition root for GL capability: cache if the stored facts still describe this
 * driver, probe and store otherwise, then derive capabilities, resolve the per-role format
 * plan, and decide the tier.
 *
 * ### Which call needs a GL context
 *
 * - [profileWithCurrentContext] — **requires** a current context and must run on the thread
 *   that owns it. This is the `onSurfaceCreated` call.
 * - [profileInOwnContext] — needs **no** context; it makes a throwaway one, and refuses if a
 *   context is already current. This is the cold-start call, from a background thread.
 * - [forget] — no context, any thread.
 *
 * Neither profile call throws. A device that fails every probe still gets a complete
 * [GlProfile] naming the ES 3.0 baseline and the RGBA8 floor, because a named plan beats a
 * black frame.
 *
 * Both profile calls touch the filesystem — a few hundred bytes in `noBackupFilesDir`. Neither
 * belongs on the main thread, and neither is ever called from it: the GL thread is not the
 * main thread, so the read in `onSurfaceCreated` raises no StrictMode disk violation.
 */
object DeviceGl {
    private const val TAG = "DeviceGl"

    private const val CACHE_FILE = "gl-probe-facts.txt"

    /**
     * The last profile computed, reused when the driver identity still matches.
     *
     * `onSurfaceCreated` runs again every time the surface is recreated — a rotation, a return
     * from background, the wallpaper engine restarting — and re-reading the cache file on each
     * of those is pure waste. Written and read from the GL thread; `@Volatile` because the
     * cold-start path may compute it on a different thread first.
     */
    @Volatile
    private var memo: GlProfile? = null

    /**
     * Reads the profile using the context that is already current on this thread.
     *
     * Cost after the first run on a given driver: three `glGetString` calls and, at most, one
     * small file read. Cost on the first run: the full probe pass — a handful of 4x4 draws and
     * readbacks, once per driver version, before the first frame.
     *
     * @param appContext any Android [Context]; only `noBackupFilesDir` is used, so an
     *   application context is the right thing to pass and nothing is retained.
     */
    fun profileWithCurrentContext(appContext: Context): GlProfile {
        val identity = GlProber.identity()
        memo?.let { if (it.matches(identity)) return it }

        val cached = readCache(appContext, identity)
        if (cached != null) return remember(profileOf(cached, ProbeSource.CACHED_FACTS))

        val report = GlProber.probe()
        writeCache(appContext, report)
        return remember(profileOf(report, ProbeSource.FRESH_PROBE))
    }

    /**
     * Reads the profile on a thread with no GL context, using a throwaway offscreen context.
     *
     * This is the cold-start path: run it while the UI is still assembling and the answer is
     * already in [memo] by the time `onSurfaceCreated` asks for it, so the probe never sits in
     * front of the first frame. Must not be called on the render thread — see
     * [EglProbeHarness.withProbeContext], which refuses rather than stealing that thread's
     * context.
     */
    fun profileInOwnContext(appContext: Context): GlProfile =
        when (
            val outcome =
                EglProbeHarness.withProbeContext { profileWithCurrentContext(appContext) }
        ) {
            is EglProbeOutcome.Probed -> outcome.value
            is EglProbeOutcome.Unavailable -> {
                Log.w(TAG, "no probe context (${outcome.summary}); staying on the ES 3.0 baseline")
                // Deliberately not memoised: an unprobed profile is the absence of a
                // measurement, and the next caller — very possibly one that does have a
                // context — should get a real probe rather than this placeholder.
                unprobed(outcome.summary)
            }
        }

    /**
     * Drops the stored facts so the next read re-probes. For a debug capability screen, and for
     * the case where a driver ships a fix without changing its version string — rare, but the
     * only recovery from it is a manual re-probe.
     */
    fun forget(appContext: Context) {
        memo = null
        bestEffort(TAG, "delete the GL probe cache") { cacheFile(appContext).delete() }
    }

    private fun profileOf(
        report: GlProbeReport,
        source: ProbeSource,
    ): GlProfile {
        val capabilities = GlCapabilities.derive(report)
        return GlProfile(
            report = report,
            capabilities = capabilities,
            formats = FormatPolicy.resolve(report),
            tier = GlTier.of(report, capabilities),
            source = source,
        )
    }

    /**
     * A profile for a device nothing has been measured on: the ES 3.0 baseline and the RGBA8
     * floor for every role. **Needs no GL context and no Android context**, so it is also the
     * right initial value for a renderer's profile field — a renderer holding this is one whose
     * surface has not been created yet, which is true and says so, rather than a null that
     * every reader has to think about.
     */
    fun unprobed(detail: String = "not probed yet"): GlProfile {
        val report = GlProber.unprobed()
        return GlProfile(
            report = report,
            capabilities = GlCapabilities.derive(report),
            formats = FormatPolicy.resolve(report),
            tier = GlTier.Baseline(BaselineCause.NoProbeContext(detail)),
            source = ProbeSource.NO_CONTEXT,
        )
    }

    private fun remember(profile: GlProfile): GlProfile {
        memo = profile
        Log.i(TAG, profile.summary)
        return profile
    }

    private fun GlProfile.matches(identity: GlIdentity): Boolean =
        report.vendor == identity.vendor &&
            report.renderer == identity.renderer &&
            report.versionString == identity.versionString

    private fun cacheFile(appContext: Context): File = File(appContext.noBackupFilesDir, CACHE_FILE)

    /**
     * `noBackupFilesDir`, not `filesDir`: a GPU capability record is the one kind of file that
     * must never ride Android Backup onto different silicon. The driver-identity check in
     * [CapabilityCache.decode] would reject it there anyway, but the cheapest way not to
     * restore a Mali's probe results onto an Adreno is not to back them up.
     */
    private fun readCache(
        appContext: Context,
        identity: GlIdentity,
    ): GlProbeReport? {
        val text =
            runCatching { cacheFile(appContext).takeIf { it.isFile }?.readText() }
                .onFailure { Log.w(TAG, "could not read the GL probe cache; re-probing", it) }
                .getOrNull() ?: return null
        // decode returns null for a schema bump, a different driver, a truncated write or a
        // tampered value, and null has exactly one meaning: re-probe. That is always safe and
        // merely slower, which is why no repair path exists here.
        return CapabilityCache.decode(text, identity.vendor, identity.renderer, identity.versionString)
    }

    private fun writeCache(
        appContext: Context,
        report: GlProbeReport,
    ) {
        // An identity-less report cannot be keyed on, so storing it would produce a file that
        // can only ever decode to null. That happens when the probe ran without a usable
        // context at all.
        if (report.versionString.isBlank()) return
        bestEffort(TAG, "write the GL probe cache") {
            cacheFile(appContext).writeText(CapabilityCache.encode(report))
        }
    }
}
