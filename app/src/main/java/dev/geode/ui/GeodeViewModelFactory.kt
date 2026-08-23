package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.hilt.navigation.compose.hiltViewModel
import androidx.lifecycle.ViewModel

/**
 * Resolves one of Geode's five ViewModels from the Hilt graph.
 *
 * Kept as a named helper rather than calling `hiltViewModel()` at every site so the screens have a
 * single seam to change if the resolution strategy moves again.
 */
@Composable
internal inline fun <reified T : ViewModel> geodeViewModel(): T = hiltViewModel()
