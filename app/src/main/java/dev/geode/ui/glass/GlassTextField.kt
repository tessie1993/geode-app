package dev.geode.ui.glass

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp

/** A pearl outlined field: a glass pill body, no visible border, placeholder in the muted tone. */
@Composable
fun GlassTextField(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    placeholder: String? = null,
    enabled: Boolean = true,
    singleLine: Boolean = true,
    keyboardType: KeyboardType = KeyboardType.Text,
    visualTransformation: VisualTransformation = VisualTransformation.None,
) {
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        modifier = modifier.glassSurface(shape = GlassShapes.pill),
        enabled = enabled,
        singleLine = singleLine,
        textStyle =
            LocalTextStyle.current.copy(
                color = GlassPalette.textPrimary,
                fontSize = MaterialTheme.typography.bodyLarge.fontSize,
            ),
        cursorBrush = SolidColor(GlassPalette.mint),
        visualTransformation = visualTransformation,
        keyboardOptions = KeyboardOptions(keyboardType = keyboardType),
        decorationBox = { innerTextField ->
            Box(Modifier.padding(horizontal = 18.dp, vertical = 12.dp)) {
                if (value.isEmpty() && placeholder != null) {
                    Text(placeholder, color = GlassPalette.textSecondary, style = MaterialTheme.typography.bodyLarge)
                }
                innerTextField()
            }
        },
    )
}
