package com.flexos.flexphone.flexcloud

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import com.flexos.flexphone.cloud.AttachClient

/**
 * ACTIVAR FLEX CLOUD EN ESTE TELEFONO (enlace `flexstorage://attach`).
 *
 * La web de Flex OS (abierta EN este telefono con el QR) pide al P4 una oferta
 * de un solo uso y abre este enlace con la direccion del P4 y la oferta. Aqui:
 *
 *  1. Se pregunta a la persona, con palabras claras, que va a pasar: el Flex
 *     OS de esa direccion podra guardar archivos en un espacio de este
 *     telefono (la cuota que elija, 5 GB de serie) y nada mas.
 *  2. Si acepta, se arranca el servidor de Flex Cloud (escucha solo en la
 *     Wi-Fi) y se empareja con [AttachClient]: aparece el CODIGO DE 6 CIFRAS,
 *     el mismo que sale en la pantalla de Flex OS, donde hay que aceptarlo.
 *  3. Solo cuando Flex OS lo aprueba Y demuestra tener la misma clave se
 *     guarda el emparejamiento (Keystore). Si algo falla, no queda nada.
 *
 * Interfaz con vistas del sistema (sin dependencias): es un dialogo corto.
 */
class StorageAttachActivity : Activity() {

    private enum class Step { CONFIRM, WORKING, CODE, DONE, FAILED }

    @Volatile private var cancelled = false
    private var host: String? = null
    private var offer: String? = null
    private var hadPairing = false

    private lateinit var title: TextView
    private lateinit var body: TextView
    private lateinit var code: TextView
    private lateinit var progress: ProgressBar
    private lateinit var primary: Button
    private lateinit var secondary: Button

