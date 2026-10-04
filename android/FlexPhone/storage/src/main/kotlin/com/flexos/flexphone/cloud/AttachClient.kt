package com.flexos.flexphone.cloud

import java.io.IOException
import java.net.InetAddress

/**
 * EMPAREJAR ESTE TELEFONO CON UN FLEX OS (lado telefono).
 *
 * Lo arranca la app cuando la web de Flex Storage (servida por el P4) le pasa
 * `flexstorage://attach?h=<ip:puerto>&o=<oferta>`:
 *
 *  1. ECDH P-256: se manda la clave publica junto con la oferta (de un solo uso,
 *     que el P4 genero para la sesion web del navegador) y el puerto en el que
 *     este telefono sirve Flex Cloud.
 *  2. Los dos lados derivan la misma clave y el mismo CODIGO DE VERIFICACION de
 *     6 cifras; la app lo ensena ([onSas]) y en la pantalla del P4 aparece el
 *     mismo con "Permitir / Rechazar".
 *  3. Se pregunta cada 1,5 s demostrando que tenemos la clave; cuando el P4 lo
 *     aprueba contesta con SU prueba, y solo entonces se guarda el
 *     emparejamiento (en Android, envuelto con el Keystore).
 *
 * Si este telefono ya estaba emparejado con ESE Flex OS, demuestra que conserva
 * la clave anterior y el P4 lo da por bueno sin volver a preguntar (vale para
 * "cambio la IP del telefono: escanea el QR otra vez").
 */
