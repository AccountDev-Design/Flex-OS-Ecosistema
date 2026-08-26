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
 */
object RelayEngine {

    private const val TAG = "FlexPhone/Engine"

    /** Tope realista. Con una pagina estatica se baja mucho de aqui. */
    private const val FPS_MAX = 12
    /** Ritmo en reposo: la pagina no cambia, no hay nada que mandar. */
    private const val FPS_IDLE = 2
    /** Ventana en la que se considera que "hay interaccion". */
    private const val ACTIVE_WINDOW_MS = 1_500L

    private var ctx: Context? = null
    private val main = Handler(Looper.getMainLooper())

    private val tabs = LinkedHashMap<Int, RelayTab>()
    private var activeTab = 1
    private var nextTabId = 1

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
    private var pump: Thread? = null
    @Volatile private var pumping = false
    private var sessionIdStr = ""

    fun init(context: Context, maxTabs: Int, jpegQuality: Int) {
        ctx = context.applicationContext
        maxTabsAllowed = maxTabs.coerceIn(1, 6)
        quality = jpegQuality.coerceIn(20, 90)
        sessionIdStr = "flexphone-" + System.currentTimeMillis().toString(16)
    }

    fun tabCount(): Int = tabs.size
    fun sessionId(): String = sessionIdStr
    fun capabilities(): Long = 0x03            // navegacion + entrada tactil
    fun maxTabs(requested: Int): Int = minOf(requested.coerceAtLeast(1), maxTabsAllowed)
    fun maxFrameBytes(requested: Long): Long = minOf(requested, maxFrame.toLong())

    /** Aplica el HELLO y devuelve el viewport CONCEDIDO. */
    fun configure(hello: Fbp.Hello): Pair<Int, Int> {
        viewportW = hello.viewportW.coerceIn(120, 1920)
        viewportH = hello.viewportH.coerceIn(120, 1920)
        quality = hello.quality.coerceIn(20, 90)
        maxFrame = hello.maxFrameBytes.coerceAtMost(512L * 1024).toInt()
        main.post { tabs.values.forEach { it.resize(viewportW, viewportH) } }
        return viewportW to viewportH
    }

    fun viewport(v: Fbp.Viewport) {
        viewportW = v.w.coerceIn(120, 1920)
        viewportH = v.h.coerceIn(120, 1920)
        quality = v.quality.coerceIn(20, 90)
        scalePct = v.scalePct.coerceIn(50, 100)
        forceKeyframe = true
        main.post { tabs.values.forEach { it.resize(viewportW, viewportH) } }
    }

    // ---------------------------------------------------------
    //  Sesion
    // ---------------------------------------------------------
    fun attach(s: RelaySession) {
        session = s
        forceKeyframe = true
        suspended = false
        main.post { ensureTab(activeTab) }
        startPump()
    }

    fun detach() {
        session = null
        stopPump()
        // Las pestanas se sueltan al irse el cliente: cada WebView vivo
        // es memoria que Android puede querer, y volver a crearlo es
        // barato comparado con que mate el proceso entero.
        main.post {
            tabs.values.forEach { it.destroy() }
            tabs.clear()
        }
    }

    /** Marca el relay como suspendido por Android. */
    fun markSuspended(why: String) {
        suspended = true
        session?.sendError(0, 503, why)
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

    private fun tab(id: Int): RelayTab? = tabs[id]

    fun newTab() = main.post {
        if (tabs.size >= maxTabsAllowed) {
            session?.sendError(0, 507, "el telefono solo admite $maxTabsAllowed pestanas")
            return@post
        }
        nextTabId++
        activeTab = nextTabId
        ensureTab(nextTabId)
        pushTabs()
    }

    fun closeTab(id: Int) = main.post {
        tabs.remove(id)?.destroy()
        if (activeTab == id) activeTab = tabs.keys.firstOrNull() ?: 1
        pushTabs()
    }

    fun selectTab(id: Int) = main.post {
        activeTab = id
        ensureTab(id)
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
        if (pumping) return
        pumping = true
        pump = Thread({
            while (pumping) {
                val s = session ?: break
                if (suspended) { Thread.sleep(500); continue }
                val active = System.currentTimeMillis() - lastInteractionMs < ACTIVE_WINDOW_MS
                val fps = if (active) FPS_MAX else FPS_IDLE
                val periodMs = (1000 / fps).toLong()
                val t0 = System.currentTimeMillis()

                val t = tabs[activeTab]
                if (t != null) {
                    val force = forceKeyframe
                    val img = t.capture(quality, scalePct, force)
                    if (img != null) {
                        if (img.size > maxFrame) {
                            // No cabe en lo que el P4 dijo que puede
                            // recibir: se baja la calidad en vez de
                            // mandar algo que va a descartar.
                            quality = (quality - 10).coerceAtLeast(20)
                            Log.i(TAG, "fotograma demasiado grande; calidad -> $quality")
                        } else {
                            forceKeyframe = false
                            val ok = s.sendFrame(
                                channel = t.id, x = 0, y = 0,
                                w = viewportW, h = viewportH,
                                keyframe = force, last = true,
                                frameId = frameId.getAndIncrement(), image = img,
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
        }, "flex-relay-pump").apply { isDaemon = true; start() }
    }

    private fun stopPump() {
        pumping = false
        pump = null
    }
}
