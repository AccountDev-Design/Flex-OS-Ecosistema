@file:Suppress("unused", "UNUSED_PARAMETER")
package androidx.navigation.compose

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.navigation.NavController
import androidx.navigation.NavGraphBuilder

@Composable
fun rememberNavController(): NavController = NavController()

@Composable
fun NavHost(
    navController: NavController,
    startDestination: String,
    modifier: Modifier = Modifier,
    builder: NavGraphBuilder.() -> Unit,
) {}

fun NavGraphBuilder.composable(route: String, content: @Composable (Any) -> Unit) {}
