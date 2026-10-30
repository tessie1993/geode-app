package dev.geode.ui

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class PrivacyPolicyTest {
    /**
     * The app shows the copy in its assets; the store links to the one in docs. They are
     * the same promise, so a change to one without the other fails here. Unit tests run
     * with the module directory as the working directory.
     */
    @Test
    fun bundledPolicyMatchesTheDocsCopy() {
        val bundled = File("src/main/assets/privacy_policy.md").readText()
        val docs = File("../docs/privacy-policy.md").readText()
        assertEquals("Copy docs/privacy-policy.md to app/src/main/assets/privacy_policy.md", docs, bundled)
    }

    @Test
    fun theWholePolicyReadsAsPlainText() {
        val lines = policyLines(File("src/main/assets/privacy_policy.md").readText())
        assertEquals(PolicyLine.Heading("MuzViz Studio Pro — Privacy Policy"), lines.first())
        val text = lines.joinToString("\n") { it.text }
        assertTrue("markup left in: $text", listOf("**", "`", "|", "](").none { it in text })
    }

    @Test
    fun paragraphsBulletsAndTableRows() {
        val markdown =
            """
            ## Heading

            One **bold** and
            *italic* line.

            - **`A`** first
              continued
            - [Site](https://example.com) and [a@b.c](mailto:a@b.c)

            | What | Why |
            |---|---|
            | Mic | Visuals |
            """.trimIndent()
        assertEquals(
            listOf(
                PolicyLine.Heading("Heading"),
                PolicyLine.Paragraph("One bold and italic line."),
                PolicyLine.Bullet("A first continued"),
                PolicyLine.Bullet("Site (https://example.com) and a@b.c"),
                PolicyLine.Bullet("Mic: Visuals."),
            ),
            policyLines(markdown),
        )
    }
}