    private val bg = Color.rgb(11, 16, 32)
    private val card = Color.rgb(24, 31, 54)
    private val txt = Color.rgb(238, 242, 251)
    private val txt2 = Color.rgb(168, 179, 204)
    private val accent = Color.rgb(79, 125, 255)
    private val bad = Color.rgb(255, 107, 107)
    private val ok = Color.rgb(57, 201, 138)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildUi()
        val data: Uri? = intent?.data
        host = data?.getQueryParameter("h")
        offer = data?.getQueryParameter("o")
        if (data?.scheme != "flexstorage" || data.host != "attach" ||
            AttachClient.parseHost(host) == null || !AttachClient.validOffer(offer)) {
            show(Step.FAILED, "Enlace no válido",
                "Este enlace de Flex Storage no es válido o no es de tu red local. Vuelve a pulsar «Activar Flex Cloud» en la web de Flex OS.")
            return
        }
        hadPairing = FlexCloudPhone.repo(this).isPaired()
        val gb = FlexCloudPhone.quotaGb(this)
        show(Step.CONFIRM, "¿Usar este teléfono como Flex Cloud?",
            "El Flex OS de ${AttachClient.parseHost(host)!!.first} podrá guardar archivos en un espacio de este teléfono " +
                "(hasta $gb GB, lo puedes cambiar en Flex Phone) y leerlos cuando se lo pidas.\n\n" +
                "No verá nada más del teléfono: ni tus fotos, ni tus archivos, ni tus apps. Solo funciona en tu Wi‑Fi.")
    }

    private fun dp(v: Int): Int = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v.toFloat(), resources.displayMetrics).toInt()

    private fun round(color: Int, radiusDp: Int): GradientDrawable =
        GradientDrawable().apply { setColor(color); cornerRadius = dp(radiusDp).toFloat() }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(bg)
            setPadding(dp(20), dp(48), dp(20), dp(24))
        }
        val box = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            background = round(card, 24)
            setPadding(dp(22), dp(24), dp(22), dp(22))
        }
        title = TextView(this).apply {
            setTextColor(txt); setTextSize(TypedValue.COMPLEX_UNIT_SP, 22f); typeface = Typeface.DEFAULT_BOLD
        }
        body = TextView(this).apply {
            setTextColor(txt2); setTextSize(TypedValue.COMPLEX_UNIT_SP, 15f)
            setLineSpacing(0f, 1.2f); setPadding(0, dp(12), 0, 0)
        }
        code = TextView(this).apply {
            setTextColor(txt); setTextSize(TypedValue.COMPLEX_UNIT_SP, 44f)
            typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
            gravity = Gravity.CENTER; letterSpacing = 0.08f
            background = round(bg, 18)
            setPadding(0, dp(14), 0, dp(14))
            visibility = View.GONE
        }
        progress = ProgressBar(this).apply { isIndeterminate = true; visibility = View.GONE }
        primary = Button(this).apply {
            isAllCaps = false; setTextColor(Color.WHITE); setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            background = round(accent, 16)
        }
        secondary = Button(this).apply {
            isAllCaps = false; setTextColor(txt); setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            background = round(Color.rgb(40, 48, 74), 16)
        }
        val lp = { top: Int -> LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(top) } }
        box.addView(title)
        box.addView(body)
        box.addView(code, lp(18))
        box.addView(progress, lp(18))
        box.addView(primary, lp(22).apply { height = dp(52) })
        box.addView(secondary, lp(10).apply { height = dp(52) })
        root.addView(box)
        setContentView(ScrollView(this).apply { setBackgroundColor(bg); addView(root) })
    }

    private fun show(step: Step, t: String, b: String, sas: String? = null) {
        title.text = t
        body.text = b
        code.visibility = if (sas != null) View.VISIBLE else View.GONE
        if (sas != null) code.text = if (sas.length == 6) sas.substring(0, 3) + " " + sas.substring(3) else sas
        progress.visibility = if (step == Step.WORKING || step == Step.CODE) View.VISIBLE else View.GONE
        title.setTextColor(when (step) { Step.FAILED -> bad; Step.DONE -> ok; else -> txt })
        when (step) {
            Step.CONFIRM -> {
                primary.visibility = View.VISIBLE; primary.text = "Activar Flex Cloud"
                primary.setOnClickListener { begin() }
                secondary.visibility = View.VISIBLE; secondary.text = "Cancelar"
                secondary.setOnClickListener { finish() }
            }
            Step.WORKING, Step.CODE -> {
                primary.visibility = View.GONE
                secondary.visibility = View.VISIBLE; secondary.text = "Cancelar"
                secondary.setOnClickListener { cancelled = true; finish() }
            }
            Step.DONE, Step.FAILED -> {
                primary.visibility = View.VISIBLE; primary.text = "Cerrar"
                primary.setOnClickListener { finish() }
                secondary.visibility = View.GONE
            }
        }
    }

    private fun begin() {
        show(Step.WORKING, "Conectando con Flex OS…", "Preparando Flex Cloud en este teléfono.")
        FlexCloudPhone.setEnabled(this, true)
        FlexStorageService.start(this)
        val app = applicationContext
        Thread({
            // El servidor tiene que estar escuchando: Flex OS abrira sesion con
            // el en cuanto se apruebe el emparejamiento.
            var port = 0
            for (i in 0 until 40) {
                port = FlexCloudPhone.port()
                if (port > 0 || cancelled) break
                Thread.sleep(150)
            }
            if (cancelled) { undo(app); return@Thread }
            if (port <= 0) {
                val why = FlexCloudPhone.status.value.error ?: "No se pudo arrancar Flex Cloud en este teléfono."
                undo(app)
                runOnUiThread { show(Step.FAILED, "No se pudo activar", why) }
                return@Thread
            }
            val r = try {
                AttachClient(FlexCloudPhone.phoneInfo(app), port, FlexCloudPhone.repo(app)).attach(host!!, offer!!, onSas = { sas, p4 ->
                    runOnUiThread {
                        if (!isFinishing) show(Step.CODE, "Comprueba el código",
                            "En la pantalla de $p4 aparece este mismo código. Si coincide, pulsa «Emparejar» allí.", sas)
                    }
                }, cancelled = { cancelled })
            } catch (e: Exception) {
                AttachClient.Result.Failed("No se pudo guardar el emparejamiento en este teléfono.")
            }
            FlexCloudPhone.refresh(app)
            when (r) {
                is AttachClient.Result.Paired -> runOnUiThread {
                    show(Step.DONE, "Flex Cloud activado",
                        "${r.p4Name} ya puede guardar archivos aquí. Lo verás en la web de Flex OS y en Flex Phone › Flex Cloud.")
                }
                is AttachClient.Result.Failed -> {
                    undo(app)
                    runOnUiThread { if (!isFinishing) show(Step.FAILED, "No se activó", r.message) }
                }
            }
        }, "flexcloud-attach").start()
    }

    /** Si no habia emparejamiento antes y no se consiguio, no se deja un servidor escuchando. */
    private fun undo(app: android.content.Context) {
        if (!hadPairing && !FlexCloudPhone.repo(app).isPaired()) {
            FlexCloudPhone.setEnabled(app, false)
            FlexStorageService.stop(app)
        }
    }

    override fun onDestroy() {
        cancelled = true
        super.onDestroy()
    }
}
