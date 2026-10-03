package com.flexos.flexphone.link

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import java.net.Inet4Address

/**
 * LA DIRECCION WI-FI DE ESTE TELEFONO, preguntada a Android y no adivinada.
 *
 * Antes el enlace (y el anuncio del relay) la sacaban de
 * `NetworkInterface.getNetworkInterfaces()` y se quedaban con la PRIMERA IPv4 de
 * cualquier interfaz que estuviera arriba. Un telefono con datos moviles tiene
 * `rmnet_data0` (10.x o 100.64.x), a veces `swlan0` (punto de acceso de Samsung,
 * 192.168.x.1) y `wlan0`, y el ORDEN en que Java las enumera no esta
 * garantizado: el relay podia anunciarle al P4 una direccion por la que el P4 no
 * llega, sin ningun error que lo explicara.
 *
 * Aqui se pregunta a ConnectivityManager por la red con transporte Wi-Fi y se
 * lee SU LinkProperties: la direccion y el nombre de la interfaz que de verdad
 * usa. Es la misma fuente que ya usa Flex Cloud (FlexCloudPhone.wifiAddress).
 */
object NetAddress {

    /** Lo que se sabe de la Wi-Fi ahora mismo. null = no hay red Wi-Fi con IPv4 privada. */
    data class WifiInfo(val address: Inet4Address, val interfaceName: String?)

    @Suppress("DEPRECATION")
    fun wifi(ctx: Context): WifiInfo? {
        val cm = ctx.applicationContext.getSystemService(ConnectivityManager::class.java) ?: return null
        for (n in cm.allNetworks) {
            val caps = cm.getNetworkCapabilities(n) ?: continue
            if (!caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) continue
            val lp = cm.getLinkProperties(n) ?: continue
            for (la in lp.linkAddresses) {
                val a = la.address
                if (a is Inet4Address && !a.isLoopbackAddress && a.isSiteLocalAddress) {
                    return WifiInfo(a, lp.interfaceName)
                }
            }
        }
        return null
    }

    fun wifiIpv4(ctx: Context): String? = wifi(ctx)?.address?.hostAddress
    fun wifiInterface(ctx: Context): String? = wifi(ctx)?.interfaceName
}
