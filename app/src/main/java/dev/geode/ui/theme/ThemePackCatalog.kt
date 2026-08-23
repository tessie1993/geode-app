package dev.geode.ui.theme

import androidx.compose.ui.graphics.Color
import dev.geode.R

object ThemePackCatalog {
    val lapisLazuli =
        ThemePack(
            slug = "lapis-lazuli",
            name = "Lapis Lazuli",
            stone = "lapis lazuli",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF050A20),
                    backgroundDeep = Color(0xFF02040D),
                    surface = Color(0xFF10265C),
                    surfaceHigh = Color(0xFF173A83),
                    primary = Color(0xFF6D98FF),
                    secondary = Color(0xFFD1B36A),
                    accent = Color(0xFF9BB9FF),
                    glow = Color(0xFF80A7FF),
                    onBackground = Color(0xFFF7F8FF),
                    onSurface = Color(0xFFF7F8FF),
                    muted = Color(0xFFBEC7E2),
                    outline = Color(0xFF516491),
                    danger = Color(0xFFFF817D),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 95,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 240,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_lapis_lazuli_material_tile,
                    glowOverlay = R.drawable.tp_lapis_lazuli_glow_overlay,
                    refractionOverlay = R.drawable.tp_lapis_lazuli_refraction_overlay,
                    ambientPortrait = R.drawable.tp_lapis_lazuli_ambient_portrait,
                    ambientLandscape = R.drawable.tp_lapis_lazuli_ambient_landscape,
                    ambientSquare = R.drawable.tp_lapis_lazuli_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_lapis_lazuli_click_soft,
                    confirm = R.raw.tp_lapis_lazuli_confirm,
                    swoop = R.raw.tp_lapis_lazuli_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_album_tile_default,
                            focused = R.drawable.tp_lapis_lazuli_album_tile_focused,
                            pressed = R.drawable.tp_lapis_lazuli_album_tile_pressed,
                            selected = R.drawable.tp_lapis_lazuli_album_tile_selected,
                            disabled = R.drawable.tp_lapis_lazuli_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_bottom_sheet_default,
                            focused = R.drawable.tp_lapis_lazuli_bottom_sheet_focused,
                            pressed = R.drawable.tp_lapis_lazuli_bottom_sheet_pressed,
                            selected = R.drawable.tp_lapis_lazuli_bottom_sheet_selected,
                            disabled = R.drawable.tp_lapis_lazuli_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_card_default,
                            focused = R.drawable.tp_lapis_lazuli_card_focused,
                            pressed = R.drawable.tp_lapis_lazuli_card_pressed,
                            selected = R.drawable.tp_lapis_lazuli_card_selected,
                            disabled = R.drawable.tp_lapis_lazuli_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_chip_default,
                            focused = R.drawable.tp_lapis_lazuli_chip_focused,
                            pressed = R.drawable.tp_lapis_lazuli_chip_pressed,
                            selected = R.drawable.tp_lapis_lazuli_chip_selected,
                            disabled = R.drawable.tp_lapis_lazuli_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_compact_button_default,
                            focused = R.drawable.tp_lapis_lazuli_compact_button_focused,
                            pressed = R.drawable.tp_lapis_lazuli_compact_button_pressed,
                            selected = R.drawable.tp_lapis_lazuli_compact_button_selected,
                            disabled = R.drawable.tp_lapis_lazuli_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_dialog_default,
                            focused = R.drawable.tp_lapis_lazuli_dialog_focused,
                            pressed = R.drawable.tp_lapis_lazuli_dialog_pressed,
                            selected = R.drawable.tp_lapis_lazuli_dialog_selected,
                            disabled = R.drawable.tp_lapis_lazuli_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_icon_button_default,
                            focused = R.drawable.tp_lapis_lazuli_icon_button_focused,
                            pressed = R.drawable.tp_lapis_lazuli_icon_button_pressed,
                            selected = R.drawable.tp_lapis_lazuli_icon_button_selected,
                            disabled = R.drawable.tp_lapis_lazuli_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_knob_default,
                            focused = R.drawable.tp_lapis_lazuli_knob_focused,
                            pressed = R.drawable.tp_lapis_lazuli_knob_pressed,
                            selected = R.drawable.tp_lapis_lazuli_knob_selected,
                            disabled = R.drawable.tp_lapis_lazuli_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_list_row_default,
                            focused = R.drawable.tp_lapis_lazuli_list_row_focused,
                            pressed = R.drawable.tp_lapis_lazuli_list_row_pressed,
                            selected = R.drawable.tp_lapis_lazuli_list_row_selected,
                            disabled = R.drawable.tp_lapis_lazuli_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_mini_player_default,
                            focused = R.drawable.tp_lapis_lazuli_mini_player_focused,
                            pressed = R.drawable.tp_lapis_lazuli_mini_player_pressed,
                            selected = R.drawable.tp_lapis_lazuli_mini_player_selected,
                            disabled = R.drawable.tp_lapis_lazuli_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_navigation_bar_default,
                            focused = R.drawable.tp_lapis_lazuli_navigation_bar_focused,
                            pressed = R.drawable.tp_lapis_lazuli_navigation_bar_pressed,
                            selected = R.drawable.tp_lapis_lazuli_navigation_bar_selected,
                            disabled = R.drawable.tp_lapis_lazuli_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_primary_button_default,
                            focused = R.drawable.tp_lapis_lazuli_primary_button_focused,
                            pressed = R.drawable.tp_lapis_lazuli_primary_button_pressed,
                            selected = R.drawable.tp_lapis_lazuli_primary_button_selected,
                            disabled = R.drawable.tp_lapis_lazuli_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_progress_ring_default,
                            focused = R.drawable.tp_lapis_lazuli_progress_ring_focused,
                            pressed = R.drawable.tp_lapis_lazuli_progress_ring_pressed,
                            selected = R.drawable.tp_lapis_lazuli_progress_ring_selected,
                            disabled = R.drawable.tp_lapis_lazuli_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_secondary_button_default,
                            focused = R.drawable.tp_lapis_lazuli_secondary_button_focused,
                            pressed = R.drawable.tp_lapis_lazuli_secondary_button_pressed,
                            selected = R.drawable.tp_lapis_lazuli_secondary_button_selected,
                            disabled = R.drawable.tp_lapis_lazuli_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_slider_thumb_default,
                            focused = R.drawable.tp_lapis_lazuli_slider_thumb_focused,
                            pressed = R.drawable.tp_lapis_lazuli_slider_thumb_pressed,
                            selected = R.drawable.tp_lapis_lazuli_slider_thumb_selected,
                            disabled = R.drawable.tp_lapis_lazuli_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_slider_track_default,
                            focused = R.drawable.tp_lapis_lazuli_slider_track_focused,
                            pressed = R.drawable.tp_lapis_lazuli_slider_track_pressed,
                            selected = R.drawable.tp_lapis_lazuli_slider_track_selected,
                            disabled = R.drawable.tp_lapis_lazuli_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_text_field_default,
                            focused = R.drawable.tp_lapis_lazuli_text_field_focused,
                            pressed = R.drawable.tp_lapis_lazuli_text_field_pressed,
                            selected = R.drawable.tp_lapis_lazuli_text_field_selected,
                            disabled = R.drawable.tp_lapis_lazuli_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_lapis_lazuli_toggle_default,
                            focused = R.drawable.tp_lapis_lazuli_toggle_focused,
                            pressed = R.drawable.tp_lapis_lazuli_toggle_pressed,
                            selected = R.drawable.tp_lapis_lazuli_toggle_selected,
                            disabled = R.drawable.tp_lapis_lazuli_toggle_disabled,
                        ),
                ),
        )

    val sugilite =
        ThemePack(
            slug = "sugilite",
            name = "Sugilite",
            stone = "sugilite",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF120A1A),
                    backgroundDeep = Color(0xFF07040B),
                    surface = Color(0xFF281538),
                    surfaceHigh = Color(0xFF43205D),
                    primary = Color(0xFFB58AE8),
                    secondary = Color(0xFFD28BDD),
                    accent = Color(0xFFF2C2FF),
                    glow = Color(0xFFD9A8FF),
                    onBackground = Color(0xFFFBF5FF),
                    onSurface = Color(0xFFFBF5FF),
                    muted = Color(0xFFCDBED6),
                    outline = Color(0xFF745A80),
                    danger = Color(0xFFFF7F9C),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 105,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 255,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_sugilite_material_tile,
                    glowOverlay = R.drawable.tp_sugilite_glow_overlay,
                    refractionOverlay = R.drawable.tp_sugilite_refraction_overlay,
                    ambientPortrait = R.drawable.tp_sugilite_ambient_portrait,
                    ambientLandscape = R.drawable.tp_sugilite_ambient_landscape,
                    ambientSquare = R.drawable.tp_sugilite_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_sugilite_click_soft,
                    confirm = R.raw.tp_sugilite_confirm,
                    swoop = R.raw.tp_sugilite_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_album_tile_default,
                            focused = R.drawable.tp_sugilite_album_tile_focused,
                            pressed = R.drawable.tp_sugilite_album_tile_pressed,
                            selected = R.drawable.tp_sugilite_album_tile_selected,
                            disabled = R.drawable.tp_sugilite_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_bottom_sheet_default,
                            focused = R.drawable.tp_sugilite_bottom_sheet_focused,
                            pressed = R.drawable.tp_sugilite_bottom_sheet_pressed,
                            selected = R.drawable.tp_sugilite_bottom_sheet_selected,
                            disabled = R.drawable.tp_sugilite_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_card_default,
                            focused = R.drawable.tp_sugilite_card_focused,
                            pressed = R.drawable.tp_sugilite_card_pressed,
                            selected = R.drawable.tp_sugilite_card_selected,
                            disabled = R.drawable.tp_sugilite_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_chip_default,
                            focused = R.drawable.tp_sugilite_chip_focused,
                            pressed = R.drawable.tp_sugilite_chip_pressed,
                            selected = R.drawable.tp_sugilite_chip_selected,
                            disabled = R.drawable.tp_sugilite_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_compact_button_default,
                            focused = R.drawable.tp_sugilite_compact_button_focused,
                            pressed = R.drawable.tp_sugilite_compact_button_pressed,
                            selected = R.drawable.tp_sugilite_compact_button_selected,
                            disabled = R.drawable.tp_sugilite_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_dialog_default,
                            focused = R.drawable.tp_sugilite_dialog_focused,
                            pressed = R.drawable.tp_sugilite_dialog_pressed,
                            selected = R.drawable.tp_sugilite_dialog_selected,
                            disabled = R.drawable.tp_sugilite_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_icon_button_default,
                            focused = R.drawable.tp_sugilite_icon_button_focused,
                            pressed = R.drawable.tp_sugilite_icon_button_pressed,
                            selected = R.drawable.tp_sugilite_icon_button_selected,
                            disabled = R.drawable.tp_sugilite_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_knob_default,
                            focused = R.drawable.tp_sugilite_knob_focused,
                            pressed = R.drawable.tp_sugilite_knob_pressed,
                            selected = R.drawable.tp_sugilite_knob_selected,
                            disabled = R.drawable.tp_sugilite_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_list_row_default,
                            focused = R.drawable.tp_sugilite_list_row_focused,
                            pressed = R.drawable.tp_sugilite_list_row_pressed,
                            selected = R.drawable.tp_sugilite_list_row_selected,
                            disabled = R.drawable.tp_sugilite_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_mini_player_default,
                            focused = R.drawable.tp_sugilite_mini_player_focused,
                            pressed = R.drawable.tp_sugilite_mini_player_pressed,
                            selected = R.drawable.tp_sugilite_mini_player_selected,
                            disabled = R.drawable.tp_sugilite_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_navigation_bar_default,
                            focused = R.drawable.tp_sugilite_navigation_bar_focused,
                            pressed = R.drawable.tp_sugilite_navigation_bar_pressed,
                            selected = R.drawable.tp_sugilite_navigation_bar_selected,
                            disabled = R.drawable.tp_sugilite_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_primary_button_default,
                            focused = R.drawable.tp_sugilite_primary_button_focused,
                            pressed = R.drawable.tp_sugilite_primary_button_pressed,
                            selected = R.drawable.tp_sugilite_primary_button_selected,
                            disabled = R.drawable.tp_sugilite_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_progress_ring_default,
                            focused = R.drawable.tp_sugilite_progress_ring_focused,
                            pressed = R.drawable.tp_sugilite_progress_ring_pressed,
                            selected = R.drawable.tp_sugilite_progress_ring_selected,
                            disabled = R.drawable.tp_sugilite_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_secondary_button_default,
                            focused = R.drawable.tp_sugilite_secondary_button_focused,
                            pressed = R.drawable.tp_sugilite_secondary_button_pressed,
                            selected = R.drawable.tp_sugilite_secondary_button_selected,
                            disabled = R.drawable.tp_sugilite_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_slider_thumb_default,
                            focused = R.drawable.tp_sugilite_slider_thumb_focused,
                            pressed = R.drawable.tp_sugilite_slider_thumb_pressed,
                            selected = R.drawable.tp_sugilite_slider_thumb_selected,
                            disabled = R.drawable.tp_sugilite_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_slider_track_default,
                            focused = R.drawable.tp_sugilite_slider_track_focused,
                            pressed = R.drawable.tp_sugilite_slider_track_pressed,
                            selected = R.drawable.tp_sugilite_slider_track_selected,
                            disabled = R.drawable.tp_sugilite_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_text_field_default,
                            focused = R.drawable.tp_sugilite_text_field_focused,
                            pressed = R.drawable.tp_sugilite_text_field_pressed,
                            selected = R.drawable.tp_sugilite_text_field_selected,
                            disabled = R.drawable.tp_sugilite_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_sugilite_toggle_default,
                            focused = R.drawable.tp_sugilite_toggle_focused,
                            pressed = R.drawable.tp_sugilite_toggle_pressed,
                            selected = R.drawable.tp_sugilite_toggle_selected,
                            disabled = R.drawable.tp_sugilite_toggle_disabled,
                        ),
                ),
        )

    val amethyst =
        ThemePack(
            slug = "amethyst",
            name = "Amethyst",
            stone = "amethyst",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF180D25),
                    backgroundDeep = Color(0xFF0B0611),
                    surface = Color(0xFF3A2050),
                    surfaceHigh = Color(0xFF5A3375),
                    primary = Color(0xFFCEAAEF),
                    secondary = Color(0xFF9A78C2),
                    accent = Color(0xFFF1D9FF),
                    glow = Color(0xFFE7C9FF),
                    onBackground = Color(0xFFFEF8FF),
                    onSurface = Color(0xFFFEF8FF),
                    muted = Color(0xFFD7C7E1),
                    outline = Color(0xFF826D91),
                    danger = Color(0xFFFF84A6),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 115,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 280,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_amethyst_material_tile,
                    glowOverlay = R.drawable.tp_amethyst_glow_overlay,
                    refractionOverlay = R.drawable.tp_amethyst_refraction_overlay,
                    ambientPortrait = R.drawable.tp_amethyst_ambient_portrait,
                    ambientLandscape = R.drawable.tp_amethyst_ambient_landscape,
                    ambientSquare = R.drawable.tp_amethyst_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_amethyst_click_soft,
                    confirm = R.raw.tp_amethyst_confirm,
                    swoop = R.raw.tp_amethyst_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_album_tile_default,
                            focused = R.drawable.tp_amethyst_album_tile_focused,
                            pressed = R.drawable.tp_amethyst_album_tile_pressed,
                            selected = R.drawable.tp_amethyst_album_tile_selected,
                            disabled = R.drawable.tp_amethyst_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_bottom_sheet_default,
                            focused = R.drawable.tp_amethyst_bottom_sheet_focused,
                            pressed = R.drawable.tp_amethyst_bottom_sheet_pressed,
                            selected = R.drawable.tp_amethyst_bottom_sheet_selected,
                            disabled = R.drawable.tp_amethyst_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_card_default,
                            focused = R.drawable.tp_amethyst_card_focused,
                            pressed = R.drawable.tp_amethyst_card_pressed,
                            selected = R.drawable.tp_amethyst_card_selected,
                            disabled = R.drawable.tp_amethyst_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_chip_default,
                            focused = R.drawable.tp_amethyst_chip_focused,
                            pressed = R.drawable.tp_amethyst_chip_pressed,
                            selected = R.drawable.tp_amethyst_chip_selected,
                            disabled = R.drawable.tp_amethyst_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_compact_button_default,
                            focused = R.drawable.tp_amethyst_compact_button_focused,
                            pressed = R.drawable.tp_amethyst_compact_button_pressed,
                            selected = R.drawable.tp_amethyst_compact_button_selected,
                            disabled = R.drawable.tp_amethyst_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_dialog_default,
                            focused = R.drawable.tp_amethyst_dialog_focused,
                            pressed = R.drawable.tp_amethyst_dialog_pressed,
                            selected = R.drawable.tp_amethyst_dialog_selected,
                            disabled = R.drawable.tp_amethyst_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_icon_button_default,
                            focused = R.drawable.tp_amethyst_icon_button_focused,
                            pressed = R.drawable.tp_amethyst_icon_button_pressed,
                            selected = R.drawable.tp_amethyst_icon_button_selected,
                            disabled = R.drawable.tp_amethyst_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_knob_default,
                            focused = R.drawable.tp_amethyst_knob_focused,
                            pressed = R.drawable.tp_amethyst_knob_pressed,
                            selected = R.drawable.tp_amethyst_knob_selected,
                            disabled = R.drawable.tp_amethyst_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_list_row_default,
                            focused = R.drawable.tp_amethyst_list_row_focused,
                            pressed = R.drawable.tp_amethyst_list_row_pressed,
                            selected = R.drawable.tp_amethyst_list_row_selected,
                            disabled = R.drawable.tp_amethyst_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_mini_player_default,
                            focused = R.drawable.tp_amethyst_mini_player_focused,
                            pressed = R.drawable.tp_amethyst_mini_player_pressed,
                            selected = R.drawable.tp_amethyst_mini_player_selected,
                            disabled = R.drawable.tp_amethyst_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_navigation_bar_default,
                            focused = R.drawable.tp_amethyst_navigation_bar_focused,
                            pressed = R.drawable.tp_amethyst_navigation_bar_pressed,
                            selected = R.drawable.tp_amethyst_navigation_bar_selected,
                            disabled = R.drawable.tp_amethyst_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_primary_button_default,
                            focused = R.drawable.tp_amethyst_primary_button_focused,
                            pressed = R.drawable.tp_amethyst_primary_button_pressed,
                            selected = R.drawable.tp_amethyst_primary_button_selected,
                            disabled = R.drawable.tp_amethyst_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_progress_ring_default,
                            focused = R.drawable.tp_amethyst_progress_ring_focused,
                            pressed = R.drawable.tp_amethyst_progress_ring_pressed,
                            selected = R.drawable.tp_amethyst_progress_ring_selected,
                            disabled = R.drawable.tp_amethyst_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_secondary_button_default,
                            focused = R.drawable.tp_amethyst_secondary_button_focused,
                            pressed = R.drawable.tp_amethyst_secondary_button_pressed,
                            selected = R.drawable.tp_amethyst_secondary_button_selected,
                            disabled = R.drawable.tp_amethyst_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_slider_thumb_default,
                            focused = R.drawable.tp_amethyst_slider_thumb_focused,
                            pressed = R.drawable.tp_amethyst_slider_thumb_pressed,
                            selected = R.drawable.tp_amethyst_slider_thumb_selected,
                            disabled = R.drawable.tp_amethyst_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_slider_track_default,
                            focused = R.drawable.tp_amethyst_slider_track_focused,
                            pressed = R.drawable.tp_amethyst_slider_track_pressed,
                            selected = R.drawable.tp_amethyst_slider_track_selected,
                            disabled = R.drawable.tp_amethyst_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_text_field_default,
                            focused = R.drawable.tp_amethyst_text_field_focused,
                            pressed = R.drawable.tp_amethyst_text_field_pressed,
                            selected = R.drawable.tp_amethyst_text_field_selected,
                            disabled = R.drawable.tp_amethyst_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_amethyst_toggle_default,
                            focused = R.drawable.tp_amethyst_toggle_focused,
                            pressed = R.drawable.tp_amethyst_toggle_pressed,
                            selected = R.drawable.tp_amethyst_toggle_selected,
                            disabled = R.drawable.tp_amethyst_toggle_disabled,
                        ),
                ),
        )

    val clearQuartz =
        ThemePack(
            slug = "clear-quartz",
            name = "Clear Quartz",
            stone = "clear quartz",
            isLight = true,
            palette =
                StonePalette(
                    background = Color(0xFFEEEAE3),
                    backgroundDeep = Color(0xFFB8B0A5),
                    surface = Color(0xFFF7F4EE),
                    surfaceHigh = Color(0xFFFFFFFF),
                    primary = Color(0xFF9C7845),
                    secondary = Color(0xFF78898F),
                    accent = Color(0xFF6D5639),
                    glow = Color(0xFFFFF7DE),
                    onBackground = Color(0xFF181713),
                    onSurface = Color(0xFF1D1B17),
                    muted = Color(0xFF5D5951),
                    outline = Color(0xFF8E867A),
                    danger = Color(0xFFA13232),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 105,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 260,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_clear_quartz_material_tile,
                    glowOverlay = R.drawable.tp_clear_quartz_glow_overlay,
                    refractionOverlay = R.drawable.tp_clear_quartz_refraction_overlay,
                    ambientPortrait = R.drawable.tp_clear_quartz_ambient_portrait,
                    ambientLandscape = R.drawable.tp_clear_quartz_ambient_landscape,
                    ambientSquare = R.drawable.tp_clear_quartz_ambient_square,
                    backgroundOpacity = 0.38f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_clear_quartz_click_soft,
                    confirm = R.raw.tp_clear_quartz_confirm,
                    swoop = R.raw.tp_clear_quartz_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_album_tile_default,
                            focused = R.drawable.tp_clear_quartz_album_tile_focused,
                            pressed = R.drawable.tp_clear_quartz_album_tile_pressed,
                            selected = R.drawable.tp_clear_quartz_album_tile_selected,
                            disabled = R.drawable.tp_clear_quartz_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_bottom_sheet_default,
                            focused = R.drawable.tp_clear_quartz_bottom_sheet_focused,
                            pressed = R.drawable.tp_clear_quartz_bottom_sheet_pressed,
                            selected = R.drawable.tp_clear_quartz_bottom_sheet_selected,
                            disabled = R.drawable.tp_clear_quartz_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_card_default,
                            focused = R.drawable.tp_clear_quartz_card_focused,
                            pressed = R.drawable.tp_clear_quartz_card_pressed,
                            selected = R.drawable.tp_clear_quartz_card_selected,
                            disabled = R.drawable.tp_clear_quartz_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_chip_default,
                            focused = R.drawable.tp_clear_quartz_chip_focused,
                            pressed = R.drawable.tp_clear_quartz_chip_pressed,
                            selected = R.drawable.tp_clear_quartz_chip_selected,
                            disabled = R.drawable.tp_clear_quartz_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_compact_button_default,
                            focused = R.drawable.tp_clear_quartz_compact_button_focused,
                            pressed = R.drawable.tp_clear_quartz_compact_button_pressed,
                            selected = R.drawable.tp_clear_quartz_compact_button_selected,
                            disabled = R.drawable.tp_clear_quartz_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_dialog_default,
                            focused = R.drawable.tp_clear_quartz_dialog_focused,
                            pressed = R.drawable.tp_clear_quartz_dialog_pressed,
                            selected = R.drawable.tp_clear_quartz_dialog_selected,
                            disabled = R.drawable.tp_clear_quartz_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_icon_button_default,
                            focused = R.drawable.tp_clear_quartz_icon_button_focused,
                            pressed = R.drawable.tp_clear_quartz_icon_button_pressed,
                            selected = R.drawable.tp_clear_quartz_icon_button_selected,
                            disabled = R.drawable.tp_clear_quartz_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_knob_default,
                            focused = R.drawable.tp_clear_quartz_knob_focused,
                            pressed = R.drawable.tp_clear_quartz_knob_pressed,
                            selected = R.drawable.tp_clear_quartz_knob_selected,
                            disabled = R.drawable.tp_clear_quartz_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_list_row_default,
                            focused = R.drawable.tp_clear_quartz_list_row_focused,
                            pressed = R.drawable.tp_clear_quartz_list_row_pressed,
                            selected = R.drawable.tp_clear_quartz_list_row_selected,
                            disabled = R.drawable.tp_clear_quartz_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_mini_player_default,
                            focused = R.drawable.tp_clear_quartz_mini_player_focused,
                            pressed = R.drawable.tp_clear_quartz_mini_player_pressed,
                            selected = R.drawable.tp_clear_quartz_mini_player_selected,
                            disabled = R.drawable.tp_clear_quartz_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_navigation_bar_default,
                            focused = R.drawable.tp_clear_quartz_navigation_bar_focused,
                            pressed = R.drawable.tp_clear_quartz_navigation_bar_pressed,
                            selected = R.drawable.tp_clear_quartz_navigation_bar_selected,
                            disabled = R.drawable.tp_clear_quartz_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_primary_button_default,
                            focused = R.drawable.tp_clear_quartz_primary_button_focused,
                            pressed = R.drawable.tp_clear_quartz_primary_button_pressed,
                            selected = R.drawable.tp_clear_quartz_primary_button_selected,
                            disabled = R.drawable.tp_clear_quartz_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_progress_ring_default,
                            focused = R.drawable.tp_clear_quartz_progress_ring_focused,
                            pressed = R.drawable.tp_clear_quartz_progress_ring_pressed,
                            selected = R.drawable.tp_clear_quartz_progress_ring_selected,
                            disabled = R.drawable.tp_clear_quartz_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_secondary_button_default,
                            focused = R.drawable.tp_clear_quartz_secondary_button_focused,
                            pressed = R.drawable.tp_clear_quartz_secondary_button_pressed,
                            selected = R.drawable.tp_clear_quartz_secondary_button_selected,
                            disabled = R.drawable.tp_clear_quartz_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_slider_thumb_default,
                            focused = R.drawable.tp_clear_quartz_slider_thumb_focused,
                            pressed = R.drawable.tp_clear_quartz_slider_thumb_pressed,
                            selected = R.drawable.tp_clear_quartz_slider_thumb_selected,
                            disabled = R.drawable.tp_clear_quartz_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_slider_track_default,
                            focused = R.drawable.tp_clear_quartz_slider_track_focused,
                            pressed = R.drawable.tp_clear_quartz_slider_track_pressed,
                            selected = R.drawable.tp_clear_quartz_slider_track_selected,
                            disabled = R.drawable.tp_clear_quartz_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_text_field_default,
                            focused = R.drawable.tp_clear_quartz_text_field_focused,
                            pressed = R.drawable.tp_clear_quartz_text_field_pressed,
                            selected = R.drawable.tp_clear_quartz_text_field_selected,
                            disabled = R.drawable.tp_clear_quartz_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_clear_quartz_toggle_default,
                            focused = R.drawable.tp_clear_quartz_toggle_focused,
                            pressed = R.drawable.tp_clear_quartz_toggle_pressed,
                            selected = R.drawable.tp_clear_quartz_toggle_selected,
                            disabled = R.drawable.tp_clear_quartz_toggle_disabled,
                        ),
                ),
        )

    val azurite =
        ThemePack(
            slug = "azurite",
            name = "Azurite",
            stone = "azurite",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF04051A),
                    backgroundDeep = Color(0xFF010208),
                    surface = Color(0xFF111A55),
                    surfaceHigh = Color(0xFF1A297E),
                    primary = Color(0xFF5872F4),
                    secondary = Color(0xFF8B62DB),
                    accent = Color(0xFF91B6FF),
                    glow = Color(0xFF6E8CFF),
                    onBackground = Color(0xFFF7F7FF),
                    onSurface = Color(0xFFF7F7FF),
                    muted = Color(0xFFC4C8E7),
                    outline = Color(0xFF4F5C9B),
                    danger = Color(0xFFFF7F9A),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 88,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 230,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_azurite_material_tile,
                    glowOverlay = R.drawable.tp_azurite_glow_overlay,
                    refractionOverlay = R.drawable.tp_azurite_refraction_overlay,
                    ambientPortrait = R.drawable.tp_azurite_ambient_portrait,
                    ambientLandscape = R.drawable.tp_azurite_ambient_landscape,
                    ambientSquare = R.drawable.tp_azurite_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_azurite_click_soft,
                    confirm = R.raw.tp_azurite_confirm,
                    swoop = R.raw.tp_azurite_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_album_tile_default,
                            focused = R.drawable.tp_azurite_album_tile_focused,
                            pressed = R.drawable.tp_azurite_album_tile_pressed,
                            selected = R.drawable.tp_azurite_album_tile_selected,
                            disabled = R.drawable.tp_azurite_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_bottom_sheet_default,
                            focused = R.drawable.tp_azurite_bottom_sheet_focused,
                            pressed = R.drawable.tp_azurite_bottom_sheet_pressed,
                            selected = R.drawable.tp_azurite_bottom_sheet_selected,
                            disabled = R.drawable.tp_azurite_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_card_default,
                            focused = R.drawable.tp_azurite_card_focused,
                            pressed = R.drawable.tp_azurite_card_pressed,
                            selected = R.drawable.tp_azurite_card_selected,
                            disabled = R.drawable.tp_azurite_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_chip_default,
                            focused = R.drawable.tp_azurite_chip_focused,
                            pressed = R.drawable.tp_azurite_chip_pressed,
                            selected = R.drawable.tp_azurite_chip_selected,
                            disabled = R.drawable.tp_azurite_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_compact_button_default,
                            focused = R.drawable.tp_azurite_compact_button_focused,
                            pressed = R.drawable.tp_azurite_compact_button_pressed,
                            selected = R.drawable.tp_azurite_compact_button_selected,
                            disabled = R.drawable.tp_azurite_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_dialog_default,
                            focused = R.drawable.tp_azurite_dialog_focused,
                            pressed = R.drawable.tp_azurite_dialog_pressed,
                            selected = R.drawable.tp_azurite_dialog_selected,
                            disabled = R.drawable.tp_azurite_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_icon_button_default,
                            focused = R.drawable.tp_azurite_icon_button_focused,
                            pressed = R.drawable.tp_azurite_icon_button_pressed,
                            selected = R.drawable.tp_azurite_icon_button_selected,
                            disabled = R.drawable.tp_azurite_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_knob_default,
                            focused = R.drawable.tp_azurite_knob_focused,
                            pressed = R.drawable.tp_azurite_knob_pressed,
                            selected = R.drawable.tp_azurite_knob_selected,
                            disabled = R.drawable.tp_azurite_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_list_row_default,
                            focused = R.drawable.tp_azurite_list_row_focused,
                            pressed = R.drawable.tp_azurite_list_row_pressed,
                            selected = R.drawable.tp_azurite_list_row_selected,
                            disabled = R.drawable.tp_azurite_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_mini_player_default,
                            focused = R.drawable.tp_azurite_mini_player_focused,
                            pressed = R.drawable.tp_azurite_mini_player_pressed,
                            selected = R.drawable.tp_azurite_mini_player_selected,
                            disabled = R.drawable.tp_azurite_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_navigation_bar_default,
                            focused = R.drawable.tp_azurite_navigation_bar_focused,
                            pressed = R.drawable.tp_azurite_navigation_bar_pressed,
                            selected = R.drawable.tp_azurite_navigation_bar_selected,
                            disabled = R.drawable.tp_azurite_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_primary_button_default,
                            focused = R.drawable.tp_azurite_primary_button_focused,
                            pressed = R.drawable.tp_azurite_primary_button_pressed,
                            selected = R.drawable.tp_azurite_primary_button_selected,
                            disabled = R.drawable.tp_azurite_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_progress_ring_default,
                            focused = R.drawable.tp_azurite_progress_ring_focused,
                            pressed = R.drawable.tp_azurite_progress_ring_pressed,
                            selected = R.drawable.tp_azurite_progress_ring_selected,
                            disabled = R.drawable.tp_azurite_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_secondary_button_default,
                            focused = R.drawable.tp_azurite_secondary_button_focused,
                            pressed = R.drawable.tp_azurite_secondary_button_pressed,
                            selected = R.drawable.tp_azurite_secondary_button_selected,
                            disabled = R.drawable.tp_azurite_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_slider_thumb_default,
                            focused = R.drawable.tp_azurite_slider_thumb_focused,
                            pressed = R.drawable.tp_azurite_slider_thumb_pressed,
                            selected = R.drawable.tp_azurite_slider_thumb_selected,
                            disabled = R.drawable.tp_azurite_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_slider_track_default,
                            focused = R.drawable.tp_azurite_slider_track_focused,
                            pressed = R.drawable.tp_azurite_slider_track_pressed,
                            selected = R.drawable.tp_azurite_slider_track_selected,
                            disabled = R.drawable.tp_azurite_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_text_field_default,
                            focused = R.drawable.tp_azurite_text_field_focused,
                            pressed = R.drawable.tp_azurite_text_field_pressed,
                            selected = R.drawable.tp_azurite_text_field_selected,
                            disabled = R.drawable.tp_azurite_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_azurite_toggle_default,
                            focused = R.drawable.tp_azurite_toggle_focused,
                            pressed = R.drawable.tp_azurite_toggle_pressed,
                            selected = R.drawable.tp_azurite_toggle_selected,
                            disabled = R.drawable.tp_azurite_toggle_disabled,
                        ),
                ),
        )

    val firestone =
        ThemePack(
            slug = "firestone",
            name = "Firestone",
            stone = "fire agate / firestone",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF170705),
                    backgroundDeep = Color(0xFF080202),
                    surface = Color(0xFF431208),
                    surfaceHigh = Color(0xFF6E1E0D),
                    primary = Color(0xFFF47C32),
                    secondary = Color(0xFFC74625),
                    accent = Color(0xFFFFC06D),
                    glow = Color(0xFFFF8C3E),
                    onBackground = Color(0xFFFFF5EA),
                    onSurface = Color(0xFFFFF5EA),
                    muted = Color(0xFFD8B9A9),
                    outline = Color(0xFF8B4D35),
                    danger = Color(0xFFFF8D7C),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 82,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 210,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_firestone_material_tile,
                    glowOverlay = R.drawable.tp_firestone_glow_overlay,
                    refractionOverlay = R.drawable.tp_firestone_refraction_overlay,
                    ambientPortrait = R.drawable.tp_firestone_ambient_portrait,
                    ambientLandscape = R.drawable.tp_firestone_ambient_landscape,
                    ambientSquare = R.drawable.tp_firestone_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_firestone_click_soft,
                    confirm = R.raw.tp_firestone_confirm,
                    swoop = R.raw.tp_firestone_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_album_tile_default,
                            focused = R.drawable.tp_firestone_album_tile_focused,
                            pressed = R.drawable.tp_firestone_album_tile_pressed,
                            selected = R.drawable.tp_firestone_album_tile_selected,
                            disabled = R.drawable.tp_firestone_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_bottom_sheet_default,
                            focused = R.drawable.tp_firestone_bottom_sheet_focused,
                            pressed = R.drawable.tp_firestone_bottom_sheet_pressed,
                            selected = R.drawable.tp_firestone_bottom_sheet_selected,
                            disabled = R.drawable.tp_firestone_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_card_default,
                            focused = R.drawable.tp_firestone_card_focused,
                            pressed = R.drawable.tp_firestone_card_pressed,
                            selected = R.drawable.tp_firestone_card_selected,
                            disabled = R.drawable.tp_firestone_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_chip_default,
                            focused = R.drawable.tp_firestone_chip_focused,
                            pressed = R.drawable.tp_firestone_chip_pressed,
                            selected = R.drawable.tp_firestone_chip_selected,
                            disabled = R.drawable.tp_firestone_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_compact_button_default,
                            focused = R.drawable.tp_firestone_compact_button_focused,
                            pressed = R.drawable.tp_firestone_compact_button_pressed,
                            selected = R.drawable.tp_firestone_compact_button_selected,
                            disabled = R.drawable.tp_firestone_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_dialog_default,
                            focused = R.drawable.tp_firestone_dialog_focused,
                            pressed = R.drawable.tp_firestone_dialog_pressed,
                            selected = R.drawable.tp_firestone_dialog_selected,
                            disabled = R.drawable.tp_firestone_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_icon_button_default,
                            focused = R.drawable.tp_firestone_icon_button_focused,
                            pressed = R.drawable.tp_firestone_icon_button_pressed,
                            selected = R.drawable.tp_firestone_icon_button_selected,
                            disabled = R.drawable.tp_firestone_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_knob_default,
                            focused = R.drawable.tp_firestone_knob_focused,
                            pressed = R.drawable.tp_firestone_knob_pressed,
                            selected = R.drawable.tp_firestone_knob_selected,
                            disabled = R.drawable.tp_firestone_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_list_row_default,
                            focused = R.drawable.tp_firestone_list_row_focused,
                            pressed = R.drawable.tp_firestone_list_row_pressed,
                            selected = R.drawable.tp_firestone_list_row_selected,
                            disabled = R.drawable.tp_firestone_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_mini_player_default,
                            focused = R.drawable.tp_firestone_mini_player_focused,
                            pressed = R.drawable.tp_firestone_mini_player_pressed,
                            selected = R.drawable.tp_firestone_mini_player_selected,
                            disabled = R.drawable.tp_firestone_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_navigation_bar_default,
                            focused = R.drawable.tp_firestone_navigation_bar_focused,
                            pressed = R.drawable.tp_firestone_navigation_bar_pressed,
                            selected = R.drawable.tp_firestone_navigation_bar_selected,
                            disabled = R.drawable.tp_firestone_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_primary_button_default,
                            focused = R.drawable.tp_firestone_primary_button_focused,
                            pressed = R.drawable.tp_firestone_primary_button_pressed,
                            selected = R.drawable.tp_firestone_primary_button_selected,
                            disabled = R.drawable.tp_firestone_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_progress_ring_default,
                            focused = R.drawable.tp_firestone_progress_ring_focused,
                            pressed = R.drawable.tp_firestone_progress_ring_pressed,
                            selected = R.drawable.tp_firestone_progress_ring_selected,
                            disabled = R.drawable.tp_firestone_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_secondary_button_default,
                            focused = R.drawable.tp_firestone_secondary_button_focused,
                            pressed = R.drawable.tp_firestone_secondary_button_pressed,
                            selected = R.drawable.tp_firestone_secondary_button_selected,
                            disabled = R.drawable.tp_firestone_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_slider_thumb_default,
                            focused = R.drawable.tp_firestone_slider_thumb_focused,
                            pressed = R.drawable.tp_firestone_slider_thumb_pressed,
                            selected = R.drawable.tp_firestone_slider_thumb_selected,
                            disabled = R.drawable.tp_firestone_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_slider_track_default,
                            focused = R.drawable.tp_firestone_slider_track_focused,
                            pressed = R.drawable.tp_firestone_slider_track_pressed,
                            selected = R.drawable.tp_firestone_slider_track_selected,
                            disabled = R.drawable.tp_firestone_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_text_field_default,
                            focused = R.drawable.tp_firestone_text_field_focused,
                            pressed = R.drawable.tp_firestone_text_field_pressed,
                            selected = R.drawable.tp_firestone_text_field_selected,
                            disabled = R.drawable.tp_firestone_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_firestone_toggle_default,
                            focused = R.drawable.tp_firestone_toggle_focused,
                            pressed = R.drawable.tp_firestone_toggle_pressed,
                            selected = R.drawable.tp_firestone_toggle_selected,
                            disabled = R.drawable.tp_firestone_toggle_disabled,
                        ),
                ),
        )

    val kyanite =
        ThemePack(
            slug = "kyanite",
            name = "Kyanite",
            stone = "blue kyanite",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF07111D),
                    backgroundDeep = Color(0xFF03070D),
                    surface = Color(0xFF163653),
                    surfaceHigh = Color(0xFF24577E),
                    primary = Color(0xFF86B9E8),
                    secondary = Color(0xFF6E8DB9),
                    accent = Color(0xFFD2EBFF),
                    glow = Color(0xFF9DCCF4),
                    onBackground = Color(0xFFF6FBFF),
                    onSurface = Color(0xFFF6FBFF),
                    muted = Color(0xFFC2D2E0),
                    outline = Color(0xFF5D7893),
                    danger = Color(0xFFFF827D),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 92,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 235,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_kyanite_material_tile,
                    glowOverlay = R.drawable.tp_kyanite_glow_overlay,
                    refractionOverlay = R.drawable.tp_kyanite_refraction_overlay,
                    ambientPortrait = R.drawable.tp_kyanite_ambient_portrait,
                    ambientLandscape = R.drawable.tp_kyanite_ambient_landscape,
                    ambientSquare = R.drawable.tp_kyanite_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_kyanite_click_soft,
                    confirm = R.raw.tp_kyanite_confirm,
                    swoop = R.raw.tp_kyanite_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_album_tile_default,
                            focused = R.drawable.tp_kyanite_album_tile_focused,
                            pressed = R.drawable.tp_kyanite_album_tile_pressed,
                            selected = R.drawable.tp_kyanite_album_tile_selected,
                            disabled = R.drawable.tp_kyanite_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_bottom_sheet_default,
                            focused = R.drawable.tp_kyanite_bottom_sheet_focused,
                            pressed = R.drawable.tp_kyanite_bottom_sheet_pressed,
                            selected = R.drawable.tp_kyanite_bottom_sheet_selected,
                            disabled = R.drawable.tp_kyanite_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_card_default,
                            focused = R.drawable.tp_kyanite_card_focused,
                            pressed = R.drawable.tp_kyanite_card_pressed,
                            selected = R.drawable.tp_kyanite_card_selected,
                            disabled = R.drawable.tp_kyanite_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_chip_default,
                            focused = R.drawable.tp_kyanite_chip_focused,
                            pressed = R.drawable.tp_kyanite_chip_pressed,
                            selected = R.drawable.tp_kyanite_chip_selected,
                            disabled = R.drawable.tp_kyanite_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_compact_button_default,
                            focused = R.drawable.tp_kyanite_compact_button_focused,
                            pressed = R.drawable.tp_kyanite_compact_button_pressed,
                            selected = R.drawable.tp_kyanite_compact_button_selected,
                            disabled = R.drawable.tp_kyanite_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_dialog_default,
                            focused = R.drawable.tp_kyanite_dialog_focused,
                            pressed = R.drawable.tp_kyanite_dialog_pressed,
                            selected = R.drawable.tp_kyanite_dialog_selected,
                            disabled = R.drawable.tp_kyanite_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_icon_button_default,
                            focused = R.drawable.tp_kyanite_icon_button_focused,
                            pressed = R.drawable.tp_kyanite_icon_button_pressed,
                            selected = R.drawable.tp_kyanite_icon_button_selected,
                            disabled = R.drawable.tp_kyanite_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_knob_default,
                            focused = R.drawable.tp_kyanite_knob_focused,
                            pressed = R.drawable.tp_kyanite_knob_pressed,
                            selected = R.drawable.tp_kyanite_knob_selected,
                            disabled = R.drawable.tp_kyanite_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_list_row_default,
                            focused = R.drawable.tp_kyanite_list_row_focused,
                            pressed = R.drawable.tp_kyanite_list_row_pressed,
                            selected = R.drawable.tp_kyanite_list_row_selected,
                            disabled = R.drawable.tp_kyanite_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_mini_player_default,
                            focused = R.drawable.tp_kyanite_mini_player_focused,
                            pressed = R.drawable.tp_kyanite_mini_player_pressed,
                            selected = R.drawable.tp_kyanite_mini_player_selected,
                            disabled = R.drawable.tp_kyanite_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_navigation_bar_default,
                            focused = R.drawable.tp_kyanite_navigation_bar_focused,
                            pressed = R.drawable.tp_kyanite_navigation_bar_pressed,
                            selected = R.drawable.tp_kyanite_navigation_bar_selected,
                            disabled = R.drawable.tp_kyanite_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_primary_button_default,
                            focused = R.drawable.tp_kyanite_primary_button_focused,
                            pressed = R.drawable.tp_kyanite_primary_button_pressed,
                            selected = R.drawable.tp_kyanite_primary_button_selected,
                            disabled = R.drawable.tp_kyanite_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_progress_ring_default,
                            focused = R.drawable.tp_kyanite_progress_ring_focused,
                            pressed = R.drawable.tp_kyanite_progress_ring_pressed,
                            selected = R.drawable.tp_kyanite_progress_ring_selected,
                            disabled = R.drawable.tp_kyanite_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_secondary_button_default,
                            focused = R.drawable.tp_kyanite_secondary_button_focused,
                            pressed = R.drawable.tp_kyanite_secondary_button_pressed,
                            selected = R.drawable.tp_kyanite_secondary_button_selected,
                            disabled = R.drawable.tp_kyanite_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_slider_thumb_default,
                            focused = R.drawable.tp_kyanite_slider_thumb_focused,
                            pressed = R.drawable.tp_kyanite_slider_thumb_pressed,
                            selected = R.drawable.tp_kyanite_slider_thumb_selected,
                            disabled = R.drawable.tp_kyanite_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_slider_track_default,
                            focused = R.drawable.tp_kyanite_slider_track_focused,
                            pressed = R.drawable.tp_kyanite_slider_track_pressed,
                            selected = R.drawable.tp_kyanite_slider_track_selected,
                            disabled = R.drawable.tp_kyanite_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_text_field_default,
                            focused = R.drawable.tp_kyanite_text_field_focused,
                            pressed = R.drawable.tp_kyanite_text_field_pressed,
                            selected = R.drawable.tp_kyanite_text_field_selected,
                            disabled = R.drawable.tp_kyanite_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_kyanite_toggle_default,
                            focused = R.drawable.tp_kyanite_toggle_focused,
                            pressed = R.drawable.tp_kyanite_toggle_pressed,
                            selected = R.drawable.tp_kyanite_toggle_selected,
                            disabled = R.drawable.tp_kyanite_toggle_disabled,
                        ),
                ),
        )

    val malachite =
        ThemePack(
            slug = "malachite",
            name = "Malachite",
            stone = "malachite",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF03140D),
                    backgroundDeep = Color(0xFF010806),
                    surface = Color(0xFF07341F),
                    surfaceHigh = Color(0xFF0B5833),
                    primary = Color(0xFF4ED58F),
                    secondary = Color(0xFF77B997),
                    accent = Color(0xFFA7FFD0),
                    glow = Color(0xFF69EDA7),
                    onBackground = Color(0xFFF3FFF8),
                    onSurface = Color(0xFFF3FFF8),
                    muted = Color(0xFFBBD7C6),
                    outline = Color(0xFF39745A),
                    danger = Color(0xFFFF857D),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 100,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 250,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_malachite_material_tile,
                    glowOverlay = R.drawable.tp_malachite_glow_overlay,
                    refractionOverlay = R.drawable.tp_malachite_refraction_overlay,
                    ambientPortrait = R.drawable.tp_malachite_ambient_portrait,
                    ambientLandscape = R.drawable.tp_malachite_ambient_landscape,
                    ambientSquare = R.drawable.tp_malachite_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_malachite_click_soft,
                    confirm = R.raw.tp_malachite_confirm,
                    swoop = R.raw.tp_malachite_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_album_tile_default,
                            focused = R.drawable.tp_malachite_album_tile_focused,
                            pressed = R.drawable.tp_malachite_album_tile_pressed,
                            selected = R.drawable.tp_malachite_album_tile_selected,
                            disabled = R.drawable.tp_malachite_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_bottom_sheet_default,
                            focused = R.drawable.tp_malachite_bottom_sheet_focused,
                            pressed = R.drawable.tp_malachite_bottom_sheet_pressed,
                            selected = R.drawable.tp_malachite_bottom_sheet_selected,
                            disabled = R.drawable.tp_malachite_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_card_default,
                            focused = R.drawable.tp_malachite_card_focused,
                            pressed = R.drawable.tp_malachite_card_pressed,
                            selected = R.drawable.tp_malachite_card_selected,
                            disabled = R.drawable.tp_malachite_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_chip_default,
                            focused = R.drawable.tp_malachite_chip_focused,
                            pressed = R.drawable.tp_malachite_chip_pressed,
                            selected = R.drawable.tp_malachite_chip_selected,
                            disabled = R.drawable.tp_malachite_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_compact_button_default,
                            focused = R.drawable.tp_malachite_compact_button_focused,
                            pressed = R.drawable.tp_malachite_compact_button_pressed,
                            selected = R.drawable.tp_malachite_compact_button_selected,
                            disabled = R.drawable.tp_malachite_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_dialog_default,
                            focused = R.drawable.tp_malachite_dialog_focused,
                            pressed = R.drawable.tp_malachite_dialog_pressed,
                            selected = R.drawable.tp_malachite_dialog_selected,
                            disabled = R.drawable.tp_malachite_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_icon_button_default,
                            focused = R.drawable.tp_malachite_icon_button_focused,
                            pressed = R.drawable.tp_malachite_icon_button_pressed,
                            selected = R.drawable.tp_malachite_icon_button_selected,
                            disabled = R.drawable.tp_malachite_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_knob_default,
                            focused = R.drawable.tp_malachite_knob_focused,
                            pressed = R.drawable.tp_malachite_knob_pressed,
                            selected = R.drawable.tp_malachite_knob_selected,
                            disabled = R.drawable.tp_malachite_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_list_row_default,
                            focused = R.drawable.tp_malachite_list_row_focused,
                            pressed = R.drawable.tp_malachite_list_row_pressed,
                            selected = R.drawable.tp_malachite_list_row_selected,
                            disabled = R.drawable.tp_malachite_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_mini_player_default,
                            focused = R.drawable.tp_malachite_mini_player_focused,
                            pressed = R.drawable.tp_malachite_mini_player_pressed,
                            selected = R.drawable.tp_malachite_mini_player_selected,
                            disabled = R.drawable.tp_malachite_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_navigation_bar_default,
                            focused = R.drawable.tp_malachite_navigation_bar_focused,
                            pressed = R.drawable.tp_malachite_navigation_bar_pressed,
                            selected = R.drawable.tp_malachite_navigation_bar_selected,
                            disabled = R.drawable.tp_malachite_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_primary_button_default,
                            focused = R.drawable.tp_malachite_primary_button_focused,
                            pressed = R.drawable.tp_malachite_primary_button_pressed,
                            selected = R.drawable.tp_malachite_primary_button_selected,
                            disabled = R.drawable.tp_malachite_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_progress_ring_default,
                            focused = R.drawable.tp_malachite_progress_ring_focused,
                            pressed = R.drawable.tp_malachite_progress_ring_pressed,
                            selected = R.drawable.tp_malachite_progress_ring_selected,
                            disabled = R.drawable.tp_malachite_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_secondary_button_default,
                            focused = R.drawable.tp_malachite_secondary_button_focused,
                            pressed = R.drawable.tp_malachite_secondary_button_pressed,
                            selected = R.drawable.tp_malachite_secondary_button_selected,
                            disabled = R.drawable.tp_malachite_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_slider_thumb_default,
                            focused = R.drawable.tp_malachite_slider_thumb_focused,
                            pressed = R.drawable.tp_malachite_slider_thumb_pressed,
                            selected = R.drawable.tp_malachite_slider_thumb_selected,
                            disabled = R.drawable.tp_malachite_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_slider_track_default,
                            focused = R.drawable.tp_malachite_slider_track_focused,
                            pressed = R.drawable.tp_malachite_slider_track_pressed,
                            selected = R.drawable.tp_malachite_slider_track_selected,
                            disabled = R.drawable.tp_malachite_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_text_field_default,
                            focused = R.drawable.tp_malachite_text_field_focused,
                            pressed = R.drawable.tp_malachite_text_field_pressed,
                            selected = R.drawable.tp_malachite_text_field_selected,
                            disabled = R.drawable.tp_malachite_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_malachite_toggle_default,
                            focused = R.drawable.tp_malachite_toggle_focused,
                            pressed = R.drawable.tp_malachite_toggle_pressed,
                            selected = R.drawable.tp_malachite_toggle_selected,
                            disabled = R.drawable.tp_malachite_toggle_disabled,
                        ),
                ),
        )

    val mookaite =
        ThemePack(
            slug = "mookaite",
            name = "Mookaite",
            stone = "mookaite jasper",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF21100E),
                    backgroundDeep = Color(0xFF0E0706),
                    surface = Color(0xFF593024),
                    surfaceHigh = Color(0xFF7B4432),
                    primary = Color(0xFFE4A54A),
                    secondary = Color(0xFFC36B50),
                    accent = Color(0xFFFFD890),
                    glow = Color(0xFFF0B55C),
                    onBackground = Color(0xFFFFF8E8),
                    onSurface = Color(0xFFFFF8E8),
                    muted = Color(0xFFE1C7AF),
                    outline = Color(0xFF8A6048),
                    danger = Color(0xFFFF8A79),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 105,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 245,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_mookaite_material_tile,
                    glowOverlay = R.drawable.tp_mookaite_glow_overlay,
                    refractionOverlay = R.drawable.tp_mookaite_refraction_overlay,
                    ambientPortrait = R.drawable.tp_mookaite_ambient_portrait,
                    ambientLandscape = R.drawable.tp_mookaite_ambient_landscape,
                    ambientSquare = R.drawable.tp_mookaite_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_mookaite_click_soft,
                    confirm = R.raw.tp_mookaite_confirm,
                    swoop = R.raw.tp_mookaite_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_album_tile_default,
                            focused = R.drawable.tp_mookaite_album_tile_focused,
                            pressed = R.drawable.tp_mookaite_album_tile_pressed,
                            selected = R.drawable.tp_mookaite_album_tile_selected,
                            disabled = R.drawable.tp_mookaite_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_bottom_sheet_default,
                            focused = R.drawable.tp_mookaite_bottom_sheet_focused,
                            pressed = R.drawable.tp_mookaite_bottom_sheet_pressed,
                            selected = R.drawable.tp_mookaite_bottom_sheet_selected,
                            disabled = R.drawable.tp_mookaite_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_card_default,
                            focused = R.drawable.tp_mookaite_card_focused,
                            pressed = R.drawable.tp_mookaite_card_pressed,
                            selected = R.drawable.tp_mookaite_card_selected,
                            disabled = R.drawable.tp_mookaite_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_chip_default,
                            focused = R.drawable.tp_mookaite_chip_focused,
                            pressed = R.drawable.tp_mookaite_chip_pressed,
                            selected = R.drawable.tp_mookaite_chip_selected,
                            disabled = R.drawable.tp_mookaite_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_compact_button_default,
                            focused = R.drawable.tp_mookaite_compact_button_focused,
                            pressed = R.drawable.tp_mookaite_compact_button_pressed,
                            selected = R.drawable.tp_mookaite_compact_button_selected,
                            disabled = R.drawable.tp_mookaite_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_dialog_default,
                            focused = R.drawable.tp_mookaite_dialog_focused,
                            pressed = R.drawable.tp_mookaite_dialog_pressed,
                            selected = R.drawable.tp_mookaite_dialog_selected,
                            disabled = R.drawable.tp_mookaite_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_icon_button_default,
                            focused = R.drawable.tp_mookaite_icon_button_focused,
                            pressed = R.drawable.tp_mookaite_icon_button_pressed,
                            selected = R.drawable.tp_mookaite_icon_button_selected,
                            disabled = R.drawable.tp_mookaite_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_knob_default,
                            focused = R.drawable.tp_mookaite_knob_focused,
                            pressed = R.drawable.tp_mookaite_knob_pressed,
                            selected = R.drawable.tp_mookaite_knob_selected,
                            disabled = R.drawable.tp_mookaite_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_list_row_default,
                            focused = R.drawable.tp_mookaite_list_row_focused,
                            pressed = R.drawable.tp_mookaite_list_row_pressed,
                            selected = R.drawable.tp_mookaite_list_row_selected,
                            disabled = R.drawable.tp_mookaite_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_mini_player_default,
                            focused = R.drawable.tp_mookaite_mini_player_focused,
                            pressed = R.drawable.tp_mookaite_mini_player_pressed,
                            selected = R.drawable.tp_mookaite_mini_player_selected,
                            disabled = R.drawable.tp_mookaite_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_navigation_bar_default,
                            focused = R.drawable.tp_mookaite_navigation_bar_focused,
                            pressed = R.drawable.tp_mookaite_navigation_bar_pressed,
                            selected = R.drawable.tp_mookaite_navigation_bar_selected,
                            disabled = R.drawable.tp_mookaite_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_primary_button_default,
                            focused = R.drawable.tp_mookaite_primary_button_focused,
                            pressed = R.drawable.tp_mookaite_primary_button_pressed,
                            selected = R.drawable.tp_mookaite_primary_button_selected,
                            disabled = R.drawable.tp_mookaite_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_progress_ring_default,
                            focused = R.drawable.tp_mookaite_progress_ring_focused,
                            pressed = R.drawable.tp_mookaite_progress_ring_pressed,
                            selected = R.drawable.tp_mookaite_progress_ring_selected,
                            disabled = R.drawable.tp_mookaite_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_secondary_button_default,
                            focused = R.drawable.tp_mookaite_secondary_button_focused,
                            pressed = R.drawable.tp_mookaite_secondary_button_pressed,
                            selected = R.drawable.tp_mookaite_secondary_button_selected,
                            disabled = R.drawable.tp_mookaite_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_slider_thumb_default,
                            focused = R.drawable.tp_mookaite_slider_thumb_focused,
                            pressed = R.drawable.tp_mookaite_slider_thumb_pressed,
                            selected = R.drawable.tp_mookaite_slider_thumb_selected,
                            disabled = R.drawable.tp_mookaite_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_slider_track_default,
                            focused = R.drawable.tp_mookaite_slider_track_focused,
                            pressed = R.drawable.tp_mookaite_slider_track_pressed,
                            selected = R.drawable.tp_mookaite_slider_track_selected,
                            disabled = R.drawable.tp_mookaite_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_text_field_default,
                            focused = R.drawable.tp_mookaite_text_field_focused,
                            pressed = R.drawable.tp_mookaite_text_field_pressed,
                            selected = R.drawable.tp_mookaite_text_field_selected,
                            disabled = R.drawable.tp_mookaite_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_mookaite_toggle_default,
                            focused = R.drawable.tp_mookaite_toggle_focused,
                            pressed = R.drawable.tp_mookaite_toggle_pressed,
                            selected = R.drawable.tp_mookaite_toggle_selected,
                            disabled = R.drawable.tp_mookaite_toggle_disabled,
                        ),
                ),
        )

    val onyx =
        ThemePack(
            slug = "onyx",
            name = "Onyx",
            stone = "banded black onyx",
            isLight = false,
            palette =
                StonePalette(
                    background = Color(0xFF08090B),
                    backgroundDeep = Color(0xFF020304),
                    surface = Color(0xFF17191D),
                    surfaceHigh = Color(0xFF292D33),
                    primary = Color(0xFFD7B47A),
                    secondary = Color(0xFF9BA4AE),
                    accent = Color(0xFFF1D8A5),
                    glow = Color(0xFFF6D6A0),
                    onBackground = Color(0xFFF7F4EF),
                    onSurface = Color(0xFFF7F4EF),
                    muted = Color(0xFFBAB7B1),
                    outline = Color(0xFF6C7178),
                    danger = Color(0xFFFF8178),
                ),
            motion =
                StoneMotion(
                    pressDurationMs = 90,
                    pressScale = 0.972f,
                    innerGlowGain = 1.42f,
                    releaseDurationMs = 220,
                    focusDurationMs = 150,
                    edgeLightGain = 1.25f,
                    selectedDurationMs = 180,
                    reduceMotionCrossfadeMs = 120,
                ),
            material =
                StoneMaterial(
                    tile = R.drawable.tp_onyx_material_tile,
                    glowOverlay = R.drawable.tp_onyx_glow_overlay,
                    refractionOverlay = R.drawable.tp_onyx_refraction_overlay,
                    ambientPortrait = R.drawable.tp_onyx_ambient_portrait,
                    ambientLandscape = R.drawable.tp_onyx_ambient_landscape,
                    ambientSquare = R.drawable.tp_onyx_ambient_square,
                    backgroundOpacity = 0.44f,
                    surfaceOpacity = 0.72f,
                    disabledOpacity = 0.18f,
                ),
            sounds =
                StoneSounds(
                    click = R.raw.tp_onyx_click_soft,
                    confirm = R.raw.tp_onyx_confirm,
                    swoop = R.raw.tp_onyx_swoop,
                ),
            surfaces =
                mapOf(
                    StoneComponent.ALBUM_TILE to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_album_tile_default,
                            focused = R.drawable.tp_onyx_album_tile_focused,
                            pressed = R.drawable.tp_onyx_album_tile_pressed,
                            selected = R.drawable.tp_onyx_album_tile_selected,
                            disabled = R.drawable.tp_onyx_album_tile_disabled,
                        ),
                    StoneComponent.BOTTOM_SHEET to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_bottom_sheet_default,
                            focused = R.drawable.tp_onyx_bottom_sheet_focused,
                            pressed = R.drawable.tp_onyx_bottom_sheet_pressed,
                            selected = R.drawable.tp_onyx_bottom_sheet_selected,
                            disabled = R.drawable.tp_onyx_bottom_sheet_disabled,
                        ),
                    StoneComponent.CARD to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_card_default,
                            focused = R.drawable.tp_onyx_card_focused,
                            pressed = R.drawable.tp_onyx_card_pressed,
                            selected = R.drawable.tp_onyx_card_selected,
                            disabled = R.drawable.tp_onyx_card_disabled,
                        ),
                    StoneComponent.CHIP to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_chip_default,
                            focused = R.drawable.tp_onyx_chip_focused,
                            pressed = R.drawable.tp_onyx_chip_pressed,
                            selected = R.drawable.tp_onyx_chip_selected,
                            disabled = R.drawable.tp_onyx_chip_disabled,
                        ),
                    StoneComponent.COMPACT_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_compact_button_default,
                            focused = R.drawable.tp_onyx_compact_button_focused,
                            pressed = R.drawable.tp_onyx_compact_button_pressed,
                            selected = R.drawable.tp_onyx_compact_button_selected,
                            disabled = R.drawable.tp_onyx_compact_button_disabled,
                        ),
                    StoneComponent.DIALOG to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_dialog_default,
                            focused = R.drawable.tp_onyx_dialog_focused,
                            pressed = R.drawable.tp_onyx_dialog_pressed,
                            selected = R.drawable.tp_onyx_dialog_selected,
                            disabled = R.drawable.tp_onyx_dialog_disabled,
                        ),
                    StoneComponent.ICON_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_icon_button_default,
                            focused = R.drawable.tp_onyx_icon_button_focused,
                            pressed = R.drawable.tp_onyx_icon_button_pressed,
                            selected = R.drawable.tp_onyx_icon_button_selected,
                            disabled = R.drawable.tp_onyx_icon_button_disabled,
                        ),
                    StoneComponent.KNOB to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_knob_default,
                            focused = R.drawable.tp_onyx_knob_focused,
                            pressed = R.drawable.tp_onyx_knob_pressed,
                            selected = R.drawable.tp_onyx_knob_selected,
                            disabled = R.drawable.tp_onyx_knob_disabled,
                        ),
                    StoneComponent.LIST_ROW to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_list_row_default,
                            focused = R.drawable.tp_onyx_list_row_focused,
                            pressed = R.drawable.tp_onyx_list_row_pressed,
                            selected = R.drawable.tp_onyx_list_row_selected,
                            disabled = R.drawable.tp_onyx_list_row_disabled,
                        ),
                    StoneComponent.MINI_PLAYER to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_mini_player_default,
                            focused = R.drawable.tp_onyx_mini_player_focused,
                            pressed = R.drawable.tp_onyx_mini_player_pressed,
                            selected = R.drawable.tp_onyx_mini_player_selected,
                            disabled = R.drawable.tp_onyx_mini_player_disabled,
                        ),
                    StoneComponent.NAVIGATION_BAR to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_navigation_bar_default,
                            focused = R.drawable.tp_onyx_navigation_bar_focused,
                            pressed = R.drawable.tp_onyx_navigation_bar_pressed,
                            selected = R.drawable.tp_onyx_navigation_bar_selected,
                            disabled = R.drawable.tp_onyx_navigation_bar_disabled,
                        ),
                    StoneComponent.PRIMARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_primary_button_default,
                            focused = R.drawable.tp_onyx_primary_button_focused,
                            pressed = R.drawable.tp_onyx_primary_button_pressed,
                            selected = R.drawable.tp_onyx_primary_button_selected,
                            disabled = R.drawable.tp_onyx_primary_button_disabled,
                        ),
                    StoneComponent.PROGRESS_RING to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_progress_ring_default,
                            focused = R.drawable.tp_onyx_progress_ring_focused,
                            pressed = R.drawable.tp_onyx_progress_ring_pressed,
                            selected = R.drawable.tp_onyx_progress_ring_selected,
                            disabled = R.drawable.tp_onyx_progress_ring_disabled,
                        ),
                    StoneComponent.SECONDARY_BUTTON to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_secondary_button_default,
                            focused = R.drawable.tp_onyx_secondary_button_focused,
                            pressed = R.drawable.tp_onyx_secondary_button_pressed,
                            selected = R.drawable.tp_onyx_secondary_button_selected,
                            disabled = R.drawable.tp_onyx_secondary_button_disabled,
                        ),
                    StoneComponent.SLIDER_THUMB to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_slider_thumb_default,
                            focused = R.drawable.tp_onyx_slider_thumb_focused,
                            pressed = R.drawable.tp_onyx_slider_thumb_pressed,
                            selected = R.drawable.tp_onyx_slider_thumb_selected,
                            disabled = R.drawable.tp_onyx_slider_thumb_disabled,
                        ),
                    StoneComponent.SLIDER_TRACK to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_slider_track_default,
                            focused = R.drawable.tp_onyx_slider_track_focused,
                            pressed = R.drawable.tp_onyx_slider_track_pressed,
                            selected = R.drawable.tp_onyx_slider_track_selected,
                            disabled = R.drawable.tp_onyx_slider_track_disabled,
                        ),
                    StoneComponent.TEXT_FIELD to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_text_field_default,
                            focused = R.drawable.tp_onyx_text_field_focused,
                            pressed = R.drawable.tp_onyx_text_field_pressed,
                            selected = R.drawable.tp_onyx_text_field_selected,
                            disabled = R.drawable.tp_onyx_text_field_disabled,
                        ),
                    StoneComponent.TOGGLE to
                        StoneStateArt(
                            default = R.drawable.tp_onyx_toggle_default,
                            focused = R.drawable.tp_onyx_toggle_focused,
                            pressed = R.drawable.tp_onyx_toggle_pressed,
                            selected = R.drawable.tp_onyx_toggle_selected,
                            disabled = R.drawable.tp_onyx_toggle_disabled,
                        ),
                ),
        )

    val all: List<ThemePack> = listOf(lapisLazuli, sugilite, amethyst, clearQuartz, azurite, firestone, kyanite, malachite, mookaite, onyx)

    fun bySlug(slug: String?): ThemePack = all.firstOrNull { it.slug == slug } ?: all.first()
}
