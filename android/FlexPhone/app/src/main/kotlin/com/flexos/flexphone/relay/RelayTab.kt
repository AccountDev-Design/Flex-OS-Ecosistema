package com.flexos.flexphone.relay

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.InputDevice
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
 *
 * EL VIEWPORT -- CSS PX, NO PIXELES DEL TELEFONO
 * ---------------------------------------------
 * El P4 pide un viewport de W x H y trata cada pixel suyo como UN px
 * CSS (es lo que hace el servicio de Ubuntu/PC: deviceScaleFactor 1).
 * Un WebView, en cambio, mide su viewport CSS como
 * (pixeles de la vista) / densidad del telefono. Antes se maquetaba a
 * W x H pixeles del TELEFONO: con una densidad de 2,75 la pagina veia
 * un movil de 175 px CSS de ancho y todo salia ampliado (el "zoom
 * raro" de m.youtube.com). Ahora la vista mide W*d x H*d pixeles --
 * viewport CSS de W x H EXACTOS -- y la captura se dibuja con escala
 * 1/d sobre un bitmap de W x H: un px CSS acaba en un pixel del P4,
 * igual que con el otro servicio. El toque y el desplazamiento llegan
 * del P4 en px CSS y se pasan a pixeles de la vista con la misma d.
 *
 * EL DESPLAZAMIENTO DE LA PAGINA
 * ------------------------------
 * El scroll de la pagina ES el scroll de la vista (scrollX/scrollY del
 * WebView), y draw() pinta el contenido desplazado esa cantidad: cuenta
 * con que quien lo dibuja traslade el lienzo, que es lo que hace el
 * padre en una jerarquia normal. Aqui lo dibujamos nosotros, asi que la
 * captura traslada (-scrollX, -scrollY). Sin eso, al bajar la pagina la
 * superficie entera se movia y quedaba una franja en blanco.
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
    private val res = ctx.resources

    /** El proceso de render murio: esta pestana hay que rehacerla (ver RelayEngine). */
    @Volatile var renderGone: Boolean = false
        private set

    /** Inicio del gesto en curso: MOVE y UP llevan el MISMO downTime que su DOWN. */
    private var downTime = 0L

    /**
     * Pixeles de la vista por px CSS. Se lee cada vez: si la persona cambia el
     * tamano de pantalla de Android con el relay en marcha, la siguiente
     * maqueta ya usa la densidad nueva.
     */
    private fun density(): Float = res.displayMetrics.density.coerceAtLeast(0.5f)

    /** Pixeles de la vista para un viewport CSS de w x h. */
    private fun viewPx(cssPx: Int): Int = Math.round(cssPx * density()).coerceAtLeast(1)

    init {
        main.post { configure() }
    }

    /**
     * Mide y coloca el WebView para un viewport CSS de [w] x [h]. Medir antes de
     * colocar es lo que hace un padre de verdad: sin measure(), algunas versiones
     * del WebView se quedan con la medida anterior y la pagina no se re-maqueta.
     */
    private fun layoutFor(w: Int, h: Int) {
        val pw = viewPx(w)
        val ph = viewPx(h)
        webView.measure(
            View.MeasureSpec.makeMeasureSpec(pw, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(ph, View.MeasureSpec.EXACTLY),
        )
        webView.layout(0, 0, pw, ph)
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
        // este en pantalla: sin esto, draw() saldria vacio. En px CSS
        // (ver EL VIEWPORT, arriba).
        layoutFor(viewportW, viewportH)
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
            renderGone = true          // RelayEngine la rehace en la misma direccion
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

    /**
     * Nuevo viewport (ventana de DeX redimensionada, pantalla completa, giro).
     * Se re-maqueta la MISMA pagina: ni se recarga ni se recrea el WebView, asi
     * que el desplazamiento, los formularios y el historial siguen ahi.
     */
    fun resize(w: Int, h: Int) {
        if (w == viewportW && h == viewportH) return
        viewportW = w; viewportH = h
        main.post {
            layoutFor(w, h)
            dirty = true
        }
    }

    /** Ultima direccion conocida (para rehacer la pestana si su render murio). */
    fun lastUrl(): String = url

    // -----------------------------------------------------------
    //  Entrada
    // -----------------------------------------------------------
    // El P4 manda el punto en px CSS del viewport; la vista mide en pixeles
    // del telefono (ver EL VIEWPORT). Mismo factor que la maqueta.
    fun pointer(action: Int, x: Int, y: Int) = main.post {
        val now = android.os.SystemClock.uptimeMillis()
        val d = density()
        val fx = x.coerceIn(0, viewportW - 1) * d
        val fy = y.coerceIn(0, viewportH - 1) * d
        val motion = when (action) {
            com.flexos.flexphone.protocol.Fbp.Pointer.DOWN -> { downTime = now; MotionEvent.ACTION_DOWN }
            com.flexos.flexphone.protocol.Fbp.Pointer.UP -> MotionEvent.ACTION_UP
            com.flexos.flexphone.protocol.Fbp.Pointer.MOVE -> MotionEvent.ACTION_MOVE
            com.flexos.flexphone.protocol.Fbp.Pointer.CANCEL -> MotionEvent.ACTION_CANCEL
            com.flexos.flexphone.protocol.Fbp.Pointer.TAP -> {
                // Un tap son dos eventos: sin el UP, la pagina se queda
                // con el dedo "apoyado" y no dispara el click.
                send(MotionEvent.ACTION_DOWN, now, now, fx, fy)
                send(MotionEvent.ACTION_UP, now, now + 40, fx, fy)
                dirty = true
                return@post
            }
            else -> return@post
        }
        // Un MOVE/UP sin DOWN previo (el DOWN se perdio) arranca su propio gesto.
        if (downTime == 0L) downTime = now
        send(motion, downTime, now, fx, fy)
        if (motion == MotionEvent.ACTION_UP || motion == MotionEvent.ACTION_CANCEL) downTime = 0L
        dirty = true
    }

    private fun send(action: Int, down: Long, time: Long, x: Float, y: Float) {
        val ev = MotionEvent.obtain(down, time, action, x, y, 0)
        ev.source = InputDevice.SOURCE_TOUCHSCREEN
        runCatching { webView.dispatchTouchEvent(ev) }
        ev.recycle()
    }

    /** Desplazamiento en px CSS (como el `mouse.wheel` del otro servicio). */
    fun scroll(dx: Int, dy: Int) = main.post {
        val d = density()
        webView.scrollBy(Math.round(dx * d), Math.round(dy * d))
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
     * Un fotograma: el JPEG y el viewport (px CSS) que CUBRE. El rectangulo
     * del FRAME se manda con ESTAS medidas, no con las del motor en el momento
     * de enviar: si el P4 cambio de tamano entre la captura y el envio, el
     * fotograma sigue describiendo exactamente lo que se capturo.
     */
    class Capture(val jpeg: ByteArray, val viewW: Int, val viewH: Int)

    /**
     * Dibuja el WebView y devuelve un JPEG, o null si no hay nada
     * nuevo que mandar.
     *
     * El bitmap se REUTILIZA entre fotogramas: crear uno de 480x800
     * por frame serian ~1,5 MB de basura por captura, y el recolector
     * acabaria provocando justo el `onRenderProcessGone` que se
     * intenta evitar.
     */
    fun capture(quality: Int, scalePct: Int, force: Boolean): Capture? {
        if (!dirty && !force) return null
        if (renderGone) return null              // un WebView muerto no pinta nada
        // TODO lo que define el fotograma se lee y se usa en el hilo principal,
        // junto con el dibujo: un resize() que llegue mientras tanto ya no
        // puede mezclar un tamano con la maqueta del otro.
        val done = java.util.concurrent.CountDownLatch(1)
        var shot: Bitmap? = null
        var vw = 0; var vh = 0
        main.post {
            runCatching {
                vw = viewportW; vh = viewportH
                val w = (vw * scalePct / 100).coerceAtLeast(1)
                val h = (vh * scalePct / 100).coerceAtLeast(1)
                var bmp = bitmap
                if (bmp == null || bmp.width != w || bmp.height != h) {
                    // El anterior se suelta sin recycle(): ver destroy().
                    bmp = Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565)
                    bitmap = bmp
                }
                val b = bmp ?: return@runCatching
                // "Limpio" ANTES de dibujar: si la pagina cambia mientras este
                // fotograma se comprime, el cambio vuelve a marcarla y sale en
                // el siguiente (antes se marcaba limpia DESPUES y se perdia).
                dirty = false
                // El bitmap se reutiliza: lo que la pagina no pinte (fondo
                // transparente, un borde) no puede ensenar el fotograma anterior.
                b.eraseColor(Color.WHITE)
                val c = Canvas(b)
                // px de la vista -> px CSS -> escala de envio: una sola matriz.
                val k = (scalePct / 100f) / density()
                c.scale(k, k)
                // El contenido se dibuja desplazado lo que va la pagina: el
                // lienzo se traslada lo contrario (ver EL DESPLAZAMIENTO).
                c.translate(-webView.scrollX.toFloat(), -webView.scrollY.toFloat())
                // draw() sobre un canvas propio: no depende del
                // compositor de pantalla, que es lo que se apaga con
                // la pantalla.
                webView.draw(c)
                shot = b
            }
            done.countDown()
        }
        // Espera ACOTADA: si el hilo principal esta atascado, se
        // pierde un frame y ya. Nunca se bloquea el servidor.
        if (!done.await(400, java.util.concurrent.TimeUnit.MILLISECONDS)) { dirty = true; return null }
        val bmp = shot ?: run { dirty = true; return null }   // sin memoria: se salta este frame, no se cae

        val out = ByteArrayOutputStream(64 * 1024)
        // JPEG baseline: es el unico formato que el decodificador del
        // P4 sabe leer (FlexOS_JPEG.cpp). Acotado: esto corre en el hilo del
        // bombeo, y una excepcion sin capturar en un hilo propio TUMBA la app.
        val ok = runCatching { bmp.compress(Bitmap.CompressFormat.JPEG, quality.coerceIn(20, 90), out) }
            .getOrDefault(false)
        if (!ok) { dirty = true; return null }      // se reintenta en la vuelta siguiente
        return Capture(out.toByteArray(), vw, vh)
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
            // Sin recycle(): el hilo del bombeo podria estar comprimiendo este
            // mismo bitmap ahora mismo. Soltar la referencia basta; el
            // recolector lo libera cuando nadie lo use.
            bitmap = null
        }
    }
}
