package com.flexos.flexphone.relay

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.flexos.flexphone.protocol.Fbp
import java.util.concurrent.atomic.AtomicLong

/**
 * Motor del Browser Relay: pestanas, captura y ritmo de envio.
 *
 * RITMO -- LO QUE SE PROMETE Y LO QUE NO
 * --------------------------------------
 * No se prometen 60 FPS. Una pagina remota renderizada en un
 * telefono, comprimida a JPEG y mandada por Wi-Fi a un ESP32 que
 * ademas tiene que DECODIFICARLA no da 60 FPS, y decir lo contrario
 * solo sirve para que el usuario piense que algo va mal.
 *
 * Lo que si se hace:
 *   · un tope realista de fotogramas por segundo (FPS_MAX);
 *   · bajar el ritmo hasta FPS_IDLE cuando la pagina no cambia, que
 *     es la mayor parte del tiempo de lectura;
 *   · subir al maximo mientras hay interaccion tactil, que es cuando
 *     de verdad se nota;
 *   · NO acumular fotogramas: si el P4 va atrasado, se descarta el
 *     viejo y se manda el MAS RECIENTE. Un fotograma antiguo entregado
 *     tarde es peor que ninguno.
 *
 * SESIONES Y PESTANAS -- LO QUE SOBREVIVE A UNA RECONEXION
 * --------------------------------------------------------
 *   · Una conexion nueva AUTENTICADA sustituye a la anterior ("la mas
 *     nueva gana"): si el P4 se reinicio o su Wi-Fi se corto, el socket
 *     viejo puede seguir pareciendo vivo un rato, y antes eso dejaba
 *     fuera la reconexion hasta que caducara.
 *   · Las pestanas NO se destruyen al irse el cliente: se conservan un
 *     periodo de gracia (el ajuste "liberar memoria tras inactividad").
 *     Si el P4 vuelve antes, encuentra la MISMA pagina, con su scroll,
 *     sus formularios y su historial. Pasado ese tiempo se sueltan (cada
 *     WebView es memoria que Android puede querer); el servidor sigue
 *     escuchando.
 *   · Si el proceso de render de una pestana muere, se rehace en la
 *     misma direccion en vez de dejar un WebView muerto en pantalla.
 *   · Un solo hilo de bombeo: cada arranque lleva su generacion y el
 *     hilo de una generacion vieja termina solo.
 */
object RelayEngine {

    private const val TAG = "FlexPhone/Engine"

    /**
     * BUILD DEL RELAY. Se anuncia al P4 al final del id de sesion (".r6"), sin
     * tocar el formato del WELCOME: un Flex OS que no lo conoce lo ignora y uno que
     * si lo conoce sabe, sin adivinar, que Flex Phone hay instalado en el telefono.
     *
     *   r6  el viewport CSS es el que pidio el P4 (escala MEDIDA en la pagina, no
     *       supuesta, y sin el "viewport ancho" del motor, que encogia las paginas
     *       sin <meta viewport> a 980 px), el desplazamiento es el del DOCUMENTO y no
     *       mueve la superficie, y la captura no arrastra barras ni resplandores.
     *
     * Subelo cuando cambie algo que el P4 deba exigir (ver FLEXBR_RELAY_MIN_BUILD
     * en FlexOS_Browser.h). Antes de r6 la web salia ampliada con la densidad del
     * telefono y al desplazar se movia toda la superficie.
     */
    const val BUILD = 6

    /** Bits de capacidades del WELCOME. 0x01 navegacion, 0x02 toque (los de siempre). */
    private const val CAP_NAV_TOUCH = 0x03L
    /** El viewport CSS pedido es el real y el desplazamiento es del documento (build >= 6). */
    private const val CAP_CSS_VIEWPORT = 0x10L

    /** Tope realista. Con una pagina estatica se baja mucho de aqui. */
    private const val FPS_MAX = 12
    /** Ritmo en reposo: la pagina no cambia, no hay nada que mandar. */
    private const val FPS_IDLE = 2
    /** Ventana en la que se considera que "hay interaccion". */
    private const val ACTIVE_WINDOW_MS = 1_500L

    private var ctx: Context? = null
    private val main = Handler(Looper.getMainLooper())

