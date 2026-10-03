@file:Suppress("unused")
package androidx.compose.ui.res

import androidx.compose.runtime.Composable

/** En Android lo da androidx.compose.ui; en escritorio, otra biblioteca. Solo la firma. */
@Composable
fun stringResource(id: Int): String = ""
