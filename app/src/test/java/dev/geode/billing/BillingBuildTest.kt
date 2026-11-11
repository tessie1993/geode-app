package dev.geode.billing

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BillingBuildTest {
    @Test
    fun `fake billing needs the flag and a debuggable build`() {
        assertTrue(BillingBuild.usesFake(flag = true, debuggable = true))
        assertFalse("a release build with the flag set still bills against Play", BillingBuild.usesFake(flag = true, debuggable = false))
        assertFalse(BillingBuild.usesFake(flag = false, debuggable = true))
        assertFalse(BillingBuild.usesFake(flag = false, debuggable = false))
    }
}