    // Las pestanas solo se tocan en el hilo PRINCIPAL. El hilo del bombeo lee
    // la activa por [current], que se publica cada vez que cambia.
    private val tabs = LinkedHashMap<Int, RelayTab>()
    private var activeTab = 1
    private var nextTabId = 1
    @Volatile private var current: RelayTab? = null
    /** Tiempo que se conservan las pestanas sin cliente (ver SESIONES Y PESTANAS). */
    @Volatile private var keepTabsMs = 10 * 60_000L
    private val releaseTabs = Runnable { destroyTabs() }

    @Volatile private var session: RelaySession? = null
    @Volatile private var viewportW = 480
    @Volatile private var viewportH = 800
    @Volatile private var quality = 62
    @Volatile private var scalePct = 100
    @Volatile private var maxTabsAllowed = 3
    @Volatile private var maxFrame = 192 * 1024
    @Volatile private var lastInteractionMs = 0L
    @Volatile private var forceKeyframe = true
    @Volatile private var suspended = false

    private val frameId = AtomicLong(1)
    private val pumpGen = java.util.concurrent.atomic.AtomicInteger(0)
    private var sessionIdStr = ""

    fun init(context: Context, maxTabs: Int, jpegQuality: Int, keepTabsMin: Int = 10) {
        ctx = context.applicationContext
        maxTabsAllowed = maxTabs.coerceIn(1, 6)
        quality = jpegQuality.coerceIn(20, 90)
        keepTabsMs = keepTabsMin.coerceIn(1, 120) * 60_000L
        sessionIdStr = Fbp.tagSessionId("flexphone-" + System.currentTimeMillis().toString(16), BUILD)
    }

    /** ¿Hay un Flex OS conectado y autenticado ahora mismo? */
    fun hasClient(): Boolean = session != null

    fun tabCount(): Int = tabs.size
    fun sessionId(): String = sessionIdStr
    fun capabilities(): Long = CAP_NAV_TOUCH or CAP_CSS_VIEWPORT   // navegacion + toque + viewport CSS exacto
    fun maxTabs(requested: Int): Int = minOf(requested.coerceAtLeast(1), maxTabsAllowed)
    fun maxFrameBytes(requested: Long): Long = minOf(requested, maxFrame.toLong())

    /** Aplica el HELLO y devuelve el viewport CONCEDIDO. */
    fun configure(hello: Fbp.Hello): Pair<Int, Int> {
        viewportW = hello.viewportW.coerceIn(120, 1920)
        viewportH = hello.viewportH.coerceIn(120, 1920)
        quality = hello.quality.coerceIn(20, 90)
        maxFrame = hello.maxFrameBytes.coerceAtMost(512L * 1024).toInt()
        val w = viewportW; val h = viewportH
        main.post { tabs.values.forEach { it.resize(w, h) } }
        return viewportW to viewportH
    }

    /**
     * VIEWPORT: las MISMAS reglas que el servicio de Ubuntu/PC (session.js).
     * Un ancho o alto por debajo de 120 significa "el tamano no cambia" -- no
     * "hazlo de 120x120", que es lo que salia antes cuando el P4 mandaba 0x0
     * al cambiar un ajuste de calidad. Calidad 0 = se queda; escala fuera de
     * 25..100 = se queda.
     */
    fun viewport(v: Fbp.Viewport) {
        if (v.w >= 120 && v.h >= 120) {
            viewportW = v.w.coerceAtMost(1920)
            viewportH = v.h.coerceAtMost(1920)
        }
        if (v.quality > 0) quality = v.quality.coerceIn(20, 90)
        if (v.scalePct in 25..100) scalePct = v.scalePct
        forceKeyframe = true
        val w = viewportW; val h = viewportH
        main.post {
            tabs.values.forEach { it.resize(w, h) }
            current?.markDirty()
        }
    }

    // ---------------------------------------------------------
    //  Sesion
    // ---------------------------------------------------------
    fun attach(s: RelaySession) {
        val old = session
        session = s
        // La mas nueva gana: la anterior se cierra (ver SESIONES Y PESTANAS).
        if (old != null && old !== s) runCatching { old.close(1001, "sustituida por una conexion nueva") }
        forceKeyframe = true
        suspended = false
        main.post {
            main.removeCallbacks(releaseTabs)          // vuelve a tiempo: nada se suelta
            ensureTab(activeTab)
            publishCurrent()
            // El P4 recien llegado no sabe nada: lista de pestanas y estado
            // de la activa ya, sin esperar a que la pagina cambie.
            pushTabs()
            tabs[activeTab]?.let { onTabState(it) }
            current?.markDirty()
        }
        startPump()
    }

