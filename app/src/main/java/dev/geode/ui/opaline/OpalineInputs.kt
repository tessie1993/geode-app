package dev.geode.ui.opaline

import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.error
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp

/**
 * An editable field in a C13 inset well with a quiet front. The host text field owns caret,
 * selection, IME and accessibility; the well only shows focus (a lit rim) and state. The label
 * sits inside the field's decoration, so it is announced as the field's label and tapping it
 * focuses the field.
 */
@Composable
fun OpalineTextField(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    label: String? = null,
    placeholder: String? = null,
    singleLine: Boolean = true,
    minLines: Int = 1,
    maxLines: Int = if (singleLine) 1 else Int.MAX_VALUE,
    enabled: Boolean = true,
    readOnly: Boolean = false,
    isError: Boolean = false,
    supportingText: String? = null,
    leadingIcon: ImageVector? = null,
    trailing: (@Composable () -> Unit)? = null,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default,
    keyboardActions: KeyboardActions = KeyboardActions.Default,
    visualTransformation: VisualTransformation = VisualTransformation.None,
    textStyle: TextStyle = MaterialTheme.typography.bodyLarge,
) {
    val palette = Opaline.palette
    val interaction = remember { MutableInteractionSource() }
    val contact = rememberOpalineContact(interaction, enabled)
    val errorText = supportingText.takeIf { isError }
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        modifier =
            modifier
                .fillMaxWidth()
                .semantics { if (errorText != null) error(errorText) },
        enabled = enabled,
        readOnly = readOnly,
        textStyle = textStyle.copy(color = palette.text),
        keyboardOptions = keyboardOptions,
        keyboardActions = keyboardActions,
        singleLine = singleLine,
        minLines = minLines,
        maxLines = maxLines,
        visualTransformation = visualTransformation,
        interactionSource = interaction,
        cursorBrush = SolidColor(palette.accent),
        decorationBox = { inner ->
            Column(Modifier.alpha(if (enabled) 1f else DISABLED_ALPHA), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (label != null) {
                    Text(
                        label,
                        Modifier.padding(start = 6.dp),
                        style = MaterialTheme.typography.labelMedium,
                        color = if (isError) palette.error else if (contact.focused) palette.accent else palette.textMuted,
                    )
                }
                Row(
                    Modifier
                        .fillMaxWidth()
                        .defaultMinSize(minHeight = 52.dp)
                        .then(rememberOpalineSurface(OpalineBody.Well, contact, selected = isError, enabled = enabled))
                        .padding(start = if (leadingIcon != null) 12.dp else 16.dp, end = if (trailing != null) 4.dp else 16.dp, top = 14.dp, bottom = 14.dp),
                    verticalAlignment = if (singleLine) Alignment.CenterVertically else Alignment.Top,
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    if (leadingIcon != null) Icon(leadingIcon, null, Modifier.size(22.dp), tint = palette.textMuted)
                    Box(Modifier.weight(1f)) {
                        if (value.isEmpty() && placeholder != null) {
                            Text(placeholder, style = textStyle, color = palette.textMuted, maxLines = maxLines)
                        }
                        inner()
                    }
                    trailing?.invoke()
                }
                if (supportingText != null) {
                    Text(
                        supportingText,
                        Modifier.padding(start = 6.dp),
                        style = MaterialTheme.typography.bodySmall,
                        color = if (isError) palette.error else palette.textMuted,
                    )
                }
            }
        },
    )
}
