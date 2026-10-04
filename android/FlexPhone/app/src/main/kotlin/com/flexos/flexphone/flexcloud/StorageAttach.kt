package com.flexos.flexphone.flexcloud

import android.content.Context
import android.net.Uri
import com.flexos.flexphone.cloud.AttachClient
import com.flexos.flexphone.cloud.Lan
import com.flexos.flexphone.cloud.LocalHttpPoster
import com.flexos.flexphone.link.NetAddress
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * EL EMPAREJAMIENTO DE FLEX CLOUD, FUERA DE LA ACTIVIDAD.
 *
 * Antes todo vivia dentro de `StorageAttachActivity`: el hilo que habla con el P4,
 * el estado ("comprobando", "este es el codigo"...) y la bandera `cancelled`, que
 * `onDestroy` ponia a true. Una Activity se destruye por muchisimas razones que
 * no son "la persona cancelo" -- un cambio de configuracion que el manifiesto no
 * cubre (tamano de letra, densidad, idioma, modo multiventana, DeX), irse al
 * navegador a mirar la web de Flex OS y que Android recicle una actividad
 * `excludeFromRecents`, o pulsar Atras sin querer --, y cada una de ellas:
 *
 *   1. cancelaba el emparejamiento que el P4 estaba a punto de aprobar,
 *   2. deshacia lo hecho (`undo`: paraba el servicio y desactivaba Flex Cloud), y
 *   3. al volver a abrir la app no quedaba rastro de nada, solo "Esperando a Flex OS".
 *
 * Eso es "durante un segundo parece perderse la conexion, la pantalla del codigo
 * desaparece y vuelve Esperando a Flex OS", y de ahi que NUNCA terminase de
 * emparejarse aunque el codigo ya hubiese llegado al P4.
 *
 * Ahora el emparejamiento es de la APP (este objeto, un proceso, un solo hilo) y
 * la Actividad -- o la pantalla Flex Cloud -- solo lo OBSERVA por [state]. Se
 * cancela UNICAMENTE con [cancel] (el boton Cancelar). Que una pantalla aparezca
 * y desaparezca no toca el protocolo, y el resultado (`Paired`) se guarda en el
 * Keystore en cuanto el P4 lo aprueba, sin esperar a que haya pantalla.
 */
object StorageAttach {

    sealed class UiState {
        /** No hay nada en curso. */
        object Idle : UiState()
        /** Hay una oferta valida del P4 y se le pregunta a la persona. */
        data class Confirm(val p4Ip: String, val quotaGb: Int) : UiState()
        /** Se esta hablando con el P4. */
        object Working : UiState()
        /** El codigo de 6 cifras: el mismo que ensena el P4 y que se aprueba alli. */
        data class Code(val sas: String, val p4Name: String) : UiState()
        /** El P4 aprobo y demostro la clave: el emparejamiento esta GUARDADO. */
        data class Done(val p4Name: String) : UiState()
        /** No se pudo; [message] dice por que. */
        data class Failed(val title: String, val message: String) : UiState()
    }

    private val _state = MutableStateFlow<UiState>(UiState.Idle)
    val state: StateFlow<UiState> = _state

    private val lock = Any()
    private var host: String? = null
    private var offer: String? = null
    private var hadPairing = false
    @Volatile private var cancelled = false
    private var worker: Thread? = null

    /** Mismo enlace que el que ya esta en curso: la actividad se recreo, no hay oferta nueva. */
    private fun sameOffer(h: String?, o: String?) = h == host && o == offer

    /**
     * Llega un enlace `flexstorage://attach?h=<ip:puerto>&o=<oferta>`. Devuelve true
     * si es valido y se esta preguntando a la persona (o ya estaba en curso).
     */
    fun offer(ctx: Context, link: Uri?): Boolean = synchronized(lock) {
        val h = link?.getQueryParameter("h")
        val o = link?.getQueryParameter("o")
        // La actividad se recrea (giro, letra, idioma...) con el MISMO enlace: no es
        // una oferta nueva y NO se toca lo que esta en marcha.
        if (sameOffer(h, o) && _state.value !is UiState.Idle && _state.value !is UiState.Failed) return@synchronized true
        val ok = link != null && link.scheme == "flexstorage" && link.host == "attach" &&
            AttachClient.parseHost(h) != null && AttachClient.validOffer(o)
        if (!ok) {
            _state.value = UiState.Failed(
                "Enlace no válido",
                "Este enlace de Flex Storage no es válido o no es de tu red local. Vuelve a pulsar «Activar Flex Cloud» en la web de Flex OS.",
            )
            return@synchronized false
        }
        // Una oferta NUEVA mientras habia otra en marcha: se sustituye (la vieja se cancela).
        if (_state.value is UiState.Working || _state.value is UiState.Code) cancelLocked(ctx, undo = false)
        host = h; offer = o
        hadPairing = FlexCloudPhone.repo(ctx).isPaired()
        _state.value = UiState.Confirm(AttachClient.parseHost(h)!!.first, FlexCloudPhone.quotaGb(ctx))
        true
    }

