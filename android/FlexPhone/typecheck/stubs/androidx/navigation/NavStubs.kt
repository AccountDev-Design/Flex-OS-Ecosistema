@file:Suppress("unused", "UNUSED_PARAMETER")
package androidx.navigation

class NavDestination { val route: String? = null }
class NavGraph { val id: Int = 0 }
class PopUpToBuilder { var inclusive: Boolean = false }
class NavOptionsBuilder {
    var launchSingleTop: Boolean = false
    fun popUpTo(id: Int, builder: PopUpToBuilder.() -> Unit = {}) {}
    fun popUpTo(route: String, builder: PopUpToBuilder.() -> Unit = {}) {}
}
class NavGraphBuilder

open class NavController {
    open fun popBackStack(): Boolean = false
    open fun navigate(route: String) {}
    open fun navigate(route: String, builder: NavOptionsBuilder.() -> Unit) {}
    val currentDestination: NavDestination? = null
    val graph: NavGraph = NavGraph()
}
