package com.flexos.flexphone.domain

/**
 * Ajustes del usuario. Inmutable a proposito: la UI observa un
 * `StateFlow<Settings>` y cada cambio produce una copia nueva, asi
 * que no hay forma de que una pantalla lea un estado a medio escribir.
 */
data class Settings(
    /** Paquetes cuyas notificaciones SI se envian. Vacio = ninguno. */
    val allowedPackages: Set<String> = emptySet(),
    /** Nunca mandar el cuerpo del mensaje, solo app y remitente. */
    val hideContent: Boolean = false,
    /** Suprimir el cuerpo de lo que parezca un codigo de un solo uso. */
    val hideSensitive: Boolean = true,
    /** Reanudar el enlace al reiniciar el telefono. Apagado de fabrica. */
    val startOnBoot: Boolean = false,
    /** Puerto del Browser Relay. 0 = que lo elija el sistema. */
    val relayPort: Int = 0,
    /** Cerrar la sesion del relay tras este tiempo sin actividad. */
    val relayIdleTimeoutMin: Int = 10,
    /** Tope de pestanas del relay. Cada una es un WebView vivo. */
    val relayMaxTabs: Int = 3,
    /** Calidad JPEG de partida del relay (20..90). */
    val relayQuality: Int = 62,
    /**
     * Identificador del Flex OS vinculado, o null.
     *
     * Es una COPIA para la interfaz. La verdad sobre si hay vinculo la
     * tiene [com.flexos.flexphone.storage.BondStore], que es quien
     * guarda la clave: aqui solo esta lo que se puede ensenar en
     * pantalla sin ser material sensible.
     */
    val flexosId: String? = null,
    val flexosName: String? = null,
    /**
     * Direccion fija de Flex OS. Normalmente vacia: Flex OS es quien
     * llama, no al reves. Queda para redes que bloquean la difusion.
     */
    val fixedHost: String = "",
) {
    fun isPackageAllowed(pkg: String): Boolean = allowedPackages.contains(pkg)
    val isPaired: Boolean get() = flexosId != null
}
