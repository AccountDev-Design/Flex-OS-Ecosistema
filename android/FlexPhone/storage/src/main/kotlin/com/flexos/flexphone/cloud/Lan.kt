package com.flexos.flexphone.cloud

import java.net.Inet4Address
import java.net.InetAddress

/**
 * Preguntas sobre la red local. NUNCA se compara una IP con otra para decidir que "es
 * la misma red": dos dispositivos de una LAN tienen IPs DISTINTAS (192.168.1.4 y
 * 192.168.1.2) y estan en la misma subred cuando coinciden bajo la mascara REAL de la
 * interfaz (el prefijo que da Android, no un /24 supuesto).
 */
object Lan {

    fun sameSubnet(a: InetAddress, b: InetAddress, prefixLength: Int): Boolean {
        if (a !is Inet4Address || b !is Inet4Address || prefixLength !in 0..32) return false
        val x = a.address
        val y = b.address
        var bits = prefixLength
        for (i in 0 until 4) {
            val m = if (bits >= 8) 0xFF else if (bits <= 0) 0 else (0xFF shl (8 - bits)) and 0xFF
            if ((x[i].toInt() and m) != (y[i].toInt() and m)) return false
            bits -= 8
        }
        return true
    }

    /**
     * Pista para "no llego a Flex OS", segun lo que se SABE de la red del telefono.
     * null = no hay nada concreto que anadir (misma subred: lo que queda son el router o
     * Flex OS, y el mensaje general lo cubre).
     */
    fun hintFor(p4Ip: String, phoneIp: InetAddress?, prefixLength: Int): String? {
        if (phoneIp == null) return "El teléfono no está conectado a una red Wi‑Fi."
        val p4 = runCatching { InetAddress.getByName(p4Ip) }.getOrNull() ?: return null
        if (sameSubnet(phoneIp, p4, prefixLength)) return null
        return "El teléfono está en la red de ${phoneIp.hostAddress}/$prefixLength y Flex OS ($p4Ip) fuera de ella: " +
            "conecta los dos al mismo router o red Wi‑Fi."
    }
}
