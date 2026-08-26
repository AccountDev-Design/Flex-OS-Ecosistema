package com.flexos.flexphone.domain

import com.flexos.flexphone.protocol.MediaState
import com.flexos.flexphone.protocol.NotifPayload
import com.flexos.flexphone.protocol.RelayInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Estado del enlace, tal como lo ve la app. */
enum class LinkState {
    /** Bluetooth apagado, permisos sin conceder o sin adaptador. */
    UNAVAILABLE,
    /** Todo listo, pero el usuario no lo ha activado. */
    OFF,
    /** Anunciando; esperando a que Flex OS conecte. */
    ADVERTISING,
    /** Hay conexion GATT, aun sin sesion Flex Link. */
    CONNECTING,
    /** Mostrando codigo; falta confirmar en los dos lados. */
    PAIRING,
    /** Sesion abierta. SOLO aqui se puede decir "conectado". */
    READY,
    /** Fallo real; `error` explica cual. */
    ERROR,
}

enum class RelayState { OFF, STARTING, UP, ERROR, SUSPENDED }

/**
 * Estado observable de Flex Phone.
 *
 * UNA SOLA INSTANCIA, creada por [com.flexos.flexphone.FlexPhoneApp].
 * Se expone con `StateFlow` y NO con sondeo: nada en esta app
 * pregunta "¿ha cambiado algo?" en un bucle. Los cambios llegan por
 * eventos (Android avisa de la notificacion, del estado del enlace y
 * de la sesion multimedia) y la UI se recompone sola.
 *
 * REGLA QUE ATRAVIESA TODO EL FICHERO: ningun campo se rellena "por
 * si acaso". Si no hay dato real, el campo es `null` o el estado es
 * el que corresponde -- nunca un valor de relleno que la pantalla
 * pinte como si fuera cierto.
 */
class FlexPhoneState(
    @Volatile var settings: Settings = Settings(),
) {
    companion object {
        @Volatile var instance: FlexPhoneState? = null
    }

    private val _link = MutableStateFlow(LinkState.OFF)
    val link: StateFlow<LinkState> = _link.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    /** Codigo de emparejamiento en curso, o null. */
    private val _pairCode = MutableStateFlow<String?>(null)
    val pairCode: StateFlow<String?> = _pairCode.asStateFlow()

    private val _relay = MutableStateFlow(RelayState.OFF)
    val relay: StateFlow<RelayState> = _relay.asStateFlow()

    private val _relayInfo = MutableStateFlow<RelayInfo?>(null)
    val relayInfo: StateFlow<RelayInfo?> = _relayInfo.asStateFlow()

    private val _media = MutableStateFlow<MediaState?>(null)
    val media: StateFlow<MediaState?> = _media.asStateFlow()

    /** Contadores de diagnostico. NUNCA contenido de mensajes. */
    data class Diag(
        val sent: Long = 0, val received: Long = 0,
        val dropped: Long = 0, val badFrames: Long = 0,
        val reconnects: Long = 0,
        val repliesOk: Long = 0, val repliesFailed: Long = 0,
        val notificationsForwarded: Long = 0, val notificationsFiltered: Long = 0,
    )
    private val _diag = MutableStateFlow(Diag())
    val diag: StateFlow<Diag> = _diag.asStateFlow()

    /** Callback que instala el servicio del enlace para enviar. */
    @Volatile var sender: ((type: Int, payload: ByteArray) -> Boolean)? = null

    // ---------------------------------------------------------
    //  Transiciones del enlace
    // ---------------------------------------------------------
    fun setLink(s: LinkState, why: String? = null) {
        _link.value = s
        _error.value = why
        if (s != LinkState.PAIRING) _pairCode.value = null
        if (s != LinkState.READY) {
            // Al perder la sesion, lo que dependia de ella deja de ser
            // cierto: no se conserva un estado multimedia de hace un
            // rato como si siguiera sonando.
            _media.value = null
            if (_relay.value == RelayState.UP) _relay.value = RelayState.OFF
            _relayInfo.value = null
        }
    }

    fun setPairCode(code: String?) {
        _pairCode.value = code
        if (code != null) _link.value = LinkState.PAIRING
    }

    fun countReconnect() = _diag.update { it.copy(reconnects = it.reconnects + 1) }
    fun countSent() = _diag.update { it.copy(sent = it.sent + 1) }
    fun countReceived() = _diag.update { it.copy(received = it.received + 1) }
    fun countDropped() = _diag.update { it.copy(dropped = it.dropped + 1) }
    fun countBadFrame() = _diag.update { it.copy(badFrames = it.badFrames + 1) }
    fun countReply(ok: Boolean) = _diag.update {
        if (ok) it.copy(repliesOk = it.repliesOk + 1) else it.copy(repliesFailed = it.repliesFailed + 1)
    }

    // ---------------------------------------------------------
    //  Notificaciones
    // ---------------------------------------------------------
    fun onNotificationPosted(p: NotifPayload) {
        // Sin sesion no se manda nada -- ni se encola "para luego".
        // Una notificacion de hace media hora entregada al reconectar
        // seria ruido, no informacion.
        val s = sender
        if (s == null) {
            _diag.update { it.copy(notificationsFiltered = it.notificationsFiltered + 1) }
            return
        }
        if (s(com.flexos.flexphone.protocol.FlexLink.T_NOTIF_ADD, p.encode()))
            _diag.update { it.copy(notificationsForwarded = it.notificationsForwarded + 1) }
        else
            _diag.update { it.copy(dropped = it.dropped + 1) }
    }

    fun onNotificationRemoved(id: Long) {
        val s = sender ?: return
        val body = com.flexos.flexphone.protocol.PayloadWriter().u32(id).build()
        s(com.flexos.flexphone.protocol.FlexLink.T_NOTIF_REMOVE, body)
    }

    // ---------------------------------------------------------
    //  Multimedia y relay
    // ---------------------------------------------------------
    fun setMedia(m: MediaState?) {
        _media.value = m
        val s = sender ?: return
        if (m != null) s(com.flexos.flexphone.protocol.FlexLink.T_MEDIA_STATE, m.encode())
    }

    fun setRelay(state: RelayState, info: RelayInfo? = null) {
        _relay.value = state
        _relayInfo.value = info
        val s = sender ?: return
        // Se anuncia SIEMPRE el estado real, error incluido: el P4
        // tiene que poder mostrar "Android suspendio el relay" en vez
        // de quedarse esperando frames que no van a llegar.
        val payload = info ?: RelayInfo(
            ip = byteArrayOf(0, 0, 0, 0), port = 0, protoVer = 1, tls = false, caps = 0,
            error = when (state) {
                RelayState.SUSPENDED -> "Android suspendio el relay"
                RelayState.ERROR -> "el relay no pudo arrancar"
                RelayState.OFF -> "relay detenido"
                else -> ""
            },
        )
        s(com.flexos.flexphone.protocol.FlexLink.T_RELAY_INFO, payload.encode())
    }
}

/** Pequeña ayuda: `update` sobre un MutableStateFlow inmutable. */
private inline fun <T> MutableStateFlow<T>.update(block: (T) -> T) {
    while (true) {
        val cur = value
        val next = block(cur)
        if (compareAndSet(cur, next)) return
    }
}