class AttachClient(
    private val phone: PhoneInfo,
    private val serverPort: Int,
    private val repo: PairingRepo,
    private val poster: FormPoster = LocalHttpPoster(),
    private val now: () -> Long = System::currentTimeMillis,
    private val sleep: (Long) -> Unit = { Thread.sleep(it) },
    /** Para una IP de Flex OS que no responde: una pista sobre la red del telefono (o null). Ver [lanHint]. */
    private val lanHint: (p4Ip: String) -> String? = { null },
) {
    sealed class Result {
        data class Paired(val p4Name: String, val p4Id: String) : Result()
        data class Failed(val message: String, val detail: String = "") : Result()
    }

    companion object {
        const val POLL_MS = 1500L
        const val TOTAL_MS = 120_000L
        const val CONNECT_ATTEMPTS = 3
        const val CONNECT_RETRY_MS = 1_000L

        /** "a.b.c.d:puerto" de la red LOCAL, o null. Nada de nombres ni de IPs publicas. */
        fun parseHost(h: String?): Pair<String, Int>? {
            if (h == null || h.length > 32) return null
            val m = Regex("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3}):(\\d{1,5})$").matchEntire(h) ?: return null
            val o = (1..4).map { m.groupValues[it].toInt() }
            if (o.any { it > 255 }) return null
            val port = m.groupValues[5].toInt()
            if (port !in 1..65535) return null
            val addr = InetAddress.getByAddress(byteArrayOf(o[0].toByte(), o[1].toByte(), o[2].toByte(), o[3].toByte()))
            if (!Http.isLocal(addr)) return null
            return "${o[0]}.${o[1]}.${o[2]}.${o[3]}" to port
        }

        fun validOffer(o: String?): Boolean = o != null && o.length == 32 && o.all { it in '0'..'9' || it in 'a'..'f' }
    }

    fun attach(hostPort: String, offer: String, onSas: (sas: String, p4Name: String) -> Unit, cancelled: () -> Boolean = { false }): Result {
        val hp = parseHost(hostPort) ?: return Result.Failed("La dirección de Flex OS no es de la red local.")
        if (!validOffer(offer)) return Result.Failed("El enlace de Flex Storage no es válido. Vuelve a pulsarlo en la web.")
        val base = "http://${hp.first}:${hp.second}"
        val ecdh = StorageCrypto.Ecdh.generate()
        val form = linkedMapOf(
            "offer" to offer, "pid" to phone.phoneId, "name" to phone.name.take(47), "model" to phone.model.take(23),
            "port" to serverPort.toString(), "pub" to StorageCrypto.hex(ecdh.publicPoint()),
        )
        val old = repo.load()
        if (old != null) {
            form["kp4"] = old.p4Id
            form["known"] = StorageCrypto.hex(StorageCrypto.knownProof(old.key, offer))
        }
        // Un fallo ANTES de que el pedido llegue (sin ruta, puerto cerrado) se repite: no gasta la
        // oferta de un solo uso, y una Wi-Fi que acaba de despertar tarda un par de segundos.
        // Uno que ocurre DESPUES de mandarlo no se repite: la oferta pudo gastarse.
        var first: Pair<Int, String>? = null
        var attempt = 0
        while (first == null) {
            if (cancelled()) return Result.Failed("Cancelado.")
            try { first = poster.post("$base/api/fs/phone/pair", form, 15_000) } catch (e: IOException) {
                attempt++
                if (e is HttpStageException && e.requestNotDelivered && attempt < CONNECT_ATTEMPTS) { sleep(CONNECT_RETRY_MS); continue }
                return explain(e, hp)
            }
        }
        val (st, txt) = first
        val j = try { Json.parseObject(txt) } catch (e: JsonException) { emptyMap() }
        if (st != 202 && st != 200) return Result.Failed((j["error"] as? String) ?: "Flex OS rechazó la conexión ($st).")
        val pairId = (j["pairId"] as? String)?.takeIf { Regex("^[a-f0-9]{16,32}$").matches(it) } ?: return Result.Failed("Respuesta inesperada de Flex OS.")
        val p4Id = (j["p4id"] as? String)?.takeIf { it.isNotEmpty() && it.length <= 40 } ?: return Result.Failed("Respuesta inesperada de Flex OS.")
        val p4Name = (j["p4name"] as? String)?.take(47) ?: "Flex OS"
        val peer = StorageCrypto.unhex(j["pub"] as? String, StorageCrypto.POINT_SIZE) ?: return Result.Failed("Respuesta inesperada de Flex OS.")
        val z = try { ecdh.shared(peer) } catch (e: Exception) { return Result.Failed("La clave de Flex OS no es válida.") }
        val key = StorageCrypto.deriveKey(z, offer, p4Id, phone.phoneId)
        if (j["approve"].jsonLong() != 0L) onSas(StorageCrypto.sas(key), p4Name)
        val proof = StorageCrypto.hex(StorageCrypto.phoneProof(key, pairId))
        val until = now() + TOTAL_MS
        var lastNetErr: IOException? = null
        while (now() < until) {
            if (cancelled()) return Result.Failed("Cancelado.")
            val (ps, ptxt) = try { poster.post("$base/api/fs/phone/pair/$pairId", mapOf("proof" to proof), 10_000) } catch (e: IOException) {
                lastNetErr = e
                sleep(POLL_MS); continue                     // un hipo de la Wi-Fi no tira el emparejamiento
            }
            lastNetErr = null
            val pj = try { Json.parseObject(ptxt) } catch (e: JsonException) { emptyMap() }
            when {
                ps == 200 && pj["state"] == "approved" -> {
                    val p4Proof = StorageCrypto.unhex(pj["proof"] as? String, 32)
                    if (p4Proof == null || !StorageCrypto.equalsConstantTime(p4Proof, StorageCrypto.p4Proof(key, pairId)))
                        return Result.Failed("Flex OS no demostró tener la misma clave. No se ha guardado nada.")
                    repo.save(P4Pairing(p4Id, p4Name, key, "${hp.first}:${hp.second}", now()))
                    return Result.Paired(p4Name, p4Id)
                }
                ps == 200 -> sleep(POLL_MS)                   // pendiente: esperando a que lo aprueben en el P4
                ps == 403 -> return Result.Failed((pj["error"] as? String) ?: "Flex OS rechazó la conexión.")
                ps == 410 -> return Result.Failed("Se acabó el tiempo para aprobarlo en Flex OS. Vuelve a intentarlo.")
                ps == 404 -> return Result.Failed("Flex OS ya no tiene esta solicitud (¿se reinició?). Vuelve a intentarlo.")
                else -> sleep(POLL_MS)
            }
        }
        // Si lo ULTIMO que paso fue un fallo de red, el motivo no es "no lo aprobaron": se perdio la comunicacion.
        lastNetErr?.let { return explain(it, hp, lost = true) }
        return Result.Failed("Se acabó el tiempo para aprobarlo en Flex OS. Vuelve a intentarlo.")
    }

    /** Dice QUE paso fallo (y no un unico "no se pudo hablar con Flex OS" para todo). */
    private fun explain(e: IOException, hp: Pair<String, Int>, lost: Boolean = false): Result.Failed {
        val t = "${hp.first}:${hp.second}"
        val detail = e.message ?: e.javaClass.simpleName
        val stage = (e as? HttpStageException)?.stage
        val lead = if (lost) "Se perdió la comunicación con Flex OS mientras esperaba su aprobación. " else ""
        val msg = when (stage) {
            HttpStage.UNREACHABLE ->
                lead + "El teléfono no llega a Flex OS ($t). " +
                    (lanHint(hp.first) ?: "Comprueba que los dos usan la misma red Wi‑Fi y que el router no aísla a los clientes entre sí (aislamiento de clientes o red de invitados).")
            HttpStage.REFUSED ->
                lead + "El teléfono llegó a la dirección de Flex OS ($t), pero su servidor web no aceptó la conexión. " +
                    "Vuelve a abrir la web de Flex OS y pulsa «Activar Flex Cloud» otra vez."
            HttpStage.NO_REPLY ->
                lead + "Flex OS ($t) aceptó la conexión pero no contestó a tiempo. Vuelve a intentarlo; si se repite, reinicia Flex OS."
            HttpStage.BAD_REPLY ->
                lead + "Lo que hay en $t no contestó como Flex OS. Comprueba que la dirección del enlace es la de tu Flex OS."
            HttpStage.NOT_LOCAL -> "La dirección de Flex OS no es de la red local."
            null -> lead + "No se pudo hablar con Flex OS ($t)."
        }
        return Result.Failed(msg, detail)
    }
}
