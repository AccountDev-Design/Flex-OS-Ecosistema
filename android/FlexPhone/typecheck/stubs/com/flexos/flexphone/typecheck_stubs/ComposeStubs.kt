// Dobles de lo exclusivo de Android que la pantalla Compose usa y que Compose
// de escritorio no trae. Solo para comprobar tipos: firmas como las de verdad.
@file:Suppress("unused")
package androidx.navigation

open class NavController {
    open fun popBackStack(): Boolean = false
    open fun navigate(route: String) {}
}
