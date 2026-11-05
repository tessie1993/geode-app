package dev.geode.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalResources
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.R
import dev.geode.data.MilkPackImporter
import dev.geode.data.MilkTexture
import dev.geode.data.MilkTextureLink
import dev.geode.data.MilkTextureLinkKind
import dev.geode.render.VisualizerView
import dev.geode.render.scene.SceneIds
import dev.geode.ui.theme.GeodeGlass
import dev.geode.ui.theme.GeodeRadius
import dev.geode.ui.theme.GeodeSpacing
import dev.geode.ui.theme.LocalGeodeMotion
import dev.geode.ui.theme.StoneHapticCue
import dev.geode.ui.theme.StoneIcon
import dev.geode.ui.theme.StoneIconArt
import dev.geode.ui.theme.performStoneHaptic
import dev.geode.ui.theme.selectedGlow
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Visuals › MilkDrop: the user's own .milk presets and what they need, as an overview (V2 rules
 * 1–3). The page opens as collapsed cards, each with its name and its state on the right ("Neon
 * Cathedral", "2 to check", "14 images"); a tap opens one in place and closes the last. Loading a
 * file or a folder sits in the presets card's ⋮, not in a row of buttons, and nothing carries a
 * caption paragraph.
 *
 * - Your presets: tap one to load it; the loaded one is checked.
 * - This preset's textures: only when the linker had to decide something for the loaded preset (a
 *   rename undone, a stand-in, the user's own choice, a missing image). Exact matches say nothing:
 *   a working preset needs no card. Each row opens the texture picker.
 * - Textures: drills into the imported images ([TexturesHubTab]), since they only matter here.
 */
@Composable
internal fun MilkDropTab(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    onOpenTextures: () -> Unit,
) {
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val pickerContext = LocalContext.current
    var refresh by remember { mutableIntStateOf(0) }
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    // Off the main thread: userMilkPresets() runs MilkStarterPack.install() - a marker read, an
    // assets.list, an exists() per asset and up to six asset->file copies on first run - then
    // listFiles() plus a lastModified() stat per preset for the sort. Inside composition that
    // measured 45-53 ms of main-thread disk I/O on every launch. Same key as before, so the
    // refresh++ below still relists; the list simply arrives a frame later instead of stalling one.
    val milkFiles by produceState(emptyList<File>(), refresh) {
        value = withContext(Dispatchers.IO) { visualsViewModel.userMilkPresets() }
    }
    val loaded by viewModel.activeMilkPath.collectAsStateWithLifecycle()
    var packReport by remember { mutableStateOf<MilkPackImporter.Report?>(null) }
    val importedTextures by visualsViewModel.textures.collectAsStateWithLifecycle()
    var linkRefresh by remember { mutableIntStateOf(0) }
    // What the linker decided for the preset on screen, kept live so a texture import or a manual
    // choice shows without leaving the page. Also disk-bound (resolutionFor() stats the preset and
    // its .links/<stem> record), so it is read off the main thread like milkFiles.
    val textureLinks by produceState(emptyList<MilkTextureLink>(), loaded, refresh, linkRefresh, importedTextures) {
        value =
            withContext(Dispatchers.IO) {
                loaded?.let { runCatching { viewModel.milkTextureLinksFor(it) }.getOrDefault(emptyList()) }.orEmpty()
            }
    }
    var pickTextureFor by remember { mutableStateOf<String?>(null) }
    // One card open at a time (solo accordion), none at first.
    var open by rememberSaveable { mutableStateOf<String?>(null) }
    val milkFolderPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            if (uri != null) {
                viewModel.importMilkFolderAsync(uri) { report ->
                    packReport = report
                    refresh++
                }
            }
        }
    val milkPicker =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                viewModel.importMilkPresetAsync(uri) { path ->
                    if (path != null) {
                        selectMilk(viewModel, visualizerView, path)
                        refresh++
                    }
                }
            }
        }
    if (!visualizerView.visualizerRenderer.milkdropAvailable) {
        Text(stringResource(R.string.milk_unavailable), Modifier.padding(GeodeSpacing.L), style = MaterialTheme.typography.bodyMedium)
        return
    }
    val onMilk = viz.sceneId == SceneIds.MILKDROP
    val toCheck = if (onMilk) textureLinks.filter { it.kind != MilkTextureLinkKind.MATCHED } else emptyList()
    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(GeodeSpacing.M),
        verticalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        if (onMilk) {
            viz.shaderError?.let {
                Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.labelMedium, maxLines = 2)
            }
        }
        val presetsTitle = stringResource(R.string.milk_presets)
        PageCard(
            title = presetsTitle,
            summary =
                loaded?.let { File(it).nameWithoutExtension }
                    ?: if (milkFiles.isEmpty()) {
                        stringResource(R.string.milk_presets_none)
                    } else {
                        pluralStringResource(R.plurals.milk_presets_count, milkFiles.size, milkFiles.size)
                    },
            expanded = open == CARD_PRESETS,
            onToggle = { open = if (open == CARD_PRESETS) null else CARD_PRESETS },
            menu = { dismiss ->
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.milk_load_file)) },
                    leadingIcon = { StoneIconArt(StoneIcon.FILE, null) },
                    onClick = {
                        dismiss()
                        milkPicker.launch(arrayOf("*/*"))
                    },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.milk_import_folder)) },
                    leadingIcon = { StoneIconArt(StoneIcon.FOLDER, null) },
                    onClick = {
                        dismiss()
                        milkFolderPicker.launchPicker(pickerContext, null)
                    },
                )
            },
            menuDescription = stringResource(R.string.milk_more),
        ) {
            packReport?.let { ImportReportLine(it) }
            if (milkFiles.isEmpty()) {
                Text(
                    stringResource(R.string.milk_presets_empty),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            milkFiles.forEach { f ->
                val active = f.absolutePath == loaded
                PageRow(
                    label = f.nameWithoutExtension,
                    selected = active,
                    onClick = {
                        selectMilk(viewModel, visualizerView, f.absolutePath)
                        refresh++
                    },
                ) {
                    if (active) StoneIconArt(StoneIcon.CHECK, null, tint = MaterialTheme.colorScheme.primary)
                }
            }
        }
        if (toCheck.isNotEmpty()) {
            PageCard(
                title = stringResource(R.string.milk_links),
                summary = pluralStringResource(R.plurals.milk_links_to_check, toCheck.size, toCheck.size),
                expanded = open == CARD_LINKS,
                onToggle = { open = if (open == CARD_LINKS) null else CARD_LINKS },
            ) {
                toCheck.forEach { link -> TextureLinkRow(link, onPick = { pickTextureFor = link.expected }) }
            }
        }
        PageRow(
            label = stringResource(R.string.milk_textures),
            selected = false,
            onClick = onOpenTextures,
            modifier = Modifier.clip16().background(Color.White.copy(alpha = GeodeGlass.FILL_RAISED)),
        ) {
            Text(
                if (importedTextures.isEmpty()) {
                    stringResource(R.string.milk_textures_none)
                } else {
                    pluralStringResource(R.plurals.milk_textures_count, importedTextures.size, importedTextures.size)
                },
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            StoneIconArt(StoneIcon.CHEVRON_RIGHT, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
    val pickingFor = pickTextureFor
    val loadedPath = loaded
    if (onMilk && pickingFor != null && loadedPath != null) {
        MilkTexturePickerDialog(
            expected = pickingFor,
            textures = importedTextures,
            onChoose = { chosen ->
                viewModel.assignMilkTextureAsync(loadedPath, pickingFor, chosen) {
                    pickTextureFor = null
                    linkRefresh++
                    // Reload so the new link is what projectM reads - search paths are re-set on
                    // every preset load, so this is the whole refresh.
                    selectMilk(viewModel, visualizerView, loadedPath)
                }
            },
            onDismiss = { pickTextureFor = null },
        )
    }
}

/** What a folder import did, in one short line; red when some presets lack their textures. */
@Composable
private fun ImportReportLine(report: MilkPackImporter.Report) {
    val res = LocalResources.current
    val line =
        buildList {
            add(res.getQuantityString(R.plurals.milk_import_presets, report.presets, report.presets))
            add(res.getQuantityString(R.plurals.milk_import_textures, report.textures, report.textures))
            if (report.skipped > 0) add(res.getQuantityString(R.plurals.milk_import_skipped, report.skipped, report.skipped))
            if (report.presetsMissingTextures > 0) {
                add(res.getQuantityString(R.plurals.milk_import_missing, report.presetsMissingTextures, report.presetsMissingTextures))
            }
        }.joinToString(" · ")
    Text(
        line,
        style = MaterialTheme.typography.labelMedium,
        color = if (report.presetsMissingTextures > 0) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/** One texture the linker decided on: the name the preset asks for, and what it got. */
@Composable
private fun TextureLinkRow(
    link: MilkTextureLink,
    onPick: () -> Unit,
) {
    val note =
        when (link.kind) {
            MilkTextureLinkKind.MATCHED -> ""
            MilkTextureLinkKind.RENAMED -> stringResource(R.string.milk_link_renamed)
            MilkTextureLinkKind.OVERRIDDEN -> stringResource(R.string.milk_link_overridden)
            MilkTextureLinkKind.SUBSTITUTED -> stringResource(R.string.milk_link_substituted)
            MilkTextureLinkKind.MISSING -> stringResource(R.string.milk_link_missing)
        }
    val needsYou = link.kind == MilkTextureLinkKind.MISSING || link.kind == MilkTextureLinkKind.SUBSTITUTED
    PageRow(label = link.expected, selected = false, onClick = onPick) {
        Text(
            link.texture?.let { stringResource(R.string.milk_link_value, it, note) } ?: note,
            style = MaterialTheme.typography.labelMedium,
            color = if (needsYou) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun MilkTexturePickerDialog(
    expected: String,
    textures: List<MilkTexture>,
    onChoose: (String?) -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        confirmButton = {},
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.action_cancel)) } },
        title = { Text(stringResource(R.string.milk_texture_pick_title, expected)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                Text(
                    stringResource(R.string.milk_texture_pick_body),
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                textures.forEach { tex -> PageRow(label = tex.name, selected = false, onClick = { onChoose(tex.name) }) }
                PageRow(label = stringResource(R.string.milk_texture_auto), selected = false, onClick = { onChoose(null) }) {
                    StoneIconArt(StoneIcon.CHECK, null, tint = MaterialTheme.colorScheme.primary)
                }
            }
        },
    )
}

/**
 * A collapsible overview card (V2 rule 1): the name, the current value on the right, ▾ and an
 * optional ⋮ menu in one 56 dp header; a tap on the header opens the body in place. The body grows
 * with the motion scheme's spring, which "Remove animations" turns into a snap.
 */
@Composable
private fun PageCard(
    title: String,
    summary: String,
    expanded: Boolean,
    onToggle: () -> Unit,
    menu: (@Composable (dismiss: () -> Unit) -> Unit)? = null,
    menuDescription: String? = null,
    content: @Composable () -> Unit,
) {
    val view = LocalView.current
    val motion = LocalGeodeMotion.current
    val turn by animateFloatAsState(if (expanded) HALF_TURN else 0f, motion.defaultEffectsSpec(), label = "card-chevron")
    val action = stringResource(if (expanded) R.string.milk_collapse else R.string.milk_expand, title)
    // The summary is read as the row's own text; the state says whether the card is open.
    val state = stringResource(if (expanded) R.string.milk_card_expanded else R.string.milk_card_collapsed)
    Column(
        Modifier
            .fillMaxWidth()
            // The open card glows softly; drawn before the clip so the glow is not cut off.
            .selectedGlow(expanded, RoundedCornerShape(GeodeRadius.Large))
            .clip16()
            .background(Color.White.copy(alpha = GeodeGlass.FILL_RAISED))
            .animateContentSize(motion.defaultSpatialSpec()),
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .heightIn(min = CARD_HEADER)
                .clickable(role = Role.Button, onClickLabel = action) {
                    view.performStoneHaptic(StoneHapticCue.TAP)
                    onToggle()
                }.semantics { stateDescription = state }
                .padding(start = GeodeSpacing.L),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            // Two lines then an ellipsis (#558): a long title at a large font wraps once instead
            // of clipping, and the header grows with it (heightIn, not height).
            Text(
                title,
                Modifier.weight(1f),
                style = MaterialTheme.typography.titleSmall,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                summary,
                Modifier.padding(horizontal = GeodeSpacing.S).weight(1f, fill = false),
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Icon(
                StoneIcon.EXPAND.vector,
                null,
                Modifier.graphicsLayer { rotationZ = turn },
                tint = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (menu != null) {
                var menuOpen by remember { mutableStateOf(false) }
                Box {
                    IconButton(onClick = { menuOpen = true }) { StoneIconArt(StoneIcon.MORE, menuDescription) }
                    DropdownMenu(menuOpen, onDismissRequest = { menuOpen = false }) { menu { menuOpen = false } }
                }
            } else {
                Box(Modifier.padding(end = GeodeSpacing.S))
            }
        }
        if (expanded) {
            Column(Modifier.padding(start = GeodeSpacing.L, end = GeodeSpacing.S, bottom = GeodeSpacing.S)) { content() }
        }
    }
}

/** A 48 dp list row: a label, and whatever [trailing] puts on the right. */
@Composable
private fun PageRow(
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    trailing: @Composable () -> Unit = {},
) {
    val view = LocalView.current
    Row(
        modifier
            .fillMaxWidth()
            .heightIn(min = GeodeSpacing.TouchTarget)
            .clickable(role = Role.Button) {
                view.performStoneHaptic(StoneHapticCue.TAP)
                onClick()
            }.then(if (selected) Modifier.semantics { this.selected = true } else Modifier)
            .padding(horizontal = GeodeSpacing.S),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(GeodeSpacing.S),
    ) {
        Text(
            label,
            Modifier.weight(1f),
            style = MaterialTheme.typography.bodyMedium,
            color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        trailing()
    }
}

private fun Modifier.clip16(): Modifier = clip(RoundedCornerShape(GeodeRadius.Large))

internal fun selectMilk(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
    path: String,
) {
    viewModel.selectScene(SceneIds.MILKDROP)
    visualizerView.visualizerRenderer.loadMilkPreset(path)
}

private const val CARD_PRESETS = "presets"
private const val CARD_LINKS = "links"
private const val HALF_TURN = 180f
private val CARD_HEADER = 56.dp
