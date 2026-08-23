package dev.geode.render.scene

/**
 * The user's Detail control as a raymarch step budget.
 *
 * The marched fragment styles (VANISHING, MORPHOGEN, NEBULA, NONEUCLID, KIFS) all bound
 * their loop with a compile-time constant and BREAK on [steps], so Detail moves without
 * recompiling anything and no shader can ever iterate past its own ceiling.
 *
 * It used to carry three more counts - `iterations`, `bulbIterations`, `seedIterations` -
 * for the six distance-estimator species in the Hyperspace family. That family is gone and
 * nothing read them afterwards, so they went with it rather than sitting here looking live.
 */
@JvmInline
value class MarchBudget(
    val steps: Int,
) {
    companion object {
        /** The loop ceiling every marched shader declares. A budget can never exceed it. */
        const val MAX_STEPS: Int = 128

        const val MIN_DETAIL: Float = 0.25f
        const val MAX_DETAIL: Float = 1.5f

        /**
         * Floor rather than 1: below this the surface breaks up into visible banding on the
         * deeper styles, which reads as a bug rather than as a quality setting.
         */
        private const val FLOOR_STEPS: Int = 64

        fun forDetail(detail: Float): MarchBudget {
            val t = ((detail - MIN_DETAIL) / (MAX_DETAIL - MIN_DETAIL)).coerceIn(0f, 1f)
            return MarchBudget(Math.round(FLOOR_STEPS + (MAX_STEPS - FLOOR_STEPS) * t))
        }
    }
}
