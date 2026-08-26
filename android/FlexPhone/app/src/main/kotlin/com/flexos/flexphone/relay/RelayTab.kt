package com.flexos.flexphone.relay

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.MotionEvent
import android.view.View
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import java.io.ByteArrayOutputStream

/**
 * Una pestana del Browser Relay: un WebView que se dibuja fuera de
 * pantalla y del que se sacan fotogramas JPEG.
 *
 * SOBRE LA PANTALLA APAGADA -- LO QUE DE VERDAD PASA
 * ---------------------------------------------------
 * Un WebView NO esta pensado para funcionar sin ventana visible.
 * Cuando la pantalla se apaga, Android puede:
 *   · dejar de entregar vsync, con lo que las animaciones y algunos
 *     `requestAnimationFrame` se paran;
 *   · aplicar restricciones de segundo plano al proceso;
 *   · matar el proceso entero por memoria.
 *
 * Lo que SI se puede hacer, y es lo que hace esta clase:
 *   · mantener el WebView adjunto a la jerarquia de una ventana
 *     invisible pero VALIDA, para que siga midiendo y componiendo;
 *   · forzar el dibujo con `draw(Canvas)` sobre un bitmap propio, que
 *     no depende del compositor de pantalla;
 *   · pedir un WakeLock PARCIAL mientras hay sesion, para que la CPU
 *     no entre en suspension profunda.
 *
 * Lo que NO se puede prometer: que esto aguante indefinidamente en
 * TODOS los telefonos. Varios fabricantes matan procesos en segundo
 * plano por politica propia. Por eso el servicio detecta la muerte
 * del WebView y avisa al P4 con un error VISIBLE en vez de dejar la
 * pantalla congelada. Ver docs/FLEX-PHONE.md, "pruebas pendientes".
 */