    /**
     * Se fue la sesion [s]. Solo cuenta si es la ACTUAL: una sustituida que
     * termina despues no puede dejar sin cliente a la nueva.
     */
    fun detach(s: RelaySession) {
        if (session !== s) return
        session = null
        stopPump()
        // Las pestanas se CONSERVAN un tiempo (ver SESIONES Y PESTANAS).
        main.post {
            main.removeCallbacks(releaseTabs)
            main.postDelayed(releaseTabs, keepTabsMs)
        }
    }

    /** Parada del relay (el servicio se va): todo fuera, ya. */
    fun shutdown() {
        val old = session
        session = null
        stopPump()
        if (old != null) runCatching { old.close(1001, "relay detenido") }
        main.post {
            main.removeCallbacks(releaseTabs)
            destroyTabs()
        }
    }

    private fun destroyTabs() {
        tabs.values.forEach { it.destroy() }
        tabs.clear()
        current = null
    }

    /** Marca el relay como suspendido por Android. */
    fun markSuspended(why: String) {
        if (suspended) return
        suspended = true
        session?.sendError(0, 503, why)
    }

    /** Android ya no lo restringe: el bombeo vuelve a su ritmo. */
    fun clearSuspended() {
        if (!suspended) return
        suspended = false
        forceKeyframe = true
    }

    // ---------------------------------------------------------
    //  Pestanas
    // ---------------------------------------------------------
    private fun ensureTab(id: Int): RelayTab? {
        val c = ctx ?: return null
        tabs[id]?.let { return it }
        if (tabs.size >= maxTabsAllowed) {
            session?.sendError(id, 507, "el telefono solo admite $maxTabsAllowed pestanas")
            return null
        }
        val t = RelayTab(c, id, viewportW, viewportH) { tab -> onTabState(tab) }
        tabs[id] = t
        return t
    }

    /** Publica la pestana activa para el hilo del bombeo. Hilo principal. */
    private fun publishCurrent() { current = tabs[activeTab] }

    private fun tab(id: Int): RelayTab? = tabs[id]

    fun newTab() = main.post {
        if (tabs.size >= maxTabsAllowed) {
            session?.sendError(0, 507, "el telefono solo admite $maxTabsAllowed pestanas")
            return@post
        }
        nextTabId++
        activeTab = nextTabId
        ensureTab(nextTabId)
        publishCurrent()
        pushTabs()
    }

    fun closeTab(id: Int) = main.post {
        tabs.remove(id)?.destroy()
        if (activeTab == id) activeTab = tabs.keys.firstOrNull() ?: 1
        publishCurrent()
        pushTabs()
    }

    fun selectTab(id: Int) = main.post {
        activeTab = id
        ensureTab(id)
        publishCurrent()
        forceKeyframe = true
        pushTabs()
    }

    private fun pushTabs() {
        val s = session ?: return
        s.sendBinary(
            Fbp.tabs(1, activeTab, tabs.values.map { it.id to it.title })
        )
    }

    // ---------------------------------------------------------
    //  Ordenes
    // ---------------------------------------------------------
    fun navigate(ch: Int, url: String) = main.post {
        val t = ensureTab(if (ch == 0) activeTab else ch) ?: return@post
        publishCurrent()
        touch(); t.navigate(url)
    }
    fun back(ch: Int) = main.post { tab(ch)?.back(); touch() }
    fun forward(ch: Int) = main.post { tab(ch)?.forward(); touch() }
    fun reload(ch: Int) = main.post { tab(ch)?.reload(); touch() }
    fun stopLoading(ch: Int) = main.post { tab(ch)?.stopLoading() }

    fun pointer(ch: Int, action: Int, x: Int, y: Int) {
        touch(); tab(ch)?.pointer(action, x, y)
    }
    fun scroll(ch: Int, dx: Int, dy: Int) {
        touch(); tab(ch)?.scroll(dx, dy)
    }
    fun key(ch: Int, action: Int, code: Int, text: String) {
        touch(); tab(ch)?.key(action, code, text)
    }
    fun requestKeyframe(ch: Int) {
        forceKeyframe = true
        tab(ch)?.markDirty()
    }