    /** Lo mismo con el enlace pegado a mano (el respaldo cuando el navegador no abre la app). */
    fun offerText(ctx: Context, text: String): Boolean {
        val t = text.trim()
        return offer(ctx, if (t.startsWith("flexstorage://")) Uri.parse(t) else null)
    }

    /** La persona acepto: se arranca el servidor de Flex Cloud y se empareja con el P4. */
    fun begin(ctx: Context) {
        val app = ctx.applicationContext
        val h: String; val o: String
        synchronized(lock) {
            if (_state.value !is UiState.Confirm) return
            h = host ?: return; o = offer ?: return
            cancelled = false
            _state.value = UiState.Working
        }
        FlexCloudPhone.setEnabled(app, true)
        FlexStorageService.start(app)
        val t = Thread({ run(app, h, o) }, "flexcloud-attach")
        synchronized(lock) { worker = t }
        t.isDaemon = true
        t.start()
    }

    private fun run(app: Context, h: String, o: String) {
        // El servidor tiene que estar escuchando: Flex OS abrira sesion con el en
        // cuanto se apruebe el emparejamiento.
        var port = 0
        for (i in 0 until 40) {
            port = FlexCloudPhone.port()
            if (port > 0 || cancelled) break
            try { Thread.sleep(150) } catch (e: InterruptedException) { return }
        }
        if (cancelled) { undo(app); return }
        if (port <= 0) {
            val why = FlexCloudPhone.status.value.error ?: "No se pudo arrancar Flex Cloud en este teléfono."
            undo(app)
            fail("No se pudo activar", why)
            return
        }
        val r = try {
            AttachClient(
                FlexCloudPhone.phoneInfo(app), port, FlexCloudPhone.repo(app),
                poster = LocalHttpPoster(newSocket = { NetAddress.wifiSocket(app) }),
                lanHint = { p4Ip ->
                    val w = NetAddress.wifi(app)
                    Lan.hintFor(p4Ip, w?.address, w?.prefixLength ?: 24)
                },
            ).attach(h, o, onSas = { sas, p4 ->
                synchronized(lock) { if (!cancelled) _state.value = UiState.Code(sas, p4) }
            }, cancelled = { cancelled })
        } catch (e: Exception) {
            AttachClient.Result.Failed("No se pudo guardar el emparejamiento en este teléfono.")
        }
        FlexCloudPhone.refresh(app)
        when (r) {
            // EL EMPAREJAMIENTO YA ESTA GUARDADO (AttachClient lo guarda con commit antes de
            // devolver Paired). Lo unico que falta es contarlo.
            is AttachClient.Result.Paired -> synchronized(lock) { _state.value = UiState.Done(r.p4Name) }
            is AttachClient.Result.Failed -> {
                if (cancelled) { undo(app); return }
                undo(app)
                android.util.Log.w("FlexPhone/FlexCloud", "emparejamiento fallido: ${r.detail.ifEmpty { r.message }}")
                fail("No se activó", if (r.detail.isEmpty()) r.message else r.message + "\n\nDetalle técnico: " + r.detail)
            }
        }
    }

    private fun fail(title: String, message: String) {
        synchronized(lock) { if (!cancelled) _state.value = UiState.Failed(title, message) }
    }

    /** Si no habia emparejamiento antes y no se consiguio, no se deja un servidor escuchando. */
    private fun undo(app: Context) {
        if (!hadPairing && !FlexCloudPhone.repo(app).isPaired()) {
            FlexCloudPhone.setEnabled(app, false)
            FlexStorageService.stop(app)
        }
    }

    /** La persona pulso Cancelar. Es lo UNICO que corta un emparejamiento en curso. */
    fun cancel(ctx: Context) = synchronized(lock) { cancelLocked(ctx.applicationContext, undo = true) }

    private fun cancelLocked(app: Context, undo: Boolean) {
        cancelled = true
        worker?.interrupt()
        worker = null
        _state.value = UiState.Idle
        if (undo) undo(app)
    }

    /** La persona vio el resultado (o cerro la pregunta sin aceptar): vuelve a no haber nada. */
    fun dismiss() = synchronized(lock) {
        val s = _state.value
        if (s is UiState.Done || s is UiState.Failed || s is UiState.Confirm) {
            _state.value = UiState.Idle
            host = null; offer = null
        }
    }
}