@SuppressLint("SetJavaScriptEnabled", "ViewConstructor")
class RelayTab(
    ctx: Context,
    val id: Int,
    private var viewportW: Int,
    private var viewportH: Int,
    private val onState: (RelayTab) -> Unit,
) {
    companion object {
        private const val TAG = "FlexPhone/Relay"

        /**
         * Esquemas PERMITIDOS. Todo lo demas se bloquea.
         *
         * `file:` y `content:` estan fuera a proposito: dejarlos
         * abriria los ficheros privados del telefono a cualquiera que
         * controle el enlace. `javascript:` tambien, porque un
         * `javascript:` inyectado desde el enlace se ejecutaria en el
         * contexto de la pagina actual.
         */
        private val ALLOWED_SCHEMES = setOf("http", "https", "about")
    }

    val webView: WebView = WebView(ctx)

    @Volatile var title: String = ""
        private set
    @Volatile var url: String = ""
        private set
    @Volatile var loading: Boolean = false
        private set
    @Volatile var progress: Int = 0
        private set
    @Volatile var dirty: Boolean = true
        private set
    @Volatile var lastError: String? = null
        private set

    /** Bitmap REUTILIZADO. No se crea uno por frame. */
    private var bitmap: Bitmap? = null
    private val main = Handler(Looper.getMainLooper())

    init {
        main.post { configure() }
    }

    private fun configure() {
        webView.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            loadWithOverviewMode = true
            useWideViewPort = true
            // Las cookies y el almacenamiento viven en el directorio
            // PRIVADO de la app. No se exportan, no se envian por el
            // enlace y no se escriben en ningun log.
            databaseEnabled = true
            // Acceso a ficheros locales DESACTIVADO: es lo que impide
            // que una pagina (o una URL inyectada) lea el
            // almacenamiento del telefono.
            allowFileAccess = false
            allowContentAccess = false
            @Suppress("DEPRECATION")
            allowFileAccessFromFileURLs = false
            @Suppress("DEPRECATION")
            allowUniversalAccessFromFileURLs = false
            mediaPlaybackRequiresUserGesture = true
            userAgentString = userAgentString + " FlexPhone/1.0"
        }
        webView.webViewClient = client
        webView.webChromeClient = object : android.webkit.WebChromeClient() {
            override fun onProgressChanged(view: WebView?, newProgress: Int) {
                progress = newProgress
                loading = newProgress < 100
                dirty = true
                onState(this@RelayTab)
            }
            override fun onReceivedTitle(view: WebView?, t: String?) {
                title = t.orEmpty(); onState(this@RelayTab)
            }
        }
        // El WebView se mide al tamano del viewport del P4 aunque no
        // este en pantalla: sin esto, draw() saldria vacio.
        webView.layout(0, 0, viewportW, viewportH)
    }

    private val client = object : WebViewClient() {
        override fun shouldOverrideUrlLoading(view: WebView?, req: WebResourceRequest?): Boolean {
            val u = req?.url ?: return true
            // Se BLOQUEA lo que no este en la lista blanca. Devolver
            // true significa "no lo cargues".
            val ok = u.scheme?.lowercase() in ALLOWED_SCHEMES
            if (!ok) Log.w(TAG, "esquema bloqueado: ${u.scheme}")   // sin la URL completa
            return !ok
        }
        override fun onPageStarted(view: WebView?, u: String?, favicon: Bitmap?) {
            url = u.orEmpty(); loading = true; progress = 0; lastError = null
            dirty = true; onState(this@RelayTab)
        }
        override fun onPageFinished(view: WebView?, u: String?) {
            url = u.orEmpty(); loading = false; progress = 100
            dirty = true; onState(this@RelayTab)
        }
        override fun onReceivedError(
            view: WebView?, req: WebResourceRequest?, err: android.webkit.WebResourceError?,
        ) {
            if (req?.isForMainFrame != true) return
            // El error del navegador se propaga al P4 tal cual: es lo
            // que permite que Flex OS diga "no se pudo cargar" en vez
            // de quedarse con la pagina anterior en pantalla.
            lastError = err?.description?.toString() ?: "no se pudo cargar la pagina"
            loading = false; dirty = true; onState(this@RelayTab)
        }
        override fun onRenderProcessGone(
            view: WebView?, detail: android.webkit.RenderProcessGoneDetail?,
        ): Boolean {
            // El proceso de render murio (memoria, casi siempre). Se
            // marca y se devuelve true: si se devolviera false, Android
            // MATARIA LA APP ENTERA. Asi Flex Phone sobrevive y el
            // usuario ve el error.
            lastError = "el navegador del telefono se quedo sin memoria"
            loading = false; dirty = true
            Log.w(TAG, "render process gone (pestana $id)")
            onState(this@RelayTab)
            return true
        }
    }

    fun navigate(target: String) {
        val scheme = runCatching { android.net.Uri.parse(target).scheme?.lowercase() }.getOrNull()
        if (scheme != null && scheme !in ALLOWED_SCHEMES) {
            lastError = "esquema no permitido"
            onState(this); return
        }
        main.post { webView.loadUrl(target) }
    }

    fun back() = main.post { if (webView.canGoBack()) webView.goBack() }
    fun forward() = main.post { if (webView.canGoForward()) webView.goForward() }
    fun reload() = main.post { webView.reload() }
    fun stopLoading() = main.post { webView.stopLoading() }

    fun canGoBack(): Boolean = webView.canGoBack()
    fun canGoForward(): Boolean = webView.canGoForward()

    fun resize(w: Int, h: Int) {
        if (w == viewportW && h == viewportH) return
        viewportW = w; viewportH = h
        main.post {
            webView.layout(0, 0, w, h)
            // El bitmap viejo ya no sirve: se suelta para que el
            // siguiente se cree al tamano nuevo.
            bitmap?.recycle(); bitmap = null
            dirty = true
        }
    }

    // -----------------------------------------------------------
    //  Entrada
    // -----------------------------------------------------------
    fun pointer(action: Int, x: Int, y: Int) = main.post {
        val now = android.os.SystemClock.uptimeMillis()
        val motion = when (action) {
            com.flexos.flexphone.protocol.Fbp.Pointer.DOWN -> MotionEvent.ACTION_DOWN
            com.flexos.flexphone.protocol.Fbp.Pointer.UP -> MotionEvent.ACTION_UP
            com.flexos.flexphone.protocol.Fbp.Pointer.MOVE -> MotionEvent.ACTION_MOVE
            com.flexos.flexphone.protocol.Fbp.Pointer.CANCEL -> MotionEvent.ACTION_CANCEL
            com.flexos.flexphone.protocol.Fbp.Pointer.TAP -> {
                // Un tap son dos eventos: sin el UP, la pagina se queda
                // con el dedo "apoyado" y no dispara el click.
                send(MotionEvent.ACTION_DOWN, now, x, y)
                send(MotionEvent.ACTION_UP, now + 40, x, y)
                dirty = true
                return@post
            }
            else -> return@post
        }
        send(motion, now, x, y)
        dirty = true
    }

    private fun send(action: Int, time: Long, x: Int, y: Int) {
        val ev = MotionEvent.obtain(time, time, action, x.toFloat(), y.toFloat(), 0)
        runCatching { webView.dispatchTouchEvent(ev) }
        ev.recycle()
    }

    fun scroll(dx: Int, dy: Int) = main.post {
        webView.scrollBy(dx, dy)
        dirty = true
    }

    fun key(action: Int, code: Int, text: String) = main.post {
        val P = com.flexos.flexphone.protocol.Fbp.Key
        val K = com.flexos.flexphone.protocol.Fbp.KeyCode
        if (action == P.TEXT && text.isNotEmpty()) {
            // El texto se inserta en el campo con foco. Se escapa como
            // literal JS para que un texto con comillas no se convierta
            // en codigo -- el teclado del P4 puede mandar cualquier cosa.
            val js = """
                (function(t){
                  var e=document.activeElement;
                  if(!e) return;
                  if(e.isContentEditable){ document.execCommand('insertText',false,t); return; }
                  if(e.value===undefined) return;
                  var s=e.selectionStart||e.value.length, n=e.selectionEnd||s;
                  e.value=e.value.slice(0,s)+t+e.value.slice(n);
                  e.selectionStart=e.selectionEnd=s+t.length;
                  e.dispatchEvent(new Event('input',{bubbles:true}));
                })(${jsString(text)});
            """.trimIndent()
            webView.evaluateJavascript(js, null)
            dirty = true
            return@post
        }
        val androidKey = when (code) {
            K.ENTER -> android.view.KeyEvent.KEYCODE_ENTER
            K.BACKSPACE -> android.view.KeyEvent.KEYCODE_DEL
            K.TAB -> android.view.KeyEvent.KEYCODE_TAB
            K.ESC -> android.view.KeyEvent.KEYCODE_ESCAPE
            K.LEFT -> android.view.KeyEvent.KEYCODE_DPAD_LEFT
            K.UP -> android.view.KeyEvent.KEYCODE_DPAD_UP
            K.RIGHT -> android.view.KeyEvent.KEYCODE_DPAD_RIGHT
            K.DOWN -> android.view.KeyEvent.KEYCODE_DPAD_DOWN
            else -> return@post
        }
        val a = if (action == P.RELEASE) android.view.KeyEvent.ACTION_UP
                else android.view.KeyEvent.ACTION_DOWN
        runCatching { webView.dispatchKeyEvent(android.view.KeyEvent(a, androidKey)) }
        dirty = true
    }

    /** Literal JS seguro: comillas, barras y control escapados. */
    private fun jsString(s: String): String {
        val sb = StringBuilder("\"")
        for (c in s) when {
            c == '"' -> sb.append("\\\"")
            c == '\\' -> sb.append("\\\\")
            c == '\n' -> sb.append("\\n")
            c == '\r' -> sb.append("\\r")
            c.code < 0x20 -> sb.append("\\u%04x".format(c.code))
            else -> sb.append(c)
        }
        return sb.append('"').toString()
    }

    // -----------------------------------------------------------
    //  Captura
    // -----------------------------------------------------------
    /**
     * Dibuja el WebView y devuelve un JPEG, o null si no hay nada
     * nuevo que mandar.
     *
     * El bitmap se REUTILIZA entre fotogramas: crear uno de 480x800
     * por frame serian ~1,5 MB de basura por captura, y el recolector
     * acabaria provocando justo el `onRenderProcessGone` que se
     * intenta evitar.
     */
    fun capture(quality: Int, scalePct: Int, force: Boolean): ByteArray? {
        if (!dirty && !force) return null
        val w = (viewportW * scalePct / 100).coerceAtLeast(1)
        val h = (viewportH * scalePct / 100).coerceAtLeast(1)

        var bmp = bitmap
        if (bmp == null || bmp.width != w || bmp.height != h) {
            bmp?.recycle()
            bmp = runCatching { Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565) }.getOrNull()
                ?: return null          // sin memoria: se salta este frame, no se cae
            bitmap = bmp
        }

        val done = java.util.concurrent.CountDownLatch(1)
        var ok = false
        main.post {
            runCatching {
                val c = Canvas(bmp)
                if (scalePct != 100) c.scale(scalePct / 100f, scalePct / 100f)
                // draw() sobre un canvas propio: no depende del
                // compositor de pantalla, que es lo que se apaga con
                // la pantalla.
                webView.draw(c)
                ok = true
            }
            done.countDown()
        }
        // Espera ACOTADA: si el hilo principal esta atascado, se
        // pierde un frame y ya. Nunca se bloquea el servidor.
        if (!done.await(400, java.util.concurrent.TimeUnit.MILLISECONDS) || !ok) return null

        val out = ByteArrayOutputStream(64 * 1024)
        // JPEG baseline: es el unico formato que el decodificador del
        // P4 sabe leer (FlexOS_JPEG.cpp).
        if (!bmp.compress(Bitmap.CompressFormat.JPEG, quality.coerceIn(20, 90), out)) return null
        dirty = false
        return out.toByteArray()
    }

    fun markDirty() { dirty = true }

    fun destroy() {
        main.post {
            runCatching {
                webView.stopLoading()
                webView.loadUrl("about:blank")
                webView.removeAllViews()
                webView.destroy()
            }
            bitmap?.recycle(); bitmap = null
        }
    }
}
