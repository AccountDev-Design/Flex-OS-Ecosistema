package com.flexos.flexphone.cloud

/**
 * El Flex OS con el que este telefono comparte Flex Cloud.
 *
 * [key] es la clave del emparejamiento (32 bytes, ECDH + HMAC). No viaja nunca
 * por la red despues del emparejamiento: solo se usa para firmar retos. En
 * Android se guarda envuelta con una clave del Keystore (ver `:app`); aqui solo
 * se define QUE se guarda.
 */
class P4Pairing(
    val p4Id: String,
    val p4Name: String,
    val key: ByteArray,
    val host: String,             // "192.168.1.50:8080" (para volver a emparejar o avisar)
    val pairedAt: Long,
)

/** Donde vive el emparejamiento (Android: Keystore; pruebas: memoria). */
interface PairingRepo {
    fun load(): P4Pairing?
    fun save(p: P4Pairing)
    fun clear()
}

class MemoryPairingRepo(private var p: P4Pairing? = null) : PairingRepo {
    @Synchronized override fun load() = p
    @Synchronized override fun save(p: P4Pairing) { this.p = p }
    @Synchronized override fun clear() { p = null }
}

/**
 * Retos y sesiones de Flex Storage (lado telefono).
 *
 *  · Un RETO vale 60 s y una sola vez. Como mucho 16 vivos.
 *  · Una SESION es un token aleatorio de 24 bytes (48 hex) LIGADO a la IP del P4
 *    que la abrio. Caduca a los 30 min sin uso y a las 12 h pase lo que pase.
 *    Como mucho 4 (la mas vieja cede su sitio).
 *  · Los fallos de autenticacion cuentan por IP: 10 en un minuto y esa IP espera
 *    un minuto entero sin que se mire ni su token.
 *
 * La comparacion de tokens recorre TODAS las sesiones en tiempo constante.
 */
class SessionManager(private val now: () -> Long = System::currentTimeMillis) {
    class Session(val token: String, val ip: String, val createdAt: Long, var lastMs: Long)

    private val challenges = LinkedHashMap<String, Long>()
    private val sessions = ArrayList<Session>()
    private val fails = HashMap<String, LongArray>()        // ip -> [cuenta, inicio de la ventana, bloqueada hasta]

    companion object {
        const val CHALLENGE_TTL_MS = 60_000L
        const val MAX_CHALLENGES = 16
        const val IDLE_MS = 30L * 60_000
        const val MAX_AGE_MS = 12L * 3600_000
        const val MAX_SESSIONS = 4
        const val FAIL_MAX = 10
        const val FAIL_WINDOW_MS = 60_000L
        const val BLOCK_MS = 60_000L
    }

    @Synchronized
    fun newChallenge(): String {
        val t = now()
        challenges.entries.removeIf { it.value <= t }
        while (challenges.size >= MAX_CHALLENGES) challenges.remove(challenges.keys.first())
        val n = StorageCrypto.hex(StorageCrypto.random(16))
        challenges[n] = t + CHALLENGE_TTL_MS
        return n
    }

    /** true si el reto existia y no habia caducado. Se gasta al mirarlo. */
    @Synchronized
    fun consumeChallenge(nonce: String): Boolean {
        val exp = challenges.remove(nonce) ?: return false
        return exp > now()
    }

    @Synchronized
    fun create(ip: String): Session {
        val t = now()
        sessions.removeIf { expired(it, t) }
        while (sessions.size >= MAX_SESSIONS) sessions.removeAt(sessions.indices.minByOrNull { sessions[it].lastMs }!!)
        val s = Session(StorageCrypto.hex(StorageCrypto.random(24)), ip, t, t)
        sessions.add(s)
        return s
    }

    private fun expired(s: Session, t: Long) = t - s.lastMs > IDLE_MS || t - s.createdAt > MAX_AGE_MS

    /** La sesion del token, si vale desde esa IP. Renueva su ultimo uso. */
    @Synchronized
    fun validate(token: String?, ip: String): Session? {
        if (token == null || token.length != 48) return null
        val t = now()
        val want = token.toByteArray(Charsets.US_ASCII)
        var found: Session? = null
        for (s in sessions) {
            val eq = StorageCrypto.equalsConstantTime(s.token.toByteArray(Charsets.US_ASCII), want)
            if (eq && !expired(s, t) && s.ip == ip) found = s
        }
        found?.lastMs = t
        return found
    }

    @Synchronized
    fun dropAll() { sessions.clear(); challenges.clear() }

    @Synchronized
    fun count(): Int { val t = now(); sessions.removeIf { expired(it, t) }; return sessions.size }

    // ------------------------------------------------------------ limitador
    @Synchronized
    fun blocked(ip: String): Boolean {
        val f = fails[ip] ?: return false
        return f[2] > now()
    }

    @Synchronized
    fun fail(ip: String) {
        val t = now()
        val f = fails.getOrPut(ip) { longArrayOf(0, t, 0) }
        if (t - f[1] > FAIL_WINDOW_MS) { f[0] = 0; f[1] = t }
        f[0]++
        if (f[0] >= FAIL_MAX) { f[2] = t + BLOCK_MS; f[0] = 0; f[1] = t }
        if (fails.size > 1024) fails.entries.removeIf { t - it.value[1] > FAIL_WINDOW_MS && it.value[2] <= t }
    }
}