    private fun touch() { lastInteractionMs = System.currentTimeMillis() }

    private fun onTabState(t: RelayTab) {
        // El proceso de render de la pestana murio: se rehace AQUI, en la misma
        // direccion, en vez de dejar un WebView muerto. El P4 recibe el error
        // (abajo) y despues la pagina vuelve sola.
        if (t.renderGone && tabs[t.id] === t) {
            val url = t.lastUrl()
            main.post {
                if (tabs[t.id] !== t) return@post
                tabs.remove(t.id)
                t.destroy()
                val n = ensureTab(t.id)
                publishCurrent()
                if (n != null && url.isNotEmpty() && url != "about:blank") n.navigate(url)
                forceKeyframe = true
            }
        }
        val s = session ?: return
        var flags = 0
        if (t.loading) flags = flags or Fbp.ST_LOADING
        if (t.canGoBack()) flags = flags or Fbp.ST_CAN_BACK
        if (t.canGoForward()) flags = flags or Fbp.ST_CAN_FORWARD
        if (t.url.startsWith("https://")) flags = flags or Fbp.ST_SECURE
        s.sendState(t.id, flags, t.progress, t.title, t.url)
        // Un error de carga se manda TAL CUAL: el P4 lo enseria en vez
        // de quedarse con la pagina anterior como si nada.
        t.lastError?.let { s.sendError(t.id, 502, it) }
    }

    // ---------------------------------------------------------
    //  Bombeo de fotogramas
    // ---------------------------------------------------------
    private fun startPump() {
        // UNA generacion por arranque. Antes, un detach() seguido de un attach()
        // rapido (una reconexion) volvia a poner `pumping` a true antes de que
        // el hilo viejo lo viera en false: quedaban DOS hilos capturando el
        // mismo bitmap y mandando fotogramas duplicados.
        val gen = pumpGen.incrementAndGet()
        Thread({
            try {
                while (pumpGen.get() == gen) {
                    val s = session ?: break
                    if (suspended) { Thread.sleep(500); continue }
                    val active = System.currentTimeMillis() - lastInteractionMs < ACTIVE_WINDOW_MS
                    val fps = if (active) FPS_MAX else FPS_IDLE
                    val periodMs = (1000 / fps).toLong()
                    val t0 = System.currentTimeMillis()

                    val t = current
                    if (t != null) {
                        val force = forceKeyframe
                        val cap = t.capture(quality, scalePct, force)
                        if (cap != null && pumpGen.get() == gen) {
                            if (cap.jpeg.size > maxFrame) {
                                // No cabe en lo que el P4 dijo que puede
                                // recibir: se baja la calidad en vez de
                                // mandar algo que va a descartar.
                                quality = (quality - 10).coerceAtLeast(20)
                                Log.i(TAG, "fotograma demasiado grande; calidad -> $quality")
                            } else {
                                forceKeyframe = false
                                // El rectangulo es el que la captura CUBRE (ver
                                // RelayTab.Capture), en px CSS del viewport.
                                val ok = s.sendFrame(
                                    channel = t.id, x = 0, y = 0,
                                    w = cap.viewW, h = cap.viewH,
                                    keyframe = force, last = true,
                                    frameId = frameId.getAndIncrement(), image = cap.jpeg,
                                )
                                if (!ok) break
                            }
                        }
                    }
                    // Ritmo por TIEMPO TRANSCURRIDO: si la captura tardo
                    // mas que el periodo, se sigue de inmediato en vez de
                    // acumular retraso.
                    val spent = System.currentTimeMillis() - t0
                    val wait = periodMs - spent
                    if (wait > 0) Thread.sleep(wait)
                }
            } catch (e: InterruptedException) {
                // parada: nada que hacer
            } catch (e: Exception) {
                // Un fallo aqui NO puede tumbar la app (hilo propio = proceso
                // muerto). Se registra el tipo y el bombeo de esta generacion
                // termina; la siguiente conexion arranca otro.
                Log.w(TAG, "bombeo detenido: ${e.javaClass.simpleName}")
            }
        }, "flex-relay-pump").apply { isDaemon = true; start() }
    }

    private fun stopPump() {
        pumpGen.incrementAndGet()          // el hilo actual sale en su siguiente vuelta
    }
}
