package dev.geode.billing

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BillingBuildTest {
    @Test
    fun `fake billing needs the flag and a debuggable build`() {
        assertTrue(BillingBuild.usesFake(flag = true, notPlayBuild = true))
        assertFalse("a release build with the flag set still bills against Play", BillingBuild.usesFake(flag = true, notPlayBuild = false))
        assertFalse(BillingBuild.usesFake(flag = false, notPlayBuild = true))
        assertFalse(BillingBuild.usesFake(flag = false, notPlayBuild = false))
    }
}
